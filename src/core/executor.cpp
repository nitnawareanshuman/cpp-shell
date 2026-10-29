#include "shell/executor.hpp"
#include "shell/expander.hpp"
#include "shell/parser.hpp"
#include "shell/path.hpp"
#include "shell/unique_fd.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

namespace shell {
namespace {

struct ExpandedRedirect {
  RedirectKind kind = RedirectKind::Out;
  std::string target;
};

struct ExpandedCommand {
  std::vector<std::string> argv;
  std::vector<ExpandedRedirect> redirs;
};

int last_status_from_wait(int status) {
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  if (WIFSTOPPED(status)) {
    return 128 + WSTOPSIG(status);
  }
  return 1;
}

void write_fd(int fd, std::string_view text) {
  while (!text.empty()) {
    const ssize_t n = ::write(fd, text.data(), text.size());
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    text.remove_prefix(static_cast<std::size_t>(n));
  }
}

std::string read_fd_all(int fd) {
  std::string out;
  char buffer[4096];
  while (true) {
    const ssize_t n = ::read(fd, buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (n == 0) {
      break;
    }
    out.append(buffer, static_cast<std::size_t>(n));
  }
  return out;
}

int open_redir_file(const ExpandedRedirect &redir, std::string &error) {
  int flags = 0;
  switch (redir.kind) {
  case RedirectKind::In:
    flags = O_RDONLY;
    break;
  case RedirectKind::Out:
  case RedirectKind::Err:
    flags = O_WRONLY | O_CREAT | O_TRUNC;
    break;
  case RedirectKind::Append:
  case RedirectKind::ErrAppend:
    flags = O_WRONLY | O_CREAT | O_APPEND;
    break;
  case RedirectKind::ErrToOut:
    return -1;
  }
  const int fd = ::open(redir.target.c_str(), flags, 0644);
  if (fd < 0) {
    error = redir.target + ": " + std::strerror(errno);
  }
  return fd;
}

bool apply_redirections(const std::vector<ExpandedRedirect> &redirs,
                        std::string &error) {
  for (const auto &redir : redirs) {
    if (redir.kind == RedirectKind::ErrToOut) {
      if (::dup2(STDOUT_FILENO, STDERR_FILENO) < 0) {
        error = std::string("2>&1: ") + std::strerror(errno);
        return false;
      }
      continue;
    }
    const int fd = open_redir_file(redir, error);
    if (fd < 0) {
      return false;
    }
    int target = STDOUT_FILENO;
    if (redir.kind == RedirectKind::In) {
      target = STDIN_FILENO;
    } else if (redir.kind == RedirectKind::Err ||
               redir.kind == RedirectKind::ErrAppend) {
      target = STDERR_FILENO;
    }
    if (::dup2(fd, target) < 0) {
      error = std::string("dup2: ") + std::strerror(errno);
      ::close(fd);
      return false;
    }
    ::close(fd);
  }
  return true;
}

struct SavedStdio {
  UniqueFd in;
  UniqueFd out;
  UniqueFd err;
};

bool save_stdio(SavedStdio &saved) {
  saved.in.reset(::dup(STDIN_FILENO));
  saved.out.reset(::dup(STDOUT_FILENO));
  saved.err.reset(::dup(STDERR_FILENO));
  return saved.in.valid() && saved.out.valid() && saved.err.valid();
}

void restore_stdio(SavedStdio &saved) {
  if (saved.in.valid()) {
    ::dup2(saved.in.get(), STDIN_FILENO);
  }
  if (saved.out.valid()) {
    ::dup2(saved.out.get(), STDOUT_FILENO);
  }
  if (saved.err.valid()) {
    ::dup2(saved.err.get(), STDERR_FILENO);
  }
}

bool expand_pipeline(const Pipeline &pipeline, ShellState &state,
                     std::vector<ExpandedCommand> &out, std::string &error) {
  for (const auto &command : pipeline.commands) {
    ExpandedCommand expanded;
    for (const auto &word : command.words) {
      auto fields = expand_word(word, state);
      if (!fields.error.empty()) {
        error = fields.error;
        return false;
      }
      expanded.argv.insert(expanded.argv.end(), fields.fields.begin(),
                           fields.fields.end());
    }
    for (const auto &redir : command.redirections) {
      ExpandedRedirect item;
      item.kind = redir.kind;
      if (redir.kind != RedirectKind::ErrToOut) {
        item.target = expand_word_unsplit(redir.target, state);
        if (item.target.empty()) {
          error = "Missing redirection target";
          return false;
        }
      }
      expanded.redirs.push_back(std::move(item));
    }
    out.push_back(std::move(expanded));
  }
  return true;
}

std::string join_argv(const std::vector<std::string> &argv) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < argv.size(); ++i) {
    if (i != 0) {
      oss << ' ';
    }
    oss << argv[i];
  }
  return oss.str();
}

int parse_job_id(const std::string &text, bool &ok) {
  ok = false;
  if (text.empty()) {
    return 0;
  }
  std::string body = text;
  if (body[0] == '%') {
    body.erase(0, 1);
  }
  try {
    std::size_t idx = 0;
    const int value = std::stoi(body, &idx);
    ok = idx == body.size();
    return value;
  } catch (...) {
    return 0;
  }
}

int run_builtin(const std::vector<std::string> &argv, ShellState &state,
                const ExecOptions &options, ExecResult &result) {
  const std::string &command = argv[0];

  if (command == "exit") {
    if (argv.size() > 2) {
      write_fd(STDERR_FILENO, "exit: too many arguments\n");
      return 1;
    }
    int code = state.last_status();
    if (argv.size() == 2) {
      try {
        code = std::stoi(argv[1]);
      } catch (...) {
        write_fd(STDERR_FILENO, "exit: numeric argument required\n");
        return 1;
      }
    }
    result.should_exit = true;
    result.exit_status = code;
    return code;
  }

  if (command == "echo") {
    std::string line;
    for (std::size_t i = 1; i < argv.size(); ++i) {
      if (i > 1) {
        line += ' ';
      }
      line += argv[i];
    }
    line += '\n';
    write_fd(STDOUT_FILENO, line);
    return 0;
  }

  if (command == "pwd") {
    if (argv.size() > 1) {
      write_fd(STDERR_FILENO, "pwd: too many arguments\n");
      return 1;
    }
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    if (ec) {
      write_fd(STDERR_FILENO, "pwd: " + ec.message() + "\n");
      return 1;
    }
    write_fd(STDOUT_FILENO, cwd.string() + "\n");
    return 0;
  }

  if (command == "cd") {
    if (argv.size() > 2) {
      write_fd(STDERR_FILENO, "cd: too many arguments\n");
      return 1;
    }
    std::string path;
    if (argv.size() == 1 || argv[1] == "~") {
      path = state.home_directory();
      if (path.empty()) {
        write_fd(STDERR_FILENO, "cd: HOME not set\n");
        return 1;
      }
    } else {
      path = argv[1];
    }
    if (::chdir(path.c_str()) != 0) {
      write_fd(STDERR_FILENO,
               "cd: " + path + ": " + std::strerror(errno) + "\n");
      return 1;
    }
    return 0;
  }

  if (command == "type") {
    if (argv.size() != 2) {
      write_fd(STDERR_FILENO, "type: expected one argument\n");
      return 1;
    }
    const std::string &target = argv[1];
    if (state.is_builtin(target)) {
      write_fd(STDOUT_FILENO, target + " is a shell builtin\n");
      return 0;
    }
    std::string executable;
    if (find_executable(target, executable)) {
      write_fd(STDOUT_FILENO, target + " is " + executable + "\n");
      return 0;
    }
    write_fd(STDERR_FILENO, target + ": not found\n");
    return 1;
  }

  if (command == "export") {
    if (argv.size() == 1) {
      for (const auto &[key, value] : state.environ_map()) {
        write_fd(STDOUT_FILENO, key + "=" + value + "\n");
      }
      return 0;
    }
    for (std::size_t i = 1; i < argv.size(); ++i) {
      const auto eq = argv[i].find('=');
      if (eq == std::string::npos) {
        continue;
      }
      if (!state.setenv_var(argv[i].substr(0, eq), argv[i].substr(eq + 1))) {
        write_fd(STDERR_FILENO, "export: failed to set " + argv[i] + "\n");
        return 1;
      }
    }
    state.reload_ifs();
    return 0;
  }

  if (command == "history") {
    const auto &entries = state.history().entries();
    for (std::size_t i = 0; i < entries.size(); ++i) {
      write_fd(STDOUT_FILENO,
               std::to_string(i + 1) + "  " + entries[i] + "\n");
    }
    return 0;
  }

  if (command == "jobs") {
    state.jobs().reap();
    for (const auto &line : state.jobs().format_list()) {
      write_fd(STDOUT_FILENO, line + "\n");
    }
    return 0;
  }

  if (command == "fg" || command == "bg") {
    state.jobs().reap();
    Job *job = nullptr;
    if (argv.size() == 1) {
      job = state.jobs().current();
    } else if (argv.size() == 2) {
      bool ok = false;
      const int id = parse_job_id(argv[1], ok);
      if (!ok) {
        write_fd(STDERR_FILENO, command + ": invalid job id\n");
        return 1;
      }
      job = state.jobs().find(id);
    } else {
      write_fd(STDERR_FILENO, command + ": too many arguments\n");
      return 1;
    }
    if (job == nullptr || job->state == JobState::Done) {
      write_fd(STDERR_FILENO, command + ": no current job\n");
      return 1;
    }
    if (::kill(-job->pgid, SIGCONT) < 0 && errno != ESRCH) {
      write_fd(STDERR_FILENO, command + ": " + std::strerror(errno) + "\n");
      return 1;
    }
    job->state = JobState::Running;
    if (command == "bg") {
      write_fd(STDOUT_FILENO,
               "[" + std::to_string(job->id) + "]  " + job->command + " &\n");
      return 0;
    }

    if (options.interactive_tty && ::isatty(STDIN_FILENO)) {
      const pid_t shell_pgid = ::getpgrp();
      const auto previous = ::signal(SIGTTOU, SIG_IGN);
      ::tcsetpgrp(STDIN_FILENO, job->pgid);
      ::signal(SIGTTOU, previous);
      int status = 0;
      pid_t waited = 0;
      while ((waited = ::waitpid(-job->pgid, &status, WUNTRACED)) > 0) {
        if (WIFSTOPPED(status)) {
          job->state = JobState::Stopped;
          break;
        }
        job->exit_status = last_status_from_wait(status);
        job->state = JobState::Done;
      }
      if (waited < 0 && errno == ECHILD) {
        job->state = JobState::Done;
      }
      const auto restore_sig = ::signal(SIGTTOU, SIG_IGN);
      ::tcsetpgrp(STDIN_FILENO, shell_pgid);
      ::signal(SIGTTOU, restore_sig);
      return job->exit_status;
    }

    int status = 0;
    while (::waitpid(-job->pgid, &status, 0) > 0) {
      job->exit_status = last_status_from_wait(status);
    }
    job->state = JobState::Done;
    return job->exit_status;
  }

  if (command == "help") {
    write_fd(STDOUT_FILENO,
             "Built-ins: bg cd echo exit export fg help history jobs pwd type\n"
             "Syntax: pipelines (|), redirection (< > >> 2> 2>> 2>&1), "
             "background (&), $VAR ${VAR} $?\n");
    return 0;
  }

  write_fd(STDERR_FILENO, command + ": not a builtin\n");
  return 127;
}

bool is_parent_builtin(const std::string &name) {
  return name == "cd" || name == "exit" || name == "export" || name == "fg" ||
         name == "bg" || name == "jobs" || name == "history";
}

void exec_child(const ExpandedCommand &command) {
  if (command.argv.empty()) {
    _exit(0);
  }
  std::vector<char *> args;
  args.reserve(command.argv.size() + 1);
  for (const auto &arg : command.argv) {
    args.push_back(const_cast<char *>(arg.c_str()));
  }
  args.push_back(nullptr);

  if (command.argv[0].find('/') != std::string::npos) {
    ::execv(command.argv[0].c_str(), args.data());
  } else {
    ::execvp(command.argv[0].c_str(), args.data());
  }
  const std::string message =
      command.argv[0] + ": " + std::strerror(errno) + "\n";
  write_fd(STDERR_FILENO, message);
  _exit(127);
}

void give_terminal(pid_t pgid) {
  if (!::isatty(STDIN_FILENO)) {
    return;
  }
  const auto previous = ::signal(SIGTTOU, SIG_IGN);
  ::tcsetpgrp(STDIN_FILENO, pgid);
  ::signal(SIGTTOU, previous);
}

void read_nonblocking(int fd, std::string &out) {
  if (fd < 0) {
    return;
  }
  char buffer[4096];
  while (true) {
    const ssize_t n = ::read(fd, buffer, sizeof(buffer));
    if (n > 0) {
      out.append(buffer, static_cast<std::size_t>(n));
      continue;
    }
    break;
  }
}

int wait_pipeline(pid_t pgid, bool interactive, JobTable *jobs,
                  const std::string &command_text, int capture_out,
                  int capture_err, std::string &captured_out,
                  std::string &captured_err) {
  const pid_t shell_pgid = ::getpgrp();
  if (interactive) {
    give_terminal(pgid);
  }

  auto make_nonblocking = [](int fd) {
    if (fd < 0) {
      return;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
      ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
  };
  make_nonblocking(capture_out);
  make_nonblocking(capture_err);

  int last = 0;
  while (true) {
    read_nonblocking(capture_out, captured_out);
    read_nonblocking(capture_err, captured_err);

    int status = 0;
    const pid_t pid = ::waitpid(-pgid, &status, WNOHANG | WUNTRACED);
    if (pid > 0) {
      last = last_status_from_wait(status);
      if (WIFSTOPPED(status)) {
        if (jobs != nullptr) {
          jobs->add(pgid, command_text, JobState::Stopped);
        }
        break;
      }
      continue;
    }
    if (pid < 0) {
      if (errno == ECHILD) {
        break;
      }
      break;
    }

    pollfd fds[2]{};
    nfds_t count = 0;
    if (capture_out >= 0) {
      fds[count].fd = capture_out;
      fds[count].events = POLLIN;
      ++count;
    }
    if (capture_err >= 0) {
      fds[count].fd = capture_err;
      fds[count].events = POLLIN;
      ++count;
    }
    ::poll(fds, count, 20);
  }

  read_nonblocking(capture_out, captured_out);
  read_nonblocking(capture_err, captured_err);

  if (interactive) {
    give_terminal(shell_pgid);
  }
  return last;
}

} // namespace

Shell::Shell() = default;

ExecResult Shell::execute(const std::string &input, const ExecOptions &options) {
  ExecResult result;
  for (const auto &notice : state_.jobs().reap()) {
    result.error += notice + "\n";
  }
  state_.jobs().remove_done();

  const ParseResult parsed = parse(input);
  if (!parsed.error.empty()) {
    result.error += parsed.error + "\n";
    result.exit_status = 2;
    state_.set_last_status(2);
    state_.history().add(input);
    return result;
  }
  if (parsed.empty) {
    return result;
  }

  std::vector<ExpandedCommand> commands;
  std::string expand_error;
  if (!expand_pipeline(parsed.pipeline, state_, commands, expand_error)) {
    result.error += expand_error + "\n";
    result.exit_status = 1;
    state_.set_last_status(1);
    state_.history().add(input);
    return result;
  }

  if (commands.size() == 1 && commands[0].argv.empty() &&
      commands[0].redirs.empty()) {
    return result;
  }

  UniqueFd capture_out_r;
  UniqueFd capture_out_w;
  UniqueFd capture_err_r;
  UniqueFd capture_err_w;
  SavedStdio saved;
  bool saved_ok = false;

  if (options.capture_output) {
    if (!make_pipe(capture_out_r, capture_out_w) ||
        !make_pipe(capture_err_r, capture_err_w)) {
      result.error += "failed to create capture pipes\n";
      result.exit_status = 1;
      state_.set_last_status(1);
      return result;
    }
    saved_ok = save_stdio(saved);
    ::dup2(capture_out_w.get(), STDOUT_FILENO);
    ::dup2(capture_err_w.get(), STDERR_FILENO);
  }

  auto restore_parent_stdio = [&]() {
    if (saved_ok) {
      restore_stdio(saved);
      saved_ok = false;
    }
  };

  auto finish_capture_reads = [&]() {
    if (!options.capture_output) {
      return;
    }
    capture_out_w.reset();
    capture_err_w.reset();
    restore_parent_stdio();
    if (capture_out_r.valid()) {
      result.output += read_fd_all(capture_out_r.get());
    }
    if (capture_err_r.valid()) {
      result.error += read_fd_all(capture_err_r.get());
    }
  };

  const bool background = parsed.pipeline.background;
  const bool single_parent_builtin =
      commands.size() == 1 && !commands[0].argv.empty() &&
      state_.is_builtin(commands[0].argv[0]) &&
      is_parent_builtin(commands[0].argv[0]) && !background;

  if (commands.size() == 1 && !commands[0].argv.empty() &&
      state_.is_builtin(commands[0].argv[0]) && !background) {
    SavedStdio local;
    const bool local_saved = save_stdio(local);
    std::string redir_error;
    if (!apply_redirections(commands[0].redirs, redir_error)) {
      if (local_saved) {
        restore_stdio(local);
      }
      finish_capture_reads();
      result.error += redir_error + "\n";
      result.exit_status = 1;
      state_.set_last_status(1);
      state_.history().add(input);
      return result;
    }
    result.exit_status = run_builtin(commands[0].argv, state_, options, result);
    if (local_saved) {
      restore_stdio(local);
    }
    finish_capture_reads();
    state_.set_last_status(result.exit_status);
    state_.history().add(input);
    return result;
  }
  (void)single_parent_builtin;

  const int n = static_cast<int>(commands.size());
  std::vector<UniqueFd> pipe_read(std::max(0, n - 1));
  std::vector<UniqueFd> pipe_write(std::max(0, n - 1));
  for (int i = 0; i < n - 1; ++i) {
    if (!make_pipe(pipe_read[i], pipe_write[i])) {
      finish_capture_reads();
      result.error += "pipe: " + std::string(std::strerror(errno)) + "\n";
      result.exit_status = 1;
      state_.set_last_status(1);
      return result;
    }
  }

  pid_t pgid = 0;
  std::vector<pid_t> children;
  bool fork_failed = false;
  std::string fork_error;

  for (int i = 0; i < n; ++i) {
    const pid_t pid = ::fork();
    if (pid < 0) {
      fork_failed = true;
      fork_error = std::strerror(errno);
      break;
    }
    if (pid == 0) {
      if (pgid == 0) {
        pgid = ::getpid();
      }
      ::setpgid(0, pgid);

      if (i > 0) {
        ::dup2(pipe_read[i - 1].get(), STDIN_FILENO);
      }
      if (i < n - 1) {
        ::dup2(pipe_write[i].get(), STDOUT_FILENO);
      }

      for (int p = 0; p < n - 1; ++p) {
        pipe_read[p].reset();
        pipe_write[p].reset();
      }
      capture_out_r.reset();
      capture_out_w.reset();
      capture_err_r.reset();
      capture_err_w.reset();

      if (background && options.capture_output) {
        const int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
          bool stdout_redirected = false;
          bool stderr_redirected = false;
          bool stdin_redirected = false;
          for (const auto &redir : commands[i].redirs) {
            stdout_redirected |= redir.kind == RedirectKind::Out ||
                                 redir.kind == RedirectKind::Append;
            stderr_redirected |= redir.kind == RedirectKind::Err ||
                                 redir.kind == RedirectKind::ErrAppend ||
                                 redir.kind == RedirectKind::ErrToOut;
            stdin_redirected |= redir.kind == RedirectKind::In;
          }
          if (!stdin_redirected && i == 0) {
            ::dup2(devnull, STDIN_FILENO);
          }
          if (!stdout_redirected && i == n - 1) {
            ::dup2(devnull, STDOUT_FILENO);
          }
          if (!stderr_redirected) {
            ::dup2(devnull, STDERR_FILENO);
          }
          ::close(devnull);
        }
      }

      std::string redir_error;
      if (!apply_redirections(commands[i].redirs, redir_error)) {
        write_fd(STDERR_FILENO, redir_error + "\n");
        _exit(1);
      }

      if (!commands[i].argv.empty() && state_.is_builtin(commands[i].argv[0])) {
        ExecResult child_result;
        const int code =
            run_builtin(commands[i].argv, state_, options, child_result);
        _exit(code);
      }
      exec_child(commands[i]);
    }

    children.push_back(pid);
    if (pgid == 0) {
      pgid = pid;
    }
    ::setpgid(pid, pgid);
  }

  for (int p = 0; p < n - 1; ++p) {
    pipe_read[p].reset();
    pipe_write[p].reset();
  }

  if (fork_failed) {
    for (pid_t child : children) {
      ::kill(child, SIGTERM);
      ::waitpid(child, nullptr, 0);
    }
    finish_capture_reads();
    result.error += std::string("fork: ") + fork_error + "\n";
    result.exit_status = 1;
    state_.set_last_status(1);
    state_.history().add(input);
    return result;
  }

  restore_parent_stdio();
  capture_out_w.reset();
  capture_err_w.reset();

  if (background) {
    const auto job = state_.jobs().add(pgid, input, JobState::Running);
    result.output +=
        "[" + std::to_string(job.id) + "] " + std::to_string(pgid) + "\n";
    result.exit_status = 0;
    state_.set_last_status(0);
    state_.history().add(input);
    return result;
  }

  std::string captured_out;
  std::string captured_err;
  result.exit_status = wait_pipeline(
      pgid, options.interactive_tty && !options.capture_output, &state_.jobs(),
      input, capture_out_r.valid() ? capture_out_r.get() : -1,
      capture_err_r.valid() ? capture_err_r.get() : -1, captured_out,
      captured_err);
  result.output += captured_out;
  result.error += captured_err;
  state_.set_last_status(result.exit_status);
  state_.history().add(input);
  return result;
}

} // namespace shell

#include "check.hpp"
#include "shell/completion.hpp"
#include "shell/executor.hpp"
#include "shell/history.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

fs::path make_temp_dir() {
  std::string tmpl =
      (fs::temp_directory_path() / "cpp-shell-it-XXXXXX").string();
  char *made = ::mkdtemp(tmpl.data());
  return made ? fs::path(made) : fs::path();
}

std::string slurp(const fs::path &p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

shell::ExecResult run(shell::Shell &sh, const std::string &line) {
  if (std::getenv("IT_TRACE")) std::cerr << "RUN: " << line << "\n";
  shell::ExecOptions opts;
  opts.capture_output = true;
  return sh.execute(line, opts);
}

void test_builtins(shell::Shell &sh, const fs::path &dir) {
  auto r = run(sh, "echo hello   world");
  CHECK_EQ(r.output, std::string("hello world\n"));
  r = run(sh, "echo 'a   b' \"c   d\" e\\ f");
  CHECK_EQ(r.output, std::string("a   b c   d e f\n"));
  r = run(sh, "pwd");
  CHECK_EQ(r.output, fs::canonical(dir).string() + "\n");
  r = run(sh, "type echo");
  CHECK(r.output.find("builtin") != std::string::npos);
  r = run(sh, "type definitely_not_a_command_xyz");
  CHECK(r.exit_status != 0 || !r.error.empty() || !r.output.empty());
  r = run(sh, "cd /nonexistent_dir_xyz");
  CHECK(r.exit_status != 0);
  r = run(sh, "exit");
  CHECK(r.should_exit);
}

void test_redirection(shell::Shell &sh, const fs::path &dir) {
  run(sh, "echo one > out.txt");
  CHECK_EQ(slurp(dir / "out.txt"), std::string("one\n"));
  run(sh, "echo two >> out.txt");
  CHECK_EQ(slurp(dir / "out.txt"), std::string("one\ntwo\n"));
  run(sh, "echo three > out.txt");
  CHECK_EQ(slurp(dir / "out.txt"), std::string("three\n"));

  run(sh, "ls /nonexistent_dir_xyz 2> err.txt");
  CHECK(!slurp(dir / "err.txt").empty());
  run(sh, "ls /nonexistent_dir_xyz 2>> err.txt");
  CHECK(slurp(dir / "err.txt").size() > 0);

  auto r = run(sh, "ls /nonexistent_dir_xyz > o.txt 2> e.txt");
  CHECK(slurp(dir / "o.txt").empty());
  CHECK(!slurp(dir / "e.txt").empty());
  CHECK(r.exit_status != 0);

  r = run(sh, "ls /nonexistent_dir_xyz 2>&1");
  CHECK(!(r.output + r.error).empty());

  run(sh, "ls /nonexistent_dir_xyz > both.txt 2>&1");
  CHECK(!slurp(dir / "both.txt").empty());

  run(sh, "echo from_input > in.txt");
  r = run(sh, "cat < in.txt");
  CHECK_EQ(r.output, std::string("from_input\n"));

  r = run(sh, "cat < missing_input.txt");
  CHECK(r.exit_status != 0);

  run(sh, "echo spaced > 'name with spaces.txt'");
  CHECK_EQ(slurp(dir / "name with spaces.txt"), std::string("spaced\n"));
}

void test_pipelines(shell::Shell &sh, const fs::path &dir) {
  auto r = run(sh, "echo hello | cat");
  CHECK_EQ(r.output, std::string("hello\n"));
  r = run(sh, "printf 'b\\na\\nc\\n' | sort | head -n 2");
  CHECK_EQ(r.output, std::string("a\nb\n"));
  r = run(sh, "echo abc | cat | cat | cat");
  CHECK_EQ(r.output, std::string("abc\n"));

  run(sh, "echo piped | cat > pipe_out.txt");
  CHECK_EQ(slurp(dir / "pipe_out.txt"), std::string("piped\n"));

  r = run(sh, "false | true");
  CHECK_EQ(r.exit_status, 0);
  r = run(sh, "true | false");
  CHECK_EQ(r.exit_status, 1);
  r = run(sh, "no_such_cmd_xyz | cat");
  CHECK_EQ(r.exit_status, 0);
  r = run(sh, "echo x | no_such_cmd_xyz");
  CHECK_EQ(r.exit_status, 127);

  // Large input must not deadlock.
  {
    std::ofstream big(dir / "big.txt");
    for (int i = 0; i < 200000; ++i) big << "line " << i << "\n";
  }
  r = run(sh, "cat big.txt | cat | wc -l");
  CHECK(r.output.find("200000") != std::string::npos);
}

void test_expansion(shell::Shell &sh, const fs::path &) {
  ::setenv("CPPSH_TEST_VAR", "value", 1);
  auto r = run(sh, "echo $CPPSH_TEST_VAR ${CPPSH_TEST_VAR}x");
  CHECK_EQ(r.output, std::string("value valuex\n"));
  r = run(sh, "echo '$CPPSH_TEST_VAR'");
  CHECK_EQ(r.output, std::string("$CPPSH_TEST_VAR\n"));
  r = run(sh, "echo \"$CPPSH_TEST_VAR\"");
  CHECK_EQ(r.output, std::string("value\n"));
  run(sh, "false");
  r = run(sh, "echo $?");
  CHECK_EQ(r.output, std::string("1\n"));
  r = run(sh, "echo ~");
  CHECK(!r.output.empty() && r.output[0] == '/');
  r = run(sh, "echo \"~\"");
  CHECK_EQ(r.output, std::string("~\n"));
  ::setenv("CPPSH_SPLIT", "a b  c", 1);
  r = run(sh, "echo $CPPSH_SPLIT");
  CHECK_EQ(r.output, std::string("a b c\n"));
  r = run(sh, "echo \"$CPPSH_SPLIT\"");
  CHECK_EQ(r.output, std::string("a b  c\n"));
  r = run(sh, "echo $CPPSH_UNSET_VAR_XYZ end");
  CHECK_EQ(r.output, std::string("end\n"));
}

void test_syntax_errors(shell::Shell &sh) {
  auto r = run(sh, "echo 'unclosed");
  CHECK(r.exit_status != 0);
  CHECK(!r.error.empty());
  r = run(sh, "echo hi |");
  CHECK(r.exit_status != 0);
  r = run(sh, "echo hi >");
  CHECK(r.exit_status != 0);
  r = run(sh, "| cat");
  CHECK(r.exit_status != 0);
}

void test_background(shell::Shell &sh) {
  auto r = run(sh, "sleep 0.2 &");
  CHECK_EQ(r.exit_status, 0);
  r = run(sh, "jobs");
  CHECK(r.output.find("sleep") != std::string::npos);
  ::usleep(600 * 1000);
  r = run(sh, "jobs");
  CHECK(r.output.find("Running") == std::string::npos);
  r = run(sh, "true");
  CHECK_EQ(r.exit_status, 0);

  // No leftover children after jobs are reaped.
  run(sh, "jobs");
  const pid_t reaped = ::waitpid(-1, nullptr, WNOHANG);
  CHECK(reaped <= 0);
}

void test_not_found(shell::Shell &sh) {
  auto r = run(sh, "no_such_cmd_xyz");
  CHECK_EQ(r.exit_status, 127);
  CHECK(r.error.find("command not found") != std::string::npos);
}

void test_completion(shell::Shell &sh, const fs::path &dir) {
  fs::create_directories(dir / "cdir");
  std::ofstream(dir / "my file.txt") << "x";
  std::ofstream(dir / "my other.txt") << "x";
  auto res = sh.state().completer().complete("cat cdi", 7);
  CHECK(res.applied);
  CHECK_EQ(res.line, std::string("cat cdir/"));
  res = sh.state().completer().complete("cat my\\ f", 10);
  CHECK(res.applied);
  CHECK_EQ(res.line, std::string("cat 'my file.txt' "));
  res = sh.state().completer().complete("cat my", 6);
  CHECK(res.matches.size() == 2);
  res = sh.state().completer().complete("ech", 3);
  CHECK(res.applied);
  CHECK_EQ(res.line, std::string("echo "));
}

void test_history(const fs::path &dir) {
  const fs::path hist = dir / "hist_it";
  {
    std::ofstream(hist) << "old command\n";
  }
  ::setenv("CPP_SHELL_HISTFILE", hist.c_str(), 1);
  {
    shell::Shell sh;
    CHECK_EQ(sh.state().history().path(), hist.string());
    run(sh, "echo first");
    run(sh, "echo first");
    run(sh, "   ");
  }
  const std::string text = slurp(hist);
  CHECK(text.find("old command") != std::string::npos);
  CHECK(text.find("echo first") != std::string::npos);
}

void test_cli_binary(const fs::path &dir) {
#ifdef CPP_SHELL_BIN
  const std::string cmd = std::string("cd '") + dir.string() + "' && '" +
                          CPP_SHELL_BIN + "' -c 'echo cli_ok | cat'";
  FILE *p = ::popen(cmd.c_str(), "r");
  CHECK(p != nullptr);
  if (p) {
    char buf[64] = {0};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, p);
    buf[n] = 0;
    ::pclose(p);
    CHECK_EQ(std::string(buf), std::string("cli_ok\n"));
  }
#else
  (void)dir;
#endif
}

} // namespace

int main() {
  const fs::path dir = make_temp_dir();
  if (dir.empty()) {
    std::cerr << "cannot create temp dir\n";
    return 1;
  }
  const fs::path home = dir / "home";
  fs::create_directories(home);
  ::setenv("HOME", home.c_str(), 1);
  ::setenv("CPP_SHELL_HISTFILE", (dir / "hist").c_str(), 1);

  const fs::path work = dir / "work";
  fs::create_directories(work);
  fs::current_path(work);

  {
    shell::Shell sh;
    test_builtins(sh, work);
    test_redirection(sh, work);
    test_pipelines(sh, work);
    test_expansion(sh, work);
    test_syntax_errors(sh);
    test_background(sh);
    test_not_found(sh);
    test_completion(sh, work);
    test_history(work);
  }
  test_cli_binary(work);

  fs::current_path(fs::temp_directory_path());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return test_summary();
}

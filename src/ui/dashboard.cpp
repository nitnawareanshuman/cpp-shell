#include "shell/dashboard.hpp"
#include "shell/completion.hpp"
#include "shell/executor.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

namespace shell {
namespace {

std::string now_string() {
  const auto now = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now());
  char buffer[16];
  std::strftime(buffer, sizeof(buffer), "%H:%M:%S", std::localtime(&now));
  return buffer;
}

std::vector<std::string> wrap_log(const std::vector<std::string> &lines,
                                  std::size_t keep) {
  if (lines.size() <= keep) {
    return lines;
  }
  return {lines.end() - static_cast<std::ptrdiff_t>(keep), lines.end()};
}

} // namespace

int run_dashboard(Shell &shell) {
  using namespace ftxui;

  std::string input;
  int cursor = 0;
  std::vector<std::string> log{"cpp-shell dashboard",
                               "Tab completes, Up/Down history, Ctrl-D exits."};
  std::string status = "ready";
  std::string completions_status;
  bool running = true;

  ExecOptions options;
  options.capture_output = true;
  options.interactive_tty = false;

  const std::string load_error = shell.state().load_completions();
  if (!load_error.empty()) {
    log.push_back(load_error);
  }

  auto append_log = [&](const std::string &text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
      log.push_back(line);
    }
    log = wrap_log(log, 400);
  };

  auto run_line = [&](const std::string &command) {
    if (command.empty()) {
      return;
    }
    append_log("$ " + command);
    status = "running";
    const ExecResult result = shell.execute(command, options);
    if (!result.output.empty()) {
      append_log(result.output);
    }
    if (!result.error.empty()) {
      append_log(result.error);
    }
    status = "exit " + std::to_string(result.exit_status);
    if (result.should_exit) {
      running = false;
    }
    for (const auto &notice : shell.state().jobs().reap()) {
      append_log(notice);
    }
  };

  InputOption option = InputOption::Default();
  option.multiline = false;
  option.cursor_position = &cursor;
  option.on_enter = [&] {
    const std::string command = input;
    input.clear();
    cursor = 0;
    completions_status.clear();
    shell.state().history().reset_cursor();
    run_line(command);
  };

  Component input_box = Input(&input, "command", option);

  auto screen = ScreenInteractive::Fullscreen();

  input_box |= CatchEvent([&](Event event) {
    if (event == Event::Escape) {
      input.clear();
      cursor = 0;
      return true;
    }
    if (event == Event::ArrowUp) {
      input = shell.state().history().up(input);
      cursor = static_cast<int>(input.size());
      return true;
    }
    if (event == Event::ArrowDown) {
      input = shell.state().history().down(input);
      cursor = static_cast<int>(input.size());
      return true;
    }
    if (event == Event::Tab) {
      auto completed = shell.state().completer().complete(input, cursor);
      if (completed.applied) {
        input = completed.line;
        cursor = completed.cursor;
        completions_status.clear();
      } else {
        completions_status = completed.display.empty()
                                 ? "no completions"
                                 : completed.display;
      }
      return true;
    }
    if (event == Event::Character('\x04')) {
      running = false;
      screen.Exit();
      return true;
    }
    if (event == Event::Character('\x03')) {
      input.clear();
      cursor = 0;
      status = "interrupted";
      return true;
    }
    if (event == Event::Character('\x0c')) {
      log.clear();
      return true;
    }
    return false;
  });

  auto renderer = Renderer(input_box, [&] {
    shell.state().jobs().reap();
    if (!running) {
      screen.Exit();
    }

    Elements output_elements;
    for (const auto &line : wrap_log(log, 80)) {
      output_elements.push_back(text(line));
    }
    if (output_elements.empty()) {
      output_elements.push_back(text(""));
    }

    Elements job_elements;
    const auto jobs = shell.state().jobs().format_list();
    if (jobs.empty()) {
      job_elements.push_back(text("No jobs") | dim);
    } else {
      for (const auto &job : jobs) {
        job_elements.push_back(text(job));
      }
    }

    return vbox({
               hbox({text(" cpp-shell ") | bold | inverted,
                     text("  " + shell.state().cwd()) | color(Color::Cyan),
                     filler(),
                     text(" " + status + "  " + now_string() + " ") | dim}),
               separator(),
               hbox({
                   vbox({text("Output") | bold,
                         separator(),
                         vbox(std::move(output_elements)) | yframe |
                             vscroll_indicator | flex}) |
                       flex,
                   separator(),
                   vbox({text("Jobs") | bold, separator(),
                         vbox(std::move(job_elements)) | yframe | flex}) |
                       size(WIDTH, EQUAL, 36),
               }) |
                   flex,
               separator(),
               text(completions_status.empty()
                        ? "Tab: complete   Up/Down: history   Ctrl-L: clear   "
                          "Ctrl-D: exit"
                        : completions_status) |
                   dim,
               hbox({text("$ ") | color(Color::Green), input_box->Render()}) |
                   border,
           }) |
           border;
  });

  screen.Loop(renderer);
  return 0;
}

} // namespace shell

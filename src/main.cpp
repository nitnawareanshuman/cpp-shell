#include "shell/dashboard.hpp"
#include "shell/executor.hpp"
#include "shell/line_editor.hpp"

#include <unistd.h>

#include <iostream>
#include <string>

namespace {

void print_usage() {
  std::cout
      << "cpp-shell — a POSIX-inspired command shell with a Qt dashboard\n"
      << "\nUsage:\n"
      << "  cpp-shell              Start the interactive CLI\n"
      << "  cpp-shell --ui         Start the Qt dashboard\n"
      << "  cpp-shell --dashboard  Same as --ui\n"
      << "  cpp-shell --help       Show this help\n";
}

} // namespace

int main(int argc, char *argv[]) {
  shell::Shell sh;
  shell::ExecOptions options;
  options.interactive_tty = ::isatty(STDIN_FILENO) != 0;

  if (argc >= 2) {
    const std::string arg = argv[1];
    if (arg == "--help" || arg == "-h") {
      print_usage();
      return 0;
    }
    if (arg == "--ui" || arg == "--dashboard") {
      return shell::run_dashboard(sh);
    }
    if (arg == "--command" || arg == "-c") {
      if (argc < 3) {
        std::cerr << "cpp-shell: -c requires a command string\n";
        return 2;
      }
      options.capture_output = false;
      const auto result = sh.execute(argv[2], options);
      std::cout << result.output;
      std::cerr << result.error;
      return result.exit_status;
    }
    std::cerr << "cpp-shell: unknown argument " << arg << "\n";
    print_usage();
    return 2;
  }

  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  shell::LineEditor editor(sh);
  int exit_code = 0;
  while (true) {
    std::string input;
    bool eof = false;
    if (!editor.read_line("$ ", input, eof)) {
      break;
    }

    const auto result = sh.execute(input, options);
    if (!result.output.empty()) {
      std::cout << result.output;
    }
    if (!result.error.empty()) {
      std::cerr << result.error;
    }
    exit_code = result.exit_status;
    if (result.should_exit) {
      break;
    }
  }
  return exit_code;
}

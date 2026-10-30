#include "shell/line_editor.hpp"
#include "shell/completion.hpp"

#include <termios.h>
#include <unistd.h>

#include <iostream>
#include <optional>

namespace shell {
namespace {

class RawTerminal {
public:
  explicit RawTerminal(int fd) : fd_(fd) {
    if (::tcgetattr(fd_, &original_) == 0) {
      termios raw = original_;
      raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN));
      raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
      raw.c_cc[VMIN] = 1;
      raw.c_cc[VTIME] = 0;
      if (::tcsetattr(fd_, TCSAFLUSH, &raw) == 0) {
        active_ = true;
      }
    }
  }

  ~RawTerminal() {
    if (active_) {
      ::tcsetattr(fd_, TCSAFLUSH, &original_);
    }
  }

  bool active() const { return active_; }

private:
  int fd_ = STDIN_FILENO;
  termios original_{};
  bool active_ = false;
};

enum class Key {
  Char,
  Enter,
  Backspace,
  Tab,
  Up,
  Down,
  Left,
  Right,
  Home,
  End,
  CtrlC,
  CtrlD,
  CtrlL,
  CtrlU,
  Other
};

struct KeyEvent {
  Key key = Key::Other;
  char ch = 0;
};

KeyEvent read_key() {
  KeyEvent event;
  unsigned char ch = 0;
  const ssize_t n = ::read(STDIN_FILENO, &ch, 1);
  if (n <= 0) {
    event.key = Key::CtrlD;
    return event;
  }
  if (ch == 3) {
    event.key = Key::CtrlC;
    return event;
  }
  if (ch == 4) {
    event.key = Key::CtrlD;
    return event;
  }
  if (ch == 12) {
    event.key = Key::CtrlL;
    return event;
  }
  if (ch == 21) {
    event.key = Key::CtrlU;
    return event;
  }
  if (ch == '\t') {
    event.key = Key::Tab;
    return event;
  }
  if (ch == '\r' || ch == '\n') {
    event.key = Key::Enter;
    return event;
  }
  if (ch == 127 || ch == 8) {
    event.key = Key::Backspace;
    return event;
  }
  if (ch == 27) {
    unsigned char seq[2] = {0, 0};
    if (::read(STDIN_FILENO, &seq[0], 1) <= 0) {
      return event;
    }
    if (seq[0] == '[' && ::read(STDIN_FILENO, &seq[1], 1) > 0) {
      if (seq[1] == 'A') {
        event.key = Key::Up;
      } else if (seq[1] == 'B') {
        event.key = Key::Down;
      } else if (seq[1] == 'C') {
        event.key = Key::Right;
      } else if (seq[1] == 'D') {
        event.key = Key::Left;
      } else if (seq[1] == 'H') {
        event.key = Key::Home;
      } else if (seq[1] == 'F') {
        event.key = Key::End;
      }
    }
    return event;
  }
  if (ch >= 32) {
    event.key = Key::Char;
    event.ch = static_cast<char>(ch);
  }
  return event;
}

void draw_line(const std::string &prompt, const std::string &line, int cursor) {
  std::cout << "\r\33[K" << prompt << line;
  const int extra = static_cast<int>(line.size()) - cursor;
  if (extra > 0) {
    std::cout << "\33[" << extra << "D";
  }
  std::cout.flush();
}

} // namespace

LineEditor::LineEditor(Shell &shell) : shell_(shell) {}

bool LineEditor::read_line(const std::string &prompt, std::string &line,
                           bool &eof) {
  eof = false;
  line.clear();

  if (!::isatty(STDIN_FILENO)) {
    std::cout << prompt << std::flush;
    if (!std::getline(std::cin, line)) {
      eof = true;
      return false;
    }
    return true;
  }

  RawTerminal raw(STDIN_FILENO);
  if (!raw.active()) {
    std::cout << prompt << std::flush;
    if (!std::getline(std::cin, line)) {
      eof = true;
      return false;
    }
    return true;
  }

  int cursor = 0;
  std::optional<std::vector<std::string>> last_matches;
  std::cout << prompt << std::flush;

  while (true) {
    const KeyEvent event = read_key();
    if (event.key != Key::Tab) {
      last_matches.reset();
    }

    if (event.key == Key::Enter) {
      std::cout << "\r\n" << std::flush;
      shell_.state().history().reset_cursor();
      return true;
    }
    if (event.key == Key::CtrlD) {
      if (line.empty()) {
        eof = true;
        std::cout << "\r\n" << std::flush;
        return false;
      }
      continue;
    }
    if (event.key == Key::CtrlC) {
      line.clear();
      cursor = 0;
      std::cout << "^C\r\n" << prompt << std::flush;
      continue;
    }
    if (event.key == Key::CtrlU) {
      line.clear();
      cursor = 0;
      draw_line(prompt, line, cursor);
      continue;
    }
    if (event.key == Key::Backspace) {
      if (cursor > 0) {
        line.erase(static_cast<std::size_t>(cursor - 1), 1);
        --cursor;
        draw_line(prompt, line, cursor);
      }
      continue;
    }
    if (event.key == Key::Left) {
      if (cursor > 0) {
        --cursor;
        draw_line(prompt, line, cursor);
      }
      continue;
    }
    if (event.key == Key::Right) {
      if (cursor < static_cast<int>(line.size())) {
        ++cursor;
        draw_line(prompt, line, cursor);
      }
      continue;
    }
    if (event.key == Key::Home) {
      cursor = 0;
      draw_line(prompt, line, cursor);
      continue;
    }
    if (event.key == Key::End) {
      cursor = static_cast<int>(line.size());
      draw_line(prompt, line, cursor);
      continue;
    }
    if (event.key == Key::Up) {
      line = shell_.state().history().up(line);
      cursor = static_cast<int>(line.size());
      draw_line(prompt, line, cursor);
      continue;
    }
    if (event.key == Key::Down) {
      line = shell_.state().history().down(line);
      cursor = static_cast<int>(line.size());
      draw_line(prompt, line, cursor);
      continue;
    }
    if (event.key == Key::Tab) {
      auto completed = shell_.state().completer().complete(line, cursor);
      if (completed.applied) {
        line = completed.line;
        cursor = completed.cursor;
        draw_line(prompt, line, cursor);
      } else if (!completed.display.empty()) {
        std::cout << "\r\n" << completed.display << "\r\n";
        draw_line(prompt, line, cursor);
      }
      continue;
    }
    if (event.key == Key::Char) {
      line.insert(line.begin() + cursor, event.ch);
      ++cursor;
      draw_line(prompt, line, cursor);
    }
  }
}

} // namespace shell

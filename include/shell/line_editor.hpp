#pragma once

#include "shell/executor.hpp"

#include <string>

namespace shell {

class LineEditor {
public:
  explicit LineEditor(Shell &shell);

  bool read_line(const std::string &prompt, std::string &line, bool &eof);

private:
  Shell &shell_;
};

} // namespace shell

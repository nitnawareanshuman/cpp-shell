#pragma once

#include "shell/state.hpp"
#include "shell/types.hpp"

#include <string>

namespace shell {

class Shell {
public:
  Shell();

  ShellState &state() { return state_; }
  const ShellState &state() const { return state_; }

  ExecResult execute(const std::string &input, const ExecOptions &options = {});

private:
  ShellState state_;
};

} // namespace shell

#pragma once

#include "shell/types.hpp"

#include <string>
#include <vector>

namespace shell {

class ShellState;

struct ExpandResult {
  std::vector<std::string> fields;
  std::string error;
};

ExpandResult expand_word(const Word &word, const ShellState &state);
std::string expand_word_unsplit(const Word &word, const ShellState &state);

} // namespace shell

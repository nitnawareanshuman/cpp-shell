#pragma once

#include "shell/lexer.hpp"
#include "shell/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace shell {

struct ParseResult {
  Pipeline pipeline;
  std::string error;
  bool empty = false;
};

ParseResult parse(const std::string &input);
ParseResult parse_tokens(const std::vector<Token> &tokens);

} // namespace shell

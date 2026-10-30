#pragma once

#include "shell/types.hpp"

#include <string>
#include <variant>
#include <vector>

namespace shell {

enum class TokenKind {
  Word,
  Pipe,
  Ampersand,
  RedirectIn,
  RedirectOut,
  RedirectAppend,
  RedirectErr,
  RedirectErrAppend,
  RedirectErrToOut,
  End
};

struct Token {
  TokenKind kind = TokenKind::End;
  Word word;
};

struct LexResult {
  std::vector<Token> tokens;
  std::string error;
};

LexResult tokenize(const std::string &input);

std::string word_raw_text(const Word &word);

} // namespace shell

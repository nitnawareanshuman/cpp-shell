#pragma once

#include <string>
#include <vector>

namespace shell {

enum class QuoteKind { Unquoted, Single, Double };

struct WordPart {
  std::string text;
  QuoteKind quote = QuoteKind::Unquoted;
  bool literal = false;
};

struct Word {
  std::vector<WordPart> parts;
};

enum class RedirectKind {
  In,           // <
  Out,          // >
  Append,       // >>
  Err,          // 2>
  ErrAppend,    // 2>>
  ErrToOut      // 2>&1
};

struct Redirection {
  RedirectKind kind = RedirectKind::Out;
  Word target;
};

struct SimpleCommand {
  std::vector<Word> words;
  std::vector<Redirection> redirections;
  bool background = false;
};

struct Pipeline {
  std::vector<SimpleCommand> commands;
  bool background = false;
};

struct ParseError {
  std::string message;
};

struct ExecOptions {
  bool capture_output = false;
  bool interactive_tty = false;
};

struct ExecResult {
  std::string output;
  std::string error;
  int exit_status = 0;
  bool should_exit = false;
};

} // namespace shell

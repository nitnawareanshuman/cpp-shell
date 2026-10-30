#include "shell/parser.hpp"

namespace shell {
namespace {

bool is_redirect(TokenKind kind) {
  return kind == TokenKind::RedirectIn || kind == TokenKind::RedirectOut ||
         kind == TokenKind::RedirectAppend || kind == TokenKind::RedirectErr ||
         kind == TokenKind::RedirectErrAppend ||
         kind == TokenKind::RedirectErrToOut;
}

RedirectKind redirect_kind(TokenKind kind) {
  switch (kind) {
  case TokenKind::RedirectIn:
    return RedirectKind::In;
  case TokenKind::RedirectOut:
    return RedirectKind::Out;
  case TokenKind::RedirectAppend:
    return RedirectKind::Append;
  case TokenKind::RedirectErr:
    return RedirectKind::Err;
  case TokenKind::RedirectErrAppend:
    return RedirectKind::ErrAppend;
  case TokenKind::RedirectErrToOut:
    return RedirectKind::ErrToOut;
  default:
    return RedirectKind::Out;
  }
}

} // namespace

ParseResult parse_tokens(const std::vector<Token> &tokens) {
  ParseResult result;
  std::size_t i = 0;

  auto peek = [&]() -> const Token & { return tokens[i]; };

  auto skip_end_check = [&]() {
    return i >= tokens.size() || tokens[i].kind == TokenKind::End;
  };

  if (tokens.empty() ||
      (tokens.size() == 1 && tokens[0].kind == TokenKind::End)) {
    result.empty = true;
    return result;
  }

  if (peek().kind == TokenKind::Pipe || peek().kind == TokenKind::Ampersand) {
    result.error = "Unexpected token at start of command";
    return result;
  }

  SimpleCommand current;
  bool saw_command = false;

  auto finish_command = [&]() -> bool {
    if (current.words.empty() && current.redirections.empty()) {
      result.error = "Empty command in pipeline";
      return false;
    }
    result.pipeline.commands.push_back(std::move(current));
    current = SimpleCommand{};
    return true;
  };

  while (!skip_end_check()) {
    const Token &tok = peek();

    if (tok.kind == TokenKind::Word) {
      current.words.push_back(tok.word);
      saw_command = true;
      ++i;
      continue;
    }

    if (is_redirect(tok.kind)) {
      Redirection redir;
      redir.kind = redirect_kind(tok.kind);
      ++i;
      if (redir.kind != RedirectKind::ErrToOut) {
        if (skip_end_check() || peek().kind != TokenKind::Word) {
          result.error = "Missing redirection target";
          return result;
        }
        redir.target = peek().word;
        ++i;
      }
      current.redirections.push_back(std::move(redir));
      saw_command = true;
      continue;
    }

    if (tok.kind == TokenKind::Pipe) {
      if (!finish_command()) {
        return result;
      }
      ++i;
      if (skip_end_check() || peek().kind == TokenKind::Pipe ||
          peek().kind == TokenKind::Ampersand) {
        result.error = "Pipeline is missing a command";
        return result;
      }
      continue;
    }

    if (tok.kind == TokenKind::Ampersand) {
      ++i;
      if (!skip_end_check()) {
        result.error = "& is only supported at the end of a pipeline";
        return result;
      }
      result.pipeline.background = true;
      continue;
    }

    result.error = "Unexpected token";
    return result;
  }

  if (saw_command || !current.words.empty() || !current.redirections.empty()) {
    if (!finish_command()) {
      return result;
    }
  }

  if (result.pipeline.commands.empty()) {
    result.empty = true;
  }
  return result;
}

ParseResult parse(const std::string &input) {
  const LexResult lexed = tokenize(input);
  if (!lexed.error.empty()) {
    ParseResult result;
    result.error = lexed.error;
    return result;
  }
  return parse_tokens(lexed.tokens);
}

} // namespace shell

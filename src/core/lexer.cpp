#include "shell/lexer.hpp"

#include <cctype>

namespace shell {
namespace {

bool is_operator_start(char ch) {
  return ch == '|' || ch == '&' || ch == '<' || ch == '>';
}

} // namespace

std::string word_raw_text(const Word &word) {
  std::string out;
  for (const auto &part : word.parts) {
    out += part.text;
  }
  return out;
}

LexResult tokenize(const std::string &input) {
  LexResult result;
  Word current;
  std::string part;
  QuoteKind quote = QuoteKind::Unquoted;
  bool in_single = false;
  bool in_double = false;
  bool word_started = false;

  auto emit_word = [&]() {
    if (!word_started) {
      return;
    }
    if (!part.empty() || current.parts.empty()) {
      WordPart wp;
      wp.text = part;
      wp.quote = quote;
      wp.literal = false;
      if (in_single) {
        wp.quote = QuoteKind::Single;
      } else if (in_double) {
        wp.quote = QuoteKind::Double;
      }
      current.parts.push_back(std::move(wp));
      part.clear();
    }
    Token token;
    token.kind = TokenKind::Word;
    token.word = std::move(current);
    result.tokens.push_back(std::move(token));
    current = Word{};
    word_started = false;
    quote = QuoteKind::Unquoted;
  };

  auto emit_op = [&](TokenKind kind) {
    emit_word();
    Token token;
    token.kind = kind;
    result.tokens.push_back(std::move(token));
  };

  auto start_word_part = [&](QuoteKind q) {
    if (!part.empty() && quote != q) {
      WordPart wp;
      wp.text = part;
      wp.quote = quote;
      wp.literal = false;
      current.parts.push_back(std::move(wp));
      part.clear();
    }
    quote = q;
    word_started = true;
  };

  for (std::size_t i = 0; i < input.size(); ++i) {
    const char ch = input[i];

    if (in_single) {
      if (ch == '\'') {
        WordPart wp;
        wp.text = part;
        wp.quote = QuoteKind::Single;
        current.parts.push_back(std::move(wp));
        part.clear();
        in_single = false;
        quote = QuoteKind::Unquoted;
      } else {
        part += ch;
      }
      continue;
    }

    if (in_double) {
      if (ch == '\\' && i + 1 < input.size()) {
        const char next = input[i + 1];
        if (next == '"' || next == '\\') {
          part += next;
          ++i;
        } else {
          part += ch;
        }
        continue;
      }
      if (ch == '"') {
        WordPart wp;
        wp.text = part;
        wp.quote = QuoteKind::Double;
        current.parts.push_back(std::move(wp));
        part.clear();
        in_double = false;
        quote = QuoteKind::Unquoted;
      } else {
        part += ch;
      }
      continue;
    }

    if (ch == '\\') {
      start_word_part(QuoteKind::Unquoted);
      if (i + 1 < input.size()) {
        WordPart wp;
        wp.text = std::string(1, input[++i]);
        wp.quote = QuoteKind::Unquoted;
        wp.literal = true;
        if (!part.empty()) {
          WordPart before;
          before.text = part;
          before.quote = QuoteKind::Unquoted;
          current.parts.push_back(std::move(before));
          part.clear();
        }
        current.parts.push_back(std::move(wp));
      } else {
        part += ch;
      }
      continue;
    }

    if (ch == '\'') {
      if (!part.empty()) {
        WordPart wp;
        wp.text = part;
        wp.quote = QuoteKind::Unquoted;
        current.parts.push_back(std::move(wp));
        part.clear();
      }
      in_single = true;
      word_started = true;
      quote = QuoteKind::Single;
      continue;
    }

    if (ch == '"') {
      if (!part.empty()) {
        WordPart wp;
        wp.text = part;
        wp.quote = QuoteKind::Unquoted;
        current.parts.push_back(std::move(wp));
        part.clear();
      }
      in_double = true;
      word_started = true;
      quote = QuoteKind::Double;
      continue;
    }

    if (std::isspace(static_cast<unsigned char>(ch))) {
      emit_word();
      continue;
    }

    if (is_operator_start(ch)) {
      std::string digits;
      if (!part.empty()) {
        bool all_digits = true;
        for (char d : part) {
          if (!std::isdigit(static_cast<unsigned char>(d))) {
            all_digits = false;
            break;
          }
        }
        if (all_digits && (ch == '>' || ch == '<') && current.parts.empty()) {
          digits = part;
        }
      }

      if (ch == '|') {
        emit_op(TokenKind::Pipe);
        continue;
      }
      if (ch == '&') {
        emit_op(TokenKind::Ampersand);
        continue;
      }
      if (ch == '<') {
        if (digits == "2" || (!digits.empty() && digits != "0" && digits != "1" &&
                              digits != "2")) {
          // Only the documented 2> family is special for stderr; other
          // numeric prefixes still terminate the word and parse as stdin.
        }
        if (!digits.empty()) {
          part.clear();
          word_started = !current.parts.empty();
        }
        emit_op(TokenKind::RedirectIn);
        continue;
      }

      // '>' or '>>' or '2>' / '2>>' / '2>&1'
      if (digits == "2") {
        part.clear();
        word_started = !current.parts.empty();
        if (i + 2 < input.size() && input[i + 1] == '&' && input[i + 2] == '1') {
          emit_op(TokenKind::RedirectErrToOut);
          i += 2;
          continue;
        }
        if (i + 1 < input.size() && input[i + 1] == '>') {
          emit_op(TokenKind::RedirectErrAppend);
          ++i;
          continue;
        }
        emit_op(TokenKind::RedirectErr);
        continue;
      }

      if (!digits.empty()) {
        part.clear();
        word_started = !current.parts.empty();
      }
      if (i + 1 < input.size() && input[i + 1] == '>') {
        emit_op(TokenKind::RedirectAppend);
        ++i;
      } else {
        emit_op(TokenKind::RedirectOut);
      }
      continue;
    }

    start_word_part(QuoteKind::Unquoted);
    part += ch;
  }

  if (in_single) {
    result.error = "Unclosed single quote";
    return result;
  }
  if (in_double) {
    result.error = "Unclosed double quote";
    return result;
  }

  emit_word();
  Token end;
  end.kind = TokenKind::End;
  result.tokens.push_back(end);
  return result;
}

} // namespace shell

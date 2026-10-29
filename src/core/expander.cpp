#include "shell/expander.hpp"
#include "shell/state.hpp"

#include <cctype>

namespace shell {
namespace {

bool is_name_start(char ch) {
  return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

bool is_name_char(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

bool is_ifs(char ch, const std::string &ifs) {
  return ifs.find(ch) != std::string::npos;
}

std::string expand_parameters(const std::string &text, const ShellState &state) {
  std::string out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '$') {
      out += text[i];
      continue;
    }
    if (i + 1 >= text.size()) {
      out += '$';
      break;
    }
    const char next = text[i + 1];
    if (next == '?') {
      out += std::to_string(state.last_status());
      ++i;
      continue;
    }
    if (next == '{') {
      const auto close = text.find('}', i + 2);
      if (close == std::string::npos) {
        out.append(text.substr(i));
        break;
      }
      const std::string name = text.substr(i + 2, close - (i + 2));
      bool valid = !name.empty() && is_name_start(name[0]);
      for (char ch : name) {
        if (!is_name_char(ch)) {
          valid = false;
          break;
        }
      }
      if (valid) {
        out += state.getenv(name);
      } else {
        out.append(text.substr(i, close - i + 1));
      }
      i = close;
      continue;
    }
    if (is_name_start(next)) {
      std::size_t j = i + 1;
      while (j < text.size() && is_name_char(text[j])) {
        ++j;
      }
      out += state.getenv(text.substr(i + 1, j - i - 1));
      i = j - 1;
      continue;
    }
    out += '$';
  }
  return out;
}

struct Segment {
  std::string text;
  bool quoted = false;
};

} // namespace

ExpandResult expand_word(const Word &word, const ShellState &state) {
  ExpandResult result;
  std::vector<Segment> segments;
  bool any_quoted = false;

  for (std::size_t index = 0; index < word.parts.size(); ++index) {
    const WordPart &part = word.parts[index];
    Segment seg;
    if (part.quote == QuoteKind::Single || part.literal) {
      seg.text = part.text;
      seg.quoted = part.quote == QuoteKind::Single || part.literal;
      if (part.quote == QuoteKind::Single) {
        any_quoted = true;
      }
    } else if (part.quote == QuoteKind::Double) {
      seg.text = expand_parameters(part.text, state);
      seg.quoted = true;
      any_quoted = true;
    } else {
      std::string text = part.text;
      if (index == 0 && !part.literal && !text.empty() && text[0] == '~' &&
          (text.size() == 1 || text[1] == '/')) {
        text = state.home_directory() + text.substr(1);
      }
      seg.text = expand_parameters(text, state);
      seg.quoted = false;
    }
    segments.push_back(std::move(seg));
  }

  if (word.parts.empty()) {
    return result;
  }

  std::string current;
  bool started = false;
  const std::string &ifs = state.ifs();

  for (const Segment &seg : segments) {
    if (seg.quoted) {
      current += seg.text;
      started = true;
      continue;
    }
    for (char ch : seg.text) {
      if (is_ifs(ch, ifs)) {
        if (started) {
          result.fields.push_back(current);
          current.clear();
          started = false;
        }
      } else {
        current += ch;
        started = true;
      }
    }
  }

  if (started) {
    result.fields.push_back(current);
  } else if (any_quoted && result.fields.empty()) {
    result.fields.emplace_back();
  }

  return result;
}

std::string expand_word_unsplit(const Word &word, const ShellState &state) {
  const auto expanded = expand_word(word, state);
  if (expanded.fields.empty()) {
    return {};
  }
  std::string joined = expanded.fields[0];
  for (std::size_t i = 1; i < expanded.fields.size(); ++i) {
    joined.push_back(' ');
    joined += expanded.fields[i];
  }
  return joined;
}

} // namespace shell

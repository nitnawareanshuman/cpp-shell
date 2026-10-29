#include "shell/completion.hpp"
#include "shell/path.hpp"
#include "shell/state.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace shell {
namespace {

struct Json {
  enum class Type { Null, Bool, Number, String, Array, Object };
  Type type = Type::Null;
  bool b = false;
  std::string s;
  std::vector<Json> a;
  std::map<std::string, Json> o;
};

class JsonParser {
public:
  explicit JsonParser(std::string text) : text_(std::move(text)) {}

  bool parse(Json &out, std::string &error) {
    skip();
    if (!parse_value(out, error)) {
      return false;
    }
    skip();
    if (i_ != text_.size()) {
      error = "Unexpected trailing JSON content";
      return false;
    }
    return true;
  }

private:
  std::string text_;
  std::size_t i_ = 0;

  void skip() {
    while (i_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[i_]))) {
      ++i_;
    }
  }

  bool parse_value(Json &out, std::string &error) {
    skip();
    if (i_ >= text_.size()) {
      error = "Unexpected end of JSON";
      return false;
    }
    const char ch = text_[i_];
    if (ch == '{') {
      return parse_object(out, error);
    }
    if (ch == '[') {
      return parse_array(out, error);
    }
    if (ch == '"') {
      return parse_string(out, error);
    }
    if (ch == 't' || ch == 'f') {
      return parse_bool(out, error);
    }
    if (ch == 'n') {
      if (text_.compare(i_, 4, "null") != 0) {
        error = "Invalid JSON null";
        return false;
      }
      i_ += 4;
      out = Json{};
      return true;
    }
    error = "Unsupported JSON value";
    return false;
  }

  bool parse_string(Json &out, std::string &error) {
    if (text_[i_] != '"') {
      error = "Expected string";
      return false;
    }
    ++i_;
    std::string value;
    while (i_ < text_.size()) {
      const char ch = text_[i_++];
      if (ch == '"') {
        out.type = Json::Type::String;
        out.s = std::move(value);
        return true;
      }
      if (ch == '\\') {
        if (i_ >= text_.size()) {
          error = "Unterminated string escape";
          return false;
        }
        const char esc = text_[i_++];
        if (esc == '"' || esc == '\\' || esc == '/') {
          value += esc;
        } else if (esc == 'n') {
          value += '\n';
        } else if (esc == 't') {
          value += '\t';
        } else {
          value += esc;
        }
      } else {
        value += ch;
      }
    }
    error = "Unterminated JSON string";
    return false;
  }

  bool parse_bool(Json &out, std::string &error) {
    if (text_.compare(i_, 4, "true") == 0) {
      i_ += 4;
      out.type = Json::Type::Bool;
      out.b = true;
      return true;
    }
    if (text_.compare(i_, 5, "false") == 0) {
      i_ += 5;
      out.type = Json::Type::Bool;
      out.b = false;
      return true;
    }
    error = "Invalid JSON boolean";
    return false;
  }

  bool parse_array(Json &out, std::string &error) {
    ++i_;
    out.type = Json::Type::Array;
    skip();
    if (i_ < text_.size() && text_[i_] == ']') {
      ++i_;
      return true;
    }
    while (true) {
      Json item;
      if (!parse_value(item, error)) {
        return false;
      }
      out.a.push_back(std::move(item));
      skip();
      if (i_ >= text_.size()) {
        error = "Unterminated JSON array";
        return false;
      }
      if (text_[i_] == ',') {
        ++i_;
        continue;
      }
      if (text_[i_] == ']') {
        ++i_;
        return true;
      }
      error = "Expected ',' or ']' in JSON array";
      return false;
    }
  }

  bool parse_object(Json &out, std::string &error) {
    ++i_;
    out.type = Json::Type::Object;
    skip();
    if (i_ < text_.size() && text_[i_] == '}') {
      ++i_;
      return true;
    }
    while (true) {
      skip();
      Json key;
      if (!parse_string(key, error)) {
        error = "Expected object key";
        return false;
      }
      skip();
      if (i_ >= text_.size() || text_[i_] != ':') {
        error = "Expected ':' after object key";
        return false;
      }
      ++i_;
      Json value;
      if (!parse_value(value, error)) {
        return false;
      }
      out.o.emplace(key.s, std::move(value));
      skip();
      if (i_ >= text_.size()) {
        error = "Unterminated JSON object";
        return false;
      }
      if (text_[i_] == ',') {
        ++i_;
        continue;
      }
      if (text_[i_] == '}') {
        ++i_;
        return true;
      }
      error = "Expected ',' or '}' in JSON object";
      return false;
    }
  }
};

std::vector<std::string> json_string_array(const Json &value) {
  std::vector<std::string> out;
  if (value.type != Json::Type::Array) {
    return out;
  }
  for (const auto &item : value.a) {
    if (item.type == Json::Type::String) {
      out.push_back(item.s);
    }
  }
  return out;
}

bool starts_with(const std::string &value, const std::string &prefix) {
  return value.size() >= prefix.size() &&
         value.compare(0, prefix.size(), prefix) == 0;
}

std::string common_prefix(const std::vector<std::string> &values) {
  if (values.empty()) {
    return {};
  }
  std::string prefix = values[0];
  for (const auto &value : values) {
    std::size_t n = 0;
    while (n < prefix.size() && n < value.size() && prefix[n] == value[n]) {
      ++n;
    }
    prefix.resize(n);
  }
  return prefix;
}

struct TokenSpan {
  std::size_t begin = 0;
  std::size_t end = 0;
  std::string text;
};

std::vector<TokenSpan> split_completion_spans(const std::string &line,
                                              int cursor) {
  std::vector<TokenSpan> spans;
  TokenSpan current;
  bool in_single = false;
  bool in_double = false;
  bool started = false;
  const std::size_t limit =
      std::min(static_cast<std::size_t>(std::max(cursor, 0)), line.size());

  auto flush = [&](std::size_t end) {
    if (!started) {
      return;
    }
    current.end = end;
    current.text = line.substr(current.begin, current.end - current.begin);
    spans.push_back(current);
    current = TokenSpan{};
    started = false;
  };

  for (std::size_t i = 0; i < limit; ++i) {
    const char ch = line[i];
    if (ch == '\\' && !in_single && i + 1 < limit) {
      if (!started) {
        current.begin = i;
        started = true;
      }
      ++i;
      continue;
    }
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      if (!started) {
        current.begin = i;
        started = true;
      }
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      if (!started) {
        current.begin = i;
        started = true;
      }
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) && !in_single &&
        !in_double) {
      flush(i);
      continue;
    }
    if (!started) {
      current.begin = i;
      started = true;
    }
  }
  if (started) {
    flush(limit);
  } else if (limit == line.size() || limit == 0 ||
             (limit > 0 &&
              std::isspace(static_cast<unsigned char>(line[limit - 1])))) {
    TokenSpan empty;
    empty.begin = limit;
    empty.end = limit;
    spans.push_back(empty);
  }
  if (spans.empty()) {
    spans.push_back(TokenSpan{});
  }
  return spans;
}

std::string unquote_prefix(std::string token) {
  std::string out;
  bool in_single = false;
  bool in_double = false;
  for (std::size_t i = 0; i < token.size(); ++i) {
    const char ch = token[i];
    if (ch == '\\' && !in_single && i + 1 < token.size()) {
      out += token[++i];
      continue;
    }
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    out += ch;
  }
  return out;
}

} // namespace

std::string default_completions_path() {
  if (const char *env = std::getenv("CPP_SHELL_COMPLETIONS")) {
    if (*env != '\0') {
      return env;
    }
  }
  const char *home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return ".cpp-shell_completions.json";
  }
  return std::string(home) + "/.cpp-shell_completions.json";
}

Completer::Completer(ShellState &state) : state_(state) {}

std::string Completer::load_file(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    return {};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  Json root;
  std::string error;
  JsonParser parser(buffer.str());
  if (!parser.parse(root, error) || root.type != Json::Type::Object) {
    return "completion: invalid configuration in " + path +
           (error.empty() ? std::string{} : (": " + error));
  }
  rules_.clear();
  for (const auto &[command, spec] : root.o) {
    if (spec.type != Json::Type::Object) {
      return "completion: command '" + command + "' must be an object";
    }
    CompletionRule rule;
    if (spec.o.contains("files")) {
      if (spec.o.at("files").type != Json::Type::Bool) {
        return "completion: '" + command + ".files' must be a boolean";
      }
      rule.files = spec.o.at("files").b;
    }
    if (spec.o.contains("directories_only")) {
      if (spec.o.at("directories_only").type != Json::Type::Bool) {
        return "completion: '" + command +
               ".directories_only' must be a boolean";
      }
      rule.directories_only = spec.o.at("directories_only").b;
    }
    if (spec.o.contains("args")) {
      const Json &args = spec.o.at("args");
      if (args.type != Json::Type::Object) {
        return "completion: '" + command + ".args' must be an object";
      }
      for (const auto &[key, value] : args.o) {
        if (key == "*") {
          rule.any_position = json_string_array(value);
          continue;
        }
        try {
          const int index = std::stoi(key);
          rule.by_position[index] = json_string_array(value);
        } catch (...) {
          return "completion: invalid argument index '" + key + "' for " +
                 command;
        }
      }
    }
    rules_[command] = std::move(rule);
  }
  return {};
}

void Completer::register_rule(const std::string &command, CompletionRule rule) {
  rules_[command] = std::move(rule);
}

std::vector<std::string> Completer::command_candidates(const std::string &prefix) {
  std::vector<std::string> matches;
  for (const auto &name : state_.builtin_names()) {
    if (starts_with(name, prefix)) {
      matches.push_back(name);
    }
  }
  const char *pathvar = std::getenv("PATH");
  const std::string path = pathvar == nullptr ? std::string{} : pathvar;
  if (cached_executables_.empty() || cached_path_ != path) {
    cached_executables_ = list_path_executables();
    cached_path_ = path;
  }
  for (const auto &name : cached_executables_) {
    if (starts_with(name, prefix)) {
      matches.push_back(name);
    }
  }
  auto files = file_candidates(prefix, false);
  matches.insert(matches.end(), files.begin(), files.end());
  std::sort(matches.begin(), matches.end());
  matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
  return matches;
}

std::vector<std::string> Completer::file_candidates(const std::string &prefix,
                                                    bool directories_only) {
  std::vector<std::string> matches;
  const std::filesystem::path raw(prefix.empty() ? "./" : prefix);
  std::filesystem::path dir = raw.parent_path();
  std::string stem = raw.filename().string();
  if (prefix.empty() || prefix.ends_with('/')) {
    dir = prefix.empty() ? std::filesystem::path(".") : std::filesystem::path(prefix);
    stem.clear();
  }
  if (dir.empty()) {
    dir = ".";
  }
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) {
      break;
    }
    const std::string name = entry.path().filename().string();
    if (!stem.empty() && !starts_with(name, stem)) {
      continue;
    }
    std::error_code type_ec;
    const bool is_dir = entry.is_directory(type_ec);
    if (directories_only && !is_dir) {
      continue;
    }
    std::filesystem::path completed = dir / name;
    std::string rendered = completed.lexically_normal().string();
    if (dir == "." && !prefix.starts_with('.')) {
      rendered = name;
    }
    if (is_dir) {
      if (!rendered.ends_with('/')) {
        rendered += '/';
      }
    }
    matches.push_back(rendered);
  }
  std::sort(matches.begin(), matches.end());
  return matches;
}

std::vector<std::string>
Completer::programmable_candidates(const std::string &command, int arg_index,
                                   const std::string &prefix) {
  std::vector<std::string> matches;
  const auto it = rules_.find(command);
  if (it == rules_.end()) {
    return matches;
  }
  const CompletionRule &rule = it->second;
  auto add_prefixed = [&](const std::vector<std::string> &values) {
    for (const auto &value : values) {
      if (starts_with(value, prefix)) {
        matches.push_back(value);
      }
    }
  };
  if (rule.by_position.contains(arg_index)) {
    add_prefixed(rule.by_position.at(arg_index));
  }
  add_prefixed(rule.any_position);
  return matches;
}

CompletionResult Completer::complete(const std::string &line, int cursor) {
  CompletionResult result;
  result.line = line;
  result.cursor = cursor;

  auto spans = split_completion_spans(line, cursor);
  const TokenSpan &span = spans.back();
  const int arg_index = static_cast<int>(spans.size()) - 1;
  const std::string prefix = unquote_prefix(span.text);
  std::string command;
  if (!spans.empty()) {
    command = unquote_prefix(spans.front().text);
  }

  std::vector<std::string> matches;
  if (arg_index == 0) {
    matches = command_candidates(prefix);
  } else {
    matches = programmable_candidates(command, arg_index, prefix);
    bool files = true;
    bool directories_only = command == "cd";
    if (const auto it = rules_.find(command); it != rules_.end()) {
      files = it->second.files;
      directories_only = directories_only || it->second.directories_only;
    }
    if (files) {
      auto file_matches = file_candidates(prefix, directories_only);
      matches.insert(matches.end(), file_matches.begin(), file_matches.end());
    }
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
  }

  result.matches = matches;
  if (matches.empty()) {
    return result;
  }

  std::string insertion = matches.size() == 1 ? matches.front()
                                              : common_prefix(matches);
  if (matches.size() == 1) {
    insertion = quote_for_shell(matches.front());
    if (!insertion.ends_with('/') && arg_index == 0) {
      insertion.push_back(' ');
    } else if (!insertion.ends_with('/')) {
      insertion.push_back(' ');
    }
  } else if (insertion.size() <= prefix.size()) {
    std::ostringstream display;
    for (std::size_t i = 0; i < matches.size(); ++i) {
      if (i != 0) {
        display << "  ";
      }
      display << matches[i];
    }
    result.display = display.str();
    return result;
  } else {
    insertion = quote_for_shell(insertion);
  }

  result.line = line.substr(0, span.begin) + insertion + line.substr(span.end);
  result.cursor = static_cast<int>(span.begin + insertion.size());
  result.applied = true;
  return result;
}

} // namespace shell

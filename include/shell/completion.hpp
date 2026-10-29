#pragma once

#include <map>
#include <string>
#include <vector>

namespace shell {

class ShellState;

struct CompletionRule {
  std::map<int, std::vector<std::string>> by_position;
  std::vector<std::string> any_position;
  bool files = true;
  bool directories_only = false;
};

struct CompletionResult {
  std::string line;
  int cursor = 0;
  std::vector<std::string> matches;
  std::string display;
  bool applied = false;
};

class Completer {
public:
  explicit Completer(ShellState &state);

  std::string load_file(const std::string &path);
  void register_rule(const std::string &command, CompletionRule rule);
  const std::map<std::string, CompletionRule> &rules() const { return rules_; }

  CompletionResult complete(const std::string &line, int cursor);

private:
  ShellState &state_;
  std::map<std::string, CompletionRule> rules_;
  std::vector<std::string> cached_executables_;
  std::string cached_path_;

  std::vector<std::string> command_candidates(const std::string &prefix);
  std::vector<std::string> file_candidates(const std::string &prefix,
                                           bool directories_only);
  std::vector<std::string> programmable_candidates(const std::string &command,
                                                   int arg_index,
                                                   const std::string &prefix);
};

std::string default_completions_path();

} // namespace shell

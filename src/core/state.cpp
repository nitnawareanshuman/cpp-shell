#include "shell/state.hpp"
#include "shell/completion.hpp"

#include <cstdlib>
#include <filesystem>
#include <unistd.h>

extern char **environ;

namespace shell {

ShellState::ShellState()
    : builtins_{"bg",  "cd",     "echo", "exit", "export", "fg",
                "help", "history", "jobs", "pwd",  "type"} {
  history_.set_path(default_history_path());
  history_.load();
  reload_ifs();
}

ShellState::~ShellState() = default;

void ShellState::reload_ifs() {
  const char *value = std::getenv("IFS");
  if (value == nullptr) {
    ifs_ = " \t\n";
  } else {
    ifs_ = value;
  }
}

std::string ShellState::getenv(const std::string &name) const {
  const char *value = ::getenv(name.c_str());
  return value == nullptr ? std::string{} : std::string(value);
}

bool ShellState::setenv_var(const std::string &name, const std::string &value) {
  return ::setenv(name.c_str(), value.c_str(), 1) == 0;
}

std::map<std::string, std::string> ShellState::environ_map() const {
  std::map<std::string, std::string> values;
  if (environ == nullptr) {
    return values;
  }
  for (char **cursor = environ; *cursor != nullptr; ++cursor) {
    const std::string entry(*cursor);
    const auto eq = entry.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    values.emplace(entry.substr(0, eq), entry.substr(eq + 1));
  }
  return values;
}

std::string ShellState::home_directory() const {
  const char *home = ::getenv("HOME");
  return home == nullptr ? std::string{} : std::string(home);
}

std::string ShellState::cwd() const {
  std::error_code ec;
  auto path = std::filesystem::current_path(ec);
  if (ec) {
    return {};
  }
  return path.string();
}

Completer &ShellState::completer() {
  if (!completer_) {
    completer_ = std::make_unique<Completer>(*this);
    completer_->load_file(completions_path());
  }
  return *completer_;
}

const Completer &ShellState::completer() const {
  return const_cast<ShellState *>(this)->completer();
}

std::string ShellState::completions_path() const {
  return default_completions_path();
}

std::string ShellState::load_completions() { return completer().load_file(completions_path()); }

bool ShellState::is_builtin(const std::string &name) const {
  for (const auto &builtin : builtins_) {
    if (builtin == name) {
      return true;
    }
  }
  return false;
}

} // namespace shell

#include "shell/history.hpp"

#include <cstdlib>
#include <fstream>
#include <unistd.h>

namespace shell {
namespace {

std::string expand_home_prefix(std::string path) {
  if (path.rfind("~/", 0) == 0) {
    const char *home = std::getenv("HOME");
    if (home != nullptr) {
      path = std::string(home) + path.substr(1);
    }
  }
  return path;
}

std::string trim_copy(std::string value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

} // namespace

std::string default_history_path() {
  if (const char *env = std::getenv("CPP_SHELL_HISTFILE")) {
    if (*env != '\0') {
      return expand_home_prefix(env);
    }
  }
  const char *home = std::getenv("HOME");
  if (home == nullptr || *home == '\0') {
    return ".cpp-shell_history";
  }
  return std::string(home) + "/.cpp-shell_history";
}

void History::set_path(std::string path) { path_ = expand_home_prefix(std::move(path)); }

std::string History::load() {
  if (path_.empty()) {
    path_ = default_history_path();
  }
  std::ifstream in(path_);
  if (!in) {
    return {};
  }
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      entries_.push_back(line);
    }
  }
  return {};
}

std::string History::save_append(const std::string &command) {
  if (path_.empty()) {
    path_ = default_history_path();
  }
  std::ofstream out(path_, std::ios::app);
  if (!out) {
    return "history: could not write " + path_;
  }
  out << command << '\n';
  return {};
}

bool History::add(const std::string &command) {
  const std::string trimmed = trim_copy(command);
  if (trimmed.empty()) {
    return false;
  }
  if (!entries_.empty() && entries_.back() == trimmed) {
    reset_cursor();
    return false;
  }
  entries_.push_back(trimmed);
  const std::string err = save_append(trimmed);
  (void)err;
  reset_cursor();
  return true;
}

void History::reset_cursor() {
  cursor_ = npos;
  stash_.clear();
}

std::string History::up(const std::string &current) {
  if (entries_.empty()) {
    return current;
  }
  if (cursor_ == npos) {
    stash_ = current;
    cursor_ = entries_.size() - 1;
    return entries_[cursor_];
  }
  if (cursor_ > 0) {
    --cursor_;
  }
  return entries_[cursor_];
}

std::string History::down(const std::string &current) {
  if (cursor_ == npos) {
    return current;
  }
  if (cursor_ + 1 < entries_.size()) {
    ++cursor_;
    return entries_[cursor_];
  }
  cursor_ = npos;
  std::string restored = stash_;
  stash_.clear();
  return restored;
}

} // namespace shell

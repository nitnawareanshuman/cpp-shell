#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace shell {

class History {
public:
  void set_path(std::string path);
  const std::string &path() const { return path_; }

  std::string load();
  std::string save_append(const std::string &command);

  bool add(const std::string &command);
  const std::vector<std::string> &entries() const { return entries_; }

  void reset_cursor();
  std::string up(const std::string &current);
  std::string down(const std::string &current);
  bool navigating() const { return cursor_ != npos; }

private:
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  std::string path_;
  std::vector<std::string> entries_;
  std::size_t cursor_ = npos;
  std::string stash_;
};

std::string default_history_path();

} // namespace shell

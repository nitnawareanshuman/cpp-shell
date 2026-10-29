#pragma once

#include "shell/history.hpp"
#include "shell/jobs.hpp"
#include "shell/types.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace shell {

class Completer;

class ShellState {
public:
  ShellState();
  ~ShellState();

  ShellState(const ShellState &) = delete;
  ShellState &operator=(const ShellState &) = delete;

  int last_status() const { return last_status_; }
  void set_last_status(int status) { last_status_ = status; }

  const std::string &ifs() const { return ifs_; }
  void reload_ifs();

  std::string getenv(const std::string &name) const;
  bool setenv_var(const std::string &name, const std::string &value);
  std::map<std::string, std::string> environ_map() const;

  std::string home_directory() const;
  std::string cwd() const;

  History &history() { return history_; }
  const History &history() const { return history_; }

  JobTable &jobs() { return jobs_; }
  const JobTable &jobs() const { return jobs_; }

  Completer &completer();
  const Completer &completer() const;

  std::string completions_path() const;
  std::string load_completions();

  const std::vector<std::string> &builtin_names() const { return builtins_; }

  bool is_builtin(const std::string &name) const;

private:
  int last_status_ = 0;
  std::string ifs_ = " \t\n";
  History history_;
  JobTable jobs_;
  std::vector<std::string> builtins_;
  mutable std::unique_ptr<Completer> completer_;
};

} // namespace shell

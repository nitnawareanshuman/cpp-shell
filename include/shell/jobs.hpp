#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

namespace shell {

enum class JobState { Running, Stopped, Done };

struct Job {
  int id = 0;
  pid_t pgid = -1;
  std::string command;
  JobState state = JobState::Running;
  int exit_status = 0;
  bool notified = false;
};

class JobTable {
public:
  Job add(pid_t pgid, std::string command, JobState state = JobState::Running);
  Job *find(int id);
  const Job *find(int id) const;
  Job *find_by_pgid(pid_t pgid);
  Job *current();
  std::vector<Job> &all() { return jobs_; }
  const std::vector<Job> &all() const { return jobs_; }

  std::vector<std::string> reap();
  std::vector<std::string> format_list() const;
  void remove_done();

  void set_notify_sink(std::vector<std::string> *sink) { notify_sink_ = sink; }

private:
  int next_id_ = 1;
  std::vector<Job> jobs_;
  std::vector<std::string> *notify_sink_ = nullptr;
};

} // namespace shell

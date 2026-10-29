#include "shell/jobs.hpp"

#include <sys/wait.h>

#include <cerrno>
#include <sstream>

namespace shell {

Job JobTable::add(pid_t pgid, std::string command, JobState state) {
  Job job;
  job.id = next_id_++;
  job.pgid = pgid;
  job.command = std::move(command);
  job.state = state;
  jobs_.push_back(job);
  return jobs_.back();
}

Job *JobTable::find(int id) {
  for (auto &job : jobs_) {
    if (job.id == id) {
      return &job;
    }
  }
  return nullptr;
}

const Job *JobTable::find(int id) const {
  for (const auto &job : jobs_) {
    if (job.id == id) {
      return &job;
    }
  }
  return nullptr;
}

Job *JobTable::find_by_pgid(pid_t pgid) {
  for (auto &job : jobs_) {
    if (job.pgid == pgid) {
      return &job;
    }
  }
  return nullptr;
}

Job *JobTable::current() {
  for (auto it = jobs_.rbegin(); it != jobs_.rend(); ++it) {
    if (it->state != JobState::Done) {
      return &(*it);
    }
  }
  return nullptr;
}

std::vector<std::string> JobTable::reap() {
  std::vector<std::string> notices;
  for (auto &job : jobs_) {
    if (job.state == JobState::Done || job.pgid <= 0) {
      continue;
    }
    int status = 0;
    while (true) {
      const pid_t pid = ::waitpid(-job.pgid, &status, WNOHANG | WUNTRACED);
      if (pid == 0) {
        break;
      }
      if (pid < 0) {
        if (errno == ECHILD && job.state != JobState::Stopped) {
          job.state = JobState::Done;
        }
        break;
      }
      if (WIFSTOPPED(status)) {
        job.state = JobState::Stopped;
        break;
      }
      if (WIFEXITED(status)) {
        job.exit_status = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        job.exit_status = 128 + WTERMSIG(status);
      }
    }
    if (job.state == JobState::Done && !job.notified) {
      std::ostringstream oss;
      oss << "[" << job.id << "]  Done  " << job.command;
      notices.push_back(oss.str());
      job.notified = true;
      if (notify_sink_ != nullptr) {
        notify_sink_->push_back(notices.back());
      }
    }
  }
  return notices;
}

std::vector<std::string> JobTable::format_list() const {
  std::vector<std::string> lines;
  for (const auto &job : jobs_) {
    const char *state = "Running";
    if (job.state == JobState::Stopped) {
      state = "Stopped";
    } else if (job.state == JobState::Done) {
      state = "Done";
    }
    std::ostringstream oss;
    oss << "[" << job.id << "]  " << state << "  " << job.command;
    lines.push_back(oss.str());
  }
  return lines;
}

void JobTable::remove_done() {
  std::erase_if(jobs_, [](const Job &job) { return job.state == JobState::Done; });
}

} // namespace shell

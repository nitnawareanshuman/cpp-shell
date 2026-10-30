#pragma once

#include <unistd.h>

#include <utility>

namespace shell {

class UniqueFd {
public:
  UniqueFd() = default;
  explicit UniqueFd(int fd) : fd_(fd) {}

  ~UniqueFd() { reset(); }

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;

  UniqueFd(UniqueFd &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

  UniqueFd &operator=(UniqueFd &&other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }

  int release() {
    int fd = fd_;
    fd_ = -1;
    return fd;
  }

  void reset(int fd = -1) {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = fd;
  }

private:
  int fd_ = -1;
};

inline bool make_pipe(UniqueFd &read_end, UniqueFd &write_end) {
  int fds[2] = {-1, -1};
  if (::pipe(fds) != 0) {
    return false;
  }
  read_end.reset(fds[0]);
  write_end.reset(fds[1]);
  return true;
}

} // namespace shell

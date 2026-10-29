#pragma once

#include <iostream>
#include <string>
#include <utility>

struct TestStats {
  int passed = 0;
  int failed = 0;
};

inline TestStats &test_stats() {
  static TestStats stats;
  return stats;
}

inline void check_impl(bool cond, const char *expr, const char *file, int line) {
  if (cond) {
    ++test_stats().passed;
    return;
  }
  ++test_stats().failed;
  std::cerr << "CHECK failed at " << file << ":" << line << " : " << expr
            << "\n";
}

template <typename A, typename B>
inline void check_eq_impl(const A &left, const B &right, const char *left_expr,
                          const char *right_expr, const char *file, int line) {
  if (left == right) {
    ++test_stats().passed;
    return;
  }
  ++test_stats().failed;
  std::cerr << "CHECK_EQ failed at " << file << ":" << line << "\n  "
            << left_expr << " == " << right_expr << "\n  left : " << left
            << "\n  right: " << right << "\n";
}

#define CHECK(cond) check_impl(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check_eq_impl((a), (b), #a, #b, __FILE__, __LINE__)

inline int test_summary() {
  std::cerr << test_stats().passed << " passed, " << test_stats().failed
            << " failed\n";
  return test_stats().failed == 0 ? 0 : 1;
}

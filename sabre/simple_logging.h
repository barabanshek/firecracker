#ifndef _SIMPLE_LOGGING_H_
#define _SIMPLE_LOGGING_H_

#include <iostream>

// Our own simple logging as glog + Rust is complicated.
namespace logging {

#define LOG_ERROR 0
#define LOG_INFO 1
#define LOG_VERBAL 2

struct Logger {
  ~Logger(void) { std::cout << std::endl; }
};

template <typename T> Logger &&operator<<(Logger &&wrap, T const &text) {
  std::cout << text;
  return std::move(wrap);
}

#define RLOG(x)                                                                \
  if (x <= logging::_g_log_severity_)                                          \
  logging::Logger()

} // namespace logging

#endif

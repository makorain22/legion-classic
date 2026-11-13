#include "pioneer/core/assert.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "pioneer/core/log.hpp"

namespace pioneer {

namespace {
std::atomic<AssertHandler> g_handler{nullptr};
std::atomic<uint64_t> g_failures{0};

AssertAction defaultHandler(const char*, const char*, int, const char*) {
#ifdef NDEBUG
  return AssertAction::Continue;
#else
  return AssertAction::Stop;
#endif
}

void report(const char* expression, const char* file, int line, const char* message) {
  const uint64_t n = g_failures.fetch_add(1) + 1;
  // The first failures in full; after that one in a thousand, so a check
  // failing every tick can't fill the disk.
  if (n <= 20 || n % 1000 == 0) {
    if (message[0])
      log::writeTo(log::Channel::Core, log::Level::Error, "Check failed: %s  (%s)  at %s:%d%s", expression, message,
                   file, line, n > 20 ? "  [and many more]" : "");
    else
      log::writeTo(log::Channel::Core, log::Level::Error, "Check failed: %s  at %s:%d%s", expression, file, line,
                   n > 20 ? "  [and many more]" : "");
  }
  const AssertHandler h = g_handler.load();
  if ((h ? h : defaultHandler)(expression, file, line, message) == AssertAction::Stop) std::abort();
}
}  // namespace

AssertHandler setAssertHandler(AssertHandler handler) { return g_handler.exchange(handler); }
uint64_t assertFailures() { return g_failures.load(); }

namespace detail {

void assertFailed(const char* expression, const char* file, int line) { report(expression, file, line, ""); }

void assertFailedMsg(const char* expression, const char* file, int line, const char* fmt, ...) {
  char msg[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(msg, sizeof msg, fmt, args);
  va_end(args);
  report(expression, file, line, msg);
}

void fatal(const char* file, int line, const char* fmt, ...) {
  char msg[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(msg, sizeof msg, fmt, args);
  va_end(args);
  log::writeTo(log::Channel::Core, log::Level::Error, "Fatal: %s  at %s:%d", msg, file, line);
  std::abort();
}

}  // namespace detail

}  // namespace pioneer

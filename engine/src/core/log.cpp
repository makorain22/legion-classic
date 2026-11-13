#include "pioneer/core/log.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace pioneer::log {

namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
Level g_minLevel = Level::Info;
const auto g_start = std::chrono::steady_clock::now();

const char* levelName(Level l) {
  switch (l) {
    case Level::Debug: return "DEBUG";
    case Level::Info: return "INFO ";
    case Level::Warn: return "WARN ";
    case Level::Error: return "ERROR";
  }
  return "?";
}
}  // namespace

void init(const char* logFilePath) {
  std::lock_guard lock(g_mutex);
  if (logFilePath) g_file = std::fopen(logFilePath, "w");
}

void shutdown() {
  std::lock_guard lock(g_mutex);
  if (g_file) {
    std::fclose(g_file);
    g_file = nullptr;
  }
}

void setMinLevel(Level level) { g_minLevel = level; }

void write(Level level, const char* fmt, ...) {
  if (level < g_minLevel) return;
  char msg[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(msg, sizeof msg, fmt, args);
  va_end(args);

  const double t =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
  std::lock_guard lock(g_mutex);
  FILE* out = level >= Level::Warn ? stderr : stdout;
  std::fprintf(out, "[%8.3f] %s %s\n", t, levelName(level), msg);
  std::fflush(out);
  if (g_file) {
    std::fprintf(g_file, "[%8.3f] %s %s\n", t, levelName(level), msg);
    std::fflush(g_file);
  }
}

}  // namespace pioneer::log

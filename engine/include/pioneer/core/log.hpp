#pragma once
// Pioneer Engine - logging to the console and to a log file.

namespace pioneer::log {

enum class Level { Debug, Info, Warn, Error };

// Opens the log file (optional; pass nullptr for console only).
void init(const char* logFilePath);
void shutdown();
void setMinLevel(Level level);

#if defined(__MINGW32__)
__attribute__((format(gnu_printf, 2, 3)))
#elif defined(__GNUC__)
__attribute__((format(printf, 2, 3)))
#endif
void write(Level level, const char* fmt, ...);

}  // namespace pioneer::log

#define PNR_DEBUG(...) ::pioneer::log::write(::pioneer::log::Level::Debug, __VA_ARGS__)
#define PNR_INFO(...) ::pioneer::log::write(::pioneer::log::Level::Info, __VA_ARGS__)
#define PNR_WARN(...) ::pioneer::log::write(::pioneer::log::Level::Warn, __VA_ARGS__)
#define PNR_ERROR(...) ::pioneer::log::write(::pioneer::log::Level::Error, __VA_ARGS__)

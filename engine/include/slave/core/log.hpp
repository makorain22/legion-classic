#pragma once
// Slave Engine - logging to the console and to a log file.

namespace slave::log {

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

}  // namespace slave::log

#define SLV_DEBUG(...) ::slave::log::write(::slave::log::Level::Debug, __VA_ARGS__)
#define SLV_INFO(...) ::slave::log::write(::slave::log::Level::Info, __VA_ARGS__)
#define SLV_WARN(...) ::slave::log::write(::slave::log::Level::Warn, __VA_ARGS__)
#define SLV_ERROR(...) ::slave::log::write(::slave::log::Level::Error, __VA_ARGS__)

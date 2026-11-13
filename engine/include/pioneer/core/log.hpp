#pragma once
// Pioneer Engine - logging: to the console, a log file, a ring buffer the
// in-game console reads, and any sink a tool cares to add.
//
// Every line belongs to a channel (who is talking) and has a level (how much
// it matters). Each channel has its own threshold, so a noisy system can be
// opened up ("log.level path debug") without drowning in the rest.
//
//   PNR_INFO("loaded %d maps", n);                 // channel Core
//   PNR_LOG(Net, Warn, "peer %d timed out", id);   // any channel, any level
//   PNR_INFO_CH(Audio, "device: %s", name);
//
// Thread-safe. Lines below a channel's threshold cost one comparison.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace pioneer::log {

enum class Level : uint8_t { Trace, Debug, Info, Warn, Error, Off };
enum class Channel : uint8_t {
  Core, Sim, AI, Path, Script, Map, Content, Assets, Render, Audio,
  UI, Input, Net, Game, Editor, Tools, Console, Jobs,
  Count
};
constexpr int kChannelCount = static_cast<int>(Channel::Count);

const char* levelName(Level l);                // "debug", "info", ...
const char* channelName(Channel c);            // "core", "sim", ...
bool levelFromName(std::string_view name, Level& out);
bool channelFromName(std::string_view name, Channel& out);

// Opens the log file (optional; pass nullptr for console only).
void init(const char* logFilePath);
void shutdown();
// The threshold of every channel at once / of one channel.
void setMinLevel(Level level);
void setChannelLevel(Channel c, Level level);
Level channelLevel(Channel c);
inline bool wanted(Channel c, Level l) { return l >= channelLevel(c) && l != Level::Off; }
// Printing to stdout/stderr (on by default; tools that print their own report switch it off).
void setConsoleOutput(bool on);

#if defined(__MINGW32__)
#define PNR_PRINTF_LIKE(f, a) __attribute__((format(gnu_printf, f, a)))
#elif defined(__GNUC__)
#define PNR_PRINTF_LIKE(f, a) __attribute__((format(printf, f, a)))
#else
#define PNR_PRINTF_LIKE(f, a)
#endif

// Channel Core.
PNR_PRINTF_LIKE(2, 3) void write(Level level, const char* fmt, ...);
PNR_PRINTF_LIKE(3, 4) void writeTo(Channel channel, Level level, const char* fmt, ...);
// A line that is already formatted.
void writeText(Channel channel, Level level, std::string_view text);

// --- The ring buffer: the newest lines, for the in-game console ----------------
struct Entry {
  uint64_t seq = 0;   // 1, 2, 3 ... in the order written
  double time = 0;    // seconds since start
  Level level = Level::Info;
  Channel channel = Channel::Core;
  std::string text;
};
// Appends entries with seq > afterSeq to `out` (oldest first); returns the
// newest seq there is. A reader keeps that number and asks again later.
uint64_t recent(std::vector<Entry>& out, uint64_t afterSeq = 0);
uint64_t lastSeq();
void setRingSize(size_t lines);  // default 2000
void clearRing();
// How many warnings and errors were written since start (for a status light).
void counts(uint64_t& warnings, uint64_t& errors);

// --- Sinks -------------------------------------------------------------------------
// Called for every line that passes its channel's threshold, on the thread
// that wrote it, with the log locked: keep it short and don't log from it.
using Sink = std::function<void(const Entry&)>;
int addSink(Sink sink);
void removeSink(int id);

}  // namespace pioneer::log

#define PNR_LOG(channel, level, ...)                                                               \
  do {                                                                                             \
    if (::pioneer::log::wanted(::pioneer::log::Channel::channel, ::pioneer::log::Level::level))    \
      ::pioneer::log::writeTo(::pioneer::log::Channel::channel, ::pioneer::log::Level::level, __VA_ARGS__); \
  } while (0)

#define PNR_TRACE(...) PNR_LOG(Core, Trace, __VA_ARGS__)
#define PNR_DEBUG(...) PNR_LOG(Core, Debug, __VA_ARGS__)
#define PNR_INFO(...) PNR_LOG(Core, Info, __VA_ARGS__)
#define PNR_WARN(...) PNR_LOG(Core, Warn, __VA_ARGS__)
#define PNR_ERROR(...) PNR_LOG(Core, Error, __VA_ARGS__)

#define PNR_TRACE_CH(channel, ...) PNR_LOG(channel, Trace, __VA_ARGS__)
#define PNR_DEBUG_CH(channel, ...) PNR_LOG(channel, Debug, __VA_ARGS__)
#define PNR_INFO_CH(channel, ...) PNR_LOG(channel, Info, __VA_ARGS__)
#define PNR_WARN_CH(channel, ...) PNR_LOG(channel, Warn, __VA_ARGS__)
#define PNR_ERROR_CH(channel, ...) PNR_LOG(channel, Error, __VA_ARGS__)

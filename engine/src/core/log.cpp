#include "pioneer/core/log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace pioneer::log {

namespace {

struct SinkEntry {
  int id;
  Sink fn;
};

// Never destroyed: threads may log while the program's statics are torn down.
struct Global {
  std::mutex m;
  FILE* file = nullptr;
  bool console = true;
  std::atomic<uint8_t> levels[kChannelCount];
  std::deque<Entry> ring;
  size_t ringSize = 2000;
  uint64_t seq = 0;
  uint64_t warnings = 0, errors = 0;
  std::vector<SinkEntry> sinks;
  int nextSink = 1;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  Global() {
    for (auto& l : levels) l.store(static_cast<uint8_t>(Level::Info));
  }
};

Global& g() {
  static Global* instance = new Global;
  return *instance;
}

const char* const kLevelNames[] = {"trace", "debug", "info", "warn", "error", "off"};
const char* const kLevelTags[] = {"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "OFF  "};
const char* const kChannelNames[kChannelCount] = {"core",   "sim",   "ai",  "path",  "script", "map",
                                                  "content", "assets", "render", "audio", "ui",     "input",
                                                  "net",    "game",  "editor", "tools", "console", "jobs"};

bool sameName(std::string_view a, const char* b) {
  size_t i = 0;
  for (; i < a.size() && b[i]; ++i) {
    char c = a[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != b[i]) return false;
  }
  return i == a.size() && !b[i];
}

void emit(Channel channel, Level level, std::string_view text) {
  Global& G = g();
  const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - G.start).count();
  const int li = static_cast<int>(level);
  const int ci = static_cast<int>(channel);
  std::lock_guard<std::mutex> lock(G.m);
  // The same line as ever for channel Core ("[  1.234] INFO  text"); others say who is talking.
  char head[48];
  if (channel == Channel::Core) std::snprintf(head, sizeof head, "[%8.3f] %s ", t, kLevelTags[li]);
  else std::snprintf(head, sizeof head, "[%8.3f] %s [%s] ", t, kLevelTags[li], kChannelNames[ci]);
  if (G.console) {
    FILE* out = level >= Level::Warn ? stderr : stdout;
    std::fprintf(out, "%s%.*s\n", head, static_cast<int>(text.size()), text.data());
    std::fflush(out);
  }
  if (G.file) {
    std::fprintf(G.file, "%s%.*s\n", head, static_cast<int>(text.size()), text.data());
    std::fflush(G.file);
  }
  if (level == Level::Warn) ++G.warnings;
  if (level == Level::Error) ++G.errors;
  Entry e;
  e.seq = ++G.seq;
  e.time = t;
  e.level = level;
  e.channel = channel;
  e.text.assign(text.data(), text.size());
  for (const SinkEntry& s : G.sinks) s.fn(e);
  if (G.ringSize > 0) {
    G.ring.push_back(std::move(e));
    while (G.ring.size() > G.ringSize) G.ring.pop_front();
  }
}

}  // namespace

const char* levelName(Level l) { return kLevelNames[std::min<int>(static_cast<int>(l), 5)]; }
const char* channelName(Channel c) {
  const int i = static_cast<int>(c);
  return i >= 0 && i < kChannelCount ? kChannelNames[i] : "?";
}

bool levelFromName(std::string_view name, Level& out) {
  for (int i = 0; i < 6; ++i)
    if (sameName(name, kLevelNames[i])) {
      out = static_cast<Level>(i);
      return true;
    }
  if (sameName(name, "warning")) {
    out = Level::Warn;
    return true;
  }
  return false;
}

bool channelFromName(std::string_view name, Channel& out) {
  for (int i = 0; i < kChannelCount; ++i)
    if (sameName(name, kChannelNames[i])) {
      out = static_cast<Channel>(i);
      return true;
    }
  return false;
}

void init(const char* logFilePath) {
  Global& G = g();
  std::lock_guard<std::mutex> lock(G.m);
  if (G.file) std::fclose(G.file);
  G.file = logFilePath ? std::fopen(logFilePath, "w") : nullptr;
}

void shutdown() {
  Global& G = g();
  std::lock_guard<std::mutex> lock(G.m);
  if (G.file) {
    std::fclose(G.file);
    G.file = nullptr;
  }
}

void setMinLevel(Level level) {
  for (auto& l : g().levels) l.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
}
void setChannelLevel(Channel c, Level level) {
  g().levels[static_cast<int>(c)].store(static_cast<uint8_t>(level), std::memory_order_relaxed);
}
Level channelLevel(Channel c) {
  return static_cast<Level>(g().levels[static_cast<int>(c)].load(std::memory_order_relaxed));
}

void setConsoleOutput(bool on) {
  Global& G = g();
  std::lock_guard<std::mutex> lock(G.m);
  G.console = on;
}

void write(Level level, const char* fmt, ...) {
  if (!wanted(Channel::Core, level)) return;
  char msg[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(msg, sizeof msg, fmt, args);
  va_end(args);
  emit(Channel::Core, level, msg);
}

void writeTo(Channel channel, Level level, const char* fmt, ...) {
  if (!wanted(channel, level)) return;
  char msg[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(msg, sizeof msg, fmt, args);
  va_end(args);
  emit(channel, level, msg);
}

void writeText(Channel channel, Level level, std::string_view text) {
  if (!wanted(channel, level)) return;
  emit(channel, level, text);
}

uint64_t recent(std::vector<Entry>& out, uint64_t afterSeq) {
  Global& G = g();
  std::lock_guard<std::mutex> lock(G.m);
  for (const Entry& e : G.ring)
    if (e.seq > afterSeq) out.push_back(e);
  return G.seq;
}

uint64_t lastSeq() {
  Global& G = g();
  std::lock_guard<std::mutex> lock(G.m);
  return G.seq;
}

void setRingSize(size_t lines) {
  Global& G = g();
  std::lock_guard<std::mutex> lock(G.m);
  G.ringSize = lines;
  while (G.ring.size() > G.ringSize) G.ring.pop_front();
}

void clearRing() {
  Global& G = g();
  std::lock_guard<std::mutex> lock(G.m);
  G.ring.clear();
}

void counts(uint64_t& warnings, uint64_t& errors) {
  Global& G = g();
  std::lock_guard<std::mutex> lock(G.m);
  warnings = G.warnings;
  errors = G.errors;
}

int addSink(Sink sink) {
  Global& G = g();
  std::lock_guard<std::mutex> lock(G.m);
  G.sinks.push_back({G.nextSink, std::move(sink)});
  return G.nextSink++;
}

void removeSink(int id) {
  Global& G = g();
  std::lock_guard<std::mutex> lock(G.m);
  G.sinks.erase(std::remove_if(G.sinks.begin(), G.sinks.end(), [id](const SinkEntry& s) { return s.id == id; }),
                G.sinks.end());
}

}  // namespace pioneer::log

#include "pioneer/core/profiler.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>

namespace pioneer::prof {

namespace {

constexpr size_t kHistory = 240;       // frames kept for the overlay
constexpr size_t kMaxFrameEvents = 200000;  // a runaway frame can't eat all memory

struct ThreadData {
  std::mutex m;  // the owner pushes, the main thread collects
  std::vector<Event> events;
  std::string name;
  uint16_t index = 0;
  uint16_t depth = 0;
};

// Lives for ever on purpose: worker threads may still close scopes while the
// program's statics are being destroyed.
struct Global {
  std::atomic<bool> enabled{false};
  std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now();
  std::mutex m;  // threads, counters, interned names
  std::vector<std::shared_ptr<ThreadData>> threads;
  std::vector<std::pair<const char*, double>> counters;
  std::set<std::string> names;

  // Main thread only.
  std::deque<FrameStats> frames;
  FrameStats empty;
  uint64_t frameIndex = 0;
  uint64_t frameStart = 0;
  bool started = false;

  std::atomic<int> captureLeft{0};
  bool captureWasEnabled = false;
  bool captureDone = false;
  std::vector<Event> capture;
  std::vector<std::pair<uint64_t, std::vector<CounterStat>>> captureCounters;
};

Global& g() {
  static Global* instance = new Global;
  return *instance;
}

ThreadData& local() {
  thread_local std::shared_ptr<ThreadData> data;
  if (!data) {
    data = std::make_shared<ThreadData>();
    Global& G = g();
    std::lock_guard<std::mutex> lk(G.m);
    data->index = static_cast<uint16_t>(G.threads.size());
    data->name = G.threads.empty() ? "Main" : "Thread " + std::to_string(G.threads.size());
    G.threads.push_back(data);
  }
  return *data;
}

std::string jsonEscape(const char* s) {
  std::string out;
  for (; *s; ++s) {
    const unsigned char c = static_cast<unsigned char>(*s);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\u%04x", c);
      out += buf;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

}  // namespace

void setEnabled(bool on) { g().enabled.store(on, std::memory_order_relaxed); }
bool enabled() { return g().enabled.load(std::memory_order_relaxed); }

uint64_t nowNs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - g().origin).count());
}

void setThreadName(const char* name) {
  ThreadData& t = local();
  std::lock_guard<std::mutex> lk(g().m);
  t.name = name;
}

const char* intern(const std::string& name) {
  Global& G = g();
  std::lock_guard<std::mutex> lk(G.m);
  return G.names.insert(name).first->c_str();
}

Scope::Scope(const char* name) : name_(nullptr) {
  if (!g().enabled.load(std::memory_order_relaxed)) return;
  name_ = name;
  ++local().depth;
  start_ = nowNs();
}

Scope::~Scope() {
  if (!name_) return;
  const uint64_t end = nowNs();
  ThreadData& t = local();
  --t.depth;
  std::lock_guard<std::mutex> lk(t.m);
  if (t.events.size() < kMaxFrameEvents) t.events.push_back({name_, start_, end, t.depth, t.index});
}

void counter(const char* name, double value) {
  Global& G = g();
  if (!G.enabled.load(std::memory_order_relaxed)) return;
  std::lock_guard<std::mutex> lk(G.m);
  for (auto& c : G.counters)
    if (c.first == name) {
      c.second = value;
      return;
    }
  G.counters.emplace_back(name, value);
}

void beginFrame() {
  Global& G = g();
  const uint64_t now = nowNs();
  if (!G.enabled.load(std::memory_order_relaxed) && G.captureLeft.load() <= 0) {
    G.started = false;
    return;
  }
  local();  // the main thread is thread 0
  if (!G.started) {  // first frame after being switched on: nothing to close yet
    G.started = true;
    G.frameStart = now;
    std::lock_guard<std::mutex> lk(G.m);
    for (auto& t : G.threads) {
      std::lock_guard<std::mutex> tl(t->m);
      t->events.clear();
    }
    return;
  }

  FrameStats f;
  f.index = G.frameIndex++;
  f.ms = static_cast<double>(now - G.frameStart) / 1e6;
  G.frameStart = now;
  {
    std::lock_guard<std::mutex> lk(G.m);
    for (auto& t : G.threads) {
      std::lock_guard<std::mutex> tl(t->m);
      f.events.insert(f.events.end(), t->events.begin(), t->events.end());
      t->events.clear();
    }
    for (auto& c : G.counters) f.counters.push_back({c.first, c.second});
  }
  std::sort(f.events.begin(), f.events.end(), [](const Event& a, const Event& b) {
    if (a.thread != b.thread) return a.thread < b.thread;
    if (a.startNs != b.startNs) return a.startNs < b.startNs;
    return a.depth < b.depth;
  });

  // Add up by name. Self time: take each scope's direct children off it.
  std::unordered_map<const char*, size_t> at;
  std::vector<double> childMs(f.events.size(), 0.0);
  {
    std::vector<size_t> stack;  // open scopes of the current thread
    uint16_t thread = 0xFFFF;
    for (size_t i = 0; i < f.events.size(); ++i) {
      const Event& e = f.events[i];
      if (e.thread != thread) {
        thread = e.thread;
        stack.clear();
      }
      while (!stack.empty() && f.events[stack.back()].endNs <= e.startNs) stack.pop_back();
      while (!stack.empty() && f.events[stack.back()].depth >= e.depth) stack.pop_back();
      if (!stack.empty()) childMs[stack.back()] += static_cast<double>(e.endNs - e.startNs) / 1e6;
      stack.push_back(i);
    }
  }
  for (size_t i = 0; i < f.events.size(); ++i) {
    const Event& e = f.events[i];
    const double ms = static_cast<double>(e.endNs - e.startNs) / 1e6;
    auto it = at.find(e.name);
    if (it == at.end()) {
      it = at.emplace(e.name, f.scopes.size()).first;
      ScopeStat s;
      s.name = e.name;
      s.depth = e.depth;
      s.mainThread = e.thread == 0;
      f.scopes.push_back(s);
    }
    ScopeStat& s = f.scopes[it->second];
    ++s.calls;
    s.ms += ms;
    s.selfMs += ms - childMs[i];
    s.maxMs = std::max(s.maxMs, ms);
    s.depth = std::min(s.depth, static_cast<int>(e.depth));
    s.mainThread = s.mainThread && e.thread == 0;
  }
  std::stable_sort(f.scopes.begin(), f.scopes.end(),
                   [](const ScopeStat& a, const ScopeStat& b) { return a.ms > b.ms; });

  if (G.captureLeft.load() > 0) {
    G.capture.insert(G.capture.end(), f.events.begin(), f.events.end());
    G.captureCounters.emplace_back(now, f.counters);
    if (G.captureLeft.fetch_sub(1) == 1) {
      G.captureDone = true;
      if (!G.captureWasEnabled) G.enabled.store(false);
    }
  }

  if (!G.frames.empty()) {  // only the newest frame keeps its raw events
    G.frames.back().events.clear();
    G.frames.back().events.shrink_to_fit();
  }
  G.frames.push_back(std::move(f));
  while (G.frames.size() > kHistory) G.frames.pop_front();
}

const FrameStats& lastFrame() {
  Global& G = g();
  return G.frames.empty() ? G.empty : G.frames.back();
}

std::vector<const FrameStats*> history(size_t max) {
  Global& G = g();
  std::vector<const FrameStats*> out;
  const size_t n = std::min(max, G.frames.size());
  for (size_t i = G.frames.size() - n; i < G.frames.size(); ++i) out.push_back(&G.frames[i]);
  return out;
}

void frameTimes(double& mean, double& worst, double& best) {
  Global& G = g();
  mean = worst = best = 0;
  if (G.frames.empty()) return;
  best = 1e30;
  for (const FrameStats& f : G.frames) {
    mean += f.ms;
    worst = std::max(worst, f.ms);
    best = std::min(best, f.ms);
  }
  mean /= static_cast<double>(G.frames.size());
}

double scopeMs(const char* name) {
  const std::string want = name;
  for (const ScopeStat& s : lastFrame().scopes)
    if (s.name == name || want == s.name) return s.ms;
  return 0;
}

std::vector<std::string> threadNames() {
  Global& G = g();
  std::lock_guard<std::mutex> lk(G.m);
  std::vector<std::string> out;
  for (auto& t : G.threads) out.push_back(t->name);
  return out;
}

void startCapture(int frames) {
  Global& G = g();
  if (frames <= 0) return;
  G.capture.clear();
  G.captureCounters.clear();
  G.captureDone = false;
  G.captureWasEnabled = G.enabled.load();
  G.captureLeft.store(frames);
  G.enabled.store(true);
}

bool capturing() { return g().captureLeft.load() > 0; }
int captureFramesLeft() { return std::max(0, g().captureLeft.load()); }
bool captureReady() { return g().captureDone; }

void discardCapture() {
  Global& G = g();
  G.capture.clear();
  G.capture.shrink_to_fit();
  G.captureCounters.clear();
  G.captureDone = false;
  if (G.captureLeft.load() > 0) {
    G.captureLeft.store(0);
    if (!G.captureWasEnabled) G.enabled.store(false);
  }
}

std::string traceJson() {
  Global& G = g();
  std::string out = "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n";
  char buf[256];
  bool first = true;
  auto sep = [&] {
    if (!first) out += ",\n";
    first = false;
  };
  const std::vector<std::string> names = threadNames();
  for (size_t i = 0; i < names.size(); ++i) {
    sep();
    out += "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":" + std::to_string(i) +
           ",\"args\":{\"name\":\"" + jsonEscape(names[i].c_str()) + "\"}}";
  }
  sep();
  out += "{\"name\":\"process_name\",\"ph\":\"M\",\"pid\":1,\"args\":{\"name\":\"Pioneer Engine\"}}";
  for (const Event& e : G.capture) {
    sep();
    std::snprintf(buf, sizeof buf, "\",\"cat\":\"pioneer\",\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f,\"pid\":1,\"tid\":%u}",
                  static_cast<double>(e.startNs) / 1e3, static_cast<double>(e.endNs - e.startNs) / 1e3,
                  static_cast<unsigned>(e.thread));
    out += "{\"name\":\"" + jsonEscape(e.name) + buf;
  }
  for (const auto& [ns, counters] : G.captureCounters) {
    for (const CounterStat& c : counters) {
      sep();
      std::snprintf(buf, sizeof buf, "\",\"ph\":\"C\",\"ts\":%.3f,\"pid\":1,\"args\":{\"value\":%.6g}}",
                    static_cast<double>(ns) / 1e3, c.value);
      out += "{\"name\":\"" + jsonEscape(c.name) + buf;
    }
  }
  out += "\n]}\n";
  return out;
}

bool saveTrace(const std::string& path) {
  const std::string json = traceJson();
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(json.data(), 1, json.size(), f) == json.size();
  std::fclose(f);
  return ok;
}

void reset() {
  Global& G = g();
  discardCapture();
  G.frames.clear();
  G.frameIndex = 0;
  G.started = false;
  std::lock_guard<std::mutex> lk(G.m);
  G.counters.clear();
  for (auto& t : G.threads) {
    std::lock_guard<std::mutex> tl(t->m);
    t->events.clear();
  }
}

}  // namespace pioneer::prof

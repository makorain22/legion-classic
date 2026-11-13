#pragma once
// Pioneer Engine - the profiler: where a frame's (or a tick's) time goes.
//
// Mark a stretch of code with PNR_PROFILE("name"). Every thread records its
// own scopes; beginFrame() on the main thread closes the frame, adds up each
// scope's calls and time, and keeps a few seconds of history for the overlay.
// A capture keeps every single scope of the next N frames and writes them as
// a Chrome trace (open in chrome://tracing, Perfetto or speedscope).
//
// Cost: when the profiler is off (the default) a scope is one predictable
// branch. When on, a scope costs two clock reads and one push onto a
// thread-local vector.
//
// Names must be string literals (the profiler keeps the pointer). For a name
// built at run time use prof::intern().
//
// The profiler only ever measures. Nothing in the simulation may read it.

#include <cstdint>
#include <string>
#include <vector>

namespace pioneer::prof {

void setEnabled(bool on);
bool enabled();

// Names the calling thread in captures and the overlay ("Main", "Worker 3").
void setThreadName(const char* name);
// A copy of `name` that lives for ever, for scope names made at run time.
const char* intern(const std::string& name);
// Nanoseconds since the profiler was first used (monotonic).
uint64_t nowNs();

class Scope {
 public:
  explicit Scope(const char* name);
  ~Scope();
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  const char* name_;  // nullptr = profiler was off when the scope opened
  uint64_t start_ = 0;
};

// A number to plot beside the timings (units alive, draw calls, bytes).
// The last value set in a frame is the frame's value.
void counter(const char* name, double value);

struct ScopeStat {
  const char* name = "";
  uint32_t calls = 0;
  double ms = 0;      // total inside the scope this frame, all threads
  double selfMs = 0;  // ...minus the scopes nested in it
  double maxMs = 0;   // the longest single call
  int depth = 0;      // nesting depth where it was first seen
  bool mainThread = true;
};

struct CounterStat {
  const char* name = "";
  double value = 0;
};

struct Event {
  const char* name = "";
  uint64_t startNs = 0, endNs = 0;
  uint16_t depth = 0;
  uint16_t thread = 0;  // index into threadNames()
};

struct FrameStats {
  uint64_t index = 0;
  double ms = 0;                    // beginFrame to beginFrame
  std::vector<ScopeStat> scopes;    // longest first
  std::vector<CounterStat> counters;
  std::vector<Event> events;        // every scope of the frame, by start time (kept for the newest frames only)
};

// Main thread, once per frame (or per tick in a headless run).
void beginFrame();
// The frame that just ended. Empty until two beginFrame() calls were made.
const FrameStats& lastFrame();
// Up to `max` most recent frames, oldest first (events only in the last one).
std::vector<const FrameStats*> history(size_t max = 240);
// Mean and worst frame time over the kept history, in milliseconds.
void frameTimes(double& mean, double& worst, double& best);
// Time a named scope took in the last frame (0 if it didn't run).
double scopeMs(const char* name);
std::vector<std::string> threadNames();

// --- Captures ---------------------------------------------------------------
// Keeps every scope of the next `frames` frames (turns the profiler on for
// that long if it was off).
void startCapture(int frames);
bool capturing();
int captureFramesLeft();
// True once a capture has finished and can be saved.
bool captureReady();
// The capture as Chrome trace JSON; clears nothing.
std::string traceJson();
bool saveTrace(const std::string& path);
void discardCapture();

// Forgets history and captures (tests).
void reset();

}  // namespace pioneer::prof

#define PNR_PROF_CAT2(a, b) a##b
#define PNR_PROF_CAT(a, b) PNR_PROF_CAT2(a, b)
#define PNR_PROFILE(name) ::pioneer::prof::Scope PNR_PROF_CAT(pnrProfScope_, __LINE__)(name)
#define PNR_PROFILE_FUNCTION() PNR_PROFILE(__func__)
#define PNR_COUNTER(name, value) ::pioneer::prof::counter(name, static_cast<double>(value))

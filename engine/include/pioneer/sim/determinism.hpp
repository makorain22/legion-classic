#pragma once
// Pioneer Engine - the determinism checker.
//
// The simulation promises that the same commands give the same state on
// every machine. These tools hold it to that:
//
// * HashTrace: the state checksum at regular ticks. A replay carries the
//   trace of the game it recorded; playing it back compares as it goes and
//   reports the tick where the two part ways. Two machines in a network game
//   compare their traces the same way.
// * twinCheck(): saves the running battle, loads it into a second simulation
//   and runs both side by side. Any state that isn't saved, or isn't
//   deterministic, shows up as a difference, and the report names the
//   sections (systems) that differ.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "pioneer/core/bytes.hpp"

namespace pioneer {

class Simulation;

struct HashSample {
  uint32_t tick = 0;
  uint32_t pad = 0;
  uint64_t hash = 0;
};

class HashTrace {
 public:
  struct Mismatch {
    bool found = false;
    uint32_t tick = 0;
    uint64_t expected = 0, got = 0;
  };

  explicit HashTrace(uint32_t interval = 15) : interval_(interval ? interval : 1) {}
  uint32_t interval() const { return interval_; }
  void clear() { samples_.clear(); }
  // Call after every tick: takes a sample when the tick count is a multiple of the interval.
  void record(Simulation& sim);
  const std::vector<HashSample>& samples() const { return samples_; }
  void setSamples(std::vector<HashSample> s) { samples_ = std::move(s); }
  bool empty() const { return samples_.empty(); }

  // Is the simulation where this trace says it should be at its current tick?
  // (No sample for that tick: nothing to say, no mismatch.)
  Mismatch check(Simulation& sim) const;
  // The first tick two traces disagree on (ticks only one of them has are skipped).
  static Mismatch compare(const HashTrace& expected, const HashTrace& got);

  void write(ByteWriter& w) const;
  bool read(ByteReader& r);

 private:
  uint32_t interval_;
  std::vector<HashSample> samples_;  // by tick, rising
};

struct TwinReport {
  bool ok = true;
  bool saveRoundTrip = true;          // saving the loaded copy gave the same bytes, section by section
  uint32_t ticksRun = 0;
  uint32_t tick = 0;                  // where they parted (when !ok)
  std::vector<std::string> sections;  // the sections that differ
  std::string text;                   // the report in words
};

// Saves `sim`, loads the state into a twin on a copy of the map and runs both
// for `ticks` ticks, comparing the state checksum after each. `drive` (if
// given) is called for each of them before every tick, to queue the same
// commands into both. `sim` itself moves on by `ticks`.
TwinReport twinCheck(Simulation& sim, uint32_t ticks,
                     const std::function<void(Simulation&, uint32_t tick)>& drive = {});

}  // namespace pioneer

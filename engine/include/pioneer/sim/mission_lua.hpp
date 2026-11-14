#pragma once
// Pioneer Engine - Lua for missions.
//
// A mission can carry a Lua 5.4 script beside its triggers (the script's
// "lua" text). It runs inside the simulation, so it must be as repeatable as
// the rest: the same on every machine, in a replay and after loading a save.
// The sandbox sees to that:
//
// * No files, no clock, no operating system, no loading of code. The only
//   libraries are a vetted base, string, table and a small math.
// * pairs() goes through keys in a fixed order (numbers, then strings);
//   tables and functions are never printed with their addresses; ^ is done
//   with multiplications; random numbers come from the simulation's dice.
// * A call from the engine may run a bounded number of steps and the script
//   a bounded amount of memory: a runaway script is stopped and reported, it
//   never hangs the game.
// * Nothing of the Lua state is saved but the global table `state` (plain
//   values and tables) and the timers set with after(). On loading, the
//   script is run again to define its functions, then `state` is put back.
//   So: keep what must survive in `state`; define functions at the top
//   level; act on the world only from handlers.
//
// The script talks to the game through the script registry: every action is
// a function taking a table of its fields (spawn_units{player = 1, area =
// "North road", unit = "rus_rifleman", value = 8}) and every condition a
// function of the table `is` (is.units_in_area{player = 0, area = "Bridge"}).
// The engine calls the script's handlers if it defines them: on_start(),
// on_second(), on_unit_died(type, owner, x, y), on_phase(number, name),
// on_countdown(name), on_wave(name, units, owner), on_defeated(player),
// on_objectives(); triggers call any function with the Call script action
// and ask one with the Script says condition.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pioneer/core/bytes.hpp"

namespace pioneer {

class Simulation;
struct SimEvent;

class MissionLua {
 public:
  static constexpr size_t kMemoryLimit = 32u << 20;       // bytes
  static constexpr int64_t kStepLimit = 5'000'000;         // VM instructions per call from the engine

  explicit MissionLua(Simulation& sim);
  ~MissionLua();
  MissionLua(const MissionLua&) = delete;
  MissionLua& operator=(const MissionLua&) = delete;

  // Does the source compile? (For editors: no script is run.)
  static bool compiles(const std::string& source, std::string& error);

  // Runs the source to define its functions. False and the error when it fails.
  bool load(const std::string& source, std::string& error);
  bool ready() const;

  void step();                                        // on_start once, timers that are due, on_second
  void deliver(const std::vector<SimEvent>& events);  // the tick's events to the handlers
  void call(const std::string& fn, int32_t value);    // Call script
  bool ask(const std::string& fn);                    // Script says
  // Runs a piece of code now (the console; breaks replays). The result or the error as text.
  std::string run(const std::string& code);

  // What survives a save: whether it has started, the timers, the `state` table.
  void save(ByteWriter& w) const;
  bool restore(ByteReader& r);

  const std::vector<std::string>& errors() const;  // the latest errors (at most 20)
  size_t memory() const;
  uint64_t stepsUsed() const;  // by the last call

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace pioneer

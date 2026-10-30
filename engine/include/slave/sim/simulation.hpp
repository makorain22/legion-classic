#pragma once
// Slave Engine - the deterministic simulation.
//
// Everything that changes the game world (player orders, the editor, the AI)
// arrives as a Command and is applied at the start of the next tick. The
// simulation runs at a fixed 15 ticks per second and uses integer maths only.

#include <cstdint>
#include <vector>

#include "slave/core/rng.hpp"
#include "slave/ecs/registry.hpp"
#include "slave/map/map.hpp"
#include "slave/math/vec.hpp"
#include "slave/sim/components.hpp"
#include "slave/sim/unit_types.hpp"

namespace slave {

constexpr int TICKS_PER_SECOND = 15;
constexpr int MAX_PLAYERS = 8;

enum class CommandType : uint8_t {
  Move,    // units -> target
  Stop,    // units stop
  Spawn,   // `count` units of unitType for player at target, facing `facing`
  Remove,  // delete units
};

struct Command {
  CommandType type = CommandType::Move;
  uint8_t player = 0;
  Vec2i target;  // leptons
  uint16_t unitType = 0;
  uint16_t count = 1;
  Angle facing = 0;
  std::vector<Entity> units;
};

class Simulation {
 public:
  Simulation(const Map& map, const UnitTypeRegistry& types, uint64_t seed);

  void queue(Command cmd) { pending_.push_back(std::move(cmd)); }
  void tick();

  uint32_t tickCount() const { return tick_; }
  Registry& registry() { return reg_; }
  const UnitTypeRegistry& types() const { return types_; }
  const Map& map() const { return map_; }

  // Deterministic checksum of the whole simulation state. Two runs given the
  // same commands must always produce the same hash.
  uint64_t stateHash();

  size_t unitCount() { return reg_.pool<CUnit>().size(); }

 private:
  Entity spawnUnit(uint16_t type, uint8_t player, Vec2i pos, Angle facing);
  void applyCommand(const Command& cmd);
  void applyMove(const Command& cmd);
  void stepMovement();
  Vec2i clampToMap(Vec2i p, int32_t margin) const;

  const Map& map_;
  const UnitTypeRegistry& types_;
  Registry reg_;
  Rng rng_;
  uint32_t tick_ = 0;
  std::vector<Command> pending_;
};

}  // namespace slave

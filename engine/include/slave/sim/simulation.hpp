#pragma once
// Slave Engine - the deterministic simulation.
//
// Everything that changes the game world (player orders, the editor, the AI)
// arrives as a Command and is applied at the start of the next tick. The
// simulation runs at a fixed 15 ticks per second and uses integer maths only.
//
// Each tick: commands, path requests, orders, movement, collision, vision
// (every 4 ticks), targeting and firing, projectiles, then removal of the dead.
// What happened (shots, impacts, deaths) is reported as events, which the
// presentation turns into flashes, explosions and wrecks.

#include <cstdint>
#include <vector>

#include "slave/core/rng.hpp"
#include "slave/ecs/registry.hpp"
#include "slave/map/map.hpp"
#include "slave/math/vec.hpp"
#include "slave/sim/components.hpp"
#include "slave/sim/pathfinder.hpp"
#include "slave/sim/spatial_hash.hpp"
#include "slave/sim/unit_types.hpp"
#include "slave/sim/vision.hpp"
#include "slave/sim/weapons.hpp"

namespace slave {

constexpr int TICKS_PER_SECOND = 15;
constexpr int MAX_PLAYERS = 8;

enum class CommandType : uint8_t {
  Move,        // units -> target
  AttackMove,  // units -> target, fighting anything met on the way
  Attack,      // units attack targetUnit
  Stop,        // units stop
  Spawn,       // `count` units of unitType for player at target, facing `facing`
  Remove,      // delete units
};

struct Command {
  CommandType type = CommandType::Move;
  uint8_t player = 0;
  Vec2i target;  // leptons
  Entity targetUnit;
  uint16_t unitType = 0;
  uint16_t count = 1;
  Angle facing = 0;
  std::vector<Entity> units;
};

enum class EventType : uint8_t { Fire, Impact, UnitDied };

struct SimEvent {
  EventType type = EventType::Fire;
  uint8_t owner = 0;
  uint16_t weapon = 0;    // Fire / Impact
  uint16_t unitType = 0;  // UnitDied
  Vec2i from;             // Fire: muzzle position
  Vec2i at;               // Fire: aim point; Impact / UnitDied: position
  Angle facing = 0;       // UnitDied: body facing
  Angle turret = 0;       // UnitDied: turret facing
  bool hit = true;        // Impact: did it hit a unit
};

struct SimStats {
  // Milliseconds spent in each step during the last tick (for profiling).
  float msPaths = 0, msOrders = 0, msMove = 0, msSpatial = 0, msSeparation = 0, msVision = 0, msTargeting = 0,
        msWeapons = 0, msProjectiles = 0;
  uint32_t pathSearches = 0;
  uint64_t pathExpansions = 0;
  uint32_t pathQueue = 0;
  uint32_t projectiles = 0;
};

class Simulation {
 public:
  Simulation(const Map& map, const UnitTypeRegistry& types, const WeaponRegistry& weapons, uint64_t seed);

  void queue(Command cmd) { pending_.push_back(std::move(cmd)); }
  void tick();

  uint32_t tickCount() const { return tick_; }
  Registry& registry() { return reg_; }
  const UnitTypeRegistry& types() const { return types_; }
  const WeaponRegistry& weapons() const { return weapons_; }
  const Map& map() const { return map_; }
  const Vision& vision() const { return vision_; }
  Vision& vision() { return vision_; }
  Pathfinder& pathfinder() { return pathfinder_; }
  const std::vector<SimEvent>& events() const { return events_; }  // from the last tick
  const SimStats& stats() const { return stats_; }

  // Deterministic checksum of the whole simulation state. Two runs given the
  // same commands must always produce the same hash.
  uint64_t stateHash();

  size_t unitCount() { return reg_.pool<CUnit>().size(); }
  int32_t maxRange(const UnitTypeDef& def) const;

 private:
  struct PathRequest {
    Entity unit;
    Vec2i goal;
    uint32_t id;
  };

  // commands (simulation.cpp)
  Entity spawnUnit(uint16_t type, uint8_t player, Vec2i pos, Angle facing);
  void applyCommand(const Command& cmd);
  void orderMove(const std::vector<Entity>& units, Vec2i target, OrderType type);
  void setOrderIdle(Entity e);

  // movement (movement.cpp)
  void requestPath(Entity e, Vec2i goal);
  void setPath(Entity e, std::vector<Vec2i> path);
  void processPathQueue();
  void stepOrders();
  void stepMovement();
  void rebuildSpatial();
  void stepSeparation();
  void stepVision();

  // combat (combat.cpp)
  void stepTargeting();
  void stepWeapons();
  void stepProjectiles();
  void impact(int weapon, uint8_t owner, Vec2i at, Entity directTarget);
  void damage(uint32_t victim, int32_t amount);
  bool canSee(uint8_t player, int32_t x, int32_t y) const;
  void removeDead();

  const Map& map_;
  const UnitTypeRegistry& types_;
  const WeaponRegistry& weapons_;
  Registry reg_;
  Rng rng_;
  uint32_t tick_ = 0;
  std::vector<Command> pending_;
  std::vector<SimEvent> events_;
  Pathfinder pathfinder_;
  SpatialHash spatial_;
  Vision vision_;
  std::vector<PathRequest> pathQueue_;
  size_t pathQueueHead_ = 0;
  uint32_t nextRequest_ = 1;
  std::vector<uint32_t> dead_;
  std::vector<int32_t> pushX_, pushY_;
  SimStats stats_;
};

}  // namespace slave

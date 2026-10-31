#pragma once
// Slave Engine - simulation components. Integer data only.

#include <array>
#include <cstdint>
#include <vector>

#include "slave/core/fixed.hpp"
#include "slave/ecs/registry.hpp"
#include "slave/math/vec.hpp"

namespace slave {

struct CPosition {
  int32_t x = 0, y = 0;  // leptons
};

// Position at the start of the current tick, for smooth rendering in between.
struct CPrevPosition {
  int32_t x = 0, y = 0;
};

struct CFacing {
  Angle angle = 0;       // body direction; always a multiple of 256 for turning units
  uint8_t turnWait = 0;  // ticks until the next 1/16 turn step
};

// How a unit behaves on its own.
enum class Stance : uint8_t {
  Aggressive = 0,  // fights back when shot, even mid-march, and hunts attackers it can't reach
  HoldGround,      // returns fire from where it stands; never moves on its own
  HoldFire,        // never shoots unless ordered to attack (ambushes)
  Count
};

struct CUnit {
  uint16_t type = 0;  // index into UnitTypeRegistry
  uint8_t owner = 0;  // player slot 0..7
  int32_t health = 100;
  Stance stance = Stance::Aggressive;
};

struct CTurret {
  Angle angle = 0;
  uint8_t wait = 0;
};

enum class OrderType : uint8_t { Idle = 0, Move, AttackMove, Attack, Harvest, Repair };

struct COrder {
  OrderType type = OrderType::Idle;
  Vec2i goal;      // Move / AttackMove destination
  Entity target;   // Attack target
  Vec2i chasedAt;  // where the target was when we last planned a path to it
  int32_t faceTo = -1;  // Angle to turn to on arrival (formation front), -1 = none
  bool forced = false;  // forced march: ignore enemy fire until arrival
};

struct CMover {
  std::vector<Vec2i> path;  // waypoints in leptons
  uint32_t index = 0;
  bool active = false;
  uint32_t request = 0;     // pending path request id (0 = none)
  Vec2i requestGoal;
  int32_t bestDist = INT32_MAX;
  uint16_t stuckTicks = 0;
  uint8_t repaths = 0;
  uint8_t failures = 0;      // consecutive failed path requests
  uint32_t retryTick = 0;    // don't ask for a new path before this tick
};

struct CCombat {
  std::array<uint16_t, 2> cooldown{};
  Entity target;
  Entity hurtBy;          // who last damaged us
  uint32_t hurtTick = 0;  // when
};

// A building, gun emplacement or fortification on the tile grid.
struct CStructure {
  int32_t tx = 0, ty = 0;  // top-left tile of the footprint
  int32_t w = 1, h = 1;
  uint32_t builtTick = 0;  // for the build-up animation
};

// Structures that turn out units: where new units head.
struct CProducer {
  bool hasRally = false;
  Vec2i rally;
  bool primary = false;  // new units of this kind come out here
};

// Supply trucks: pump oil from fields and drive it to a refinery.
enum class HarvestState : uint8_t { FindField, ToField, Pumping, FindRefinery, ToRefinery, Unloading };
struct CHarvester {
  HarvestState state = HarvestState::FindField;
  int32_t cargo = 0;  // oil units on board
  Vec2i field{-1, -1};  // tile being worked
  Entity refinery;
  uint16_t timer = 0;
  uint16_t idleTicks = 0;  // nothing to do: wait a while before looking again
};

// Low-frame-rate walk/drive animation.
struct CAnim {
  uint8_t frame = 0;
  uint8_t timer = 0;
  bool moving = false;
};

// Turns `a` one facing step toward `desired` when its wait runs out.
// Returns true once `a` points the same way (to the nearest 1/16 turn).
inline bool turnToward(Angle& a, Angle desired, uint8_t& wait, int32_t turnTicks) {
  if (facingOf(a) == facingOf(desired)) return true;
  if (turnTicks <= 0) {
    a = angleOfFacing(facingOf(desired));
    return true;
  }
  if (wait > 0) {
    --wait;
    return false;
  }
  a = angleOfFacing(facingOf(a) + (angleDelta(a, desired) > 0 ? 1 : -1));
  wait = static_cast<uint8_t>(turnTicks - 1);
  return facingOf(a) == facingOf(desired);
}

struct CProjectile {
  Vec2i from, to;
  int32_t x = 0, y = 0;
  int32_t travelled = 0, total = 0;
  uint16_t weapon = 0;
  uint8_t owner = 0;
  uint16_t delay = 0;  // ticks before it starts flying (artillery barrages)
  Entity shooter;      // who fired it (for return fire)
};

}  // namespace slave

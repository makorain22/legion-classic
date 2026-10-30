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

struct CUnit {
  uint16_t type = 0;  // index into UnitTypeRegistry
  uint8_t owner = 0;  // player slot 0..7
  int32_t health = 100;
};

struct CTurret {
  Angle angle = 0;
  uint8_t wait = 0;
};

enum class OrderType : uint8_t { Idle = 0, Move, AttackMove, Attack };

struct COrder {
  OrderType type = OrderType::Idle;
  Vec2i goal;      // Move / AttackMove destination
  Entity target;   // Attack target
  Vec2i chasedAt;  // where the target was when we last planned a path to it
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
};

// Low-frame-rate walk/drive animation.
struct CAnim {
  uint8_t frame = 0;
  uint8_t timer = 0;
  bool moving = false;
};

struct CProjectile {
  Vec2i from, to;
  int32_t x = 0, y = 0;
  int32_t travelled = 0, total = 0;
  uint16_t weapon = 0;
  uint8_t owner = 0;
};

}  // namespace slave

#pragma once
// Slave Engine - simulation components. Integer data only.

#include <cstdint>

#include "slave/core/fixed.hpp"

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
  uint16_t type = 0;   // index into UnitTypeRegistry
  uint8_t owner = 0;   // player slot 0..7
  int32_t health = 100;
};

struct CMoveOrder {
  int32_t tx = 0, ty = 0;
  bool active = false;
};

// Low-frame-rate walk/drive animation.
struct CAnim {
  uint8_t frame = 0;
  uint8_t timer = 0;
  bool moving = false;
};

}  // namespace slave

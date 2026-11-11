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
  uint8_t damageFrac = 0;  // hundredths of a hit point taken but not yet counted (rifles on armour)
};

// --- Unit organisation ---------------------------------------------------------
// Infantry: fireteam (5), squad (10), platoon (20), company (100).
// Vehicles: platoon (5), company (15), battalion (50).
enum class Echelon : uint8_t { None = 0, Fireteam, Squad, Platoon, Company, Battalion };

// A unit's membership of a group (fireteam, platoon...).
struct CGroupMember {
  Entity group;
};

// A group entity: a bound fighting unit. It has no position; its members do.
struct CGroup {
  uint8_t owner = 0;
  bool vehicles = false;      // an armoured unit (else infantry); also set for combined arms
  bool combined = false;      // vehicles and infantry together (a battle group)
  Echelon echelon = Echelon::Fireteam;
  uint16_t number = 1;        // 1st, 2nd... per side and echelon, for its name
  std::vector<Entity> members;
};

// Infantry inside a building, bunker or other shelter.
struct CGarrisoned {
  Entity building;
};

// A structure that infantry can occupy.
struct CGarrison {
  std::vector<Entity> occupants;
};

struct CTurret {
  Angle angle = 0;
  uint8_t wait = 0;
};

enum class OrderType : uint8_t { Idle = 0, Move, AttackMove, Attack, Harvest, Repair, Occupy };

struct COrder {
  OrderType type = OrderType::Idle;
  Vec2i goal;      // Move / AttackMove destination
  Entity target;   // Attack target
  Vec2i chasedAt;  // where the target was when we last planned a path to it
  int32_t faceTo = -1;  // Angle to turn to on arrival (formation front), -1 = none
  bool forced = false;  // forced march: ignore enemy fire until arrival
};

// --- Formations (9.5) ------------------------------------------------------------
// A group ordered to move together marches as one body: a frame (CFormation)
// travels along the route at the pace of its slowest member and every member
// keeps its slot in that frame. The component stays on a unit after it
// arrives (with no formation), remembering the shape it was last given.
struct CFormationMember {
  enum : uint8_t { kJoined = 1, kDetour = 2 };
  Entity formation;   // the march it is part of, or null
  Vec2i offset;       // its slot in the frame: x across the front (right positive), y back from the centre; leptons
  Vec2i goal;         // its final slot (the order's goal): a different goal means a newer order
  uint8_t shape = 0;  // the Formation it was last ordered in
  uint8_t flags = 0;  // kJoined: in place, the body waits for it; kDetour: finding its own way to its slot
  Angle heading = 0;  // the way it faces while it keeps its slot: the body's front, or the road's in a column
};

// A marching formation. It has no position of its own in the unit pools.
struct CFormation {
  uint8_t owner = 0;
  uint8_t shape = 0;
  uint8_t loco = 0;          // Locomotor the route was found for
  bool reverse = false;      // backing up: the frame keeps its front, vehicles use reverse gear
  bool moving = false;       // the frame advanced this tick
  bool pursuing = false;     // attack-move: off its route, closing with an enemy
  std::vector<Vec2i> path;   // the route of the frame's centre
  uint32_t index = 0;
  Vec2i start;               // where the route began (a column trails back along the way it came)
  Vec2i anchor;              // the frame's centre now
  Vec2i dest;                // where the centre ends up
  Vec2i leash;               // where it left its route to pursue
  Angle facing = 0;          // the way the frame's front points now
  Angle finalFacing = 0;
  int32_t remaining = 0;     // leptons of route left
  uint16_t stall = 0;        // ticks spent waiting for laggards
  uint16_t fight = 0;        // ticks it has been in contact (it stands to fight at first, then presses on)
  std::vector<Entity> members;
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

// Supply trucks (9.6): they run between a logistics office and their side's
// headquarters, loading at the office and unloading money at the yard.
enum class HarvestState : uint8_t {
  Seek = 0,   // looking for an office of its side with room for a truck
  ToOffice,   // driving out to it
  Loading,    // standing at its door, loading
  ToYard,     // driving the load to the nearest construction yard
  Unloading,  // at the yard: the load is paid in
};
struct CHarvester {
  HarvestState state = HarvestState::Seek;
  int32_t cargo = 0;       // supplies on board
  Entity office;           // the logistics office it works for
  Entity yard;             // the yard it is driving to
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

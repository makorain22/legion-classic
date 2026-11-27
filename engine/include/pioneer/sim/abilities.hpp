#pragma once
// Pioneer Engine - unit abilities (Milestone 9.15): an order a unit gives
// itself now and then, with a cooldown, a limited number of uses or a cost -
// a grenade, a smoke screen, a sprint, digging in, a charge, a rally. They are
// data ("abilities" in units.json, listed by id on each unit), shown as
// buttons and used by the computer player. The call-ins of 9.14 are the same
// idea at the level of the army.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "pioneer/math/vec.hpp"

namespace pioneer {

class WeaponRegistry;

enum class AbilityKind : uint8_t {
  Throw = 0,  // one round of `weapon` at a point in `range` (a grenade, a smoke grenade, a satchel charge)
  Barrage,    // `shots` rounds of `weapon` at a point over `duration` (a smoke barrage from the guns)
  Sprint,     // `percent` faster for `duration`
  DigIn,      // `percent` less damage until the unit moves again
  Charge,     // `percent` faster for `duration`, deaf to fire, half as hard again at close quarters; goes for the point
  Rally,      // men within `radius` get up: suppression gone, morale steadied, the broken stop running
  Count
};
const char* abilityKindId(AbilityKind k);
bool abilityKindFromId(const std::string& id, AbilityKind& out);
// What the player points at: nothing (it acts at once), or a point.
bool abilityNeedsTarget(AbilityKind k);

struct AbilityDef {
  std::string id;
  std::string name;
  std::string description;
  AbilityKind kind = AbilityKind::Throw;
  std::string weaponId;  // Throw, Barrage
  int weapon = -1;
  int32_t range = 4 * 256;      // leptons (Throw, Barrage, Charge)
  int32_t cooldown = 30 * 15;   // ticks
  int32_t charges = 0;          // uses a battle (0: no limit)
  int32_t cost = 0;             // supplies a use
  int32_t shots = 1;            // Barrage
  int32_t duration = 6 * 15;    // ticks (Barrage, Sprint, Charge)
  int32_t percent = 50;         // Sprint/Charge: faster by; DigIn: less damage by
  int32_t radius = 5 * 256;     // Rally (leptons)
  std::string icon;             // a short label for the button ("Grenade")
};

class AbilityRegistry {
 public:
  bool loadJson(const nlohmann::json& list, std::string& error);
  bool resolve(const WeaponRegistry& weapons, std::string& error);
  int find(const std::string& id) const;
  const AbilityDef& get(int i) const { return defs_[static_cast<size_t>(i)]; }
  size_t size() const { return defs_.size(); }
  const std::vector<AbilityDef>& all() const { return defs_; }

 private:
  std::vector<AbilityDef> defs_;
};

constexpr int kMaxAbilities = 4;  // per unit type

// A unit's abilities in use: cooldowns, uses, what is under way. Only units
// that have used one carry it (saved as pool CABL, in the state checksum).
struct CAbilities {
  std::array<uint16_t, kMaxAbilities> cooldown{};  // ticks before each may be used again (by the unit's own list)
  std::array<uint8_t, kMaxAbilities> used{};       // times used
  uint32_t activeUntil = 0;  // a sprint, a charge or a barrage lasts until this tick (dug in: until it moves)
  Vec2i pendingAt;           // a throw or barrage waiting for the unit to get in range, or a barrage's target
  uint8_t active = 0;        // slot + 1 of what is under way (0: nothing)
  uint8_t pending = 0;       // slot + 1 of a throw or barrage waiting to get in range (0: none)
  uint8_t shotsLeft = 0;     // a barrage's rounds still to fire
  uint8_t pad = 0;
};

}  // namespace pioneer

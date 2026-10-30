#pragma once
// Slave Engine - unit type definitions, loaded from content/rules/*.json.
// The engine never hard-codes unit names; everything comes from data.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "slave/sim/weapons.hpp"

namespace slave {

enum class Locomotor : uint8_t { Foot, Wheeled, Tracked };

struct UnitTypeDef {
  std::string id;           // "ger_medium_tank"
  std::string name;         // "Panzerkampfwagen M" (shown to players)
  std::string faction;      // "ger"
  std::string model;        // placeholder voxel model name
  Locomotor locomotor = Locomotor::Tracked;
  int32_t speed = 20;       // leptons per tick (15 ticks per second)
  int32_t turnTicks = 2;    // ticks per 1/16 turn; 0 = turns instantly
  int32_t radius = 96;      // selection / spacing radius in leptons
  int32_t animTicks = 2;    // ticks per animation frame while moving
  int32_t maxHealth = 100;
  ArmorClass armor = ArmorClass::Light;
  int32_t sight = 7 * 256;  // leptons
  std::vector<std::string> weaponIds;
  std::vector<int> weapons;  // resolved indices into WeaponRegistry
  bool turret = false;       // weapons on a turret that turns on its own
  int32_t turretTurnTicks = 1;
  bool infantry() const { return locomotor == Locomotor::Foot; }
};

class UnitTypeRegistry {
 public:
  // Adds every unit in a {"units": [...]} JSON document. Returns false and
  // fills `error` on bad data.
  bool loadJson(const nlohmann::json& doc, std::string& error);
  bool loadFile(const std::string& path, std::string& error);
  // Looks up every unit's weapon ids. Call after loading weapons.
  bool resolveWeapons(const WeaponRegistry& weapons, std::string& error);

  int find(const std::string& id) const;  // -1 if missing
  const UnitTypeDef& get(int index) const { return types_[static_cast<size_t>(index)]; }
  size_t size() const { return types_.size(); }
  const std::vector<UnitTypeDef>& all() const { return types_; }

 private:
  std::vector<UnitTypeDef> types_;
};

}  // namespace slave

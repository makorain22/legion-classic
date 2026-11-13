#pragma once
// Pioneer Engine - unit type definitions, loaded from content/rules/*.json.
// The engine never hard-codes unit names; everything comes from data.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "pioneer/sim/weapons.hpp"

namespace pioneer {

enum class Locomotor : uint8_t { Foot, Wheeled, Tracked };

// What a type is, which decides where it is built and how it is used.
enum class UnitCategory : uint8_t {
  Infantry = 0,   // trained at a barracks
  Vehicle,        // built at a factory
  Building,       // built by the construction yard, placed when ready
  Defense,        // guns and bunkers, built by the construction yard
  Fortification,  // trenches, sandbags, wire, tank traps: bought and placed directly
  Count
};
const char* categoryName(UnitCategory c);

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
  ArmorClass armor = ArmorClass::Soft;
  int32_t sight = 7 * 256;  // leptons
  std::vector<std::string> weaponIds;
  std::vector<int> weapons;  // resolved indices into WeaponRegistry
  bool turret = false;       // weapons on a turret that turns on its own
  int32_t turretTurnTicks = 1;
  UnitCategory category = UnitCategory::Vehicle;

  // Economy.
  int32_t cost = 0;          // supplies
  int32_t buildTime = 150;   // ticks at full power
  int32_t power = 0;         // structures: + produced, - consumed
  std::string builtAt;       // role of the structure that makes it: "yard", "barracks", "factory"
  std::vector<std::string> requiresIds;  // the owner needs one of each of these first
  std::vector<int> prereqs;              // resolved
  int32_t capacity = 0;      // supply trucks: supplies a truck carries from a logistics office to the HQ per trip
  std::string deploysToId;   // construction truck: the structure it unfolds into
  int deploysTo = -1;
  std::string freeUnitId;    // a logistics office comes with its supply trucks
  int freeUnit = -1;
  int32_t convoy = 0;        // logistics office: how many supply trucks run from it (it arrives with that many)
  int32_t perYard = 0;       // structures: how many of this type a side may have per construction yard (0 = any number)
  int32_t auraRadius = 0;    // structures: friends within this many leptons fire faster...
  int32_t auraFireRate = 0;  // ...by this many percent (the best aura counts, they don't add up)
  std::string nato;          // map symbol for unit cards and chips ("infantry", "armour"...)
  bool scout = false;        // reconnaissance ("nato": "recon"): falls back from enemies that come close
  int32_t garrison = 0;      // structures: infantry it can hold
  int32_t income = 0;        // supplies per minute it earns (post office, warehouse, fuel depot; a yard's trickle)
  int32_t repair = 0;        // engineers: hit points restored per tick to friendly machines and works
  int32_t aiWeight = 1;      // how often the computer picks it (relative), 0 = never
  std::string group;         // heading it is listed under in the sidebar ("Tanks", "Support"...)
  std::string description;   // a line or two for the encyclopedia

  // Structures (footW > 0).
  int32_t footW = 0, footH = 0;  // footprint in tiles
  uint8_t blocks = 0;            // locomotors it stops (bit per Locomotor)
  std::string role;              // what it provides: "yard", "barracks", "factory", "power", "post", "warehouse", "fuel", "office"
  int32_t buildRadius = 0;       // tiles around it where the owner may build
  uint8_t cover = 0;             // % less damage for infantry standing in it (trenches)
  bool bridge = false;           // a bridge piece: anything may cross its tile, water or not
  uint8_t coverAround = 0;       // % less damage for infantry right next to it (sandbags)
  bool autoTarget = true;        // enemies attack it of their own accord
  bool targetable = true;        // can be attacked at all
  bool hidden = false;           // deleted by a rules change mid-battle: kept for what exists, never built

  bool isStructure() const { return footW > 0; }
  bool infantry() const { return locomotor == Locomotor::Foot && !isStructure(); }
};

class UnitTypeRegistry {
 public:
  // Adds every unit in a {"units": [...]} JSON document. Returns false and
  // fills `error` on bad data.
  bool loadJson(const nlohmann::json& doc, std::string& error);
  bool loadFile(const std::string& path, std::string& error);
  // Looks up every unit's weapon ids. Call after loading weapons.
  bool resolveWeapons(const WeaponRegistry& weapons, std::string& error);
  // Looks up references between types (requirements, deploy targets, free
  // units). Call after loading every unit and building file.
  bool resolveLinks(std::string& error);

  int find(const std::string& id) const;  // -1 if missing
  const UnitTypeDef& get(int index) const { return types_[static_cast<size_t>(index)]; }
  size_t size() const { return types_.size(); }
  const std::vector<UnitTypeDef>& all() const { return types_; }

 private:
  std::vector<UnitTypeDef> types_;
};

// Builds both registries from the three merged rule documents (weapons.json,
// units.json, buildings.json, in that order) and checks every link between
// them. Errors name the file. Used by the game, the rules editor and tests.
bool loadRuleSet(const nlohmann::json& weapons, const nlohmann::json& units, const nlohmann::json& buildings,
                 WeaponRegistry& weaponsOut, UnitTypeRegistry& typesOut, std::string& error);

}  // namespace pioneer

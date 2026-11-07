#include "slave/sim/unit_types.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace slave {

const char* categoryName(UnitCategory c) {
  static const char* names[] = {"Infantry", "Vehicles", "Buildings", "Defences", "Fortifications"};
  return names[static_cast<int>(c) % static_cast<int>(UnitCategory::Count)];
}

namespace {
bool parseCategory(const std::string& s, UnitCategory& out) {
  if (s == "infantry") out = UnitCategory::Infantry;
  else if (s == "vehicle") out = UnitCategory::Vehicle;
  else if (s == "building") out = UnitCategory::Building;
  else if (s == "defense" || s == "defence") out = UnitCategory::Defense;
  else if (s == "fortification") out = UnitCategory::Fortification;
  else return false;
  return true;
}

bool parseLocomotor(const std::string& s, Locomotor& out) {
  if (s == "foot") out = Locomotor::Foot;
  else if (s == "wheeled") out = Locomotor::Wheeled;
  else if (s == "tracked") out = Locomotor::Tracked;
  else return false;
  return true;
}
}  // namespace

bool UnitTypeRegistry::loadJson(const nlohmann::json& doc, std::string& error) {
  if (!doc.contains("units") || !doc["units"].is_array()) {
    error = "missing \"units\" array";
    return false;
  }
  for (const auto& u : doc["units"]) {
    UnitTypeDef d;
    try {
      d.id = u.at("id").get<std::string>();
      d.name = u.value("name", d.id);
      d.faction = u.value("faction", "");
      d.model = u.at("model").get<std::string>();
      d.speed = u.value("speed", d.speed);
      d.turnTicks = u.value("turnTicks", d.turnTicks);
      d.radius = u.value("radius", d.radius);
      d.animTicks = u.value("animTicks", d.animTicks);
      d.maxHealth = u.value("health", d.maxHealth);
      if (!parseArmor(u.value("armor", std::string("light")), d.armor)) {
        error = "unit '" + d.id + "': unknown armor";
        return false;
      }
      if (u.contains("sight")) d.sight = static_cast<int32_t>(u["sight"].get<double>() * 256.0 + 0.5);
      if (u.contains("weapons")) d.weaponIds = u["weapons"].get<std::vector<std::string>>();
      if (u.contains("turret")) {
        d.turret = true;
        d.turretTurnTicks = u["turret"].value("turnTicks", 1);
      }
      if (!parseLocomotor(u.value("locomotor", "tracked"), d.locomotor)) {
        error = "unit '" + d.id + "': unknown locomotor";
        return false;
      }
      // Economy.
      d.cost = u.value("cost", 0);
      if (u.contains("buildTime")) d.buildTime = static_cast<int32_t>(u["buildTime"].get<double>() * 15.0 + 0.5);
      d.power = u.value("power", 0);
      d.builtAt = u.value("builtAt", "");
      if (u.contains("requires")) d.requiresIds = u["requires"].get<std::vector<std::string>>();
      d.capacity = u.value("capacity", 0);
      d.deploysToId = u.value("deploysTo", "");
      d.freeUnitId = u.value("freeUnit", "");
      d.repair = u.value("repair", 0);
      d.garrison = u.value("garrison", 0);
      d.nato = u.value("nato", std::string());
      d.income = u.value("income", 0);
      d.aiWeight = u.value("aiWeight", 1);
      d.group = u.value("group", std::string());
      d.description = u.value("description", std::string());
      // Structures.
      if (u.contains("footprint")) {
        d.footW = u["footprint"].at(0).get<int32_t>();
        d.footH = u["footprint"].at(1).get<int32_t>();
        if (d.footW < 1 || d.footH < 1 || d.footW > 8 || d.footH > 8) {
          error = "unit '" + d.id + "': footprint must be 1..8 tiles a side";
          return false;
        }
        d.speed = 0;
        d.turnTicks = 0;
        if (!u.contains("radius")) d.radius = std::max(d.footW, d.footH) * 128;
        d.blocks = 0x7;  // structures stop everyone unless told otherwise
        d.sight = static_cast<int32_t>(u.value("sight", 4.0) * 256.0 + 0.5);
      }
      if (u.contains("blocks")) {
        d.blocks = 0;
        for (const auto& b : u["blocks"]) {
          Locomotor l;
          if (!parseLocomotor(b.get<std::string>(), l)) {
            error = "unit '" + d.id + "': unknown locomotor in blocks";
            return false;
          }
          d.blocks |= static_cast<uint8_t>(1u << static_cast<int>(l));
        }
      }
      d.role = u.value("role", "");
      d.buildRadius = u.value("buildRadius", 0);
      d.cover = static_cast<uint8_t>(std::clamp(u.value("cover", 0), 0, 90));
      d.coverAround = static_cast<uint8_t>(std::clamp(u.value("coverAround", 0), 0, 90));
      d.bridge = u.value("bridge", false);
      if (d.bridge && !u.contains("blocks")) d.blocks = 0;  // a road over the water
      d.autoTarget = u.value("autoTarget", true);
      d.targetable = u.value("targetable", true);
      d.hidden = u.value("hidden", false);
      d.category = d.isStructure() ? UnitCategory::Building
                   : d.locomotor == Locomotor::Foot ? UnitCategory::Infantry : UnitCategory::Vehicle;
      if (u.contains("category") && !parseCategory(u["category"].get<std::string>(), d.category)) {
        error = "unit '" + d.id + "': unknown category";
        return false;
      }
    } catch (const nlohmann::json::exception& e) {
      error = std::string("bad unit entry: ") + e.what();
      return false;
    }
    if ((!d.isStructure() && (d.speed <= 0 || d.speed > 256)) || d.radius <= 0 || d.animTicks <= 0) {
      error = "unit '" + d.id + "': speed, radius or animTicks out of range";
      return false;
    }
    if (find(d.id) >= 0) {
      error = "duplicate unit id '" + d.id + "'";
      return false;
    }
    types_.push_back(std::move(d));
  }
  return true;
}

bool UnitTypeRegistry::loadFile(const std::string& path, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
  if (doc.is_discarded()) {
    error = path + ": invalid JSON";
    return false;
  }
  if (!loadJson(doc, error)) {
    error = path + ": " + error;
    return false;
  }
  return true;
}

bool UnitTypeRegistry::resolveWeapons(const WeaponRegistry& weapons, std::string& error) {
  for (UnitTypeDef& d : types_) {
    d.weapons.clear();
    for (const std::string& id : d.weaponIds) {
      const int w = weapons.find(id);
      if (w < 0) {
        error = "unit '" + d.id + "' uses unknown weapon '" + id + "'";
        return false;
      }
      d.weapons.push_back(w);
    }
    if (d.weapons.size() > 2) {
      error = "unit '" + d.id + "' has more than 2 weapons";
      return false;
    }
  }
  return true;
}

bool UnitTypeRegistry::resolveLinks(std::string& error) {
  for (UnitTypeDef& d : types_) {
    d.prereqs.clear();
    for (const std::string& id : d.requiresIds) {
      const int r = find(id);
      if (r < 0) {
        error = "'" + d.id + "' requires unknown type '" + id + "'";
        return false;
      }
      d.prereqs.push_back(r);
    }
    d.deploysTo = d.deploysToId.empty() ? -1 : find(d.deploysToId);
    d.freeUnit = d.freeUnitId.empty() ? -1 : find(d.freeUnitId);
    if ((!d.deploysToId.empty() && d.deploysTo < 0) || (!d.freeUnitId.empty() && d.freeUnit < 0)) {
      error = "'" + d.id + "' refers to an unknown type";
      return false;
    }
  }
  return true;
}

bool loadRuleSet(const nlohmann::json& weapons, const nlohmann::json& units, const nlohmann::json& buildings,
                 WeaponRegistry& weaponsOut, UnitTypeRegistry& typesOut, std::string& error) {
  WeaponRegistry w;
  UnitTypeRegistry t;
  if (!w.loadJson(weapons, error)) {
    error = "weapons.json: " + error;
    return false;
  }
  if (!t.loadJson(units, error)) {
    error = "units.json: " + error;
    return false;
  }
  if (!t.loadJson(buildings, error)) {
    error = "buildings.json: " + error;
    return false;
  }
  if (!t.resolveWeapons(w, error) || !t.resolveLinks(error)) return false;
  weaponsOut = std::move(w);
  typesOut = std::move(t);
  return true;
}

int UnitTypeRegistry::find(const std::string& id) const {
  for (size_t i = 0; i < types_.size(); ++i)
    if (types_[i].id == id) return static_cast<int>(i);
  return -1;
}

}  // namespace slave

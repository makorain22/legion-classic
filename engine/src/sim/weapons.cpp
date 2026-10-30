#include "slave/sim/weapons.hpp"

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

namespace slave {

bool parseArmor(const std::string& s, ArmorClass& out) {
  if (s == "infantry") out = ArmorClass::Infantry;
  else if (s == "light") out = ArmorClass::Light;
  else if (s == "medium") out = ArmorClass::Medium;
  else if (s == "heavy") out = ArmorClass::Heavy;
  else return false;
  return true;
}

namespace {
int32_t tilesToLeptons(const nlohmann::json& v) { return static_cast<int32_t>(std::lround(v.get<double>() * 256.0)); }
}  // namespace

bool WeaponRegistry::loadJson(const nlohmann::json& doc, std::string& error) {
  try {
    for (const auto& [id, w] : doc.at("warheads").items()) {
      WarheadDef d;
      d.id = id;
      static const char* names[ARMOR_COUNT] = {"infantry", "light", "medium", "heavy"};
      for (int a = 0; a < ARMOR_COUNT; ++a) d.versus[static_cast<size_t>(a)] = w.value(names[a], 100);
      warheads_.push_back(d);
    }
    for (const auto& w : doc.at("weapons")) {
      WeaponDef d;
      d.id = w.at("id").get<std::string>();
      d.name = w.value("name", d.id);
      d.damage = w.at("damage").get<int32_t>();
      d.range = tilesToLeptons(w.at("range"));
      d.cooldown = w.at("cooldown").get<int32_t>();
      const std::string proj = w.value("projectile", "instant");
      if (proj == "instant") d.projectile = ProjectileKind::Instant;
      else if (proj == "shell") d.projectile = ProjectileKind::Shell;
      else {
        error = "weapon '" + d.id + "': unknown projectile '" + proj + "'";
        return false;
      }
      d.speed = w.value("speed", d.speed);
      if (w.contains("splash")) d.splash = tilesToLeptons(w["splash"]);
      d.accuracy = w.value("accuracy", d.accuracy);
      d.muzzle = w.value("muzzle", uint8_t{1});
      const std::string wh = w.at("warhead").get<std::string>();
      int found = -1;
      for (size_t i = 0; i < warheads_.size(); ++i)
        if (warheads_[i].id == wh) found = static_cast<int>(i);
      if (found < 0) {
        error = "weapon '" + d.id + "': unknown warhead '" + wh + "'";
        return false;
      }
      d.warhead = static_cast<uint16_t>(found);
      if (d.damage < 0 || d.range <= 0 || d.cooldown <= 0 || d.speed <= 0) {
        error = "weapon '" + d.id + "': damage, range, cooldown and speed must be positive";
        return false;
      }
      weapons_.push_back(std::move(d));
    }
  } catch (const nlohmann::json::exception& e) {
    error = std::string("bad weapons data: ") + e.what();
    return false;
  }
  return true;
}

bool WeaponRegistry::loadFile(const std::string& path, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
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

int WeaponRegistry::find(const std::string& id) const {
  for (size_t i = 0; i < weapons_.size(); ++i)
    if (weapons_[i].id == id) return static_cast<int>(i);
  return -1;
}

}  // namespace slave

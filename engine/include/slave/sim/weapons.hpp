#pragma once
// Slave Engine - weapons, warheads and armour, loaded from content/rules/weapons.json.
//
// Damage dealt = weapon damage x warhead-vs-armour percentage / 100, reduced
// with distance from the impact for splash weapons. Ranges and splash are
// written in tiles in the data and stored in leptons.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace slave {

enum class ArmorClass : uint8_t { Infantry = 0, Light, Medium, Heavy, Count };
constexpr int ARMOR_COUNT = static_cast<int>(ArmorClass::Count);
bool parseArmor(const std::string& s, ArmorClass& out);

enum class ProjectileKind : uint8_t { Instant = 0, Shell };

struct WarheadDef {
  std::string id;
  std::array<int32_t, ARMOR_COUNT> versus{100, 100, 100, 100};  // percent
};

struct WeaponDef {
  std::string id;
  std::string name;
  int32_t damage = 10;
  int32_t range = 5 * 256;   // leptons
  int32_t cooldown = 15;     // ticks between shots
  ProjectileKind projectile = ProjectileKind::Instant;
  int32_t speed = 160;       // leptons per tick (shells)
  int32_t splash = 0;        // leptons
  int32_t accuracy = 100;    // percent chance to hit (instant weapons)
  uint16_t warhead = 0;
  uint8_t muzzle = 1;        // muzzle-flash size class 0..2 (presentation)
};

class WeaponRegistry {
 public:
  bool loadJson(const nlohmann::json& doc, std::string& error);
  bool loadFile(const std::string& path, std::string& error);
  int find(const std::string& id) const;
  const WeaponDef& get(int i) const { return weapons_[static_cast<size_t>(i)]; }
  const WarheadDef& warhead(int i) const { return warheads_[static_cast<size_t>(i)]; }
  size_t size() const { return weapons_.size(); }
  int32_t damageAgainst(int weapon, ArmorClass armor) const {
    const WeaponDef& w = get(weapon);
    return w.damage * warhead(w.warhead).versus[static_cast<size_t>(armor)] / 100;
  }

 private:
  std::vector<WeaponDef> weapons_;
  std::vector<WarheadDef> warheads_;
};

}  // namespace slave

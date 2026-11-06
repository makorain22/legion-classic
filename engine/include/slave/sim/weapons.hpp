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

enum class ArmorClass : uint8_t { Infantry = 0, Light, Medium, Heavy, Structure, Count };
constexpr int ARMOR_COUNT = static_cast<int>(ArmorClass::Count);
bool parseArmor(const std::string& s, ArmorClass& out);

enum class ProjectileKind : uint8_t { Instant = 0, Shell };

// Which side of a vehicle a round strikes, judged from its line of flight
// against the hull's facing.
enum class HitSide : uint8_t { Front = 0, Side, Rear };

struct WarheadDef {
  std::string id;
  std::array<int32_t, ARMOR_COUNT> versus{100, 100, 100, 100, 100};  // percent
  // Percent damage by side struck, against vehicles only (infantry has no "rear armour").
  std::array<int32_t, 3> facing{100, 100, 100};
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
  int vsInfantry = -1;       // a gun's alternative round against infantry (e.g. HE), or -1
  int32_t burst = 1;         // shells per shot (rocket salvoes), launched a tick apart
  int32_t burstSpread = 0;   // leptons of extra scatter for each shell of a burst
  std::string sound;         // presentation: the firing sound (content/audio/audio.json), "" = by kind
  std::string impact;        // presentation: the sound where it lands, "" = by warhead
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
  // Damage including the side struck (front / side / rear) for vehicles.
  int32_t damageAgainst(int weapon, ArmorClass armor, HitSide side) const {
    const int32_t base = damageAgainst(weapon, armor);
    if (armor == ArmorClass::Infantry || armor == ArmorClass::Structure) return base;
    return base * warhead(get(weapon).warhead).facing[static_cast<size_t>(side)] / 100;
  }
  // Damage in hundredths of a hit point (small arms against armour are fractions of a point).
  int32_t damageCenti(int weapon, ArmorClass armor) const {
    const WeaponDef& w = get(weapon);
    return w.damage * warhead(w.warhead).versus[static_cast<size_t>(armor)];
  }
  int32_t damageCenti(int weapon, ArmorClass armor, HitSide side) const {
    const int32_t base = damageCenti(weapon, armor);
    if (armor == ArmorClass::Infantry || armor == ArmorClass::Structure) return base;
    return base * warhead(get(weapon).warhead).facing[static_cast<size_t>(side)] / 100;
  }
  // The round a weapon fires at this armour (guns switch to HE against infantry).
  int forTarget(int weapon, ArmorClass armor) const {
    const WeaponDef& w = get(weapon);
    return (armor == ArmorClass::Infantry && w.vsInfantry >= 0) ? w.vsInfantry : weapon;
  }

 private:
  std::vector<WeaponDef> weapons_;
  std::vector<WarheadDef> warheads_;
};

}  // namespace slave

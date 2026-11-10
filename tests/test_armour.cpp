// Armour and artillery (Milestone 9.4): the health scale, the three weights of
// armour-piercing shot, longer-ranged artillery and reverse gear.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>

#include "slave/map/map.hpp"
#include "slave/sim/simulation.hpp"

using namespace slave;

namespace {
struct Rules {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  Rules() {
    std::string err;
    const std::string dir = LEGION_RULES_DIR;
    REQUIRE(weapons.loadFile(dir + "weapons.json", err));
    REQUIRE(types.loadFile(dir + "units.json", err));
    REQUIRE(types.loadFile(dir + "buildings.json", err));
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
  }
  uint16_t t(const std::string& id) const {
    const int i = types.find(id);
    REQUIRE(i >= 0);
    return static_cast<uint16_t>(i);
  }
  const UnitTypeDef& def(const std::string& id) const { return types.get(t(id)); }
  // The most a unit's guns do in one shot to this armour, from the front.
  int32_t punch(const std::string& id, ArmorClass armor) const {
    int32_t best = 0;
    for (int w : def(id).weapons) best = std::max(best, weapons.damageAgainst(w, armor, HitSide::Front));
    return best;
  }
};

Entity spawn(Simulation& sim, uint16_t type, uint8_t owner, Vec2i at, Angle facing = 0) {
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{type, owner, at, facing, -1}};
  sim.queue(c);
  sim.tick();
  for (const auto& r : sim.spawnLog()) return r.entity;
  return {};
}
void order(Simulation& sim, CommandType type, Entity e, Vec2i to) {
  Command c;
  c.type = type;
  c.units = {e};
  c.target = to;
  sim.queue(c);
}
Map flat(int size = 64) {
  Map m;
  m.resize(size, size);
  return m;
}
}  // namespace

TEST_CASE("the health scale: a medium tank has half its army's heavy, a light tank a quarter") {
  Rules R;
  for (const char* f : {"ger", "rus", "gbr"}) {
    const std::string a = f;
    const int32_t heavy = R.def(a + "_heavy_tank").maxHealth;
    CHECK(heavy >= 600);
    CHECK(heavy <= 760);  // lowered from 950-1150
    CHECK(R.def(a + "_medium_tank").maxHealth == heavy / 2);
    CHECK(R.def(a + "_light_tank").maxHealth == heavy / 4);
    // Tank destroyers sit between the light and the medium; the thin-skinned stay below the light tank.
    CHECK(R.def(a + "_tank_destroyer").maxHealth < R.def(a + "_medium_tank").maxHealth);
    CHECK(R.def(a + "_tank_destroyer").maxHealth > R.def(a + "_light_tank").maxHealth);
    for (const char* thin : {"_halftrack", "_scout_car", "_artillery"})
      CHECK(R.def(a + thin).maxHealth < R.def(a + "_light_tank").maxHealth);
    // Only heavy tanks (and the casemate tank destroyers) carry heavy armour.
    CHECK(R.def(a + "_heavy_tank").armor == ArmorClass::Heavy);
    CHECK(R.def(a + "_medium_tank").armor == ArmorClass::Medium);
    CHECK(R.def(a + "_light_tank").armor == ArmorClass::Light);
    for (const char* thin : {"_halftrack", "_scout_car", "_artillery", "_mcv"}) CHECK(R.def(a + thin).armor == ArmorClass::Soft);
  }
  CHECK(R.def("gbr_firefly").maxHealth == R.def("gbr_heavy_tank").maxHealth / 2);
}

TEST_CASE("heavy tanks shrug off light and medium guns from the front; tank destroyers go through") {
  Rules R;
  for (const char* f : {"ger", "rus", "gbr"}) {
    const std::string a = f;
    const int32_t light = R.punch(a + "_light_tank", ArmorClass::Heavy);
    const int32_t medium = R.punch(a + "_medium_tank", ArmorClass::Heavy);
    const int32_t td = R.punch(a + "_tank_destroyer", ArmorClass::Heavy);
    CHECK(light < medium);
    CHECK(medium * 2 < td);
    // Against a medium tank every gun bites at full weight.
    CHECK(R.punch(a + "_light_tank", ArmorClass::Light) > light);
    CHECK(R.punch(a + "_light_tank", ArmorClass::Light) >= R.punch(a + "_light_tank", ArmorClass::Medium));
    // Shots a heavy takes from the front: a dozen from a medium tank, a handful from a tank destroyer.
    for (const char* g : {"ger", "rus", "gbr"}) {
      const int32_t hp = R.def(std::string(g) + "_heavy_tank").maxHealth;
      CHECK((hp + medium - 1) / medium >= 10);
      CHECK((hp + td - 1) / td <= 5);
    }
  }
  // From behind, even a medium gun does real harm (rear armour counts 2.5 times).
  const int w = R.def("ger_medium_tank").weapons.front();
  CHECK(R.weapons.damageAgainst(w, ArmorClass::Heavy, HitSide::Rear) * 2 ==
        R.weapons.damageAgainst(w, ArmorClass::Heavy, HitSide::Front) * 5);
}

TEST_CASE("a heavy tank beats two medium tanks head on, and falls to two tank destroyers") {
  Rules R;
  // How many of ten duels the heavy survives. It faces east; the others come
  // at its front from eight tiles away.
  auto duel = [&](const char* heavyId, const char* otherId, int others) {
    int won = 0;
    for (uint64_t seed = 1; seed <= 10; ++seed) {
      Map m = flat(64);
      Simulation sim(m, R.types, R.weapons, seed);
      const Entity heavy = spawn(sim, R.t(heavyId), 0, {20 * 256, 32 * 256}, 0);
      std::vector<Entity> foes;
      for (int i = 0; i < others; ++i)
        foes.push_back(spawn(sim, R.t(otherId), 1, {28 * 256, (33 - others + i * 2) * 256}, ANGLE_FULL / 2));
      for (int i = 0; i < 15 * 180 && sim.registry().alive(heavy); ++i) {
        bool any = false;
        for (Entity e : foes) any = any || sim.registry().alive(e);
        if (!any) break;
        sim.tick();
      }
      won += sim.registry().alive(heavy);
    }
    return won;
  };
  CHECK(duel("ger_heavy_tank", "rus_medium_tank", 2) >= 7);
  CHECK(duel("rus_heavy_tank", "ger_medium_tank", 2) >= 7);
  CHECK(duel("gbr_heavy_tank", "ger_medium_tank", 2) >= 5);  // the Churchill: thick hide, modest gun
  // One tank destroyer alone loses; two of them (about a heavy's price) win.
  CHECK(duel("ger_heavy_tank", "rus_tank_destroyer", 1) >= 7);
  CHECK(duel("ger_heavy_tank", "rus_tank_destroyer", 2) <= 3);
  CHECK(duel("rus_heavy_tank", "ger_tank_destroyer", 2) <= 3);
  CHECK(duel("ger_heavy_tank", "gbr_tank_destroyer", 2) <= 6);
  // Numbers do it too: four mediums wear a heavy down.
  CHECK(duel("ger_heavy_tank", "rus_medium_tank", 4) <= 1);
}

TEST_CASE("a shell that lands on a tank's hull hits it; the same miss leaves a man unhurt") {
  Rules R;
  Map m = flat(32);
  Simulation sim(m, R.types, R.weapons, 2);
  const Entity tank = spawn(sim, R.t("rus_medium_tank"), 1, {10 * 256, 10 * 256}, 0);
  const Entity man = spawn(sim, R.t("rus_rifleman"), 1, {20 * 256, 10 * 256}, 0);
  const int gun = R.weapons.find("cannon_at");  // splash 0.2 tiles = 51 leptons
  REQUIRE(gun >= 0);
  auto shell = [&](Vec2i at) {
    Command c;
    c.type = CommandType::Strike;
    c.weapon = static_cast<uint16_t>(gun);
    c.target = at;
    c.count = 1;
    c.radius = 0;
    sim.queue(c);
    for (int i = 0; i < 8; ++i) sim.tick();
  };
  shell({10 * 256 + 90, 10 * 256});  // 90 leptons off both: on the tank's hull, past the man
  shell({20 * 256 + 90, 10 * 256});
  CHECK(sim.registry().get<CUnit>(tank).health < R.def("rus_medium_tank").maxHealth);
  CHECK(sim.registry().get<CUnit>(man).health == R.def("rus_rifleman").maxHealth);
}

TEST_CASE("artillery reaches far beyond any tank gun") {
  Rules R;
  int32_t tankGun = 0;
  for (const UnitTypeDef& d : R.types.all())
    if (d.nato == "armour" || d.nato == "antitank_armour") {
      Map m = flat(8);
      Simulation sim(m, R.types, R.weapons, 1);
      tankGun = std::max(tankGun, sim.maxRange(d));
    }
  Map m = flat(8);
  Simulation sim(m, R.types, R.weapons, 1);
  for (const char* f : {"ger", "rus", "gbr"}) {
    const std::string a = f;
    CHECK(sim.maxRange(R.def(a + "_artillery")) >= 24 * 256);
    CHECK(sim.maxRange(R.def(a + "_artillery")) >= tankGun * 2);
    CHECK(sim.maxRange(R.def(a + "_howitzer_pit")) >= 34 * 256);
    CHECK(sim.maxRange(R.def(a + "_howitzer_pit")) > sim.maxRange(R.def(a + "_artillery")));
  }
}

TEST_CASE("a howitzer shells what a scout sees, far outside its own sight") {
  Rules R;
  Map m = flat(96);
  Simulation sim(m, R.types, R.weapons, 3);
  const Entity gun = spawn(sim, R.t("ger_artillery"), 0, {10 * 256, 48 * 256}, 0);
  const Entity scout = spawn(sim, R.t("ger_scout_car"), 0, {22 * 256, 48 * 256}, 0);
  Command hold;  // the scout only watches
  hold.type = CommandType::SetStance;
  hold.units = {scout};
  hold.stance = Stance::HoldFire;
  sim.queue(hold);
  const Entity target = spawn(sim, R.t("rus_power"), 1, {36 * 256, 48 * 256});  // 26 tiles from the gun
  REQUIRE(target.valid());
  (void)gun;
  const int32_t full = sim.registry().get<CUnit>(target).health;
  for (int i = 0; i < 15 * 40; ++i) sim.tick();
  CHECK((!sim.registry().alive(target) || sim.registry().get<CUnit>(target).health < full));
}

TEST_CASE("reverse gear: a tank sent a short way to its rear backs up, still facing forward") {
  Rules R;
  Map m = flat(64);
  Simulation sim(m, R.types, R.weapons, 5);
  const Vec2i start{32 * 256, 32 * 256};
  const Entity tank = spawn(sim, R.t("ger_medium_tank"), 0, start, 0);  // facing east
  const Entity twin = spawn(sim, R.t("ger_medium_tank"), 0, {32 * 256, 40 * 256}, 0);
  REQUIRE(sim.wouldReverse(tank, {28 * 256, 32 * 256}));
  CHECK_FALSE(sim.wouldReverse(tank, {36 * 256, 32 * 256}));  // ahead: just drive
  CHECK_FALSE(sim.wouldReverse(tank, {12 * 256, 32 * 256}));  // a long way back: turn round
  order(sim, CommandType::Move, tank, {28 * 256, 32 * 256});  // four tiles west, behind it
  order(sim, CommandType::Move, twin, {36 * 256, 40 * 256});  // its twin drives four tiles forward
  int back = 0, forward = 0;
  for (int i = 1; i <= 15 * 30; ++i) {
    sim.tick();
    const CPosition& p = sim.registry().get<CPosition>(tank);
    if (p.x > 28 * 256 + 64) {
      back = i;
      CHECK(facingOf(sim.registry().get<CFacing>(tank).angle) == 0);  // never shows its rear
    }
    if (sim.registry().get<CPosition>(twin).x < 36 * 256 - 64) forward = i;
  }
  CHECK(std::abs(sim.registry().get<CPosition>(tank).x - 28 * 256) < 96);
  CHECK(facingOf(sim.registry().get<CFacing>(tank).angle) == 0);
  CHECK(back > forward);  // reverse is the slower gear

  // A long way back it turns round and drives.
  order(sim, CommandType::Move, tank, {8 * 256, 32 * 256});
  for (int i = 0; i < 15 * 6; ++i) sim.tick();
  CHECK(facingOf(sim.registry().get<CFacing>(tank).angle) == 8);  // facing west now

  // Men have no reverse gear.
  const Entity man = spawn(sim, R.t("ger_rifleman"), 0, {32 * 256, 50 * 256}, 0);
  CHECK_FALSE(sim.wouldReverse(man, {30 * 256, 50 * 256}));
}

TEST_CASE("reverse gear: under fire a tank backs away much farther, front armour to the enemy") {
  Rules R;
  Map m = flat(64);
  Simulation sim(m, R.types, R.weapons, 9);
  const Entity tank = spawn(sim, R.t("ger_heavy_tank"), 0, {30 * 256, 32 * 256}, 0);  // facing east
  CHECK_FALSE(sim.wouldReverse(tank, {20 * 256, 32 * 256}));  // ten tiles back, at peace: it would turn
  const Entity foe = spawn(sim, R.t("rus_medium_tank"), 1, {38 * 256, 32 * 256}, ANGLE_FULL / 2);
  for (int i = 0; i < 15 * 5; ++i) sim.tick();  // they trade shots
  REQUIRE(sim.registry().alive(tank));
  REQUIRE(sim.registry().alive(foe));
  CHECK(sim.wouldReverse(tank, {20 * 256, 32 * 256}));
  order(sim, CommandType::Move, tank, {20 * 256, 32 * 256});
  int32_t lastX = sim.registry().get<CPosition>(tank).x;
  bool moved = false;
  for (int i = 0; i < 15 * 12 && sim.registry().alive(tank) && sim.registry().alive(foe); ++i) {
    sim.tick();
    const int32_t x = sim.registry().get<CPosition>(tank).x;
    if (x < lastX) {
      moved = true;
      CHECK(facingOf(sim.registry().get<CFacing>(tank).angle) == 0);  // backing west, facing east
    }
    lastX = x;
  }
  CHECK(moved);
}

// River ice (Milestone 9.11): frozen water carries men and wheels but not
// tracks; the script can freeze water and break the ice; heavy shells break
// it where they land, and what stands there goes under; maps keep their ice.
#include <doctest/doctest.h>

#include <algorithm>

#include "pioneer/map/map.hpp"
#include "pioneer/map/map_io.hpp"
#include "pioneer/sim/script.hpp"
#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

namespace {
struct Rules {
  UnitTypeRegistry types;
  WeaponRegistry weapons;
  Rules() {
    std::string err;
    const std::string dir = LEGION_RULES_DIR;
    REQUIRE(weapons.loadFile(dir + "weapons.json", err));
    REQUIRE(types.loadFile(dir + "units.json", err));
    REQUIRE(types.loadFile(dir + "buildings.json", err));
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
  }
  uint16_t t(const char* id) const {
    const int i = types.find(id);
    REQUIRE(i >= 0);
    return static_cast<uint16_t>(i);
  }
};

// A 64x64 meadow with a deep river running north-south, its bed over columns
// 27..36 and gentle banks either side (water from column 24 to 39).
Map riverMap() {
  Map m(64, 64, 300);
  m.setWaterLevel(260);
  for (int cy = 0; cy < m.cornersY(); ++cy)
    for (int cx = 22; cx <= 42; ++cx) {
      const int fromBank = std::min(cx - 22, 42 - cx);
      m.setCorner(cx, cy, static_cast<uint16_t>(std::max(200, 300 - fromBank * 20)));
    }
  m.recomputeInfo();
  return m;
}

Entity spawn(Simulation& sim, uint16_t type, uint8_t owner, Vec2i tile) {
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{type, owner, {tile.x * 256 + 128, tile.y * 256 + 128}, 0, -1}};
  sim.queue(c);
  sim.tick();
  for (const auto& r : sim.spawnLog()) return r.entity;
  return {};
}
void moveTo(Simulation& sim, Entity e, Vec2i tile) {
  Command c;
  c.type = CommandType::Move;
  c.player = 0;
  c.units = {e};
  c.target = {tile.x * 256 + 128, tile.y * 256 + 128};
  sim.queue(c);
}
int tileX(Simulation& sim, Entity e) { return sim.registry().get<CPosition>(e).x >> LEPTON_SHIFT; }
}  // namespace

TEST_CASE("ice carries men and wheels, not tracks, at a slow pace") {
  Map m = riverMap();
  REQUIRE(m.info(31, 20).waterDepth > 0);
  CHECK_FALSE(m.passable(31, 20, Locomotor::Foot));
  CHECK_FALSE(m.passable(31, 20, Locomotor::Wheeled));
  CHECK(m.setIce(0, 0, 63, 63, true) > 0);
  CHECK(m.iced(31, 20));
  CHECK_FALSE(m.iced(10, 20));  // dry land takes no ice
  CHECK(m.passable(31, 20, Locomotor::Foot));
  CHECK(m.passable(31, 20, Locomotor::Wheeled));
  CHECK_FALSE(m.passable(31, 20, Locomotor::Tracked));
  CHECK(m.speedPercent(31, 20, Locomotor::Foot) == 70);
  CHECK(m.speedPercent(31, 20, Locomotor::Wheeled) == 60);
  // Saved and loaded with the map.
  std::string err;
  MapFile f;
  REQUIRE(decodeMap(encodeMap(m, {}, {}, ""), f, err));
  CHECK(f.map.iced(31, 20));
  CHECK_FALSE(f.map.iced(10, 20));
  CHECK(f.map.passable(31, 20, Locomotor::Wheeled));
  CHECK(m.setIce(0, 0, 63, 63, false) > 0);
  CHECK_FALSE(m.passable(31, 20, Locomotor::Wheeled));
}

TEST_CASE("a lorry drives over the frozen river; a tank can't follow") {
  Rules R;
  Map m = riverMap();
  m.setIce(0, 0, 63, 63, true);
  Simulation sim(m, R.types, R.weapons, 3);
  const Entity truck = spawn(sim, R.t("ger_supply_truck"), 0, {20, 30});
  const Entity tank = spawn(sim, R.t("ger_medium_tank"), 0, {20, 36});
  const Entity man = spawn(sim, R.t("ger_rifleman"), 0, {20, 24});
  moveTo(sim, truck, {46, 30});
  moveTo(sim, tank, {46, 36});
  moveTo(sim, man, {46, 24});
  for (int i = 0; i < 15 * 90; ++i) sim.tick();
  CHECK(tileX(sim, truck) > 40);
  CHECK(tileX(sim, man) > 40);
  CHECK(tileX(sim, tank) < 24);
}

TEST_CASE("the script freezes the river and breaks the ice under a column") {
  Rules R;
  Map m = riverMap();
  Simulation sim(m, R.types, R.weapons, 5);
  ScenarioScript sc;
  sc.areas.push_back({"River", 26, 0, 37, 63});
  Trigger freeze;
  freeze.name = "Freeze";
  freeze.conditions = {Condition{}};
  freeze.conditions[0].type = CondType::Always;
  Action fa;
  fa.type = ActionType::FreezeWater;
  fa.area = 0;
  freeze.actions = {fa};
  Trigger thaw;
  thaw.name = "Break";
  Condition c;
  c.type = CondType::Timer;
  c.value = 20;
  thaw.conditions = {c};
  Action ta;
  ta.type = ActionType::BreakIce;
  ta.area = 0;
  thaw.actions = {ta};
  sc.triggers = {freeze, thaw};
  sim.setScript(sc, false);
  sim.tick();
  CHECK(m.iced(31, 10));
  CHECK(std::string(actionId(ActionType::FreezeWater)) == "freeze_water");
  CHECK(std::string(actionId(ActionType::BreakIce)) == "break_ice");
  // A lorry halfway over when the ice goes.
  const Entity truck = spawn(sim, R.t("ger_supply_truck"), 0, {31, 10});
  REQUIRE(sim.registry().alive(truck));
  for (int i = 0; i < 15 * 22; ++i) sim.tick();
  CHECK_FALSE(m.iced(31, 10));
  CHECK_FALSE(sim.registry().alive(truck));
}

TEST_CASE("a howitzer shell breaks the ice where it lands") {
  Rules R;
  Map m = riverMap();
  m.setIce(0, 0, 63, 63, true);
  Simulation sim(m, R.types, R.weapons, 9);
  const int before = [&] {
    int n = 0;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) n += m.iced(x, y);
    return n;
  }();
  Command c;
  c.type = CommandType::Strike;
  c.player = 1;
  c.weapon = static_cast<uint16_t>(R.weapons.find("howitzer_150"));
  c.count = 6;
  c.target = {31 * 256 + 128, 30 * 256 + 128};
  c.radius = 256;
  sim.queue(c);
  for (int i = 0; i < 15 * 30; ++i) sim.tick();
  int after = 0;
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) after += m.iced(x, y);
  CHECK(after < before);
  CHECK(after > before - 12);  // only where the shells fell
}

// Flooding (Milestone 9.12): opened sluices put the land under shallow water
// that men can wade and vehicles can't cross; draining puts it back, even
// after the map (or a save) has been written and read.
TEST_CASE("flooded land stops vehicles, not men, and drains back to what it was") {
  Map m = riverMap();
  REQUIRE(m.info(8, 8).waterDepth < 0);
  const uint16_t before = m.corner(8, 8);
  CHECK(m.flood(4, 4, 12, 12) > 0);
  CHECK(m.info(8, 8).waterDepth > 0);
  CHECK(m.passable(8, 8, Locomotor::Foot));
  CHECK_FALSE(m.passable(8, 8, Locomotor::Wheeled));
  CHECK_FALSE(m.passable(8, 8, Locomotor::Tracked));
  CHECK(m.speedPercent(8, 8, Locomotor::Foot) == 50);
  CHECK(m.flood(4, 4, 12, 12) == 0);  // already under water
  std::string err;
  MapFile f;
  REQUIRE(decodeMap(encodeMap(m, {}, {}, ""), f, err));
  CHECK(f.map.info(8, 8).waterDepth > 0);
  CHECK(f.map.drain(0, 0, 63, 63) > 0);
  CHECK(f.map.corner(8, 8) == before);
  CHECK(f.map.passable(8, 8, Locomotor::Wheeled));
  CHECK(f.map.floodMemory().empty());
  CHECK(std::string(actionId(ActionType::FloodArea)) == "flood_area");
  CHECK(std::string(actionId(ActionType::DrainArea)) == "drain_area");
}

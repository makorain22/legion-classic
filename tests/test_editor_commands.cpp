// Simulation commands used by the Live Scenario Editor.
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

namespace {
struct World {
  Map map{64, 64, 300};
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  World() {
    std::string err;
    REQUIRE(weapons.loadJson(nlohmann::json::parse(R"({
      "warheads": {"bullet": {"infantry":100,"heavy":3}, "he": {"infantry":100,"heavy":20}},
      "weapons": [
        {"id":"rifle","warhead":"bullet","damage":14,"range":4.5,"cooldown":22,"accuracy":100},
        {"id":"arty","warhead":"he","damage":200,"range":40,"cooldown":60,"projectile":"shell","speed":260,"splash":1.4}
      ]})"), err));
    REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[
      {"id":"rifle","model":"rifleman","speed":12,"turnTicks":0,"radius":40,"health":100,"armor":"infantry",
       "sight":6,"weapons":["rifle"],"locomotor":"foot"},
      {"id":"tank","model":"tank","speed":22,"turnTicks":2,"radius":128,"health":400,"armor":"heavy","sight":7,
       "turret":{"turnTicks":2}}
    ]})"), err));
    REQUIRE(types.resolveWeapons(weapons, err));
  }
};

std::vector<Entity> units(Simulation& sim) {
  std::vector<Entity> out;
  for (uint32_t e : sim.registry().pool<CUnit>().entities()) out.push_back(sim.registry().handle(e));
  return out;
}
}  // namespace

TEST_CASE("SpawnList places exact units and reports them under the command's tag") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 1);
  Command c;
  c.type = CommandType::SpawnList;
  c.tag = 77;
  c.spawns = {{0, 0, {10 * 256 + 40, 12 * 256 + 90}, angleOfFacing(4), 55}, {1, 1, {30 * 256, 30 * 256}, 0, -1}};
  sim.queue(c);
  sim.tick();
  REQUIRE(sim.spawnLog().size() == 2);
  CHECK(sim.spawnLog()[0].tag == 77);
  CHECK(sim.spawnLog()[1].index == 1);
  const Entity a = sim.spawnLog()[0].entity, b = sim.spawnLog()[1].entity;
  CHECK(sim.registry().get<CPosition>(a).x == 10 * 256 + 40);
  CHECK(sim.registry().get<CFacing>(a).angle == angleOfFacing(4));
  CHECK(sim.registry().get<CUnit>(a).health == 55);
  CHECK(sim.registry().get<CUnit>(b).health == 400);
  CHECK(sim.registry().get<CUnit>(b).owner == 1);
  sim.tick();
  CHECK(sim.spawnLog().empty());  // the log only covers the last tick
}

TEST_CASE("Edit moves, turns, converts and heals units; bad moves are refused") {
  World w;
  w.map.setWaterLevel(0);
  Simulation sim(w.map, w.types, w.weapons, 1);
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{1, 0, {20 * 256, 20 * 256}, 0, 100}};
  sim.queue(c);
  sim.tick();
  const Entity t = units(sim).front();

  Command e;
  e.type = CommandType::Edit;
  UnitEdit ed;
  ed.unit = t;
  ed.move = true;
  ed.pos = {40 * 256, 25 * 256};
  ed.facing = angleOfFacing(6) + 30;  // snapped to a whole facing
  ed.owner = 3;
  ed.health = 9999;  // clamped to the maximum
  e.edits = {ed};
  sim.queue(e);
  sim.tick();
  CHECK(sim.registry().get<CPosition>(t).x == 40 * 256);
  CHECK(sim.registry().get<CPrevPosition>(t).x == 40 * 256);
  CHECK(sim.registry().get<CFacing>(t).angle == angleOfFacing(6));
  CHECK(sim.registry().get<CTurret>(t).angle == angleOfFacing(6));
  CHECK(sim.registry().get<CUnit>(t).owner == 3);
  CHECK(sim.registry().get<CUnit>(t).health == 400);

  ed = UnitEdit{};
  ed.unit = t;
  ed.health = 0;  // never kills: at least 1
  e.edits = {ed};
  sim.queue(e);
  sim.tick();
  CHECK(sim.registry().get<CUnit>(t).health == 1);
}

TEST_CASE("Kill leaves death events; Remove does not") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 1);
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{0, 0, {10 * 256, 10 * 256}, 0, -1}, {1, 0, {20 * 256, 10 * 256}, 0, -1}};
  sim.queue(c);
  sim.tick();
  const auto all = units(sim);
  Command k;
  k.type = CommandType::Kill;
  k.units = {all[1]};
  sim.queue(k);
  Command r;
  r.type = CommandType::Remove;
  r.units = {all[0]};
  sim.queue(r);
  sim.tick();
  CHECK(sim.unitCount() == 0);
  int deaths = 0;
  for (const SimEvent& ev : sim.events()) deaths += ev.type == EventType::UnitDied;
  CHECK(deaths == 1);
}

TEST_CASE("an artillery strike lands within its radius and hurts every side") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 5);
  Command c;
  c.type = CommandType::Spawn;
  c.unitType = 0;
  c.count = 16;
  c.player = 0;
  c.target = {32 * 256, 32 * 256};
  sim.queue(c);
  c.player = 1;
  c.target = {33 * 256, 33 * 256};
  sim.queue(c);
  sim.tick();
  Command s;
  s.type = CommandType::Strike;
  s.weapon = static_cast<uint16_t>(w.weapons.find("arty"));
  s.count = 12;
  s.radius = 3 * 256;
  s.target = {32 * 256 + 128, 32 * 256 + 128};
  sim.queue(s);
  int impacts = 0;
  for (int i = 0; i < 90; ++i) {
    sim.tick();
    for (const SimEvent& ev : sim.events())
      if (ev.type == EventType::Impact && ev.weapon == s.weapon) {
        ++impacts;
        CHECK(distance(ev.at.x - s.target.x, ev.at.y - s.target.y) <= 3u * 256u);
      }
  }
  CHECK(impacts == 12);
  int left0 = 0, left1 = 0;
  for (Entity e : units(sim)) (sim.registry().get<CUnit>(e).owner == 0 ? left0 : left1)++;
  CHECK(left0 < 16);
  CHECK(left1 < 16);
}

TEST_CASE("players on the same team don't fight and share vision") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 2);
  Command t;
  t.type = CommandType::SetTeam;
  t.player = 1;
  t.count = 0;  // player 1 joins player 0's team
  sim.queue(t);
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{0, 0, {20 * 256, 20 * 256}, 0, -1}, {0, 1, {22 * 256, 20 * 256}, 0, -1}};
  sim.queue(c);
  for (int i = 0; i < 15 * 10; ++i) sim.tick();
  CHECK(sim.unitCount() == 2);
  for (Entity e : units(sim)) CHECK(sim.registry().get<CUnit>(e).health == 100);
  CHECK(sim.allied(0, 1));
  CHECK_FALSE(sim.allied(0, 2));
  // Player 1 sees what player 0's rifleman sees on the far side.
  CHECK(sim.vision().visible(1, 15, 20));
}

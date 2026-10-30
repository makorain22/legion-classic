// Unit behaviour: formations, stances and answering fire.
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "slave/sim/simulation.hpp"

using namespace slave;

namespace {
struct World {
  Map map{64, 64, 300};
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  World() {
    std::string err;
    REQUIRE(weapons.loadJson(nlohmann::json::parse(R"({
      "warheads": {"bullet": {"infantry":100,"heavy":2}},
      "weapons": [{"id":"rifle","warhead":"bullet","damage":10,"range":5,"cooldown":20,"accuracy":100}]})"), err));
    REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[
      {"id":"rifle","model":"rifleman","speed":12,"turnTicks":0,"radius":40,"health":100,"armor":"infantry",
       "sight":7,"weapons":["rifle"],"locomotor":"foot"},
      {"id":"tank","model":"tank","speed":20,"turnTicks":1,"radius":110,"health":400,"armor":"heavy","sight":7}
    ]})"), err));
    REQUIRE(types.resolveWeapons(weapons, err));
  }
};

std::vector<Entity> spawnList(Simulation& sim, std::vector<SpawnSpec> specs) {
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = std::move(specs);
  sim.queue(c);
  sim.tick();
  std::vector<Entity> out;
  for (const SpawnRecord& r : sim.spawnLog()) out.push_back(r.entity);
  return out;
}
}  // namespace

TEST_CASE("a line formation spreads across its front and faces it") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 1);
  std::vector<SpawnSpec> specs;
  for (int i = 0; i < 9; ++i) specs.push_back({0, 0, {10 * 256 + (i % 3) * 120, 10 * 256 + (i / 3) * 120}, 0, -1});
  const auto units = spawnList(sim, specs);
  Command m;
  m.type = CommandType::Move;
  m.units = units;
  m.target = {40 * 256, 32 * 256};
  m.formation = Formation::Line;
  m.formFacing = angleOfFacing(0);  // front faces +X: the line runs along Y
  m.formWidth = 8 * 256;
  sim.queue(m);
  for (int i = 0; i < 15 * 80; ++i) sim.tick();
  int32_t minX = INT32_MAX, maxX = INT32_MIN, minY = INT32_MAX, maxY = INT32_MIN;
  for (Entity e : units) {
    const CPosition& p = sim.registry().get<CPosition>(e);
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y);
    maxY = std::max(maxY, p.y);
    CHECK(sim.registry().get<COrder>(e).type == OrderType::Idle);
    CHECK(facingOf(sim.registry().get<CFacing>(e).angle) == 0);
  }
  CHECK(maxY - minY >= 4 * 256);          // a broad front...
  CHECK(maxX - minX < (maxY - minY) / 2);  // ...and shallow
}

TEST_CASE("armour forms up in front of infantry") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 1);
  const auto units = spawnList(sim, {{1, 0, {10 * 256, 30 * 256}, 0, -1}, {0, 0, {11 * 256, 31 * 256}, 0, -1},
                                     {0, 0, {11 * 256, 29 * 256}, 0, -1}, {0, 0, {12 * 256, 30 * 256}, 0, -1}});
  Command m;
  m.type = CommandType::Move;
  m.units = units;
  m.target = {40 * 256, 30 * 256};
  m.formFacing = angleOfFacing(0);  // heading east: the front is the larger X
  sim.queue(m);
  for (int i = 0; i < 15 * 70; ++i) sim.tick();
  const int32_t tankX = sim.registry().get<CPosition>(units[0]).x;
  for (size_t i = 1; i < units.size(); ++i) CHECK(sim.registry().get<CPosition>(units[i]).x < tankX);
}

TEST_CASE("units shot on the march fight back; a forced march keeps going") {
  World w;
  auto run = [&](bool forced) {
    Simulation sim(w.map, w.types, w.weapons, 4);
    // Our rifleman marches north past an enemy rifleman hidden at the roadside.
    const auto us = spawnList(sim, {{0, 0, {30 * 256, 50 * 256}, 0, -1}});
    const auto them = spawnList(sim, {{0, 1, {34 * 256, 40 * 256}, 0, -1}});
    Command m;
    m.type = CommandType::Move;
    m.units = us;
    m.target = {30 * 256, 5 * 256};
    m.forced = forced;
    sim.queue(m);
    bool foughtBack = false;
    for (int i = 0; i < 15 * 20; ++i) {
      sim.tick();
      if (!sim.registry().alive(us[0])) break;
      if (sim.registry().get<CCombat>(us[0]).target == them[0]) foughtBack = true;
    }
    return foughtBack;
  };
  CHECK(run(false));
  CHECK_FALSE(run(true));
}

TEST_CASE("hold fire keeps an ambush quiet until ordered") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 2);
  const auto ambush = spawnList(sim, {{0, 0, {20 * 256, 20 * 256}, 0, -1}});
  const auto passer = spawnList(sim, {{1, 1, {23 * 256, 20 * 256}, 0, -1}});  // tanks can't be hurt by rifles anyway
  const auto victim = spawnList(sim, {{0, 1, {23 * 256, 22 * 256}, 0, -1}});
  Command s;
  s.type = CommandType::SetStance;
  s.units = ambush;
  s.stance = Stance::HoldFire;
  sim.queue(s);
  // The enemy rifleman is also told to hold fire, so nobody starts it.
  Command s2 = s;
  s2.units = victim;
  sim.queue(s2);
  for (int i = 0; i < 15 * 5; ++i) sim.tick();
  CHECK(sim.registry().get<CUnit>(victim[0]).health == 100);
  Command a;
  a.type = CommandType::Attack;
  a.units = ambush;
  a.targetUnit = victim[0];
  sim.queue(a);
  for (int i = 0; i < 15 * 5; ++i) sim.tick();
  CHECK((!sim.registry().alive(victim[0]) || sim.registry().get<CUnit>(victim[0]).health < 100));
  (void)passer;
}

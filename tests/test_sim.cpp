#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "slave/ecs/registry.hpp"
#include "slave/sim/simulation.hpp"

using namespace slave;

namespace {
UnitTypeRegistry makeTypes() {
  UnitTypeRegistry types;
  std::string err;
  const auto doc = nlohmann::json::parse(R"({"units":[
    {"id":"tank","model":"tank","speed":24,"turnTicks":2,"radius":110},
    {"id":"rifle","model":"rifleman","speed":12,"turnTicks":0,"radius":48,"locomotor":"foot"}
  ]})");
  REQUIRE(types.loadJson(doc, err));
  return types;
}

Command spawn(uint16_t type, uint8_t player, Vec2i at, uint16_t count) {
  Command c;
  c.type = CommandType::Spawn;
  c.unitType = type;
  c.player = player;
  c.target = at;
  c.count = count;
  return c;
}

std::vector<Entity> allUnits(Simulation& sim) {
  std::vector<Entity> out;
  for (uint32_t i : sim.registry().pool<CUnit>().entities()) out.push_back(sim.registry().handle(i));
  return out;
}
}  // namespace

TEST_CASE("registry: create, destroy, stale handles, iteration") {
  Registry reg;
  struct A { int v; };
  struct B { int w; };
  const Entity e1 = reg.create();
  const Entity e2 = reg.create();
  reg.add<A>(e1, {1});
  reg.add<A>(e2, {2});
  reg.add<B>(e2, {20});
  int sum = 0, count = 0;
  reg.each<A, B>([&](uint32_t, A& a, B& b) { sum += a.v + b.w; ++count; });
  CHECK(count == 1);
  CHECK(sum == 22);
  reg.destroy(e1);
  CHECK_FALSE(reg.alive(e1));
  const Entity e3 = reg.create();  // reuses e1's slot with a new generation
  CHECK(e3.index == e1.index);
  CHECK_FALSE(reg.alive(e1));
  CHECK(reg.tryGet<A>(e1) == nullptr);
  CHECK(reg.aliveCount() == 2);
}

TEST_CASE("unit type loading rejects bad data") {
  UnitTypeRegistry types;
  std::string err;
  CHECK_FALSE(types.loadJson(nlohmann::json::parse(R"({"units":[{"id":"x"}]})"), err));
  CHECK_FALSE(types.loadJson(nlohmann::json::parse(R"({"units":[{"id":"x","model":"m","speed":0}]})"), err));
  CHECK(types.loadJson(nlohmann::json::parse(R"({"units":[{"id":"x","model":"m"}]})"), err));
  CHECK_FALSE(types.loadJson(nlohmann::json::parse(R"({"units":[{"id":"x","model":"m"}]})"), err));  // duplicate
}

TEST_CASE("units turn in 16 steps, then drive to the target and stop") {
  Map map(64, 64);
  const auto types = makeTypes();
  Simulation sim(map, types, 1);
  sim.queue(spawn(0, 0, {1000, 1000}, 1));
  sim.tick();
  auto units = allUnits(sim);
  REQUIRE(units.size() == 1);

  Command move;
  move.type = CommandType::Move;
  move.units = units;
  move.target = {1000, 5000};  // straight +Y: facing 4, unit starts at facing 0
  sim.queue(move);

  auto& reg = sim.registry();
  // Turning 4 steps at 2 ticks each: no movement in the first ticks.
  sim.tick();
  CHECK(reg.get<CPosition>(units[0]).y == 1000);
  for (int i = 0; i < 400; ++i) sim.tick();
  const auto& p = reg.get<CPosition>(units[0]);
  CHECK(p.x == 1000);
  CHECK(p.y == 5000);
  CHECK(facingOf(reg.get<CFacing>(units[0]).angle) == 4);
  CHECK_FALSE(reg.get<CMoveOrder>(units[0]).active);
}

TEST_CASE("group move gives every unit its own formation slot") {
  Map map(64, 64);
  const auto types = makeTypes();
  Simulation sim(map, types, 1);
  sim.queue(spawn(1, 0, {2000, 2000}, 25));
  sim.tick();
  Command move;
  move.type = CommandType::Move;
  move.units = allUnits(sim);
  move.target = {8000, 8000};
  sim.queue(move);
  for (int i = 0; i < 2000; ++i) sim.tick();
  std::vector<std::pair<int, int>> spots;
  for (Entity e : move.units) {
    const auto& p = sim.registry().get<CPosition>(e);
    spots.emplace_back(p.x, p.y);
    CHECK(std::abs(p.x - 8000) < 1000);
    CHECK(std::abs(p.y - 8000) < 1000);
  }
  std::sort(spots.begin(), spots.end());
  CHECK(std::adjacent_find(spots.begin(), spots.end()) == spots.end());
}

TEST_CASE("simulation is deterministic: same commands, same state hash") {
  Map map(128, 128);
  const auto types = makeTypes();
  auto run = [&]() {
    Simulation sim(map, types, 1234);
    sim.queue(spawn(0, 0, {3000, 3000}, 300));
    sim.queue(spawn(1, 1, {20000, 20000}, 700));
    sim.tick();
    Command move;
    move.type = CommandType::Move;
    move.units = allUnits(sim);
    move.target = {12000, 15000};
    sim.queue(move);
    for (int i = 0; i < 300; ++i) sim.tick();
    return sim.stateHash();
  };
  const uint64_t h1 = run();
  const uint64_t h2 = run();
  CHECK(h1 == h2);
}

TEST_CASE("units never leave the map") {
  Map map(16, 16);
  const auto types = makeTypes();
  Simulation sim(map, types, 1);
  sim.queue(spawn(0, 0, {-5000, 99999}, 4));
  sim.tick();
  Command move;
  move.type = CommandType::Move;
  move.units = allUnits(sim);
  move.target = {999999, -999999};
  sim.queue(move);
  for (int i = 0; i < 1000; ++i) sim.tick();
  for (Entity e : move.units) {
    const auto& p = sim.registry().get<CPosition>(e);
    CHECK(p.x >= 0);
    CHECK(p.y >= 0);
    CHECK(p.x < map.widthLeptons());
    CHECK(p.y < map.heightLeptons());
  }
}

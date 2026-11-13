#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

namespace {
// A wall across the map with one gap near the bottom.
Map wallMap() {
  Map m(64, 64, 100);
  for (int y = 0; y <= 64; ++y)
    if (y < 50 || y > 54) m.setCorner(32, y, 500);
  m.recomputeInfo();
  return m;
}
Vec2i tc(int x, int y) { return {x * 256 + 128, y * 256 + 128}; }
}  // namespace

TEST_CASE("A* finds a way around a wall through the gap") {
  Map m = wallMap();
  Pathfinder pf(m);
  std::vector<Vec2i> path;
  REQUIRE(pf.findPath(tc(10, 10), tc(50, 10), Locomotor::Tracked, path));
  REQUIRE(!path.empty());
  CHECK(path.back() == tc(50, 10));
  bool throughGap = false;
  for (const Vec2i& p : path) throughGap = throughGap || (p.y >> 8) >= 48;
  CHECK(throughGap);
  // Every straight leg of the smoothed path is drivable.
  Vec2i cur = tc(10, 10);
  for (const Vec2i& p : path) {
    CHECK(pf.lineClear(cur, p, Locomotor::Tracked));
    cur = p;
  }
}

TEST_CASE("unreachable targets resolve to the nearest reachable point") {
  Map m(64, 64, 100);
  m.setWaterLevel(80);
  for (int y = 0; y <= 64; ++y)
    for (int x = 30; x <= 34; ++x) m.setCorner(x, y, 20);  // a river across the whole map
  m.recomputeInfo();
  Pathfinder pf(m);
  pf.refresh();
  CHECK(pf.region(10, 10, Locomotor::Tracked) != pf.region(50, 10, Locomotor::Tracked));
  const Vec2i near = pf.nearestReachable(tc(10, 10), tc(50, 10), Locomotor::Tracked);
  CHECK((near.x >> 8) < 30);
  CHECK((near.x >> 8) > 24);
  std::vector<Vec2i> path;
  REQUIRE(pf.findPath(tc(10, 10), tc(50, 10), Locomotor::Tracked, path));
  CHECK((path.back().x >> 8) < 30);
  CHECK(pf.expansionsLastCall() < 2000);  // never floods the far side
}

TEST_CASE("open ground needs no search at all") {
  Map m(64, 64, 100);
  Pathfinder pf(m);
  std::vector<Vec2i> path;
  REQUIRE(pf.findPath(tc(2, 2), tc(60, 60), Locomotor::Foot, path));
  CHECK(path.size() == 1);
  CHECK(pf.expansionsLastCall() == 0);
}

TEST_CASE("regions refresh after the terrain changes") {
  Map m(64, 64, 100);
  Pathfinder pf(m);
  pf.refresh();
  CHECK(pf.region(10, 10, Locomotor::Wheeled) == pf.region(50, 10, Locomotor::Wheeled));
  for (int y = 0; y <= 64; ++y) m.setCorner(32, y, 900);
  m.recomputeInfo();
  pf.refresh();
  CHECK(pf.region(10, 10, Locomotor::Wheeled) != pf.region(50, 10, Locomotor::Wheeled));
}

TEST_CASE("a group crosses a wall through the gap and nobody overlaps at the end") {
  Map m = wallMap();
  UnitTypeRegistry types;
  WeaponRegistry weapons;
  std::string err;
  REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[{"id":"t","model":"m","speed":24,"turnTicks":1,"radius":100,"locomotor":"tracked"}]})"), err));
  Simulation sim(m, types, weapons, 5);
  Command s;
  s.type = CommandType::Spawn;
  s.target = tc(12, 20);
  s.count = 12;
  sim.queue(s);
  sim.tick();
  Command mv;
  mv.type = CommandType::Move;
  for (uint32_t e : sim.registry().pool<CUnit>().entities()) mv.units.push_back(sim.registry().handle(e));
  mv.target = tc(50, 20);
  sim.queue(mv);
  for (int i = 0; i < 15 * 120; ++i) sim.tick();
  int arrived = 0;
  std::vector<Vec2i> spots;
  for (Entity e : mv.units) {
    const CPosition& p = sim.registry().get<CPosition>(e);
    if ((p.x >> 8) > 40) ++arrived;
    spots.push_back({p.x, p.y});
  }
  CHECK(arrived == 12);
  for (size_t i = 0; i < spots.size(); ++i)
    for (size_t j = i + 1; j < spots.size(); ++j)
      CHECK(distance(spots[i].x - spots[j].x, spots[i].y - spots[j].y) > 100);
}

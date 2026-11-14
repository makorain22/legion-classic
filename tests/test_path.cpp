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

// --- Pioneer part 3: sectors, corridors, the route cache, flow fields ---------
namespace {
// How long a route is, walked waypoint to waypoint (leptons).
int64_t routeLength(Vec2i from, const std::vector<Vec2i>& path) {
  int64_t len = 0;
  Vec2i at = from;
  for (const Vec2i& p : path) {
    len += distance(p.x - at.x, p.y - at.y);
    at = p;
  }
  return len;
}

// Region labels by a plain flood fill over the whole map, to hold the sector graph against.
std::vector<uint32_t> floodRegions(const Map& m, Locomotor loco) {
  const int W = m.width(), H = m.height();
  std::vector<uint32_t> reg(static_cast<size_t>(W) * H, 0), queue;
  uint32_t next = 1;
  static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x;
      if (reg[i] || !m.passable(x, y, loco)) continue;
      reg[i] = next;
      queue.assign(1, static_cast<uint32_t>(i));
      for (size_t q = 0; q < queue.size(); ++q) {
        const int cx = static_cast<int>(queue[q] % W), cy = static_cast<int>(queue[q] / W);
        for (int d = 0; d < 4; ++d) {
          const int nx = cx + dx[d], ny = cy + dy[d];
          if (!m.inBounds(nx, ny)) continue;
          const size_t ni = static_cast<size_t>(ny) * W + nx;
          if (reg[ni] || !m.passable(nx, ny, loco)) continue;
          reg[ni] = next;
          queue.push_back(static_cast<uint32_t>(ni));
        }
      }
      ++next;
    }
  return reg;
}
}  // namespace

TEST_CASE("sector regions agree with a flood fill of the whole map, for every movement type") {
  for (uint64_t seed : {3u, 1946u, 77u}) {
    Map m(seed == 77u ? 160 : 96, 128);
    m.generateCountryside(seed);
    Pathfinder pf(m);
    pf.refresh();
    for (int li = 0; li < 3; ++li) {
      const Locomotor loco = static_cast<Locomotor>(li);
      const std::vector<uint32_t> want = floodRegions(m, loco);
      // Same partition: tiles share a label in one exactly when they do in the other.
      std::unordered_map<uint32_t, uint32_t> a2b, b2a;
      bool same = true;
      for (int y = 0; y < m.height() && same; ++y)
        for (int x = 0; x < m.width() && same; ++x) {
          const uint32_t a = want[static_cast<size_t>(y) * m.width() + x], b = pf.region(x, y, loco);
          if ((a == 0) != (b == 0)) same = false;
          if (!a || !b) continue;
          auto ia = a2b.emplace(a, b), ib = b2a.emplace(b, a);
          if (ia.first->second != b || ib.first->second != a) same = false;
        }
      CHECK_MESSAGE(same, "seed ", seed, " locomotor ", li);
    }
    CHECK(pf.stats().parts > 0);
    CHECK(pf.stats().sectors == static_cast<uint32_t>(((m.width() + 15) / 16) * ((m.height() + 15) / 16)));
  }
}

TEST_CASE("only the sectors a change touches are worked out again") {
  Map m(128, 128, 100);
  Pathfinder pf(m);
  pf.refresh();
  const uint64_t all = pf.stats().sectorRebuilds;
  CHECK(all == 64);
  CHECK(pf.region(5, 5, Locomotor::Tracked) == pf.region(120, 120, Locomotor::Tracked));
  // A building goes up in the middle of one sector.
  for (int y = 36; y < 39; ++y)
    for (int x = 36; x < 39; ++x) m.setBlock(x, y, 7);
  pf.refresh();
  CHECK(pf.stats().sectorRebuilds == all + 1);
  CHECK(pf.region(37, 37, Locomotor::Tracked) == 0);
  CHECK(pf.region(5, 5, Locomotor::Tracked) == pf.region(120, 120, Locomotor::Tracked));
  // A wall across the whole map, on a sector border column: the sectors along it, and the map is cut in two.
  for (int y = 0; y < 128; ++y) m.setBlock(64, y, 7);
  pf.refresh();
  CHECK(pf.stats().sectorRebuilds == all + 1 + 8);
  CHECK(pf.region(5, 5, Locomotor::Tracked) != pf.region(120, 120, Locomotor::Tracked));
  // Nothing changed: nothing rebuilt.
  pf.refresh();
  CHECK(pf.stats().sectorRebuilds == all + 1 + 8);
}

TEST_CASE("long routes are guided by the sector graph: as short, and far less searching round obstacles") {
  // Open country: the guide must not make routes longer or dearer.
  {
    Map m(256, 256);
    m.generateCountryside(1946);
    Pathfinder plain(m), fast(m);
    plain.setCorridors(false);
    plain.setCache(false);
    fast.setCache(false);
    plain.refresh();
    fast.refresh();
    uint64_t plainWork = 0, fastWork = 0;
    int64_t plainLen = 0, fastLen = 0;
    int routes = 0;
    Rng rng(5);
    for (int i = 0; i < 400 && routes < 40; ++i) {
      const int ax = static_cast<int>(rng.next() % 250) + 3, ay = static_cast<int>(rng.next() % 250) + 3;
      const int bx = static_cast<int>(rng.next() % 250) + 3, by = static_cast<int>(rng.next() % 250) + 3;
      const Locomotor loco = static_cast<Locomotor>(i % 3);
      if (std::max(std::abs(ax - bx), std::abs(ay - by)) < 120) continue;
      if (!fast.region(ax, ay, loco) || fast.region(ax, ay, loco) != fast.region(bx, by, loco)) continue;
      std::vector<Vec2i> a, b;
      REQUIRE(plain.findPath(tc(ax, ay), tc(bx, by), loco, a, 1 << 30));
      REQUIRE(fast.findPath(tc(ax, ay), tc(bx, by), loco, b, 1 << 30));
      if (plain.expansionsLastCall() == 0) continue;  // a straight line: nothing to compare
      REQUIRE(a.back() == tc(bx, by));
      REQUIRE(b.back() == tc(bx, by));
      plainWork += plain.expansionsLastCall();
      fastWork += fast.expansionsLastCall();
      plainLen += routeLength(tc(ax, ay), a);
      fastLen += routeLength(tc(ax, ay), b);
      ++routes;
    }
    REQUIRE(routes >= 20);
    MESSAGE("open country, ", routes, " long routes on 256 x 256: plain A* ", plainWork / routes, " nodes each, guided ",
            fastWork / routes, "; length ", fastLen * 100 / plainLen, "% of plain");
    CHECK(fast.stats().corridors >= static_cast<uint64_t>(routes));
    CHECK(fastWork <= plainWork * 12 / 10);
    CHECK(fastLen * 100 <= plainLen * 104);
  }
  // A river across the map with one ford at the far end, and a walled town to go round.
  {
    Map m(256, 256, 100);
    for (int y = 0; y < 256; ++y)
      if (y < 236 || y > 240)
        for (int x = 126; x < 130; ++x) m.setBlock(x, y, 7);
    for (int x = 150; x < 230; ++x) m.setBlock(x, 60, 7);
    for (int y = 60; y < 140; ++y) m.setBlock(150, y, 7);
    for (int x = 150; x < 230; ++x) m.setBlock(x, 140, 7);
    Pathfinder plain(m), fast(m);
    plain.setCorridors(false);
    plain.setCache(false);
    fast.setCache(false);
    uint64_t plainWork = 0, fastWork = 0;
    int64_t plainLen = 0, fastLen = 0;
    const Vec2i trips[][2] = {{tc(20, 20), tc(240, 20)},  {tc(60, 100), tc(200, 100)}, {tc(100, 10), tc(190, 130)},
                              {tc(240, 30), tc(10, 60)},  {tc(110, 120), tc(140, 120)}, {tc(30, 200), tc(220, 100)}};
    for (const auto& t : trips) {
      std::vector<Vec2i> a, b;
      REQUIRE(plain.findPath(t[0], t[1], Locomotor::Tracked, a, 1 << 30));
      REQUIRE(fast.findPath(t[0], t[1], Locomotor::Tracked, b, 1 << 30));
      REQUIRE(a.back() == t[1]);
      REQUIRE(b.back() == t[1]);
      plainWork += plain.expansionsLastCall();
      fastWork += fast.expansionsLastCall();
      plainLen += routeLength(t[0], a);
      fastLen += routeLength(t[0], b);
    }
    MESSAGE("one ford and a walled town, 6 routes: plain A* ", plainWork / 6, " nodes each, guided ", fastWork / 6, "; length ",
            fastLen * 100 / plainLen, "% of plain");
    CHECK(fastWork * 3 < plainWork);         // at least three times as cheap
    CHECK(fastLen * 100 <= plainLen * 108);  // within a twelfth of the length
  }
}

TEST_CASE("the route cache answers the same trip again, with the same route and the same cost") {
  Map m = wallMap();
  Pathfinder pf(m);
  std::vector<Vec2i> first, second, third;
  REQUIRE(pf.findPath(tc(10, 10), tc(55, 12), Locomotor::Tracked, first));
  const uint64_t work = pf.expansionsLastCall();
  CHECK(work > 0);
  CHECK(pf.stats().cacheHits == 0);
  REQUIRE(pf.findPath(tc(10, 10), tc(55, 12), Locomotor::Tracked, second));
  CHECK(pf.stats().cacheHits == 1);
  CHECK(pf.expansionsLastCall() == work);  // charged as if searched: a game runs the same warm or cold
  CHECK(first == second);
  // From elsewhere in the same tile: the same tiles, smoothed from where the unit stands.
  REQUIRE(pf.findPath({10 * 256 + 30, 10 * 256 + 200}, tc(55, 12), Locomotor::Tracked, third));
  CHECK(pf.stats().cacheHits == 2);
  CHECK(third.back() == first.back());
  // The ground changes: the cache is forgotten.
  m.setBlock(20, 20, 7);
  REQUIRE(pf.findPath(tc(10, 10), tc(55, 12), Locomotor::Tracked, second));
  CHECK(pf.stats().cacheHits == 2);
}

TEST_CASE("a battle plays out the same with the route cache warm or cold") {
  UnitTypeRegistry types;
  WeaponRegistry weapons;
  std::string err;
  REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[
    {"id":"tank","model":"tank","speed":24,"turnTicks":2,"radius":110},
    {"id":"rifle","model":"rifleman","speed":12,"turnTicks":0,"radius":48,"locomotor":"foot"}]})"), err));
  auto run = [&](bool cache) {
    Map m(160, 160);
    m.generateCountryside(31);
    Simulation sim(m, types, weapons, 4);
    sim.pathfinder().setCache(cache);
    for (int g = 0; g < 6; ++g) {
      Command s;
      s.type = CommandType::Spawn;
      s.unitType = static_cast<uint16_t>(g % 2);
      s.player = 0;
      s.count = 8;
      s.target = tc(20 + g * 3, 20);
      sim.queue(s);
    }
    sim.tick();
    std::vector<Entity> all;
    for (uint32_t i : sim.registry().pool<CUnit>().entities()) all.push_back(sim.registry().handle(i));
    for (int leg = 0; leg < 3; ++leg) {
      // One at a time, so every unit asks for its own route (and the same trips come round again).
      for (Entity e : all) {
        Command c;
        c.type = CommandType::Move;
        c.units = {e};
        c.target = leg == 1 ? tc(24, 24) : tc(140, 130);
        sim.queue(c);
      }
      for (int i = 0; i < 15 * 40; ++i) sim.tick();
    }
    return std::pair<uint64_t, uint64_t>(sim.stateHash(), sim.pathfinder().stats().cacheHits);
  };
  const auto warm = run(true), cold = run(false);
  CHECK(warm.first == cold.first);
  CHECK(warm.second > 0);
  CHECK(cold.second == 0);
}

TEST_CASE("a flow field leads every tile it reaches to the goal, at the cost a search would find") {
  Map m = wallMap();
  Pathfinder pf(m);
  const Vec2i goal = tc(55, 12);
  const FlowField* f = pf.flowField(goal, Locomotor::Tracked);
  REQUIRE(f);
  CHECK(f->w == 64);
  CHECK(f->dirAt(55, 12) == FlowField::kGoal);
  CHECK(f->costAt(55, 12) == 0);
  CHECK(pf.stats().flowFields == 1);
  CHECK(pf.flowField(goal, Locomotor::Tracked) == f);  // kept
  CHECK(pf.stats().flowFields == 1);
  int reached = 0;
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x) {
      if (!m.passable(x, y, Locomotor::Tracked)) {
        CHECK(f->dirAt(x, y) == FlowField::kNone);
        continue;
      }
      // Follow the arrows: they must end at the goal, and never climb.
      int cx = x, cy = y, steps = 0, nx = 0, ny = 0;
      uint32_t last = f->costAt(cx, cy);
      bool down = true;
      while (f->next(cx, cy, nx, ny) && steps < 5000) {
        cx = nx;
        cy = ny;
        ++steps;
        down = down && f->costAt(cx, cy) < last;
        last = f->costAt(cx, cy);
      }
      CHECK(down);
      if (cx == 55 && cy == 12) ++reached;
      else FAIL_CHECK("no way from ", x, ",", y);
    }
  CHECK(reached > 3000);
  // From the far side of the wall the way runs through the gap.
  std::vector<Vec2i> path;
  REQUIRE(pf.flowPath(*f, tc(10, 10), path));
  CHECK(path.back() == goal);
  bool throughGap = false;
  Vec2i at = tc(10, 10);
  for (const Vec2i& p : path) {
    // Where the leg crosses x = 32 tiles it must be in the gap (rows 50..53).
    if ((at.x < 32 * 256) != (p.x < 32 * 256)) {
      const int64_t t = (int64_t{32 * 256} - at.x) * 1000 / (p.x - at.x);
      const int row = static_cast<int>((at.y + (p.y - at.y) * t / 1000) / 256);
      throughGap = row >= 49 && row <= 54;
    }
    at = p;
  }
  CHECK(throughGap);
  std::vector<Vec2i> searched;
  Pathfinder plain(m);
  REQUIRE(plain.findPath(tc(10, 10), goal, Locomotor::Tracked, searched));
  CHECK(routeLength(tc(10, 10), path) <= routeLength(tc(10, 10), searched) * 105 / 100);

  // A field over part of the map only, and one whose goal can't be reached from a walled-off pocket.
  const FlowField* part = pf.flowField(tc(8, 8), Locomotor::Foot, 0, 0, 24, 24);
  REQUIRE(part);
  CHECK(part->w == 25);
  CHECK(part->dirAt(30, 30) == FlowField::kNone);
  CHECK(part->dirAt(20, 20) < 8);
  for (int i = 0; i < 12; ++i) pf.flowField(tc(3 + i, 40), Locomotor::Foot);  // push the old ones out
  CHECK(pf.stats().flowFields == 14);
}

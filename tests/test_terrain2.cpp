// Terrain 2.0 (Milestone 9.2): speeds by ground, levelled building plots,
// graded roads and straight bridges set into high banks.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <set>

#include "slave/map/map.hpp"
#include "slave/sim/simulation.hpp"

using namespace slave;

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
Entity spawn(Simulation& sim, uint16_t type, uint8_t owner, Vec2i at) {
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns.push_back({type, owner, at, 0, -1});
  sim.queue(c);
  sim.tick();
  return sim.spawnLog().empty() ? Entity{} : sim.spawnLog().front().entity;
}
int avgHeight(const Map& m, int tx, int ty) {
  return (m.corner(tx, ty) + m.corner(tx + 1, ty) + m.corner(tx, ty + 1) + m.corner(tx + 1, ty + 1)) / 4;
}
}  // namespace

TEST_CASE("bridges run straight between high banks, their ends level with the deck") {
  int spans = 0;
  for (Biome b : {Biome::Temperate, Biome::Desert, Biome::Winter})
    for (uint64_t seed : {11u, 1940u, 77u})
      for (int size : {128, 160, 256}) {
      Map m;
      m.resize(size, size);
      m.generate(b, seed, 0);  // the river valley
      std::set<std::pair<int, int>> tiles;
      for (const Map::Lot& l : m.lots)
        if (l.kind == Map::kBridgeLot || l.kind == Map::kWoodBridgeLot) tiles.insert({l.tx, l.ty});
      // Walk each span: straight runs along x or y.
      std::set<std::pair<int, int>> seen;
      for (const auto& t : tiles) {
        if (seen.count(t)) continue;
        std::vector<std::pair<int, int>> span{t};
        seen.insert(t);
        for (size_t i = 0; i < span.size(); ++i)
          for (auto d : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const std::pair<int, int> n{span[i].first + d.first, span[i].second + d.second};
            if (tiles.count(n) && !seen.count(n)) {
              seen.insert(n);
              span.push_back(n);
            }
          }
        int x0 = 1 << 30, x1 = -1, y0 = 1 << 30, y1 = -1;
        for (const auto& s : span) {
          x0 = std::min(x0, s.first);
          x1 = std::max(x1, s.first);
          y0 = std::min(y0, s.second);
          y1 = std::max(y1, s.second);
        }
        ++spans;
        INFO("span at " << x0 << "," << y0 << " to " << x1 << "," << y1);
        CHECK((x0 == x1 || y0 == y1));  // straight, square to the grid
        CHECK(static_cast<int>(span.size()) == std::max(x1 - x0, y1 - y0) + 1);
        // Both end tiles are dry, high and level with each other.
        const int ea = avgHeight(m, x0, y0), eb = avgHeight(m, x1, y1);
        CHECK(ea >= m.waterLevel() + 12);
        CHECK(eb >= m.waterLevel() + 12);
        CHECK(ea == eb);
        // ... and something between them is water.
        bool wet = false;
        for (const auto& s : span) wet = wet || m.info(s.first, s.second).waterDepth > 0;
        CHECK(wet);
      }
      }
  CHECK(spans > 0);
}

TEST_CASE("ground sets the pace: slopes, mud and fords slow units; roads and bridges don't") {
  Map m(32, 32, 200);
  m.setWaterLevel(150);
  // A gentle ramp, a patch of mud, a ford: a road tile at the water's edge.
  for (int y = 0; y <= 32; ++y) m.setCorner(10, y, 215);
  for (int sy = 0; sy < 4; ++sy)
    for (int sx = 0; sx < 4; ++sx) m.paint(20 * SPLAT_PER_TILE + sx - 1, 20 * SPLAT_PER_TILE + sy - 1, Layer::Mud, 255);
  for (int y = 25; y <= 30; ++y)
    for (int x = 0; x <= 32; ++x) m.setCorner(x, y, 140);  // a channel of water
  for (int x = 5; x <= 7; ++x) m.setCorner(x, 25, 151), m.setCorner(x, 26, 151), m.setCorner(x, 24, 152);
  for (int sy = 0; sy < 6; ++sy)
    for (int sx = 0; sx < 4; ++sx) m.paint(5 * SPLAT_PER_TILE + sx, 23 * SPLAT_PER_TILE + sy, Layer::Road, 255);
  m.recomputeInfo();
  CHECK(m.speedPercent(2, 2, Locomotor::Wheeled) == 100);           // flat grass
  CHECK(m.speedPercent(9, 5, Locomotor::Wheeled) < 100);            // the ramp
  CHECK(m.speedPercent(20, 20, Locomotor::Wheeled) <= 65);          // mud: wheels bog down
  CHECK(m.speedPercent(20, 20, Locomotor::Foot) > m.speedPercent(20, 20, Locomotor::Wheeled));
  CHECK(m.info(5, 24).ford);
  CHECK(m.speedPercent(5, 24, Locomotor::Tracked) == 50);           // the ford: half speed
  CHECK(m.passable(5, 24, Locomotor::Wheeled));                     // ... but open to everything
  m.setBlock(5, 24, Map::kDeck);
  CHECK(m.speedPercent(5, 24, Locomotor::Tracked) == 100);          // a bridge deck is full speed
}

TEST_CASE("units cross mud slower, and the pathfinder goes round it when the road is near") {
  Rules R;
  Map m(48, 24, 200);
  // A wide band of mud across the middle, rows 4..19, with a road through it at x = 33..35.
  for (int sy = 4 * SPLAT_PER_TILE; sy < 20 * SPLAT_PER_TILE; ++sy)
    for (int sx = 0; sx < 48 * SPLAT_PER_TILE; ++sx) m.paint(sx, sy, sx / SPLAT_PER_TILE >= 33 && sx / SPLAT_PER_TILE <= 35 ? Layer::Road : Layer::Mud, 255);
  m.recomputeInfo();
  Pathfinder pf(m);
  std::vector<Vec2i> path;
  REQUIRE(pf.findPath({30 * 256 + 128, 2 * 256 + 128}, {30 * 256 + 128, 21 * 256 + 128}, Locomotor::Wheeled, path));
  bool viaRoad = false;
  for (const Vec2i& p : path) viaRoad = viaRoad || (p.x >> 8) >= 32;
  CHECK(viaRoad);  // a few tiles round by the road beat 16 tiles of mud for a car

  // The same truck takes longer through the mud than over grass.
  auto timeToCross = [&](bool mud) {
    Map g(48, 24, 200);
    if (mud)
      for (int sy = 0; sy < 24 * SPLAT_PER_TILE; ++sy)
        for (int sx = 0; sx < 48 * SPLAT_PER_TILE; ++sx) g.paint(sx, sy, Layer::Mud, 255);
    g.recomputeInfo();
    Simulation sim(g, R.types, R.weapons, 1);
    const Entity e = spawn(sim, R.t("ger_scout_car"), 0, {4 * 256, 12 * 256});
    Command mv;
    mv.type = CommandType::Move;
    mv.player = 0;
    mv.units = {e};
    mv.target = {20 * 256, 12 * 256};
    sim.queue(mv);
    for (int t = 1; t < 4000; ++t) {
      sim.tick();
      if (sim.registry().get<CPosition>(e).x >= 19 * 256) return t;
    }
    return 4000;
  };
  const int grass = timeToCross(false), mud = timeToCross(true);
  CHECK(mud > grass * 13 / 10);
}

TEST_CASE("a new building levels its plot and eases the ground round it") {
  Rules R;
  Map m(32, 32, 200);
  for (int y = 0; y <= 32; ++y)
    for (int x = 0; x <= 32; ++x) m.setCorner(x, y, static_cast<uint16_t>(200 + x * 4));  // a steady slope
  m.recomputeInfo();
  Simulation sim(m, R.types, R.weapons, 1);
  const Entity b = spawn(sim, R.t("ger_barracks"), 0, {16 * 256, 16 * 256});
  REQUIRE(b.valid());
  const CStructure& st = sim.registry().get<CStructure>(b);
  const int level = m.corner(st.tx, st.ty);
  for (int y = st.ty; y <= st.ty + st.h; ++y)
    for (int x = st.tx; x <= st.tx + st.w; ++x) CHECK(m.corner(x, y) == level);
  // Round it no step is steeper than the skirt allows, and far off nothing moved.
  for (int x = st.tx - 3; x < st.tx + st.w + 3; ++x) CHECK(std::abs(m.corner(x + 1, st.ty) - m.corner(x, st.ty)) <= 14);
  CHECK(m.corner(2, 2) == 208);
  // The renderer is told which ground changed.
  CHECK(!m.takeHeightEdits().empty());
}

// Milestone 9.3: scouts see far and keep their distance.
TEST_CASE("a scout on watch falls back from an enemy that comes close, but keeps to the area") {
  Rules R;
  CHECK(R.types.get(R.t("ger_scout_car")).scout);
  CHECK(R.types.get(R.t("ger_scout_car")).sight >= 2 * R.types.get(R.t("ger_rifleman")).sight - 2 * LEPTONS_PER_TILE);
  Map m(48, 24, 200);
  m.recomputeInfo();
  Simulation sim(m, R.types, R.weapons, 1);
  const Entity s = spawn(sim, R.t("ger_scout_car"), 0, {20 * 256, 12 * 256});
  const Entity t = spawn(sim, R.t("rus_light_tank"), 1, {34 * 256, 12 * 256});
  REQUIRE(s.valid());
  REQUIRE(t.valid());
  for (int i = 0; i < 30; ++i) sim.tick();
  CHECK(sim.registry().get<CPosition>(s).x == 20 * 256);  // far off: it just watches
  Command mv;
  mv.type = CommandType::Move;
  mv.player = 1;
  mv.units = {t};
  mv.target = {24 * 256, 12 * 256};
  sim.queue(mv);
  bool fellBack = false;
  for (int i = 0; i < 600; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events()) fellBack = fellBack || (e.type == EventType::ScoutFallsBack && e.owner == 0);
  }
  CHECK(fellBack);
  if (sim.registry().alive(s)) {
    const int x = sim.registry().get<CPosition>(s).x >> 8;
    CHECK(x < 20);       // away from the tank
    CHECK(x >= 20 - 12);  // but not off out of the area
  }
}

TEST_CASE("a scout given orders goes where it is told, enemy or not") {
  Rules R;
  Map m(48, 24, 200);
  m.recomputeInfo();
  Simulation sim(m, R.types, R.weapons, 1);
  const Entity s = spawn(sim, R.t("ger_scout_car"), 0, {10 * 256, 12 * 256});
  spawn(sim, R.t("rus_rifleman"), 1, {22 * 256, 12 * 256});
  Command mv;
  mv.type = CommandType::Move;
  mv.player = 0;
  mv.units = {s};
  mv.target = {19 * 256, 12 * 256};
  sim.queue(mv);
  bool fellBack = false;
  int reached = 0;
  for (int i = 1; i < 400 && !reached; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events()) fellBack = fellBack || e.type == EventType::ScoutFallsBack;
    if (sim.registry().alive(s) && sim.registry().get<CPosition>(s).x >= 18 * 256) reached = i;
  }
  CHECK_FALSE(fellBack);
  CHECK(reached > 0);
}

// Towns and villages (Milestone 9.8): compact settlements of row houses built
// wall to wall along streets and round courtyards.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

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

bool isBridge(const Map::Lot& l) { return l.kind == Map::kBridgeLot || l.kind == Map::kWoodBridgeLot; }
bool isRow(const Map::Lot& l) { return l.kind >= Map::kRowLot && l.kind < Map::kRowLot + Map::kRowLots; }

// The tiles a lot's building stands on.
std::vector<Vec2i> tilesOf(const Map::Lot& l) {
  const Vec2i sz = Map::lotFootprint(l.kind);
  std::vector<Vec2i> out;
  for (int y = 0; y < sz.y; ++y)
    for (int x = 0; x < sz.x; ++x) out.push_back({l.tx - sz.x / 2 + x, l.ty - sz.y / 2 + y});
  return out;
}

// What stands on each tile of a generated map: -1 nothing, else the lot's index.
std::vector<int> builtOn(const Map& m) {
  std::vector<int> grid(static_cast<size_t>(m.width()) * m.height(), -1);
  for (size_t i = 0; i < m.lots.size(); ++i)
    for (const Vec2i& t : tilesOf(m.lots[i]))
      if (m.inBounds(t.x, t.y)) grid[static_cast<size_t>(t.y) * m.width() + t.x] = static_cast<int>(i);
  return grid;
}

// Can a unit get from a to b across ground and round the buildings?
bool reach(const Map& m, const std::vector<int>& built, Vec2i a, Vec2i b, Locomotor loco) {
  std::vector<uint8_t> seen(static_cast<size_t>(m.width()) * m.height(), 0);
  std::vector<Vec2i> q{a};
  seen[static_cast<size_t>(a.y) * m.width() + a.x] = 1;
  for (size_t i = 0; i < q.size(); ++i) {
    if (q[i] == b) return true;
    for (const Vec2i d : {Vec2i{1, 0}, Vec2i{-1, 0}, Vec2i{0, 1}, Vec2i{0, -1}}) {
      const Vec2i n{q[i].x + d.x, q[i].y + d.y};
      if (!m.inBounds(n.x, n.y)) continue;
      const size_t at = static_cast<size_t>(n.y) * m.width() + n.x;
      if (seen[at] || built[at] >= 0 || !m.passable(n.x, n.y, loco)) continue;
      seen[at] = 1;
      q.push_back(n);
    }
  }
  return false;
}
}  // namespace

TEST_CASE("the generator knows the footprint of every building it places") {
  Rules R;
  for (int kind = 0; kind < Map::kRowLot + Map::kRowLots; ++kind) {
    CAPTURE(kind);
    const int t = R.types.find(Map::lotBuildingId(static_cast<uint8_t>(kind)));
    REQUIRE(t >= 0);
    const UnitTypeDef& d = R.types.get(t);
    CHECK(d.faction == "neutral");
    CHECK(Map::lotFootprint(static_cast<uint8_t>(kind)).x == d.footW);
    CHECK(Map::lotFootprint(static_cast<uint8_t>(kind)).y == d.footH);
  }
  for (uint8_t kind : {Map::kBridgeLot, Map::kWoodBridgeLot}) {
    const UnitTypeDef& d = R.types.get(R.types.find(Map::lotBuildingId(kind)));
    CHECK(Map::lotFootprint(kind).x == d.footW);
    CHECK(Map::lotFootprint(kind).y == d.footH);
  }
  // Row houses are small houses: a few men each, garrisoned like any other.
  for (int style = 0; style < Map::kRowLots; ++style) {
    const UnitTypeDef& d = R.types.get(R.types.find(Map::lotBuildingId(static_cast<uint8_t>(Map::kRowLot + style))));
    CHECK(d.garrison >= 3);
    CHECK(d.garrison <= 4);
    CHECK(d.maxHealth <= 500);
  }
}

TEST_CASE("villages are rows of houses built wall to wall, close together") {
  int maps = 0;
  for (int layout : {0, 1, 2, 5, 8, 10})
    for (uint64_t seed : {7ull, 1944ull})
      for (int size : {128, 192}) {
        CAPTURE(layout);
        CAPTURE(seed);
        CAPTURE(size);
        Map m(size, size);
        m.generate(Biome::Temperate, seed, layout);
        const std::vector<int> built = builtOn(m);
        auto at = [&](int x, int y) { return m.inBounds(x, y) ? built[static_cast<size_t>(y) * size + x] : -1; };
        int houses = 0, rows = 0, joined = 0;
        int64_t near = 0;
        for (size_t i = 0; i < m.lots.size(); ++i) {
          const Map::Lot& l = m.lots[i];
          if (isBridge(l)) continue;
          ++houses;
          // Nothing shares a tile, stands in the water or on a starting area.
          for (const Vec2i& t : tilesOf(l)) {
            REQUIRE(m.inBounds(t.x, t.y));
            CHECK(at(t.x, t.y) == static_cast<int>(i));
            CHECK(m.info(t.x, t.y).waterDepth < 0);
          }
          for (const Vec2i home : {Vec2i{size * 17 / 100, size / 2}, Vec2i{size * 83 / 100, size / 2}, Vec2i{size / 2, size * 17 / 100},
                                   Vec2i{size / 2, size * 83 / 100}})
            CHECK(std::max(std::abs(l.tx - home.x), std::abs(l.ty - home.y)) >= 13);
          // How many other buildings stand within five tiles.
          for (const Map::Lot& o : m.lots)
            if (&o != &l && !isBridge(o) && std::abs(o.tx - l.tx) <= 5 && std::abs(o.ty - l.ty) <= 5) ++near;
          if (!isRow(l)) continue;
          ++rows;
          bool touches = false, open = false;
          for (const Vec2i d : {Vec2i{1, 0}, Vec2i{-1, 0}, Vec2i{0, 1}, Vec2i{0, -1}}) {
            const int o = at(l.tx + d.x, l.ty + d.y);
            if (o >= 0) {
              // Only row houses stand hard against a row house.
              INFO("neighbour kind " << int(m.lots[static_cast<size_t>(o)].kind));
              CHECK(isRow(m.lots[static_cast<size_t>(o)]));
              touches = true;
            } else if (m.passable(l.tx + d.x, l.ty + d.y, Locomotor::Foot)) {
              open = true;
            }
          }
          joined += touches;
          CHECK(open);  // a door on open ground: men can get in
          // The house stands level, and level with the houses it is joined to.
          const uint16_t h = m.corner(l.tx, l.ty);
          CHECK(m.corner(l.tx + 1, l.ty) == h);
          CHECK(m.corner(l.tx, l.ty + 1) == h);
          CHECK(m.corner(l.tx + 1, l.ty + 1) == h);
        }
        if (houses == 0) continue;  // (a layout without roads has no villages)
        ++maps;
        INFO(houses << " buildings, " << rows << " row houses, " << joined << " joined, " << near / houses << " neighbours each");
        CHECK(houses >= 15);
        CHECK(rows * 10 >= houses * 6);    // mostly row houses
        CHECK(joined * 10 >= rows * 7);    // mostly standing in a row
        CHECK(near >= houses * 6);         // close together (the old hamlets had one or two neighbours)
      }
  CHECK(maps >= 20);
}

TEST_CASE("the market town has blocks round courtyards, and the land is not walled off") {
  for (uint64_t seed : {3ull, 7ull, 11ull, 1944ull})
    for (Biome b : {Biome::Temperate, Biome::Winter, Biome::Desert}) {
      CAPTURE(seed);
      CAPTURE(static_cast<int>(b));
      Map m(160, 160);
      m.generate(b, seed, static_cast<int>(MapLayout::Crossroads));
      const std::vector<int> built = builtOn(m);
      auto at = [&](int x, int y) { return m.inBounds(x, y) ? built[static_cast<size_t>(y) * 160 + x] : -1; };
      // A block: a free tile with row houses on all four sides at some distance (a yard inside a ring).
      int yards = 0, rows = 0, town = 0;
      for (const Map::Lot& l : m.lots) {
        rows += isRow(l);
        town += !isBridge(l) && std::abs(l.tx - 80) <= 22 && std::abs(l.ty - 80) <= 22;
      }
      for (int y = 2; y < 158; ++y)
        for (int x = 2; x < 158; ++x) {
          if (at(x, y) >= 0) continue;
          bool ringed = true;
          for (const Vec2i d : {Vec2i{1, 0}, Vec2i{-1, 0}, Vec2i{0, 1}, Vec2i{0, -1}}) {
            bool wall = false;
            for (int k = 1; k <= 3 && !wall; ++k) {
              const int o = at(x + d.x * k, y + d.y * k);
              wall = o >= 0 && isRow(m.lots[static_cast<size_t>(o)]);
            }
            ringed = ringed && wall;
          }
          yards += ringed;
        }
      INFO(rows << " row houses, " << town << " buildings in town, " << yards << " yard tiles");
      CHECK(town >= 40);
      CHECK(yards >= 4);
    }
  // Buildings counted as obstacles, every base still reaches every other, on
  // tracks and on foot, on every layout.
  for (int layout = 0; layout < static_cast<int>(MapLayout::Count); ++layout)
    for (Biome b : {Biome::Temperate, Biome::Desert}) {
      CAPTURE(layout);
      CAPTURE(static_cast<int>(b));
      const int size = 128;
      Map m(size, size);
      m.generate(b, 500 + static_cast<uint64_t>(layout), layout);
      const std::vector<int> built = builtOn(m);
      const Vec2i west{size * 17 / 100, size / 2}, east{size * 83 / 100, size / 2}, north{size / 2, size * 17 / 100}, south{size / 2, size * 83 / 100};
      CHECK(reach(m, built, west, east, Locomotor::Tracked));
      CHECK(reach(m, built, north, south, Locomotor::Tracked));
      CHECK(reach(m, built, west, south, Locomotor::Foot));
      // And nothing open is shut in: every yard and lane can be walked to.
      std::vector<uint8_t> seen(static_cast<size_t>(size) * size, 0);
      std::vector<Vec2i> q{west};
      seen[static_cast<size_t>(west.y) * size + west.x] = 1;
      for (size_t i = 0; i < q.size(); ++i)
        for (const Vec2i d : {Vec2i{1, 0}, Vec2i{-1, 0}, Vec2i{0, 1}, Vec2i{0, -1}}) {
          const Vec2i n{q[i].x + d.x, q[i].y + d.y};
          if (!m.inBounds(n.x, n.y)) continue;
          const size_t k = static_cast<size_t>(n.y) * size + n.x;
          if (seen[k] || built[k] >= 0 || !m.passable(n.x, n.y, Locomotor::Foot)) continue;
          seen[k] = 1;
          q.push_back(n);
        }
      int shutIn = 0;
      for (const Map::Lot& l : m.lots) {
        if (!isRow(l)) continue;
        for (const Vec2i d : {Vec2i{1, 0}, Vec2i{-1, 0}, Vec2i{0, 1}, Vec2i{0, -1}}) {
          const Vec2i n{l.tx + d.x, l.ty + d.y};
          const size_t k = static_cast<size_t>(n.y) * size + n.x;
          if (built[k] < 0 && m.passable(n.x, n.y, Locomotor::Foot) && !seen[k]) ++shutIn;
        }
      }
      CHECK(shutIn == 0);
    }
}

TEST_CASE("timber country builds apart; open country has no settlements; the same seed gives the same town") {
  // Log houses and stilt huts stand apart (but along the street all the same).
  for (Biome b : {Biome::Winter, Biome::Tropical}) {
    Map m(128, 128);
    m.generate(b, 5, static_cast<int>(MapLayout::RiverValley));
    int houses = 0;
    for (const Map::Lot& l : m.lots) {
      CHECK(!isRow(l));
      houses += !isBridge(l);
    }
    CHECK(houses >= 12);
  }
  // Without settlements: bridges only, and the land is the same away from where the villages stood.
  Map with(128, 128), without(128, 128), again(128, 128);
  with.generate(Biome::Temperate, 7, static_cast<int>(MapLayout::RiverValley));
  without.generate(Biome::Temperate, 7, static_cast<int>(MapLayout::RiverValley), false);
  again.generate(Biome::Temperate, 7, static_cast<int>(MapLayout::RiverValley));
  for (const Map::Lot& l : without.lots) CHECK(isBridge(l));
  CHECK(with.supplyPoints == without.supplyPoints);
  int bridgesWith = 0, bridgesWithout = 0;
  for (const Map::Lot& l : with.lots) bridgesWith += isBridge(l);
  for (const Map::Lot& l : without.lots) bridgesWithout += isBridge(l);
  CHECK(bridgesWith == bridgesWithout);
  int changedFar = 0, changedNear = 0;
  for (int cy = 0; cy <= 128; ++cy)
    for (int cx = 0; cx <= 128; ++cx) {
      if (with.corner(cx, cy) == without.corner(cx, cy)) continue;
      bool near = false;
      for (const Map::Lot& l : with.lots) near = near || (std::abs(l.tx - cx) <= 9 && std::abs(l.ty - cy) <= 9);
      (near ? changedNear : changedFar) += 1;
    }
  CHECK(changedFar == 0);
  CHECK(changedNear > 0);  // the ground under the rows was levelled
  REQUIRE(again.lots.size() == with.lots.size());
  for (size_t i = 0; i < with.lots.size(); ++i) {
    CHECK(again.lots[i].tx == with.lots[i].tx);
    CHECK(again.lots[i].ty == with.lots[i].ty);
    CHECK(again.lots[i].kind == with.lots[i].kind);
  }
  CHECK(again.heights() == with.heights());
}

TEST_CASE("men occupy a house in the middle of a row and fight from it") {
  Rules R;
  const int32_t T = LEPTONS_PER_TILE;
  Map map(64, 64, 300);
  Simulation sim(map, R.types, R.weapons, 3);
  Command c;
  c.type = CommandType::SpawnList;
  for (int i = 0; i < 5; ++i)
    c.spawns.push_back({R.t(i == 2 ? "neutral_row_brick" : "neutral_row"), NEUTRAL_OWNER, {(20 + i) * T + T / 2, 20 * T + T / 2}, 0, -1});
  sim.queue(c);
  sim.tick();
  REQUIRE(sim.spawnLog().size() == 5);
  const Entity house = sim.spawnLog()[2].entity;
  // Wall to wall: the five tiles in a row are all taken, the tiles in front are free.
  for (int i = 0; i < 5; ++i) CHECK(sim.structureAt(20 + i, 20).valid());
  CHECK(sim.structureAt(22, 20) == house);
  CHECK(!sim.structureAt(22, 21).valid());
  c.spawns.clear();
  std::vector<Entity> men;
  for (int i = 0; i < 4; ++i) c.spawns.push_back({R.t("gbr_rifleman"), 1, {(21 + i) * T, 24 * T}, 0, -1});
  sim.queue(c);
  sim.tick();
  for (const auto& s : sim.spawnLog()) men.push_back(s.entity);
  REQUIRE(men.size() == 4);
  Command occ;
  occ.type = CommandType::Occupy;
  occ.player = 1;
  occ.units = men;
  occ.targetUnit = house;
  sim.queue(occ);
  for (int i = 0; i < 15 * 20; ++i) sim.tick();
  int inside = 0;
  for (Entity m : men) inside += sim.registry().has<CGarrisoned>(m);
  CHECK(inside == 4);  // the brick house holds four
}

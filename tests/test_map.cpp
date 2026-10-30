#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "slave/map/map_io.hpp"
#include "slave/sim/simulation.hpp"

using namespace slave;

TEST_CASE("map sizes are rounded to multiples of 32 and clamped") {
  CHECK(Map::roundSize(10) == 32);
  CHECK(Map::roundSize(100) == 96);
  CHECK(Map::roundSize(112) == 128);
  CHECK(Map::roundSize(5000) == 1024);
  Map m(100, 40);
  CHECK(m.width() == 96);
  CHECK(m.height() == 32);
  CHECK(m.heights().size() == size_t(97 * 33));
}

TEST_CASE("height interpolation is exact at corners and linear between") {
  Map m(32, 32, 100);
  m.setCorner(1, 0, 200);
  CHECK(m.heightAtFine(256, 0) == 200 * 256);
  CHECK(m.heightAtFine(128, 0) == 150 * 256);
  CHECK(m.heightAtFine(0, 0) == 100 * 256);
  CHECK(m.heightAt(128.f, 0.f) == doctest::Approx(150.f));
}

TEST_CASE("painting keeps layer weights summing to 255") {
  Map m(32, 32);
  for (int i = 0; i < 50; ++i) {
    m.paint(5, 5, static_cast<Layer>(i % LAYER_COUNT), 37);
    int sum = 0;
    for (int l = 0; l < LAYER_COUNT; ++l) sum += m.splat(5, 5)[l];
    CHECK(sum == 255);
  }
  m.paint(3, 3, Layer::Mud, 255);
  CHECK(m.splat(3, 3)[int(Layer::Mud)] == 255);
  CHECK(m.dominantLayer(3, 3) == Layer::Mud);
}

TEST_CASE("slopes and water decide passability per locomotor") {
  Map m(32, 32, 100);
  m.setWaterLevel(80);
  m.recomputeInfo();
  CHECK(m.passable(4, 4, Locomotor::Tracked));
  // A cliff: one corner 60 units higher.
  m.setCorner(10, 10, 160);
  m.recomputeInfo();
  CHECK_FALSE(m.passable(9, 9, Locomotor::Wheeled));
  CHECK_FALSE(m.passable(10, 10, Locomotor::Foot));
  // A pond: 20 units deep.
  for (int y = 20; y <= 22; ++y)
    for (int x = 20; x <= 22; ++x) m.setCorner(x, y, 60);
  m.recomputeInfo();
  CHECK_FALSE(m.passable(20, 20, Locomotor::Tracked));
  CHECK_FALSE(m.passable(20, 20, Locomotor::Foot));
  CHECK_FALSE(m.passable(-1, 0, Locomotor::Foot));
}

TEST_CASE("generated countryside is deterministic and has water, roads and woods") {
  Map a(128, 128), b(128, 128);
  a.generateCountryside(77);
  b.generateCountryside(77);
  CHECK(a.heights() == b.heights());
  CHECK(a.props().size() == b.props().size());
  int water = 0, road = 0;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x)
      if (a.info(x, y).waterDepth > 0) ++water;
  for (int y = 0; y < a.splatH(); ++y)
    for (int x = 0; x < a.splatW(); ++x)
      if (a.dominantLayer(x, y) == Layer::Road) ++road;
  CHECK(water > 100);
  CHECK(road > 200);
  CHECK(a.props().size() > 200);
  Map c(128, 128);
  c.generateCountryside(78);
  CHECK(c.heights() != a.heights());
}

TEST_CASE("map files round-trip exactly") {
  Map m(64, 96);
  m.generateCountryside(5);
  m.name = "Test Valley";
  m.author = "makorain22";
  std::vector<UnitPlacement> units = {{"ger_medium_tank", 0, 1234, 5678, 512, -1},
                                      {"rus_rifleman", 1, 9000, 100, 0, 40}};
  const auto bytes = encodeMap(m, units);
  MapFile f;
  std::string err;
  REQUIRE(decodeMap(bytes, f, err));
  CHECK(f.map.width() == 64);
  CHECK(f.map.height() == 96);
  CHECK(f.map.heights() == m.heights());
  CHECK(f.map.splatData() == m.splatData());
  CHECK(f.map.waterLevel() == m.waterLevel());
  CHECK(f.map.props().size() == m.props().size());
  CHECK(f.map.name == "Test Valley");
  REQUIRE(f.units.size() == 2);
  CHECK(f.units[1].type == "rus_rifleman");
  CHECK(f.units[1].health == 40);
  CHECK(f.units[0].facing == 512);
  CHECK(f.players.empty());  // no sides saved: the game uses its defaults
  // Compressed: a 64x96 map should be far smaller than its raw arrays.
  CHECK(bytes.size() < m.splatData().size() / 3);
}

TEST_CASE("damaged or foreign map files are rejected, never crash") {
  MapFile f;
  std::string err;
  CHECK_FALSE(decodeMap({}, f, err));
  CHECK_FALSE(decodeMap({'P', 'K', 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, f, err));
  Map m(32, 32);
  auto bytes = encodeMap(m, {});
  bytes[4] = 99;  // future version
  CHECK_FALSE(decodeMap(bytes, f, err));
  bytes = encodeMap(m, {});
  bytes.resize(bytes.size() - 10);  // truncated
  CHECK_FALSE(decodeMap(bytes, f, err));
  bytes = encodeMap(m, {});
  bytes[bytes.size() / 2] ^= 0x5a;  // corrupted
  CHECK_FALSE(decodeMap(bytes, f, err));
}

TEST_CASE("units stop at cliffs instead of driving through") {
  Map map(32, 32, 100);
  for (int y = 0; y <= 32; ++y) map.setCorner(16, y, 400);  // a ridge across the map
  map.recomputeInfo();
  UnitTypeRegistry types;
  std::string err;
  REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[{"id":"t","model":"m","speed":24,"turnTicks":0,"locomotor":"tracked"}]})"), err));
  WeaponRegistry noWeapons;
  Simulation sim(map, types, noWeapons, 1);
  Command spawn;
  spawn.type = CommandType::Spawn;
  spawn.target = {10 * 256, 10 * 256};
  sim.queue(spawn);
  sim.tick();
  const Entity e = sim.registry().handle(sim.registry().pool<CUnit>().entities()[0]);
  Command move;
  move.type = CommandType::Move;
  move.units = {e};
  move.target = {25 * 256, 10 * 256};
  sim.queue(move);
  for (int i = 0; i < 300; ++i) sim.tick();
  CHECK(sim.registry().get<CPosition>(e).x < 15 * 256);
  CHECK_FALSE(sim.registry().get<CMover>(e).active);
}

TEST_CASE("scenario sides round-trip with the map") {
  Map m(64, 64);
  std::vector<PlayerSetup> players(3);
  players[0] = {"Wehrmacht", "ger", 0xFF2C3AC4, 0, true, true, 1000, 2000};
  players[1] = {"Red Army", "rus", 0xFFC46034, 1, true, false, -1, -1};
  players[2] = {"Partisans", "rus", 0xFF48A042, 1, false, false, 300, 300};
  MapFile f;
  std::string err;
  REQUIRE(decodeMap(encodeMap(m, {}, players), f, err));
  REQUIRE(f.players.size() == 3);
  CHECK(f.players[0].name == "Wehrmacht");
  CHECK(f.players[0].startY == 2000);
  CHECK(f.players[1].faction == "rus");
  CHECK_FALSE(f.players[1].human);
  CHECK(f.players[2].team == 1);
  CHECK_FALSE(f.players[2].active);
  CHECK(f.players[2].color == 0xFF48A042u);
}

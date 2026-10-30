// Structures, oil, production and fortifications.
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
      "warheads": {"bullet": {"infantry":100,"heavy":2,"structure":2}, "he": {"infantry":100,"structure":90}},
      "weapons": [{"id":"rifle","warhead":"bullet","damage":20,"range":5,"cooldown":10,"accuracy":100},
                  {"id":"howitzer","warhead":"he","damage":100,"range":12,"cooldown":30,"projectile":"shell","speed":200,"splash":0.3}]})"), err));
    REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[
      {"id":"rifle","model":"rifleman","speed":12,"turnTicks":0,"radius":28,"health":100,"armor":"infantry","sight":7,
       "weapons":["rifle"],"locomotor":"foot","cost":100,"buildTime":2,"builtAt":"barracks"},
      {"id":"truck","model":"truck","speed":40,"turnTicks":0,"radius":80,"health":400,"armor":"light","sight":5,
       "locomotor":"wheeled","capacity":100,"cost":1000,"buildTime":5,"builtAt":"factory"},
      {"id":"mcv","model":"mcv","speed":20,"turnTicks":0,"radius":90,"health":800,"armor":"light","locomotor":"wheeled",
       "deploysTo":"yard"},
      {"id":"gun","model":"tank","speed":10,"turnTicks":0,"radius":90,"health":500,"armor":"heavy","sight":14,"weapons":["howitzer"]}
    ]})"), err));
    REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[
      {"id":"yard","model":"yard","footprint":[4,3],"health":2000,"armor":"structure","role":"yard","buildRadius":6,"power":10},
      {"id":"power","model":"power","footprint":[2,2],"health":700,"armor":"structure","power":100,"cost":300,"buildTime":4,
       "builtAt":"yard","requires":["yard"],"buildRadius":4},
      {"id":"refinery","model":"refinery","footprint":[3,3],"health":1000,"armor":"structure","power":-30,"cost":1000,
       "buildTime":4,"builtAt":"yard","role":"refinery","freeUnit":"truck","buildRadius":4},
      {"id":"barracks","model":"barracks","footprint":[3,2],"health":800,"armor":"structure","power":-200,"cost":500,
       "buildTime":4,"builtAt":"yard","role":"barracks","requires":["power"],"buildRadius":4},
      {"id":"trench","model":"trench","footprint":[1,1],"health":999,"armor":"structure","category":"fortification",
       "cost":20,"cover":60,"blocks":["wheeled"],"targetable":false,"autoTarget":false,"builtAt":"yard"}
    ]})"), err));
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
  }
  int t(const char* id) const { return types.find(id); }
};

Entity spawnOne(Simulation& sim, int type, uint8_t owner, Vec2i at) {
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{static_cast<uint16_t>(type), owner, at, 0, -1}};
  sim.queue(c);
  sim.tick();
  return sim.spawnLog().empty() ? kNullEntity : sim.spawnLog().front().entity;
}

int countType(Simulation& sim, int type) {
  int n = 0;
  for (uint32_t e : sim.registry().pool<CUnit>().entities()) n += sim.registry().pool<CUnit>().get(e).type == type;
  return n;
}

void ticks(Simulation& sim, int n) {
  for (int i = 0; i < n; ++i) sim.tick();
}
}  // namespace

TEST_CASE("structures occupy their footprint, block paths, and free it when gone") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 1);
  const Entity yard = spawnOne(sim, w.t("yard"), 0, {20 * 256, 20 * 256});
  REQUIRE(yard.valid());
  const CStructure& s = sim.registry().get<CStructure>(yard);
  CHECK(s.w == 4);
  CHECK(s.h == 3);
  CHECK_FALSE(w.map.passable(s.tx, s.ty, Locomotor::Foot));
  CHECK(sim.structureAt(s.tx + 3, s.ty + 2) == yard);
  // A second structure can't overlap.
  CHECK_FALSE(spawnOne(sim, w.t("power"), 0, {20 * 256, 20 * 256}).valid());
  Command r;
  r.type = CommandType::Remove;
  r.units = {yard};
  sim.queue(r);
  sim.tick();
  CHECK(w.map.passable(s.tx, s.ty, Locomotor::Foot) == true);
  CHECK_FALSE(sim.structureAt(21, 21).valid());
}

TEST_CASE("building a structure: queue, pay as it builds, place within range") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 1);
  spawnOne(sim, w.t("yard"), 0, {20 * 256, 20 * 256});
  Command money;
  money.type = CommandType::SetCredits;
  money.player = 0;
  money.amount = 1000;
  sim.queue(money);
  Command p;
  p.type = CommandType::Produce;
  p.player = 0;
  p.unitType = static_cast<uint16_t>(w.t("power"));
  p.amount = 1;
  sim.queue(p);
  ticks(sim, 70);
  CHECK(sim.playerState(0).queues[static_cast<size_t>(Queue::Buildings)].ready);
  CHECK(sim.playerState(0).credits == 700);
  // Too far from the yard: refused.
  Command place;
  place.type = CommandType::PlaceBuilding;
  place.player = 0;
  place.unitType = p.unitType;
  place.target = {50 * 256, 50 * 256};
  sim.queue(place);
  sim.tick();
  CHECK(countType(sim, w.t("power")) == 0);
  place.target = {26 * 256, 20 * 256};
  sim.queue(place);
  sim.tick();
  CHECK(countType(sim, w.t("power")) == 1);
  CHECK_FALSE(sim.playerState(0).queues[static_cast<size_t>(Queue::Buildings)].ready);
  CHECK(sim.playerState(0).powerMade == 110);
}

TEST_CASE("prerequisites, low power and running out of money slow or stop production") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 1);
  spawnOne(sim, w.t("yard"), 0, {20 * 256, 20 * 256});
  Command money;
  money.type = CommandType::SetCredits;
  money.player = 0;
  money.amount = 5000;
  sim.queue(money);
  CHECK_FALSE(sim.canProduce(0, w.t("barracks")));  // needs a power station
  spawnOne(sim, w.t("power"), 0, {26 * 256, 20 * 256});
  CHECK(sim.canProduce(0, w.t("barracks")));
  CHECK_FALSE(sim.canProduce(0, w.t("rifle")));  // no barracks yet
  spawnOne(sim, w.t("barracks"), 0, {20 * 256, 26 * 256});
  CHECK(sim.playerState(0).lowPower());  // the barracks uses more than the station makes
  Command p;
  p.type = CommandType::Produce;
  p.player = 0;
  p.unitType = static_cast<uint16_t>(w.t("rifle"));
  p.amount = 2;
  sim.queue(p);
  ticks(sim, 62);  // 2 s of work at half speed takes 4 s: one rifleman done
  CHECK(countType(sim, w.t("rifle")) == 1);
  ticks(sim, 61);
  CHECK(countType(sim, w.t("rifle")) == 2);
  CHECK(sim.playerState(0).credits == 4800);
}

TEST_CASE("a refinery's supply truck turns oil into supplies") {
  World w;
  w.map.addOilField(30, 20, 3, 7);
  Simulation sim(w.map, w.types, w.weapons, 1);
  spawnOne(sim, w.t("refinery"), 0, {22 * 256, 20 * 256});
  CHECK(countType(sim, w.t("truck")) == 1);  // comes free
  int oilBefore = 0;
  for (uint8_t v : w.map.oilData()) oilBefore += v;
  ticks(sim, 15 * 60);
  int oilAfter = 0;
  for (uint8_t v : w.map.oilData()) oilAfter += v;
  CHECK(sim.playerState(0).credits > 500);
  CHECK(oilAfter < oilBefore);
  CHECK(sim.playerState(0).credits == (oilBefore - oilAfter - [&] {
          for (uint32_t e : sim.registry().pool<CHarvester>().entities()) return sim.registry().pool<CHarvester>().get(e).cargo;
          return 0;
        }()) * 5);
}

TEST_CASE("a construction truck unfolds into a yard; selling refunds half") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 1);
  const Entity mcv = spawnOne(sim, w.t("mcv"), 0, {30 * 256, 30 * 256});
  Command d;
  d.type = CommandType::Deploy;
  d.units = {mcv};
  sim.queue(d);
  sim.tick();
  CHECK(countType(sim, w.t("mcv")) == 0);
  CHECK(countType(sim, w.t("yard")) == 1);
  const Entity power = spawnOne(sim, w.t("power"), 0, {36 * 256, 30 * 256});
  Command s;
  s.type = CommandType::Sell;
  s.units = {power};
  sim.queue(s);
  sim.tick();
  CHECK(countType(sim, w.t("power")) == 0);
  CHECK(sim.playerState(0).credits == 150);
}

TEST_CASE("infantry in a trench take far less damage; shells hit structures they land on") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 1);
  Command money;
  money.type = CommandType::SetCredits;
  money.player = 0;
  money.amount = 100;
  sim.queue(money);
  spawnOne(sim, w.t("yard"), 0, {10 * 256, 10 * 256});
  Command walls;
  walls.type = CommandType::PlaceWalls;
  walls.player = 0;
  walls.unitType = static_cast<uint16_t>(w.t("trench"));
  walls.spawns = {{0, 0, {14 * 256 + 128, 16 * 256 + 128}, 0, -1}, {0, 0, {15 * 256 + 128, 16 * 256 + 128}, 0, -1}};
  sim.queue(walls);
  sim.tick();
  sim.tick();  // cover is worked out on the next economy step
  CHECK(countType(sim, w.t("trench")) == 2);
  CHECK(sim.playerState(0).credits == 60);
  CHECK(sim.coverAt(14, 16) == 60);
  CHECK(w.map.passable(14, 16, Locomotor::Foot));
  CHECK_FALSE(w.map.passable(14, 16, Locomotor::Wheeled));

  // Two riflemen, one in the trench, one in the open, each shot by an enemy.
  const Entity inTrench = spawnOne(sim, w.t("rifle"), 0, {14 * 256 + 128, 16 * 256 + 128});
  const Entity inOpen = spawnOne(sim, w.t("rifle"), 0, {14 * 256 + 128, 24 * 256 + 128});
  for (Entity e : {inTrench, inOpen}) {
    Command st;
    st.type = CommandType::SetStance;
    st.units = {e};
    st.stance = Stance::HoldFire;
    sim.queue(st);
  }
  const Entity a = spawnOne(sim, w.t("rifle"), 1, {18 * 256, 16 * 256 + 128});
  const Entity b = spawnOne(sim, w.t("rifle"), 1, {18 * 256, 24 * 256 + 128});
  for (auto [shooter, target] : {std::pair{a, inTrench}, std::pair{b, inOpen}}) {
    Command at;
    at.type = CommandType::Attack;
    at.units = {shooter};
    at.targetUnit = target;
    sim.queue(at);
  }
  ticks(sim, 25);
  REQUIRE(sim.registry().alive(inTrench));
  const int32_t trenchHp = sim.registry().get<CUnit>(inTrench).health;
  const int32_t openHp = sim.registry().alive(inOpen) ? sim.registry().get<CUnit>(inOpen).health : 0;
  CHECK(trenchHp > openHp);
  CHECK(100 - trenchHp < (100 - openHp) / 2);
}

TEST_CASE("players with nothing left are defeated") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 1);
  const Entity r = spawnOne(sim, w.t("rifle"), 1, {10 * 256, 10 * 256});
  ticks(sim, 15);
  CHECK_FALSE(sim.playerState(1).defeated);
  Command k;
  k.type = CommandType::Kill;
  k.units = {r};
  sim.queue(k);
  bool announced = false;
  for (int i = 0; i < 20; ++i) {
    sim.tick();
    for (const SimEvent& ev : sim.events()) announced |= ev.type == EventType::PlayerDefeated && ev.owner == 1;
  }
  CHECK(announced);
  CHECK(sim.playerState(1).defeated);
  CHECK_FALSE(sim.playerState(0).defeated);  // never had anything: not "defeated"
}

// The soldier under fire (Milestone 9.15, part 2): suppression, prone and
// pinned, morale and breaking, the ambush volley, minefields, observers for
// the guns, and sight at night and in bad weather.
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

namespace {
constexpr int32_t T = 256;
struct World {
  Map map{96, 96, 300};
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  World() {
    std::string err;
    REQUIRE_MESSAGE(weapons.loadJson(nlohmann::json::parse(R"({
      "warheads": {"bullet": {"infantry":100,"light":30,"medium":6,"heavy":3,"structure":5},
                   "he": {"infantry":100,"light":80,"medium":40,"heavy":25,"structure":60},
                   "ap": {"infantry":20,"light":90,"medium":100,"heavy":100,"structure":30}},
      "weapons": [
        {"id":"rifle","warhead":"bullet","damage":14,"range":5,"cooldown":22,"accuracy":100},
        {"id":"mg","warhead":"bullet","damage":2,"range":9,"cooldown":3,"accuracy":20},
        {"id":"atgun","warhead":"ap","damage":40,"range":6,"cooldown":40,"accuracy":100},
        {"id":"howitzer","warhead":"he","damage":60,"range":30,"cooldown":20,"projectile":"shell","speed":200,"splash":0.6},
        {"id":"mine_at","warhead":"ap","damage":500,"range":1,"cooldown":1,"projectile":"shell","speed":400,"splash":0.6}
      ]})"), err), err);
    REQUIRE_MESSAGE(types.loadJson(nlohmann::json::parse(R"({"units":[
      {"id":"rifle","model":"rifleman","speed":12,"turnTicks":0,"radius":40,"health":100,"armor":"infantry",
       "sight":6,"weapons":["rifle"],"locomotor":"foot"},
      {"id":"mg","model":"mg_team","speed":10,"turnTicks":0,"radius":40,"health":100000,"armor":"infantry",
       "sight":10,"weapons":["mg"],"locomotor":"foot"},
      {"id":"at","model":"at_team","speed":10,"turnTicks":0,"radius":40,"health":100,"armor":"infantry",
       "sight":7,"weapons":["atgun"],"locomotor":"foot"},
      {"id":"engineer","model":"engineer","speed":12,"turnTicks":0,"radius":40,"health":100,"armor":"infantry",
       "sight":6,"locomotor":"foot","repair":2},
      {"id":"observer","model":"sniper","speed":12,"turnTicks":0,"radius":40,"health":100,"armor":"infantry",
       "sight":12,"locomotor":"foot","spotter":true},
      {"id":"tank","model":"tank","speed":20,"turnTicks":2,"radius":110,"health":2000,"armor":"heavy","sight":5},
      {"id":"gun","model":"artillery","speed":8,"turnTicks":2,"radius":90,"health":300,"armor":"light","sight":5,
       "weapons":["howitzer"],"turret":{"turnTicks":1}},
      {"id":"yard","model":"yard","armor":"structure","footprint":[3,3],"health":2000,"sight":5,"role":"yard"},
      {"id":"mines","model":"mines","armor":"structure","footprint":[1,1],"health":40,"category":"fortification",
       "blocks":[],"autoTarget":false,"targetable":false,"mine":{"weapon":"mine_at","against":"vehicles"}}
    ]})"), err), err);
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
  }
  int t(const char* id) const {
    const int i = types.find(id);
    REQUIRE(i >= 0);
    return i;
  }
};

Vec2i tile(int x, int y) { return {x * T + 128, y * T + 128}; }
Entity spawn(Simulation& sim, int type, uint8_t owner, Vec2i at) {
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{static_cast<uint16_t>(type), owner, at, 0, -1}};
  sim.queue(c);
  sim.tick();
  return sim.spawnLog().empty() ? kNullEntity : sim.spawnLog().front().entity;
}
void order(Simulation& sim, CommandType type, uint8_t player, std::vector<Entity> units, Vec2i at = {}) {
  Command c;
  c.type = type;
  c.player = player;
  c.units = std::move(units);
  c.target = at;
  sim.queue(c);
}
void run(Simulation& sim, int ticks) {
  for (int i = 0; i < ticks; ++i) sim.tick();
}
const CCondition* cond(Simulation& sim, Entity e) { return sim.registry().pool<CCondition>().tryGet(e.index); }
}  // namespace

TEST_CASE("machine-gun fire pins men down; pinned men don't move; it wears off once the firing stops") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 3);
  const Entity man = spawn(sim, w.t("rifle"), 1, tile(30, 40));
  sim.registry().get<CUnit>(man).health = 100000;  // lives through it
  sim.registry().get<CUnit>(man).stance = Stance::HoldFire;
  const Entity mg = spawn(sim, w.t("mg"), 0, tile(22, 40));
  Posture worst = Posture::Standing;
  for (int i = 0; i < 15 * 20; ++i) {
    sim.tick();
    if (const CCondition* c = cond(sim, man)) worst = std::max(worst, c->posture);
  }
  REQUIRE(cond(sim, man) != nullptr);
  CHECK(worst == Posture::Pinned);
  CHECK(sim.conditionSpeedPercent(man.index) == 0);
  CHECK(sim.conditionShelterPercent(man.index) < 100);
  // Ordered to run while pinned: he stays put.
  const CPosition before = sim.registry().get<CPosition>(man);
  order(sim, CommandType::Move, 1, {man}, tile(40, 40));
  run(sim, 30);
  CHECK(sim.registry().get<CPosition>(man).x == before.x);
  // The gun falls silent: the men get up again.
  order(sim, CommandType::Kill, 0, {mg});
  run(sim, 15 * 20);
  CHECK(cond(sim, man)->posture == Posture::Standing);
  CHECK(cond(sim, man)->suppression < 400);
}

TEST_CASE("long, heavy fire breaks men; they run for their base and steady in quiet") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 5);
  spawn(sim, w.t("yard"), 1, tile(70, 40));
  const Entity man = spawn(sim, w.t("rifle"), 1, tile(30, 40));
  sim.registry().get<CUnit>(man).health = 100000;
  const Entity mg = spawn(sim, w.t("mg"), 0, tile(23, 40));
  bool broke = false;
  for (int i = 0; i < 15 * 90 && !broke; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events()) broke = broke || (e.type == EventType::UnitBroke && e.owner == 1);
  }
  REQUIRE(broke);
  CHECK(sim.routing(man.index));
  CHECK(sim.registry().get<COrder>(man).forced);
  order(sim, CommandType::Kill, 0, {mg});
  run(sim, 15 * 40);
  CHECK_FALSE(sim.routing(man.index));
}

TEST_CASE("an anti-tank team lying in wait hits six times as hard with its first volley") {
  World w;
  auto firstHit = [&](bool wait) {
    Simulation sim(w.map, w.types, w.weapons, 7);
    const Entity at = spawn(sim, w.t("at"), 0, tile(30, 40));
    if (wait) sim.registry().get<CUnit>(at).stance = Stance::HoldFire;
    run(sim, 15 * 12);  // ten seconds unseen
    if (wait) {
      REQUIRE(cond(sim, at) != nullptr);
      CHECK((cond(sim, at)->flags & CCondition::kAmbush) != 0);
    }
    const Entity tank = spawn(sim, w.t("tank"), 1, tile(34, 40));
    Command c;
    c.type = CommandType::Attack;
    c.player = 0;
    c.units = {at};
    c.targetUnit = tank;
    sim.queue(c);
    const int32_t full = sim.registry().get<CUnit>(tank).health;
    for (int i = 0; i < 15 * 10; ++i) {
      sim.tick();
      const int32_t now = sim.registry().get<CUnit>(tank).health;
      if (now < full) return full - now;
    }
    return 0;
  };
  const int32_t plain = firstHit(false), ambush = firstHit(true);
  REQUIRE(plain > 0);
  CHECK(ambush == plain * 6);
}

TEST_CASE("an anti-tank mine stops a tank, not a man; an engineer lifts one") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 9);
  spawn(sim, w.t("mines"), 0, tile(40, 40));
  const Entity man = spawn(sim, w.t("rifle"), 1, tile(36, 40));
  order(sim, CommandType::Move, 1, {man}, tile(44, 40));
  run(sim, 15 * 10);
  CHECK(sim.registry().alive(man));
  CHECK(sim.registry().get<CUnit>(man).health == 100);
  const Entity tank = spawn(sim, w.t("tank"), 1, tile(36, 40));
  order(sim, CommandType::Move, 1, {tank}, tile(44, 40));
  run(sim, 15 * 10);
  CHECK(sim.registry().get<CUnit>(tank).health < 2000 - 250);
  CHECK(sim.structureAt(40, 40) == kNullEntity);  // the mine went off

  spawn(sim, w.t("mines"), 0, tile(50, 50));
  const Entity eng = spawn(sim, w.t("engineer"), 1, tile(47, 50));
  order(sim, CommandType::Move, 1, {eng}, tile(49, 50));
  bool lifted = false;
  for (int i = 0; i < 15 * 10 && !lifted; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events()) lifted = lifted || (e.type == EventType::MineCleared && e.owner == 1);
  }
  CHECK(lifted);
}

TEST_CASE("an observer on the target halves the guns' scatter; night and mist cut sight") {
  World w;
  auto scatter = [&](bool observer) {
    Simulation sim(w.map, w.types, w.weapons, 11);
    const Entity gun = spawn(sim, w.t("gun"), 0, tile(10, 48));
    if (observer) spawn(sim, w.t("observer"), 0, tile(32, 48));
    const Vec2i point = tile(36, 48);
    order(sim, CommandType::AttackGround, 0, {gun}, point);
    int64_t total = 0;
    int n = 0;
    for (int i = 0; i < 15 * 60; ++i) {
      sim.tick();
      for (const SimEvent& e : sim.events())
        if (e.type == EventType::Impact && e.owner == 0) {
          total += distance(e.at.x - point.x, e.at.y - point.y);
          ++n;
        }
    }
    REQUIRE(n > 10);
    return total / n;
  };
  const int64_t alone = scatter(false), watched = scatter(true);
  CHECK(watched * 10 < alone * 7);

  Simulation sim(w.map, w.types, w.weapons, 13);
  CHECK(sim.weatherSightPercent() == 100);
  sim.setTimeOfDay(23 * 3600, 0);
  CHECK(sim.weatherSightPercent() == 55);
  sim.setTimeOfDay(12 * 3600, 0);
  sim.setWeather(7, 0);  // mist
  CHECK(sim.weatherSightPercent() == 50);
}

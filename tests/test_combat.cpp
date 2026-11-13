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
      "warheads": {"bullet": {"infantry":100,"light":30,"medium":6,"heavy":3},
                   "ap": {"infantry":30,"light":90,"medium":100,"heavy":100,
                          "facing":{"front":100,"side":160,"rear":450}}},
      "weapons": [
        {"id":"rifle","warhead":"bullet","damage":14,"range":4.5,"cooldown":22,"accuracy":100},
        {"id":"gun","warhead":"ap","damage":70,"range":6.5,"cooldown":40,"projectile":"shell","speed":150,"splash":0.35}
      ]})"), err));
    REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[
      {"id":"tank","model":"tank","speed":22,"turnTicks":2,"radius":110,"health":400,"armor":"heavy","sight":7,
       "weapons":["gun"],"turret":{"turnTicks":2}},
      {"id":"rifle","model":"rifleman","speed":12,"turnTicks":0,"radius":40,"health":100,"armor":"infantry",
       "sight":6,"weapons":["rifle"],"locomotor":"foot"}
    ]})"), err));
    REQUIRE(types.resolveWeapons(weapons, err));
  }
};

void spawn(Simulation& sim, int type, uint8_t player, Vec2i at, uint16_t count = 1) {
  Command c;
  c.type = CommandType::Spawn;
  c.unitType = static_cast<uint16_t>(type);
  c.player = player;
  c.target = at;
  c.count = count;
  sim.queue(c);
}

int countOwner(Simulation& sim, uint8_t owner) {
  int n = 0;
  for (uint32_t e : sim.registry().pool<CUnit>().entities())
    if (sim.registry().pool<CUnit>().get(e).owner == owner) ++n;
  return n;
}
}  // namespace

TEST_CASE("weapons and armour data load; damage follows the warhead table") {
  World w;
  const int rifle = w.weapons.find("rifle"), gun = w.weapons.find("gun");
  REQUIRE(rifle >= 0);
  REQUIRE(gun >= 0);
  CHECK(w.weapons.get(gun).range == 6 * 256 + 128);
  CHECK(w.weapons.damageAgainst(rifle, ArmorClass::Infantry) == 14);
  CHECK(w.weapons.damageAgainst(rifle, ArmorClass::Heavy) == 0);  // 14 * 3 / 100
  CHECK(w.weapons.damageAgainst(gun, ArmorClass::Heavy) == 70);
  CHECK(w.types.get(0).weapons.size() == 1);
  std::string err;
  UnitTypeRegistry bad;
  REQUIRE(bad.loadJson(nlohmann::json::parse(R"({"units":[{"id":"x","model":"m","weapons":["nope"]}]})"), err));
  CHECK_FALSE(bad.resolveWeapons(w.weapons, err));
}

TEST_CASE("idle units shoot enemies in range; tanks beat riflemen") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 9);
  spawn(sim, 0, 0, {20 * 256, 20 * 256});
  spawn(sim, 1, 1, {25 * 256, 20 * 256}, 4);
  sim.tick();
  REQUIRE(countOwner(sim, 1) == 4);
  bool fired = false, died = false;
  for (int i = 0; i < 15 * 150 && countOwner(sim, 1) > 0; ++i) {
    sim.tick();
    for (const SimEvent& ev : sim.events()) {
      fired = fired || ev.type == EventType::Fire;
      died = died || ev.type == EventType::UnitDied;
    }
  }
  CHECK(fired);
  CHECK(died);
  CHECK(countOwner(sim, 1) == 0);   // rifles can't hurt heavy armour...
  CHECK(countOwner(sim, 0) == 1);   // ...so the tank wins
}

TEST_CASE("units ignore enemies they cannot see") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 3);
  spawn(sim, 1, 0, {10 * 256, 10 * 256});
  spawn(sim, 1, 1, {40 * 256, 40 * 256});
  for (int i = 0; i < 100; ++i) sim.tick();
  CHECK(countOwner(sim, 0) == 1);
  CHECK(countOwner(sim, 1) == 1);
  CHECK(sim.vision().visible(0, 10, 10));
  CHECK_FALSE(sim.vision().visible(0, 40, 40));
  CHECK(sim.vision().explored(0, 12, 12));
}

TEST_CASE("attack orders chase the target across the map") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 4);
  spawn(sim, 0, 0, {8 * 256, 8 * 256});
  spawn(sim, 1, 1, {50 * 256, 50 * 256});
  sim.tick();
  Entity tank, rifle;
  for (uint32_t e : sim.registry().pool<CUnit>().entities())
    (sim.registry().pool<CUnit>().get(e).owner == 0 ? tank : rifle) = sim.registry().handle(e);
  Command a;
  a.type = CommandType::Attack;
  a.units = {tank};
  a.targetUnit = rifle;
  sim.queue(a);
  for (int i = 0; i < 15 * 90 && sim.registry().alive(rifle); ++i) sim.tick();
  CHECK_FALSE(sim.registry().alive(rifle));
}

TEST_CASE("combat is deterministic") {
  World w;
  auto run = [&]() {
    Simulation sim(w.map, w.types, w.weapons, 2024);
    spawn(sim, 0, 0, {12 * 256, 30 * 256}, 9);
    spawn(sim, 1, 0, {14 * 256, 34 * 256}, 30);
    spawn(sim, 0, 1, {50 * 256, 30 * 256}, 9);
    spawn(sim, 1, 1, {48 * 256, 26 * 256}, 30);
    sim.tick();
    std::vector<Entity> all;
    for (uint32_t e : sim.registry().pool<CUnit>().entities()) all.push_back(sim.registry().handle(e));
    Command m;
    m.type = CommandType::AttackMove;
    m.units = all;
    m.target = {32 * 256, 30 * 256};
    sim.queue(m);
    for (int i = 0; i < 15 * 40; ++i) sim.tick();
    return std::make_pair(sim.stateHash(), sim.unitCount());
  };
  const auto a = run(), b = run();
  CHECK(a.first == b.first);
  CHECK(a.second == b.second);
  CHECK(a.second < 78);  // the armies actually fought
}

TEST_CASE("guns switch to HE against infantry; vehicles take more damage from the side and rear") {
  WeaponRegistry weapons;
  std::string err;
  REQUIRE(weapons.loadJson(nlohmann::json::parse(R"({
    "warheads": {"ap": {"infantry":25,"heavy":100,"facing":{"front":100,"side":160,"rear":450}},
                 "he": {"infantry":100,"heavy":15}},
    "weapons": [
      {"id":"ap","warhead":"ap","damage":100,"range":10,"cooldown":50,"projectile":"shell","vsInfantry":"he"},
      {"id":"he","warhead":"he","damage":80,"range":9,"cooldown":50,"projectile":"shell","splash":0.9}
    ]})"), err));
  const int ap = weapons.find("ap"), he = weapons.find("he");
  CHECK(weapons.forTarget(ap, ArmorClass::Infantry) == he);
  CHECK(weapons.forTarget(ap, ArmorClass::Heavy) == ap);
  CHECK(weapons.damageAgainst(ap, ArmorClass::Heavy, HitSide::Front) == 100);
  CHECK(weapons.damageAgainst(ap, ArmorClass::Heavy, HitSide::Side) == 160);
  CHECK(weapons.damageAgainst(ap, ArmorClass::Heavy, HitSide::Rear) == 450);
  CHECK(weapons.damageAgainst(ap, ArmorClass::Infantry, HitSide::Rear) == 25);  // no rear armour on a man

  // Unknown alternative rounds are reported.
  WeaponRegistry bad;
  CHECK_FALSE(bad.loadJson(nlohmann::json::parse(R"({"warheads":{"ap":{}},
    "weapons":[{"id":"x","warhead":"ap","damage":1,"range":1,"cooldown":1,"vsInfantry":"nope"}]})"), err));
}

TEST_CASE("a shot into a tank's rear is deadlier than one into its front") {
  World w;
  // Two identical duels: the target faces away from (rear) or towards (front) the shooter.
  auto duel = [&](Angle targetFacing) {
    Simulation sim(w.map, w.types, w.weapons, 3);
    Command c;
    c.type = CommandType::Spawn;
    c.unitType = 0;
    c.player = 0;
    c.target = {20 * 256, 32 * 256};
    c.facing = 0;
    sim.queue(c);
    c.player = 1;
    c.target = {25 * 256, 32 * 256};
    c.facing = targetFacing;
    sim.queue(c);
    sim.tick();
    Entity shooter, target;
    for (uint32_t e : sim.registry().pool<CUnit>().entities())
      (sim.registry().pool<CUnit>().get(e).owner == 0 ? shooter : target) = sim.registry().handle(e);
    Command a;
    a.type = CommandType::Attack;
    a.units = {shooter};
    a.targetUnit = target;
    sim.queue(a);
    // Freeze the target so it neither turns nor shoots back.
    sim.registry().remove<CCombat>(target);
    int32_t firstHit = 0;
    for (int i = 0; i < 300 && sim.registry().alive(target); ++i) {
      sim.tick();
      for (const SimEvent& ev : sim.events())
        if (ev.type == EventType::Impact && ev.hit && !firstHit && sim.registry().alive(target))
          firstHit = 400 - sim.registry().get<CUnit>(target).health;
    }
    return firstHit;
  };
  const int32_t rear = duel(0);                      // facing +X, away from the shooter in the west
  const int32_t front = duel(angleOfFacing(8));      // facing -X, towards the shooter
  // Splash falls off from the burst point, so a hit deals 50-100 % of the table value.
  CHECK(front > 0);
  CHECK(front <= 70);
  CHECK(rear >= 157);
}

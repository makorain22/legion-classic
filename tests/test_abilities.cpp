// Unit abilities (Milestone 9.15, part 3): grenades, smoke, sprints,
// digging in, rallies, charges and barrages; cooldowns, uses and costs;
// saved with the battle; used by the computer.
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "pioneer/core/bytes.hpp"
#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

namespace {
constexpr int32_t T = 256;
struct World {
  Map map{96, 96, 300};
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  World() {
    const auto w = nlohmann::json::parse(R"({
      "warheads": {"bullet": {"infantry":100,"light":30,"medium":6,"heavy":3,"structure":5},
                   "he": {"infantry":100,"light":80,"medium":40,"heavy":25,"structure":60}},
      "weapons": [
        {"id":"rifle","warhead":"bullet","damage":14,"range":5,"cooldown":22,"accuracy":100},
        {"id":"mg","warhead":"bullet","damage":2,"range":9,"cooldown":3,"accuracy":20},
        {"id":"grenade","warhead":"he","damage":75,"range":4,"cooldown":30,"projectile":"shell","speed":40,"splash":0.8,"muzzle":0},
        {"id":"smoke_shell","warhead":"he","damage":1,"range":22,"cooldown":60,"projectile":"shell","speed":120,"splash":1.5,"smoke":30},
        {"id":"howitzer","warhead":"he","damage":60,"range":22,"cooldown":45,"projectile":"shell","speed":120,"splash":1.0}
      ]})");
    const auto u = nlohmann::json::parse(R"({
      "abilities": [
        {"id":"grenade","kind":"throw","weapon":"grenade","range":4,"cooldown":20},
        {"id":"sprint","kind":"sprint","percent":60,"duration":8,"cooldown":40},
        {"id":"dig_in","kind":"dig_in","percent":40,"cooldown":5},
        {"id":"rally","kind":"rally","radius":5,"cooldown":60},
        {"id":"charge","kind":"charge","percent":50,"duration":6,"range":12,"cooldown":60,"charges":1},
        {"id":"smoke","kind":"barrage","weapon":"smoke_shell","shots":4,"duration":6,"range":22,"cooldown":60,"cost":100}
      ],
      "units":[
      {"id":"rifle","model":"rifleman","speed":12,"turnTicks":0,"radius":40,"health":100,"armor":"infantry",
       "sight":6,"weapons":["rifle"],"locomotor":"foot","abilities":["grenade","sprint","rally","charge"]},
      {"id":"mg","model":"mg_team","speed":10,"turnTicks":0,"radius":40,"health":100000,"armor":"infantry",
       "sight":10,"weapons":["mg"],"locomotor":"foot","abilities":["dig_in"]},
      {"id":"gun","model":"artillery","speed":8,"turnTicks":2,"radius":90,"health":300,"armor":"light","sight":5,
       "weapons":["howitzer"],"turret":{"turnTicks":2},"abilities":["smoke"]},
      {"id":"yard","model":"yard","armor":"structure","footprint":[3,3],"health":2000,"sight":5,"role":"yard"}
    ]})");
    std::string err;
    REQUIRE_MESSAGE(loadRuleSet(w, u, nlohmann::json::parse(R"({"units":[]})"), weapons, types, err), err);
  }
  int t(const char* id) const {
    const int i = types.find(id);
    REQUIRE(i >= 0);
    return i;
  }
  int a(const char* id) const {
    const int i = types.abilities().find(id);
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
void use(Simulation& sim, uint8_t player, std::vector<Entity> units, int ability, Vec2i at = {}) {
  Command c;
  c.type = CommandType::UseAbility;
  c.player = player;
  c.units = std::move(units);
  c.unitType = static_cast<uint16_t>(ability);
  c.target = at;
  sim.queue(c);
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
}  // namespace

TEST_CASE("abilities load from the rules and are looked up for each unit; bad ones are caught") {
  World w;
  CHECK(w.types.abilities().size() == 6);
  CHECK(w.types.get(w.t("rifle")).abilities.size() == 4);
  CHECK(w.types.abilitySlot(w.t("rifle"), w.a("sprint")) == 1);
  CHECK(w.types.abilitySlot(w.t("mg"), w.a("sprint")) == -1);
  CHECK(w.types.abilities().get(w.a("sprint")).duration == 8 * 15);
  CHECK(abilityNeedsTarget(AbilityKind::Throw));
  CHECK_FALSE(abilityNeedsTarget(AbilityKind::Rally));

  std::string err;
  AbilityRegistry bad;
  CHECK_FALSE(bad.loadJson(nlohmann::json::parse(R"([{"id":"x","kind":"teleport"}])"), err));
  REQUIRE(bad.loadJson(nlohmann::json::parse(R"([{"id":"x","kind":"throw"}])"), err));
  CHECK_FALSE(bad.resolve(w.weapons, err));  // a throw needs a weapon
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  CHECK_FALSE(loadRuleSet(nlohmann::json::parse(R"({"weapons":[]})"),
                          nlohmann::json::parse(R"({"units":[{"id":"u","model":"m","abilities":["nope"]}]})"),
                          nlohmann::json::parse(R"({"units":[]})"), weapons, types, err));
}

TEST_CASE("a grenade: thrown at once in reach, walked up to out of reach; then it waits for its cooldown") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 3);
  const Entity man = spawn(sim, w.t("rifle"), 0, tile(20, 40));
  const int grenade = w.a("grenade");
  CHECK(sim.abilityReady(man.index, 0));
  use(sim, 0, {man}, grenade, tile(23, 40));
  bool burst = false;
  for (int i = 0; i < 60 && !burst; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events())
      if (e.type == EventType::Impact && e.weapon == w.weapons.find("grenade")) burst = true;
  }
  CHECK(burst);
  std::string why;
  CHECK_FALSE(sim.abilityReady(man.index, 0, &why));
  CHECK(why.find("ready") != std::string::npos);
  CHECK(sim.abilityCooldown(man.index, 0) > 0);
  run(sim, 20 * 15);
  CHECK(sim.abilityReady(man.index, 0));
  // Out of reach: he walks up and throws.
  use(sim, 0, {man}, grenade, tile(34, 40));
  bool thrown = false;
  for (int i = 0; i < 15 * 20 && !thrown; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events())
      if (e.type == EventType::AbilityUsed && e.owner == 0) thrown = true;
  }
  CHECK(thrown);
  const CPosition& p = sim.registry().get<CPosition>(man);
  CHECK(distance(p.x - tile(34, 40).x, p.y - tile(34, 40).y) <= 4u * T);
}

TEST_CASE("sprint goes faster; dig in takes less damage until moving; a rally gets pinned men up") {
  World w;
  auto covered = [&](bool sprint) {
    Simulation sim(w.map, w.types, w.weapons, 5);
    const Entity man = spawn(sim, w.t("rifle"), 0, tile(10, 40));
    order(sim, CommandType::Move, 0, {man}, tile(80, 40));
    if (sprint) use(sim, 0, {man}, w.a("sprint"));
    run(sim, 15 * 6);
    return sim.registry().get<CPosition>(man).x - tile(10, 40).x;
  };
  CHECK(covered(true) * 100 > covered(false) * 140);

  Simulation sim(w.map, w.types, w.weapons, 7);
  const Entity mg = spawn(sim, w.t("mg"), 0, tile(20, 40));
  CHECK(sim.abilityShelterPercent(mg.index) == 100);
  use(sim, 0, {mg}, w.a("dig_in"));
  run(sim, 2);
  CHECK(sim.abilityShelterPercent(mg.index) == 60);
  order(sim, CommandType::Move, 0, {mg}, tile(26, 40));
  run(sim, 10);
  CHECK(sim.abilityShelterPercent(mg.index) == 100);

  // Two men pinned down by a machine gun; a third rallies them.
  const Entity enemyMg = spawn(sim, w.t("mg"), 1, tile(50, 60));
  std::vector<Entity> pinned;
  for (int k = 0; k < 2; ++k) {
    pinned.push_back(spawn(sim, w.t("rifle"), 0, tile(58, 60 + k)));
    sim.registry().get<CUnit>(pinned.back()).health = 100000;
    sim.registry().get<CUnit>(pinned.back()).stance = Stance::HoldFire;
  }
  run(sim, 15 * 15);
  REQUIRE(sim.registry().pool<CCondition>().tryGet(pinned[0].index) != nullptr);
  CHECK(sim.registry().get<CCondition>(pinned[0]).suppression >= 400);
  order(sim, CommandType::Kill, 1, {enemyMg});
  const Entity sergeant = spawn(sim, w.t("rifle"), 0, tile(60, 61));
  use(sim, 0, {sergeant}, w.a("rally"));
  run(sim, 2);
  CHECK(sim.registry().get<CCondition>(pinned[0]).suppression < 50);
  CHECK(sim.registry().get<CCondition>(pinned[0]).posture == Posture::Standing);
}

TEST_CASE("a charge runs at the point deaf to fire, once only; a smoke barrage costs supplies and lays a screen") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 9);
  const Entity man = spawn(sim, w.t("rifle"), 0, tile(20, 40));
  const int charge = w.a("charge");
  use(sim, 0, {man}, charge, tile(30, 40));
  run(sim, 2);
  CHECK(sim.charging(man.index));
  CHECK(sim.registry().get<COrder>(man).type == OrderType::AttackMove);
  CHECK(sim.abilitySpeedPercent(man.index) == 150);
  sim.suppress(man.index, 900);
  CHECK((sim.registry().pool<CCondition>().tryGet(man.index) == nullptr ||
         sim.registry().get<CCondition>(man).suppression == 0));
  run(sim, 15 * 61);
  std::string why;
  CHECK_FALSE(sim.abilityReady(man.index, 3, &why));  // one use a battle
  CHECK(why == "used up");

  const Entity gun = spawn(sim, w.t("gun"), 0, tile(10, 70));
  Command money;
  money.type = CommandType::SetCredits;
  money.player = 0;
  money.amount = 50;
  sim.queue(money);
  run(sim, 1);
  CHECK_FALSE(sim.abilityReady(gun.index, 0, &why));
  CHECK(why == "not enough supplies");
  money.amount = 1000;
  sim.queue(money);
  run(sim, 1);
  const Vec2i point = tile(28, 70);
  use(sim, 0, {gun}, w.a("smoke"), point);
  int rounds = 0;
  bool screen = false;
  for (int i = 0; i < 15 * 12; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events()) rounds += e.type == EventType::Fire && e.owner == 0 && e.weapon == w.weapons.find("smoke_shell");
    screen = screen || (sim.vision().ground(point.x >> LEPTON_SHIFT, point.y >> LEPTON_SHIFT) & Vision::kSmoke);
  }
  CHECK(rounds == 4);
  CHECK(screen);
  CHECK(sim.playerState(0).credits == 900);
}

TEST_CASE("abilities under way are saved with the battle; the computer throws grenades at a knot of enemies") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 11);
  const Entity gun = spawn(sim, w.t("gun"), 0, tile(10, 70));
  Command money;
  money.type = CommandType::SetCredits;
  money.player = 0;
  money.amount = 1000;
  sim.queue(money);
  run(sim, 1);
  use(sim, 0, {gun}, w.a("smoke"), tile(28, 70));
  run(sim, 20);  // mid-barrage
  ByteWriter out;
  sim.saveState(out);
  Map copy = w.map;
  Simulation loaded(copy, w.types, w.weapons, 99);
  ByteReader in(out.bytes());
  std::string err;
  REQUIRE_MESSAGE(loaded.loadState(in, err), err);
  CHECK(loaded.stateHash() == sim.stateHash());
  run(sim, 15 * 10);
  run(loaded, 15 * 10);
  CHECK(loaded.stateHash() == sim.stateHash());

  Simulation ai(w.map, w.types, w.weapons, 13);
  spawn(ai, w.t("yard"), 0, tile(10, 40));
  const Entity man = spawn(ai, w.t("rifle"), 0, tile(30, 40));
  ai.registry().get<CUnit>(man).stance = Stance::HoldFire;  // only the grenade
  for (int k = 0; k < 3; ++k) spawn(ai, w.t("rifle"), 1, tile(33, 40 + (k % 2)));
  Command c;
  c.type = CommandType::SetController;
  c.player = 0;
  c.amount = 2;
  ai.queue(c);
  bool threw = false;
  for (int i = 0; i < 15 * 10 && !threw; ++i) {
    ai.tick();
    for (const SimEvent& e : ai.events()) threw = threw || (e.type == EventType::AbilityUsed && e.owner == 0 && e.unitType == w.a("grenade"));
  }
  CHECK(threw);
}

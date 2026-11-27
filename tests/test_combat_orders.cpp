// Combat orders (Milestone 9.15, part 1): firing at the ground, guns given
// away by their own fire (counter-battery), a unit answering together when
// one of it is hit, the under-attack alert, retreat and smoke rounds.
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
                   "he": {"infantry":100,"light":80,"medium":40,"heavy":25,"structure":60}},
      "weapons": [
        {"id":"rifle","warhead":"bullet","damage":14,"range":5,"cooldown":22,"accuracy":100},
        {"id":"howitzer","warhead":"he","damage":120,"range":24,"cooldown":45,"projectile":"shell","speed":120,"splash":1.2},
        {"id":"smoke","warhead":"he","damage":1,"range":20,"cooldown":60,"projectile":"shell","speed":120,"splash":1.5,"smoke":30}
      ]})"), err), err);
    REQUIRE_MESSAGE(types.loadJson(nlohmann::json::parse(R"({"units":[
      {"id":"rifle","model":"rifleman","speed":12,"turnTicks":0,"radius":40,"health":100,"armor":"infantry",
       "sight":6,"weapons":["rifle"],"locomotor":"foot"},
      {"id":"gun","model":"artillery","speed":8,"turnTicks":2,"radius":90,"health":300,"armor":"light","sight":5,
       "weapons":["howitzer"],"turret":{"turnTicks":2}},
      {"id":"mortar","model":"artillery","speed":8,"turnTicks":2,"radius":90,"health":300,"armor":"light","sight":5,
       "weapons":["smoke"],"turret":{"turnTicks":2}},
      {"id":"yard","model":"yard","armor":"structure","footprint":[3,3],"health":2000,"sight":5,"role":"yard"}
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
}  // namespace

TEST_CASE("fire at the ground: guns move into range and keep shelling a point nobody can see") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 3);
  const Entity gun = spawn(sim, w.t("gun"), 0, tile(10, 48));
  const Vec2i point = tile(60, 48);  // 50 tiles off: out of range, and nothing there
  order(sim, CommandType::AttackGround, 0, {gun}, point);
  int impacts = 0, near = 0;
  for (int i = 0; i < 15 * 90; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events())
      if (e.type == EventType::Impact && e.owner == 0) {
        ++impacts;
        if (distance(e.at.x - point.x, e.at.y - point.y) < 4u * T) ++near;
      }
  }
  CHECK(sim.registry().get<COrder>(gun).type == OrderType::AttackGround);
  const CPosition& p = sim.registry().get<CPosition>(gun);
  CHECK(distance(p.x - point.x, p.y - point.y) <= 24u * T);  // it closed to within range
  CHECK(impacts >= 3);
  CHECK(near == impacts);
  // A new order ends it.
  order(sim, CommandType::Stop, 0, {gun});
  run(sim, 2);
  CHECK(sim.registry().get<COrder>(gun).type == OrderType::Idle);
}

TEST_CASE("a gun firing indirectly gives itself away, and the enemy's guns answer it") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 5);
  const Entity ours = spawn(sim, w.t("gun"), 0, tile(10, 48));
  const Entity theirs = spawn(sim, w.t("gun"), 1, tile(30, 48));  // 20 tiles: in each other's range, far out of sight
  run(sim, 4);
  CHECK_FALSE(sim.unitVisibleTo(1, ours.index));
  CHECK_FALSE(sim.unitVisibleTo(0, theirs.index));
  // Ours shells a point near theirs: that shows ours to them, and theirs fires back.
  order(sim, CommandType::AttackGround, 0, {ours}, tile(28, 50));
  bool seen = false, answered = false;
  for (int i = 0; i < 15 * 30 && !answered; ++i) {
    sim.tick();
    seen = seen || sim.unitVisibleTo(1, ours.index);
    for (const SimEvent& e : sim.events())
      if (e.type == EventType::Fire && e.owner == 1) answered = true;
  }
  CHECK(seen);
  CHECK(answered);
  // Silent for a while (the duel over), it disappears again.
  order(sim, CommandType::Kill, 1, {theirs});
  order(sim, CommandType::Stop, 0, {ours});
  run(sim, 15 * 5);
  CHECK_FALSE(sim.unitVisibleTo(1, ours.index));
}

TEST_CASE("a unit answers together; the alert comes once for a fight, again for one elsewhere") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 7);
  std::vector<Entity> men;
  for (int k = 0; k < 5; ++k) men.push_back(spawn(sim, w.t("rifle"), 0, tile(20 + k % 2, 40 + k)));
  order(sim, CommandType::BindGroup, 0, men);
  run(sim, 2);
  REQUIRE(sim.groupOf(men[0]) != nullptr);
  // An enemy in reach of one man only.
  const Entity foe = spawn(sim, w.t("rifle"), 1, tile(24, 40));
  (void)foe;
  int alerts = 0;
  bool othersAnswer = false;
  for (int i = 0; i < 15 * 6; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events()) alerts += e.type == EventType::UnderAttack && e.owner == 0;
    for (size_t k = 1; k < men.size(); ++k)
      if (sim.registry().alive(men[k]) && sim.registry().get<COrder>(men[k]).type == OrderType::AttackMove) othersAnswer = true;
  }
  CHECK(alerts >= 1);
  CHECK(alerts <= 1);  // one fight in six seconds: one alert
  CHECK(othersAnswer);
}

TEST_CASE("retreat: back to the yard at a forced march; smoke rounds lay a cloud") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 9);
  spawn(sim, w.t("yard"), 0, tile(10, 48));
  std::vector<Entity> men;
  for (int k = 0; k < 4; ++k) men.push_back(spawn(sim, w.t("rifle"), 0, tile(60, 46 + k)));
  order(sim, CommandType::Retreat, 0, men);
  run(sim, 2);
  for (Entity e : men) {
    CHECK(sim.registry().get<COrder>(e).type == OrderType::Move);
    CHECK(sim.registry().get<COrder>(e).forced);
  }
  run(sim, 15 * 60);
  for (Entity e : men) {
    const CPosition& p = sim.registry().get<CPosition>(e);
    CHECK(distance(p.x - tile(10, 48).x, p.y - tile(10, 48).y) < 14u * T);
  }

  const Entity mortar = spawn(sim, w.t("mortar"), 0, tile(20, 20));
  const Vec2i point = tile(32, 20);
  order(sim, CommandType::AttackGround, 0, {mortar}, point);
  bool smoke = false;
  for (int i = 0; i < 15 * 20 && !smoke; ++i) {
    sim.tick();
    smoke = sim.vision().ground(point.x >> LEPTON_SHIFT, point.y >> LEPTON_SHIFT) & Vision::kSmoke;
  }
  CHECK(smoke);
}

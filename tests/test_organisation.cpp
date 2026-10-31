// Fireteams, platoons and companies; garrisons; small arms against armour;
// supply points.
#include <doctest/doctest.h>

#include "slave/sim/simulation.hpp"

using namespace slave;

namespace {
struct Rules {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
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
constexpr int32_t T = LEPTONS_PER_TILE;

std::vector<Entity> spawnMany(Simulation& sim, uint16_t type, uint8_t owner, Vec2i at, int n, int spacing = 60) {
  Command c;
  c.type = CommandType::SpawnList;
  for (int i = 0; i < n; ++i) c.spawns.push_back({type, owner, {at.x + (i % 10) * spacing, at.y + (i / 10) * spacing}, 0, -1});
  sim.queue(c);
  sim.tick();
  std::vector<Entity> out;
  for (const auto& r : sim.spawnLog()) out.push_back(r.entity);
  return out;
}

int groupsOf(Simulation& sim, uint8_t owner, Echelon e) {
  int n = 0;
  for (uint32_t g : sim.registry().pool<CGroup>().entities()) {
    const CGroup& c = sim.registry().pool<CGroup>().get(g);
    n += c.owner == owner && c.echelon == e;
  }
  return n;
}
}  // namespace

TEST_CASE("five men of a kind standing together become a fireteam") {
  Rules R;
  Map map(64, 64, 300);
  Simulation sim(map, R.types, R.weapons, 1);
  spawnMany(sim, R.t("ger_rifleman"), 0, {20 * T, 20 * T}, 12);
  spawnMany(sim, R.t("ger_rifleman"), 0, {50 * T, 50 * T}, 3);  // too few, far away
  for (int i = 0; i < 20; ++i) sim.tick();
  CHECK(groupsOf(sim, 0, Echelon::Fireteam) == 2);  // 12 men: two fireteams and two left over
  int loose = 0;
  for (uint32_t e : sim.registry().pool<CUnit>().entities()) loose += !sim.registry().pool<CGroupMember>().has(e);
  CHECK(loose == 5);
}

TEST_CASE("binding makes squads, platoons and companies; casualties demote; unbinding disbands") {
  Rules R;
  Map map(64, 64, 300);
  Simulation sim(map, R.types, R.weapons, 1);
  std::vector<Entity> men = spawnMany(sim, R.t("ger_rifleman"), 0, {10 * T, 10 * T}, 100, 70);
  Command bind;
  bind.type = CommandType::BindGroup;
  bind.player = 0;
  bind.units = men;
  sim.queue(bind);
  sim.tick();
  REQUIRE(groupsOf(sim, 0, Echelon::Company) == 1);
  const CGroup* g = sim.groupOf(men[0]);
  REQUIRE(g);
  CHECK(g->members.size() == 100);
  CHECK(sim.groupBonus(men[0]) > sim.groupBonus(kNullEntity));
  // Kill 79: down to 21 the company holds; at 20 it becomes a platoon.
  Command kill;
  kill.type = CommandType::Kill;
  kill.units.assign(men.begin(), men.begin() + 79);
  sim.queue(kill);
  for (int i = 0; i < 3; ++i) sim.tick();
  CHECK(groupsOf(sim, 0, Echelon::Company) == 1);
  kill.units = {men[79]};
  sim.queue(kill);
  for (int i = 0; i < 3; ++i) sim.tick();
  CHECK(groupsOf(sim, 0, Echelon::Company) == 0);
  CHECK(groupsOf(sim, 0, Echelon::Platoon) == 1);
  // Far-flung members stay members.
  Command mv;
  mv.type = CommandType::Move;
  mv.player = 0;
  mv.units = {men[99]};
  mv.target = {55 * T, 55 * T};
  sim.queue(mv);
  for (int i = 0; i < 60; ++i) sim.tick();
  CHECK(sim.groupOf(men[99]) != nullptr);
  // Unbind.
  Command un;
  un.type = CommandType::UnbindGroup;
  un.player = 0;
  un.units = {men[90]};
  sim.queue(un);
  sim.tick();
  CHECK(sim.groupOf(men[90]) == nullptr);
  CHECK(groupsOf(sim, 0, Echelon::Platoon) == 0);

  // Vehicles: 5 make a platoon, 15 a company.
  std::vector<Entity> tanks = spawnMany(sim, R.t("ger_medium_tank"), 0, {30 * T, 30 * T}, 15, 160);
  bind.units = tanks;
  sim.queue(bind);
  sim.tick();
  CHECK(groupsOf(sim, 0, Echelon::Company) == 1);
  REQUIRE(sim.groupOf(tanks[0]));
  CHECK(sim.groupOf(tanks[0])->vehicles);
}

TEST_CASE("infantry garrison buildings, take half damage inside, and come out when ordered") {
  Rules R;
  Map map(64, 64, 300);
  Simulation sim(map, R.types, R.weapons, 3);
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{R.t("neutral_townhouse"), NEUTRAL_OWNER, {20 * T, 20 * T}, 0, -1}};
  sim.queue(c);
  sim.tick();
  const Entity house = sim.spawnLog().front().entity;
  std::vector<Entity> men = spawnMany(sim, R.t("rus_rifleman"), 1, {26 * T, 20 * T}, 8);
  Command occ;
  occ.type = CommandType::Occupy;
  occ.player = 1;
  occ.units = men;
  occ.targetUnit = house;
  sim.queue(occ);
  for (int i = 0; i < 15 * 20; ++i) sim.tick();
  int inside = 0;
  for (Entity m : men) inside += sim.registry().has<CGarrisoned>(m);
  CHECK(inside == 8);
  CHECK(sim.garrisonLevel(house) == 2);  // 8 of 20

  // Half damage inside: same burst on a man in the open and one inside.
  std::vector<Entity> outside = spawnMany(sim, R.t("rus_rifleman"), 1, {40 * T, 40 * T}, 1);
  auto& units = sim.registry().pool<CUnit>();
  const int32_t hpIn = units.get(men[0].index).health, hpOut = units.get(outside[0].index).health;
  // Deal damage directly through the same path the guns use.
  // (Rifle hits: 14 x 100% = 1400 hundredths each.)
  for (int i = 0; i < 3; ++i) {
    sim.debugDamage(men[0], 1400);
    sim.debugDamage(outside[0], 1400);
  }
  const int32_t lostIn = hpIn - units.get(men[0].index).health, lostOut = hpOut - units.get(outside[0].index).health;
  CHECK(lostOut == 42);
  CHECK(lostIn * 100 <= lostOut * 50);  // half or less inside (less the more fortified)
  CHECK(lostIn * 100 >= lostOut * 35);

  Command out;
  out.type = CommandType::Unload;
  out.player = 1;
  out.units = {house};
  sim.queue(out);
  sim.tick();
  for (Entity m : men) CHECK_FALSE(sim.registry().has<CGarrisoned>(m));
}

TEST_CASE("rifles barely scratch a heavy tank, but thirty men wear it down") {
  Rules R;
  Map map(64, 64, 300);
  Simulation sim(map, R.types, R.weapons, 4);
  std::vector<Entity> tank = spawnMany(sim, R.t("ger_heavy_tank"), 0, {32 * T, 32 * T}, 1);
  Command hold;
  hold.type = CommandType::SetStance;
  hold.stance = Stance::HoldFire;
  hold.units = tank;
  sim.queue(hold);
  std::vector<Entity> men = spawnMany(sim, R.t("rus_rifleman"), 1, {27 * T, 30 * T}, 30, 50);
  const int32_t full = sim.registry().get<CUnit>(tank[0]).health;
  for (int i = 0; i < 15 * 30; ++i) sim.tick();
  const int32_t left = sim.registry().get<CUnit>(tank[0]).health;
  CHECK(left < full);             // they do hurt it
  CHECK(left < full * 3 / 4);     // thirty men for half a minute do real damage
  CHECK(left > 0);                // but don't simply kill it
}

TEST_CASE("a supply depot earns only on a supply point") {
  Rules R;
  Map map(64, 64, 300);
  map.supplyPoints = {{20, 20}};
  Simulation sim(map, R.types, R.weapons, 5);
  const int depot = R.t("ger_depot");
  CHECK_FALSE(sim.canPlace(depot, {40, 40}));       // no point there
  CHECK(sim.canPlace(depot, {19, 19}));             // over the point
  CHECK(sim.supplyPointFor(depot, {19, 19}) == 0);
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{static_cast<uint16_t>(depot), 0, {20 * T + 128, 20 * T + 128}, 0, -1}};
  sim.queue(c);
  Command money;
  money.type = CommandType::SetCredits;
  money.player = 0;
  money.amount = 0;
  sim.queue(money);
  sim.tick();
  CHECK(sim.depotOn(0).valid());
  CHECK_FALSE(sim.canPlace(depot, {17, 21}));       // the point is taken
  for (int i = 0; i < 15 * 60; ++i) sim.tick();
  CHECK(sim.playerState(0).credits >= R.types.get(depot).income - 20);  // about a minute's worth
  CHECK(sim.playerState(0).incomePerMinute == R.types.get(depot).income);
}

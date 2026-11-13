// The 9.6 economy: logistics points, offices and convoys; income buildings
// and their per-yard limits; the warehouse's aura; levy infantry.
#include <doctest/doctest.h>

#include <algorithm>

#include "pioneer/core/bytes.hpp"
#include "pioneer/map/map.hpp"
#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

namespace {
constexpr int T = 256;
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
std::vector<Entity> spawn(Simulation& sim, uint16_t type, uint8_t owner, const std::vector<Vec2i>& at) {
  Command c;
  c.type = CommandType::SpawnList;
  for (const Vec2i& p : at) c.spawns.push_back({type, owner, p, 0, -1});
  sim.queue(c);
  sim.tick();
  std::vector<Entity> out;
  for (const auto& r : sim.spawnLog()) out.push_back(r.entity);
  return out;
}
void credits(Simulation& sim, uint8_t p, int amount) {
  Command c;
  c.type = CommandType::SetCredits;
  c.player = p;
  c.amount = amount;
  sim.queue(c);
}
int countOf(Simulation& sim, uint16_t type, int owner = -1) {
  int n = 0;
  for (uint32_t e : sim.registry().pool<CUnit>().entities()) {
    const CUnit& u = sim.registry().pool<CUnit>().get(e);
    n += u.type == type && u.health > 0 && (owner < 0 || u.owner == owner);
  }
  return n;
}
std::vector<Vec2i> row(Vec2i at, int n, int step = 150) {
  std::vector<Vec2i> out;
  for (int i = 0; i < n; ++i) out.push_back({at.x + i * step, at.y});
  return out;
}
void run(Simulation& sim, int seconds) {
  for (int i = 0; i < seconds * 15; ++i) sim.tick();
}
}  // namespace

TEST_CASE("troops who hold a logistics point alone get an office and three supply trucks") {
  Rules R;
  Map m(64, 64, 100);
  m.supplyPoints = {{30, 30}};
  Simulation sim(m, R.types, R.weapons, 1);
  // Nothing may be built on a free point.
  CHECK_FALSE(sim.canPlace(R.t("ger_power"), {30, 30}));
  CHECK(sim.canPlace(R.t("ger_power"), {36, 30}));
  spawn(sim, R.t("ger_rifleman"), 0, row({30 * T, 31 * T + 128}, 4));
  run(sim, 6);
  CHECK(sim.pointState(0).side == 0);
  CHECK(sim.pointState(0).progress > 0);
  CHECK_FALSE(sim.officeOn(0).valid());  // not yet
  // An enemy walks up: nobody gains while the point is contested.
  const std::vector<Entity> foe = spawn(sim, R.t("rus_engineer"), 1, {{32 * T, 28 * T}});  // unarmed: doesn't count
  const uint16_t held = sim.pointState(0).progress;
  run(sim, 2);
  CHECK(sim.pointState(0).progress > held);
  bool captured = false;
  for (int i = 0; i < 15 * 20 && !captured; ++i) {
    sim.tick();
    for (const SimEvent& ev : sim.events()) captured = captured || (ev.type == EventType::PointCaptured && ev.owner == 0);
  }
  CHECK(captured);
  const Entity office = sim.officeOn(0);
  REQUIRE(office.valid());
  CHECK(sim.registry().get<CUnit>(office).owner == 0);
  CHECK(sim.types().get(sim.registry().get<CUnit>(office).type).id == "ger_office");
  CHECK(countOf(sim, R.t("ger_supply_truck"), 0) == 3);
  CHECK(sim.officeTrucks(office) == 3);
  // The men who stood where it went up stepped aside: nobody is inside it.
  for (uint32_t e : sim.registry().pool<CUnit>().entities()) {
    if (sim.types().get(sim.registry().pool<CUnit>().get(e).type).isStructure()) continue;
    const CPosition& p = sim.registry().pool<CPosition>().get(e);
    CHECK(sim.structureAt(p.x >> 8, p.y >> 8) != office);
  }
  (void)foe;
}

TEST_CASE("a contested point goes to nobody; an enemy must destroy the office to take it") {
  Rules R;
  Map m(64, 64, 100);
  m.supplyPoints = {{30, 30}};
  Simulation sim(m, R.types, R.weapons, 2);
  const std::vector<Entity> ger = spawn(sim, R.t("ger_heavy_tank"), 0, {{30 * T, 32 * T}});
  const std::vector<Entity> rus = spawn(sim, R.t("rus_heavy_tank"), 1, {{31 * T, 28 * T}});
  for (Entity e : {ger[0], rus[0]}) {  // they just stand and look at each other
    Command c;
    c.type = CommandType::SetStance;
    c.units = {e};
    c.stance = Stance::HoldFire;
    sim.queue(c);
  }
  run(sim, 20);
  CHECK_FALSE(sim.officeOn(0).valid());
  CHECK(sim.pointState(0).progress <= 3);  // (the Germans were there one tick before the Soviets)
  // The Germans leave: the Soviets take it.
  Command kill;
  kill.type = CommandType::Kill;
  kill.units = ger;
  sim.queue(kill);
  run(sim, 16);
  Entity office = sim.officeOn(0);
  REQUIRE(office.valid());
  CHECK(sim.registry().get<CUnit>(office).owner == 1);
  // A German patrol standing there gains nothing while the office stands...
  spawn(sim, R.t("ger_rifleman"), 0, row({27 * T, 33 * T}, 3));
  Command hold;
  hold.type = CommandType::SetStance;
  hold.stance = Stance::HoldFire;
  for (uint32_t e : sim.registry().pool<CUnit>().entities()) hold.units.push_back(sim.registry().handle(e));
  sim.queue(hold);
  run(sim, 16);
  CHECK(sim.officeOn(0) == office);
  // ...and once it is destroyed the point is theirs for the taking.
  sim.debugDamage(office, 100000 * 100);
  Command gone;
  gone.type = CommandType::Kill;
  gone.units = rus;
  sim.queue(gone);
  run(sim, 16);
  office = sim.officeOn(0);
  REQUIRE(office.valid());
  CHECK(sim.registry().get<CUnit>(office).owner == 0);
}

TEST_CASE("supply trucks carry money from the office to the headquarters, trip after trip") {
  Rules R;
  Map m(96, 64, 100);
  m.supplyPoints = {{70, 30}};
  Simulation sim(m, R.types, R.weapons, 3);
  spawn(sim, R.t("gbr_yard"), 0, {{14 * T, 30 * T}});
  spawn(sim, R.t("gbr_rifleman"), 0, row({70 * T, 32 * T}, 3));
  run(sim, 14);
  const Entity office = sim.officeOn(0);
  REQUIRE(office.valid());
  credits(sim, 0, 0);
  int paid = 0, arrivals = 0;
  for (int i = 0; i < 15 * 240; ++i) {
    sim.tick();
    for (const SimEvent& ev : sim.events())
      if (ev.type == EventType::ConvoyArrived && ev.owner == 0) {
        paid += ev.amount;
        ++arrivals;
        CHECK(ev.amount == R.types.get(R.t("gbr_supply_truck")).capacity);
      }
  }
  CHECK(arrivals >= 6);  // three trucks, at least two trips each in four minutes
  // Everything it has came from the convoys and the yard's trickle (120 a minute).
  CHECK(sim.playerState(0).credits >= paid + 470);
  CHECK(sim.playerState(0).credits <= paid + 490);
  // The office falls (and its guard with it): trucks on the road bring in what they carry, then stand idle.
  Command kill;
  kill.type = CommandType::Kill;
  for (uint32_t e : sim.registry().pool<CUnit>().entities())
    if (sim.registry().pool<CUnit>().get(e).type == R.t("gbr_rifleman")) kill.units.push_back(sim.registry().handle(e));
  sim.queue(kill);
  sim.debugDamage(office, 100000 * 100);
  run(sim, 120);
  const int before = sim.playerState(0).credits;
  run(sim, 60);
  CHECK(sim.playerState(0).credits <= before + 125);  // only the yard's trickle now
  CHECK(countOf(sim, R.t("gbr_supply_truck"), 0) == 3);
  for (uint32_t e : sim.registry().pool<CHarvester>().entities()) CHECK(sim.registry().pool<CHarvester>().get(e).cargo == 0);
  // Taken again, the new office takes on the trucks that were left over instead of bringing more.
  spawn(sim, R.t("gbr_rifleman"), 0, row({69 * T, 33 * T}, 3));
  run(sim, 30);
  REQUIRE(sim.officeOn(0).valid());
  CHECK(countOf(sim, R.t("gbr_supply_truck"), 0) == 3);
  CHECK(sim.officeTrucks(sim.officeOn(0)) == 3);
}

TEST_CASE("a lost supply truck is replaced from a logistics warehouse and goes to the office that is short") {
  Rules R;
  Map m(96, 64, 100);
  m.supplyPoints = {{60, 30}};
  Simulation sim(m, R.types, R.weapons, 4);
  spawn(sim, R.t("rus_yard"), 0, {{14 * T, 30 * T}});
  spawn(sim, R.t("rus_power"), 0, {{8 * T, 24 * T}});
  spawn(sim, R.t("rus_post"), 0, {{8 * T, 36 * T}});
  const uint16_t truck = R.t("rus_supply_truck");
  CHECK_FALSE(sim.canProduce(0, truck));  // no warehouse yet
  spawn(sim, R.t("rus_warehouse"), 0, {{20 * T, 38 * T}});
  CHECK(sim.canProduce(0, truck));
  spawn(sim, R.t("rus_rifleman"), 0, row({60 * T, 32 * T}, 3));
  run(sim, 14);
  const Entity office = sim.officeOn(0);
  REQUIRE(office.valid());
  REQUIRE(sim.officeTrucks(office) == 3);
  Command kill;
  kill.type = CommandType::Kill;
  kill.units = {sim.registry().handle(sim.registry().pool<CHarvester>().entities().front())};
  sim.queue(kill);
  run(sim, 1);
  CHECK(sim.officeTrucks(office) == 2);
  credits(sim, 0, 5000);
  Command make;
  make.type = CommandType::Produce;
  make.player = 0;
  make.unitType = truck;
  make.amount = 2;  // one more than the office can use
  sim.queue(make);
  run(sim, 30);
  CHECK(countOf(sim, truck, 0) == 4);
  CHECK(sim.officeTrucks(office) == 3);  // the fourth has no run and waits
}

TEST_CASE("income buildings: so many for each construction yard") {
  Rules R;
  Map m(96, 64, 100);
  Simulation sim(m, R.types, R.weapons, 5);
  spawn(sim, R.t("ger_yard"), 0, {{20 * T, 30 * T}});
  spawn(sim, R.t("ger_power"), 0, {{14 * T, 24 * T}});
  const uint16_t post = R.t("ger_post"), warehouse = R.t("ger_warehouse"), fuel = R.t("ger_depot");
  CHECK(R.types.get(post).income < R.types.get(warehouse).income);
  CHECK(R.types.get(warehouse).income < R.types.get(fuel).income);
  CHECK(sim.roomFor(0, post) == 3);
  CHECK(sim.roomFor(0, fuel) == 1);
  CHECK(sim.roomFor(0, R.t("ger_barracks")) == INT32_MAX);
  spawn(sim, post, 0, {{14 * T, 36 * T}});
  spawn(sim, warehouse, 0, {{26 * T, 37 * T}});
  CHECK(sim.canProduce(0, fuel));
  spawn(sim, fuel, 0, {{28 * T, 24 * T}});
  CHECK(sim.roomFor(0, fuel) == 0);
  CHECK_FALSE(sim.canProduce(0, fuel));  // one per yard
  credits(sim, 0, 0);
  run(sim, 60);
  const int income = 120 + R.types.get(post).income + R.types.get(warehouse).income + R.types.get(fuel).income;
  CHECK(sim.playerState(0).incomePerMinute == income);
  CHECK(std::abs(sim.playerState(0).credits - income) <= 30);
  // A second yard allows a second fuel depot.
  spawn(sim, R.t("ger_yard"), 0, {{60 * T, 30 * T}});
  CHECK(sim.roomFor(0, fuel) == 1);
  CHECK(sim.canProduce(0, fuel));
  CHECK(sim.roomFor(0, post) == 5);
}

TEST_CASE("a logistics warehouse makes friends near it fire faster") {
  Rules R;
  Map m(96, 64, 100);
  Simulation sim(m, R.types, R.weapons, 6);
  spawn(sim, R.t("gbr_warehouse"), 0, {{20 * T, 30 * T}});
  sim.tick();
  const UnitTypeDef& w = R.types.get(R.t("gbr_warehouse"));
  REQUIRE(w.auraFireRate > 0);
  CHECK(sim.fireRateBonus(0, 24 * T, 30 * T) == w.auraFireRate);
  CHECK(sim.fireRateBonus(0, 20 * T + w.auraRadius + 2 * T, 30 * T) == 0);  // out of reach
  CHECK(sim.fireRateBonus(1, 24 * T, 30 * T) == 0);                          // not for the enemy
  // Two MG teams shoot at the same kind of target for the same time: the one by the warehouse fires more.
  auto shots = [&](int x) {
    Map mm(96, 64, 100);
    Simulation s(mm, R.types, R.weapons, 7);
    spawn(s, R.t("gbr_warehouse"), 0, {{20 * T, 30 * T}});
    spawn(s, R.t("gbr_mg_team"), 0, {{x * T, 40 * T}});
    spawn(s, R.t("ger_heavy_tank"), 1, {{x * T, 44 * T}});  // bullets bounce off: it stands there all day
    Command hold;
    hold.type = CommandType::SetStance;
    hold.stance = Stance::HoldFire;
    for (uint32_t e : s.registry().pool<CUnit>().entities())
      if (s.registry().pool<CUnit>().get(e).owner == 1) hold.units.push_back(s.registry().handle(e));
    s.queue(hold);
    int n = 0;
    for (int i = 0; i < 15 * 60; ++i) {
      s.tick();
      for (const SimEvent& ev : s.events()) n += ev.type == EventType::Fire && ev.owner == 0;
    }
    return n;
  };
  const int near = shots(24), far = shots(60);
  REQUIRE(far > 20);
  CHECK(near * 100 >= far * 106);
  CHECK(near * 100 <= far * 114);
}

TEST_CASE("levy infantry are called up at the post office: cheap, weak, every army has them") {
  Rules R;
  for (const char* f : {"ger", "rus", "gbr"}) {
    const std::string F = f;
    const char* levyId = F == "ger" ? "ger_volkssturm" : F == "rus" ? "rus_conscript" : "gbr_recruit";
    const UnitTypeDef& levy = R.types.get(R.t(levyId));
    const UnitTypeDef& rifle = R.types.get(R.t((F + "_rifleman").c_str()));
    CHECK(levy.cost * 2 <= rifle.cost);
    CHECK(levy.maxHealth < rifle.maxHealth);
    CHECK(levy.builtAt == "post");
    Map m(64, 64, 100);
    Simulation sim(m, R.types, R.weapons, 8);
    spawn(sim, R.t((F + "_yard").c_str()), 0, {{20 * T, 30 * T}});
    spawn(sim, R.t((F + "_power").c_str()), 0, {{14 * T, 24 * T}});
    CHECK_FALSE(sim.canProduce(0, R.t(levyId)));
    const Entity post = spawn(sim, R.t((F + "_post").c_str()), 0, {{14 * T, 36 * T}}).front();
    CHECK(sim.canProduce(0, R.t(levyId)));
    credits(sim, 0, 1000);
    Command make;
    make.type = CommandType::Produce;
    make.player = 0;
    make.unitType = R.t(levyId);
    make.amount = 10;
    sim.queue(make);
    run(sim, 12);
    CHECK(countOf(sim, R.t(levyId), 0) == 10);
    // They came out of the post office, not a barracks (there is none).
    const CPosition& pp = sim.registry().get<CPosition>(post);
    for (uint32_t e : sim.registry().pool<CUnit>().entities()) {
      if (sim.registry().pool<CUnit>().get(e).type != R.t(levyId)) continue;
      const CPosition& p = sim.registry().pool<CPosition>().get(e);
      CHECK(distance(p.x - pp.x, p.y - pp.y) < 6 * T);
    }
  }
}

TEST_CASE("a game saved with convoys on the road continues exactly") {
  Rules R;
  auto build = [&](Simulation& sim) {
    spawn(sim, R.t("ger_yard"), 0, {{14 * T, 30 * T}});
    spawn(sim, R.t("ger_rifleman"), 0, row({60 * T, 32 * T}, 3));
    spawn(sim, R.t("rus_rifleman"), 1, row({58 * T, 12 * T}, 2));  // on their way to a second point
  };
  Map m1(96, 64, 100), m2(96, 64, 100);
  m1.supplyPoints = m2.supplyPoints = {{60, 30}, {60, 10}};
  Simulation a(m1, R.types, R.weapons, 9);
  build(a);
  run(a, 20);  // the first office is up and its trucks are loading; the second point is half taken
  REQUIRE(a.officeOn(0).valid());
  ByteWriter w;
  a.saveState(w);
  Simulation b(m2, R.types, R.weapons, 99);
  ByteReader r(w.bytes());
  std::string err;
  REQUIRE(b.loadState(r, err));
  CHECK(b.stateHash() == a.stateHash());
  CHECK(b.pointState(1).progress == a.pointState(1).progress);
  run(a, 90);
  run(b, 90);
  CHECK(b.stateHash() == a.stateHash());
  CHECK(b.playerState(0).credits == a.playerState(0).credits);
  CHECK(a.playerState(0).credits >= 400);  // and money has come in
}

TEST_CASE("generated maps have a few logistics points, far from every base, in mirrored pairs") {
  for (int size : {96, 128, 160, 256})
    for (uint64_t seed : {11ull, 12ull, 13ull}) {
      Map m(size, size);
      m.generate(static_cast<Biome>(seed % 4), seed);
      const int n = static_cast<int>(m.supplyPoints.size());
      INFO("size " << size << " seed " << seed << ": " << n << " points");
      CHECK(n >= 2);
      CHECK(n <= std::max(3, size / 40));
      const Vec2i homes[4] = {{size * 17 / 100, size / 2}, {size * 83 / 100, size / 2}, {size / 2, size * 17 / 100}, {size / 2, size * 83 / 100}};
      int twins = 0;
      for (const Vec2i& p : m.supplyPoints) {
        for (const Vec2i& h : homes) CHECK(distance(p.x - h.x, p.y - h.y) >= static_cast<uint32_t>(size * 17 / 100));
        for (const Vec2i& q : m.supplyPoints)
          if (p != q && std::abs(q.x - (size - 1 - p.x)) <= 6 && std::abs(q.y - (size - 1 - p.y)) <= 6) {
            ++twins;
            break;
          }
        for (const Vec2i& q : m.supplyPoints)
          if (p != q) CHECK(distance(p.x - q.x, p.y - q.y) >= static_cast<uint32_t>(size * 11 / 100));
      }
      CHECK(twins >= n - 1);  // all but an odd one in the middle
    }
}

// The computer player in 9.7: a main enemy and raids on the others in a
// free-for-all, armies kept in bound units, a scout on watch, and money
// turned into production (further works and barracks speed their line).
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

// Places a structure on the nearest clear ground.
Entity place(Simulation& sim, uint16_t type, uint8_t owner, Vec2i at) {
  for (int r = 0; r < 12; ++r)
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
        const Vec2i p{at.x + dx * T, at.y + dy * T};
        if (!sim.canPlace(type, sim.footprintOrigin(type, p))) continue;
        Command c;
        c.type = CommandType::SpawnList;
        c.spawns = {{type, owner, p, 0, -1}};
        sim.queue(c);
        sim.tick();
        return sim.spawnLog().empty() ? kNullEntity : sim.spawnLog().front().entity;
      }
  return kNullEntity;
}

void base(Simulation& sim, const Rules& R, uint8_t owner, const char* f, Vec2i at, int credits = 6000) {
  const std::string F = f;
  place(sim, R.t((F + "_yard").c_str()), owner, at);
  place(sim, R.t((F + "_power").c_str()), owner, {at.x - 5 * T, at.y - 4 * T});
  place(sim, R.t((F + "_post").c_str()), owner, {at.x - 5 * T, at.y + 3 * T});
  place(sim, R.t((F + "_warehouse").c_str()), owner, {at.x - 1 * T, at.y + 7 * T});
  place(sim, R.t((F + "_barracks").c_str()), owner, {at.x + 5 * T, at.y + 3 * T});
  Command c;
  c.type = CommandType::SetCredits;
  c.player = owner;
  c.amount = credits;
  sim.queue(c);
}

void computer(Simulation& sim, uint8_t p, int difficulty = 2) {
  Command c;
  c.type = CommandType::SetController;
  c.player = p;
  c.amount = difficulty;
  sim.queue(c);
}

void spawn(Simulation& sim, uint16_t type, uint8_t owner, Vec2i at, int count) {
  Command s;
  s.type = CommandType::Spawn;
  s.player = owner;
  s.unitType = type;
  s.count = count;
  s.target = at;
  sim.queue(s);
}

int32_t gap(Vec2i a, Vec2i b) { return static_cast<int32_t>(distance(a.x - b.x, a.y - b.y)); }
}  // namespace

TEST_CASE("with several enemies the computer goes for the nearest and raids the others") {
  Rules R;
  Map map(160, 160);
  map.generateCountryside(1946);
  Simulation sim(map, R.types, R.weapons, 31);
  // The computer (Germany) in the west; a Soviet base near it to the
  // north-east; a British base far off in the east. Every side for itself.
  const Vec2i home{24 * T, 80 * T}, near{78 * T, 44 * T}, far{136 * T, 96 * T};
  base(sim, R, 0, "ger", home, 12000);
  base(sim, R, 1, "rus", near);
  base(sim, R, 2, "gbr", far);
  spawn(sim, R.t("ger_rifleman"), 0, {32 * T, 80 * T}, 10);
  spawn(sim, R.t("rus_rifleman"), 1, {76 * T, 50 * T}, 6);
  spawn(sim, R.t("gbr_rifleman"), 2, {132 * T, 96 * T}, 6);
  computer(sim, 0);
  sim.tick();
  REQUIRE_FALSE(sim.allied(1, 2));

  auto stands = [&](uint8_t p) {
    for (uint32_t e : sim.registry().pool<CStructure>().entities())
      if (sim.registry().pool<CUnit>().get(e).owner == p) return true;
    return false;
  };
  bool raidedFar = false, raidersNearFar = false, waveAtNear = false, waveAtFarFirst = false, turnedOnFar = false;
  int farLosses = 0;
  for (int i = 0; i < 15 * 60 * 16; ++i) {
    sim.tick();
    const AiPlayer& ai = sim.ai().player(0);
    for (const SimEvent& ev : sim.events())
      if (ev.type == EventType::UnitDied && ev.owner == 2) ++farLosses;
    if (i % 15 || i < 15 * 30) continue;
    const bool nearStands = stands(1);
    if (nearStands) CHECK(ai.mainEnemy == 1);  // the nearest, and it stays the main enemy while it stands
    else if (stands(2) && i % 300 == 0 && ai.mainEnemy == 2) turnedOnFar = true;  // then the next one
    if (!ai.raiders.empty() && ai.raidEnemy == 2 && nearStands) {
      raidedFar = true;
      for (Entity e : ai.raiders)
        if (sim.registry().alive(e)) {
          const CPosition& c = sim.registry().get<CPosition>(e);
          raidersNearFar |= gap({c.x, c.y}, far) < 20 * T;
        }
    }
    if (nearStands && ai.phase == 1 && ai.flank.x < 0 && ai.waveTarget.x >= 0) {
      waveAtNear |= gap(ai.waveTarget, near) < 24 * T;
      waveAtFarFirst |= gap(ai.waveTarget, far) < 24 * T;
    }
  }
  CHECK(waveAtNear);             // the main force went for the nearest enemy
  CHECK_FALSE(waveAtFarFirst);   // and not for the far one while the near one stood
  CHECK(raidedFar);              // which the raiders were sent against meanwhile
  CHECK(raidersNearFar);         // and reached
  CHECK(farLosses > 0);          // and hurt
  if (!stands(1)) CHECK((turnedOnFar || !stands(2)));  // with the nearest beaten, the far one is next
}

TEST_CASE("the computer keeps most of its army in bound units, and a scout on watch") {
  Rules R;
  Map map(128, 128);
  map.generateCountryside(1951);
  Simulation sim(map, R.types, R.weapons, 11);
  base(sim, R, 0, "ger", {22 * T, 64 * T});
  base(sim, R, 1, "rus", {106 * T, 64 * T});
  for (uint8_t p : {uint8_t{0}, uint8_t{1}}) {
    spawn(sim, R.t(p ? "rus_rifleman" : "ger_rifleman"), p, {(p ? 96 : 32) * T, 64 * T}, 10);
    computer(sim, p);
  }
  sim.tick();
  int64_t army[2] = {}, inUnits[2] = {}, bonus[2] = {};
  bool scoutOut[2] = {};
  Registry& reg = sim.registry();
  for (int i = 0; i < 15 * 60 * 10; ++i) {
    sim.tick();
    if (i % 15 || i < 15 * 60 * 2) continue;  // sampled once a second from the second minute on
    for (uint32_t e : reg.pool<CUnit>().entities()) {
      const CUnit& u = reg.pool<CUnit>().get(e);
      if (u.owner > 1) continue;
      const UnitTypeDef& d = sim.types().get(u.type);
      if (d.isStructure() || d.weapons.empty() || d.capacity > 0) continue;
      ++army[u.owner];
      const CGroup* g = sim.groupOf(reg.handle(e));
      inUnits[u.owner] += g && (g->combined || g->echelon > Echelon::Fireteam);
      bonus[u.owner] += sim.groupBonus(reg.handle(e));
    }
    for (uint8_t p = 0; p < 2; ++p) {
      const AiPlayer& ai = sim.ai().player(p);
      if (!reg.alive(ai.scout)) continue;
      CHECK(sim.types().get(reg.get<CUnit>(ai.scout).type).scout);
      const CPosition& c = reg.get<CPosition>(ai.scout);
      scoutOut[p] |= gap({c.x, c.y}, ai.base) > 25 * T;
    }
  }
  for (int p = 0; p < 2; ++p) {
    REQUIRE(army[p] > 0);
    CHECK(inUnits[p] * 100 / army[p] >= 65);  // squads, platoons and battle groups, not just the fireteams men form by themselves
    CHECK(bonus[p] * 10 / army[p] >= 60);     // at least +6% firepower on average
    CHECK(scoutOut[p]);                       // its scout went out to watch
  }
}

TEST_CASE("further barracks and works speed up their line") {
  Rules R;
  auto timeFor = [&](int barracks) {
    Map map(64, 64, 500);
    Simulation sim(map, R.types, R.weapons, 3);
    place(sim, R.t("ger_yard"), 0, {30 * T, 30 * T});
    place(sim, R.t("ger_power"), 0, {24 * T, 26 * T});
    place(sim, R.t("ger_power"), 0, {24 * T, 34 * T});
    for (int i = 0; i < barracks; ++i) place(sim, R.t("ger_barracks"), 0, {(12 + i * 9) * T, 46 * T});
    Command m;
    m.type = CommandType::SetCredits;
    m.player = 0;
    m.amount = 5000;
    sim.queue(m);
    Command c;
    c.type = CommandType::Produce;
    c.player = 0;
    c.unitType = R.t("ger_rifleman");
    c.amount = 12;
    sim.queue(c);
    sim.tick();
    CHECK(sim.lineSpeed(0, R.t("ger_rifleman")) == 2 + std::min(4, barracks - 1));
    int ticks = 0;
    auto made = [&] {
      int n = 0;
      for (uint32_t e : sim.registry().pool<CUnit>().entities()) n += sim.registry().pool<CUnit>().get(e).type == R.t("ger_rifleman");
      return n;
    };
    while (made() < 12 && ticks < 15 * 120) {
      sim.tick();
      ++ticks;
    }
    CHECK(made() == 12);
    // The price is the same however fast the line (the yard's own trickle of income comes on top).
    const int32_t spent = 12 * R.types.get(R.t("ger_rifleman")).cost;
    CHECK(sim.playerState(0).credits >= 5000 - spent);
    CHECK(sim.playerState(0).credits <= 5000 - spent + 130 * ticks / 900 + 2);
    return ticks;
  };
  const int one = timeFor(1), two = timeFor(2), five = timeFor(5), seven = timeFor(7);
  CHECK(two * 3 <= one * 2 + 30);   // half as fast again
  CHECK(two * 3 >= one * 2 - 30);
  CHECK(five * 3 <= one + 30);      // three times the speed with five
  CHECK(five * 3 >= one - 30);
  CHECK(seven == five);             // and no more beyond that
}

TEST_CASE("vehicles of different kinds, too few of each for a unit of their own, bind into one mixed platoon") {
  Rules R;
  Map map(64, 64, 501);
  Simulation sim(map, R.types, R.weapons, 3);
  Command c;
  c.type = CommandType::SpawnList;
  const char* kinds[5] = {"ger_scout_car", "ger_scout_car", "ger_halftrack", "ger_halftrack", "ger_light_tank"};
  for (int i = 0; i < 5; ++i) c.spawns.push_back({R.t(kinds[i]), 0, {(20 + i * 2) * T, 20 * T}, 0, -1});
  sim.queue(c);
  sim.tick();
  std::vector<Entity> party;
  for (const auto& r : sim.spawnLog()) party.push_back(r.entity);
  REQUIRE(party.size() == 5);
  Command b;
  b.type = CommandType::BindGroup;
  b.player = 0;
  b.units = party;
  sim.queue(b);
  sim.tick();
  const CGroup* g = sim.groupOf(party.front());
  REQUIRE(g);
  CHECK(g->members.size() == 5);
  CHECK(g->vehicles);
  CHECK(g->echelon == Echelon::Platoon);
  for (Entity e : party) CHECK(sim.groupOf(e) == g);
  CHECK(sim.groupBonus(party.back()) > 0);
}

TEST_CASE("a free-for-all saved in mid-game continues exactly (the computer's main enemy, raids and scout are saved)") {
  Rules R;
  Map map(128, 128);
  map.generateCountryside(1947);
  Simulation sim(map, R.types, R.weapons, 5);
  base(sim, R, 0, "ger", {20 * T, 64 * T});
  base(sim, R, 1, "rus", {64 * T, 22 * T});
  base(sim, R, 2, "gbr", {108 * T, 64 * T});
  for (uint8_t p = 0; p < 3; ++p) {
    static const char* men[3] = {"ger_rifleman", "rus_rifleman", "gbr_rifleman"};
    spawn(sim, R.t(men[p]), p, {(p == 0 ? 28 : p == 1 ? 64 : 100) * T, (p == 1 ? 30 : 64) * T}, 10);
    computer(sim, p);
  }
  sim.tick();
  for (int i = 0; i < 15 * 60 * 5; ++i) sim.tick();
  CHECK(sim.ai().player(0).mainEnemy < MAX_PLAYERS);

  ByteWriter w;
  sim.saveState(w);
  Map copy = map;
  Simulation loaded(copy, R.types, R.weapons, 777);
  ByteReader r(w.bytes());
  std::string err;
  REQUIRE(loaded.loadState(r, err));
  CHECK(r.atEnd());
  for (uint8_t p = 0; p < 3; ++p) {
    CHECK(loaded.ai().player(p).mainEnemy == sim.ai().player(p).mainEnemy);
    CHECK(loaded.ai().player(p).raidEnemy == sim.ai().player(p).raidEnemy);
    CHECK(loaded.ai().player(p).raids == sim.ai().player(p).raids);
    CHECK(loaded.ai().player(p).scout == sim.ai().player(p).scout);
  }
  for (int i = 0; i < 15 * 60 * 2; ++i) {
    sim.tick();
    loaded.tick();
  }
  CHECK(loaded.stateHash() == sim.stateHash());
}

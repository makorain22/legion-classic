// Pioneer part 3: vision 2.0 (scouted sight, line of sight, smoke, woodland)
// and the unit condition fields.
#include <doctest/doctest.h>

#include <chrono>
#include <map>

#include <nlohmann/json.hpp>

#include "pioneer/map/map.hpp"
#include "pioneer/sim/simulation.hpp"
#include "pioneer/sim/vision.hpp"

using namespace pioneer;

namespace {
constexpr int T = 256;
Vec2i tc(int x, int y) { return {x * T + 128, y * T + 128}; }

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

Entity one(Simulation& sim, uint16_t type, uint8_t owner, Vec2i at) {
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{type, owner, at, 0, -1}};
  sim.queue(c);
  sim.tick();
  REQUIRE_FALSE(sim.spawnLog().empty());
  return sim.spawnLog().front().entity;
}

void rules(Simulation& sim, uint16_t flags, int percent = 100) {
  Command c;
  c.type = CommandType::SetVisionRules;
  c.count = flags;
  c.amount = percent;
  sim.queue(c);
}

int seenTiles(const Vision& v, int player, uint8_t bit) {
  int n = 0;
  for (uint8_t b : v.grid(player)) n += (b & bit) ? 1 : 0;
  return n;
}
}  // namespace

TEST_CASE("vision: sight along rays over flat open ground marks what the plain circle marks") {
  for (int r : {1, 3, 7, 12, 24, 48}) {
    Vision a, b;
    a.resize(128, 128);
    b.resize(128, 128);
    a.prepare(r);
    b.prepare(r);
    a.begin();
    b.begin();
    a.stamp(0, 64, 60, r);
    b.stampRays(0, 64, 60, r, Vision::kVisible | Vision::kExplored, nullptr, 0);
    int missing = 0, extra = 0;
    for (int y = 0; y < 128; ++y)
      for (int x = 0; x < 128; ++x) {
        missing += a.visible(0, x, y) && !b.visible(0, x, y);
        extra += !a.visible(0, x, y) && b.visible(0, x, y);
      }
    CHECK_MESSAGE(missing == 0, "radius ", r, ": ", missing, " tiles the rays miss");
    CHECK_MESSAGE(extra == 0, "radius ", r);
  }
}

TEST_CASE("vision: a scout's sight is marked as scouted, and fades with it") {
  Vision v;
  v.resize(64, 64);
  v.prepare(5);
  v.begin();
  v.stamp(0, 20, 20, 5);
  v.stamp(0, 40, 40, 5, Vision::kVisible | Vision::kExplored | Vision::kScouted);
  CHECK(v.visible(0, 20, 20));
  CHECK_FALSE(v.scouted(0, 20, 20));
  CHECK(v.scouted(0, 41, 42));
  v.begin();
  CHECK_FALSE(v.scouted(0, 41, 42));
  CHECK_FALSE(v.visible(0, 41, 42));
  CHECK(v.explored(0, 41, 42));
}

TEST_CASE("vision: with line of sight a ridge hides the valley behind it, not the hill beyond") {
  Vision v;
  v.resize(64, 16);
  std::vector<int32_t> h(64 * 16, 100);
  for (int y = 0; y < 16; ++y) {
    for (int x = 20; x < 23; ++x) h[static_cast<size_t>(y) * 64 + x] = 220;  // the ridge
    for (int x = 40; x < 44; ++x) h[static_cast<size_t>(y) * 64 + x] = 420;  // a hill beyond
  }
  v.prepare(40);
  v.begin();
  v.stampRays(0, 5, 8, 40, Vision::kVisible | Vision::kExplored, h.data(), 24);
  CHECK(v.visible(0, 12, 8));        // the near ground
  CHECK(v.visible(0, 20, 8));        // the ridge's face
  CHECK_FALSE(v.visible(0, 28, 8));  // the valley behind it
  CHECK_FALSE(v.visible(0, 36, 8));
  CHECK(v.visible(0, 41, 8));        // the hill stands above the ridge line
  CHECK_FALSE(v.visible(0, 44, 8));  // and hides what is behind it in turn
  // From the top of the ridge the valley is in plain view.
  v.begin();
  v.stampRays(0, 21, 8, 30, Vision::kVisible | Vision::kExplored, h.data(), 24);
  CHECK(v.visible(0, 28, 8));
  CHECK(v.visible(0, 36, 8));
}

TEST_CASE("smoke blinds: nobody shoots through it, and it blows away") {
  Rules R;
  Map map(64, 32, 100);
  Simulation sim(map, R.types, R.weapons, 1);
  const Entity a = one(sim, R.t("ger_rifleman"), 0, tc(20, 16));
  const Entity b = one(sim, R.t("rus_rifleman"), 1, tc(24, 16));
  for (int i = 0; i < 8; ++i) sim.tick();
  CHECK(sim.unitVisibleTo(0, b.index));
  // A cloud between them (and over the Russian).
  Command s;
  s.type = CommandType::Smoke;
  s.target = tc(23, 16);
  s.radius = 2 * T;
  s.amount = 6;  // seconds
  sim.queue(s);
  for (int i = 0; i < 12; ++i) sim.tick();
  REQUIRE(sim.smoke().size() == 1);
  CHECK((sim.vision().ground(23, 16) & Vision::kSmoke) != 0);
  CHECK_FALSE(sim.unitVisibleTo(0, b.index));
  CHECK_FALSE(sim.registry().get<CCombat>(a).target.valid());
  const int32_t health = sim.registry().get<CUnit>(b).health;
  for (int i = 0; i < 15 * 3; ++i) sim.tick();
  CHECK(sim.registry().get<CUnit>(b).health == health);  // nobody hit anybody
  // Saved in mid-cloud and loaded: the same battle.
  {
    ByteWriter w;
    sim.saveState(w);
    Map copy = map;
    Simulation loaded(copy, R.types, R.weapons, 9);
    ByteReader r(w.bytes());
    std::string err;
    REQUIRE_MESSAGE(loaded.loadState(r, err), err);
    CHECK(loaded.smoke().size() == 1);
    for (int i = 0; i < 20; ++i) {
      sim.tick();
      loaded.tick();
    }
    CHECK(loaded.stateHash() == sim.stateHash());
    CHECK(Simulation::digestDifference(loaded.digest(), sim.digest()).empty());
  }
  for (int i = 0; i < 15 * 5; ++i) sim.tick();
  CHECK(sim.smoke().empty());
  CHECK((sim.vision().ground(23, 16) & Vision::kSmoke) == 0);
  CHECK(sim.unitVisibleTo(0, b.index));
  for (int i = 0; i < 15 * 4; ++i) sim.tick();
  CHECK((!sim.registry().alive(b) || sim.registry().get<CUnit>(b).health < health || !sim.registry().alive(a)));  // the fight is on again
}

TEST_CASE("woodland rule: men inside a wood are seen only by scouts; the outskirts and vehicles as ever") {
  Rules R;
  Map map(64, 64, 100);
  // A wood of 12 x 12 tiles, a tree on every tile.
  for (int y = 26; y < 38; ++y)
    for (int x = 30; x < 42; ++x) {
      Prop p;
      p.kind = PropKind::Pine;
      p.x = x * T + 128;
      p.y = y * T + 128;
      map.props().push_back(p);
    }
  Simulation sim(map, R.types, R.weapons, 1);
  const Entity deep = one(sim, R.t("rus_rifleman"), 1, tc(35, 32));  // well inside
  const Entity edge = one(sim, R.t("rus_rifleman"), 1, tc(30, 32));  // on the outskirts
  const Entity tank = one(sim, R.t("rus_medium_tank"), 1, tc(36, 30));
  const Entity watcher = one(sim, R.t("ger_rifleman"), 0, tc(26, 32));
  Command hold;
  hold.type = CommandType::SetStance;
  hold.stance = Stance::HoldFire;
  hold.units = {deep, edge, tank, watcher};
  sim.queue(hold);
  for (int i = 0; i < 8; ++i) sim.tick();
  // Without the rule: all in view (the rifleman sees 7 tiles... check what he does see).
  const bool sawDeepBefore = sim.unitVisibleTo(0, deep.index);
  CHECK_FALSE(sim.concealed(deep.index));
  CHECK(sim.vision().ground(35, 32) == 0);

  rules(sim, Simulation::VisionRules::kWoodland);
  for (int i = 0; i < 8; ++i) sim.tick();
  CHECK(sim.visionRules().woodland());
  CHECK((sim.vision().ground(35, 32) & Vision::kWood) != 0);
  CHECK((sim.vision().ground(35, 32) & Vision::kWoodEdge) == 0);
  CHECK((sim.vision().ground(30, 32) & Vision::kWoodEdge) != 0);
  CHECK(sim.vision().ground(20, 20) == 0);
  CHECK(sim.concealed(deep.index));
  CHECK_FALSE(sim.concealed(edge.index));
  CHECK_FALSE(sim.concealed(tank.index));  // a tank is no man
  // Bring the watcher right up to the wood: the man on the edge is seen, the one inside is not.
  Command mv;
  mv.type = CommandType::Edit;
  UnitEdit ed;
  ed.unit = watcher;
  ed.move = true;
  ed.pos = tc(29, 32);
  mv.edits = {ed};
  sim.queue(mv);
  for (int i = 0; i < 8; ++i) sim.tick();
  CHECK(sim.vision().visible(0, 35, 32));  // the ground itself is in sight
  CHECK(sim.unitVisibleTo(0, edge.index));
  CHECK(sim.unitVisibleTo(0, tank.index));
  CHECK_FALSE(sim.unitVisibleTo(0, deep.index));
  CHECK(sim.unitVisibleTo(1, deep.index));  // his own side knows where he is
  // A scout car looks: now he is seen.
  const Entity scout = one(sim, R.t("ger_scout_car"), 0, tc(22, 32));
  Command hs = hold;
  hs.units = {scout};
  sim.queue(hs);
  for (int i = 0; i < 8; ++i) sim.tick();
  CHECK(sim.vision().scouted(0, 35, 32));
  CHECK(sim.unitVisibleTo(0, deep.index));
  // The rule off again: as before.
  rules(sim, 0);
  for (int i = 0; i < 8; ++i) sim.tick();
  CHECK_FALSE(sim.concealed(deep.index));
  CHECK(sim.vision().ground(35, 32) == 0);
  (void)sawDeepBefore;
}

TEST_CASE("weather: sight scaled for the whole battle") {
  Rules R;
  Map map(64, 64, 100);
  Simulation sim(map, R.types, R.weapons, 1);
  one(sim, R.t("ger_rifleman"), 0, tc(32, 32));
  for (int i = 0; i < 8; ++i) sim.tick();
  const int clear = seenTiles(sim.vision(), 0, Vision::kVisible);
  rules(sim, 0, 50);
  for (int i = 0; i < 8; ++i) sim.tick();
  const int fog = seenTiles(sim.vision(), 0, Vision::kVisible);
  CHECK(sim.visionRules().sightPercent == 50);
  CHECK(fog * 3 < clear);  // half the radius: about a quarter of the ground
  CHECK(fog > 0);
  rules(sim, 0, 100);
  for (int i = 0; i < 8; ++i) sim.tick();
  CHECK(seenTiles(sim.vision(), 0, Vision::kVisible) == clear);
}

TEST_CASE("a unit's condition is carried, saved and counted in the checksum") {
  Rules R;
  Map map(32, 32, 100);
  Simulation sim(map, R.types, R.weapons, 1);
  const Entity e = one(sim, R.t("ger_rifleman"), 0, tc(10, 10));
  CHECK(sim.condition(e) == nullptr);
  const uint64_t before = sim.stateHash();
  CCondition& c = sim.conditionOf(e);
  CHECK(c.morale == 1000);
  CHECK(sim.stateHash() != before);
  c.suppression = 640;
  c.posture = Posture::Prone;
  c.flags = CCondition::kAmbush;
  c.experience = 12;
  ByteWriter w;
  sim.saveState(w);
  Map copy = map;
  Simulation loaded(copy, R.types, R.weapons, 2);
  ByteReader r(w.bytes());
  std::string err;
  REQUIRE_MESSAGE(loaded.loadState(r, err), err);
  const CCondition* l = loaded.condition(e);
  REQUIRE(l);
  CHECK(l->suppression == 640);
  CHECK(l->posture == Posture::Prone);
  CHECK(l->flags == CCondition::kAmbush);
  CHECK(l->experience == 12);
  CHECK(loaded.stateHash() == sim.stateHash());
  // It goes with the unit.
  Command k;
  k.type = CommandType::Remove;
  k.units = {e};
  sim.queue(k);
  sim.tick();
  CHECK(sim.registry().pool<CCondition>().size() == 0);
}

TEST_CASE("the budget: 1500 soldiers and tanks fight across a 256 x 256 map") {
  Rules R;
  Map map(256, 256);
  map.generateCountryside(1946);
  Simulation sim(map, R.types, R.weapons, 3);
  // Two armies of 750 in blocks of 50, facing each other across the middle; each block attacks the one opposite.
  static const char* ger[] = {"ger_rifleman", "ger_rifleman", "ger_medium_tank"};
  static const char* rus[] = {"rus_rifleman", "rus_rifleman", "rus_medium_tank"};
  auto home = [](int b, uint8_t p) { return tc(p ? 140 + (b % 3) * 12 : 116 - (b % 3) * 12, 40 + b * 12); };
  size_t units = 0;
  for (int b = 0; b < 15; ++b)
    for (uint8_t p = 0; p < 2; ++p) {
      Command s;
      s.type = CommandType::Spawn;
      s.player = p;
      s.unitType = R.t((p ? rus : ger)[b % 3]);
      s.count = 50;
      s.target = home(b, p);
      sim.queue(s);
      sim.tick();
      Command c;
      c.type = CommandType::AttackMove;
      c.player = p;
      c.target = home(b, p ? 0 : 1);
      for (uint32_t i = static_cast<uint32_t>(units); i < sim.registry().pool<CUnit>().entities().size(); ++i)
        c.units.push_back(sim.registry().handle(sim.registry().pool<CUnit>().entities()[i]));
      units = sim.unitCount();
      sim.queue(c);
    }
  sim.tick();
  CHECK(units == 1500);
  const int ticks = 15 * 60;
  double worst = 0;
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < ticks; ++i) {
    const auto a = std::chrono::steady_clock::now();
    sim.tick();
    worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count());
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / ticks;
  const Pathfinder::Stats& ps = sim.pathfinder().stats();
  MESSAGE(units, " units at the start, ", sim.unitCount(), " after a minute of battle: ", ms, " ms a tick on average (",
          1000.0 / 15.0, " ms is real time), worst ", worst, " ms; routes: ", ps.straight, " straight, ", ps.searches,
          " searched, ", ps.cacheHits, " from the cache");
  CHECK(sim.kills(0) + sim.kills(1) > 50);  // they did fight
  std::map<std::string, int> kinds;
  for (uint32_t i : sim.registry().pool<CUnit>().entities()) ++kinds[R.types.get(sim.registry().pool<CUnit>().get(i).type).id];
  std::string what;
  for (const auto& [id, n] : kinds) what += id + " " + std::to_string(n) + "  ";
  MESSAGE("standing at the end: ", what);
  // Generous: a debug build on a loaded machine must pass too. The number to read is in the message.
  CHECK(ms < 400.0);
}

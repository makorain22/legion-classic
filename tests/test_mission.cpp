// The mission engine (9.9): the trigger language's new conditions and
// actions, named units and set pieces, the play area, countdowns, the
// computer player taking orders from a script, maps that aren't square, and
// all of it surviving a save.
#include <doctest/doctest.h>

#include <algorithm>
#include <nlohmann/json.hpp>

#include "pioneer/core/bytes.hpp"
#include "pioneer/map/map.hpp"
#include "pioneer/map/map_io.hpp"
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
  int t(const char* id) const {
    const int i = types.find(id);
    REQUIRE(i >= 0);
    return i;
  }
};

std::vector<Entity> spawn(Simulation& sim, int type, uint8_t owner, const std::vector<Vec2i>& at) {
  Command c;
  c.type = CommandType::SpawnList;
  for (const Vec2i& p : at) c.spawns.push_back({static_cast<uint16_t>(type), owner, p, 0, -1});
  sim.queue(c);
  sim.tick();
  std::vector<Entity> out;
  for (const auto& r : sim.spawnLog()) out.push_back(r.entity);
  return out;
}
std::vector<Vec2i> row(Vec2i at, int n, int step = 150) {
  std::vector<Vec2i> out;
  for (int i = 0; i < n; ++i) out.push_back({at.x + i * step, at.y});
  return out;
}
Entity place(Simulation& sim, int type, uint8_t owner, Vec2i at) {
  for (int r = 0; r < 12; ++r)
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
        const Vec2i p{at.x + dx * T, at.y + dy * T};
        if (!sim.canPlace(type, sim.footprintOrigin(type, p))) continue;
        return spawn(sim, type, owner, {p}).front();
      }
  return kNullEntity;
}
void computer(Simulation& sim, uint8_t p, int difficulty = 2) {
  Command c;
  c.type = CommandType::SetController;
  c.player = p;
  c.amount = difficulty;
  sim.queue(c);
}
void run(Simulation& sim, int seconds) {
  for (int i = 0; i < seconds * TICKS_PER_SECOND; ++i) sim.tick();
}
bool alive(Simulation& sim, Entity e) {
  return sim.registry().alive(e) && sim.registry().has<CUnit>(e) && sim.registry().get<CUnit>(e).health > 0;
}
Vec2i at(Simulation& sim, Entity e) {
  const CPosition& p = sim.registry().get<CPosition>(e);
  return {p.x, p.y};
}

// Script-building shorthand.
struct S {
  ScenarioScript s;
  int area(int x0, int y0, int x1, int y1) {
    s.areas.push_back({"A" + std::to_string(s.areas.size()), x0, y0, x1, y1});
    return static_cast<int>(s.areas.size()) - 1;
  }
  int on(std::vector<Condition> c, std::vector<Action> a, bool repeat = false, bool any = false) {
    Trigger t;
    t.conditions = std::move(c);
    t.actions = std::move(a);
    t.repeat = repeat;
    t.any = any;
    s.triggers.push_back(std::move(t));
    return static_cast<int>(s.triggers.size()) - 1;
  }
};
Condition cond(CondType t, uint8_t player = 0, int area = -1, int value = 0, const std::string& text = {}) {
  Condition c;
  c.type = t;
  c.player = player;
  c.area = area;
  c.value = value;
  c.text = text;
  return c;
}
Condition notc(Condition c) {
  c.negate = true;
  return c;
}
Action act(ActionType t, uint8_t player = ANY_PLAYER, int value = 0, const std::string& text = {}, int area = -1,
           int area2 = -1, int unit = -1) {
  Action a;
  a.type = t;
  a.player = player;
  a.value = value;
  a.text = text;
  a.area = area;
  a.area2 = area2;
  a.unitType = unit;
  return a;
}
}  // namespace

TEST_CASE("every condition and action keeps its id, and 'not', 'any' and 'scaled' survive the file") {
  Rules R;
  ScenarioScript s;
  s.areas.push_back({"Here", 1, 2, 3, 4});
  Trigger t;
  t.any = true;
  for (int i = 0; i < static_cast<int>(CondType::Count); ++i) {
    Condition c = cond(static_cast<CondType>(i), 1, 0, i, "x");
    c.negate = i % 2;
    t.conditions.push_back(c);
  }
  for (int i = 0; i < static_cast<int>(ActionType::Count); ++i) {
    Action a = act(static_cast<ActionType>(i), 2, i, "y", 0);
    a.scaled = i % 3 == 0;
    t.actions.push_back(a);
  }
  s.triggers.push_back(t);
  const ScenarioScript back = scriptFromJson(nlohmann::json::parse(scriptToJson(s, &R.types).dump()), &R.types);
  REQUIRE(back.triggers.size() == 1);
  const Trigger& b = back.triggers[0];
  CHECK(b.any);
  REQUIRE(b.conditions.size() == static_cast<size_t>(CondType::Count));
  REQUIRE(b.actions.size() == static_cast<size_t>(ActionType::Count));
  for (int i = 0; i < static_cast<int>(CondType::Count); ++i) {
    CHECK(static_cast<int>(b.conditions[static_cast<size_t>(i)].type) == i);
    CHECK(b.conditions[static_cast<size_t>(i)].negate == (i % 2 == 1));
    CHECK(std::string(condId(static_cast<CondType>(i))).size() > 0);
  }
  for (int i = 0; i < static_cast<int>(ActionType::Count); ++i) {
    CHECK(static_cast<int>(b.actions[static_cast<size_t>(i)].type) == i);
    CHECK(b.actions[static_cast<size_t>(i)].scaled == (i % 3 == 0));
  }
}

TEST_CASE("a command's text survives replays, and older files without it still read") {
  Command c;
  c.type = CommandType::TagUnits;
  c.text = "Oster's car";
  c.count = 3;
  c.amount = 1;
  ByteWriter w;
  writeCommand(w, c);
  ByteReader r(w.bytes());
  const Command d = readCommand(r);
  CHECK(d.type == CommandType::TagUnits);
  CHECK(d.text == "Oster's car");
  CHECK(r.atEnd());
  // An old command (no text at the end) reads with withText = false.
  ByteWriter old;
  writeCommand(old, Command{});
  std::vector<uint8_t> bytes = old.bytes();
  bytes.resize(bytes.size() - 4);  // the empty string's length
  ByteReader ro(bytes);
  readCommand(ro, false);
  CHECK(ro.ok());
  CHECK(ro.atEnd());
}

TEST_CASE("countdowns run, report, pause and stop") {
  Rules R;
  Map map(64, 64);
  Simulation sim(map, R.types, R.weapons, 1);
  S s;
  s.on({cond(CondType::Always)}, {act(ActionType::Countdown, 0, 10, "Columns across")});
  const int warn = s.on({cond(CondType::CountdownBelow, 0, -1, 4, "Columns across")}, {act(ActionType::SetCounter, ANY_PLAYER, 1, "warned")});
  const int done = s.on({cond(CondType::CountdownDone, 0, -1, 0, "Columns across")}, {act(ActionType::SetCounter, ANY_PLAYER, 1, "done")});
  sim.setScript(s.s, false);
  run(sim, 3);
  REQUIRE(sim.countdowns().size() == 1);
  CHECK(sim.countdownLeft("Columns across") == 7);
  CHECK_FALSE(sim.triggerFired(warn));
  run(sim, 4);
  CHECK(sim.triggerFired(warn));
  CHECK_FALSE(sim.triggerFired(done));
  bool event = false;
  for (int i = 0; i < 5 * TICKS_PER_SECOND; ++i) {
    sim.tick();
    for (const SimEvent& ev : sim.events()) event |= ev.type == EventType::CountdownDone && ev.text == "Columns across";
  }
  CHECK(event);
  CHECK(sim.triggerFired(done));
  CHECK(sim.counter("done") == 1);
  CHECK(sim.countdownLeft("Columns across") == 0);
  CHECK(sim.countdownLeft("nothing") == -1);

  // Pause and resume.
  S p;
  p.on({cond(CondType::Always)}, {act(ActionType::Countdown, 0, 20, "Hold")});
  p.on({cond(CondType::Timer, 0, -1, 2)}, {act(ActionType::PauseCountdown, 0, 1, "Hold")});
  p.on({cond(CondType::Timer, 0, -1, 12)}, {act(ActionType::PauseCountdown, 0, 0, "Hold")});
  p.on({cond(CondType::Timer, 0, -1, 14)}, {act(ActionType::StopCountdown, 0, 0, "Hold")});
  Map map2(64, 64);
  Simulation sim2(map2, R.types, R.weapons, 1);
  sim2.setScript(p.s, false);
  run(sim2, 8);
  CHECK(sim2.countdownLeft("Hold") == 18);  // paused at about two seconds in
  run(sim2, 5);
  CHECK(sim2.countdownLeft("Hold") <= 18);
  CHECK(sim2.countdownLeft("Hold") >= 16);
  run(sim2, 2);
  CHECK(sim2.countdowns().empty());
}

TEST_CASE("named units: spawned, found, hurt and killed by name; set pieces can't die until released") {
  Rules R;
  Map map(64, 64);
  Simulation sim(map, R.types, R.weapons, 2);
  S s;
  const int depot = s.area(20, 20, 24, 24), there = s.area(40, 20, 44, 24);
  const int spawned = s.on({cond(CondType::Always)},
                           {act(ActionType::SpawnNamed, 0, 1, "Tiger", depot, -1, R.t("ger_heavy_tank")),
                            act(ActionType::SetInvulnerable, ANY_PLAYER, 1, "Tiger")});
  sim.setScript(s.s, false);
  run(sim, 1);
  REQUIRE(sim.triggerFired(spawned));
  std::vector<Entity> tiger = sim.named("Tiger");
  REQUIRE(tiger.size() == 1);
  CHECK(sim.unitName(tiger[0]) == "Tiger");
  CHECK((sim.scriptFlags(tiger[0]) & kScriptInvulnerable));
  // Shot to pieces, it lives on.
  for (int i = 0; i < 50; ++i) sim.debugDamage(tiger[0], 100000);
  run(sim, 1);
  CHECK(alive(sim, tiger[0]));
  CHECK(sim.registry().get<CUnit>(tiger[0]).health == R.types.get(R.t("ger_heavy_tank")).maxHealth);

  // Conditions on it.
  Condition here = cond(CondType::NamedInArea, 0, depot, 1, "Tiger");
  Condition gone = cond(CondType::NamedInArea, 0, there, 1, "Tiger");
  ScenarioScript probe = s.s;
  probe.triggers.clear();
  Trigger t1;
  t1.conditions = {here, cond(CondType::NamedAlive, 0, -1, 0, "Tiger"), notc(gone), notc(cond(CondType::NamedDead, 0, -1, 0, "Tiger"))};
  t1.actions = {act(ActionType::SetCounter, ANY_PLAYER, 1, "seen")};
  probe.triggers.push_back(t1);
  // Half its health away, by name; then the hurt condition; then killed by name despite invulnerability.
  Trigger t2;
  t2.conditions = {cond(CondType::Timer, 0, -1, 1)};
  t2.actions = {act(ActionType::SetHealth, ANY_PLAYER, 40, "Tiger")};
  probe.triggers.push_back(t2);
  Trigger t3;
  t3.conditions = {cond(CondType::NamedHealthBelow, 0, -1, 50, "Tiger")};
  t3.actions = {act(ActionType::SetCounter, ANY_PLAYER, 1, "hurt"), act(ActionType::KillUnits, ANY_PLAYER, 0, "Tiger")};
  probe.triggers.push_back(t3);
  Trigger t4;
  t4.conditions = {cond(CondType::NamedDead, 0, -1, 0, "Tiger")};
  t4.actions = {act(ActionType::SetCounter, ANY_PLAYER, 1, "dead")};
  probe.triggers.push_back(t4);
  sim.setScript(probe, false);
  run(sim, 4);
  CHECK(sim.counter("seen") == 1);
  CHECK(sim.counter("hurt") == 1);
  CHECK(sim.counter("dead") == 1);
  CHECK_FALSE(alive(sim, tiger[0]));
  CHECK(sim.losses(0) == 1);
}

TEST_CASE("set pieces: untargetable units draw no fire, locked units ignore their player, the script still moves them") {
  Rules R;
  Map map(64, 64);
  Simulation sim(map, R.types, R.weapons, 3);
  const std::vector<Entity> riflemen = spawn(sim, R.t("rus_rifleman"), 1, row({30 * T, 30 * T}, 4));
  const std::vector<Entity> ghost = spawn(sim, R.t("ger_rifleman"), 0, row({33 * T, 30 * T}, 2));
  Command tag;
  tag.type = CommandType::TagUnits;
  tag.units = ghost;
  tag.text = "Ghosts";
  tag.count = kScriptUntargetable | kScriptLocked | kScriptInvulnerable;
  tag.amount = kScriptUntargetable | kScriptLocked;
  sim.queue(tag);
  Command hold;
  hold.type = CommandType::SetStance;
  hold.units = ghost;
  hold.stance = Stance::HoldFire;
  sim.queue(hold);
  run(sim, 10);
  for (Entity e : ghost) CHECK(alive(sim, e));
  CHECK(sim.named("Ghosts").size() == 2);
  // The player's order is ignored ...
  const Vec2i before = at(sim, ghost[0]);
  Command mv;
  mv.type = CommandType::Move;
  mv.player = 0;
  mv.units = ghost;
  mv.target = {10 * T, 10 * T};
  sim.queue(mv);
  run(sim, 5);
  CHECK(distance(at(sim, ghost[0]).x - before.x, at(sim, ghost[0]).y - before.y) < 64);
  // ... the script's isn't.
  S s;
  const int a = s.area(30, 28, 36, 32), b = s.area(4, 4, 8, 8);
  s.on({cond(CondType::Always)}, {act(ActionType::MoveUnits, ANY_PLAYER, 0, "Ghosts", a, b)});
  sim.setScript(s.s, false);
  run(sim, 30);
  CHECK(at(sim, ghost[0]).x < 25 * T);  // riflemen walk about half a tile a second
  // Released: targetable again, and the riflemen open up.
  S r;
  r.s.areas = s.s.areas;
  r.on({cond(CondType::Always)}, {act(ActionType::SetTargetable, ANY_PLAYER, 1, "Ghosts"), act(ActionType::SetLocked, ANY_PLAYER, 0, "Ghosts"),
                                  act(ActionType::MoveUnits, ANY_PLAYER, 0, "Ghosts", b, a)});
  sim.setScript(r.s, false);
  run(sim, 90);
  CHECK(sim.named("Ghosts").size() < 2);
  CHECK(sim.kills(1) >= 1);
}

TEST_CASE("the computer leaves a script's units alone, and obeys attack, defend and behaviour orders") {
  Rules R;
  Map map(128, 128);
  map.generateCountryside(7);
  Simulation sim(map, R.types, R.weapons, 4);
  const std::vector<Entity> army = spawn(sim, R.t("ger_rifleman"), 0, row({30 * T, 64 * T}, 12, 200));
  const std::vector<Entity> pieces = spawn(sim, R.t("ger_medium_tank"), 0, row({20 * T, 40 * T}, 2, 600));
  spawn(sim, R.t("rus_rifleman"), 1, row({100 * T, 64 * T}, 2));
  S s;
  const int target = s.area(96, 60, 104, 68), hold = s.area(40, 90, 46, 96), pieceArea = s.area(15, 35, 30, 45);
  s.on({cond(CondType::Always)}, {act(ActionType::SetAiControl, 0, 0, "", pieceArea), act(ActionType::AiBehaviour, 0, 0, "capture"),
                                  act(ActionType::AiBehaviour, 0, 0, "raid")});
  const int go = s.on({cond(CondType::Timer, 0, -1, 2)}, {act(ActionType::AiAttack, 0, 0, "", target)});
  const int stop = s.on({cond(CondType::Timer, 0, -1, 110)}, {act(ActionType::AiDefend, 0, 0, "", hold)});
  sim.setScript(s.s, false);
  computer(sim, 0);
  run(sim, 1);
  for (Entity e : pieces) CHECK((sim.scriptFlags(e) & kScriptNoAi));
  run(sim, 60);
  REQUIRE(sim.triggerFired(go));
  CHECK(sim.ai().player(0).order == 1);
  // The army set off for the target; the set pieces never moved.
  int forward = 0;
  for (Entity e : army)
    if (alive(sim, e) && at(sim, e).x > 45 * T) ++forward;
  CHECK(forward >= 6);
  for (size_t i = 0; i < pieces.size(); ++i) {
    CHECK(alive(sim, pieces[i]));
    CHECK(distance(at(sim, pieces[i]).x - (20 * T + static_cast<int>(i) * 600), at(sim, pieces[i]).y - 40 * T) < 300);
  }
  run(sim, 60);
  REQUIRE(sim.triggerFired(stop));
  CHECK(sim.ai().player(0).order == 2);
  run(sim, 120);
  int home = 0;
  for (Entity e : army)
    if (alive(sim, e) && distance(at(sim, e).x - 43 * T, at(sim, e).y - 93 * T) < 14 * T) ++home;
  CHECK(home >= 4);

  // Behaviour switches.
  S b;
  b.on({cond(CondType::Always)}, {act(ActionType::AiBehaviour, 0, 0, "attack"), act(ActionType::AiBehaviour, 0, 0, "train"),
                                  act(ActionType::AiWaveSize, 0, 50), act(ActionType::AiFree, 0)});
  sim.setScript(b.s, false);
  run(sim, 1);
  CHECK_FALSE(sim.ai().allowed(0, kAiAttack));
  CHECK_FALSE(sim.ai().allowed(0, kAiTrain));
  CHECK_FALSE(sim.ai().allowed(0, kAiRaid));
  CHECK(sim.ai().allowed(0, kAiScout));
  CHECK(sim.ai().player(0).waveScale == 50);
  CHECK(sim.ai().player(0).order == 0);
}

TEST_CASE("a play area keeps the player's orders and buildings inside it") {
  Rules R;
  Map map(96, 96);
  Simulation sim(map, R.types, R.weapons, 5);
  const std::vector<Entity> men = spawn(sim, R.t("ger_rifleman"), 0, row({20 * T, 20 * T}, 3));
  S s;
  const int box = s.area(10, 10, 40, 40);
  s.on({cond(CondType::Always)}, {act(ActionType::SetPlayArea, ANY_PLAYER, 0, "", box)});
  sim.setScript(s.s, false);
  run(sim, 1);
  CHECK(sim.playAreaLimited(0));
  CHECK(sim.inPlayArea(0, 20, 20));
  CHECK_FALSE(sim.inPlayArea(0, 60, 60));
  Command mv;
  mv.type = CommandType::Move;
  mv.player = 0;
  mv.units = men;
  mv.target = {80 * T, 80 * T};
  sim.queue(mv);
  run(sim, 60);
  for (Entity e : men) {
    CHECK(at(sim, e).x < 42 * T);
    CHECK(at(sim, e).y < 42 * T);
  }
  // The whole map again.
  S w;
  w.on({cond(CondType::Always)}, {act(ActionType::SetPlayArea, ANY_PLAYER, 0, "", -1)});
  sim.setScript(w.s, false);
  run(sim, 1);
  CHECK_FALSE(sim.playAreaLimited(0));
}

TEST_CASE("counters, flags, any-of triggers, negation, firing and resetting triggers") {
  Rules R;
  Map map(64, 64);
  Simulation sim(map, R.types, R.weapons, 6);
  sim.setFlags({{"tiger_survived", 1}, {"veterans", 3}});
  S s;
  const int a = s.area(10, 10, 14, 14);
  s.on({cond(CondType::Timer, 0, -1, 1)}, {act(ActionType::AddCounter, ANY_PLAYER, 2, "bridges"), act(ActionType::AddFlag, ANY_PLAYER, 1, "veterans")}, true);
  const int three = s.on({cond(CondType::CounterAtLeast, 0, -1, 6, "bridges")}, {act(ActionType::SetCounter, ANY_PLAYER, 1, "enough")});
  const int either = s.on({cond(CondType::FlagAtLeast, 0, -1, 9, "nothing"), cond(CondType::FlagAtLeast, 0, -1, 1, "tiger_survived")},
                          {act(ActionType::SpawnFromFlag, 0, 0, "veterans", a, -1, R.t("ger_rifleman"))}, false, true);
  const int never = s.on({notc(cond(CondType::Always))}, {act(ActionType::SetCounter, ANY_PLAYER, 1, "never")});
  const int later = s.on({cond(CondType::Timer, 0, -1, 9999)}, {act(ActionType::AddCounter, ANY_PLAYER, 10, "fired")});
  Action fire = act(ActionType::FireTrigger);
  fire.trigger = later;
  Action reset = act(ActionType::ResetTrigger);
  reset.trigger = three;
  s.on({cond(CondType::CounterAtLeast, 0, -1, 1, "enough")}, {fire, reset, act(ActionType::SetCounter, ANY_PLAYER, 0, "enough")});
  sim.setScript(s.s, false);
  run(sim, 1);
  CHECK(sim.triggerFired(either));
  CHECK(sim.countUnits(0, a, R.t("ger_rifleman")) == 3);  // as many as the flag said
  run(sim, 4);
  CHECK(sim.triggerFired(three));
  CHECK_FALSE(sim.triggerFired(never));
  CHECK(sim.counter("fired") == 10);
  CHECK(sim.flag("veterans") >= 5);
}

TEST_CASE("alliances change mid-mission; spawns scale with difficulty; logistics points and income rate") {
  Rules R;
  Map map(64, 64);
  Simulation sim(map, R.types, R.weapons, 7);
  sim.setMissionDifficulty(2);
  S s;
  const int a = s.area(10, 10, 16, 16), b = s.area(40, 40, 44, 44);
  Action ally = act(ActionType::SetAlliance, 2, 1);
  ally.player2 = 0;
  Action spawnScaled = act(ActionType::SpawnUnits, 1, 10, "", a, -1, R.t("rus_rifleman"));
  spawnScaled.scaled = true;
  s.on({cond(CondType::Always)}, {ally, spawnScaled, act(ActionType::AddLogisticsPoint, ANY_PLAYER, 0, "", b),
                                  act(ActionType::SetIncomeRate, 0, 50)});
  const int allied = s.on({cond(CondType::AlliedWith, 2, -1, 0)}, {act(ActionType::SetCounter, ANY_PLAYER, 1, "friends")});
  Action betray = act(ActionType::SetAlliance, 2, 0);
  betray.player2 = 0;
  s.on({cond(CondType::Timer, 0, -1, 2)}, {betray});
  sim.setScript(s.s, false);
  const size_t points = map.supplyPoints.size();
  run(sim, 1);
  CHECK(sim.allied(2, 0));
  CHECK(sim.triggerFired(allied));
  CHECK(sim.countUnits(1, a, R.t("rus_rifleman")) == 13);  // 10 at 135%
  CHECK(map.supplyPoints.size() == points + 1);
  CHECK(sim.pointState(points).side == 0xFF);
  CHECK(sim.incomeRate(0) == 50);
  run(sim, 2);
  CHECK_FALSE(sim.allied(2, 0));
  CHECK_FALSE(sim.allied(2, 1));
}

TEST_CASE("barrage, garrison, evacuate, patrol and the tallies") {
  Rules R;
  Map map(96, 96);
  Simulation sim(map, R.types, R.weapons, 8);
  const std::vector<Entity> target = spawn(sim, R.t("rus_rifleman"), 1, row({60 * T, 60 * T}, 6, 120));
  const Entity house = place(sim, R.t("ger_bunker"), 0, {20 * T, 20 * T});
  REQUIRE(house.valid());
  const std::vector<Entity> men = spawn(sim, R.t("ger_rifleman"), 0, row({24 * T, 24 * T}, 3));
  const std::vector<Entity> walkers = spawn(sim, R.t("ger_scout_car"), 0, {{70 * T, 20 * T}});
  S s;
  const int shelled = s.area(58, 58, 62, 62), bunker = s.area(17, 17, 23, 23), menArea = s.area(22, 22, 28, 26);
  const int p1 = s.area(68, 18, 72, 22), p2 = s.area(84, 18, 88, 22);
  s.on({cond(CondType::Always)}, {act(ActionType::Barrage, 0, 60, "howitzer_105", shelled),
                                  act(ActionType::Garrison, 0, 0, "", menArea, bunker),
                                  act(ActionType::Patrol, 0, 0, "", p1, p2)});
  const int out = s.on({cond(CondType::Timer, 0, -1, 25)}, {act(ActionType::Evacuate, 0, 0, "", bunker)});
  sim.setScript(s.s, false);
  bool garrisoned = false, sawFar = false, cameBack = false;
  for (int i = 0; i < 60 * TICKS_PER_SECOND; ++i) {
    sim.tick();
    if (i == 20 * TICKS_PER_SECOND)
      for (Entity e : men) garrisoned |= sim.registry().has<CGarrisoned>(e);
    if (alive(sim, walkers[0])) {
      const Vec2i w = at(sim, walkers[0]);
      if (w.x > 82 * T) sawFar = true;
      if (sawFar && w.x < 74 * T) cameBack = true;
    }
  }
  CHECK(garrisoned);
  CHECK(sim.triggerFired(out));
  for (Entity e : men) CHECK_FALSE(sim.registry().has<CGarrisoned>(e));
  CHECK(sawFar);
  CHECK(cameBack);
  int dead = 0;
  for (Entity e : target) dead += !alive(sim, e);
  CHECK(dead >= 3);
  CHECK(sim.losses(1) == dead);
}

TEST_CASE("structures, area control, objectives, chance and health conditions") {
  Rules R;
  Map map(96, 96);
  Simulation sim(map, R.types, R.weapons, 9);
  place(sim, R.t("rus_bunker"), 1, {40 * T, 40 * T});
  spawn(sim, R.t("ger_rifleman"), 0, row({60 * T, 60 * T}, 4));
  S s;
  const int bunkerArea = s.area(36, 36, 44, 44), field = s.area(56, 56, 66, 64);
  Action obj = act(ActionType::AddObjective, 0, 0, "Take the field", field);
  s.on({cond(CondType::Always)}, {obj, act(ActionType::AddObjective, 0, 1, "Optional")});
  s.on({cond(CondType::StructuresAtLeast, 1, bunkerArea, 1), cond(CondType::StructuresAtMost, 1, bunkerArea, 1),
        cond(CondType::AreaControlled, 0, field, 4), notc(cond(CondType::AreaControlled, 1, field))},
       {act(ActionType::CompleteObjective, ANY_PLAYER, 0), act(ActionType::HideObjective, ANY_PLAYER, 1)});
  const int done = s.on({cond(CondType::ObjectiveDone, 0, -1, 0)}, {act(ActionType::SetCounter, ANY_PLAYER, 1, "won")});
  const int hurt = s.on({cond(CondType::HealthBelow, 0, field, 100)}, {act(ActionType::SetCounter, ANY_PLAYER, 1, "hurt")});
  s.on({cond(CondType::Chance, 0, -1, 50)}, {act(ActionType::AddCounter, ANY_PLAYER, 1, "coin")}, true);
  sim.setScript(s.s, false);
  run(sim, 2);
  REQUIRE(sim.objectives().size() == 2);
  CHECK(sim.objectives()[0].state == 1);
  CHECK(sim.objectives()[0].area == field);
  CHECK(sim.objectives()[1].state == 3);
  CHECK(sim.triggerFired(done));
  CHECK_FALSE(sim.triggerFired(hurt));  // at full health
  run(sim, 60);
  const int coins = sim.counter("coin");
  CHECK(coins > 5);
  CHECK(coins < 55);
}

TEST_CASE("a mission saved in the middle continues exactly, with its countdowns, names, flags and orders") {
  Rules R;
  Map map(128, 128);
  map.generateCountryside(11);
  Map map2 = map;
  Simulation sim(map, R.types, R.weapons, 10);
  spawn(sim, R.t("ger_rifleman"), 0, row({30 * T, 64 * T}, 10, 200));
  spawn(sim, R.t("rus_rifleman"), 1, row({90 * T, 64 * T}, 8, 200));
  S s;
  const int front = s.area(60, 58, 70, 70), box = s.area(5, 5, 120, 120), p1 = s.area(28, 60, 32, 68), p2 = s.area(40, 60, 44, 68);
  s.on({cond(CondType::Always)}, {act(ActionType::Countdown, 0, 300, "Relief"), act(ActionType::NameUnits, 0, 3, "Vanguard", p1),
                                  act(ActionType::SetInvulnerable, ANY_PLAYER, 1, "Vanguard"), act(ActionType::SetPlayArea, 0, 0, "", box),
                                  act(ActionType::MarkArea, 0, 0, "Front", front), act(ActionType::AiAttack, 1, 0, "", p1),
                                  act(ActionType::AiBehaviour, 1, 0, "raid"), act(ActionType::SetCounter, ANY_PLAYER, 5, "c"),
                                  act(ActionType::Patrol, 0, 0, "Vanguard", p1, p2)});
  sim.setScript(s.s, false);
  computer(sim, 1);
  run(sim, 20);
  ByteWriter w;
  sim.saveState(w);
  Simulation copy(map2, R.types, R.weapons, 99);
  copy.setScript(s.s, false);
  ByteReader r(w.bytes());
  std::string err;
  REQUIRE(copy.loadState(r, err));
  CHECK(copy.stateHash() == sim.stateHash());
  CHECK(copy.countdownLeft("Relief") == sim.countdownLeft("Relief"));
  CHECK(copy.named("Vanguard").size() == 3);
  CHECK(copy.markers().size() == 1);
  CHECK(copy.counter("c") == 5);
  CHECK(copy.playAreaLimited(0));
  CHECK(copy.ai().player(1).order == 1);
  CHECK_FALSE(copy.ai().allowed(1, kAiRaid));
  CHECK(copy.patrols().size() == 1);
  for (int i = 0; i < 40 * TICKS_PER_SECOND; ++i) {
    sim.tick();
    copy.tick();
  }
  CHECK(copy.stateHash() == sim.stateHash());
}

TEST_CASE("maps needn't be square: every landscape generates long and wide, and units keep their names in the file") {
  for (int layout = 0; layout < static_cast<int>(MapLayout::Count); ++layout)
    for (auto [w, h] : {std::pair{96, 288}, std::pair{320, 96}}) {
      Map m(w, h);
      m.generate(static_cast<Biome>(layout % 4), 1946 + static_cast<uint64_t>(layout), layout, true);
      CHECK(m.width() == w);
      CHECK(m.height() == h);
      for (const Vec2i& p : m.supplyPoints) CHECK(m.inBounds(p.x, p.y));
      for (const Map::Lot& l : m.lots) CHECK(m.inBounds(l.tx, l.ty));
    }
  Map m(64, 192);
  std::vector<UnitPlacement> units{{"ger_heavy_tank", 0, 1000, 2000, 0, -1, "Tiger", kScriptInvulnerable | kScriptNoAi, 1}};
  const std::vector<uint8_t> bytes = encodeMap(m, units, {}, "");
  MapFile f;
  std::string err;
  REQUIRE(decodeMap(bytes, f, err));
  CHECK(f.map.width() == 64);
  CHECK(f.map.height() == 192);
  REQUIRE(f.units.size() == 1);
  CHECK(f.units[0].name == "Tiger");
  CHECK(f.units[0].flags == (kScriptInvulnerable | kScriptNoAi));
  CHECK(f.units[0].stance == 1);
}

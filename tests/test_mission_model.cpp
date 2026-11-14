// Pioneer part 4: the mission model - phases, goals, waves and paths as data
// the simulation runs.
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "pioneer/map/map.hpp"
#include "pioneer/sim/determinism.hpp"
#include "pioneer/sim/script_registry.hpp"
#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

namespace {
constexpr int T = 256;
Vec2i tc(int x, int y) { return {x * T + 128, y * T + 128}; }

UnitTypeRegistry makeTypes() {
  UnitTypeRegistry types;
  std::string err;
  REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[
    {"id":"rifle","model":"rifleman","speed":40,"turnTicks":0,"radius":48,"locomotor":"foot"},
    {"id":"tank","model":"tank","speed":30,"turnTicks":2,"radius":110}]})"), err));
  return types;
}

Condition cond(CondType t, uint8_t player = 0, int area = -1, int value = 0) {
  Condition c = script::makeCondition(t);
  c.player = player;
  c.area = area;
  c.value = value;
  return c;
}
Action act(ActionType t, int value = 0, const std::string& text = {}) {
  Action a = script::makeAction(t);
  a.value = value;
  a.text = text;
  return a;
}

// "Take the bridge, hold it against the raiders, mop up" - or lose the bridge.
ScenarioScript bridgeMission() {
  ScenarioScript s;
  s.areas = {{"Start", 4, 4, 10, 10}, {"Bridge", 30, 28, 34, 34}, {"North", 50, 4, 56, 10}};
  s.paths.push_back({"Raiders' road", {{53, 20}, {40, 30}, {32, 31}}, false});

  MissionPhase approach;
  approach.name = "Approach";
  Action hero = act(ActionType::NameUnits, 1, "Hero");
  hero.player = 0;
  hero.area = 0;
  approach.onEnter = {hero};
  MissionPhase hold;
  hold.name = "Hold the bridge";
  hold.enter = {cond(CondType::UnitsInArea, 0, 1, 1)};
  hold.onEnter = {act(ActionType::SendWave, 0)};
  hold.music = "battle";
  MissionPhase mop;
  mop.name = "Mop up";  // only by Go to phase
  MissionPhase lost;
  lost.name = "The bridge is lost";
  lost.from = 1;
  lost.enter = {cond(CondType::NoUnitsInArea, 0, 1), cond(CondType::Timer, 0, -1, 2)};
  lost.onEnter = {[] {
    Action d = act(ActionType::Defeat);
    d.player = 0;
    return d;
  }()};
  s.phases = {approach, hold, mop, lost};

  MissionWave w;
  w.name = "Raiders";
  w.player = 1;
  w.area = 2;
  w.units = {{0, {1, 2, 3}}};
  w.delay = 2;
  w.times = 2;
  w.interval = 6;
  w.order = MissionWave::AlongPath;
  w.path = 0;
  w.unitName = "Raiders";
  w.stance = static_cast<int32_t>(Stance::HoldFire);
  s.waves = {w};

  MissionGoal reach;
  reach.type = "reach";
  reach.text = "Reach the bridge";
  reach.area = 1;
  reach.phase = 0;
  MissionGoal holdIt;
  holdIt.type = "hold";
  holdIt.text = "Hold the bridge for forty seconds";
  holdIt.area = 1;
  holdIt.value = 40;
  holdIt.phase = 1;
  holdIt.onDone = {act(ActionType::GotoPhase, 2)};
  MissionGoal protect;
  protect.type = "protect";
  protect.text = "Keep the hero alive";
  protect.unit = "Hero";
  protect.phase = 1;
  protect.optional = true;
  s.goals = {reach, holdIt, protect};

  Trigger inHold;
  inHold.name = "Three seconds into the defence";
  inHold.phase = 1;
  inHold.conditions = {cond(CondType::PhaseElapsed, 0, -1, 3)};
  inHold.actions = {act(ActionType::AddCounter, 1, "defence")};
  Trigger won;
  won.name = "Won";
  won.phase = 2;
  won.conditions = {cond(CondType::GoalDone, 0, -1, 1)};
  Action v = act(ActionType::Victory);
  v.player = 0;
  won.actions = {v};
  Trigger early;
  early.name = "Only in the approach";
  early.phase = 0;
  early.repeat = true;
  early.conditions = {cond(CondType::Always)};
  early.actions = {act(ActionType::AddCounter, 1, "approach")};
  s.triggers = {inHold, won, early};
  return s;
}

std::vector<Entity> ownedBy(Simulation& sim, uint8_t p) {
  std::vector<Entity> out;
  for (uint32_t i : sim.registry().pool<CUnit>().entities())
    if (sim.registry().pool<CUnit>().get(i).owner == p) out.push_back(sim.registry().handle(i));
  return out;
}
void teleport(Simulation& sim, const std::vector<Entity>& units, Vec2i to) {
  Command c;
  c.type = CommandType::Edit;
  int i = 0;
  for (Entity e : units) {
    UnitEdit ed;
    ed.unit = e;
    ed.move = true;
    ed.pos = {to.x + (i % 3) * 200, to.y + (i / 3) * 200};
    ++i;
    c.edits.push_back(ed);
  }
  sim.queue(c);
}
void run(Simulation& sim, int seconds) {
  for (int i = 0; i < seconds * 15; ++i) sim.tick();
}
}  // namespace

TEST_CASE("mission model: phases, paths, waves, goals and sequences go to the map file and back") {
  UnitTypeRegistry types = makeTypes();
  ScenarioScript s = bridgeMission();
  MissionSequence q;
  q.name = "Opening";
  q.events.push_back({0, "camera", "pan", {{"to", "Bridge"}, {"seconds", "6"}}});
  q.events.push_back({20, "dialogue", "radio", {{"text", "Oster: Take the bridge."}}});
  s.sequences = {q};
  const nlohmann::json j = scriptToJson(s, &types);
  const ScenarioScript back = scriptFromJson(j, &types);
  CHECK(scriptToJson(back, &types) == j);
  REQUIRE(back.phases.size() == 4);
  CHECK(back.phases[1].music == "battle");
  CHECK(back.phases[3].from == 1);
  CHECK(back.phases[3].enter.size() == 2);
  REQUIRE(back.waves.size() == 1);
  CHECK(back.waves[0].units[0].count == std::array<int32_t, 3>{1, 2, 3});
  CHECK(back.waves[0].order == MissionWave::AlongPath);
  CHECK(back.paths[0].points.size() == 3);
  CHECK(back.goals[1].onDone.size() == 1);
  CHECK(back.triggers[0].phase == 1);
  CHECK(back.sequences[0].events[1].params[0].second == "Oster: Take the bridge.");
  // A script without the model is written as it always was.
  ScenarioScript plain;
  plain.areas = s.areas;
  plain.triggers = {s.triggers[2]};
  plain.triggers[0].phase = -1;
  const nlohmann::json pj = scriptToJson(plain, &types);
  CHECK_FALSE(pj.contains("phases"));
  CHECK_FALSE(pj["triggers"][0].contains("phase"));
  // And it checks out.
  bool errors = false;
  for (const script::Issue& i : script::check(s, {}))
    if (i.severity == script::Issue::Error) {
      errors = true;
      MESSAGE(i.text);
    }
  CHECK_FALSE(errors);
}

TEST_CASE("mission model: a mission played through its phases, with a wave on a path and goals keeping score") {
  UnitTypeRegistry types = makeTypes();
  WeaponRegistry weapons;
  Map map(64, 64, 100);
  Simulation sim(map, types, weapons, 1);
  sim.setMissionDifficulty(2);  // hard: three raiders at a time
  sim.setScript(bridgeMission(), false);
  Command s;
  s.type = CommandType::Spawn;
  s.player = 0;
  s.unitType = 0;
  s.count = 3;
  s.target = tc(7, 7);
  sim.queue(s);
  CHECK(sim.missionPhase() == -1);
  run(sim, 2);
  // Phase 0: its goal is on the list, its trigger runs, the others don't.
  CHECK(sim.missionPhase() == 0);
  REQUIRE(sim.objectives().size() == 1);
  CHECK(sim.objectives()[0].text == "Reach the bridge");
  CHECK(sim.goalState(0) == 1);
  CHECK(sim.goalState(1) == 0);
  CHECK(sim.named("Hero").size() == 1);
  const int32_t approach = sim.counter("approach");
  CHECK(approach >= 1);
  CHECK(sim.counter("defence") == 0);

  // The player reaches the bridge: the goal is done, the next phase begins and sends for the raiders.
  teleport(sim, ownedBy(sim, 0), tc(31, 30));
  run(sim, 1);
  CHECK(sim.goalState(0) == 2);
  CHECK(sim.objectives()[0].state == 1);
  CHECK(sim.missionPhase() == 1);
  CHECK(sim.wavesPending() == 1);
  CHECK(sim.objectives().size() == 3);  // hold and protect joined the list
  bool announced = false;
  run(sim, 1);
  CHECK(sim.counter("approach") <= approach + 2);  // phase 0's trigger has stopped
  run(sim, 2);
  CHECK(sim.counter("defence") == 1);
  // The first raiders arrive in the north (three, on hard), holding fire, and take the road.
  std::vector<Entity> raiders = sim.named("Raiders");
  REQUIRE(raiders.size() == 3);
  CHECK(sim.registry().get<CUnit>(raiders[0]).owner == 1);
  CHECK(sim.registry().get<CUnit>(raiders[0]).stance == Stance::HoldFire);
  CHECK(sim.routesRunning() == 1);
  (void)announced;

  // Saved here, with a wave still to come and men on the road: the loaded copy plays out the same.
  {
    ByteWriter w;
    sim.saveState(w);
    Map copy = map;
    Simulation loaded(copy, types, weapons, 9);
    ByteReader r(w.bytes());
    std::string err;
    REQUIRE_MESSAGE(loaded.loadState(r, err), err);
    CHECK(loaded.missionPhase() == 1);
    CHECK(loaded.wavesPending() == 1);
    CHECK(loaded.routesRunning() == 1);
    CHECK(loaded.script().phases.size() == 4);
    for (int i = 0; i < 15 * 8; ++i) {
      sim.tick();
      loaded.tick();
    }
    CHECK(loaded.stateHash() == sim.stateHash());
    CHECK(Simulation::digestDifference(loaded.digest(), sim.digest()).empty());
  }
  // The second lot has come (six seconds after the first); all walk the path to the bridge.
  CHECK(sim.named("Raiders").size() == 6);
  CHECK(sim.wavesPending() == 0);
  run(sim, 22);
  int atBridge = 0;
  for (Entity e : sim.named("Raiders")) {
    const CPosition& p = sim.registry().get<CPosition>(e);
    atBridge += std::abs((p.x >> 8) - 32) <= 4 && std::abs((p.y >> 8) - 31) <= 4;
  }
  CHECK(atBridge >= 5);
  CHECK(sim.routesRunning() == 0);  // the road ends there
  // Raiders on the bridge: it is not held. The hold goal waits.
  CHECK(sim.goalState(1) == 1);
  CHECK(sim.missionPhase() == 1);
  // The raiders are destroyed; forty seconds later (counted afresh) the bridge counts as held.
  Command kill;
  kill.type = CommandType::Kill;
  kill.units = sim.named("Raiders");
  sim.queue(kill);
  run(sim, 30);
  CHECK(sim.goalState(1) == 1);
  run(sim, 12);
  CHECK(sim.goalState(1) == 2);
  // Its on-done action moved the mission on; the protect goal of the phase left is met; the last trigger wins it.
  CHECK(sim.missionPhase() == 2);
  CHECK(sim.goalState(2) == 2);
  run(sim, 1);
  CHECK(sim.playerState(0).won);
  CHECK_FALSE(sim.playerState(0).defeated);
}

TEST_CASE("mission model: the branch - the bridge is lost") {
  UnitTypeRegistry types = makeTypes();
  WeaponRegistry weapons;
  Map map(64, 64, 100);
  Simulation sim(map, types, weapons, 1);
  sim.setScript(bridgeMission(), false);
  Command s;
  s.type = CommandType::Spawn;
  s.player = 0;
  s.unitType = 0;
  s.count = 3;
  s.target = tc(7, 7);
  sim.queue(s);
  run(sim, 1);
  teleport(sim, ownedBy(sim, 0), tc(31, 30));
  run(sim, 1);
  REQUIRE(sim.missionPhase() == 1);
  // The player's men leave the bridge (all but the hero are lost, he runs).
  std::vector<Entity> men = ownedBy(sim, 0);
  const Entity hero = sim.named("Hero").front();
  Command kill;
  kill.type = CommandType::Kill;
  for (Entity e : men)
    if (!(e == hero)) kill.units.push_back(e);
  sim.queue(kill);
  teleport(sim, {hero}, tc(7, 7));
  run(sim, 1);
  CHECK(sim.missionPhase() == 1);  // "Timer 2" in a phase's conditions counts from when the current phase began
  run(sim, 3);
  CHECK(sim.missionPhase() == 3);
  CHECK(sim.playerState(0).defeated);
  CHECK(sim.goalState(1) == 1);  // never held
}

TEST_CASE("mission model: the checker knows the model") {
  ScenarioScript s = bridgeMission();
  s.waves[0].area = 9;            // no such area
  s.waves[0].path = 4;            // no such path
  s.goals[2].unit = "Nobody";     // never named
  s.goals.push_back({});
  s.goals.back().type = "conquer";  // no such kind
  s.phases[2].enter.clear();
  s.goals[1].onDone.clear();      // now nothing leads to "Mop up"
  s.paths.push_back({"Stub", {{5, 5}}, false});
  s.triggers[1].conditions[0].value = 12;  // goal 12
  const std::vector<script::Issue> issues = script::check(s, {});
  auto has = [&](const char* part) {
    for (const script::Issue& i : issues)
      if (i.text.find(part) != std::string::npos) return true;
    return false;
  };
  CHECK(has("wave 'Raiders': no area to arrive in"));
  CHECK(has("wave 'Raiders': no path to follow"));
  CHECK(has("nothing is ever named 'Nobody'"));
  CHECK(has("'conquer' is not a kind of goal"));
  CHECK(has("phase 'Mop up': it has no conditions to begin on and no Go to phase action leads to it"));
  CHECK(has("path 'Stub': a path needs at least two points"));
  CHECK(has("the mission has no goal 12"));
  CHECK(script::goalTypes().size() == 7);
  for (size_t i = 0; i < script::goalTypes().size(); ++i) CHECK(std::string(script::goalTypes()[i].id) == kGoalTypes[i]);
}

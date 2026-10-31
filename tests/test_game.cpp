// Whole-game tests with the real rules: saving and loading, replays, scripted
// triggers and the computer player.
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "slave/sim/savefile.hpp"
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

// Places a structure on the nearest clear ground.
void place(Simulation& sim, uint16_t type, uint8_t owner, Vec2i at) {
  for (int r = 0; r < 12; ++r)
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
        const Vec2i p{at.x + dx * 256, at.y + dy * 256};
        if (!sim.canPlace(type, sim.footprintOrigin(type, p))) continue;
        Command c;
        c.type = CommandType::SpawnList;
        c.spawns = {{type, owner, p, 0, -1}};
        sim.queue(c);
        sim.tick();
        return;
      }
}

void base(Simulation& sim, const Rules& R, uint8_t owner, const char* f, Vec2i at) {
  const std::string F = f;
  place(sim, R.t((F + "_yard").c_str()), owner, at);
  place(sim, R.t((F + "_power").c_str()), owner, {at.x - 5 * 256, at.y - 4 * 256});
  {  // a supply depot on the nearest supply point
    Vec2i best = sim.map().supplyPoints.front();
    for (const Vec2i& sp : sim.map().supplyPoints)
      if (std::abs(sp.x * 256 - at.x) + std::abs(sp.y * 256 - at.y) < std::abs(best.x * 256 - at.x) + std::abs(best.y * 256 - at.y))
        best = sp;
    place(sim, R.t((F + "_depot").c_str()), owner, {best.x * 256 + 128, best.y * 256 + 128});
  }
  place(sim, R.t((F + "_barracks").c_str()), owner, {at.x + 5 * 256, at.y + 3 * 256});
  Command c;
  c.type = CommandType::SetCredits;
  c.player = owner;
  c.amount = 6000;
  sim.queue(c);
}

void skirmishSetup(Simulation& sim, const Rules& R, bool ai) {
  const int32_t T = 256;
  base(sim, R, 0, "ger", {22 * T, 64 * T});
  base(sim, R, 1, "rus", {106 * T, 64 * T});
  Command s;
  s.type = CommandType::Spawn;
  s.player = 0;
  s.unitType = R.t("ger_medium_tank");
  s.count = 4;
  s.target = {40 * T, 64 * T};
  sim.queue(s);
  s.player = 1;
  s.unitType = R.t("rus_rifleman");
  s.count = 12;
  s.target = {88 * T, 64 * T};
  sim.queue(s);
  if (ai)
    for (uint8_t p : {uint8_t{0}, uint8_t{1}}) {
      Command c;
      c.type = CommandType::SetController;
      c.player = p;
      c.amount = 2;
      sim.queue(c);
    }
  sim.tick();
}
}  // namespace

TEST_CASE("commands survive a binary round trip") {
  Command c;
  c.type = CommandType::PlaceWalls;
  c.player = 3;
  c.target = {123, 456};
  c.units = {{7, 2}, {9, 0}};
  c.spawns = {{4, 3, {10, 20}, 512, 77}};
  c.edits.push_back({});
  c.edits[0].unit = {5, 1};
  c.edits[0].health = 33;
  c.formation = Formation::Wedge;
  c.formFacing = 1024;
  c.forced = true;
  c.amount = -2;
  ByteWriter w;
  writeCommand(w, c);
  ByteReader r(w.bytes());
  const Command d = readCommand(r);
  CHECK(r.ok());
  CHECK(r.atEnd());
  CHECK(d.type == c.type);
  CHECK(d.player == 3);
  CHECK(d.target == c.target);
  CHECK(d.units.size() == 2);
  CHECK(d.units[1].index == 9);
  CHECK(d.spawns[0].health == 77);
  CHECK(d.edits[0].health == 33);
  CHECK(d.formation == Formation::Wedge);
  CHECK(d.forced);
  CHECK(d.amount == -2);
  // A truncated command is reported, not read past the end.
  ByteReader cut(w.bytes().data(), w.bytes().size() / 2);
  readCommand(cut);
  CHECK_FALSE(cut.ok());
}

TEST_CASE("a saved game continues exactly as the original would") {
  Rules R;
  Map map(128, 128);
  map.generateCountryside(1940);
  Simulation sim(map, R.types, R.weapons, 99);
  skirmishSetup(sim, R, true);
  for (int i = 0; i < 15 * 90; ++i) sim.tick();  // a minute and a half of an AI-vs-AI game

  ByteWriter w;
  sim.saveState(w);
  Map copy = map;  // the save file carries the terrain (and oil) alongside the state
  Simulation loaded(copy, R.types, R.weapons, 12345);  // different seed: the state must override it
  ByteReader r(w.bytes());
  std::string err;
  REQUIRE(loaded.loadState(r, err));
  CHECK(r.atEnd());
  CHECK(loaded.stateHash() == sim.stateHash());
  for (int i = 0; i < 15 * 60; ++i) {
    sim.tick();
    loaded.tick();
  }
  CHECK(loaded.stateHash() == sim.stateHash());
  CHECK(loaded.unitCount() == sim.unitCount());
  CHECK(loaded.playerState(1).credits == sim.playerState(1).credits);

  // Damaged data is refused cleanly.
  std::vector<uint8_t> bad(w.bytes().begin(), w.bytes().begin() + static_cast<long>(w.bytes().size() / 3));
  Map copy2 = map;
  Simulation broken(copy2, R.types, R.weapons, 1);
  ByteReader br(bad);
  CHECK_FALSE(broken.loadState(br, err));
}

TEST_CASE("a replay (start state + logged commands) reproduces the game") {
  Rules R;
  Map map(128, 128);
  map.generateCountryside(7);
  Simulation sim(map, R.types, R.weapons, 5);
  skirmishSetup(sim, R, false);
  ByteWriter start;
  sim.saveState(start);
  Map startMap = map;
  sim.startRecording();
  // A human player's orders over a while.
  for (int i = 0; i < 15 * 40; ++i) {
    if (i == 10) {
      Command p;
      p.type = CommandType::Produce;
      p.player = 0;
      p.unitType = R.t("ger_rifleman");
      p.amount = 5;
      sim.queue(p);
    }
    if (i == 100) {
      Command m;
      m.type = CommandType::AttackMove;
      m.player = 0;
      for (uint32_t e : sim.registry().pool<CUnit>().entities())
        if (sim.registry().pool<CUnit>().get(e).owner == 0 && !sim.registry().pool<CStructure>().has(e))
          m.units.push_back(sim.registry().handle(e));
      m.target = {80 * 256, 64 * 256};
      sim.queue(m);
    }
    sim.tick();
  }
  const auto log = sim.commandLog();
  CHECK(log.size() == 2);

  Simulation replay(startMap, R.types, R.weapons, 1);
  ByteReader r(start.bytes());
  std::string err;
  REQUIRE(replay.loadState(r, err));
  size_t next = 0;
  while (replay.tickCount() < sim.tickCount()) {
    while (next < log.size() && log[next].tick == replay.tickCount()) replay.queue(log[next++].cmd);
    replay.tick();
  }
  CHECK(replay.stateHash() == sim.stateHash());
}

TEST_CASE("scenario triggers: areas, timers, messages, objectives, spawns and victory") {
  Rules R;
  Map map(64, 64, 300);
  Simulation sim(map, R.types, R.weapons, 3);
  ScenarioScript s;
  s.areas.push_back({"Bridge", 30, 30, 34, 34});
  s.areas.push_back({"Reinforcements", 5, 5, 7, 7});
  // 0: at the start, give the briefing and an objective.
  Trigger brief;
  brief.name = "Briefing";
  brief.conditions.push_back({CondType::Always});
  Action msg;
  msg.type = ActionType::Message;
  msg.text = "Take the bridge.";
  brief.actions.push_back(msg);
  Action obj;
  obj.type = ActionType::AddObjective;
  obj.player = 0;
  obj.text = "Hold the bridge with 3 units";
  brief.actions.push_back(obj);
  s.triggers.push_back(brief);
  // 1: after 10 seconds, reinforcements.
  Trigger reinf;
  reinf.name = "Reinforcements";
  Condition timer;
  timer.type = CondType::Timer;
  timer.value = 10;
  reinf.conditions.push_back(timer);
  Action spawn;
  spawn.type = ActionType::SpawnUnits;
  spawn.player = 0;
  spawn.area = 1;
  spawn.unitType = R.t("ger_rifleman");
  spawn.value = 4;
  reinf.actions.push_back(spawn);
  s.triggers.push_back(reinf);
  // 2: three of player 0's units on the bridge: objective done, victory.
  Trigger win;
  win.name = "Win";
  Condition there;
  there.type = CondType::UnitsInArea;
  there.player = 0;
  there.area = 0;
  there.value = 3;
  win.conditions.push_back(there);
  Action done;
  done.type = ActionType::CompleteObjective;
  done.value = 0;
  win.actions.push_back(done);
  Action victory;
  victory.type = ActionType::Victory;
  victory.player = 0;
  win.actions.push_back(victory);
  s.triggers.push_back(win);

  // The script survives the trip through JSON (unit types by id).
  const nlohmann::json j = scriptToJson(s, &R.types);
  const ScenarioScript back = scriptFromJson(j, &R.types);
  REQUIRE(back.triggers.size() == 3);
  CHECK(back.triggers[1].actions[0].unitType == R.t("ger_rifleman"));
  CHECK(back.areas[0].x1 == 34);
  sim.setScript(back, false);

  bool sawMessage = false, won = false;
  for (int i = 0; i < 15 * 12; ++i) {
    sim.tick();
    for (const SimEvent& ev : sim.events()) {
      if (ev.type == EventType::ScriptMessage && ev.text == "Take the bridge.") sawMessage = true;
      if (ev.type == EventType::PlayerWon) won = true;
    }
  }
  CHECK(sawMessage);
  REQUIRE(sim.objectives().size() == 1);
  CHECK(sim.objectives()[0].state == 0);
  CHECK(sim.countUnits(0, 1, -1) == 4);  // reinforcements arrived
  CHECK_FALSE(won);
  // March them onto the bridge.
  Command m;
  m.type = CommandType::Move;
  for (uint32_t e : sim.registry().pool<CUnit>().entities()) m.units.push_back(sim.registry().handle(e));
  m.target = {32 * 256, 32 * 256};
  sim.queue(m);
  for (int i = 0; i < 15 * 90 && !won; ++i) {
    sim.tick();
    for (const SimEvent& ev : sim.events()) won |= ev.type == EventType::PlayerWon && ev.owner == 0;
  }
  CHECK(won);
  CHECK(sim.playerState(0).won);
  CHECK(sim.objectives()[0].state == 1);
  CHECK(sim.triggerFired(2));
}

TEST_CASE("computer players build up, train an army and attack") {
  Rules R;
  Map map(128, 128);
  map.generateCountryside(1940);
  Simulation sim(map, R.types, R.weapons, 11);
  skirmishSetup(sim, R, true);
  auto structures = [&](uint8_t p) {
    int n = 0;
    for (uint32_t e : sim.registry().pool<CStructure>().entities()) n += sim.registry().pool<CUnit>().get(e).owner == p;
    return n;
  };
  const int startStructures = structures(0);
  bool attacked = false;
  for (int i = 0; i < 15 * 60 * 12 && !attacked; ++i) {  // up to 12 minutes
    sim.tick();
    for (const SimEvent& ev : sim.events())
      if (ev.type == EventType::UnitDied && sim.types().get(ev.unitType).isStructure()) attacked = true;
  }
  CHECK(structures(0) > startStructures);  // it built
  CHECK(sim.ai().player(0).waves > 0);      // it attacked
  CHECK(attacked);                          // and destroyed something of the enemy's (or lost something)
}

TEST_CASE("save and replay files round-trip and refuse damage") {
  SaveFile f;
  f.metaJson = R"({"title":"Test"})";
  f.mapBytes = {1, 2, 3, 4};
  f.stateBytes.assign(5000, 7);
  Command c;
  c.type = CommandType::Move;
  c.target = {5, 6};
  f.commands.push_back({42, c});
  f.endTick = 99;
  const auto bytes = encodeSaveFile(f, true);
  SaveFile g;
  std::string err;
  REQUIRE(decodeSaveFile(bytes, g, true, err));
  CHECK(g.metaJson == f.metaJson);
  CHECK(g.stateBytes == f.stateBytes);
  REQUIRE(g.commands.size() == 1);
  CHECK(g.commands[0].tick == 42);
  CHECK(g.commands[0].cmd.target == Vec2i{5, 6});
  CHECK(g.endTick == 99);
  CHECK_FALSE(decodeSaveFile(bytes, g, false, err));  // a replay isn't a save
  auto broken = bytes;
  broken[broken.size() / 2] ^= 0xFF;
  CHECK_FALSE(decodeSaveFile(broken, g, true, err));
  broken = bytes;
  broken.resize(broken.size() - 10);
  CHECK_FALSE(decodeSaveFile(broken, g, true, err));
}

TEST_CASE("each side's computer player fights to its own doctrine") {
  Rules R;
  Map map(128, 128);
  map.generateCountryside(1951);
  Simulation sim(map, R.types, R.weapons, 11);
  base(sim, R, 0, "ger", {22 * 256, 64 * 256});
  base(sim, R, 1, "rus", {106 * 256, 64 * 256});
  for (uint8_t p : {uint8_t{0}, uint8_t{1}}) {
    Command s;
    s.type = CommandType::Spawn;
    s.player = p;
    s.unitType = R.t(p ? "rus_rifleman" : "ger_rifleman");
    s.count = 10;
    s.target = {(p ? 96 : 32) * 256, 64 * 256};
    sim.queue(s);
    Command c;
    c.type = CommandType::SetController;
    c.player = p;
    c.amount = 2;
    sim.queue(c);
  }
  sim.tick();
  bool raided = false, regrouped = false, bound = false;
  int peakDepots[2] = {};
  for (int i = 0; i < 15 * 60 * 8; ++i) {
    sim.tick();
    for (uint8_t p = 0; p < 2; ++p) {
      raided |= !sim.ai().player(p).raiders.empty();
      regrouped |= sim.ai().player(p).phase == 2;
    }
    if (i % 150 == 0) {
      int depots[2] = {};
      for (uint32_t e : sim.registry().pool<CUnit>().entities()) {
        bound |= sim.registry().pool<CGroupMember>().has(e);
        const CUnit& u = sim.registry().pool<CUnit>().get(e);
        if (u.owner <= 1 && sim.types().get(u.type).role == "depot") ++depots[u.owner];
      }
      for (int p = 0; p < 2; ++p) peakDepots[p] = std::max(peakDepots[p], depots[p]);
    }
  }
  int count[2][4] = {};  // depots, trenches, wire, men (depots: see the peak instead)
  int army[2] = {};
  for (uint32_t e : sim.registry().pool<CUnit>().entities()) {
    const CUnit& u = sim.registry().pool<CUnit>().get(e);
    if (u.owner > 1) continue;
    const UnitTypeDef& d = sim.types().get(u.type);
    count[u.owner][0] += d.role == "depot";
    count[u.owner][1] += d.model == "trench";
    count[u.owner][2] += d.model == "wire";
    if (!d.isStructure() && !d.weapons.empty()) {
      ++army[u.owner];
      count[u.owner][3] += d.infantry();
    }
  }
  for (int p = 0; p < 2; ++p) {
    CHECK(peakDepots[p] >= 4);  // it expanded onto more supply points
    CHECK(count[p][1] >= 5);  // and dug a trench line
  }
  CHECK(count[0][2] > 0);   // the Germans wire their front
  CHECK(raided);            // fast vehicles went raiding
  CHECK(bound);             // forces fight as bound units
  CHECK(regrouped);         // a hurt attack regroups instead of running home
}

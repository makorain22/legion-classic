// Campaigns and the tutorial: the files, the progress kept between missions,
// and the tutorial played through the way its hints say.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>

#include "campaign.hpp"
#include "mission_battle.hpp"
#include "slave/map/map_io.hpp"
#include "slave/sim/script.hpp"
#include "slave/sim/simulation.hpp"

using namespace slave;
using namespace legion;

namespace {
using testkit::Battle;
using testkit::Rules;
using testkit::kContent;

const CampaignInfo* find(const std::vector<CampaignInfo>& cs, const char* id) {
  for (const CampaignInfo& c : cs)
    if (c.id == id) return &c;
  return nullptr;
}
}  // namespace

TEST_CASE("the campaigns and the tutorial are all there, and every mission is sound") {
  std::string warn;
  const auto cs = scanCampaigns({kContent + "campaigns/"}, warn);
  CHECK(warn.empty());
  REQUIRE(cs.size() >= 4);
  CHECK(cs.front().id == "tutorial");  // first in order
  CHECK(cs.front().tutorial);
  for (const char* id : {"ger", "rus", "gbr"}) {
    const CampaignInfo* c = find(cs, id);
    REQUIRE_MESSAGE(c, id);
    CHECK(c->faction == id);
    CHECK(c->missions.size() >= 3);
  }
  Rules R;
  for (const CampaignInfo& c : cs)
    for (size_t m = 0; m < c.missions.size(); ++m) {
      INFO(c.id << " / " << c.missions[m].id);
      CHECK(std::filesystem::exists(c.briefingPath(m)));
      Battle b(R);
      REQUIRE(b.load(c.mapPath(m)));
      const ScenarioScript& s = b.script;
      const nlohmann::json raw = nlohmann::json::parse(b.f.scriptJson);
      REQUIRE(raw["triggers"].size() == s.triggers.size());
      CHECK(!s.triggers.empty());
      for (const ScriptArea& a : s.areas) {
        CHECK(a.x0 >= 0);
        CHECK(a.y0 >= 0);
        CHECK(a.x1 < b.f.map.width());
        CHECK(a.y1 < b.f.map.height());
      }
      int objectives = 0;
      bool victory = false;
      for (size_t t = 0; t < s.triggers.size(); ++t) {
        // Every unit a script names exists in the rules (an unknown id would read as "any unit").
        for (const auto& x : raw["triggers"][t]["conditions"]) CHECK((x["unit"].is_null() || R.types.find(x["unit"]) >= 0));
        for (const auto& x : raw["triggers"][t]["actions"]) CHECK((x["unit"].is_null() || R.types.find(x["unit"]) >= 0));
        for (const Condition& k : s.triggers[t].conditions) {
          if (k.area >= 0) CHECK(k.area < static_cast<int>(s.areas.size()));
          if (k.type == CondType::TriggerFired) CHECK((k.trigger >= 0 && k.trigger < static_cast<int>(s.triggers.size())));
        }
        for (const Action& a : s.triggers[t].actions) {
          if (a.area >= 0) CHECK(a.area < static_cast<int>(s.areas.size()));
          if (a.area2 >= 0) CHECK(a.area2 < static_cast<int>(s.areas.size()));
          if (a.type == ActionType::AddObjective) ++objectives;
          if (a.type == ActionType::CompleteObjective || a.type == ActionType::FailObjective) CHECK(a.value < objectives + 8);
          if (a.type == ActionType::Victory) victory = true;
          if (a.type == ActionType::SpawnUnits) CHECK(a.unitType >= 0);
        }
      }
      CHECK(victory);  // every mission can be won
      // A minute of the battle runs, and the briefing has given the objectives.
      for (int i = 0; i < 15 * 60; ++i) b.sim->tick();
      CHECK(!b.sim->objectives().empty());
      CHECK_FALSE(b.sim->playerState(0).defeated);
    }
}

TEST_CASE("campaign progress: missions open in order, flags carry over, and it survives a restart") {
  CampaignInfo c;
  c.id = "x";
  c.missions = {{"m1", "One", "a", "a", "", ""}, {"m2", "Two", "b", "b", "", ""}, {"m3", "Three", "c", "c", "", ""}};
  CampaignProgress p;
  CHECK(p.open(c, 0));
  CHECK_FALSE(p.open(c, 1));
  p.recordWin("x", "m1", {{"tiger_survived", 1}});
  CHECK(p.open(c, 1));
  CHECK_FALSE(p.open(c, 2));
  CHECK(p.wonCount(c) == 1);
  p.recordWin("x", "m2", {{"bonus", 3}});
  const std::string path = (std::filesystem::temp_directory_path() / "legion_progress_test.json").string();
  REQUIRE(p.save(path));
  CampaignProgress q;
  REQUIRE(q.load(path));
  CHECK(q.open(c, 2));
  CHECK(q.flags["x"]["tiger_survived"] == 1);
  CHECK(q.flags["x"]["bonus"] == 3);
  std::filesystem::remove(path);

  const auto lines = parseBriefing("# Title\n\nSome **bold** text\nrunning on.\n\n## Orders\n- one\n- two\n> a quote\n");
  REQUIRE(lines.size() >= 6);
  CHECK(lines[0].kind == BriefingLine::Title);
  CHECK(lines[2].text == "Some bold text running on.");
  CHECK(lines[4].kind == BriefingLine::Heading);
  CHECK(lines[5].text == "one");
  CHECK(lines.back().kind == BriefingLine::Quote);
}

TEST_CASE("flags from earlier missions reach the later ones") {
  Rules R;
  for (bool tiger : {false, true}) {
    Battle b(R);
    std::map<std::string, int32_t> flags;
    if (tiger) flags["tiger_survived"] = 1;
    REQUIRE(b.load(kContent + "campaigns/ger/g2_ice_road.lmap", flags));
    for (int i = 0; i < 15 * 3; ++i) b.sim->tick();
    CHECK(b.ofType(0, "ger_heavy_tank").size() == (tiger ? 1u : 0u));  // the Tiger from Kalach comes back
  }
}

TEST_CASE("tutorial 1, First Orders, is won by doing what the hints say") {
  Rules R;
  Battle b(R);
  REQUIRE(b.load(kContent + "campaigns/tutorial/t1_first_orders.lmap"));
  CHECK(b.sim->hint(0).find("Basic Training") == 0);
  auto men = [&] { return b.units(0, [](const UnitTypeDef& d, Entity) { return !d.isStructure(); }); };
  b.order(CommandType::Move, men(), {26, 32});
  REQUIRE(b.runUntil([&] { return b.fired("At the farm"); }, 120));
  CHECK(b.fired("First move"));
  CHECK(b.sim->hint(0).find("Attack-move") == 0);
  b.order(CommandType::AttackMove, men(), {44, 32});
  REQUIRE(b.runUntil([&] { return b.fired("Wood clear"); }, 240));
  b.runUntil([] { return false; }, 2);
  b.order(CommandType::Move, men(), {44, 32});  // the reinforcements too
  REQUIRE(b.runUntil([&] { return b.won(); }, 300));
}

TEST_CASE("tutorial 2, Cover and Armour, is won by doing what the hints say") {
  Rules R;
  Battle b(R);
  REQUIRE(b.load(kContent + "campaigns/tutorial/t2_cover.lmap"));
  const auto cottages = b.ofType(NEUTRAL_OWNER, "neutral_cottage");
  REQUIRE(cottages.size() == 2);
  auto riflemen = b.ofType(0, "gbr_rifleman");
  REQUIRE(riflemen.size() == 8);
  b.order(CommandType::Occupy, {riflemen.begin(), riflemen.begin() + 4}, {0, 0}, cottages[0]);
  b.order(CommandType::Occupy, {riflemen.begin() + 4, riflemen.end()}, {0, 0}, cottages[1]);
  REQUIRE(b.runUntil([&] { return b.fired("Garrisoned"); }, 120));
  b.order(CommandType::Move, b.ofType(0, "gbr_mg_team"), {28, 32});
  // When the tank comes, the PIAT team goes for it.
  const bool tankDone = b.runUntil([&] { return b.fired("Tank knocked out"); }, 400, [&] {
    if (!b.fired("Tank")) return;
    const auto tanks = b.ofType(1, "ger_light_tank");
    if (!tanks.empty()) b.order(CommandType::Attack, b.ofType(0, "gbr_at_team"), {0, 0}, tanks[0]);
  });
  REQUIRE(tankDone);
  // Out of the cottages, as the last hint asks.
  b.order(CommandType::Unload, cottages, {0, 0});
  REQUIRE(b.runUntil([&] { return b.won(); }, 60));
  CHECK(b.fired("Evacuated"));
}

TEST_CASE("tutorial 3, Digging In, is won by doing what the hints say") {
  Rules R;
  Battle b(R);
  REQUIRE(b.load(kContent + "campaigns/tutorial/t3_dig_in.lmap"));
  // Only field works can be built.
  CHECK(b.sim->buildLocked(0, R.types.find("gbr_rifleman")));
  CHECK_FALSE(b.sim->buildLocked(0, R.types.find("gbr_trench")));
  CHECK_FALSE(b.sim->canProduce(0, R.types.find("gbr_power")));
  auto walls = [&](const char* id, int x, int y0, int y1) {
    Command c;
    c.type = CommandType::PlaceWalls;
    c.player = 0;
    c.unitType = static_cast<uint16_t>(R.types.find(id));
    for (int y = y0; y <= y1; ++y) c.spawns.push_back({c.unitType, 0, {x * 256 + 128, y * 256 + 128}, 0, -1});
    b.sim->queue(c);
  };
  walls("gbr_trench", 25, 28, 35);
  REQUIRE(b.runUntil([&] { return b.fired("Dug"); }, 30));
  const auto trench = b.ofType(0, "gbr_trench");
  REQUIRE(trench.size() >= 6);
  b.order(CommandType::Occupy, b.ofType(0, "gbr_rifleman"), {0, 0}, trench[trench.size() / 2]);
  REQUIRE(b.runUntil([&] { return b.fired("Manned"); }, 90));
  walls("gbr_wire", 28, 27, 36);
  REQUIRE(b.runUntil([&] { return b.fired("Wired"); }, 30));
  // The Bren teams man a short wall of sandbags behind the trench (two
  // tiles nobody is standing on: men walking up may be in the way).
  auto clear = [&](int x, int y) {
    Registry& reg = b.sim->registry();
    for (uint32_t e : reg.pool<CUnit>().entities()) {
      const CPosition& p = reg.pool<CPosition>().get(e);
      if (p.x / 256 == x && p.y / 256 == y) return false;
    }
    return true;
  };
  int bx = 24, by = 31;
  for (int x : {24, 23}) {
    for (int y = 30; y <= 35; ++y)
      if (clear(x, y) && clear(x, y + 1)) {
        bx = x;
        by = y;
        break;
      }
    if (clear(bx, by) && clear(bx, by + 1)) break;
  }
  walls("gbr_sandbags", bx, by, by + 1);
  b.runUntil([] { return false; }, 1);
  const auto bags = b.ofType(0, "gbr_sandbags");
  REQUIRE(bags.size() == 2);
  b.order(CommandType::Occupy, b.ofType(0, "gbr_mg_team"), {0, 0}, bags[0]);
  b.runUntil([] { return false; }, 25);  // machine-gun teams are slow
  for (Entity m : b.ofType(0, "gbr_mg_team")) CHECK(b.sim->inTrench(m));
  REQUIRE(b.runUntil([&] { return b.won(); }, 420));
  CHECK_FALSE(b.sim->buildLocked(0, R.types.find("gbr_rifleman")));  // everything open again
}

TEST_CASE("tutorial 4, Base and Supply, is won by doing what the hints say") {
  Rules R;
  Battle b(R);
  REQUIRE(b.load(kContent + "campaigns/tutorial/t4_base.lmap"));
  CHECK(b.sim->highlight(0) == "order.deploy");
  const auto mcv = b.ofType(0, "gbr_mcv");
  REQUIRE(mcv.size() == 1);
  b.order(CommandType::Deploy, mcv, {0, 0});
  REQUIRE(b.runUntil([&] { return b.fired("Yard"); }, 60));
  CHECK(b.sim->highlight(0) == "build:gbr_power");
  const CPosition& yp = b.sim->registry().get<CPosition>(b.ofType(0, "gbr_yard")[0]);
  const Vec2i yard{yp.x / 256, yp.y / 256};
  REQUIRE(b.build("gbr_power", {yard.x - 5, yard.y - 4}));
  REQUIRE(b.runUntil([&] { return b.fired("Power"); }, 10));
  REQUIRE(b.build("gbr_post", {yard.x - 5, yard.y + 3}));
  REQUIRE(b.runUntil([&] { return b.fired("Depot"); }, 10));
  REQUIRE(b.build("gbr_barracks", {yard.x + 5, yard.y + 3}));
  Command train;
  train.type = CommandType::Produce;
  train.player = 0;
  train.unitType = static_cast<uint16_t>(R.types.find("gbr_rifleman"));
  train.amount = 10;
  b.sim->queue(train);
  REQUIRE(b.runUntil([&] { return b.fired("Riflemen"); }, 120));
  REQUIRE(b.build("gbr_warehouse", {yard.x - 1, yard.y + 8}));
  REQUIRE(b.runUntil([&] { return b.fired("Warehouse"); }, 10));
  // The riflemen go and stand on the logistics point: the office goes up and the lorries start.
  REQUIRE(b.f.map.supplyPoints.size() == 1);
  const Vec2i sp = b.f.map.supplyPoints.front();
  b.order(CommandType::Move, b.ofType(0, "gbr_rifleman"), sp);
  REQUIRE(b.runUntil([&] { return b.fired("Office"); }, 120));
  CHECK(b.ofType(0, "gbr_supply_truck").size() == 3);
  REQUIRE(b.build("gbr_factory", {yard.x + 6, yard.y + 9}, 240));
  Command tank = train;
  tank.unitType = static_cast<uint16_t>(R.types.find("gbr_medium_tank"));
  tank.amount = 3;
  b.sim->queue(tank);
  REQUIRE(b.runUntil([&] { return b.fired("Cromwell"); }, 240));
  CHECK(b.sim->highlight(0).empty());
  // Wait for the other two tanks, then everyone attacks the outpost.
  b.runUntil([&] { return b.ofType(0, "gbr_medium_tank").size() >= 3; }, 120);
  auto army = [&] {
    return b.units(0, [](const UnitTypeDef& d, Entity) { return !d.isStructure() && !d.weapons.empty(); });
  };
  REQUIRE(b.runUntil([&] { return b.won(); }, 900, [&] { b.order(CommandType::AttackMove, army(), {78, 20}); }));
}

// The mission engine showcase (9.9): its story runs phase by phase - the
// cinematic opening, the set piece kept alive, the valley opening up when the
// village falls, the allied company sent in, the clock, the counter-attack
// that lets the heavy tanks fight and die, and victory at the crossing.
TEST_CASE("the mission engine showcase plays through its phases") {
  Rules R;
  Battle b(R);
  REQUIRE(b.load(kContent + "scenarios/The_Crossing_at_Dawn.lmap"));
  CHECK_FALSE(b.fired("Adler is lost"));  // the major's half-track carries its name from the first tick
  CHECK(b.sim->named("Adler").size() == 1);
  CHECK(b.f.map.width() == 96);
  CHECK(b.f.map.height() == 224);
  b.sim->tick();
  CHECK(b.sim->cinematic(0));
  CHECK(b.sim->playAreaLimited(0));
  CHECK_FALSE(b.sim->inPlayArea(0, 48, 60));
  const std::vector<Entity> guns = b.sim->named("Village guns");
  REQUIRE(guns.size() == 2);
  REQUIRE(b.sim->named("Adler").size() == 1);
  CHECK((b.sim->scriptFlags(guns[0]) & kScriptInvulnerable));
  CHECK(b.sim->ai().player(2).order == 2);  // the allied company holds its ground for now
  REQUIRE(b.runUntil([&] { return !b.sim->cinematic(0); }, 20));
  REQUIRE(b.sim->objectives().size() == 1);
  CHECK(b.sim->objectives()[0].area >= 0);
  // The set piece shrugs off anything.
  for (Entity g : guns) b.sim->debugDamage(g, 10000000);
  b.sim->tick();
  CHECK(b.sim->named("Village guns").size() == 2);
  // Clear the village and walk in.
  const int village = [&] {
    for (size_t i = 0; i < b.script.areas.size(); ++i)
      if (b.script.areas[i].name == "The village") return static_cast<int>(i);
    return -1;
  }();
  REQUIRE(village >= 0);
  const ScriptArea& va = b.script.areas[static_cast<size_t>(village)];
  Command kill;
  kill.type = CommandType::Kill;
  for (Entity e : b.units(1))
    if (va.contains(b.sim->registry().get<CPosition>(e).x >> 8, b.sim->registry().get<CPosition>(e).y >> 8)) kill.units.push_back(e);
  b.sim->queue(kill);
  std::vector<UnitEdit> moves;
  int k = 0;
  for (Entity e : b.ofType(0, "ger_rifleman")) {
    UnitEdit ed;
    ed.unit = e;
    ed.move = true;
    ed.pos = {(va.x0 + 4 + k % 8) * 256 + 128, (va.y1 - 3) * 256 + 128};
    moves.push_back(ed);
    ++k;
  }
  Command mv;
  mv.type = CommandType::Edit;
  mv.edits = moves;
  b.sim->queue(mv);
  REQUIRE(b.runUntil([&] { return b.fired("Village taken"); }, 30));
  CHECK_FALSE(b.sim->playAreaLimited(0));
  CHECK(b.sim->countdownLeft("Reich counter-attack") > 200);
  CHECK(b.sim->ai().player(2).order == 1);  // the grenadiers go for the crossing
  CHECK(b.sim->objectives().size() == 2);
  // The clock runs out: the heavies are let loose.
  REQUIRE(b.runUntil([&] { return b.fired("Counter-attack"); }, 260));
  for (Entity g : b.sim->named("Village guns")) {
    CHECK_FALSE((b.sim->scriptFlags(g) & kScriptInvulnerable));
    CHECK_FALSE((b.sim->scriptFlags(g) & kScriptNoAi));
  }
  CHECK(b.sim->ai().player(1).order == 1);
  CHECK(b.sim->countdowns().empty());
  for (Entity g : b.sim->named("Village guns")) b.sim->debugDamage(g, 10000000);
  REQUIRE(b.runUntil([&] { return b.fired("Heavies gone"); }, 5));
  CHECK(b.sim->objectives()[2].state == 1);
}

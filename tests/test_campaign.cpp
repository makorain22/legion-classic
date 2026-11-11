// Campaigns and the tutorial: the files, the progress kept between missions,
// and the tutorial played through the way its hints say.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>

#include "campaign.hpp"
#include "slave/map/map_io.hpp"
#include "slave/sim/script.hpp"
#include "slave/sim/simulation.hpp"

using namespace slave;
using namespace legion;

namespace {
const std::string kContent = LEGION_CONTENT_DIR;

struct Rules {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  Rules() {
    std::string err;
    const std::string dir = kContent + "rules/";
    REQUIRE(weapons.loadFile(dir + "weapons.json", err));
    REQUIRE(types.loadFile(dir + "units.json", err));
    REQUIRE(types.loadFile(dir + "buildings.json", err));
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
  }
};

// A mission set up the way the game starts one (GameScene::replaceWorld).
struct Battle {
  const Rules& R;
  MapFile f;
  std::unique_ptr<Simulation> sim;
  ScenarioScript script;
  explicit Battle(const Rules& r) : R(r) {}
  bool load(const std::string& path, std::map<std::string, int32_t> flags = {}) {
    std::string err;
    if (!loadMap(path, f, err)) {
      MESSAGE(path << ": " << err);
      return false;
    }
    sim = std::make_unique<Simulation>(f.map, R.types, R.weapons, 7);
    sim->setFlags(std::move(flags));
    for (uint8_t i = 0; i < MAX_PLAYERS && i < f.players.size(); ++i) {
      Command c;
      c.type = CommandType::SetTeam;
      c.player = i;
      c.count = f.players[i].team;
      sim->queue(c);
    }
    Command spawns;
    spawns.type = CommandType::SpawnList;
    for (const UnitPlacement& u : f.units) {
      const int t = R.types.find(u.type);
      if (t < 0) {
        MESSAGE("unknown unit " << u.type);
        return false;
      }
      spawns.spawns.push_back({static_cast<uint16_t>(t), u.owner, {u.x, u.y}, u.facing, u.health});
    }
    sim->queue(spawns);
    for (uint8_t i = 0; i < f.players.size() && i < MAX_PLAYERS; ++i) {
      if (f.players[i].active && !f.players[i].human) {
        Command c;
        c.type = CommandType::SetController;
        c.player = i;
        c.amount = f.players[i].difficulty;
        sim->queue(c);
      }
      Command c;
      c.type = CommandType::SetCredits;
      c.player = i;
      c.amount = f.players[i].credits;
      sim->queue(c);
    }
    script = scriptFromJson(nlohmann::json::parse(f.scriptJson), &R.types);
    sim->setScript(script, false);
    sim->tick();
    return true;
  }
  std::vector<Entity> units(uint8_t owner, const std::function<bool(const UnitTypeDef&, Entity)>& keep = {}) {
    std::vector<Entity> out;
    Registry& reg = sim->registry();
    for (uint32_t e : reg.pool<CUnit>().entities()) {
      const CUnit& u = reg.pool<CUnit>().get(e);
      if (u.owner != owner) continue;
      const UnitTypeDef& d = R.types.get(u.type);
      if (keep && !keep(d, reg.handle(e))) continue;
      out.push_back(reg.handle(e));
    }
    return out;
  }
  std::vector<Entity> ofType(uint8_t owner, const char* id) {
    return units(owner, [&](const UnitTypeDef& d, Entity) { return d.id == id; });
  }
  void order(CommandType t, std::vector<Entity> who, Vec2i tile, Entity target = kNullEntity) {
    Command c;
    c.type = t;
    c.player = 0;
    c.units = std::move(who);
    c.target = {tile.x * 256 + 128, tile.y * 256 + 128};
    c.targetUnit = target;
    sim->queue(c);
  }
  // Runs until `done` or `seconds` of game time pass. Returns whether done.
  bool runUntil(const std::function<bool()>& done, int seconds, const std::function<void()>& each = {}) {
    for (int i = 0; i < seconds * 15; ++i) {
      if (done()) return true;
      if (each && i % 30 == 0) each();
      sim->tick();
    }
    return done();
  }
  int trigger(const std::string& name) const {
    for (size_t i = 0; i < script.triggers.size(); ++i)
      if (script.triggers[i].name == name) return static_cast<int>(i);
    FAIL("no trigger " << name);
    return -1;
  }
  bool fired(const std::string& name) const { return sim->triggerFired(trigger(name)); }
  bool won() const { return sim->playerState(0).won; }
  // Builds a structure for player 0: orders it and places it near (x, y) when ready.
  bool build(const char* id, Vec2i near, int seconds = 120) {
    const int type = R.types.find(id);
    Command p;
    p.type = CommandType::Produce;
    p.player = 0;
    p.unitType = static_cast<uint16_t>(type);
    p.amount = 1;
    sim->queue(p);
    const bool ready = runUntil([&] {
      for (const ProductionQueue& q : sim->playerState(0).queues)
        if (q.ready && !q.items.empty() && q.items.front() == type) return true;
      return false;
    }, seconds);
    if (!ready) return false;
    for (int r = 0; r < 14; ++r)
      for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
          if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
          const Vec2i at{(near.x + dx) * 256, (near.y + dy) * 256};
          if (!sim->canPlace(type, sim->footprintOrigin(type, at))) continue;
          const size_t before = ofType(0, id).size();
          Command c;
          c.type = CommandType::PlaceBuilding;
          c.player = 0;
          c.unitType = static_cast<uint16_t>(type);
          c.target = at;
          sim->queue(c);
          sim->tick();
          sim->tick();
          if (ofType(0, id).size() > before) return true;
        }
    return false;
  }
};

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

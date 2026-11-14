// Pioneer part 4: the script registry - every condition and action described
// once, the script checker, and the reference document written from it.
#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "pioneer/map/map.hpp"
#include "pioneer/map/map_io.hpp"
#include "pioneer/sim/script_registry.hpp"
#include "pioneer/sim/unit_types.hpp"
#include "pioneer/sim/weapons.hpp"

using namespace pioneer;

TEST_CASE("script registry: every condition and action is described, with help for itself and each field") {
  REQUIRE(script::conditions().size() == static_cast<size_t>(CondType::Count));
  REQUIRE(script::actions().size() == static_cast<size_t>(ActionType::Count));
  std::set<std::string> ids;
  for (const auto* list : {&script::conditions(), &script::actions()})
    for (size_t i = 0; i < list->size(); ++i) {
      const script::Entry& e = (*list)[i];
      CAPTURE(e.id);
      CHECK(e.type == static_cast<int>(i));
      CHECK(std::string(e.id).size() > 2);
      CHECK(std::string(e.name).size() > 2);
      CHECK(std::string(e.group).size() > 2);
      CHECK(std::string(e.summary).size() > 15);
      CHECK(std::string(e.example).size() > 5);
      CHECK(ids.insert(std::string(e.action ? "a:" : "c:") + e.id).second);
      std::set<int> slots;
      for (const script::Field& f : e.fields) {
        CHECK(std::string(f.label).size() > 1);
        CHECK(std::string(f.help).size() > 5);
        CHECK(std::string(f.key).size() > 1);
        CHECK(slots.insert(static_cast<int>(f.slot)).second);  // no slot used twice
        if (f.kind == script::Kind::Choice) CHECK(std::string(f.choices).find('|') != std::string::npos);
        if (f.slot != script::Slot::Text) CHECK(f.min <= f.max);
      }
      if (e.action) {
        CHECK(script::findAction(e.id) == &e);
        CHECK(e.takesUnits == actionTakesUnits(static_cast<ActionType>(i)));
      } else {
        CHECK(script::findCondition(e.id) == &e);
      }
    }
  CHECK(script::findAction("no_such_action") == nullptr);
  // Defaults land in the right members.
  const Action spawn = script::makeAction(ActionType::SpawnUnits);
  CHECK(spawn.value == 5);
  CHECK(spawn.player == 0);
  const Action msg = script::makeAction(ActionType::Message);
  CHECK(msg.player == ANY_PLAYER);
  const Condition diff = script::makeCondition(CondType::DifficultyAtLeast);
  CHECK(diff.value == 2);
  CHECK(script::entry(ActionType::AttackMove).field(script::Slot::Area2) != nullptr);
  CHECK(script::entry(ActionType::AttackMove).fieldByKey("text")->optional);  // units by name
}

TEST_CASE("script check: finds what is wrong with a script and says where") {
  ScenarioScript s;
  s.areas.push_back({"Bridge", 10, 10, 14, 14});
  s.areas.push_back({"Unused", 2, 2, 3, 3});
  Trigger t;
  t.name = "Wave";
  Condition c = script::makeCondition(CondType::UnitsInArea);
  c.area = 7;  // no such area
  t.conditions.push_back(c);
  Condition named = script::makeCondition(CondType::NamedDead);
  named.text = "Adlr";  // never named
  t.conditions.push_back(named);
  Action spawn = script::makeAction(ActionType::SpawnUnits);
  spawn.area = 0;  // but no unit type
  t.actions.push_back(spawn);
  Action done = script::makeAction(ActionType::CompleteObjective);
  done.value = 3;  // nothing adds objectives
  t.actions.push_back(done);
  Action fire = script::makeAction(ActionType::FireTrigger);
  fire.trigger = 0;  // itself
  t.actions.push_back(fire);
  s.triggers.push_back(t);
  Trigger off;
  off.name = "Never";
  off.enabled = false;
  off.conditions.push_back(script::makeCondition(CondType::Always));
  s.triggers.push_back(off);

  const std::vector<script::Issue> issues = script::check(s, {});
  auto has = [&](script::Issue::Severity sev, const char* part, int trig) {
    for (const script::Issue& i : issues)
      if (i.severity == sev && i.trigger == trig && i.text.find(part) != std::string::npos) return true;
    return false;
  };
  CHECK(has(script::Issue::Error, "area 7 does not exist", 0));
  CHECK(has(script::Issue::Warning, "nothing is ever named 'Adlr'", 0));
  CHECK(has(script::Issue::Error, "no unit type chosen", 0));
  CHECK(has(script::Issue::Warning, "only adds 0", 0));
  CHECK(has(script::Issue::Error, "fires itself", 0));
  CHECK(has(script::Issue::Warning, "switched off and nothing switches it on", 1));
  CHECK(has(script::Issue::Warning, "has no actions", 1));
  CHECK(has(script::Issue::Note, "area 'Unused' is not used", -1));
  CHECK(has(script::Issue::Note, "no Victory action", -1));

  // The clock-hiding trap, and a way that doesn't exist.
  ScenarioScript s2;
  s2.areas.push_back({"West", 2, 2, 6, 6});
  s2.areas.push_back({"East", 50, 2, 56, 6});
  Trigger stop;
  stop.name = "Charges found";
  stop.conditions.push_back(script::makeCondition(CondType::Always));
  Action st = script::makeAction(ActionType::StopCountdown);
  st.text = "Demolition";
  stop.actions.push_back(st);
  Action go = script::makeAction(ActionType::MoveUnits);
  go.area = 0;
  go.area2 = 1;
  stop.actions.push_back(go);
  Action start = script::makeAction(ActionType::Countdown);
  start.text = "Demolition";
  stop.actions.push_back(start);
  s2.triggers.push_back(stop);
  Trigger blown;
  blown.name = "Bridge blown";
  Condition cd = script::makeCondition(CondType::CountdownDone);
  cd.text = "Demolition";
  blown.conditions.push_back(cd);
  blown.actions.push_back(script::makeAction(ActionType::Victory));
  s2.triggers.push_back(blown);
  Map map(64, 32, 100);
  for (int y = 0; y < map.height(); ++y) map.setBlock(30, y, 7);  // a wall from edge to edge
  script::CheckContext ctx;
  ctx.map = &map;
  const std::vector<script::Issue> i2 = script::check(s2, ctx);
  bool trap = false, noWay = false, errors = false;
  for (const script::Issue& i : i2) {
    trap = trap || (i.trigger == 1 && i.text.find("never sees it run out") != std::string::npos);
    noWay = noWay || i.text.find("can't get from 'West' to 'East'") != std::string::npos;
    errors = errors || i.severity == script::Issue::Error;
  }
  CHECK(trap);
  CHECK(noWay);
  CHECK_FALSE(errors);
}

TEST_CASE("script check: every mission that ships has no errors") {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  std::string err;
  const std::string rules = LEGION_RULES_DIR;
  REQUIRE(weapons.loadFile(rules + "weapons.json", err));
  REQUIRE(types.loadFile(rules + "units.json", err));
  REQUIRE(types.loadFile(rules + "buildings.json", err));
  int missions = 0, triggers = 0, warnings = 0, notes = 0, errors = 0;
  std::map<std::string, int> kinds;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(std::string(LEGION_CONTENT_DIR))) {
    if (entry.path().extension() != ".lmap") continue;
    MapFile mf;
    REQUIRE_MESSAGE(loadMap(entry.path().string(), mf, err), err);
    if (mf.scriptJson.empty()) continue;
    const ScenarioScript s = scriptFromJson(nlohmann::json::parse(mf.scriptJson), &types);
    if (s.triggers.empty()) continue;
    ++missions;
    triggers += static_cast<int>(s.triggers.size());
    script::CheckContext ctx;
    ctx.types = &types;
    ctx.map = &mf.map;
    for (const UnitPlacement& u : mf.units)
      if (!u.name.empty()) ctx.placedNames.push_back(u.name);
    for (const script::Issue& i : script::check(s, ctx)) {
      if (i.severity == script::Issue::Error) {
        ++errors;
        FAIL_CHECK(entry.path().filename().string(), " trigger ", i.trigger, ": ", i.text);
      } else if (i.severity == script::Issue::Warning) {
        ++warnings;
        // Group by the message with names and numbers taken out.
        std::string k = i.text.substr(0, i.text.find('\''));
        ++kinds[k.substr(0, 60)];
      } else {
        ++notes;
      }
    }
  }
  CHECK(missions >= 70);
  CHECK(errors == 0);
  std::string top;
  for (const auto& [k, n] : kinds)
    if (n >= 3) top += "\n    " + std::to_string(n) + " x " + k;
  MESSAGE(missions, " missions, ", triggers, " triggers: ", warnings, " warnings, ", notes, " notes", top);
}

TEST_CASE("script reference: docs/SCRIPT_REFERENCE.md is what the registry says") {
  const std::string path = std::string(LEGION_CONTENT_DIR) + "../docs/SCRIPT_REFERENCE.md";
  const std::string now = script::referenceMarkdown();
  if (std::getenv("LEGION_WRITE_DOCS")) std::ofstream(path, std::ios::binary) << now;
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  CHECK_MESSAGE(ss.str() == now, "docs/SCRIPT_REFERENCE.md is out of date: run the tests once with LEGION_WRITE_DOCS=1");
  CHECK(now.find("#### Spawn units (`spawn_units`)") != std::string::npos);
}

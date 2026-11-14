// Pioneer part 4: the event bus, and Lua for missions (the sandbox, the bridge
// to the script registry, saving, determinism).
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "pioneer/core/events.hpp"
#include "pioneer/map/map.hpp"
#include "pioneer/sim/mission_lua.hpp"
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

struct World {
  UnitTypeRegistry types = makeTypes();
  WeaponRegistry weapons;
  Map map{64, 64, 100};
  Simulation sim{map, types, weapons, 7};
  explicit World(const std::string& lua, uint8_t difficulty = 1) {
    ScenarioScript s;
    s.areas = {{"Start", 4, 4, 10, 10}, {"North road", 40, 4, 46, 10}, {"Bridge", 30, 28, 34, 34}};
    s.lua = lua;
    sim.setMissionDifficulty(difficulty);
    sim.setScript(s, false);
  }
  void run(int seconds) {
    for (int i = 0; i < seconds * 15; ++i) sim.tick();
  }
  std::string lua(const std::string& code) { return sim.lua() ? sim.lua()->run(code) : "no lua"; }
};
}  // namespace

TEST_CASE("event bus: typed events, priorities, queued delivery, subscriptions that end themselves") {
  struct Ping {
    int n;
  };
  struct Other {
    std::string s;
  };
  EventBus bus;
  std::string heard;
  auto a = bus.subscribe<Ping>([&](const Ping& p) { heard += "a" + std::to_string(p.n); });
  auto b = bus.subscribe<Ping>([&](const Ping& p) { heard += "b" + std::to_string(p.n); }, 10);  // hears first
  auto c = bus.subscribe<Other>([&](const Other& o) { heard += o.s; });
  CHECK(bus.subscribers<Ping>() == 2);
  CHECK(bus.publish(Ping{1}) == 2);
  CHECK(heard == "b1a1");
  CHECK(bus.publish(Other{"!"}) == 1);
  // Posted events wait for dispatch, and keep their order.
  heard.clear();
  bus.post(Ping{2});
  bus.post(Other{"?"});
  bus.post(Ping{3});
  CHECK(heard.empty());
  CHECK(bus.pending() == 3);
  CHECK(bus.dispatch() == 3);
  CHECK(heard == "b2a2?b3a3");
  // A subscription ends with its guard; a handler may unsubscribe (itself or others) and subscribe while being told.
  heard.clear();
  b.reset();
  EventBus::Subscription late;
  auto d = bus.subscribe<Ping>([&](const Ping&) {
    a.reset();
    if (!late.active()) late = bus.subscribe<Ping>([&](const Ping& p) { heard += "L" + std::to_string(p.n); });
  }, 5);
  bus.publish(Ping{4});
  CHECK(heard.empty());  // a was ended before its turn; the late one was not there when it began
  bus.publish(Ping{5});
  CHECK(heard == "L5");
  CHECK(bus.subscribers<Ping>() == 2);
  {
    auto scoped = bus.subscribe<Other>([&](const Other&) { heard += "x"; });
    CHECK(bus.subscribers<Other>() == 2);
  }
  CHECK(bus.subscribers<Other>() == 1);
  // A handler that posts: delivered in the same dispatch.
  auto e = bus.subscribe<Other>([&](const Other& o) {
    if (o.s == "again") bus.post(Ping{6});
  });
  heard.clear();
  bus.post(Other{"again"});
  CHECK(bus.dispatch() == 2);
  CHECK(heard == "againL6");
}

TEST_CASE("lua: a script drives the mission through the registry, by names") {
  World w(R"lua(
    state.waves = 0
    function on_start()
      add_objective{player = 0, text = "Hold on"}
      spawn_named{player = 1, area = "North road", unit = "rifle", value = 3, text = "Raiders"}
      attack_move{text = "Raiders", area2 = "Bridge"}
      countdown{text = "Relief", value = 5, player = "all"}
      set_counter{text = "started", value = 1}
      after(2, "second_wave", 4)
    end
    function second_wave(n)
      state.waves = state.waves + 1
      spawn_units{player = 1, area = "North road", unit = "rifle", value = n}
    end
    function on_countdown(name)
      if name == "Relief" then complete_objective{value = 0} victory{player = 0} end
    end
    function on_unit_died(kind, owner, x, y)
      state.last_dead = kind .. "@" .. owner
    end
  )lua");
  REQUIRE(w.sim.lua());
  REQUIRE_MESSAGE(w.sim.lua()->ready(), (w.sim.scriptErrors().empty() ? std::string() : w.sim.scriptErrors().back()));
  w.run(1);
  CHECK(w.sim.scriptErrors().empty());
  CHECK(w.sim.counter("started") == 1);
  REQUIRE(w.sim.objectives().size() == 1);
  CHECK(w.sim.named("Raiders").size() == 3);
  CHECK(w.sim.countUnits(1, 1, -1) + w.sim.countUnits(1, -1, -1) >= 3);
  CHECK(w.sim.registry().get<COrder>(w.sim.named("Raiders")[0]).type == OrderType::AttackMove);
  w.run(2);
  CHECK(w.sim.countUnits(1, -1, -1) == 7);  // the timer brought four more
  CHECK(w.lua("state.waves") == "1");
  // Queries and conditions.
  CHECK(w.lua("count(1)") == "7");
  CHECK(w.lua("count('all', 'Start')") == "0");
  CHECK(w.lua("is.unit_count_at_least{player = 1, value = 7}") == "true");
  CHECK(w.lua("is.unit_count_at_least{player = 1, value = 8}") == "false");
  CHECK(w.lua("is.unit_count_at_least{player = 1, value = 8, ['not'] = true}") == "true");
  CHECK(w.lua("named('Raiders')") == "3");
  CHECK(w.lua("counter('started') + flag('nothing')") == "1");
  CHECK(w.lua("select(2, difficulty())") == "normal");
  // A death is heard.
  Command kill;
  kill.type = CommandType::Kill;
  kill.units = {w.sim.named("Raiders")[0]};
  w.sim.queue(kill);
  w.run(1);
  CHECK(w.lua("state.last_dead") == "rifle@1");
  // The clock runs out: the handler ends the mission.
  w.run(3);
  CHECK(w.sim.objectives()[0].state == 1);
  CHECK(w.sim.playerState(0).won);
  CHECK(w.sim.scriptErrors().empty());
}

TEST_CASE("lua: mistakes are told, not fatal") {
  World w(R"lua(
    function on_start()
      spawn_units{player = 1, aera = "North road", unit = "rifle"}   -- misspelt field
    end
    function bad_area() spawn_units{player = 1, area = "Nowhere", unit = "rifle"} end
    function bad_unit() spawn_units{player = 1, area = "Start", unit = "panzer"} end
    function forever() while true do end end
    function hungry() local t = {} for i = 1, 1e9 do t[i] = ("x"):rep(1000) .. i end end
    function fine(n) set_counter{text = "fine", value = n} end
  )lua");
  w.run(1);
  REQUIRE(w.sim.scriptErrors().size() == 1);
  CHECK(w.sim.scriptErrors()[0].find("spawn_units has no field 'aera' (it has: player, area, unit, value, scaled)") != std::string::npos);
  CHECK(w.sim.scriptErrors()[0].find("on_start") != std::string::npos);
  CHECK(w.lua("bad_area()").find("there is no area called 'Nowhere'") != std::string::npos);
  CHECK(w.lua("bad_unit()").find("there is no unit type 'panzer'") != std::string::npos);
  // A loop that never ends is stopped; so is one that eats memory. The game goes on.
  CHECK(w.lua("forever()").find("ran too long") != std::string::npos);
  CHECK(w.sim.lua()->stepsUsed() >= static_cast<uint64_t>(MissionLua::kStepLimit));
  CHECK(w.lua("hungry()").find("not enough memory") != std::string::npos);
  CHECK(w.sim.lua()->memory() < MissionLua::kMemoryLimit);
  CHECK(w.lua("fine(5)") == "nil");
  CHECK(w.sim.counter("fine") == 5);
  w.run(1);

  // A script that doesn't compile, and one that touches the world while loading.
  World broken("function on_start( end");
  REQUIRE(broken.sim.lua());
  CHECK_FALSE(broken.sim.lua()->ready());
  REQUIRE_FALSE(broken.sim.scriptErrors().empty());
  broken.run(1);
  World eager("victory{player = 0}");
  CHECK_FALSE(eager.sim.lua()->ready());
  CHECK(eager.sim.scriptErrors().back().find("can't be changed while the script is being loaded") != std::string::npos);
  eager.run(1);
  CHECK_FALSE(eager.sim.playerState(0).won);
  std::string err;
  CHECK(MissionLua::compiles("function ok() return 1 end", err));
  CHECK_FALSE(MissionLua::compiles("function (", err));
  CHECK(err.find("mission:1") != std::string::npos);
}

TEST_CASE("lua: the sandbox - nothing a script could tell two machines apart with") {
  World w("");
  w.sim.setScript([] {
    ScenarioScript s;
    s.lua = "x = 1";
    return s;
  }(), false);
  REQUIRE(w.sim.lua());
  for (const char* gone : {"io", "os", "debug", "package", "require", "load", "loadstring", "dofile", "loadfile", "collectgarbage", "next",
                           "coroutine", "string.dump", "math.sin", "math.exp", "math.log", "math.pi"})
    CHECK_MESSAGE(w.lua(gone) == "nil", gone);
  for (const char* here : {"pairs", "ipairs", "tostring", "tonumber", "type", "pcall", "error", "assert", "select", "string.format", "table.sort",
                           "table.insert", "math.floor", "math.max", "math.sqrt", "math.random", "random", "after", "log", "is.always"})
    CHECK_MESSAGE(w.lua(std::string("type(") + here + ")") == "function", here);
  // No addresses.
  CHECK(w.lua("tostring({})") == "table");
  CHECK(w.lua("tostring(print)") == "function");
  CHECK(w.lua("tostring(12) .. tostring(true) .. tostring(nil)") == "12truenil");
  // pairs in a fixed order: numbers rising, then strings.
  CHECK(w.lua("(function() local t, s = {b = 1, [3] = 1, a = 1, [1] = 1, zz = 1, [2] = 1}, '' for k in pairs(t) do s = s .. k .. ' ' end return s end)()") ==
        "1 2 3 a b zz ");
  CHECK(w.lua("(function() local t = {} t[{}] = 1 for k in pairs(t) do end end)()").find("only have numbers and strings as keys") != std::string::npos);
  // No finalisers, no weak tables.
  CHECK(w.lua("setmetatable({}, {__gc = function() end})").find("'__gc' is not allowed") != std::string::npos);
  CHECK(w.lua("setmetatable({}, {__mode = 'k'})").find("'__mode' is not allowed") != std::string::npos);
  CHECK(w.lua("getmetatable(setmetatable({}, {__index = {v = 5}})).__index.v") == "5");
  // Arithmetic that is the same everywhere.
  CHECK(w.lua("2 ^ 10") == "1024.0");
  CHECK(w.lua("2 ^ -2") == "0.25");
  CHECK(w.lua("2 ^ 0.5 ~= 2 ^ 0.5") == "true");  // not a whole power: nan, not the C library's guess
  CHECK(w.lua("7 // 2 .. ' ' .. 7 % 3 .. ' ' .. math.floor(3.7) .. ' ' .. math.sqrt(16)") == "3 1 3 4.0");
  CHECK(w.lua("math.max(3, 9, 4) + math.min(3, 9, 4) + math.abs(-5)") == "17");
  // The dice are the simulation's: the same seed, the same rolls.
  World a(""), b("");
  for (World* x : {&a, &b}) {
    ScenarioScript s;
    s.lua = "function roll() local t = {} for i = 1, 8 do t[i] = random(6) end return table.concat(t) end";
    x->sim.setScript(s, false);
  }
  const std::string rolls = a.lua("roll()");
  CHECK(rolls.size() == 8);
  CHECK(rolls == b.lua("roll()"));
  CHECK(a.lua("roll()") != rolls);
  for (char c : rolls) CHECK((c >= '1' && c <= '6'));
}

TEST_CASE("lua: `state` and timers survive saving; the loaded mission carries on the same") {
  const char* src = R"lua(
    state.count = 0
    state.log = {}
    state.nested = {a = {1, 2, 3}, flag = true, ratio = 0.5}
    function on_start() after(3, "tick_tock", 1) end
    function tick_tock(n)
      state.count = state.count + n
      state.log[#state.log + 1] = "t" .. seconds()
      spawn_units{player = 1, area = "North road", unit = "rifle", value = random(3)}
      after(3, "tick_tock", n + 1)
    end
    function on_second() state.seconds = (state.seconds or 0) + 1 end
  )lua";
  World w(src);
  w.run(7);
  CHECK(w.lua("state.count") == "3");  // 1 + 2
  ByteWriter saved;
  w.sim.saveState(saved);
  // A fresh simulation loads it: functions defined again, `state` and the timers put back.
  World l("");
  ByteReader r(saved.bytes());
  std::string err;
  REQUIRE_MESSAGE(l.sim.loadState(r, err), err);
  CHECK(r.atEnd());
  REQUIRE(l.sim.lua());
  CHECK(l.lua("state.count") == "3");
  CHECK(l.lua("state.nested.a[3] + (state.nested.flag and 10 or 0) + state.nested.ratio") == "13.5");
  CHECK(l.lua("table.concat(state.log, ',')") == w.lua("table.concat(state.log, ',')"));
  CHECK(l.sim.stateHash() == w.sim.stateHash());
  CHECK(Simulation::digestDifference(l.sim.digest(), w.sim.digest()).empty());
  for (int i = 0; i < 15 * 10; ++i) {
    w.sim.tick();
    l.sim.tick();
  }
  CHECK(l.sim.stateHash() == w.sim.stateHash());
  CHECK(l.lua("state.count") == w.lua("state.count"));
  CHECK(l.lua("state.count") == "15");  // 1 + 2 before the save, 3 + 4 + 5 after: no timer lost, none doubled
  CHECK(Simulation::digestDifference(l.sim.digest(), w.sim.digest()).empty());
  CHECK(w.sim.scriptErrors().empty());
  // What can't be kept is said, not silently dropped.
  w.lua("state.fn = function() end");
  ByteWriter again;
  w.sim.saveState(again);
  REQUIRE_FALSE(w.sim.scriptErrors().empty());
  CHECK(w.sim.scriptErrors().back().find("`state` may hold numbers, texts, true/false and tables, not a function") != std::string::npos);
}

TEST_CASE("lua: triggers call it and ask it") {
  UnitTypeRegistry types = makeTypes();
  WeaponRegistry weapons;
  Map map(64, 64, 100);
  Simulation sim(map, types, weapons, 1);
  ScenarioScript s;
  s.areas = {{"Start", 4, 4, 10, 10}};
  s.lua = R"lua(
    state.calls = 0
    function ready() return seconds() >= 2 end
    function reinforce(n) state.calls = state.calls + 1 spawn_units{player = 0, area = "Start", unit = "tank", value = n} end
  )lua";
  Trigger t;
  t.name = "When Lua says so";
  Condition c = script::makeCondition(CondType::ScriptSays);
  c.text = "ready";
  t.conditions = {c};
  Action a = script::makeAction(ActionType::CallScript);
  a.text = "reinforce";
  a.value = 2;
  t.actions = {a};
  s.triggers = {t};
  bool errors = false;
  for (const script::Issue& i : script::check(s, {})) errors = errors || i.severity == script::Issue::Error;
  CHECK_FALSE(errors);
  sim.setScript(s, false);
  for (int i = 0; i < 15; ++i) sim.tick();
  CHECK(sim.unitCount() == 0);
  for (int i = 0; i < 30; ++i) sim.tick();
  CHECK(sim.unitCount() == 2);
  CHECK(sim.scriptErrors().empty());
  // Edited while the battle runs: the new script takes over what the old one kept.
  s.lua += "\nfunction extra() return state.calls end";
  sim.setScript(s, true);
  CHECK(sim.lua()->run("extra()") == "1");
  // The checker reads the Lua too.
  s.lua = "function broken(";
  bool found = false;
  for (const script::Issue& i : script::check(s, {})) found = found || i.text.find("the Lua script does not compile") != std::string::npos;
  CHECK(found);
}

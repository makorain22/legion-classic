// Pioneer Engine - Lua for missions: the sandbox and the bridge to the script registry.
#include "pioneer/sim/mission_lua.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <functional>

// Lua is built as C++ (engine/src/lua/pioneer_lua.cpp): its headers are C++ here too.
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "pioneer/core/log.hpp"
#include "pioneer/core/profiler.hpp"
#include "pioneer/sim/script_registry.hpp"
#include "pioneer/sim/simulation.hpp"

namespace pioneer {

struct MissionLua::Impl {
  Simulation& sim;
  lua_State* L = nullptr;
  size_t mem = 0;
  int64_t steps = 0;
  uint64_t lastSteps = 0;
  bool loading = false;  // the chunk is being run: the world may not be touched
  bool started = false;
  bool ok = false;
  struct Timer {
    uint32_t tick = 0;
    std::string fn;
    uint8_t kind = 0;  // 0 no argument, 1 number, 2 text
    double num = 0;
    std::string text;
  };
  std::vector<Timer> timers;
  std::vector<std::string> errors;
  explicit Impl(Simulation& s) : sim(s) {}

  static Impl& of(lua_State* L) { return **static_cast<Impl**>(lua_getextraspace(L)); }
  void fail(const std::string& what) {
    if (errors.size() >= 20) errors.erase(errors.begin());
    errors.push_back(what);
    PNR_WARN_CH(Script, "Lua: %s", what.c_str());
  }
  // Calls the function on the stack below its `nargs` arguments. True if it ran without error.
  bool pcall(int nargs, int nresults, const char* what) {
    steps = 0;
    const int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, [](lua_State* S) {
      luaL_traceback(S, S, lua_tostring(S, 1), 1);
      return 1;
    });
    lua_insert(L, base);
    const int rc = lua_pcall(L, nargs, nresults, base);
    lastSteps = static_cast<uint64_t>(steps);
    if (rc != LUA_OK) {
      const char* msg = lua_tostring(L, -1);
      fail(std::string(what) + ": " + (msg ? msg : "error"));
      lua_pop(L, 1);
    }
    lua_remove(L, base);
    return rc == LUA_OK;
  }
  bool callGlobal(const char* fn, int nargs) {  // arguments already pushed
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1 + nargs);
      return false;
    }
    lua_insert(L, -1 - nargs);
    return pcall(nargs, 0, fn);
  }
};

namespace {
using Impl = MissionLua::Impl;

void* allocate(void* ud, void* ptr, size_t osize, size_t nsize) {
  Impl* self = static_cast<Impl*>(ud);
  const size_t old = ptr ? osize : 0;
  if (nsize == 0) {
    self->mem -= std::min(self->mem, old);
    std::free(ptr);
    return nullptr;
  }
  if (nsize > old && self->mem + (nsize - old) > MissionLua::kMemoryLimit) return nullptr;
  void* p = std::realloc(ptr, nsize);
  if (p) self->mem = self->mem - std::min(self->mem, old) + nsize;
  return p;
}

void countHook(lua_State* L, lua_Debug*) {
  Impl& self = Impl::of(L);
  self.steps += 1000;
  if (self.steps > MissionLua::kStepLimit) luaL_error(L, "the script ran too long (over %d steps in one call): a loop that never ends?", static_cast<int>(MissionLua::kStepLimit));
}

// --- A base library a script can't tell machines apart with ---------------------
// tostring without addresses.
int l_tostring(lua_State* L) {
  luaL_checkany(L, 1);
  switch (lua_type(L, 1)) {
    case LUA_TTABLE:
      if (luaL_getmetafield(L, 1, "__tostring") != LUA_TNIL) {
        lua_pushvalue(L, 1);
        lua_call(L, 1, 1);
        if (!lua_isstring(L, -1)) return luaL_error(L, "'__tostring' must return a string");
        return 1;
      }
      lua_pushliteral(L, "table");
      return 1;
    case LUA_TFUNCTION: lua_pushliteral(L, "function"); return 1;
    case LUA_TUSERDATA:
    case LUA_TLIGHTUSERDATA: lua_pushliteral(L, "userdata"); return 1;
    case LUA_TTHREAD: lua_pushliteral(L, "thread"); return 1;
    default: luaL_tolstring(L, 1, nullptr); return 1;
  }
}
int l_log(lua_State* L) {
  std::string line;
  const int n = lua_gettop(L);
  for (int i = 1; i <= n; ++i) {
    lua_pushcfunction(L, l_tostring);
    lua_pushvalue(L, i);
    lua_call(L, 1, 1);
    if (i > 1) line += "  ";
    line += lua_tostring(L, -1);
    lua_pop(L, 1);
  }
  PNR_INFO_CH(Script, "%s", line.c_str());
  return 0;
}
// pairs in a fixed order: numbers rising, then strings in byte order.
struct Key {
  bool text = false;
  bool whole = true;
  lua_Integer i = 0;
  lua_Number n = 0;
  std::string s;
};
int l_pairsNext(lua_State* L) {
  // upvalues: the table, the key list, the position
  lua_Integer at = lua_tointeger(L, lua_upvalueindex(3));
  const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, lua_upvalueindex(2)));
  while (++at <= count) {
    lua_rawgeti(L, lua_upvalueindex(2), at);
    lua_pushvalue(L, -1);
    lua_rawget(L, lua_upvalueindex(1));
    if (!lua_isnil(L, -1)) {  // (a key taken out during the walk is passed over)
      lua_pushinteger(L, at);
      lua_replace(L, lua_upvalueindex(3));
      return 2;
    }
    lua_pop(L, 2);
  }
  lua_pushnil(L);
  return 1;
}
int l_pairs(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  std::vector<Key> keys;
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    lua_pop(L, 1);
    Key k;
    if (lua_type(L, -1) == LUA_TNUMBER) {
      k.whole = lua_isinteger(L, -1) != 0;
      if (k.whole) k.i = lua_tointeger(L, -1);
      k.n = lua_tonumber(L, -1);
    } else if (lua_type(L, -1) == LUA_TSTRING) {
      size_t len = 0;
      const char* s = lua_tolstring(L, -1, &len);
      k.text = true;
      k.s.assign(s, len);
    } else {
      return luaL_error(L, "pairs: a table walked with pairs may only have numbers and strings as keys (found a %s)", luaL_typename(L, -1));
    }
    keys.push_back(std::move(k));
  }
  std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) {
    if (a.text != b.text) return !a.text;
    if (a.text) return a.s < b.s;
    if (a.whole && b.whole) return a.i < b.i;
    return a.n < b.n;
  });
  lua_pushvalue(L, 1);
  lua_createtable(L, static_cast<int>(keys.size()), 0);
  for (size_t i = 0; i < keys.size(); ++i) {
    if (keys[i].text) lua_pushlstring(L, keys[i].s.data(), keys[i].s.size());
    else if (keys[i].whole) lua_pushinteger(L, keys[i].i);
    else lua_pushnumber(L, keys[i].n);
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  lua_pushinteger(L, 0);
  lua_pushcclosure(L, l_pairsNext, 3);
  return 1;
}
// setmetatable, but no finalisers and no weak tables: both depend on when the collector runs.
int l_setmetatable(lua_State* L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_argexpected(L, lua_isnil(L, 2) || lua_istable(L, 2), 2, "nil or table");
  if (lua_istable(L, 2)) {
    for (const char* banned : {"__gc", "__mode", "__close"}) {
      lua_getfield(L, 2, banned);
      if (!lua_isnil(L, -1)) return luaL_error(L, "setmetatable: '%s' is not allowed in mission scripts", banned);
      lua_pop(L, 1);
    }
  }
  lua_settop(L, 2);
  lua_setmetatable(L, 1);
  return 1;
}

// --- math: whole-number and exact operations only -----------------------------------
int m_floor(lua_State* L) {
  if (lua_isinteger(L, 1)) lua_settop(L, 1);
  else {
    const lua_Number f = std::floor(luaL_checknumber(L, 1));
    lua_Integer n;
    if (lua_numbertointeger(f, &n)) lua_pushinteger(L, n);
    else lua_pushnumber(L, f);
  }
  return 1;
}
int m_ceil(lua_State* L) {
  if (lua_isinteger(L, 1)) lua_settop(L, 1);
  else {
    const lua_Number f = std::ceil(luaL_checknumber(L, 1));
    lua_Integer n;
    if (lua_numbertointeger(f, &n)) lua_pushinteger(L, n);
    else lua_pushnumber(L, f);
  }
  return 1;
}
int m_abs(lua_State* L) {
  if (lua_isinteger(L, 1)) {
    const lua_Integer n = lua_tointeger(L, 1);
    lua_pushinteger(L, n < 0 ? static_cast<lua_Integer>(0u - static_cast<lua_Unsigned>(n)) : n);
  } else {
    lua_pushnumber(L, std::fabs(luaL_checknumber(L, 1)));
  }
  return 1;
}
int m_sqrt(lua_State* L) {
  lua_pushnumber(L, std::sqrt(luaL_checknumber(L, 1)));  // correctly rounded everywhere
  return 1;
}
int m_minmax(lua_State* L) {
  const bool wantMax = lua_toboolean(L, lua_upvalueindex(1)) != 0;
  const int n = lua_gettop(L);
  luaL_argcheck(L, n >= 1, 1, "number expected");
  int best = 1;
  for (int i = 1; i <= n; ++i) {
    luaL_checknumber(L, i);
    if (wantMax ? lua_compare(L, best, i, LUA_OPLT) : lua_compare(L, i, best, LUA_OPLT)) best = i;
  }
  lua_pushvalue(L, best);
  return 1;
}
int m_tointeger(lua_State* L) {
  lua_Integer n;
  if (lua_isinteger(L, 1)) lua_settop(L, 1);
  else if (lua_type(L, 1) == LUA_TNUMBER && lua_numbertointeger(lua_tonumber(L, 1), &n)) lua_pushinteger(L, n);
  else lua_pushnil(L);
  return 1;
}

// --- The game ---------------------------------------------------------------------------
int indexByName(lua_State* L, int idx, const char* what, size_t count, const std::function<const std::string&(size_t)>& name) {
  if (lua_isinteger(L, idx)) return static_cast<int>(lua_tointeger(L, idx));
  if (lua_type(L, idx) == LUA_TSTRING) {
    const std::string want = lua_tostring(L, idx);
    for (size_t i = 0; i < count; ++i)
      if (name(i) == want) return static_cast<int>(i);
    return luaL_error(L, "there is no %s called '%s'", what, want.c_str());
  }
  return luaL_error(L, "%s: a name or a number expected, got %s", what, luaL_typename(L, idx));
}

// Reads the field on top of the stack into a number for its slot.
int32_t fieldValue(lua_State* L, Impl& self, const script::Field& f);

int32_t fieldValue(lua_State* L, Impl& self, const script::Field& f) {
  const ScenarioScript& s = self.sim.script();
  const int top = lua_gettop(L);
  switch (f.kind) {
    case script::Kind::Player:
    case script::Kind::PlayerOrAny:
      if (lua_type(L, top) == LUA_TSTRING) {
        const std::string v = lua_tostring(L, top);
        if (v == "all" || v == "everyone" || v == "any") return ANY_PLAYER;
        return luaL_error(L, "%s: a side from 0 to 7 (or \"all\") expected", f.key);
      }
      return static_cast<int32_t>(luaL_checkinteger(L, top));
    case script::Kind::Area:
    case script::Kind::AreaOrMap:
      return indexByName(L, top, "area", s.areas.size(), [&](size_t i) -> const std::string& { return s.areas[i].name; });
    case script::Kind::Trigger:
      return indexByName(L, top, "trigger", s.triggers.size(), [&](size_t i) -> const std::string& { return s.triggers[i].name; });
    case script::Kind::Phase:
      return indexByName(L, top, "phase", s.phases.size(), [&](size_t i) -> const std::string& { return s.phases[i].name; });
    case script::Kind::Wave:
      return indexByName(L, top, "wave", s.waves.size(), [&](size_t i) -> const std::string& { return s.waves[i].name; });
    case script::Kind::Path:
      return indexByName(L, top, "path", s.paths.size(), [&](size_t i) -> const std::string& { return s.paths[i].name; });
    case script::Kind::Goal:
      return indexByName(L, top, "goal", s.goals.size(), [&](size_t i) -> const std::string& { return s.goals[i].text; });
    case script::Kind::UnitType: {
      if (lua_isinteger(L, top)) return static_cast<int32_t>(lua_tointeger(L, top));
      const std::string id = luaL_checkstring(L, top);
      if (id == "any") return -1;
      const int t = self.sim.types().find(id);
      if (t < 0) return luaL_error(L, "there is no unit type '%s'", id.c_str());
      return t;
    }
    case script::Kind::Bool:
      return lua_isboolean(L, top) ? lua_toboolean(L, top) : static_cast<int32_t>(luaL_checkinteger(L, top));
    case script::Kind::Choice: {
      if (lua_type(L, top) != LUA_TSTRING) return static_cast<int32_t>(luaL_checkinteger(L, top));
      const std::string want = lua_tostring(L, top);
      int32_t n = f.min;
      const char* p = f.choices;
      while (*p) {
        const char* e = std::strchr(p, '|');
        const std::string one = e ? std::string(p, e) : std::string(p);
        if (one == want) return n;
        ++n;
        if (!e) break;
        p = e + 1;
      }
      return luaL_error(L, "%s: '%s' is not one of %s", f.key, want.c_str(), f.choices);
    }
    case script::Kind::Order: {
      if (lua_type(L, top) != LUA_TSTRING) return static_cast<int32_t>(luaL_checkinteger(L, top));
      return orderFromId(lua_tostring(L, top));
    }
    default:
      return static_cast<int32_t>(luaL_checkinteger(L, top));
  }
}

// Fills a condition or action from the table at index 1 by the registry's field names.
template <class T>
void readFields(lua_State* L, Impl& self, const script::Entry& e, T& out) {
  if (lua_isnoneornil(L, 1)) return;
  luaL_checktype(L, 1, LUA_TTABLE);
  // A misspelt field is a mistake worth hearing about.
  lua_pushnil(L);
  while (lua_next(L, 1)) {
    lua_pop(L, 1);
    if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "%s: fields are given by name, as in %s{player = 0}", e.id, e.id);
    const std::string key = lua_tostring(L, -1);
    if (!e.fieldByKey(key) && !(key == "not" && !e.action)) {
      std::string known;
      for (const script::Field& f : e.fields) known += std::string(known.empty() ? "" : ", ") + f.key;
      luaL_error(L, "%s has no field '%s' (it has: %s)", e.id, key.c_str(), known.empty() ? "none" : known.c_str());
    }
  }
  for (const script::Field& f : e.fields) {
    lua_getfield(L, 1, f.key);
    if (!lua_isnil(L, -1)) {
      if (f.slot == script::Slot::Text) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, -1, &len);
        out.text.assign(s, len);
        lua_pop(L, 1);
      } else {
        script::set(out, f.slot, fieldValue(L, self, f));
      }
    }
    lua_pop(L, 1);
  }
}

int l_action(lua_State* L) {
  Impl& self = Impl::of(L);
  const script::Entry& e = script::actions()[static_cast<size_t>(lua_tointeger(L, lua_upvalueindex(1)))];
  if (self.loading)
    return luaL_error(L, "%s: the world can't be changed while the script is being loaded; do it from on_start() or another handler", e.id);
  Action a = script::makeAction(static_cast<ActionType>(e.type));
  readFields(L, self, e, a);
  self.sim.luaPerform(a);
  return 0;
}
int l_condition(lua_State* L) {
  Impl& self = Impl::of(L);
  const script::Entry& e = script::conditions()[static_cast<size_t>(lua_tointeger(L, lua_upvalueindex(1)))];
  Condition c = script::makeCondition(static_cast<CondType>(e.type));
  readFields(L, self, e, c);
  if (lua_istable(L, 1)) {
    lua_getfield(L, 1, "not");
    c.negate = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
  }
  lua_pushboolean(L, self.sim.luaTest(c));
  return 1;
}

int g_tick(lua_State* L) {
  lua_pushinteger(L, Impl::of(L).sim.tickCount());
  return 1;
}
int g_seconds(lua_State* L) {
  lua_pushinteger(L, Impl::of(L).sim.tickCount() / TICKS_PER_SECOND);
  return 1;
}
int g_phase(lua_State* L) {
  const Simulation& sim = Impl::of(L).sim;
  const int32_t p = sim.missionPhase();
  lua_pushinteger(L, p);
  if (p >= 0 && p < static_cast<int32_t>(sim.script().phases.size())) lua_pushstring(L, sim.script().phases[static_cast<size_t>(p)].name.c_str());
  else lua_pushnil(L);
  return 2;
}
int g_difficulty(lua_State* L) {
  static const char* kNames[3] = {"easy", "normal", "hard"};
  const uint8_t d = Impl::of(L).sim.missionDifficulty();
  lua_pushinteger(L, d);
  lua_pushstring(L, kNames[std::min<uint8_t>(d, 2)]);
  return 2;
}
int g_flag(lua_State* L) {
  lua_pushinteger(L, Impl::of(L).sim.flag(luaL_checkstring(L, 1)));
  return 1;
}
int g_counter(lua_State* L) {
  lua_pushinteger(L, Impl::of(L).sim.counter(luaL_checkstring(L, 1)));
  return 1;
}
int g_named(lua_State* L) {
  lua_pushinteger(L, static_cast<lua_Integer>(Impl::of(L).sim.named(luaL_checkstring(L, 1)).size()));
  return 1;
}
int g_supplies(lua_State* L) {
  const lua_Integer p = luaL_checkinteger(L, 1);
  luaL_argcheck(L, p >= 0 && p < MAX_PLAYERS, 1, "a side from 0 to 7");
  lua_pushinteger(L, Impl::of(L).sim.playerState(static_cast<uint8_t>(p)).credits);
  return 1;
}
// count(side[, area[, unit type]]): how many units; side may be "all".
int g_count(lua_State* L) {
  Impl& self = Impl::of(L);
  script::Field side{script::Slot::Player, script::Kind::PlayerOrAny, "player", "", "", 0, 0, 255, "", false};
  script::Field area{script::Slot::Area, script::Kind::AreaOrMap, "area", "", "", -1, -1, INT32_MAX, "", true};
  script::Field unit{script::Slot::UnitType, script::Kind::UnitType, "unit", "", "", -1, -1, INT32_MAX, "", true};
  lua_settop(L, 3);
  lua_pushvalue(L, 1);
  const int32_t p = fieldValue(L, self, side);
  lua_pop(L, 1);
  int32_t a = -1, u = -1;
  if (!lua_isnil(L, 2)) {
    lua_pushvalue(L, 2);
    a = fieldValue(L, self, area);
    lua_pop(L, 1);
  }
  if (!lua_isnil(L, 3)) {
    lua_pushvalue(L, 3);
    u = fieldValue(L, self, unit);
    lua_pop(L, 1);
  }
  lua_pushinteger(L, self.sim.countUnits(static_cast<uint8_t>(p), a, u));
  return 1;
}
int g_objective(lua_State* L) {
  static const char* kStates[4] = {"open", "done", "failed", "hidden"};
  const Simulation& sim = Impl::of(L).sim;
  const lua_Integer i = luaL_checkinteger(L, 1);
  if (i < 0 || i >= static_cast<lua_Integer>(sim.objectives().size())) lua_pushnil(L);
  else lua_pushstring(L, kStates[sim.objectives()[static_cast<size_t>(i)].state & 3]);
  return 1;
}
int g_goal(lua_State* L) {
  static const char* kStates[4] = {"waiting", "open", "done", "failed"};
  Impl& self = Impl::of(L);
  script::Field goal{script::Slot::Value, script::Kind::Goal, "goal", "", "", 0, 0, INT32_MAX, "", false};
  lua_settop(L, 1);
  const int32_t g = fieldValue(L, self, goal);
  lua_pushstring(L, kStates[self.sim.goalState(g) & 3]);
  return 1;
}
// random(n): 1..n; random(a, b): a..b. The simulation's dice: a replay rolls the same.
int g_random(lua_State* L) {
  Impl& self = Impl::of(L);
  lua_Integer lo = 1, hi;
  if (lua_gettop(L) >= 2) {
    lo = luaL_checkinteger(L, 1);
    hi = luaL_checkinteger(L, 2);
  } else {
    hi = luaL_checkinteger(L, 1);
  }
  luaL_argcheck(L, lo <= hi && hi - lo < INT32_MAX, 1, "an empty or too wide range");
  lua_pushinteger(L, lo + self.sim.luaRandom(static_cast<uint32_t>(hi - lo + 1)));
  return 1;
}
// after(seconds, "function name"[, a number or a text]): calls the function later. Survives saving.
int g_after(lua_State* L) {
  Impl& self = Impl::of(L);
  const lua_Number secs = luaL_checknumber(L, 1);
  luaL_argcheck(L, secs >= 0 && secs <= 86400, 1, "seconds from 0 to 86400");
  Impl::Timer t;
  t.fn = luaL_checkstring(L, 2);
  t.tick = self.sim.tickCount() + static_cast<uint32_t>(secs * TICKS_PER_SECOND);
  if (lua_type(L, 3) == LUA_TNUMBER) {
    t.kind = 1;
    t.num = lua_tonumber(L, 3);
  } else if (lua_type(L, 3) == LUA_TSTRING) {
    t.kind = 2;
    t.text = lua_tostring(L, 3);
  } else if (!lua_isnoneornil(L, 3)) {
    return luaL_error(L, "after: the argument passed on may be a number or a text (keep the rest in `state`)");
  }
  luaL_argcheck(L, self.timers.size() < 4096, 1, "too many timers");
  self.timers.push_back(std::move(t));
  return 0;
}

// --- `state`: plain values and tables, written in a fixed order ------------------------
bool writeValue(lua_State* L, int idx, ByteWriter& w, int depth, std::string& error) {
  idx = lua_absindex(L, idx);
  switch (lua_type(L, idx)) {
    case LUA_TNIL: w.u8(0); return true;
    case LUA_TBOOLEAN: w.u8(lua_toboolean(L, idx) ? 2 : 1); return true;
    case LUA_TNUMBER:
      if (lua_isinteger(L, idx)) {
        w.u8(3);
        w.pod<int64_t>(lua_tointeger(L, idx));
      } else {
        w.u8(4);
        w.pod<double>(lua_tonumber(L, idx));
      }
      return true;
    case LUA_TSTRING: {
      size_t len = 0;
      const char* s = lua_tolstring(L, idx, &len);
      w.u8(5);
      w.str(std::string(s, len));
      return true;
    }
    case LUA_TTABLE: {
      if (depth > 24) {
        error = "`state` is nested too deeply (or holds itself)";
        return false;
      }
      w.u8(6);
      // In the order pairs gives: fixed.
      lua_pushcfunction(L, l_pairs);
      lua_pushvalue(L, idx);
      if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        error = std::string("`state`: ") + lua_tostring(L, -1);
        lua_pop(L, 1);
        return false;
      }
      const int iter = lua_gettop(L);
      uint32_t count = 0;
      const size_t countAt = w.bytes().size();
      w.u32(0);
      for (;;) {
        lua_pushvalue(L, iter);
        lua_call(L, 0, 2);
        if (lua_isnil(L, -2)) {
          lua_pop(L, 2);
          break;
        }
        const bool ok = writeValue(L, -2, w, depth + 1, error) && writeValue(L, -1, w, depth + 1, error);
        lua_pop(L, 2);
        if (!ok) {
          lua_pop(L, 1);
          return false;
        }
        ++count;
      }
      lua_pop(L, 1);
      for (int i = 0; i < 4; ++i) w.bytes()[countAt + static_cast<size_t>(i)] = static_cast<uint8_t>(count >> (i * 8));
      return true;
    }
    default:
      error = std::string("`state` may hold numbers, texts, true/false and tables, not a ") + luaL_typename(L, idx);
      return false;
  }
}
bool readValue(lua_State* L, ByteReader& r, int depth) {
  switch (r.u8()) {
    case 0: lua_pushnil(L); return r.ok();
    case 1: lua_pushboolean(L, 0); return r.ok();
    case 2: lua_pushboolean(L, 1); return r.ok();
    case 3: lua_pushinteger(L, r.pod<int64_t>()); return r.ok();
    case 4: lua_pushnumber(L, r.pod<double>()); return r.ok();
    case 5: {
      const std::string s = r.str();
      lua_pushlstring(L, s.data(), s.size());
      return r.ok();
    }
    case 6: {
      if (depth > 24) return false;
      const uint32_t n = r.u32();
      if (!r.ok() || n > 1u << 22) return false;
      lua_newtable(L);
      for (uint32_t i = 0; i < n; ++i) {
        if (!readValue(L, r, depth + 1)) return false;
        if (!readValue(L, r, depth + 1)) {
          lua_pop(L, 1);
          return false;
        }
        if (lua_isnil(L, -2)) lua_pop(L, 2);
        else lua_rawset(L, -3);
      }
      return r.ok();
    }
    default:
      return false;
  }
}

void openSandbox(lua_State* L) {
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);
  // Out: anything that loads code, reads the machine, or shows an address.
  for (const char* gone : {"dofile", "loadfile", "load", "loadstring", "collectgarbage", "next", "require", "print"}) {
    lua_pushnil(L);
    lua_setglobal(L, gone);
  }
  lua_getglobal(L, "string");
  lua_pushnil(L);
  lua_setfield(L, -2, "dump");
  lua_pop(L, 1);
  lua_register(L, "tostring", l_tostring);
  lua_register(L, "pairs", l_pairs);
  lua_register(L, "setmetatable", l_setmetatable);
  lua_register(L, "log", l_log);
  lua_register(L, "print", l_log);
  // math
  lua_newtable(L);
  const luaL_Reg math[] = {{"floor", m_floor}, {"ceil", m_ceil}, {"abs", m_abs}, {"sqrt", m_sqrt}, {"tointeger", m_tointeger}, {nullptr, nullptr}};
  luaL_setfuncs(L, math, 0);
  lua_pushboolean(L, 0);
  lua_pushcclosure(L, m_minmax, 1);
  lua_setfield(L, -2, "min");
  lua_pushboolean(L, 1);
  lua_pushcclosure(L, m_minmax, 1);
  lua_setfield(L, -2, "max");
  lua_pushcfunction(L, g_random);
  lua_setfield(L, -2, "random");
  lua_pushinteger(L, LUA_MAXINTEGER);
  lua_setfield(L, -2, "maxinteger");
  lua_pushinteger(L, LUA_MININTEGER);
  lua_setfield(L, -2, "mininteger");
  lua_pushnumber(L, static_cast<lua_Number>(HUGE_VAL));
  lua_setfield(L, -2, "huge");
  lua_setglobal(L, "math");
  // The game.
  const luaL_Reg game[] = {{"tick", g_tick},       {"seconds", g_seconds}, {"phase", g_phase},   {"difficulty", g_difficulty},
                           {"flag", g_flag},       {"counter", g_counter}, {"named", g_named},   {"supplies", g_supplies},
                           {"count", g_count},     {"objective", g_objective}, {"goal", g_goal}, {"random", g_random},
                           {"after", g_after},     {nullptr, nullptr}};
  lua_pushglobaltable(L);
  luaL_setfuncs(L, game, 0);
  lua_pop(L, 1);
  // Every action a function, every condition a function of `is`.
  for (size_t i = 0; i < script::actions().size(); ++i) {
    lua_pushinteger(L, static_cast<lua_Integer>(i));
    lua_pushcclosure(L, l_action, 1);
    lua_setglobal(L, script::actions()[i].id);
  }
  lua_newtable(L);
  for (size_t i = 0; i < script::conditions().size(); ++i) {
    lua_pushinteger(L, static_cast<lua_Integer>(i));
    lua_pushcclosure(L, l_condition, 1);
    lua_setfield(L, -2, script::conditions()[i].id);
  }
  lua_setglobal(L, "is");
  lua_newtable(L);
  lua_setglobal(L, "state");
}
}  // namespace

MissionLua::MissionLua(Simulation& sim) : impl_(std::make_unique<Impl>(sim)) {
  impl_->L = lua_newstate(allocate, impl_.get());
  if (!impl_->L) return;
  *static_cast<Impl**>(lua_getextraspace(impl_->L)) = impl_.get();
  lua_sethook(impl_->L, countHook, LUA_MASKCOUNT, 1000);
  // Opening the libraries can run out of memory only if the limit is absurd: guard anyway.
  lua_pushcfunction(impl_->L, [](lua_State* L) {
    openSandbox(L);
    return 0;
  });
  if (lua_pcall(impl_->L, 0, 0, 0) != LUA_OK) {
    impl_->fail(std::string("could not set up: ") + lua_tostring(impl_->L, -1));
    lua_close(impl_->L);
    impl_->L = nullptr;
  }
}

MissionLua::~MissionLua() {
  if (impl_->L) lua_close(impl_->L);
}

bool MissionLua::compiles(const std::string& source, std::string& error) {
  lua_State* L = luaL_newstate();
  if (!L) return false;
  const bool ok = luaL_loadbufferx(L, source.data(), source.size(), "=mission", "t") == LUA_OK;
  if (!ok) error = lua_tostring(L, -1);
  lua_close(L);
  return ok;
}

bool MissionLua::load(const std::string& source, std::string& error) {
  impl_->ok = false;
  if (!impl_->L) {
    error = "Lua could not start";
    return false;
  }
  lua_State* L = impl_->L;
  if (luaL_loadbufferx(L, source.data(), source.size(), "=mission", "t") != LUA_OK) {
    error = lua_tostring(L, -1);
    lua_pop(L, 1);
    impl_->fail(error);
    return false;
  }
  impl_->loading = true;
  const size_t before = impl_->errors.size();
  const bool ran = impl_->pcall(0, 0, "loading the script");
  impl_->loading = false;
  if (!ran) {
    error = impl_->errors.size() > before ? impl_->errors.back() : "the script failed";
    return false;
  }
  impl_->ok = true;
  return true;
}

bool MissionLua::ready() const { return impl_->ok; }

void MissionLua::step() {
  if (!impl_->ok) return;
  PNR_PROFILE("Sim: Lua");
  if (!impl_->started) {
    impl_->started = true;
    impl_->callGlobal("on_start", 0);
  }
  // Timers that are due, in the order they were set.
  const uint32_t now = impl_->sim.tickCount();
  for (size_t i = 0; i < impl_->timers.size();) {
    if (impl_->timers[i].tick > now) {
      ++i;
      continue;
    }
    const Impl::Timer t = impl_->timers[i];
    impl_->timers.erase(impl_->timers.begin() + static_cast<long>(i));
    int args = 0;
    if (t.kind == 1) {
      lua_Integer whole;
      if (lua_numbertointeger(t.num, &whole) && static_cast<double>(whole) == t.num) lua_pushinteger(impl_->L, whole);
      else lua_pushnumber(impl_->L, t.num);
      args = 1;
    } else if (t.kind == 2) {
      lua_pushstring(impl_->L, t.text.c_str());
      args = 1;
    }
    lua_getglobal(impl_->L, t.fn.c_str());
    if (!lua_isfunction(impl_->L, -1)) {
      lua_pop(impl_->L, 1 + args);
      impl_->fail("after: there is no function '" + t.fn + "'");
      continue;
    }
    lua_insert(impl_->L, -1 - args);
    impl_->pcall(args, 0, t.fn.c_str());
  }
  if (now % TICKS_PER_SECOND == 0) impl_->callGlobal("on_second", 0);
}

void MissionLua::deliver(const std::vector<SimEvent>& events) {
  if (!impl_->ok || !impl_->started) return;
  lua_State* L = impl_->L;
  const size_t n = events.size();  // handlers may add events: those are not handed back to them
  for (size_t i = 0; i < n; ++i) {
    const SimEvent ev = events[i];
    switch (ev.type) {
      case EventType::UnitDied:
        lua_pushstring(L, ev.unitType < impl_->sim.types().size() ? impl_->sim.types().get(ev.unitType).id.c_str() : "");
        lua_pushinteger(L, ev.owner);
        lua_pushinteger(L, ev.at.x >> LEPTON_SHIFT);
        lua_pushinteger(L, ev.at.y >> LEPTON_SHIFT);
        impl_->callGlobal("on_unit_died", 4);
        break;
      case EventType::PhaseChanged:
        lua_pushinteger(L, ev.amount);
        lua_pushstring(L, ev.text.c_str());
        impl_->callGlobal("on_phase", 2);
        break;
      case EventType::CountdownDone:
        lua_pushstring(L, ev.text.c_str());
        impl_->callGlobal("on_countdown", 1);
        break;
      case EventType::WaveArrived:
        lua_pushstring(L, ev.text.c_str());
        lua_pushinteger(L, ev.amount);
        lua_pushinteger(L, ev.owner);
        impl_->callGlobal("on_wave", 3);
        break;
      case EventType::PlayerDefeated:
        lua_pushinteger(L, ev.owner);
        impl_->callGlobal("on_defeated", 1);
        break;
      case EventType::ObjectivesChanged:
        impl_->callGlobal("on_objectives", 0);
        break;
      default:
        break;
    }
  }
}

void MissionLua::call(const std::string& fn, int32_t value) {
  if (!impl_->ok) return;
  lua_getglobal(impl_->L, fn.c_str());
  if (!lua_isfunction(impl_->L, -1)) {
    lua_pop(impl_->L, 1);
    impl_->fail("Call script: there is no function '" + fn + "'");
    return;
  }
  lua_pushinteger(impl_->L, value);
  impl_->pcall(1, 0, fn.c_str());
}

bool MissionLua::ask(const std::string& fn) {
  if (!impl_->ok) return false;
  lua_State* L = impl_->L;
  lua_getglobal(L, fn.c_str());
  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  if (!impl_->pcall(0, 1, fn.c_str())) return false;
  const bool yes = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return yes;
}

std::string MissionLua::run(const std::string& code) {
  if (!impl_->L) return "Lua is not running";
  lua_State* L = impl_->L;
  // As an expression first, so `lua.run counter("x")` shows a value.
  const std::string expr = "return " + code;
  if (luaL_loadbufferx(L, expr.data(), expr.size(), "=console", "t") != LUA_OK) {
    lua_pop(L, 1);
    if (luaL_loadbufferx(L, code.data(), code.size(), "=console", "t") != LUA_OK) {
      std::string err = lua_tostring(L, -1);
      lua_pop(L, 1);
      return err;
    }
  }
  const size_t before = impl_->errors.size();
  if (!impl_->pcall(0, 1, "console")) return impl_->errors.size() > before ? impl_->errors.back() : "error";
  lua_pushcfunction(L, l_tostring);
  lua_insert(L, -2);
  lua_call(L, 1, 1);
  std::string out = lua_tostring(L, -1);
  lua_pop(L, 1);
  return out;
}

void MissionLua::save(ByteWriter& w) const {
  w.u8(impl_->started ? 1 : 0);
  w.u32(static_cast<uint32_t>(impl_->timers.size()));
  for (const Impl::Timer& t : impl_->timers) {
    w.u32(t.tick);
    w.str(t.fn);
    w.u8(t.kind);
    w.pod(t.num);
    w.str(t.text);
  }
  ByteWriter state;
  std::string error;
  bool ok = false;
  if (impl_->L && impl_->ok) {
    lua_getglobal(impl_->L, "state");
    ok = writeValue(impl_->L, -1, state, 0, error);
    lua_pop(impl_->L, 1);
    if (!ok) impl_->fail("saving: " + error + " (it was not saved)");
  }
  w.u8(ok ? 1 : 0);
  if (ok) w.podVector(state.bytes());
}

bool MissionLua::restore(ByteReader& r) {
  impl_->started = r.u8() != 0;
  impl_->timers.clear();
  const uint32_t n = r.u32();
  if (!r.ok() || n > 4096) return false;
  for (uint32_t i = 0; i < n; ++i) {
    Impl::Timer t;
    t.tick = r.u32();
    t.fn = r.str();
    t.kind = r.u8();
    t.num = r.pod<double>();
    t.text = r.str();
    impl_->timers.push_back(std::move(t));
  }
  if (r.u8() != 0) {
    const std::vector<uint8_t> bytes = r.podVector<uint8_t>();
    if (!r.ok()) return false;
    if (impl_->L && impl_->ok) {
      ByteReader sr(bytes);
      if (!readValue(impl_->L, sr, 0)) {
        lua_settop(impl_->L, 0);
        return false;
      }
      lua_setglobal(impl_->L, "state");
    }
  }
  return r.ok();
}

const std::vector<std::string>& MissionLua::errors() const { return impl_->errors; }
size_t MissionLua::memory() const { return impl_->mem; }
uint64_t MissionLua::stepsUsed() const { return impl_->lastSteps; }

}  // namespace pioneer

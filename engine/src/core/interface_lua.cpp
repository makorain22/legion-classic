// Pioneer Engine - Lua for the interface (see the header).
#include "pioneer/core/interface_lua.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

// Lua is built as C++ (engine/src/lua/pioneer_lua.cpp): its headers are C++ here too.
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <nlohmann/json.hpp>

#include "pioneer/core/log.hpp"

namespace pioneer {

struct InterfaceLua::Impl {
  lua_State* L = nullptr;
  size_t mem = 0;
  int64_t steps = 0;
  std::map<std::string, std::function<Value()>> provided;
  std::function<std::string(const std::string&)> console, translate;
  struct PanelRec {
    Panel info;
    int draw = LUA_NOREF;
    int strikes = 0;
  };
  std::vector<PanelRec> panels;
  struct Handler {
    std::string event;
    int fn = LUA_NOREF;
    int strikes = 0;
  };
  std::vector<Handler> handlers;
  std::vector<std::string> errors;
  std::string loading;                  // the script being run
  InterfaceWidgets* widgets = nullptr;  // set while panels draw
  size_t scripts = 0;
  std::string savedKeep;                // `saved` across a reset

  void error(const std::string& what) {
    if (!errors.empty() && errors.back() == what) return;
    if (errors.size() >= 20) errors.erase(errors.begin());
    errors.push_back(what);
    PNR_WARN("Interface script: %s", what.c_str());
  }
};

namespace {
using Impl = InterfaceLua::Impl;

Impl& self(lua_State* L) { return **static_cast<Impl**>(lua_getextraspace(L)); }

void* allocate(void* ud, void* ptr, size_t osize, size_t nsize) {
  Impl* im = static_cast<Impl*>(ud);
  const size_t old = ptr ? osize : 0;
  if (nsize == 0) {
    im->mem -= std::min(im->mem, old);
    std::free(ptr);
    return nullptr;
  }
  if (nsize > old && im->mem + (nsize - old) > InterfaceLua::kMemoryLimit) return nullptr;
  void* out = std::realloc(ptr, nsize);
  if (out) im->mem = im->mem - std::min(im->mem, old) + nsize;
  return out;
}

void countHook(lua_State* L, lua_Debug*) {
  Impl& im = self(L);
  im.steps -= 1000;
  if (im.steps < 0) luaL_error(L, "the script ran too long and was stopped");
}

// Runs the function on the stack top with nargs arguments under the limits.
bool protectedCall(Impl& im, int nargs, int nresults, const std::string& where) {
  im.steps = InterfaceLua::kStepLimit;
  if (lua_pcall(im.L, nargs, nresults, 0) == LUA_OK) return true;
  const char* msg = lua_tostring(im.L, -1);
  im.error(where + ": " + (msg ? msg : "error"));
  lua_pop(im.L, 1);
  return false;
}

void push(lua_State* L, const InterfaceLua::Value& v) {
  if (const double* d = std::get_if<double>(&v)) {
    // Whole numbers arrive as integers, so "side " .. game.player reads "side 1", not "side 1.0".
    if (*d == std::floor(*d) && std::fabs(*d) < 9.0e15) lua_pushinteger(L, static_cast<lua_Integer>(*d));
    else lua_pushnumber(L, *d);
  }
  else if (const std::string* s = std::get_if<std::string>(&v)) lua_pushlstring(L, s->data(), s->size());
  else lua_pushboolean(L, std::get<bool>(v));
}

std::string textArg(lua_State* L, int i) {
  size_t n = 0;
  const char* s = luaL_tolstring(L, i, &n);
  std::string out(s, n);
  lua_pop(L, 1);
  return out;
}

InterfaceWidgets& widgets(lua_State* L) {
  Impl& im = self(L);
  if (!im.widgets) luaL_error(L, "ui functions work only inside a panel's draw function");
  return *im.widgets;
}

// --- ui.* --------------------------------------------------------------------
template <InterfaceWidgets::Style kStyle>
int uiText(lua_State* L) {
  widgets(L).text(textArg(L, 1), kStyle);
  return 0;
}
int uiButton(lua_State* L) {
  lua_pushboolean(L, widgets(L).button(textArg(L, 1)));
  return 1;
}
int uiCheckbox(lua_State* L) {
  bool v = lua_toboolean(L, 2) != 0;
  const bool ch = widgets(L).checkbox(textArg(L, 1), v);
  lua_pushboolean(L, v);
  lua_pushboolean(L, ch);
  return 2;
}
int uiSlider(lua_State* L) {
  double v = luaL_checknumber(L, 2);
  const double lo = luaL_optnumber(L, 3, 0.0), hi = luaL_optnumber(L, 4, 1.0);
  const bool ch = widgets(L).slider(textArg(L, 1), v, lo, hi);
  lua_pushnumber(L, std::clamp(v, std::min(lo, hi), std::max(lo, hi)));
  lua_pushboolean(L, ch);
  return 2;
}
int uiInput(lua_State* L) {
  std::string v = lua_isnoneornil(L, 2) ? std::string() : textArg(L, 2);
  const bool ch = widgets(L).input(textArg(L, 1), v);
  lua_pushlstring(L, v.data(), v.size());
  lua_pushboolean(L, ch);
  return 2;
}
int uiProgress(lua_State* L) {
  const double f = luaL_checknumber(L, 1);
  widgets(L).progress(std::isfinite(f) ? std::clamp(f, 0.0, 1.0) : 0.0, lua_isnoneornil(L, 2) ? std::string() : textArg(L, 2));
  return 0;
}
int uiSameLine(lua_State* L) {
  widgets(L).sameLine();
  return 0;
}
int uiSeparator(lua_State* L) {
  widgets(L).separator();
  return 0;
}
int uiTooltip(lua_State* L) {
  widgets(L).tooltip(textArg(L, 1));
  return 0;
}

// --- Globals -----------------------------------------------------------------
int luaPanel(lua_State* L) {
  Impl& im = self(L);
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_getfield(L, 1, "title");
  if (!lua_isstring(L, -1)) return luaL_error(L, "panel{...} needs a title");
  const std::string title = lua_tostring(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, 1, "draw");
  if (!lua_isfunction(L, -1)) return luaL_error(L, "panel '%s' needs draw = function() ... end", title.c_str());
  for (const Impl::PanelRec& p : im.panels)
    if (p.info.title == title) return luaL_error(L, "there is a panel '%s' already (in %s)", title.c_str(), p.info.script.c_str());
  if (im.panels.size() >= 64) return luaL_error(L, "too many panels (64 at most)");
  Impl::PanelRec rec;
  rec.draw = luaL_ref(L, LUA_REGISTRYINDEX);
  rec.info.title = title;
  rec.info.script = im.loading;
  lua_getfield(L, 1, "open");
  rec.info.open = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  im.panels.push_back(std::move(rec));
  return 0;
}

int luaOn(lua_State* L) {
  Impl& im = self(L);
  const std::string event = luaL_checkstring(L, 1);
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushvalue(L, 2);
  im.handlers.push_back({event, luaL_ref(L, LUA_REGISTRYINDEX), 0});
  return 0;
}

int luaConsole(lua_State* L) {
  Impl& im = self(L);
  if (!im.console) return luaL_error(L, "there is no console here");
  if (!im.loading.empty()) return luaL_error(L, "console() can't be used while a script loads (use it in a panel or a handler)");
  const std::string out = im.console(luaL_checkstring(L, 1));
  lua_pushlstring(L, out.data(), out.size());
  return 1;
}

int luaTr(lua_State* L) {
  Impl& im = self(L);
  const std::string key = luaL_checkstring(L, 1);
  const std::string out = im.translate ? im.translate(key) : key;
  lua_pushlstring(L, out.data(), out.size());
  return 1;
}

int luaLog(lua_State* L) {
  std::string line;
  for (int i = 1; i <= lua_gettop(L); ++i) line += (i > 1 ? " " : "") + textArg(L, i);
  PNR_INFO("[ui script] %s", line.c_str());
  return 0;
}

int gameIndex(lua_State* L) {
  Impl& im = self(L);
  const char* key = luaL_checkstring(L, 2);
  const auto it = im.provided.find(key);
  if (it == im.provided.end()) {
    std::string known;
    for (const auto& [name, fn] : im.provided) known += (known.empty() ? "" : ", ") + name;
    return luaL_error(L, "game.%s: there is no such thing (there are: %s)", key, known.c_str());
  }
  push(L, it->second());
  return 1;
}
int gameNewIndex(lua_State* L) { return luaL_error(L, "game is for reading: an interface script can't change the battle"); }

void openSandbox(lua_State* L) {
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(L, 1);
  // Out: anything that loads code or reaches the machine.
  for (const char* gone : {"dofile", "loadfile", "load", "loadstring", "require", "collectgarbage", "print"}) {
    lua_pushnil(L);
    lua_setglobal(L, gone);
  }
  lua_getglobal(L, "string");
  lua_pushnil(L);
  lua_setfield(L, -2, "dump");
  lua_pop(L, 1);

  static const luaL_Reg kUi[] = {{"text", uiText<InterfaceWidgets::Plain>},     {"heading", uiText<InterfaceWidgets::Heading>},
                                 {"faint", uiText<InterfaceWidgets::Faint>},    {"good", uiText<InterfaceWidgets::Good>},
                                 {"warn", uiText<InterfaceWidgets::Warning>},   {"bad", uiText<InterfaceWidgets::Bad>},
                                 {"mono", uiText<InterfaceWidgets::Mono>},      {"button", uiButton},
                                 {"checkbox", uiCheckbox},                      {"slider", uiSlider},
                                 {"input", uiInput},                            {"progress", uiProgress},
                                 {"same_line", uiSameLine},                     {"separator", uiSeparator},
                                 {"tooltip", uiTooltip},                        {nullptr, nullptr}};
  luaL_newlib(L, kUi);
  lua_setglobal(L, "ui");
  lua_register(L, "panel", luaPanel);
  lua_register(L, "on", luaOn);
  lua_register(L, "console", luaConsole);
  lua_register(L, "tr", luaTr);
  lua_register(L, "log", luaLog);
  lua_register(L, "print", luaLog);

  lua_newtable(L);  // game: an empty table whose reads go to the providers
  lua_newtable(L);
  lua_pushcfunction(L, gameIndex);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, gameNewIndex);
  lua_setfield(L, -2, "__newindex");
  lua_pushboolean(L, 0);
  lua_setfield(L, -2, "__metatable");
  lua_setmetatable(L, -2);
  lua_setglobal(L, "game");

  lua_newtable(L);
  lua_setglobal(L, "saved");
}

// --- `saved` to JSON and back ------------------------------------------------
nlohmann::json toJson(lua_State* L, int index, int depth) {
  using Json = nlohmann::json;
  switch (lua_type(L, index)) {
    case LUA_TBOOLEAN: return lua_toboolean(L, index) != 0;
    case LUA_TNUMBER:
      if (lua_isinteger(L, index)) return static_cast<int64_t>(lua_tointeger(L, index));
      return std::isfinite(lua_tonumber(L, index)) ? Json(lua_tonumber(L, index)) : Json(nullptr);
    case LUA_TSTRING: return std::string(lua_tostring(L, index));
    case LUA_TTABLE: {
      if (depth > 8) return nullptr;
      index = lua_absindex(L, index);
      const lua_Unsigned n = lua_rawlen(L, index);
      // A list (1..n and nothing else) or a table of named things.
      size_t count = 0;
      lua_pushnil(L);
      while (lua_next(L, index)) {
        ++count;
        lua_pop(L, 1);
      }
      if (n > 0 && count == n) {
        Json out = Json::array();
        for (lua_Unsigned i = 1; i <= n; ++i) {
          lua_rawgeti(L, index, static_cast<lua_Integer>(i));
          out.push_back(toJson(L, -1, depth + 1));
          lua_pop(L, 1);
        }
        return out;
      }
      Json out = Json::object();
      lua_pushnil(L);
      while (lua_next(L, index)) {
        if (lua_type(L, -2) == LUA_TSTRING) out[lua_tostring(L, -2)] = toJson(L, -1, depth + 1);
        else if (lua_isinteger(L, -2)) out[std::to_string(lua_tointeger(L, -2))] = toJson(L, -1, depth + 1);
        lua_pop(L, 1);
      }
      return out;
    }
    default: return nullptr;  // functions and the like are not kept
  }
}

void fromJson(lua_State* L, const nlohmann::json& j, int depth) {
  if (j.is_boolean()) lua_pushboolean(L, j.get<bool>());
  else if (j.is_number_integer()) lua_pushinteger(L, static_cast<lua_Integer>(j.get<int64_t>()));
  else if (j.is_number()) lua_pushnumber(L, j.get<double>());
  else if (j.is_string()) lua_pushstring(L, j.get_ref<const std::string&>().c_str());
  else if (j.is_array() && depth <= 8) {
    lua_createtable(L, static_cast<int>(j.size()), 0);
    lua_Integer i = 1;
    for (const auto& v : j) {
      fromJson(L, v, depth + 1);
      lua_rawseti(L, -2, i++);
    }
  } else if (j.is_object() && depth <= 8) {
    lua_createtable(L, 0, static_cast<int>(j.size()));
    for (auto it = j.begin(); it != j.end(); ++it) {
      fromJson(L, *it, depth + 1);
      lua_setfield(L, -2, it.key().c_str());
    }
  } else {
    lua_pushnil(L);
  }
}

void openState(Impl& im) {
  im.mem = 0;
  im.L = lua_newstate(allocate, &im);
  if (!im.L) return;
  *static_cast<Impl**>(lua_getextraspace(im.L)) = &im;
  lua_sethook(im.L, countHook, LUA_MASKCOUNT, 1000);
  lua_pushcfunction(im.L, [](lua_State* L) {
    openSandbox(L);
    return 0;
  });
  if (lua_pcall(im.L, 0, 0, 0) != LUA_OK) {
    lua_close(im.L);
    im.L = nullptr;
  }
}
}  // namespace

InterfaceLua::InterfaceLua() : impl_(std::make_unique<Impl>()) { openState(*impl_); }
InterfaceLua::~InterfaceLua() {
  if (impl_->L) lua_close(impl_->L);
}

void InterfaceLua::provide(const std::string& name, std::function<Value()> getter) { impl_->provided[name] = std::move(getter); }
void InterfaceLua::setConsole(std::function<std::string(const std::string&)> run) { impl_->console = std::move(run); }
void InterfaceLua::setTranslate(std::function<std::string(const std::string&)> tr) { impl_->translate = std::move(tr); }

bool InterfaceLua::load(const std::string& name, const std::string& code) {
  Impl& im = *impl_;
  if (!im.L) return false;
  ++im.scripts;
  // Text only: precompiled chunks could do what the sandbox forbids.
  if (luaL_loadbufferx(im.L, code.data(), code.size(), ("=" + name).c_str(), "t") != LUA_OK) {
    const char* msg = lua_tostring(im.L, -1);
    im.error(msg ? msg : name + ": does not compile");
    lua_pop(im.L, 1);
    return false;
  }
  im.loading = name;
  const bool ok = protectedCall(im, 0, 0, name);
  im.loading.clear();
  return ok;
}

void InterfaceLua::reset() {
  Impl& im = *impl_;
  im.savedKeep = savedJson();
  if (im.L) lua_close(im.L);
  im.L = nullptr;
  im.panels.clear();
  im.handlers.clear();
  im.scripts = 0;
  im.widgets = nullptr;
  openState(im);
  restoreSaved(im.savedKeep);
}

size_t InterfaceLua::scripts() const { return impl_->scripts; }

void InterfaceLua::emit(const std::string& event, const Fields& fields) {
  Impl& im = *impl_;
  if (!im.L) return;
  bool any = false;
  for (const Impl::Handler& h : im.handlers) any = any || (h.event == event && h.fn != LUA_NOREF);
  if (!any) return;
  for (size_t i = 0; i < im.handlers.size(); ++i) {  // by index: a handler may add handlers
    if (im.handlers[i].event != event || im.handlers[i].fn == LUA_NOREF) continue;
    lua_rawgeti(im.L, LUA_REGISTRYINDEX, im.handlers[i].fn);
    lua_createtable(im.L, 0, static_cast<int>(fields.size()));
    for (const auto& [key, value] : fields) {
      push(im.L, value);
      lua_setfield(im.L, -2, key.c_str());
    }
    if (protectedCall(im, 1, 0, "on " + event)) {
      im.handlers[i].strikes = 0;
    } else if (++im.handlers[i].strikes >= kStrikes) {
      luaL_unref(im.L, LUA_REGISTRYINDEX, im.handlers[i].fn);
      im.handlers[i].fn = LUA_NOREF;
      im.error("a handler of '" + event + "' failed " + std::to_string(kStrikes) + " times in a row and was switched off");
    }
  }
}

void InterfaceLua::draw(InterfaceWidgets& w) {
  Impl& im = *impl_;
  if (!im.L) return;
  for (size_t i = 0; i < im.panels.size(); ++i) {
    Impl::PanelRec& p = im.panels[i];
    if (!p.info.open || p.info.broken || p.draw == LUA_NOREF) continue;
    bool open = true;
    const bool shown = w.beginWindow(p.info.title, open);
    if (shown) {
      im.widgets = &w;
      lua_rawgeti(im.L, LUA_REGISTRYINDEX, p.draw);
      const bool ok = protectedCall(im, 0, 0, "panel '" + p.info.title + "'");
      im.widgets = nullptr;
      Impl::PanelRec& again = im.panels[i];  // (the vector may have grown)
      if (ok) {
        again.strikes = 0;
      } else if (++again.strikes >= kStrikes) {
        again.info.broken = true;
        im.error("panel '" + again.info.title + "' failed " + std::to_string(kStrikes) + " times in a row and was closed");
      }
    }
    w.endWindow();
    if (!open) im.panels[i].info.open = false;
  }
}

std::vector<InterfaceLua::Panel> InterfaceLua::panels() const {
  std::vector<Panel> out;
  for (const Impl::PanelRec& p : impl_->panels)
    if (p.draw != LUA_NOREF) out.push_back(p.info);
  return out;
}

bool InterfaceLua::setOpen(const std::string& title, bool open) {
  for (Impl::PanelRec& p : impl_->panels)
    if (p.info.title == title && p.draw != LUA_NOREF) {
      p.info.open = open;
      if (open) {  // opening it again gives a broken panel another chance
        p.info.broken = false;
        p.strikes = 0;
      }
      return true;
    }
  return false;
}

std::string InterfaceLua::savedJson() const {
  Impl& im = *impl_;
  if (!im.L) return im.savedKeep.empty() ? "{}" : im.savedKeep;
  lua_getglobal(im.L, "saved");
  nlohmann::json j = lua_istable(im.L, -1) ? toJson(im.L, -1, 0) : nlohmann::json::object();
  lua_pop(im.L, 1);
  if (!j.is_object()) j = nlohmann::json::object();  // an empty table reads as a list
  return j.dump();
}

bool InterfaceLua::restoreSaved(const std::string& text) {
  Impl& im = *impl_;
  const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object() || !im.L) return false;
  fromJson(im.L, j, 0);
  lua_setglobal(im.L, "saved");
  return true;
}

const std::vector<std::string>& InterfaceLua::errors() const { return impl_->errors; }
void InterfaceLua::clearErrors() { impl_->errors.clear(); }
size_t InterfaceLua::memory() const { return impl_->mem; }

}  // namespace pioneer

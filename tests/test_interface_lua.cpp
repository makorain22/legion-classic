// Interface scripts: panels drawn by Lua, with widgets that only write down
// what they were asked to draw.
#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

#include "pioneer/core/interface_lua.hpp"

using namespace pioneer;

namespace {
struct Recorder final : InterfaceWidgets {
  std::vector<std::string> log;
  std::string press;         // a button with these words reports a click
  std::string closeWindow;   // a window with this title is closed by its user
  std::string typed;         // what the user types into inputs ("" leaves them)
  bool beginWindow(const std::string& title, bool& open) override {
    log.push_back("window " + title);
    if (title == closeWindow) open = false;
    return true;
  }
  void endWindow() override { log.push_back("end"); }
  void text(const std::string& s, Style style) override { log.push_back((style == Heading ? "heading " : style == Bad ? "bad " : "text ") + s); }
  bool button(const std::string& label) override {
    log.push_back("button " + label);
    return label == press;
  }
  bool checkbox(const std::string& label, bool& value) override {
    log.push_back("checkbox " + label + (value ? " on" : " off"));
    return false;
  }
  bool slider(const std::string& label, double& value, double, double) override {
    log.push_back("slider " + label);
    value = 1e9;  // a widget that misbehaves: the script still gets a value in range
    return true;
  }
  bool input(const std::string& label, std::string& value) override {
    log.push_back("input " + label + "=" + value);
    if (typed.empty()) return false;
    value = typed;
    return true;
  }
  void progress(double fraction, const std::string& label) override { log.push_back("progress " + std::to_string(static_cast<int>(fraction * 100)) + " " + label); }
  void sameLine() override { log.push_back("same_line"); }
  void separator() override { log.push_back("separator"); }
  void tooltip(const std::string& s) override { log.push_back("tooltip " + s); }
  bool has(const std::string& line) const { return std::find(log.begin(), log.end(), line) != log.end(); }
};
}  // namespace

TEST_CASE("interface lua: a script declares a panel, reads the game, hears events and draws") {
  InterfaceLua lua;
  double supplies = 500;
  lua.provide("supplies", [&]() -> InterfaceLua::Value { return supplies; });
  lua.provide("player", []() -> InterfaceLua::Value { return 0.0; });
  lua.provide("side_name", []() -> InterfaceLua::Value { return std::string("Germany"); });
  lua.provide("in_battle", []() -> InterfaceLua::Value { return true; });
  lua.setTranslate([](const std::string& key) { return key == "report.title" ? std::string("Report") : key; });
  std::vector<std::string> ran;
  lua.setConsole([&](const std::string& line) {
    ran.push_back(line);
    return "done: " + line;
  });

  REQUIRE(lua.load("report.lua", R"(
    local lost, last = 0, ""
    on("unit_died", function(e)
      if e.owner == game.player then lost = lost + 1 end
      last = e.unit
    end)
    panel{ title = "Report", open = true, draw = function()
      ui.heading(tr("report.title") .. " for " .. game.side_name)
      ui.text("supplies " .. math.floor(game.supplies))
      ui.text("lost " .. lost .. " " .. last)
      ui.progress(lost / 4, "losses")
      ui.tooltip("of four")
      local v = ui.slider("volume", 0.5, 0, 1)
      ui.text("slider " .. v)
      saved.name = ui.input("name", saved.name or "nobody")
      if ui.button("Pause") then saved.said = console("pause 1") end
      local on = ui.checkbox("show", game.in_battle)
    end }
    panel{ title = "Hidden", draw = function() ui.text("not shown") end }
  )"));
  CHECK(lua.errors().empty());
  CHECK(lua.scripts() == 1);
  REQUIRE(lua.panels().size() == 2);
  CHECK(lua.panels()[0].open);
  CHECK_FALSE(lua.panels()[1].open);
  CHECK(lua.panels()[0].script == "report.lua");

  lua.emit("unit_died", {{"owner", 0.0}, {"unit", std::string("ger_rifleman")}});
  lua.emit("unit_died", {{"owner", 1.0}, {"unit", std::string("rus_t34")}});
  lua.emit("nobody_listens", {});
  supplies = 742.9;

  Recorder r;
  r.typed = "Hans";
  lua.draw(r);
  CHECK(lua.errors().empty());
  CHECK(r.log.front() == "window Report");
  CHECK(r.has("heading Report for Germany"));
  CHECK(r.has("text supplies 742"));
  CHECK(r.has("text lost 1 rus_t34"));
  CHECK(r.has("progress 25 losses"));
  CHECK(r.has("tooltip of four"));
  CHECK(r.has("text slider 1.0"));   // clamped to the slider's range
  CHECK(r.has("input name=nobody"));
  CHECK(r.has("checkbox show on"));
  CHECK_FALSE(r.has("text not shown"));
  CHECK(ran.empty());

  // A click runs a console command; what was typed is kept in `saved`.
  r.log.clear();
  r.press = "Pause";
  r.typed.clear();
  lua.draw(r);
  CHECK(r.has("input name=Hans"));
  CHECK(ran == std::vector<std::string>{"pause 1"});
  const std::string saved = lua.savedJson();
  CHECK(saved.find("\"name\":\"Hans\"") != std::string::npos);
  CHECK(saved.find("done: pause 1") != std::string::npos);

  // Opening and closing: by the game, and by the window's own button.
  CHECK(lua.setOpen("Hidden", true));
  CHECK_FALSE(lua.setOpen("No such panel", true));
  r.log.clear();
  r.press.clear();
  r.closeWindow = "Report";
  lua.draw(r);
  CHECK(r.has("text not shown"));
  CHECK_FALSE(lua.panels()[0].open);
  r.log.clear();
  lua.draw(r);
  CHECK_FALSE(r.has("window Report"));
  CHECK(r.has("window Hidden"));

  // Scripts are read again (a file changed): panels and handlers go, `saved` stays.
  lua.reset();
  CHECK(lua.panels().empty());
  CHECK(lua.scripts() == 0);
  CHECK(lua.savedJson().find("Hans") != std::string::npos);
  REQUIRE(lua.load("again.lua", R"(panel{ title = "Again", open = true, draw = function() ui.text("hello " .. saved.name) end })"));
  r.log.clear();
  lua.draw(r);
  CHECK(r.has("text hello Hans"));
}

TEST_CASE("interface lua: mistakes are told, contained and stop repeating") {
  InterfaceLua lua;
  lua.provide("tick", []() -> InterfaceLua::Value { return 7.0; });
  Recorder r;

  // Does not compile; fails while loading; both are told and neither is fatal.
  CHECK_FALSE(lua.load("broken.lua", "panel{ title = "));
  CHECK_FALSE(lua.load("fails.lua", "error('no good')"));
  REQUIRE(lua.errors().size() == 2);
  CHECK(lua.errors()[0].find("broken.lua") != std::string::npos);
  CHECK(lua.errors()[1].find("no good") != std::string::npos);
  lua.clearErrors();

  // What a panel needs, and one title only once.
  CHECK_FALSE(lua.load("a.lua", "panel{ draw = function() end }"));
  CHECK_FALSE(lua.load("b.lua", "panel{ title = 'T' }"));
  CHECK(lua.load("c.lua", "panel{ title = 'T', open = true, draw = function() ui.text(game.tick) ui.text(game.nothing) end }"));
  CHECK_FALSE(lua.load("d.lua", "panel{ title = 'T', draw = function() end }"));
  CHECK(lua.errors().back().find("already") != std::string::npos);
  lua.clearErrors();

  // A panel that fails every frame is closed after three goes; the window is always ended.
  for (int i = 0; i < 5; ++i) lua.draw(r);
  CHECK(std::count(r.log.begin(), r.log.end(), "window T") == InterfaceLua::kStrikes);
  CHECK(std::count(r.log.begin(), r.log.end(), "end") == InterfaceLua::kStrikes);
  CHECK(r.has("text 7"));
  REQUIRE(!lua.errors().empty());
  CHECK(lua.errors().front().find("game.nothing") != std::string::npos);
  CHECK(lua.errors().front().find("tick") != std::string::npos);  // the message lists what there is
  CHECK(lua.panels()[0].broken);
  CHECK(lua.errors().size() == 2);  // the same error is told once, then the closing
  // Ticking it again gives it another chance.
  CHECK(lua.setOpen("T", true));
  CHECK_FALSE(lua.panels()[0].broken);

  // Handlers the same.
  lua.clearErrors();
  CHECK(lua.load("h.lua", "count = 0  on('boom', function(e) count = count + 1  error('bang') end)  on('boom', function(e) saved.heard = (saved.heard or 0) + 1 end)"));
  for (int i = 0; i < 6; ++i) lua.emit("boom");
  CHECK(lua.savedJson().find("\"heard\":6") != std::string::npos);  // the good handler never missed one
  CHECK(lua.errors().back().find("switched off") != std::string::npos);

  // ui.* outside a panel, writing to game, console while loading.
  lua.clearErrors();
  CHECK_FALSE(lua.load("e.lua", "ui.text('too early')"));
  CHECK(lua.errors().back().find("inside a panel") != std::string::npos);
  CHECK_FALSE(lua.load("f.lua", "game.tick = 5"));
  CHECK(lua.errors().back().find("for reading") != std::string::npos);
  lua.setConsole([](const std::string&) { return std::string(); });
  CHECK_FALSE(lua.load("g.lua", "console('quit')"));
  CHECK(lua.errors().back().find("while a script loads") != std::string::npos);
}

TEST_CASE("interface lua: the sandbox, the limits and what is kept") {
  InterfaceLua lua;
  // Nothing that loads code or reaches the machine.
  for (const char* name : {"dofile", "loadfile", "load", "require", "io", "os", "debug", "package", "collectgarbage"}) {
    CAPTURE(name);
    CHECK(lua.load("probe.lua", std::string("assert(") + name + " == nil)"));
  }
  CHECK(lua.load("probe.lua", "assert(string.dump == nil) assert(getmetatable(game) == false) assert(math.random(1, 6) >= 1)"));
  CHECK(lua.errors().empty());
  // Compiled code is refused (it could do what the sandbox forbids).
  CHECK_FALSE(lua.load("binary.lua", std::string("\x1bLua\x54\x00", 6)));
  lua.clearErrors();

  // A script that never ends is stopped; one that eats memory is stopped; the rest goes on.
  CHECK_FALSE(lua.load("forever.lua", "while true do end"));
  CHECK(lua.errors().back().find("too long") != std::string::npos);
  CHECK_FALSE(lua.load("greedy.lua", "local t = {} for i = 1, 1e9 do t[i] = string.rep('x', 4096) .. i end"));
  CHECK(lua.memory() < InterfaceLua::kMemoryLimit);
  CHECK(lua.load("fine.lua", "saved.ok = true"));

  // `saved`: plain values, lists and tables come back; functions are left out.
  REQUIRE(lua.load("keep.lua", R"(
    saved.n = 3  saved.half = 0.5  saved.name = "x"  saved.list = {1, 2, 3}
    saved.nest = { a = { b = true } }  saved.fn = function() end
  )"));
  const std::string json = lua.savedJson();
  InterfaceLua other;
  REQUIRE(other.restoreSaved(json));
  REQUIRE(other.load("check.lua", R"(
    assert(saved.n == 3 and math.type(saved.n) == "integer")
    assert(saved.half == 0.5 and saved.name == "x" and saved.ok == true)
    assert(#saved.list == 3 and saved.list[3] == 3)
    assert(saved.nest.a.b == true and saved.fn == nil)
  )"));
  CHECK(other.errors().empty());
  CHECK_FALSE(other.restoreSaved("not json"));
  CHECK_FALSE(other.restoreSaved("[1,2]"));
  CHECK(InterfaceLua().savedJson() == "{}");
}

TEST_CASE("interface lua: the script the game ships loads and draws") {
  std::ifstream f(std::string(LEGION_CONTENT_DIR) + "ui/battle_report.lua", std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  REQUIRE(!ss.str().empty());
  InterfaceLua lua;
  bool battle = false;
  lua.provide("in_battle", [&]() -> InterfaceLua::Value { return battle; });
  lua.provide("seconds", []() -> InterfaceLua::Value { return 125.4; });
  lua.provide("player", []() -> InterfaceLua::Value { return 0.0; });
  lua.provide("selected", []() -> InterfaceLua::Value { return 12.0; });
  lua.provide("phase", []() -> InterfaceLua::Value { return 1.0; });
  lua.provide("phase_name", []() -> InterfaceLua::Value { return std::string("Assault"); });
  REQUIRE(lua.load("ui/battle_report.lua", ss.str()));
  REQUIRE(lua.panels().size() == 1);
  CHECK_FALSE(lua.panels()[0].open);  // a mod's panels don't open themselves
  REQUIRE(lua.setOpen("Battle report", true));
  Recorder r;
  lua.draw(r);
  CHECK(r.has("text No battle is being fought."));
  battle = true;
  lua.emit("battle_started");
  lua.emit("unit_died", {{"owner", 0.0}});
  for (int i = 0; i < 3; ++i) lua.emit("unit_died", {{"owner", 1.0}});
  lua.emit("wave_arrived", {{"owner", 1.0}});
  r.log.clear();
  lua.draw(r);
  CHECK(lua.errors().empty());
  CHECK(r.has("heading 2:05"));
  CHECK(r.has("text Lost: 1"));
  CHECK(r.has("text Destroyed: 3"));
  CHECK(r.has("progress 75 exchange"));
  CHECK(r.has("text Phase: Assault"));
  CHECK(r.has("text Waves arrived: 1"));
}

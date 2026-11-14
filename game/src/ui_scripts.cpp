// Interface scripts: the game's side of core/interface_lua.hpp. What scripts
// can read (game.*), the events they hear, loading ui/*.lua from the content
// and the mods, and keeping their `saved` table and open panels between runs.
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "game_scene.hpp"
#include "pioneer/core/console.hpp"
#include "pioneer/core/strings.hpp"
#include "pioneer/ui/interface_widgets.hpp"

using namespace pioneer;

namespace legion {

namespace {
const char* eventName(EventType t) {
  switch (t) {
    case EventType::UnitDied: return "unit_died";
    case EventType::StructurePlaced: return "structure_placed";
    case EventType::StructureSold: return "structure_sold";
    case EventType::UnitReady: return "unit_ready";
    case EventType::PlayerDefeated: return "player_defeated";
    case EventType::PlayerWon: return "player_won";
    case EventType::ScriptMessage: return "message";
    case EventType::ObjectivesChanged: return "objectives_changed";
    case EventType::GroupFormed: return "group_formed";
    case EventType::PointCaptured: return "point_captured";
    case EventType::ConvoyArrived: return "convoy_arrived";
    case EventType::Transmission: return "transmission";
    case EventType::CountdownDone: return "countdown_done";
    case EventType::PhaseChanged: return "phase_changed";
    case EventType::WaveArrived: return "wave_arrived";
    default: return nullptr;  // shots and impacts: thousands a minute, not for scripts
  }
}
}  // namespace

void GameScene::initInterfaceScripts() {
  uiLua_ = std::make_unique<InterfaceLua>();
  uiWidgets_ = pioneer::ui::makeInterfaceWidgets();
  using Value = InterfaceLua::Value;
  InterfaceLua& lua = *uiLua_;
  const auto battle = [this] { return mode_ != Mode::Menu && sim_ != nullptr; };
  lua.provide("in_battle", [battle]() -> Value { return battle(); });
  lua.provide("tick", [this, battle]() -> Value { return battle() ? static_cast<double>(sim_->tickCount()) : 0.0; });
  lua.provide("seconds", [this, battle]() -> Value { return battle() ? static_cast<double>(sim_->tickCount()) / TICKS_PER_SECOND : 0.0; });
  lua.provide("player", [this]() -> Value { return static_cast<double>(player_); });
  lua.provide("supplies", [this, battle]() -> Value { return battle() ? static_cast<double>(sim_->playerState(player_).credits) : 0.0; });
  lua.provide("selected", [this]() -> Value { return static_cast<double>(selection_.size()); });
  lua.provide("phase", [this, battle]() -> Value { return battle() ? static_cast<double>(sim_->missionPhase()) : -1.0; });
  lua.provide("phase_name", [this, battle]() -> Value {
    const int p = battle() ? sim_->missionPhase() : -1;
    return p >= 0 && p < static_cast<int>(scenario_.phases.size()) ? scenario_.phases[static_cast<size_t>(p)].name : std::string();
  });
  lua.provide("phase_seconds", [this, battle]() -> Value { return battle() ? static_cast<double>(sim_->phaseSeconds()) : 0.0; });
  lua.provide("fps", [this]() -> Value { return static_cast<double>(app_.fps()); });
  lua.provide("clock", [this]() -> Value { return static_cast<double>(time_); });
  lua.provide("language", []() -> Value { return loc::strings().language(); });
  lua.setTranslate([](const std::string& key) { return loc::strings().get(key); });
  lua.setConsole([](const std::string& line) {
    // What the command prints comes back to the script as text.
    Console& con = Console::global();
    std::string out;
    const int id = con.addOutput([&out](std::string_view l, bool) {
      out.append(l);
      out += '\n';
    });
    con.execute(line);
    con.removeOutput(id);
    if (!out.empty()) out.pop_back();
    return out;
  });

  // The battle's events, by name, with the fields that mean something for each.
  uiLuaEvents_ = bus_.subscribe<SimEvent>([this](const SimEvent& e) {
    const char* name = eventName(e.type);
    if (!name || !uiLua_) return;
    InterfaceLua::Fields f;
    f.emplace_back("owner", static_cast<double>(e.owner));
    f.emplace_back("x", static_cast<double>(e.at.x) / 256.0);  // in tiles
    f.emplace_back("y", static_cast<double>(e.at.y) / 256.0);
    f.emplace_back("amount", static_cast<double>(e.amount));
    f.emplace_back("text", e.text);
    if (e.type == EventType::UnitDied || e.type == EventType::UnitReady || e.type == EventType::StructurePlaced ||
        e.type == EventType::StructureSold)
      f.emplace_back("unit", e.unitType < types_.size() ? types_.get(e.unitType).id : std::string());
    uiLua_->emit(name, f);
  });

  // What the scripts kept, and which panels were open.
  std::ifstream in(app_.userDir() + "ui_scripts.json", std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  const nlohmann::json j = nlohmann::json::parse(ss.str(), nullptr, false);
  if (j.is_object()) {
    if (const auto s = j.find("saved"); s != j.end() && s->is_object()) uiLua_->restoreSaved(s->dump());
    if (const auto o = j.find("open"); o != j.end() && o->is_array())
      for (const auto& t : *o)
        if (t.is_string()) uiPanelsOpen_.push_back(t.get<std::string>());
  }
  loadInterfaceScripts();
}

void GameScene::loadInterfaceScripts() {
  if (!uiLua_) return;
  // Open panels are remembered across the reload (by title).
  if (uiLua_->scripts() > 0) {
    uiPanelsOpen_.clear();
    for (const InterfaceLua::Panel& p : uiLua_->panels())
      if (p.open) uiPanelsOpen_.push_back(p.title);
  }
  uiLua_->reset();
  uiLua_->clearErrors();
  const vfs::Vfs& fs = *content_.fileSystem();
  size_t loaded = 0;
  for (const vfs::FileInfo& f : fs.list("ui", false, ".lua")) {
    std::string code;
    if (!fs.readText(f.path, code)) continue;
    if (uiLua_->load(fs.describe(f.mount, f.path), code)) ++loaded;
  }
  for (const std::string& title : uiPanelsOpen_) uiLua_->setOpen(title, true);
  PNR_INFO_CH(Content, "Interface scripts: %zu loaded, %zu panels%s", loaded, uiLua_->panels().size(),
              uiLua_->errors().empty() ? "" : " (with errors: see ui.scripts)");
}

void GameScene::saveInterfaceScripts() {
  if (!uiLua_ || app_.offscreen()) return;
  nlohmann::json j;
  j["saved"] = nlohmann::json::parse(uiLua_->savedJson(), nullptr, false);
  if (j["saved"].is_discarded()) j["saved"] = nlohmann::json::object();
  j["open"] = nlohmann::json::array();
  for (const InterfaceLua::Panel& p : uiLua_->panels())
    if (p.open) j["open"].push_back(p.title);
  std::ofstream out(app_.userDir() + "ui_scripts.json", std::ios::binary);
  out << j.dump(1);
}

void GameScene::drawInterfaceScripts() {
  if (!uiLua_) return;
  // A battle beginning or ending is news too.
  const bool battle = mode_ != Mode::Menu && sim_ != nullptr;
  const void* now = battle ? static_cast<const void*>(sim_.get()) : nullptr;
  if (now != uiLuaBattle_) {
    uiLuaBattle_ = now;
    uiLua_->emit(battle ? "battle_started" : "battle_ended");
  }
  uiLua_->draw(*uiWidgets_);
}

void GameScene::registerInterfaceScriptCommands() {
  Console& con = Console::global();
  con.addCommand("ui.scripts", "ui.scripts - the panels interface scripts (ui/*.lua) declare, and what went wrong in them.",
                 [this](const ConsoleArgs&, Console& c) {
                   if (!uiLua_) return;
                   for (const InterfaceLua::Panel& p : uiLua_->panels())
                     c.printFmt("%-28s %-8s %s", p.title.c_str(), p.broken ? "broken" : p.open ? "open" : "closed", p.script.c_str());
                   for (const std::string& e : uiLua_->errors()) c.error(e);
                   c.printFmt("%zu scripts, %zu panels, %zu errors, %zu KB in use", uiLua_->scripts(), uiLua_->panels().size(),
                              uiLua_->errors().size(), uiLua_->memory() / 1024);
                 });
  con.addCommand("ui.panel", "ui.panel <title> [0|1] - opens or closes a panel of an interface script.",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (!uiLua_ || a.size() < 2) return c.print("ui.panel <title> [0|1]; ui.scripts lists them");
                   // The title may have spaces: every word but a last 0 or 1.
                   size_t end = a.size();
                   bool open = true, given = false;
                   if (end > 2 && (a.str(end - 1) == "0" || a.str(end - 1) == "1")) {
                     open = a.str(end - 1) == "1";
                     given = true;
                     --end;
                   }
                   std::string title;
                   for (size_t i = 1; i < end; ++i) title += (i > 1 ? " " : "") + a.str(i);
                   if (!given)
                     for (const InterfaceLua::Panel& p : uiLua_->panels())
                       if (p.title == title) open = !p.open;
                   if (!uiLua_->setOpen(title, open)) c.error("there is no panel '" + title + "' (ui.scripts lists them)");
                 });
  con.addCommand("ui.reload", "ui.reload - reads the interface scripts again (they also reload when a file changes).",
                 [this](const ConsoleArgs&, Console& c) {
                   loadInterfaceScripts();
                   c.printFmt("%zu scripts, %zu panels, %zu errors", uiLua_->scripts(), uiLua_->panels().size(), uiLua_->errors().size());
                 });
}

}  // namespace legion

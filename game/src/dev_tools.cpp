// Legion Classic - developer tools: the console (` or F9) and the profiler
// (F8), and the commands the game adds to the engine's console.
//
// The engine brings log.*, prof.*, mem.* and jobs.* (pioneer/core/console.hpp);
// this file adds what only the game knows: speed, pause, screenshots, the
// state hash, and the cheats used when testing (spawn, credits, reveal ...).
// Settings typed here and marked "saved" go to console.cfg in the user
// folder; a player's own lines belong in autoexec.cfg beside it.
#include "game_scene.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "editor.hpp"
#include "pioneer/core/chunks.hpp"
#include "pioneer/core/console.hpp"
#include "pioneer/core/data_commands.hpp"
#include "pioneer/core/jobs.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/core/profiler.hpp"
#include "pioneer/core/version.hpp"
#include "pioneer/sim/determinism.hpp"
#include "settings.hpp"
#include "sidebar.hpp"

using namespace pioneer;

namespace legion {

namespace {
// Every command and variable this file registers, so they can be taken away
// again when the scene goes (they hold a pointer to it).
const char* const kGameCommands[] = {"quit",   "exit",    "screenshot", "speed", "pause", "hash", "status",
                                     "reveal", "credits", "spawn",      "kill",  "side",  "goto", "map",
                                     "restart", "editor", "clear",      "profiler", "console", "mod.list", "mod.pack"};

bool readText(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}
}  // namespace

void GameScene::toggleConsole() {
  showConsole_ = !showConsole_;
  if (showConsole_) consoleWin_.focus();
}

void GameScene::initDevTools() {
  Console& con = Console::global();
  registerCoreCommands(con, app_.userDir());
  // The data systems: what is mounted, the assets, hot reload, packs.
  DataHooks data;
  data.fileSystem = [this] { return content_.fileSystem(); };
  data.registry = [this] { return &assets_; };
  data.hotReload = &hotReload_;
  data.resolvePath = [this](const std::string& typed, bool output) {
    std::error_code ec;
    const bool bare = typed.find_first_of("/\\:") == std::string::npos;
    if (output) return bare ? exportsDir() + typed : typed;
    if (std::filesystem::exists(typed, ec)) return typed;
    // A mod's id stands for its folder (or its pack).
    if (const ModInfo* m = findMod(mods_, typed)) return m->packed ? m->file : m->dir;
    if (bare && std::filesystem::exists(exportsDir() + typed, ec)) return exportsDir() + typed;
    if (bare && std::filesystem::exists(userModsDir() + typed, ec)) return userModsDir() + typed;
    return typed;
  };
  registerDataCommands(con, std::move(data));
  registerConsoleCommands();

  // Automated runs (self-tests, screenshots) neither read nor write the
  // player's console settings, so they behave the same on every machine.
  if (!app_.offscreen()) {
    std::string text;
    if (readText(app_.userDir() + "console.cfg", text)) con.executeConfig(text);
    if (readText(app_.userDir() + "autoexec.cfg", text)) con.execute(text);
  }
  if (opt_.profile) {
    con.setLogOutput(false);
    con.setVar("prof.measure", "1");
    con.setLogOutput(true);
    showProfiler_ = !app_.offscreen();
  }
  if (!opt_.trace.empty() && opt_.bench == 0)
    con.queue("prof.capture " + std::to_string(std::max(1, opt_.traceFrames)) + " " + Console::quote(opt_.trace));
  if (!opt_.exec.empty()) con.queue(opt_.exec);

  // What the game's own systems hold, for mem.report and the profiler's Memory tab.
  memReporters_.emplace_back(mem::Tag::Mesh, "voxel models on the GPU", [this] { return meshes_.vertexBytes(); });
  memReporters_.emplace_back(mem::Tag::Vision, "what each side sees", [this]() -> size_t {
    if (!sim_) return 0;
    size_t n = 0;
    for (int p = 0; p < MAX_PLAYERS; ++p) n += sim_->vision().grid(p).capacity();
    return n;
  });
  memReporters_.emplace_back(mem::Tag::Map, "map tiles", [this]() -> size_t {
    return static_cast<size_t>(map_.width()) * static_cast<size_t>(map_.height()) * sizeof(TileInfo);
  });
  devToolsReady_ = true;
  PNR_LOG(Console, Debug, "%zu console commands, %zu variables", con.commands({}, true).size(),
          con.vars({}, true).size());
}

void GameScene::shutdownDevTools() {
  if (!devToolsReady_) return;
  devToolsReady_ = false;
  Console& con = Console::global();
  if (!app_.offscreen()) con.saveConfig(app_.userDir() + "console.cfg");
  for (const char* name : kGameCommands) con.removeCommand(name);
  unregisterDataCommands(con);
  memReporters_.clear();
}

bool GameScene::devToolsEvent(const SDL_Event& e) {
  if (!devToolsReady_ || e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return false;
  const ImGuiIO& io = ImGui::GetIO();
  // While something else is being typed into, a key that makes a character
  // (the ` of the console) is text, not a command; F9 and F8 always work.
  const bool typingElsewhere = io.WantTextInput && !consoleWin_.hasKeyboard();
  const bool printable = e.key.key < 0x40000000 && (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) == 0;
  if (typingElsewhere && printable) return false;
  if (settings_.pressed(InputAction::Console, e.key)) {
    toggleConsole();
    return true;
  }
  if (settings_.pressed(InputAction::Profiler, e.key)) {
    Console::global().execute("profiler");
    return true;
  }
  // Keys bound with the console's 'bind' (not while typing a command).
  if (!io.WantTextInput) {
    const KeyCombo combo{e.key.key, modsOf(e.key.mod)};
    if (Console::global().keyPressed(comboName(combo))) return true;
  }
  return false;
}

void GameScene::updateDevTools() {
  if (!devToolsReady_) return;
  Console& con = Console::global();
  con.update();
  updateCoreCommands(con);
}

void GameScene::drawDevTools() {
  if (!devToolsReady_) return;
  Console& con = Console::global();
  if (const CVar* v = con.find("r.showfps"); v && v->b() && !showConsole_)
    ui::drawFrameCounter(app_.fps(), app_.frameMs(), prof::scopeMs("Sim tick"),
                         ImGui::GetIO().DisplaySize.x - (mode_ == Mode::Game ? Sidebar::kWidth : 0.0f) - 4);
  if (showProfiler_) {
    profilerWin_.draw(&showProfiler_, app_.userDir());
    if (!showProfiler_) con.execute("profiler 0");  // closed with its own button
  }
  if (showConsole_) {
    if (const CVar* h = con.find("con.height")) consoleWin_.setHeight(h->f());
    consoleWin_.draw(con, &showConsole_);
  }
}

void GameScene::registerConsoleCommands() {
  Console& con = Console::global();
  auto inBattle = [this](Console& c) {
    if (mode_ != Mode::Menu && sim_) return true;
    c.error("That needs a battle: start one first.");
    return false;
  };

  con.addBool("r.showfps", false, "Shows frames per second and frame time in the top-left corner.", kCVarArchive);
  con.addFloat("con.height", 0.45, 0.2, 1.0, "How far down the screen the console reaches (0.2 .. 1).", kCVarArchive);
  con.addInt("sys.threads", 0, 0, 32,
             "Threads for the job system: 0 = one per processor core, 1 = no extra threads (old hardware).",
             kCVarArchive)
      .onChange([this](const CVar& v) {
        if (opt_.threads > 0) return;  // fixed by --threads for this run
        jobs::system().start(v.i());
        PNR_LOG(Jobs, Info, "The job system now runs on %d thread(s)", jobs::system().threadCount());
      });

  con.addCommand("quit", "quit - leaves the game.", [this](const ConsoleArgs&, Console&) { quit(); });
  con.addCommand("exit", "exit - leaves the game.", [this](const ConsoleArgs&, Console&) { quit(); });

  con.addCommand("clear", "clear - empties the console window (the log file keeps everything).",
                 [this](const ConsoleArgs&, Console&) { consoleWin_.clear(); });

  con.addCommand("console", "console [0|1] - opens or closes the console window.",
                 [this](const ConsoleArgs& a, Console&) {
                   if (a.size() > 1 ? a.flag(1) != showConsole_ : true) toggleConsole();
                 });

  con.addCommand("profiler", "profiler [0|1] - opens or closes the profiler window (and measuring with it).",
                 [this](const ConsoleArgs& a, Console& c) {
                   showProfiler_ = a.size() > 1 ? a.flag(1) : !showProfiler_;
                   c.setLogOutput(false);
                   c.setVar("prof.measure", showProfiler_ ? "1" : "0");
                   c.setLogOutput(true);
                 });

  con.addCommand("screenshot", "screenshot [name] - saves the screen as a PNG in the user folder.",
                 [this](const ConsoleArgs& a, Console& c) {
                   std::string name = a.str(1, "screenshot-console.png");
                   if (name.size() < 4 || name.compare(name.size() - 4, 4, ".png") != 0) name += ".png";
                   const bool bare = name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
                   const std::string path = bare ? app_.userDir() + name : name;
                   app_.requestScreenshot(path);
                   c.print("Screenshot: " + path);
                 });

  con.addCommand("speed", "speed [x] - game speed, 0.25 to 16 (1 = normal).", [this](const ConsoleArgs& a, Console& c) {
    if (a.size() > 1) speed_ = std::clamp(static_cast<float>(a.number(1, 1.0)), 0.25f, 16.0f);
    c.printFmt("Game speed x%.2f%s", static_cast<double>(speed_), paused_ ? " (paused)" : "");
  });
  con.addCommand("pause", "pause [0|1] - pauses or resumes the battle.", [this](const ConsoleArgs& a, Console& c) {
    paused_ = a.size() > 1 ? a.flag(1) : !paused_;
    c.print(paused_ ? "Paused" : "Running");
  });

  con.addCommand("hash", "hash - the checksum of the whole battle state (the same on every machine in step).",
                 [this, inBattle](const ConsoleArgs&, Console& c) {
                   if (!inBattle(c)) return;
                   c.printFmt("tick %u  state %016llx", sim_->tickCount(),
                            static_cast<unsigned long long>(sim_->stateHash()));
                 });

  con.addCommand("sim.digest", "sim.digest - a checksum of every section of the battle state, with its size when saved.",
                 [this, inBattle](const ConsoleArgs&, Console& c) {
                   if (!inBattle(c)) return;
                   const Simulation::StateDigest d = sim_->digest();
                   size_t total = 0;
                   for (const auto& sec : d.sections) {
                     const char* what = "";
                     for (const auto& info : Simulation::stateSections())
                       if (info.tag == sec.tag) what = info.what;
                     c.printFmt("  %-4s %016llx %8u bytes  %s", fourccName(sec.tag).c_str(),
                                static_cast<unsigned long long>(sec.hash), sec.bytes, what);
                     total += sec.bytes;
                   }
                   c.printFmt("tick %u  all sections %016llx  %zu bytes", d.tick, static_cast<unsigned long long>(d.total),
                              total);
                 });
  con.addCommand("sim.twincheck",
                 "sim.twincheck [seconds] - saves the battle, loads it into a second simulation and runs both side by "
                 "side (default 10 s): they must stay identical. The battle moves on by that long.",
                 [this, inBattle](const ConsoleArgs& a, Console& c) {
                   if (!inBattle(c)) return;
                   const int seconds = std::clamp(a.size() > 1 ? a.integer(1) : 10, 1, 600);
                   const TwinReport rep = twinCheck(*sim_, static_cast<uint32_t>(seconds * TICKS_PER_SECOND));
                   if (rep.ok) c.print("OK: " + rep.text);
                   else c.error("NOT DETERMINISTIC: " + rep.text);
                   replayValid_ = false;  // ticks went by that no replay recorded the pace of
                 });
  con.addCommand("replay.check", "replay.check - while a replay plays: whether it still matches the game it recorded.",
                 [this](const ConsoleArgs&, Console& c) {
                   if (mode_ != Mode::Replay) return c.error("no replay is playing");
                   if (replayTrace_.empty()) return c.print("this replay was made before checksums were recorded");
                   if (replayDrift_) c.error("out of step since tick " + std::to_string(replayDrift_));
                   else c.printFmt("in step: %zu checksums, playing tick %u of %u", replayTrace_.samples().size(),
                                   sim_->tickCount(), replayEnd_);
                 });

  con.addCommand("status", "status - tick, units and each side's supplies.",
                 [this, inBattle](const ConsoleArgs&, Console& c) {
                   if (!inBattle(c)) return;
                   const uint32_t t = sim_->tickCount();
                   c.printFmt("%u:%02u (tick %u), %zu units, speed x%.2f%s", t / 900, t / 15 % 60, t, sim_->unitCount(),
                            static_cast<double>(speed_), paused_ ? ", paused" : "");
                   for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
                     if (!players_[p].active) continue;
                     const PlayerState& ps = sim_->playerState(p);
                     c.printFmt("  %u %-12s %s  supplies %d (+%d/min)%s%s", p, players_[p].name.c_str(),
                              players_[p].faction.c_str(), ps.credits, ps.incomePerMinute,
                              ps.defeated ? "  defeated" : "", p == player_ ? "  <- you" : "");
                   }
                 });

  con.addCommand("map", "map - the size and landscape of the map in play.",
                 [this, inBattle](const ConsoleArgs&, Console& c) {
                   if (!inBattle(c)) return;
                   c.printFmt("%d x %d tiles", map_.width(), map_.height());
                 });

  con.addCommand("goto", "goto <x> <y> - moves the camera to a tile.",
                 [this, inBattle](const ConsoleArgs& a, Console& c) {
                   if (!inBattle(c)) return;
                   if (a.size() < 3) return c.error("goto <tile x> <tile y>");
                   const int x = std::clamp(a.integer(1), 0, map_.width() - 1);
                   const int y = std::clamp(a.integer(2), 0, map_.height() - 1);
                   centerOn({x * LEPTONS_PER_TILE + LEPTONS_PER_TILE / 2, y * LEPTONS_PER_TILE + LEPTONS_PER_TILE / 2});
                 });

  con.addCommand("restart", "restart - starts the battle or mission again.",
                 [this, inBattle](const ConsoleArgs&, Console& c) {
                   if (inBattle(c)) restart();
                 });

  con.addCommand("editor", "editor [0|1] - opens or closes the Live Scenario Editor.",
                 [this, inBattle](const ConsoleArgs& a, Console& c) {
                   if (!inBattle(c) || mode_ != Mode::Game) return;
                   editor_->setActive(a.size() > 1 ? a.flag(1) : !editor_->active());
                 });

  con.addCommand("mod.list", "mod.list - the mods installed: folders and packs, and which are switched on.",
                 [this](const ConsoleArgs&, Console& c) {
                   rescanMods();
                   if (mods_.empty()) return c.print("No mods are installed.");
                   for (const ModInfo& m : mods_) {
                     const bool on = std::find(settings_.mods.begin(), settings_.mods.end(), m.id) != settings_.mods.end();
                     c.printFmt("  %s %-22s %-8s %s  %s%s%s", on ? "[on] " : "[off]", m.id.c_str(), m.version.c_str(),
                                m.packed ? "pack  " : "folder", m.name.c_str(), m.error.empty() ? "" : "  BROKEN: ",
                                m.error.c_str());
                   }
                 });
  con.addCommand("mod.pack",
                 "mod.pack <mod id> - packs one of the installed mods into a single file you can hand to someone "
                 "else; they drop it into their mods folder.",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 2) return c.error("mod.pack <mod id>   ('mod.list' shows them)");
                   std::string file, err;
                   if (!packMod(a[1], file, err)) return c.error("Not packed: " + err);
                   c.print("Packed into " + file);
                 },
                 0,
                 [this](const ConsoleArgs&, const std::string& partial) {
                   std::vector<std::string> out;
                   for (const ModInfo& m : mods_)
                     if (!m.packed && m.id.compare(0, partial.size(), partial) == 0) out.push_back(m.id);
                   return out;
                 });

  // --- Cheats: for testing. They need 'cheats 1' first. -----------------------
  con.addCommand("reveal", "reveal [0|1] - shows every enemy, seen or not.",
                 [this](const ConsoleArgs& a, Console& c) {
                   revealMap_ = a.size() > 1 ? a.flag(1) : !revealMap_;
                   c.print(revealMap_ ? "Everything is shown" : "Only what your side sees is shown");
                 },
                 kCmdCheat);

  con.addCommand("credits", "credits <amount> [side] - sets a side's supplies (your own if no side is given).",
                 [this, inBattle](const ConsoleArgs& a, Console& c) {
                   if (!inBattle(c)) return;
                   if (a.size() < 2) return c.error("credits <amount> [side]");
                   const int side = a.integer(2, player_);
                   if (side < 0 || side >= MAX_PLAYERS || !players_[static_cast<size_t>(side)].active)
                     return c.error("There is no side " + a.str(2) + " in this battle.");
                   Command cmd;
                   cmd.type = CommandType::SetCredits;
                   cmd.player = static_cast<uint8_t>(side);
                   cmd.amount = std::clamp(a.integer(1), 0, 9999999);
                   sim_->queue(cmd);
                   c.printFmt("%s now has %d supplies", players_[static_cast<size_t>(side)].name.c_str(), cmd.amount);
                 },
                 kCmdCheat);

  con.addCommand("spawn", "spawn <unit id> [count] [side] - creates units under the mouse ('spawn ' + Tab lists the ids).",
                 [this, inBattle](const ConsoleArgs& a, Console& c) {
                   if (!inBattle(c)) return;
                   if (a.size() < 2) return c.error("spawn <unit id> [count] [side]");
                   const int type = types_.find(a[1]);
                   if (type < 0) return c.error("There is no unit or building called '" + a[1] + "'.");
                   const int side = a.integer(3, player_);
                   if (side < 0 || side >= MAX_PLAYERS) return c.error("Sides are 0 to 7.");
                   Command cmd;
                   cmd.type = CommandType::Spawn;
                   cmd.player = static_cast<uint8_t>(side);
                   cmd.unitType = static_cast<uint16_t>(type);
                   cmd.count = std::clamp(a.integer(2, 1), 1, 500);
                   cmd.target = mouseWorldLeptons();
                   sim_->queue(cmd);
                   c.printFmt("%d x %s for side %d", static_cast<int>(cmd.count), a[1].c_str(), side);
                 },
                 kCmdCheat,
                 [this](const ConsoleArgs& a, const std::string&) {
                   std::vector<std::string> out;
                   if (a.size() == 1)
                     for (const UnitTypeDef& t : types_.all()) out.push_back(t.id);
                   return out;
                 });

  con.addCommand("kill", "kill - destroys the selected units.",
                 [this, inBattle](const ConsoleArgs&, Console& c) {
                   if (!inBattle(c)) return;
                   pruneSelection();
                   if (selection_.empty()) return c.error("Select something first.");
                   Command cmd;
                   cmd.type = CommandType::Kill;
                   cmd.units = selection_;
                   sim_->queue(std::move(cmd));
                   c.printFmt("%zu units destroyed", selection_.size());
                   selection_.clear();
                 },
                 kCmdCheat);

  con.addCommand("side", "side [n] - commands (and sees through the eyes of) another side.",
                 [this, inBattle](const ConsoleArgs& a, Console& c) {
                   if (!inBattle(c)) return;
                   if (a.size() < 2) return c.printFmt("You command side %d (%s)", player_, players_[player_].name.c_str());
                   const int side = a.integer(1, -1);
                   if (side < 0 || side >= MAX_PLAYERS || !players_[static_cast<size_t>(side)].active)
                     return c.error("There is no side " + a[1] + " in this battle.");
                   setPlayer(static_cast<uint8_t>(side));
                 },
                 kCmdCheat);
}

}  // namespace legion

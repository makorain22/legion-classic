#include "game_scene.hpp"

#include "encyclopedia.hpp"
#include "game_audio.hpp"
#include "ui_style.hpp"

#include <glad/gl.h>
#include <map>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "pioneer/sim/factions.hpp"

#include "editor.hpp"
#include "menus.hpp"
#include "rules_editor.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/core/rng.hpp"
#include "pioneer/math/iso.hpp"

using namespace pioneer;

namespace legion {

namespace {
constexpr uint32_t kSelectGreen = rgba8(120, 236, 96);
constexpr uint32_t kAttackRed = rgba8(236, 70, 52);
constexpr float kEdgeScrollPx = 6.0f;
constexpr float kScrollSpeed = 1100.0f;  // screen pixels per second
constexpr float kPxPerUnit = iso::HEIGHT_PX_PER_TILE / HEIGHT_UNITS_PER_TILE;

ImVec4 toImVec(uint32_t c) {
  return ImVec4((c & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, ((c >> 16) & 0xff) / 255.0f, 1.0f);
}
}  // namespace

namespace {
std::array<PlayerSetup, MAX_PLAYERS> defaultPlayers() {
  const uint32_t colors[MAX_PLAYERS] = {rgba8(196, 58, 44),  rgba8(52, 96, 196),  rgba8(66, 160, 72),
                                        rgba8(214, 184, 62), rgba8(220, 120, 40), rgba8(132, 72, 180),
                                        rgba8(60, 170, 170), rgba8(210, 210, 200)};
  std::array<PlayerSetup, MAX_PLAYERS> p;
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    p[static_cast<size_t>(i)].name = "Player " + std::to_string(i + 1);
    p[static_cast<size_t>(i)].faction = i % 2 ? "rus" : "ger";
    p[static_cast<size_t>(i)].color = colors[i];
    p[static_cast<size_t>(i)].team = static_cast<uint8_t>(i);
  }
  p[0].name = "Germany";
  p[0].active = true;
  p[1].name = "Russia";
  p[1].active = true;
  return p;
}
}  // namespace

GameScene::GameScene(App& app, SceneOptions opt) : app_(app), opt_(opt) {
  players_ = defaultPlayers();
  for (int i = 0; i < MAX_PLAYERS; ++i) teamColors_[static_cast<size_t>(i)] = players_[static_cast<size_t>(i)].color;
  showHelp_ = opt.showHelp;
  showDebug_ = opt.showDebug;
}

GameScene::~GameScene() {
  shutdownDevTools();
  if (cursorArrow_) SDL_DestroyCursor(cursorArrow_);
  if (cursorCross_) SDL_DestroyCursor(cursorCross_);
}

bool GameScene::init(const std::string& contentDir, std::string& error) {
  contentDir_ = contentDir;
  // Settings first: they say which mods to load and whether to use the art pack.
  const bool firstStart = !settings_.load(app_.userDir() + "settings.json");
  if (!loadRules(error)) {
    if (settings_.mods.empty()) return false;
    // A broken mod must not keep the game from starting: start without mods and say why.
    const std::string why = error;
    settings_.mods.clear();
    if (!loadRules(error)) return false;
    modWarnings_ += "The mods were switched off: " + why + "\n";
    PNR_WARN("Mods switched off: %s", why.c_str());
  }
  const ArtOptions artOpt = artOptions();
  artPack_ = !artOpt.packDirs.empty();
  if (!buildPlaceholderArt(art_, error, artOpt)) return false;
  if (!renderer_.init(error)) return false;
  if (!terrain_.init(error)) return false;
  if (!meshes_.init(error)) return false;
  {
    std::string serr;
    if (!shadow_.init(2048, serr)) PNR_WARN("No shadows: %s", serr.c_str());
  }
  renderer_.setAtlas(art_.atlas, art_.frames);
  frameMesh_.assign(art_.frames.size(), -2);
  renderer_.setWhiteFrame(art_.white);
  if (std::getenv("LEGION_MESH_ALL")) {
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < art_.frames.size(); ++i) meshOf(static_cast<int>(i));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    PNR_INFO("Meshed %zu models in %.0f ms: %.1f MB, %.0fk triangles", meshes_.meshCount(), ms, meshes_.vertexBytes() / 1048576.0,
             meshes_.vertexBytes() / 20.0 / 3.0 / 1000.0);
  }

  if (!pickUnitArt(error)) return false;
  loadCampaigns();
  // Sound: base content then each mod's audio folder (a mod's file replaces the game's).
  audio_ = std::make_unique<GameAudio>();
  if (!opt_.noSound && (!app_.offscreen() || opt_.forceSound)) {
    std::string aerr;
    if (!audio_->init(content_.all("audio/"), aerr)) PNR_WARN("Sound off: %s", aerr.c_str());
    ui::setClickSound([this](bool tab) { audio_->ui(tab ? "ui_tab" : "ui_click"); });
  }
  fx_.init(&art_, &unitArt_, &types_, &weapons_, [](float lx, float ly) { return iso::worldToScreen(lx, ly); },
           [this](float lx, float ly) { return map_.heightAt(lx, ly) * kPxPerUnit; });
  if (!app_.offscreen()) {
    cursorArrow_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    cursorCross_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
  }

  mapsDir_ = app_.userDir() + "maps/";
  savesDir_ = app_.userDir() + "saves/";
  replaysDir_ = app_.userDir() + "replays/";
  std::error_code ec;
  for (const std::string& d : {mapsDir_, savesDir_, replaysDir_}) std::filesystem::create_directories(d, ec);
  if (firstStart) {
    showHelp_ = opt_.showHelp;  // first start: show the controls once
    settings_.save(app_.userDir() + "settings.json");
  } else {
    showHelp_ = false;
  }
  applySettings();
  initDevTools();
  watchContent();
  editor_ = std::make_unique<Editor>(*this);
  sidebar_ = std::make_unique<Sidebar>(*this);
  menus_ = std::make_unique<Menus>(*this);
  encyclopedia_ = std::make_unique<Encyclopedia>(*this);
  rulesEditor_ = std::make_unique<RulesEditor>(*this);

  cam_.setViewport(app_.pixelWidth(), app_.pixelHeight());
  cam_.set3D(settings_.view3D && !opt_.classic);
  if (cam_.is3D()) {
    cam_.setZoom3D(opt_.zoom);
    if (opt_.yaw > -999) cam_.setYaw(opt_.yaw * 0.01745329f);
    if (opt_.pitch > -999) cam_.setPitch(opt_.pitch * 0.01745329f);
  } else if (opt_.zoom >= 2.0f) {
    cam_.zoomStep(+1, {0, 0}), cam_.zoomStep(+1, {0, 0});
  } else if (opt_.zoom <= 0.5f) {
    cam_.zoomStep(-1, {0, 0});
  }

  speed_ = std::clamp(opt_.speed, 0.25f, 16.0f);
  if (opt_.showMenu) {
    startAttract();
  } else if (opt_.aiBattle || opt_.flowSelfTest || opt_.monkey > 0) {
    SkirmishSetup s;
    s.seed = opt_.seed;
    s.biome = opt_.biome;
    s.mapSize = opt_.mapSize;
    s.layout = opt_.layout;
    s.sides[0] = {true, opt_.side == 1 ? "rus" : "ger", opt_.flowSelfTest || opt_.monkey > 0, 2, 0, rgba8(196, 58, 44)};
    s.sides[1] = {true, "rus", false, 2, 1, rgba8(52, 96, 196)};
    if (opt_.sides == 3) {  // every side for itself
      s.sides[2] = {true, "gbr", false, 2, 2, rgba8(220, 180, 40)};
    } else if (opt_.sides >= 4) {
      s.sides[2] = {true, "ger", false, 2, 0, rgba8(220, 180, 40)};
      s.sides[3] = {true, "rus", false, 2, 1, rgba8(60, 170, 80)};
    }
    if (opt_.ffa)  // --ffa: every side for itself (four of them with --sides 4)
      for (size_t i = 0; i < s.sides.size(); ++i) s.sides[i].team = static_cast<int>(i);
    // --factions gbr,rus[,ger,gbr]: who plays each side.
    {
      size_t i = 0, at = 0;
      const std::string& f = opt_.factions;
      while (at < f.size() && i < s.sides.size()) {
        const size_t comma = std::min(f.find(',', at), f.size());
        const std::string id = f.substr(at, comma - at);
        if (pioneer::factionIndex(id) != 0 || id == "ger") s.sides[i].faction = id;
        ++i;
        at = comma + 1;
      }
    }
    startSkirmish(s);
    speed_ = std::clamp(opt_.speed, 0.25f, 16.0f);  // (a new skirmish starts at 1x: --speed asks for more)
    if (opt_.bench > 0) {
      runBench();
      quit_ = true;
    }
    if (opt_.side >= 0 && opt_.side < MAX_PLAYERS && players_[opt_.side].active) {
      setPlayer(static_cast<uint8_t>(opt_.side));
      if (players_[player_].startX >= 0) centerOn({players_[player_].startX, players_[player_].startY});
    }
  } else if (!opt_.loadMap.empty()) {
    MapFile f;
    if (!loadMap(opt_.loadMap, f, error)) return false;
    currentMapPath_ = opt_.loadMap;
    replaceWorld(std::move(f));
  } else {
    MapFile f;
    f.map.resize(opt_.mapSize, opt_.mapSize);
    f.map.generate(static_cast<Biome>(opt_.biome), opt_.seed, opt_.layout);
    f.map.name = "Random countryside";
    addTownBuildings(f);
    replaceWorld(std::move(f));
    setupStartingArmies();
    sim_->tick();  // apply the spawn commands
    beginRecording();
  }
  if (!opt_.showMenu) menus_->close();
  if (opt_.startInEditor) editor_->setActive(true);
  if (opt_.editorTab >= 0) editor_->showTab(opt_.editorTab, opt_.editorPage);
  if (!opt_.encyclopedia.empty()) encyclopedia_->open(types_.find(opt_.encyclopedia));
  if (!opt_.tool.empty()) {
    const size_t colon = opt_.tool.find(':');
    const std::string which = opt_.tool.substr(0, colon), arg = colon == std::string::npos ? "" : opt_.tool.substr(colon + 1);
    if (which == "rules") {
      rulesEditor_->open();
      if (!arg.empty()) rulesEditor_->select(types_.find(arg) >= 0 && types_.get(types_.find(arg)).isStructure() ? RulesEditor::Buildings
                                             : types_.find(arg) >= 0 ? RulesEditor::Units : RulesEditor::Weapons, arg);
    } else if (which == "mods") {
      menus_->showMods();
    }
  }
  auto split = [](const std::string& s) {
    const size_t c = s.find(':');
    return std::pair<std::string, int>{s.substr(0, c), c == std::string::npos ? 0 : std::atoi(s.c_str() + c + 1)};
  };
  if (!opt_.campaign.empty()) {
    const auto [id, n] = split(opt_.campaign);
    if (n > 0) menus_->openBriefing(id, n - 1);
    else menus_->openCampaign(id);
  }
  if (!opt_.mission.empty()) {
    const auto [id, n] = split(opt_.mission);
    std::string err;
    if (!startMission(id, std::max(1, n) - 1, err)) PNR_WARN("--mission %s: %s", opt_.mission.c_str(), err.c_str());
    else menus_->close();
  }
  PNR_INFO("Map %dx%d, %zu props, %zu units", map_.width(), map_.height(), map_.props().size(), sim_->unitCount());
  return true;
}

// Men in the selection (or in the selected buildings and trenches) come out.
void GameScene::evacuateSelection() {
  pruneSelection();
  if (selection_.empty()) return;
  Command c;
  c.type = CommandType::Unload;
  c.player = player_;
  c.units = selection_;
  queueOrder(std::move(c));
}

bool GameScene::selectionCanEvacuate() const {
  Registry& reg = sim_->registry();
  for (Entity e : selection_) {
    if (!reg.alive(e)) continue;
    if (reg.has<CGarrisoned>(e)) return true;
    if (const CGarrison* g = reg.pool<CGarrison>().tryGet(e.index); g && !g->occupants.empty()) return true;
  }
  return false;
}

void GameScene::openEncyclopedia(int type) {
  // With nothing pointed at, open on the first selected unit.
  if (type < 0 && !selection_.empty() && sim_->registry().alive(selection_.front()))
    type = sim_->registry().get<CUnit>(selection_.front()).type;
  encyclopedia_->open(type);
}

void GameScene::replaceWorld(MapFile&& file, bool keepCamera) {
  const Vec2f camera = cam_.is3D() ? cameraFocus() : cam_.center();
  selection_.clear();
  for (auto& g : groups_) g.clear();
  markers_.clear();
  fx_.clear();
  map_ = std::move(file.map);
  map_.recomputeInfo();
  {
    std::string err;
    if (!pickUnitArt(err)) PNR_WARN("%s", err.c_str());
  }
  sim_ = std::make_unique<Simulation>(map_, types_, weapons_, opt_.seed);
  sim_->setFlags(std::move(nextFlags_));
  nextFlags_.clear();
  terrain_.setMap(map_);
  if (audio_) {
    audio_->setBiome(map_.biome);
    if (mode_ != Mode::Menu) audio_->newBattle();
  }
  players_ = defaultPlayers();
  for (size_t i = 0; i < file.players.size() && i < players_.size(); ++i) players_[i] = file.players[i];
  if (nextDifficulty_ >= 0) {
    // A campaign mission at the chosen difficulty: the script's scaled
    // reinforcements follow it, and enemy computers play a step easier or harder.
    sim_->setMissionDifficulty(static_cast<uint8_t>(nextDifficulty_));
    uint8_t humanTeam = 255;
    for (const PlayerSetup& p : players_)
      if (p.active && p.human && humanTeam == 255) humanTeam = p.team;
    for (PlayerSetup& p : players_)
      if (p.active && !p.human && p.team != humanTeam)
        p.difficulty = static_cast<uint8_t>(std::clamp(static_cast<int>(p.difficulty) + nextDifficulty_ - 1, 0, 2));
  }
  applyPlayers();
  // Command the first human side (or keep the current one when it's still in play).
  if (!keepCamera || !players_[player_].active) {
    int pick = -1;
    for (uint8_t i = 0; i < MAX_PLAYERS && pick < 0; ++i)
      if (players_[i].active && players_[i].human) pick = i;
    for (uint8_t i = 0; i < MAX_PLAYERS && pick < 0; ++i)
      if (players_[i].active) pick = i;
    if (pick >= 0 && (!keepCamera || !players_[player_].active)) player_ = static_cast<uint8_t>(pick);
  }
  // Units exactly as saved: position, facing and health; then, in the same
  // tick, the names, script flags and stances missions give them.
  {
    constexpr uint32_t kPlacedTag = 0x504C4344;  // "PLCD"
    Command c;
    c.type = CommandType::SpawnList;
    c.tag = kPlacedTag;
    std::vector<Command> tags;
    for (const UnitPlacement& u : file.units) {
      const int t = types_.find(u.type);
      if (t < 0) {
        PNR_WARN("Map uses unknown unit type '%s' - skipped", u.type.c_str());
        continue;
      }
      if (!u.name.empty() || u.flags || u.stance) {
        Command tag;
        tag.type = CommandType::TagUnits;
        tag.player = NEUTRAL_OWNER;
        tag.tag = kPlacedTag;
        tag.indexBase = static_cast<uint32_t>(c.spawns.size());
        tag.text = u.name;
        tag.count = 0xFF;
        tag.amount = u.flags;
        tag.radius = u.stance ? u.stance + 1 : 0;
        tags.push_back(std::move(tag));
      }
      c.spawns.push_back({static_cast<uint16_t>(t), u.owner, {u.x, u.y}, u.facing, u.health});
    }
    if (!c.spawns.empty()) sim_->queue(std::move(c));
    for (Command& t : tags) sim_->queue(std::move(t));
  }
  // Computer players and the scenario script.
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    if (!players_[i].active || players_[i].human) continue;
    Command c;
    c.type = CommandType::SetController;
    c.player = i;
    c.amount = players_[i].difficulty;
    sim_->queue(std::move(c));
  }
  if (!file.scriptJson.empty()) {
    const nlohmann::json sj = nlohmann::json::parse(file.scriptJson, nullptr, false);
    scenario_ = sj.is_discarded() ? ScenarioScript{} : scriptFromJson(sj, &types_);
  } else {
    scenario_ = ScenarioScript{};
  }
  sim_->setScript(scenario_, false);
  messages_.clear();
  banner_.clear();
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    Command c;
    c.type = CommandType::SetCredits;
    c.player = i;
    c.amount = players_[i].credits;
    sim_->queue(std::move(c));
  }
  sim_->tick();  // apply teams, supplies and spawns
  updateCameraBounds();
  if (keepCamera) {
    if (cam_.is3D()) focusCamera(camera);
    else cam_.setCenter(camera);
  } else {
    const PlayerSetup& me = players_[player_];
    if (me.startX >= 0) centerOn({me.startX, me.startY});
    else centerOn({map_.widthLeptons() / 2, map_.heightLeptons() / 2});
  }
  // What a mission's script did on its very first tick (radio, camera, music, cinematic).
  for (const SimEvent& ev : sim_->events())
    switch (ev.type) {
      case EventType::ScriptMessage: case EventType::ObjectivesChanged: case EventType::CameraTo: case EventType::Hint:
      case EventType::Highlight: case EventType::Transmission: case EventType::CinematicChanged: case EventType::CameraPan:
      case EventType::PlaySound: case EventType::PlayMusic: case EventType::PlayAreaChanged:
        announce(ev);
        break;
      default:
        break;
    }
  if (editor_) editor_->onWorldReplaced();
  if (sidebar_) sidebar_->cancel();
  beginRecording();
}

void GameScene::applyPlayers() {
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    PlayerSetup& p = players_[i];
    p.color |= 0xFF000000u;
    teamColors_[i] = p.color;
    if (sim_) {
      Command c;
      c.type = CommandType::SetTeam;
      c.player = i;
      c.count = p.team;
      sim_->queue(std::move(c));
    }
  }
}

void GameScene::setPlayer(uint8_t p) {
  if (p >= MAX_PLAYERS) return;
  player_ = p;
  selection_.clear();
  attackMoveMode_ = false;
  showStatus("Now commanding " + players_[p].name + " (you see what they see)", 2.5f);
}

void GameScene::takeSnapshot() {
  snapshot_ = std::make_unique<MapFile>();
  snapshot_->map = map_;
  snapshot_->units = exportUnits();
  snapshot_->players = playerList();
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) snapshot_->players[i].credits = sim_->playerState(i).credits;
  showStatus("Snapshot taken: " + std::to_string(snapshot_->units.size()) + " units", 2.5f);
}

void GameScene::restoreSnapshot() {
  if (!snapshot_) return;
  MapFile copy;
  copy.map = snapshot_->map;
  copy.units = snapshot_->units;
  copy.players = snapshot_->players;
  const uint8_t me = player_;
  const uint64_t t0 = SDL_GetTicksNS();
  replaceWorld(std::move(copy), true);
  PNR_INFO("Snapshot restored in %.0f ms", (SDL_GetTicksNS() - t0) / 1e6);
  if (players_[me].active) player_ = me;
  showStatus("Snapshot restored", 2.5f);
}

std::vector<UnitPlacement> GameScene::exportUnits() {
  std::vector<UnitPlacement> out;
  Registry& reg = sim_->registry();
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    const CPosition& p = reg.pool<CPosition>().get(e);
    UnitPlacement pl{types_.get(u.type).id, u.owner, p.x, p.y, reg.pool<CFacing>().get(e).angle, u.health};
    const Entity h = reg.handle(e);
    pl.name = sim_->unitName(h);
    pl.flags = sim_->scriptFlags(h);
    pl.stance = static_cast<uint8_t>(u.stance);
    out.push_back(std::move(pl));
  }
  return out;
}

void GameScene::showStatus(const std::string& text, float seconds) {
  status_ = text;
  statusTime_ = seconds;
}

// Showcase: fast-forward into the game, then record it at normal speed with
// the camera easing to wherever the fiercest fighting is.
void GameScene::showcaseStep(float dt) {
  showTime_ += dt;
  paused_ = false;
  std::erase_if(hot_, [&](const Hot& h) { return showTime_ - h.t > 6.0f; });
  if (showPhase_ == 0) {
    speed_ = 16;
    if (sim_->tickCount() < static_cast<uint32_t>(opt_.showcaseWarm)) return;
    speed_ = 1;
    showPhase_ = 1;
    app_.startRecording(opt_.showcase, 2, 30);  // every other frame at 60 = 30 fps
  }
  ++showFrames_;
  if (showFrames_ >= opt_.showcaseSeconds * 60) {
    app_.stopRecording();
    quit_ = true;
    return;
  }
  // A new place to look every eight seconds, or when it has gone quiet here.
  const int32_t T = LEPTONS_PER_TILE;
  int hereCount = 0;
  for (const Hot& h : hot_)
    if (showFocus_.x >= 0 && std::abs(h.at.x - showFocus_.x) < 10 * T && std::abs(h.at.y - showFocus_.y) < 10 * T) ++hereCount;
  if (showTime_ - showFocusAt_ > 8.0f || (showTime_ - showFocusAt_ > 3.0f && hereCount == 0) || showFocus_.x < 0) {
    int best = 0;
    Vec2i pick{-1, -1};
    for (const Hot& a : hot_) {
      int n = 0;
      for (const Hot& b : hot_) n += std::abs(a.at.x - b.at.x) < 10 * T && std::abs(a.at.y - b.at.y) < 10 * T;
      if (n > best) best = n, pick = a.at;
    }
    if (pick.x < 0 && showFocus_.x < 0) pick = {players_[player_].startX, players_[player_].startY};
    if (pick.x >= 0) {
      showFocus_ = {static_cast<float>(pick.x), static_cast<float>(pick.y)};
      showFocusAt_ = showTime_;
      // Select our men there, as a player would.
      selection_.clear();
      for (uint32_t e : sim_->registry().pool<CUnit>().entities()) {
        const CUnit& u = sim_->registry().pool<CUnit>().get(e);
        const CPosition& p = sim_->registry().pool<CPosition>().get(e);
        if (u.owner == player_ && !types_.get(u.type).isStructure() && std::abs(p.x - pick.x) < 9 * T && std::abs(p.y - pick.y) < 9 * T)
          selection_.push_back(sim_->registry().handle(e));
      }
    }
  }
  if (showFocus_.x >= 0 && cam_.is3D()) {
    const Vec2f c = cameraFocus();
    const float k = std::min(1.0f, dt * 1.1f);
    focusCamera({c.x + (showFocus_.x - c.x) * k, c.y + (showFocus_.y - c.y) * k});
  } else if (showFocus_.x >= 0) {
    const Vec2f want = groundToScreenWorld(showFocus_.x, showFocus_.y);
    const Vec2f c = cam_.center();
    const float k = std::min(1.0f, dt * 1.1f);
    cam_.setCenter({c.x + (want.x - c.x) * k, c.y + (want.y - c.y) * k});
  }
}

// Crash hunting: random mouse and keyboard input, as a restless player would
// give it. Never quits, saves or opens files.
void GameScene::monkeyStep() {
  auto rnd = [&](uint32_t n) {
    monkeyRng_ = monkeyRng_ * 1664525u + 1013904223u;
    return n ? (monkeyRng_ >> 8) % n : 0u;
  };
  const float W = static_cast<float>(app_.pixelWidth()), H = static_cast<float>(app_.pixelHeight());
  if (rnd(120) == 0) {  // look at something of ours again
    std::vector<Vec2i> mine;
    for (uint32_t e : sim_->registry().pool<CUnit>().entities())
      if (sim_->registry().pool<CUnit>().get(e).owner == player_)
        mine.push_back({sim_->registry().pool<CPosition>().get(e).x, sim_->registry().pool<CPosition>().get(e).y});
    if (!mine.empty()) centerOn(mine[rnd(static_cast<uint32_t>(mine.size()))]);
  }
  for (int i = 0; i < opt_.monkey; ++i) {
    SDL_Event e{};
    const uint32_t what = rnd(100);
    // Keep the cursor mostly on the battlefield, sometimes on the sidebar.
    const float x = rnd(4) == 0 ? W - 1 - static_cast<float>(rnd(280)) : static_cast<float>(rnd(static_cast<uint32_t>(W - 300)));
    const float y = static_cast<float>(rnd(static_cast<uint32_t>(H)));
    if (what < 45) {
      e.type = SDL_EVENT_MOUSE_MOTION;
      e.motion.x = x;
      e.motion.y = y;
      SDL_WarpMouseInWindow(nullptr, x, y);
      SDL_PushEvent(&e);
    } else if (what < 80) {
      const uint8_t button = rnd(3) == 0 ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
      e.type = SDL_EVENT_MOUSE_MOTION;
      e.motion.x = x;
      e.motion.y = y;
      SDL_PushEvent(&e);
      e = SDL_Event{};
      e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      e.button.button = button;
      e.button.down = true;
      e.button.clicks = static_cast<uint8_t>(1 + (rnd(5) == 0));
      e.button.x = x;
      e.button.y = y;
      SDL_PushEvent(&e);
      // Sometimes a drag.
      const float x2 = rnd(3) == 0 ? x + static_cast<float>(rnd(300)) - 150 : x;
      const float y2 = rnd(3) == 0 ? y + static_cast<float>(rnd(200)) - 100 : y;
      e = SDL_Event{};
      e.type = SDL_EVENT_MOUSE_MOTION;
      e.motion.x = x2;
      e.motion.y = y2;
      SDL_PushEvent(&e);
      e = SDL_Event{};
      e.type = SDL_EVENT_MOUSE_BUTTON_UP;
      e.button.button = button;
      e.button.x = x2;
      e.button.y = y2;
      SDL_PushEvent(&e);
    } else if (what < 85) {
      e.type = SDL_EVENT_MOUSE_WHEEL;
      e.wheel.y = rnd(2) ? 1.0f : -1.0f;
      e.wheel.mouse_x = x;
      e.wheel.mouse_y = y;
      SDL_PushEvent(&e);
    } else {
      // A bound action, but never the menu, the editor, screenshots or fullscreen.
      const InputAction a = static_cast<InputAction>(rnd(ACTION_COUNT));
      if (a == InputAction::Menu || a == InputAction::Editor || a == InputAction::Screenshot ||
          a == InputAction::Debug || a == InputAction::Pause || a == InputAction::SpeedDown)
        continue;
      const KeyCombo& k = settings_.keys[static_cast<size_t>(a)][rnd(2)];
      if (k.empty()) continue;
      SDL_Keymod mod = SDL_KMOD_NONE;
      if (k.mods & kModCtrl) mod = static_cast<SDL_Keymod>(mod | SDL_KMOD_LCTRL);
      if (k.mods & kModShift) mod = static_cast<SDL_Keymod>(mod | SDL_KMOD_LSHIFT);
      if (k.mods & kModAlt) mod = static_cast<SDL_Keymod>(mod | SDL_KMOD_LALT);
      if (rnd(4) == 0 && !(k.mods & kModCtrl)) {  // number keys: control groups
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = static_cast<SDL_Keycode>(SDLK_1 + rnd(9));
        e.key.mod = rnd(2) ? SDL_KMOD_LCTRL : SDL_KMOD_NONE;
        e.key.down = true;
        SDL_PushEvent(&e);
        continue;
      }
      e.type = SDL_EVENT_KEY_DOWN;
      e.key.key = k.key;
      e.key.mod = mod;
      e.key.down = true;
      SDL_PushEvent(&e);
      e.type = SDL_EVENT_KEY_UP;
      e.key.down = false;
      SDL_PushEvent(&e);
    }
  }
}

void GameScene::updateCameraBounds() {
  const float w = static_cast<float>(map_.width()), h = static_cast<float>(map_.height());
  if (sim_ && sim_->playAreaLimited(player_) && mode_ != Mode::Menu && !editor_->active()) {
    // A mission's play area: the camera stays over it, with a little to spare.
    const TileRect r = sim_->playArea(player_);
    cam_.setBounds3D({std::max(0.0f, r.x0 - 6.0f), std::max(0.0f, r.y0 - 6.0f), std::min(w, r.x1 + 7.0f), std::min(h, r.y1 + 7.0f)});
  } else {
    cam_.setBounds3D({0, 0, w, h});
  }
  const float lift = map_.maxHeight() * kPxPerUnit;
  cam_.setBounds({-h * iso::HALF_W, -lift, w * iso::HALF_W, (w + h) * iso::HALF_H});
}

Vec2i GameScene::findLand(Vec2i near) const {
  const int cx = near.x / LEPTONS_PER_TILE, cy = near.y / LEPTONS_PER_TILE;
  for (int r = 0; r < 40; ++r)
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
        bool ok = true;
        for (int oy = -2; oy <= 2 && ok; ++oy)
          for (int ox = -2; ox <= 2 && ok; ++ox) ok = map_.passable(cx + dx + ox, cy + dy + oy, Locomotor::Wheeled);
        if (ok) return {(cx + dx) * LEPTONS_PER_TILE + 128, (cy + dy) * LEPTONS_PER_TILE + 128};
      }
  return near;
}

void GameScene::spawnAt(const std::string& typeId, uint8_t player, Vec2i at, int count) {
  const int t = types_.find(typeId);
  if (t < 0 || count <= 0) return;
  Command c;
  c.type = CommandType::Spawn;
  c.unitType = static_cast<uint16_t>(t);
  c.player = player;
  c.target = at;
  c.count = static_cast<uint16_t>(std::min(count, 65535));
  c.facing = player == 0 ? angleOfFacing(2) : angleOfFacing(10);
  sim_->queue(std::move(c));
}

// A starting base laid out like a military post: headquarters in the middle
// of a beaten-earth yard, barracks and workshops in rows behind it, a road
// to the gate, a sandbagged front with MG nests and watchtowers at the
// corners, wire and tank traps beyond, all facing the enemy.
void GameScene::buildBase(uint8_t player, Vec2i centre) {
  const int32_t T = LEPTONS_PER_TILE;
  const std::string f = players_[player].faction;
  // Forward: towards the middle of the map, along the nearer axis.
  const int32_t dx = map_.widthLeptons() / 2 - centre.x, dy = map_.heightLeptons() / 2 - centre.y;
  const int fx = std::abs(dx) >= std::abs(dy) ? (dx >= 0 ? 1 : -1) : 0;
  const int fy = fx == 0 ? (dy >= 0 ? 1 : -1) : 0;
  const int sx = -fy, sy = fx;  // to the side
  auto at = [&](int fwd, int side) { return Vec2i{centre.x + (fwd * fx + side * sx) * T, centre.y + (fwd * fy + side * sy) * T}; };
  // Placed one at a time, each on the nearest clear ground to its planned spot.
  auto add = [&](const std::string& id, int fwd, int side, int search = 6) {
    const int t = types_.find(f + "_" + id);
    if (t < 0) return;
    const Vec2i want = at(fwd, side);
    for (int r = 0; r <= search; ++r)
      for (int oy = -r; oy <= r; ++oy)
        for (int ox = -r; ox <= r; ++ox) {
          if (std::max(std::abs(ox), std::abs(oy)) != r) continue;
          const Vec2i p{want.x + ox * T, want.y + oy * T};
          if (!sim_->canPlace(t, sim_->footprintOrigin(t, p))) continue;
          Command c;
          c.type = CommandType::SpawnList;
          c.spawns.push_back({static_cast<uint16_t>(t), player, p, angleOfFacing(fx > 0 ? 0 : fx < 0 ? 8 : fy > 0 ? 4 : 12), -1});
          sim_->queue(std::move(c));
          sim_->tick();
          return;
        }
  };
  // Ground: a beaten-earth compound and a road from the gate to headquarters.
  auto paintTile = [&](Vec2i l, Layer layer, int amount) {
    const int tx = l.x / T, ty = l.y / T;
    if (!map_.inBounds(tx, ty)) return;
    for (int j = 0; j < SPLAT_PER_TILE; ++j)
      for (int i = 0; i < SPLAT_PER_TILE; ++i) map_.paint(tx * SPLAT_PER_TILE + i, ty * SPLAT_PER_TILE + j, layer, amount);
  };
  for (int fwd = -8; fwd <= 10; ++fwd)
    for (int side = -9; side <= 9; ++side) {
      const int edge = std::max(std::abs(side) - 7, std::max(-fwd - 6, fwd - 8));
      paintTile(at(fwd, side), Layer::Dirt, edge <= 0 ? 150 : edge == 1 ? 80 : 30);
    }
  for (int fwd = -1; fwd <= 16; ++fwd) {
    paintTile(at(fwd, 0), Layer::Road, 255);
    paintTile(at(fwd, 1), Layer::Road, 200);
  }
  for (int side = -7; side <= 7; ++side) paintTile(at(3, side), Layer::Road, 180);  // the cross street
  {
    const Vec2i a = at(-10, -11), b = at(18, 11);
    terrain_.markSplatDirty(std::min(a.x, b.x) / T * SPLAT_PER_TILE, std::min(a.y, b.y) / T * SPLAT_PER_TILE,
                            std::max(a.x, b.x) / T * SPLAT_PER_TILE + 1, std::max(a.y, b.y) / T * SPLAT_PER_TILE + 1);
  }
  // Clear the woods off the compound.
  {
    const Vec2i a = at(-9, -10), b = at(16, 10);
    const int32_t x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
    std::erase_if(map_.props(), [&](const Prop& p) { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; });
  }
  // Buildings in rows.
  add("yard", 0, -3);
  add("barracks", 5, -5);
  add("power", -5, -6);
  add("power", -5, -3);
  // Income from the start: a post office and a logistics warehouse behind the headquarters.
  add("post", -5, 3);
  add("warehouse", -5, 7);
  // The front: sandbags with a gate on the road, MG nests either side of
  // it, watchtowers at the corners, wire and tank traps beyond.
  for (int side = -8; side <= 8; ++side)
    if (std::abs(side) > 2) add("sandbags", 11, side, 0);
  add("mg_nest", 11, -2, 1);
  add("mg_nest", 11, 3, 1);
  add("tower", 10, -9, 1);
  add("tower", 10, 9, 1);
  add("tower", -8, -9, 1);
  for (int side = -9; side <= 9; side += 1)
    if (std::abs(side) > 3 && side % 4 != 0) add("wire", 13, side, 0);
  for (int side : {-6, -5, 5, 6}) add("traps", 15, side, 0);
}

// Selecting one man of a bound unit selects the whole unit (Alt: just him).
void GameScene::expandToGroups() {
  if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) return;
  Registry& reg = sim_->registry();
  std::vector<Entity> out;
  for (Entity e : selection_) {
    if (!reg.alive(e)) continue;
    if (const CGroup* g = sim_->groupOf(e)) {
      for (Entity m : g->members)
        if (reg.alive(m) && std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
    } else if (std::find(out.begin(), out.end(), e) == out.end()) {
      out.push_back(e);
    }
  }
  selection_ = std::move(out);
}

Entity GameScene::cardAt(Vec2f screenPx) const {
  const float ps = app_.pixelScale();
  const float x = screenPx.x / ps, y = screenPx.y / ps;
  for (auto it = cardHits_.rbegin(); it != cardHits_.rend(); ++it)
    if (x >= it->x0 && x <= it->x1 && y >= it->y0 && y <= it->y1 && sim_->registry().alive(it->group) &&
        sim_->registry().has<CGroup>(it->group) && selectableOwner(sim_->registry().get<CGroup>(it->group).owner))
      return it->group;
  return kNullEntity;
}

std::vector<Entity> GameScene::cardGroupsAt(Vec2f screenPx) const {
  const float ps = app_.pixelScale();
  const float x = screenPx.x / ps, y = screenPx.y / ps;
  Registry& reg = sim_->registry();
  for (auto it = cardHits_.rbegin(); it != cardHits_.rend(); ++it) {
    if (x < it->x0 || x > it->x1 || y < it->y0 || y > it->y1) continue;
    std::vector<Entity> out;
    for (Entity g : it->stack.empty() ? std::vector<Entity>{it->group} : it->stack)
      if (reg.alive(g) && reg.has<CGroup>(g) && selectableOwner(reg.get<CGroup>(g).owner)) out.push_back(g);
    if (!out.empty()) return out;
  }
  return {};
}

void GameScene::bindSelection() {
  pruneSelection();
  if (selection_.size() < 2) return;
  Command c;
  c.type = CommandType::BindGroup;
  c.player = player_;
  c.units = selection_;
  sim_->queue(std::move(c));
}

void GameScene::unbindSelection() {
  pruneSelection();
  if (selection_.empty()) return;
  Command c;
  c.type = CommandType::UnbindGroup;
  c.player = player_;
  c.units = selection_;
  sim_->queue(std::move(c));
  showStatus("Unit disbanded", 2);
}

void GameScene::setupStartingArmies() {
  const int32_t T = LEPTONS_PER_TILE;
  const int mid = map_.height() / 2;
  if (!opt_.demo && !opt_.noBases && map_.width() >= 96) {
    // Bases next to each side's home oil fields; the armies wait in front.
    const Vec2i gb = findLand({map_.width() * 17 / 100 * T, mid * T}), rb = findLand({map_.width() * 83 / 100 * T, mid * T});
    buildBase(0, gb);
    buildBase(1, rb);
  }
  // The demo battle starts with the armies a short drive apart.
  const bool bases = !opt_.demo && !opt_.noBases && map_.width() >= 96;
  const int west = map_.width() * (opt_.demo ? 50 : bases ? 40 : 14) / 128,
            east = map_.width() * (opt_.demo ? 78 : bases ? 88 : 114) / 128;
  const Vec2i g = findLand({west * T, mid * T}), r = findLand({east * T, mid * T});
  spawnAt("ger_medium_tank", 0, {g.x, g.y - 4 * T}, 6);
  spawnAt("ger_scout_car", 0, {g.x + 5 * T, g.y + 4 * T}, 4);
  spawnAt("ger_rifleman", 0, {g.x - 4 * T, g.y + 4 * T}, 20);
  spawnAt("rus_medium_tank", 1, {r.x, r.y + 4 * T}, 6);
  spawnAt("rus_scout_car", 1, {r.x - 5 * T, r.y - 4 * T}, 4);
  spawnAt("rus_rifleman", 1, {r.x + 4 * T, r.y - 4 * T}, 20);
  if (opt_.stressUnits > 0) {
    spawnAt("ger_rifleman", 0, {map_.width() / 3 * T, map_.height() / 3 * T}, opt_.stressUnits / 2);
    spawnAt("rus_medium_tank", 1, {map_.width() * 2 / 3 * T, map_.height() * 2 / 3 * T},
            opt_.stressUnits - opt_.stressUnits / 2);
  }
  if (opt_.demo) centerOn({(g.x + r.x) / 2, (g.y + r.y) / 2});
  else if (bases) centerOn({map_.width() * 21 / 100 * T, mid * T});
  else centerOn({g.x + 6 * T, g.y});
}

void GameScene::centerOn(Vec2i l) { focusCamera({static_cast<float>(l.x), static_cast<float>(l.y)}); }

void GameScene::focusCamera(Vec2f l) {
  if (!cam_.is3D()) {
    cam_.setCenter(groundToScreenWorld(l.x, l.y));
    return;
  }
  const float h = std::max<float>(map_.heightAt(l.x, l.y), map_.waterLevel());
  cam_.setTarget(worldPoint(l.x, l.y, h));
}

Vec2f GameScene::cameraFocus() const {
  if (cam_.is3D()) return {cam_.target().x * 256.0f, cam_.target().y * 256.0f};
  return groundAt({cam_.viewportW() * 0.5f, cam_.viewportH() * 0.5f});
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
Vec2f GameScene::groundToScreenWorld(float lx, float ly) const { return project(lx, ly, map_.heightAt(lx, ly)); }

Vec2f GameScene::project(float lx, float ly, float h) const {
  if (!cam_.is3D()) {
    Vec2f s = iso::worldToScreen(lx, ly);
    s.y -= h * kPxPerUnit;
    return s;
  }
  Vec2f s;
  cam_.project(worldPoint(lx, ly, h), s);
  return s;
}

Vec2f GameScene::groundAt(Vec2f screenPx) const {
  if (cam_.is3D()) return groundAt3D(screenPx);
  // Start on flat ground, then refine using the height found there.
  const Vec2f w = cam_.screenToWorld(screenPx);
  Vec2f l = iso::screenToWorld(w);
  for (int i = 0; i < 8; ++i) {
    const float h = map_.heightAt(l.x, l.y) * kPxPerUnit;
    l = iso::screenToWorld({w.x, w.y + h});
  }
  return l;
}

// A bridge is laid level, as high as the higher of its banks; its deck stands
// this far above that (the deck's voxels are 1/24 tile).
constexpr float kUnitsPerFineVoxel = static_cast<float>(HEIGHT_UNITS_PER_TILE) / 24.0f;
constexpr float kDeckLift = (kBridgeDeckZ - kBridgePierDepth) * kUnitsPerFineVoxel;

float GameScene::surfaceHeight(float lx, float ly) const {
  const float h = map_.heightAt(lx, ly);
  const int tx = static_cast<int>(lx) >> LEPTON_SHIFT, ty = static_cast<int>(ly) >> LEPTON_SHIFT;
  if (map_.iced(tx, ty) && map_.waterLevel() > h) return map_.waterLevel();  // standing on the ice
  if (!map_.inBounds(tx, ty) || bridgeLevel_.size() != static_cast<size_t>(map_.width()) * map_.height()) return h;
  const float level = bridgeLevel_[static_cast<size_t>(ty) * map_.width() + tx];
  return level > 0 ? level + kDeckLift : h;
}

void GameScene::refreshBridges() {
  Registry& reg = sim_->registry();
  uint64_t sig = 1469598103934665603ull;
  for (uint32_t e : reg.pool<CStructure>().entities()) {
    const uint16_t t = reg.pool<CUnit>().get(e).type;
    if (t < bridgeTypes_.size() && bridgeTypes_[t]) sig = (sig ^ e) * 1099511628211ull;
  }
  const size_t tiles = static_cast<size_t>(map_.width()) * map_.height();
  if (sig == bridgeSig_ && bridgeLevel_.size() == tiles) return;
  bridgeSig_ = sig;
  bridgeLevel_.assign(tiles, 0.0f);
  auto isBridge = [&](int x, int y) {
    const Entity o = sim_->structureAt(x, y);
    if (!o.valid()) return false;
    const uint16_t t = reg.get<CUnit>(o).type;
    return t < bridgeTypes_.size() && bridgeTypes_[t];
  };
  std::vector<uint8_t> seen(tiles, 0);
  for (int y = 0; y < map_.height(); ++y)
    for (int x = 0; x < map_.width(); ++x) {
      if (seen[static_cast<size_t>(y) * map_.width() + x] || !isBridge(x, y)) continue;
      // One bridge: every piece joined to this one, laid at the highest bank.
      std::vector<Vec2i> comp, stack{{x, y}};
      seen[static_cast<size_t>(y) * map_.width() + x] = 1;
      float level = static_cast<float>(map_.waterLevel());
      while (!stack.empty()) {
        const Vec2i t = stack.back();
        stack.pop_back();
        comp.push_back(t);
        level = std::max(level, map_.heightAt(t.x * 256.0f + 128, t.y * 256.0f + 128));
        for (const Vec2i d : {Vec2i{1, 0}, Vec2i{-1, 0}, Vec2i{0, 1}, Vec2i{0, -1}}) {
          const int nx = t.x + d.x, ny = t.y + d.y;
          if (!map_.inBounds(nx, ny) || seen[static_cast<size_t>(ny) * map_.width() + nx] || !isBridge(nx, ny)) continue;
          seen[static_cast<size_t>(ny) * map_.width() + nx] = 1;
          stack.push_back({nx, ny});
        }
      }
      for (const Vec2i& t : comp) bridgeLevel_[static_cast<size_t>(t.y) * map_.width() + t.x] = std::max(level, 1.0f);
    }
}

Vec2f GameScene::unitScreenWorld(uint32_t e) const {
  Registry& reg = sim_->registry();
  const CPosition& p = reg.pool<CPosition>().get(e);
  const CPrevPosition& q = reg.pool<CPrevPosition>().get(e);
  const float lx = q.x + (p.x - q.x) * alpha_, ly = q.y + (p.y - q.y) * alpha_;
  return project(lx, ly, surfaceHeight(lx, ly));  // up on the deck when crossing a bridge
}

Vec2i GameScene::mouseWorldLeptons() const {
  const Vec2f l = groundAt(mouse_);
  return {static_cast<int32_t>(std::lround(l.x)), static_cast<int32_t>(std::lround(l.y))};
}

// A building under the mouse holding men of the side we command.
Entity GameScene::garrisonUnder(Vec2f screenPx) const {
  Registry& reg = sim_->registry();
  const Vec2f g = groundAt(screenPx);
  const int tx = static_cast<int>(g.x) >> LEPTON_SHIFT, ty = static_cast<int>(g.y) >> LEPTON_SHIFT;
  const Entity b = sim_->structureAt(tx, ty);
  if (!b.valid() || !reg.has<CGarrison>(b)) return kNullEntity;
  for (Entity m : reg.get<CGarrison>(b).occupants)
    if (reg.alive(m) && selectableOwner(reg.get<CUnit>(m).owner)) return b;
  return kNullEntity;
}

Entity GameScene::pickUnit(Vec2f screenPx, bool ownOnly) const {
  Registry& reg = sim_->registry();
  const Vec2f w = cam_.screenToWorld(screenPx);
  Entity best;
  int64_t bestDepth = INT64_MIN;
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    if (reg.pool<CGarrisoned>().has(e) && !sim_->inTrench(reg.handle(e))) continue;  // inside a building: picked through it
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (ownOnly && !selectableOwner(u.owner)) continue;
    if (u.owner != player_) {
      const CPosition& lp = reg.pool<CPosition>().get(e);
      if (!watching(lp.x, lp.y)) continue;  // can't click what you can't see
    }
    const UnitTypeDef& d = types_.get(u.type);
    const CPosition& p = reg.pool<CPosition>().get(e);
    if (d.isStructure()) {
      // Structures: the ground under the mouse is inside the footprint (or the tall part above it).
      const Vec2f g = groundAt(screenPx);
      const CStructure& st = reg.pool<CStructure>().get(e);
      const bool onFoot = g.x >= st.tx * 256.0f && g.y >= st.ty * 256.0f && g.x < (st.tx + st.w) * 256.0f &&
                          g.y < (st.ty + st.h) * 256.0f;
      const Vec2f s = unitScreenWorld(e);
      // In 3D the boxes grow and shrink with the distance.
      const float k = cam_.is3D() ? cam_.scaleAt(worldPoint(static_cast<float>(p.x), static_cast<float>(p.y), map_.heightAt(static_cast<float>(p.x), static_cast<float>(p.y)))) : 1.0f;
      const float hw = d.footW * 12.0f * k, hh = (d.footH * 10.0f + 18.0f) * k;
      const bool onBody = d.category != UnitCategory::Fortification && std::abs(w.x - s.x) < hw && w.y < s.y && w.y > s.y - hh;
      if (!onFoot && !onBody) continue;
    } else {
      const Vec2f s = unitScreenWorld(e);
      const float k = cam_.is3D() ? std::max(0.6f, cam_.scaleAt(worldPoint(static_cast<float>(p.x), static_cast<float>(p.y), surfaceHeight(static_cast<float>(p.x), static_cast<float>(p.y))))) : 1.0f;
      const float hw = std::max(7.0f, d.radius * 0.2f) * k;
      const float hh = hw * 0.85f;
      if (std::abs(w.x - s.x) > hw || std::abs(w.y - (s.y - hh * 0.6f)) > hh) continue;
    }
    // Prefer units over the structure they stand in front of, then the frontmost.
    int64_t depth = iso::depthKey(p.x, p.y);
    if (cam_.is3D()) {
      // Nearest the camera wins.
      Vec2f px;
      float z = 1;
      cam_.project(worldPoint(static_cast<float>(p.x), static_cast<float>(p.y), surfaceHeight(static_cast<float>(p.x), static_cast<float>(p.y))), px, &z);
      depth = -static_cast<int64_t>(z * 1e9);
    }
    depth += d.isStructure() ? 0 : (int64_t{1} << 40);
    if (depth > bestDepth) {
      bestDepth = depth;
      best = reg.handle(e);
    }
  }
  return best;
}

void GameScene::selectInBox(Vec2f a, Vec2f b, bool add) {
  if (!add) selection_.clear();
  const RectF box = RectF::fromPoints(a, b);
  Registry& reg = sim_->registry();
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    if (!selectableOwner(reg.pool<CUnit>().get(e).owner)) continue;
    // Box selection picks units, not buildings (except with the editor's select tool).
    if (types_.get(reg.pool<CUnit>().get(e).type).isStructure() && !editor_->selectsAnyOwner()) continue;
    const Vec2f s = cam_.worldToScreen(unitScreenWorld(e));
    if (!box.contains({s.x, s.y - 6})) continue;
    if (reg.pool<CGarrisoned>().has(e) && !sim_->inTrench(reg.handle(e))) continue;
    const Entity h = reg.handle(e);
    if (std::find(selection_.begin(), selection_.end(), h) == selection_.end()) selection_.push_back(h);
  }
}

void GameScene::selectSameTypeOnScreen(Entity like) {
  Registry& reg = sim_->registry();
  if (!reg.alive(like)) return;
  const uint16_t type = reg.get<CUnit>(like).type;
  selection_.clear();
  const RectF view{0, 0, static_cast<float>(app_.pixelWidth()), static_cast<float>(app_.pixelHeight())};
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (!selectableOwner(u.owner) || u.type != type) continue;
    if (view.contains(cam_.worldToScreen(unitScreenWorld(e)))) selection_.push_back(reg.handle(e));
  }
}

void GameScene::pruneSelection() {
  Registry& reg = sim_->registry();
  std::erase_if(selection_, [&](Entity e) { return !reg.alive(e); });
}

bool GameScene::selectableOwner(uint8_t owner) const {
  return owner == player_ || (editor_ && editor_->selectsAnyOwner());
}

void GameScene::queueOrder(Command c) {
  // Each side's units are ordered separately (editor selections can mix sides).
  Registry& reg = sim_->registry();
  std::array<std::vector<Entity>, MAX_PLAYERS> byOwner;
  for (Entity e : c.units)
    if (reg.alive(e)) byOwner[reg.get<CUnit>(e).owner % MAX_PLAYERS].push_back(e);
  for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
    if (byOwner[p].empty()) continue;
    Command part = c;
    part.player = p;
    part.units = std::move(byOwner[p]);
    sim_->queue(std::move(part));
  }
}

int GameScene::selectionShape() {
  pruneSelection();
  Registry& reg = sim_->registry();
  int counts[static_cast<int>(Formation::Count)] = {};
  int movers = 0;
  for (Entity e : selection_)
    if (types_.get(reg.get<CUnit>(e).type).speed > 0) {
      ++counts[static_cast<int>(sim_->shapeOf(e))];
      ++movers;
    }
  if (!movers) return -1;
  int best = 0;
  for (int f = 1; f < static_cast<int>(Formation::Count); ++f)
    if (counts[f] > counts[best]) best = f;
  return best;
}

void GameScene::markSlots(const std::vector<Vec2i>& slots, uint32_t color) {
  if (slots.size() < 2 || slots.size() > 400) return;
  for (const Vec2i& p : slots) markers_.push_back({{static_cast<float>(p.x), static_cast<float>(p.y)}, 0, color, true});
}

void GameScene::setFormation(Formation f) {
  pruneSelection();
  Registry& reg = sim_->registry();
  std::vector<Entity> movers;
  for (Entity e : selection_)
    if (types_.get(reg.get<CUnit>(e).type).speed > 0 && !reg.has<CGarrisoned>(e)) movers.push_back(e);
  if (movers.empty()) return;
  static const char* names[] = {"Box", "Line", "Column", "Wedge", "Loose"};
  Command c;
  c.player = player_;
  c.units = movers;
  c.formation = f;
  if (const CFormation* march = sim_->formationOf(movers.front())) {
    // On the march: the same destination in the new shape.
    c.type = reg.get<COrder>(movers.front()).type == OrderType::AttackMove ? CommandType::AttackMove : CommandType::Move;
    c.target = march->dest;
  } else {
    // Standing: form up where they are, keeping the front they have.
    int64_t cx = 0, cy = 0;
    for (Entity e : movers) {
      cx += reg.get<CPosition>(e).x;
      cy += reg.get<CPosition>(e).y;
    }
    const Vec2i centre{static_cast<int32_t>(cx / static_cast<int64_t>(movers.size())),
                       static_cast<int32_t>(cy / static_cast<int64_t>(movers.size()))};
    int votes[FACINGS] = {};
    for (Entity e : movers) {
      const COrder& o = reg.get<COrder>(e);
      ++votes[facingOf(o.faceTo >= 0 ? static_cast<Angle>(o.faceTo) : reg.get<CFacing>(e).angle)];
    }
    int front = 0;
    for (int i = 1; i < FACINGS; ++i)
      if (votes[i] > votes[front]) front = i;
    c.type = CommandType::Move;
    c.formFacing = angleOfFacing(front);
    // A given front puts the first rank on the target: shift it so the whole
    // formation is centred on where the group stands.
    c.target = centre;
    const std::vector<Vec2i> slots = sim_->formationPreview(movers, centre, f, c.formFacing, 0);
    if (!slots.empty()) {
      int64_t sx = 0, sy = 0;
      for (const Vec2i& p : slots) {
        sx += p.x;
        sy += p.y;
      }
      c.target = {centre.x * 2 - static_cast<int32_t>(sx / static_cast<int64_t>(slots.size())),
                  centre.y * 2 - static_cast<int32_t>(sy / static_cast<int64_t>(slots.size()))};
    }
  }
  markSlots(sim_->formationPreview(movers, c.target, f, c.formFacing, 0), kSelectGreen);
  queueOrder(std::move(c));
  showStatus(std::string("Formation: ") + names[static_cast<int>(f)], 1.5f);
}

void GameScene::orderMove(Vec2i target, CommandType type, int formation) {
  pruneSelection();
  if (selection_.empty()) return;
  Command c;
  c.type = type;
  c.player = player_;
  c.target = target;
  c.units = selection_;
  c.formation = static_cast<Formation>(formation >= 0 ? formation : std::max(0, selectionShape()));
  c.forced = (SDL_GetModState() & SDL_KMOD_ALT) != 0;  // Alt: forced march, don't stop to fight
  markSlots(sim_->formationPreview(c.units, target, c.formation, -1, 0), type == CommandType::AttackMove ? kAttackRed : kSelectGreen);
  queueOrder(std::move(c));
  orderSound(type == CommandType::AttackMove);
  markers_.push_back({{static_cast<float>(target.x), static_cast<float>(target.y)}, 0,
                      type == CommandType::AttackMove ? kAttackRed : kSelectGreen});
}

bool GameScene::draggedFront(Vec2f startPx, Vec2f endPx, Vec2i& mid, Angle& facing, int32_t& width) {
  Registry& reg = sim_->registry();
  const Vec2f a = groundAt(startPx), b = groundAt(endPx);
  mid = {static_cast<int32_t>((a.x + b.x) * 0.5f), static_cast<int32_t>((a.y + b.y) * 0.5f)};
  // The front faces away from where the units are now.
  int64_t cx = 0, cy = 0, n = 0;
  for (Entity e : selection_)
    if (reg.alive(e) && types_.get(reg.get<CUnit>(e).type).speed > 0) {
      cx += reg.get<CPosition>(e).x;
      cy += reg.get<CPosition>(e).y;
      ++n;
    }
  if (!n) return false;
  cx /= n;
  cy /= n;
  float nx = -(b.y - a.y), ny = b.x - a.x;  // a normal to the line
  if (nx * (static_cast<float>(mid.x) - static_cast<float>(cx)) + ny * (static_cast<float>(mid.y) - static_cast<float>(cy)) < 0) {
    nx = -nx;
    ny = -ny;
  }
  facing = atan2Angle(static_cast<int32_t>(ny * 64), static_cast<int32_t>(nx * 64));
  width = static_cast<int32_t>(std::hypot(b.x - a.x, b.y - a.y));
  return true;
}

void GameScene::orderAttack(Entity target) {
  pruneSelection();
  if (selection_.empty() || !sim_->registry().alive(target)) return;
  Command c;
  c.type = CommandType::Attack;
  c.player = player_;
  c.targetUnit = target;
  c.units = selection_;
  queueOrder(std::move(c));
  orderSound(true);
  const CPosition& p = sim_->registry().get<CPosition>(target);
  markers_.push_back({{static_cast<float>(p.x), static_cast<float>(p.y)}, 0, kAttackRed});
}

// Right mouse button released. A click: attack an enemy under the mouse,
// send trucks to oil, set a rally point for factories, or move. A drag: move
// into the chosen formation with its front along the dragged line.
void GameScene::rightClick(Vec2f start, bool dragged, Vec2f end) {
  pruneSelection();
  if (selection_.empty()) return;
  Registry& reg = sim_->registry();
  const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
  if (ctrl && !dragged) {  // Ctrl+right-click: pick how to get there
    formationPicker_ = true;
    pickerScreen_ = start;
    pickerTarget_ = mouseWorldLeptons();
    return;
  }
  if (dragged) {
    Vec2i mid;
    Angle facing = 0;
    int32_t width = 0;
    if (!draggedFront(start, end, mid, facing, width)) return;
    Command c;
    c.type = ctrl ? CommandType::AttackMove : CommandType::Move;
    c.player = player_;
    c.target = mid;
    c.units = selection_;
    c.formation = static_cast<Formation>(std::max(0, selectionShape()));
    c.formFacing = facing;
    c.formWidth = width;
    c.forced = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    markSlots(sim_->formationPreview(c.units, mid, c.formation, c.formFacing, c.formWidth), ctrl ? kAttackRed : kSelectGreen);
    queueOrder(std::move(c));
    orderSound(ctrl);
    markers_.push_back({{static_cast<float>(mid.x), static_cast<float>(mid.y)}, 0,
                        ctrl ? kAttackRed : kSelectGreen});
    return;
  }
  Entity hit = pickUnit(start, false);
  // Infantry: right-click a building or bunker to go in, a trench or a wall
  // of sandbags to man it (on it or right next to it).
  if (!hit.valid() || !sim_->isTrench(hit)) {
    bool men = false;
    for (Entity e : selection_) men = men || types_.get(reg.get<CUnit>(e).type).infantry();
    if (men && !hit.valid()) {
      const Vec2f g = groundAt(start);
      const int tx = static_cast<int>(std::floor(g.x / 256.0f)), ty = static_cast<int>(std::floor(g.y / 256.0f));
      float best = 200.0f;  // leptons from the piece's middle
      for (int y = ty - 1; y <= ty + 1; ++y)
        for (int x = tx - 1; x <= tx + 1; ++x) {
          const Entity o = sim_->structureAt(x, y);
          if (!o.valid() || !sim_->isTrench(o) || !sim_->allied(reg.get<CUnit>(o).owner, player_)) continue;
          const float d = std::hypot(g.x - (x * 256.0f + 128), g.y - (y * 256.0f + 128));
          if (d < best) best = d, hit = o;
        }
    }
  }
  if (hit.valid()) {
    const UnitTypeDef& hd = types_.get(reg.get<CUnit>(hit).type);
    const bool shelter = hd.garrison > 0 && (reg.get<CUnit>(hit).owner >= MAX_PLAYERS || sim_->allied(reg.get<CUnit>(hit).owner, player_));
    const bool trench = hd.category == UnitCategory::Fortification && hd.cover > 0 && sim_->allied(reg.get<CUnit>(hit).owner, player_);
    std::vector<Entity> men, others;
    for (Entity e : selection_) (types_.get(reg.get<CUnit>(e).type).infantry() ? men : others).push_back(e);
    if ((shelter || trench) && !men.empty() && !(SDL_GetModState() & SDL_KMOD_CTRL)) {
      Command c;
      c.type = CommandType::Occupy;
      c.player = player_;
      c.units = men;
      c.targetUnit = hit;
      sim_->queue(std::move(c));
      const CPosition& hp = reg.get<CPosition>(hit);
      markers_.push_back({{static_cast<float>(hp.x), static_cast<float>(hp.y)}, 0, rgba8(236, 200, 96)});
      if (others.empty()) return;
      const std::vector<Entity> keep = selection_;
      selection_ = others;
      rightClick(start, dragged, end);
      selection_ = keep;
      return;
    }
  }
  if (hit.valid() && !sim_->allied(reg.get<CUnit>(hit).owner, player_) && types_.get(reg.get<CUnit>(hit).type).targetable) {
    orderAttack(hit);
    return;
  }
  // Engineers: right-click a damaged friendly vehicle or building to mend it.
  if (hit.valid()) {
    std::vector<Entity> engineers;
    for (Entity e : selection_)
      if (reg.alive(e) && sim_->canRepair(e, hit) && types_.get(reg.get<CUnit>(e).type).repair > 0) engineers.push_back(e);
    if (!engineers.empty()) {
      Command c;
      c.type = CommandType::Repair;
      c.player = player_;
      c.units = engineers;
      c.targetUnit = hit;
      sim_->queue(std::move(c));
      const CPosition& hp = reg.get<CPosition>(hit);
      markers_.push_back({{static_cast<float>(hp.x), static_cast<float>(hp.y)}, 0, rgba8(120, 200, 255)});
      std::vector<Entity> others;
      for (Entity e : selection_)
        if (std::find(engineers.begin(), engineers.end(), e) == engineers.end()) others.push_back(e);
      if (others.empty()) return;
      // Everyone else takes the click as usual.
      const std::vector<Entity> keep = selection_;
      selection_ = others;
      rightClick(start, dragged, end);
      selection_ = keep;
      return;
    }
  }
  const Vec2i g = mouseWorldLeptons();
  std::vector<Entity> trucks, producers, deployers, rest;
  for (Entity e : selection_) {
    if (reg.has<CHarvester>(e)) trucks.push_back(e);
    else if (reg.has<CProducer>(e)) producers.push_back(e);
    else if (hit == e && types_.get(reg.get<CUnit>(e).type).deploysTo >= 0) deployers.push_back(e);
    else rest.push_back(e);
  }
  // Supply trucks: right-click one of your logistics offices to put them on its run.
  const bool onOffice = hit.valid() && reg.get<CUnit>(hit).owner == player_ && types_.get(reg.get<CUnit>(hit).type).role == "office";
  if (!trucks.empty() && onOffice) {
    Command c;
    c.type = CommandType::Harvest;
    c.player = player_;
    c.units = trucks;
    c.targetUnit = hit;
    sim_->queue(std::move(c));
    showStatus(sim_->officeTrucks(hit) >= std::max(1, types_.get(reg.get<CUnit>(hit).type).convoy)
                   ? "That office has all the trucks it can use: they go where one is short"
                   : "Supply trucks assigned to the office",
               2.5f);
    trucks.clear();
  }
  rest.insert(rest.end(), trucks.begin(), trucks.end());
  if (!producers.empty()) {
    Command c;
    c.type = CommandType::SetRally;
    c.units = producers;
    c.target = g;
    sim_->queue(std::move(c));
    markers_.push_back({{static_cast<float>(g.x), static_cast<float>(g.y)}, 0, rgba8(236, 200, 96)});
  }
  if (!deployers.empty()) {
    Command c;
    c.type = CommandType::Deploy;
    c.units = deployers;
    sim_->queue(std::move(c));
  }
  if (!rest.empty()) {
    const auto keep = selection_;
    selection_ = rest;
    orderMove(g, ctrl ? CommandType::AttackMove : CommandType::Move);
    selection_ = keep;
  }
}

bool GameScene::fogEnabled() const { return !revealMap_ && (!editor_->active() || editorFog_); }

bool GameScene::watching(int32_t lx, int32_t ly) const {
  return !fogEnabled() || sim_->vision().visible(player_, lx >> LEPTON_SHIFT, ly >> LEPTON_SHIFT);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void GameScene::onEvent(const SDL_Event& e, bool uiMouse, bool uiKeys) {
  const float ps = app_.pixelScale();
  if (e.type == SDL_EVENT_MOUSE_MOTION) mouse_ = {e.motion.x * ps, e.motion.y * ps};
  if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
    mouse_ = {e.button.x * ps, e.button.y * ps};

  // Rebinding a key on the settings page.
  if (menus_->capturingKey()) {
    if (e.type == SDL_EVENT_KEY_DOWN) menus_->captureKey(e.key);
    return;
  }
  // The developer console and profiler work everywhere, menus included.
  if (devToolsEvent(e)) return;
  // Menus take everything while open; in the main menu the battle behind is scenery.
  if (mode_ == Mode::Menu || menus_->wantsInput()) {
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE && mode_ != Mode::Menu) menus_->close();
    return;
  }
  // Replays: look around, but no orders.
  if (mode_ == Mode::Replay) {
    if (e.type == SDL_EVENT_KEY_DOWN && !uiKeys) {
      const SDL_Keycode k = e.key.key;
      if (settings_.pressed(InputAction::Menu, e.key)) menus_->openInGameMenu();
      else if (k == SDLK_V) revealMap_ = !revealMap_;
      else if (settings_.pressed(InputAction::Pause, e.key)) paused_ = !paused_;
      else if (settings_.pressed(InputAction::SpeedUp, e.key)) speed_ = std::min(16.0f, speed_ * 2);
      else if (settings_.pressed(InputAction::SpeedDown, e.key)) speed_ = std::max(0.25f, speed_ / 2);
      else if (settings_.pressed(InputAction::NextSide, e.key))
        for (int i = 1; i <= MAX_PLAYERS; ++i) {
          const uint8_t p = static_cast<uint8_t>((player_ + i) % MAX_PLAYERS);
          if (players_[p].active) {
            player_ = p;
            showStatus("Watching " + players_[p].name, 2);
            break;
          }
        }
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION && mmbDown_) mmbDrag(e.motion.xrel * ps, e.motion.yrel * ps);
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_MIDDLE) mmbDown_ = true;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_MIDDLE) mmbDown_ = false;
    if (e.type == SDL_EVENT_MOUSE_WHEEL && !uiMouse && e.wheel.y != 0) cam_.zoomStep(e.wheel.y > 0 ? +1 : -1, mouse_);
    return;
  }
  // A mission's cinematic: the script has the camera; the menu, pause and speed keys still work.
  if (missionInputBlocked()) {
    if (e.type == SDL_EVENT_KEY_DOWN && !uiKeys) {
      if (settings_.pressed(InputAction::Menu, e.key)) menus_->openInGameMenu();
      else if (settings_.pressed(InputAction::Pause, e.key)) paused_ = !paused_;
      else if (settings_.pressed(InputAction::SpeedUp, e.key)) speed_ = std::min(16.0f, speed_ * 2);
      else if (settings_.pressed(InputAction::SpeedDown, e.key)) speed_ = std::max(0.25f, speed_ / 2);
    }
    lmbDown_ = rmbDown_ = mmbDown_ = dragging_ = rmbDragging_ = false;
    return;
  }
  // The editor gets first look at input while it is open, then the sidebar's
  // placement and sell modes.
  if (editor_->onEvent(e, uiMouse, uiKeys)) return;
  if (sidebar_->onEvent(e, uiMouse)) return;

  switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
      if (mmbDown_) mmbDrag(e.motion.xrel * ps, e.motion.yrel * ps);
      if (lmbDown_ && !dragging_ && (mouse_ - dragStart_).length() > 6.0f) dragging_ = true;
      if (rmbDown_ && !rmbDragging_ && (mouse_ - rmbStart_).length() > 14.0f) rmbDragging_ = true;
      break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (uiMouse) break;
      if (e.button.button == SDL_BUTTON_LEFT) {
        if (attackMoveMode_) {
          attackMoveMode_ = false;
          const Entity hit = pickUnit(mouse_, false);
          if (hit.valid() && sim_->registry().get<CUnit>(hit).owner != player_) orderAttack(hit);
          else orderMove(mouseWorldLeptons(), CommandType::AttackMove);
          break;
        }
        lmbDown_ = true;
        dragging_ = false;
        dragStart_ = mouse_;
      } else if (e.button.button == SDL_BUTTON_RIGHT) {
        if (attackMoveMode_) {
          attackMoveMode_ = false;
          break;
        }
        // Decided on release: a click orders, a drag lays out a formation front.
        rmbDown_ = true;
        rmbDragging_ = false;
        rmbStart_ = mouse_;
      } else if (e.button.button == SDL_BUTTON_MIDDLE) {
        mmbDown_ = true;
      }
      break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (e.button.button == SDL_BUTTON_MIDDLE) mmbDown_ = false;
      if (e.button.button == SDL_BUTTON_RIGHT && rmbDown_) {
        rmbDown_ = false;
        rightClick(rmbStart_, rmbDragging_, mouse_);
        rmbDragging_ = false;
      }
      if (e.button.button == SDL_BUTTON_LEFT && lmbDown_) {
        lmbDown_ = false;
        const bool add = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        if (dragging_) {
          selectInBox(dragStart_, mouse_, add);
        } else if (const Entity card = cardAt(mouse_); card.valid()) {
          // A unit card: the whole bound unit (double-click: and look at it).
          const uint64_t now = SDL_GetTicks();
          if (!add) selection_.clear();
          for (Entity g : cardGroupsAt(mouse_))  // a stack card: every unit in it
            for (Entity m : sim_->registry().get<CGroup>(g).members)
              if (sim_->registry().alive(m) && std::find(selection_.begin(), selection_.end(), m) == selection_.end())
                selection_.push_back(m);
          if (card == lastCard_ && now - lastClickMs_ < 350 && !selection_.empty())
            centerOn({sim_->registry().get<CPosition>(selection_.front()).x, sim_->registry().get<CPosition>(selection_.front()).y});
          lastCard_ = card;
          lastClickMs_ = now;
        } else {
          const Entity hit = pickUnit(mouse_, true);
          const uint64_t now = SDL_GetTicks();
          if (hit.valid() && hit == lastClicked_ && now - lastClickMs_ < 350) {
            selectSameTypeOnScreen(hit);
          } else if (hit.valid()) {
            if (!add) selection_.clear();
            auto it = std::find(selection_.begin(), selection_.end(), hit);
            if (it == selection_.end()) selection_.push_back(hit);
            else if (add) selection_.erase(it);
          } else if (const Entity house = garrisonUnder(mouse_); house.valid()) {
            // A building our men hold: select the men inside.
            if (!add) selection_.clear();
            for (Entity m : sim_->registry().get<CGarrison>(house).occupants)
              if (sim_->registry().alive(m)) selection_.push_back(m);
          } else if (!add) {
            selection_.clear();
          }
          lastClicked_ = hit;
          lastClickMs_ = now;
        }
        dragging_ = false;
      }
      break;

    case SDL_EVENT_MOUSE_WHEEL:
      if (!uiMouse && e.wheel.y != 0) cam_.zoomStep(e.wheel.y > 0 ? +1 : -1, mouse_);
      break;

    case SDL_EVENT_KEY_DOWN: {
      if (uiKeys) break;
      const SDL_Keycode k = e.key.key;
      const bool ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
      const Settings& K = settings_;
      if (K.pressed(InputAction::Menu, e.key)) {
        if (k == SDLK_ESCAPE && encyclopedia_->isOpen()) encyclopedia_->close();
        else if (k == SDLK_ESCAPE && formationPicker_) formationPicker_ = false;
        else if (k == SDLK_ESCAPE && attackMoveMode_) attackMoveMode_ = false;
        else if (k == SDLK_ESCAPE && !selection_.empty()) selection_.clear();
        else menus_->openInGameMenu();
      } else if (K.pressed(InputAction::AttackMove, e.key)) {
        pruneSelection();
        attackMoveMode_ = !selection_.empty();
      } else if (K.pressed(InputAction::Help, e.key)) {
        showHelp_ = !showHelp_;
      } else if (K.pressed(InputAction::Encyclopedia, e.key)) {
        if (encyclopedia_->isOpen()) encyclopedia_->close();
        else openEncyclopedia(sidebar_->hoveredType() >= 0 ? sidebar_->hoveredType() : -1);
      } else if (K.pressed(InputAction::Editor, e.key)) {
        editor_->setActive(!editor_->active());
      } else if (K.pressed(InputAction::Debug, e.key)) {
        showDebug_ = !showDebug_;
      } else if (K.pressed(InputAction::RadioLog, e.key)) {
        showRadioLog_ = !showRadioLog_;
      } else if (K.pressed(InputAction::RangeRings, e.key)) {
        // Selection -> every unit -> off -> selection.
        settings_.rangeRings = settings_.rangeRings == 1 ? 2 : settings_.rangeRings == 2 ? 0 : 1;
        static const char* names[] = {"Range circles off", "Range circles: selection and the unit under the mouse",
                                      "Range circles: every unit in view"};
        showStatus(names[settings_.rangeRings]);
        saveSettings();
      } else if (K.pressed(InputAction::ToggleInfo, e.key)) {
        settings_.showInfoPanel = !settings_.showInfoPanel;
        saveSettings();
      } else if (K.pressed(InputAction::Screenshot, e.key)) {
        const std::time_t t = std::time(nullptr);
        char name[64];
        std::strftime(name, sizeof name, "screenshot_%Y%m%d_%H%M%S.png", std::localtime(&t));
        app_.requestScreenshot(app_.userDir() + name);
        showStatus(std::string("Screenshot saved to ") + app_.userDir() + name, 4);
      } else if (K.pressed(InputAction::Pause, e.key)) {
        paused_ = !paused_;
      } else if (K.pressed(InputAction::NextSide, e.key)) {
        for (int i = 1; i <= MAX_PLAYERS; ++i) {  // next active side
          const uint8_t p = static_cast<uint8_t>((player_ + i) % MAX_PLAYERS);
          if (players_[p].active) {
            setPlayer(p);
            break;
          }
        }
      } else if (K.pressed(InputAction::NextFormation, e.key)) {
        const int shape = selectionShape();
        if (shape >= 0) setFormation(static_cast<Formation>((shape + 1) % static_cast<int>(Formation::Count)));
      } else if (K.pressed(InputAction::Stop, e.key)) {
        pruneSelection();
        Command c;
        c.type = CommandType::Stop;
        c.units = selection_;
        sim_->queue(std::move(c));
      } else if (K.pressed(InputAction::SelectAll, e.key)) {
        selection_.clear();
        Registry& reg = sim_->registry();
        for (uint32_t i : reg.pool<CUnit>().entities())
          if (reg.pool<CUnit>().get(i).owner == player_) selection_.push_back(reg.handle(i));
      } else if (K.pressed(InputAction::Deploy, e.key)) {
        pruneSelection();
        Command c;
        c.type = CommandType::Deploy;
        c.units = selection_;
        sim_->queue(std::move(c));
      } else if (K.pressed(InputAction::Evacuate, e.key)) {
        evacuateSelection();
      } else if (K.pressed(InputAction::BindGroup, e.key)) {
        bindSelection();
      } else if (K.pressed(InputAction::UnbindGroup, e.key)) {
        unbindSelection();
      } else if (k == SDLK_DELETE && !editor_->active() && showDebug_) {  // debug only
        Command c;
        c.type = CommandType::Remove;
        c.units = selection_;
        sim_->queue(std::move(c));
        selection_.clear();
      } else if (K.pressed(InputAction::CentreSelection, e.key) && !selection_.empty()) {
        pruneSelection();
        if (!selection_.empty()) {
          const CPosition& p = sim_->registry().get<CPosition>(selection_.front());
          centerOn({p.x, p.y});
        }
      } else if (k >= SDLK_0 && k <= SDLK_9) {
        const size_t g = static_cast<size_t>(k - SDLK_0);
        if (ctrl) {
          pruneSelection();
          groups_[g] = selection_;
        } else {
          std::erase_if(groups_[g], [&](Entity x) { return !sim_->registry().alive(x); });
          selection_ = groups_[g];
        }
      } else if (K.pressed(InputAction::SpeedUp, e.key)) {
        speed_ = std::min(8.0f, speed_ * 2);
      } else if (K.pressed(InputAction::SpeedDown, e.key)) {
        speed_ = std::max(0.25f, speed_ / 2);
      }
      break;
    }
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Simulation & per-frame update
// ---------------------------------------------------------------------------
void GameScene::editorOpen(bool on) { editor_->setActive(on); }

// The player's order heard: a radio click, a whistle for an infantry attack, an engine.
void GameScene::orderSound(bool attack) {
  if (!audio_ || mode_ != Mode::Game) return;
  Registry& reg = sim_->registry();
  int inf = 0, veh = 0;
  float x = 0, y = 0;
  for (Entity e : selection_) {
    if (!reg.alive(e)) continue;
    const UnitTypeDef& d = types_.get(reg.get<CUnit>(e).type);
    if (d.isStructure()) continue;
    (d.infantry() ? inf : veh)++;
    const CPosition& p = reg.get<CPosition>(e);
    x += static_cast<float>(p.x);
    y += static_cast<float>(p.y);
  }
  const int n = inf + veh;
  if (n == 0) return;
  audio_->onOrder(attack, inf, veh, x / n / LEPTONS_PER_TILE, y / n / LEPTONS_PER_TILE);
}

// Where the ear is, and what is moving and burning around it.
void GameScene::updateAudio(float dt) {
  if (!audio_ || !sim_) return;
  pioneer::audio::Listener ear;
  const Vec2f g = cam_.is3D() ? cameraFocus() : iso::screenToWorld(cam_.center());
  ear.x = g.x / LEPTONS_PER_TILE;
  ear.y = g.y / LEPTONS_PER_TILE;
  ear.zoom = cam_.zoom();
  ear.viewTiles = static_cast<float>(cam_.viewportW()) / (2.0f * cam_.zoom()) / 45.25f;
  audioBedsTimer_ -= dt;
  static AudioBeds beds;
  if (audioBedsTimer_ <= 0) {
    audioBedsTimer_ = 0.2f;
    beds = AudioBeds{};
    Registry& reg = sim_->registry();
    const float r2 = (ear.viewTiles * 1.3f) * (ear.viewTiles * 1.3f);
    auto& movers = reg.pool<CMover>();
    for (uint32_t e : movers.entities()) {
      if (!movers.get(e).active || !reg.pool<CUnit>().has(e)) continue;
      const CPosition& p = reg.pool<CPosition>().get(e);
      const float x = static_cast<float>(p.x) / LEPTONS_PER_TILE, y = static_cast<float>(p.y) / LEPTONS_PER_TILE;
      if ((x - ear.x) * (x - ear.x) + (y - ear.y) * (y - ear.y) > r2) continue;
      const UnitTypeDef& d = types_.get(reg.pool<CUnit>().get(e).type);
      if (d.infantry()) {
        ++beds.marching;
        beds.marchX += x;
        beds.marchY += y;
      } else if (d.locomotor == Locomotor::Wheeled) {
        ++beds.wheeled;
        beds.wheeledX += x;
        beds.wheeledY += y;
      } else {
        ++beds.tracked;
        beds.trackedX += x;
        beds.trackedY += y;
      }
    }
    auto& structs = reg.pool<CStructure>();
    for (uint32_t e : structs.entities()) {
      const CUnit& u = reg.pool<CUnit>().get(e);
      const UnitTypeDef& d = types_.get(u.type);
      if (d.category == UnitCategory::Fortification || u.health * 10 > d.maxHealth * 4) continue;
      const CPosition& p = reg.pool<CPosition>().get(e);
      const float x = static_cast<float>(p.x) / LEPTONS_PER_TILE, y = static_cast<float>(p.y) / LEPTONS_PER_TILE;
      if ((x - ear.x) * (x - ear.x) + (y - ear.y) * (y - ear.y) > r2) continue;
      ++beds.burning;
      beds.fireX += x;
      beds.fireY += y;
    }
    auto centre = [](int n, float& x, float& y) {
      if (n > 0) {
        x /= static_cast<float>(n);
        y /= static_cast<float>(n);
      }
    };
    centre(beds.marching, beds.marchX, beds.marchY);
    centre(beds.wheeled, beds.wheeledX, beds.wheeledY);
    centre(beds.tracked, beds.trackedX, beds.trackedY);
    centre(beds.burning, beds.fireX, beds.fireY);
  }
  audio_->update(dt, ear, mode_ == Mode::Menu, menus_ && menus_->inGameMenuOpen(), paused_, speed_, beds);
}

void GameScene::applySettings() {
  app_.setFpsLimit(settings_.fpsLimit);
  app_.setVsync(settings_.vsync);
  if (audio_) {
    AudioVolumes v;
    v.master = settings_.volMaster;
    v.sfx = settings_.volEffects;
    v.voice = settings_.volVoices;
    v.music = settings_.volMusic;
    v.ambience = settings_.volAmbience;
    v.ui = settings_.volInterface;
    audio_->setVolumes(v);
  }
  // Switching between the 3D view and the classic one keeps looking at the same place.
  const bool want3D = settings_.view3D && !opt_.classic;
  if (want3D != cam_.is3D() && sim_) {
    const Vec2f f = cameraFocus();
    cam_.set3D(want3D);
    updateCameraBounds();
    focusCamera(f);
  }
}

void GameScene::saveSettings() { settings_.save(app_.userDir() + "settings.json"); }

void GameScene::onTick() {
  // Replays feed the recorded orders in at the tick they were given.
  if (mode_ == Mode::Replay) {
    if (sim_->tickCount() >= replayEnd_) {
      if (!paused_) showStatus("End of the replay", 5);
      paused_ = true;
      return;
    }
    while (replayNext_ < replayLog_.size() && replayLog_[replayNext_].tick <= sim_->tickCount())
      sim_->queueInternal(replayLog_[replayNext_++].cmd);
  }
  if (opt_.demo && sim_->tickCount() == 3) {
    Registry& reg = sim_->registry();
    std::vector<Entity> ger, rus;
    for (uint32_t i : reg.pool<CUnit>().entities())
      (reg.pool<CUnit>().get(i).owner == 0 ? ger : rus).push_back(reg.handle(i));
    const int32_t T = LEPTONS_PER_TILE;
    // Both sides advance on each other, fighting whatever they meet.
    selection_ = ger;
    if (!ger.empty() && !rus.empty()) {
      const CPosition& gp = reg.get<CPosition>(ger.front());
      const CPosition& rp = reg.get<CPosition>(rus.front());
      orderMove({rp.x + 4 * T, rp.y}, CommandType::AttackMove);
      Command c;
      c.type = CommandType::AttackMove;
      c.player = 1;
      c.units = rus;
      c.target = {gp.x - 4 * T, gp.y};
      sim_->queue(std::move(c));
    }
  }
  sim_->tick();
  if (mode_ == Mode::Replay && !replayDrift_) {
    // A replay made by other rules or another version stops matching the
    // game it recorded: say so once, rather than show a battle that never was.
    const HashTrace::Mismatch m = replayTrace_.check(*sim_);
    if (m.found) {
      replayDrift_ = m.tick;
      PNR_WARN_CH(Sim, "Replay out of step at tick %u: recorded %016llx, now %016llx", m.tick,
                  static_cast<unsigned long long>(m.expected), static_cast<unsigned long long>(m.got));
      showStatus("This replay no longer matches the game it recorded (from " + std::to_string(m.tick / 900) + ":" +
                     (m.tick / 15 % 60 < 10 ? "0" : "") + std::to_string(m.tick / 15 % 60) +
                     "). It was made with other rules or another version.",
                 8);
    }
  }
  editor_->onSimTick(sim_->spawnLog());
  const auto& events = sim_->events();
  for (const SimEvent& ev : events) {
    const bool seen = watching(ev.at.x, ev.at.y) || (ev.type == EventType::Fire && watching(ev.from.x, ev.from.y));
    fx_.onEvent(ev, seen, teamColors_);
    if (audio_) audio_->onEvent(ev, *sim_, player_, mode_ == Mode::Game);
    announce(ev);
    if (!opt_.showcase.empty() && (ev.type == EventType::Impact || ev.type == EventType::UnitDied) && seen)
      hot_.push_back({ev.at, showTime_});
    if (mode_ == Mode::Menu && (ev.type == EventType::Impact || ev.type == EventType::UnitDied) && (frame_ % 90 == 0 || attractFocus_.x < 0))
      attractFocus_ = {static_cast<float>(ev.at.x), static_cast<float>(ev.at.y)};
    if (ev.type == EventType::StructurePlaced) {
      // Clear trees and rocks from under a new structure.
      const UnitTypeDef& d = types_.get(ev.unitType);
      const int32_t x0 = ev.at.x - d.footW * 128, y0 = ev.at.y - d.footH * 128;
      std::erase_if(map_.props(), [&](const Prop& p) {
        return p.x >= x0 && p.y >= y0 && p.x < x0 + d.footW * 256 && p.y < y0 + d.footH * 256;
      });
    }
  }
}

// Messages for the side being commanded.
void GameScene::announce(const SimEvent& ev) {
  switch (ev.type) {
    case EventType::UnitReady:
      if (ev.owner != player_) break;
      if (mode_ != Mode::Game) break;
      if (types_.get(ev.unitType).isStructure()) showStatus("Construction complete: " + types_.get(ev.unitType).name + " ready to place", 3);
      else showStatus(types_.get(ev.unitType).name + " ready", 2);
      break;
    case EventType::CannotBuild: {
      if (ev.owner != player_) break;
      const UnitTypeDef& d = types_.get(ev.unitType);
      if (sim_->playerState(player_).credits < d.cost) showStatus("Not enough supplies", 2.5f);
      else if (d.deploysTo >= 0) showStatus("Can't deploy here: needs flat, open ground", 2.5f);
      else showStatus("Can't build " + d.name + " there", 2.5f);
      break;
    }
    case EventType::PointCaptured:
      if (mode_ != Mode::Game) break;
      if (ev.owner == player_) showStatus("Logistics point taken: the office is up and three supply trucks are on their way", 4);
      else if (sim_->allied(ev.owner, player_)) showStatus("An ally has taken a logistics point", 3);
      break;
    case EventType::ConvoyArrived:
      if (ev.owner == player_ || revealMap_) {
        char txt[32];
        std::snprintf(txt, sizeof txt, "+$%d", ev.amount);
        floaters_.push_back({{static_cast<float>(ev.at.x), static_cast<float>(ev.at.y)}, 0, txt});
      }
      break;
    case EventType::ScoutFallsBack:
      // Once in a while, not for every scout every time.
      if (ev.owner == player_ && mode_ == Mode::Game && frame_ - lastScoutNote_ > 600) {
        lastScoutNote_ = frame_;
        showStatus("Scout falling back: enemy close", 2.5f);
      }
      break;
    case EventType::ScriptMessage:
      if (ev.owner == ANY_PLAYER || ev.owner == player_) messages_.push_back({ev.text, 0});
      break;
    case EventType::ObjectivesChanged:
      if (ev.owner == ANY_PLAYER || sim_->allied(ev.owner, player_)) showStatus("Objectives updated", 2.5f);
      break;
    case EventType::CameraTo:
      if ((ev.owner == ANY_PLAYER || ev.owner == player_) && ev.at.x >= 0 && mode_ == Mode::Game) {
        panDuration_ = 0;
        centerOn(ev.at);
      }
      break;
    case EventType::Transmission:
    case EventType::CinematicChanged:
    case EventType::CameraPan:
    case EventType::PlaySound:
    case EventType::PlayMusic:
    case EventType::CountdownDone:
    case EventType::PlayAreaChanged:
      onMissionEvent(ev);
      break;
    case EventType::PlayerWon:
      if (mode_ == Mode::Menu) break;
      banner_ = sim_->allied(ev.owner, player_) ? "VICTORY" : players_[ev.owner].name + " wins";
      if (!sim_->allied(ev.owner, player_) && players_[player_].human) banner_ = "DEFEAT";
      bannerTime_ = 8;
      if (audio_ && mode_ == Mode::Game) audio_->endOfBattle(sim_->allied(ev.owner, player_));
      if (mode_ == Mode::Game) missionOver(sim_->allied(ev.owner, player_));
      break;
    case EventType::PlayerDefeated: {
      if (mode_ == Mode::Menu) break;
      banner_ = players_[ev.owner].name + " has been defeated";
      bannerTime_ = 6;
      // Last side standing (with its allies) wins.
      bool enemiesLeft = false;
      for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        if (players_[p].active && !sim_->allied(p, player_) && !sim_->playerState(p).defeated) enemiesLeft = true;
      // A mission whose script declares victory decides for itself when it is won.
      bool scripted = false;
      for (const Trigger& t : scenario_.triggers)
        for (const Action& a : t.actions) scripted = scripted || a.type == ActionType::Victory;
      if (ev.owner == player_) banner_ = "DEFEAT";
      else if (!enemiesLeft && !scripted && !sim_->playerState(player_).defeated) banner_ = "VICTORY";
      else if (scripted && ev.owner != player_) {
        banner_.clear();  // the enemy's last wave is gone, not the enemy
        break;
      }
      if (audio_ && mode_ == Mode::Game && (banner_ == "VICTORY" || banner_ == "DEFEAT")) audio_->endOfBattle(banner_ == "VICTORY");
      if (mode_ == Mode::Game && (banner_ == "VICTORY" || banner_ == "DEFEAT")) missionOver(banner_ == "VICTORY");
      break;
    }
    default:
      break;
  }
}

// Damaged buildings smoke and burn.
void GameScene::drawStructureEffects(float dt) {
  if (dt <= 0) return;
  Registry& reg = sim_->registry();
  auto& structs = reg.pool<CStructure>();
  for (uint32_t e : structs.entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    const UnitTypeDef& d = types_.get(u.type);
    if (d.category == UnitCategory::Fortification) continue;
    const float hp = static_cast<float>(u.health) / std::max(1, d.maxHealth);
    if (hp > 0.6f) continue;
    const CPosition& p = reg.pool<CPosition>().get(e);
    if (!watching(p.x, p.y)) continue;
    fx_.emitDamage({p.x, p.y}, d.footW, 1.0f - hp, dt);
  }
}

void GameScene::onUpdate(float dt, float alpha) {
  updateDevTools();
  hotReload_.update(content_.fileSystem(), dt, &assets_);
  if (opt_.monkey > 0) monkeyStep();
  if (!opt_.showcase.empty() && mode_ == Mode::Game) showcaseStep(dt);
  alpha_ = alpha;
  time_ += dt;
  cam_.setViewport(app_.pixelWidth(), app_.pixelHeight());

  Vec2f pan;
  const bool* keys = SDL_GetKeyboardState(nullptr);
  const SDL_Keymod mods = SDL_GetModState();
  const bool inMenu = mode_ == Mode::Menu || menus_->wantsInput();
  if (!ImGui::GetIO().WantTextInput && !inMenu) {
    if (settings_.held(InputAction::ScrollLeft, keys, mods)) pan.x -= 1;
    if (settings_.held(InputAction::ScrollRight, keys, mods)) pan.x += 1;
    if (settings_.held(InputAction::ScrollUp, keys, mods)) pan.y -= 1;
    if (settings_.held(InputAction::ScrollDown, keys, mods)) pan.y += 1;
  }
  // Edge scrolling works over the sidebar and panels too (they touch the
  // screen edges), but not while dragging a slider or a window.
  if (!app_.offscreen() && settings_.edgeScroll && !inMenu && SDL_GetMouseFocus() == app_.window() && !mmbDown_ &&
      !ImGui::IsAnyItemActive()) {
    const float w = static_cast<float>(app_.pixelWidth()), h = static_cast<float>(app_.pixelHeight());
    if (mouse_.x <= kEdgeScrollPx) pan.x -= 1;
    if (mouse_.x >= w - 1 - kEdgeScrollPx) pan.x += 1;
    if (mouse_.y <= kEdgeScrollPx) pan.y -= 1;
    if (mouse_.y >= h - 1 - kEdgeScrollPx) pan.y += 1;
  }
  // Screen lock: the cursor stays in the window while playing.
  const bool lock = settings_.mouseLock && !inMenu;
  if (lock != app_.mouseLock()) app_.setMouseLock(lock);
  if (pan.x != 0 || pan.y != 0) cam_.pan(pan * (kScrollSpeed * settings_.scrollSpeed * dt));
  if (!ImGui::GetIO().WantTextInput && !inMenu) cameraKeys(dt);
  if (cam_.is3D()) {
    // The camera rides over the ground: its target follows the height below it.
    const Vec3f t = cam_.target();
    const float ground = std::max<float>(map_.heightAt(t.x * 256.0f, t.y * 256.0f), map_.waterLevel()) / HEIGHT_UNITS_PER_TILE;
    const float k = std::min(1.0f, dt * 4.0f);
    if (std::abs(ground - t.z) > 1e-3f) cam_.setTarget({t.x, t.y, t.z + (ground - t.z) * k});
  }

  ++frame_;
  updateAudio(dt);
  if (mode_ == Mode::Menu) {
    // The menu's battle: drift slowly over the field; start another when it's decided.
    attractTime_ += dt;
    // Ease toward wherever the fighting is (or a base, before it starts).
    const float k = std::min(1.0f, dt * 0.4f);
    if (cam_.is3D()) {
      const Vec2f c = cameraFocus();
      focusCamera({c.x + (attractFocus_.x - c.x) * k, c.y + (attractFocus_.y - c.y) * k});
      cam_.rotate(dt * 0.035f, 0);  // and slowly circle the field
    } else {
      const Vec2f want = groundToScreenWorld(attractFocus_.x, attractFocus_.y);
      const Vec2f c = cam_.center();
      cam_.setCenter({c.x + (want.x - c.x) * k, c.y + (want.y - c.y) * k});
    }
    if (attractTime_ > 600 || sim_->playerState(0).defeated || sim_->playerState(1).defeated) startAttract();
  }
  for (Message& m : messages_) m.age += dt;
  std::erase_if(messages_, [](const Message& m) { return m.age > 14.0f; });
  if (opt_.editorDemo) runEditorDemo();
  if (opt_.liveDemo) runLiveDemo();
  if (opt_.editorSelfTest) runEditorSelfTest();
  if (opt_.economyDemo) runEconomyDemo();
  if (opt_.gallery) runGallery();
  if (opt_.battleDemo) runBattleDemo();
  if (opt_.orgDemo) runOrgDemo();
  if (opt_.worksDemo) runWorksDemo();
  if (opt_.formationDemo) runFormationDemo();
  if (opt_.townDemo) runTownDemo();
  if (opt_.flowSelfTest) runFlowSelfTest();
  if (opt_.modsSelfTest) runModsSelfTest();
  editor_->update(dt);
  sidebar_->update(dt);
  drawStructureEffects(dt * timeScale());
  if (bannerTime_ > 0) bannerTime_ -= dt;
  // Ground the battle reshaped (levelled building plots).
  for (const Map::TileRect& r : map_.takeHeightEdits()) terrain_.markHeightsDirty(r.x0, r.y0, r.x1, r.y1);
  terrain_.update();
  updateRockFaces();
  // Intel instead of fog (9.3): the ground is never darkened - except outside
  // a mission's play area (and the mission's camera, radio and clocks).
  updateMission(dt);
  updateIntel();
  terrain_.updateFog(dt);
  fx_.update(dt * timeScale());
  hovered_ = app_.offscreen() || ImGui::GetIO().WantCaptureMouse ? Entity{} : pickUnit(mouse_, false);
  updateCursor();

  for (Marker& m : markers_) m.age += dt;
  if (attackMoveMode_) {
    pruneSelection();
    if (selection_.empty()) attackMoveMode_ = false;
  }
  std::erase_if(markers_, [](const Marker& m) { return m.age > (m.slot ? 1.2f : 0.6f); });
  for (Floater& f : floaters_) f.age += dt;
  std::erase_if(floaters_, [](const Floater& f) { return f.age > 2.2f; });
  if (statusTime_ > 0) statusTime_ -= dt;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void GameScene::onRender() {
  if (cam_.is3D()) {
    render3D();
    return;
  }
  glClearColor(0.05f, 0.05f, 0.045f, 1.0f);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  renderer_.resetStats();
  pruneSelection();

  terrain_.draw(cam_, time_);
  {
    // Colour grade per biome: worn, slightly faded colours like period film.
    static const float kGrade[5][4] = {
        {1.03f, 1.00f, 0.93f, 0.86f},  // temperate
        {0.97f, 1.00f, 1.05f, 0.82f},  // winter
        {1.05f, 1.00f, 0.90f, 0.90f},  // desert
        {1.00f, 1.03f, 0.95f, 0.94f},  // tropical
        {0.98f, 0.98f, 0.97f, 0.74f},  // urban
    };
    const float* g = kGrade[static_cast<int>(map_.biome) % 5];
    if (settings_.atmosphere) renderer_.setGrade(g[0], g[1], g[2], g[3]);
    else renderer_.setGrade(1, 1, 1, 1);
  }
  renderer_.beginWorld(cam_);

  const RectF view = cam_.visibleWorld(120);
  const bool fog = fogEnabled();
  const BattleFx::FogAt fogAt = [&](int tx, int ty) -> uint8_t { return fog ? intelAt(tx, ty) : 255; };

  drawGroundLayer(view);
  fx_.drawGround(renderer_, view, fogAt);
  for (Entity e : selection_) {
    const UnitTypeDef& d = types_.get(sim_->registry().get<CUnit>(e).type);
    if (d.isStructure()) {
      // Structures: mark the corners of the footprint.
      const CStructure& st = sim_->registry().get<CStructure>(e);
      for (int cy : {st.ty, st.ty + st.h})
        for (int cx : {st.tx, st.tx + st.w}) {
          const Vec2f s = groundToScreenWorld(cx * 256.0f, cy * 256.0f);
          renderer_.rect(std::floor(s.x) - 2, std::floor(s.y) - 1, 5, 3, kSelectGreen);
        }
      continue;
    }
    const Vec2f s = unitScreenWorld(e.index);
    renderer_.sprite(d.radius > 60 ? art_.selectLarge : art_.selectSmall, s.x, s.y, kWhite, kSelectGreen);
  }
  drawUnits(view);
  fx_.drawAir(renderer_, view, fogAt);
  drawOverlays(view, true);
}

// Health bars, order markers, the editor's and sidebar's tools, a formation
// front being dragged, the selection box. In 3D these are flat on the screen.
void GameScene::drawOverlays(const RectF& view, bool markers) {
  // Health bars: the selection, the unit under the mouse, and (debug) every damaged unit.
  Registry& reg = sim_->registry();
  for (Entity e : selection_) drawHealthBar(e.index, true);
  if (hovered_.valid() && reg.alive(hovered_) &&
      std::find(selection_.begin(), selection_.end(), hovered_) == selection_.end())
    drawHealthBar(hovered_.index, false);
  if (allHealthBars_) {
    auto& units = reg.pool<CUnit>();
    for (size_t i = 0; i < units.size(); ++i) {
      const uint32_t e = units.entities()[i];
      const CUnit& u = units.data()[i];
      if (u.health >= types_.get(u.type).maxHealth) continue;
      const CPosition& p = reg.pool<CPosition>().get(e);
      if (u.owner != player_ && !watching(p.x, p.y)) continue;
      if (view.contains(unitScreenWorld(e))) drawHealthBar(e, false);
    }
  }

  for (const Marker& m : markers_) {
    if (!markers) break;
    const float life = m.slot ? 1.2f : 0.6f;
    const uint8_t alpha = static_cast<uint8_t>(255 * std::max(0.0f, 1.0f - m.age / life));
    const Vec2f ms = groundToScreenWorld(m.pos.x, m.pos.y);
    const uint32_t col = (m.color & 0x00FFFFFFu) | (uint32_t(alpha) << 24);
    if (m.slot) renderer_.rect(std::floor(ms.x) - 2, std::floor(ms.y) - 2, 4, 4, col);  // where one unit will stand
    else renderer_.sprite(art_.moveMarker, ms.x, ms.y, kWhite, col);
  }
  editor_->renderWorld(renderer_);
  sidebar_->renderWorld(renderer_);
  // A formation front being dragged out: the line, a mark where every unit
  // will stand, and a tick showing which way the front will face.
  if (rmbDragging_ && !selection_.empty()) {
    const Vec2f a = groundAt(rmbStart_), b = groundAt(mouse_);
    const int n = std::clamp(static_cast<int>(std::hypot(b.x - a.x, b.y - a.y) / 40.0f), 2, 200);
    for (int i = 0; i <= n; ++i) {
      const float t = static_cast<float>(i) / n;
      const Vec2f s = groundToScreenWorld(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
      renderer_.rect(std::floor(s.x) - 1, std::floor(s.y) - 1, 3, 3, kSelectGreen);
    }
    Vec2i mid;
    Angle facing = 0;
    int32_t width = 0;
    if (draggedFront(rmbStart_, mouse_, mid, facing, width)) {
      const int shape = std::max(0, selectionShape());
      const std::vector<Vec2i> slots = sim_->formationPreview(selection_, mid, static_cast<Formation>(shape), facing, width);
      if (slots.size() <= 600)
        for (const Vec2i& p : slots) {
          const Vec2f s = groundToScreenWorld(static_cast<float>(p.x), static_cast<float>(p.y));
          renderer_.rect(std::floor(s.x) - 3, std::floor(s.y) - 3, 6, 6, rgba8(20, 40, 16, 200));
          renderer_.rect(std::floor(s.x) - 2, std::floor(s.y) - 2, 4, 4, rgba8(190, 255, 150));
        }
      const float fx = static_cast<float>(cosQ16(facing)) / 65536.0f, fy = static_cast<float>(sinQ16(facing)) / 65536.0f;
      for (int i = 1; i <= 6; ++i) {  // the way the front faces
        const Vec2f s = groundToScreenWorld(static_cast<float>(mid.x) + fx * 60.0f * i, static_cast<float>(mid.y) + fy * 60.0f * i);
        renderer_.rect(std::floor(s.x) - 1, std::floor(s.y) - 1, 3, 3, rgba8(236, 200, 96));
      }
    }
  }
  renderer_.flush();

  if (dragging_) {
    renderer_.beginScreen(app_.pixelWidth(), app_.pixelHeight());
    const RectF r = RectF::fromPoints(dragStart_, mouse_);
    renderer_.rect(r.x0, r.y0, r.width(), r.height(), rgba8(120, 236, 96, 28));
    renderer_.rectOutline(std::floor(r.x0), std::floor(r.y0), std::floor(r.width()), std::floor(r.height()), 1,
                          kSelectGreen);
    renderer_.flush();
  }
}

// Oil stains and trenches: flat on the ground, under everything else.
void GameScene::drawGroundLayer(const RectF& view) {
  const bool fog = fogEnabled();
  // Visible tile range: the corners of the view, mapped to the ground.
  const float vw = static_cast<float>(app_.pixelWidth()), vh = static_cast<float>(app_.pixelHeight());
  int tx0 = map_.width(), ty0 = map_.height(), tx1 = 0, ty1 = 0;
  for (Vec2f c : {Vec2f{0, 0}, Vec2f{vw, 0}, Vec2f{0, vh + 200}, Vec2f{vw, vh + 200}, Vec2f{0, -200}, Vec2f{vw, -200}}) {
    const Vec2f g = groundAt(c);
    tx0 = std::min(tx0, static_cast<int>(g.x / 256) - 2);
    ty0 = std::min(ty0, static_cast<int>(g.y / 256) - 2);
    tx1 = std::max(tx1, static_cast<int>(g.x / 256) + 2);
    ty1 = std::max(ty1, static_cast<int>(g.y / 256) + 2);
  }
  tx0 = std::max(0, tx0);
  ty0 = std::max(0, ty0);
  tx1 = std::min(map_.width() - 1, tx1);
  ty1 = std::min(map_.height() - 1, ty1);
  auto fogTint = [&](int tx, int ty, float alpha) -> uint32_t {
    float k = 1.0f;
    if (fog) {
      const uint8_t f = terrain_.fogAt(tx, ty);
      if (f == 0) return 0;
      k = f < 128 ? 0.48f * f / 128.0f : 0.48f + 0.52f * (f - 128) / 127.0f;
    }
    const uint8_t c = static_cast<uint8_t>(255 * k);
    return rgba8(c, c, c, static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255));
  };
  for (int ty = ty0; ty <= ty1; ++ty)  // river ice, at the water's surface (9.11)
    for (int tx = tx0; tx <= tx1; ++tx) {
      if (!map_.iced(tx, ty)) continue;
      const Vec2f s = project(tx * 256.0f + 128, ty * 256.0f + 128, map_.waterLevel());
      if (!view.contains(s)) continue;
      renderer_.sprite(art_.ice[0], s.x, s.y, kWhite, kWhite);
      if (hash2(tx, ty, 11) % 4 == 0) renderer_.sprite(art_.ice[1 + hash2(tx, ty, 13) % 3], s.x, s.y, kWhite, kWhite);
    }
  for (int ty = ty0; ty <= ty1; ++ty)
    for (int tx = tx0; tx <= tx1; ++tx) {
      const uint8_t oil = map_.oil(tx, ty);
      if (!oil) continue;
      const Vec2f s = groundToScreenWorld(tx * 256.0f + 128, ty * 256.0f + 128);
      if (!view.contains(s)) continue;
      const uint32_t tint = fogTint(tx, ty, 0.35f + oil / 255.0f * 0.65f);
      if (tint) renderer_.sprite(art_.oil[hash2(tx, ty, 5) & 3], s.x, s.y, kWhite, tint);
    }
  // Trenches join up with their neighbours.
  Registry& reg = sim_->registry();
  for (uint32_t e : reg.pool<CStructure>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.type >= trenchTypes_.size() || !trenchTypes_[u.type]) continue;
    const CStructure& st = reg.pool<CStructure>().get(e);
    if (st.tx < tx0 || st.tx > tx1 || st.ty < ty0 || st.ty > ty1) continue;
    if (fog && !structureKnown(e)) continue;
    auto isTrench = [&](int x, int y) {
      const Entity o = sim_->structureAt(x, y);
      return o.valid() && trenchTypes_[reg.get<CUnit>(o).type];
    };
    const int mask = (isTrench(st.tx + 1, st.ty) ? 1 : 0) | (isTrench(st.tx, st.ty + 1) ? 2 : 0) |
                     (isTrench(st.tx - 1, st.ty) ? 4 : 0) | (isTrench(st.tx, st.ty - 1) ? 8 : 0);
    const float cx = st.tx * 256.0f + 128, cy = st.ty * 256.0f + 128;
    const Vec2f s = groundToScreenWorld(cx, cy);
    const uint32_t tint = fogTint(st.tx, st.ty, 1.0f);
    // Draped over the ground, so trenches follow slopes instead of floating.
    const float h0 = static_cast<float>(map_.heightAt(cx, cy));
    if (tint)
      renderer_.spriteDraped(art_.trench[static_cast<size_t>(mask)], s.x, s.y, tint, 8, [&](float dx, float dy) {
        // Screen offset back to ground (on the flat), then the height there.
        const float lx = cx + (dx / 32.0f + dy / 16.0f) * 128.0f, ly = cy + (dy / 16.0f - dx / 32.0f) * 128.0f;
        return (static_cast<float>(map_.heightAt(lx, ly)) - h0) * kPxPerUnit;
      });
  }
}

// Units, wrecks and props, sorted back to front. Vehicles with turrets are two
// sprites: the hull, then the turret on top at its own pivot and facing.
void GameScene::drawUnits(const RectF& view) {
  Registry& reg = sim_->registry();
  refreshBridges();
  const bool fog = fogEnabled();
  drawList_.clear();
  uint32_t order = 0;
  const bool threeD = cam_.is3D();
  for (const Prop& p : map_.props()) {
    if (!threeD) {
      const Vec2f flat = iso::worldToScreen(static_cast<float>(p.x), static_cast<float>(p.y));
      if (flat.x < view.x0 || flat.x > view.x1 || flat.y < view.y0 || flat.y > view.y1 + 400) continue;
    }
    const float ph = map_.heightAt(static_cast<float>(p.x), static_cast<float>(p.y));
    const Vec2f s = project(static_cast<float>(p.x), static_cast<float>(p.y), ph);
    if (!view.contains(s)) continue;
    uint32_t tint = kWhite;
    if (fog) {
      const uint8_t f = terrain_.fogAt(p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT);
      if (f == 0) continue;
      const float k = f < 128 ? 0.48f * f / 128.0f : 0.48f + 0.52f * (f - 128) / 127.0f;
      const uint8_t c = static_cast<uint8_t>(255 * k);
      tint = rgba8(c, c, c);
    }
    const auto& variants = art_.props[static_cast<size_t>(p.kind)];
    drawList_.push_back({iso::depthKey(p.x, p.y), order++, variants[p.variant % variants.size()], s.x, s.y, kWhite, tint,
                         static_cast<float>(p.x), static_cast<float>(p.y), ph});
    drawList_.back().blend = 0.06f;  // grass round the trunk or stone
  }
  // Logistics points: a fenced compound with crates and fuel drums, a gate in
  // front; the office of whoever takes it goes up inside.
  if (auto fence = art_.joined.find("fence"); fence != art_.joined.end()) {
    constexpr int R = 3;  // compound: 7 x 7 tiles, fence on the border
    for (const Vec2i& sp : map_.supplyPoints) {
      const Vec2f c = groundToScreenWorld(sp.x * 256.0f + 128, sp.y * 256.0f + 128);
      if (c.x < view.x0 - 300 || c.x > view.x1 + 300 || c.y < view.y0 - 200 || c.y > view.y1 + 300) continue;
      auto onFence = [&](int x, int y) {
        const int dx = x - sp.x, dy = y - sp.y;
        if (std::abs(dx) > R || std::abs(dy) > R || (std::abs(dx) != R && std::abs(dy) != R)) return false;
        return !(dy == R && (dx == 0 || dx == 1));  // the gate
      };
      auto put = [&](int frame, float lx, float ly) {
        const int tx = static_cast<int>(lx) >> LEPTON_SHIFT, ty = static_cast<int>(ly) >> LEPTON_SHIFT;
        uint32_t tint = kWhite;
        if (fog) {
          const uint8_t f = terrain_.fogAt(tx, ty);
          if (f == 0) return;
          const float k = f < 128 ? 0.48f * f / 128.0f : 0.48f + 0.52f * (f - 128) / 127.0f;
          const uint8_t g = static_cast<uint8_t>(255 * k);
          tint = rgba8(g, g, g);
        }
        const float gh = map_.heightAt(lx, ly);
        const Vec2f s = project(lx, ly, gh);
        drawList_.push_back({iso::depthKey(static_cast<int32_t>(lx), static_cast<int32_t>(ly)), order++, frame, s.x, s.y,
                             kWhite, tint, lx, ly, gh});
      };
      for (int y = sp.y - R; y <= sp.y + R; ++y)
        for (int x = sp.x - R; x <= sp.x + R; ++x) {
          if (!onFence(x, y) || !map_.inBounds(x, y) || map_.info(x, y).waterDepth > -8) continue;
          if (sim_->structureAt(x, y).valid()) continue;  // a building stands there
          const int mask = (onFence(x + 1, y) ? 1 : 0) | (onFence(x, y + 1) ? 2 : 0) | (onFence(x - 1, y) ? 4 : 0) | (onFence(x, y - 1) ? 8 : 0);
          put(fence->second[static_cast<size_t>(mask)], x * 256.0f + 128, y * 256.0f + 128);
        }
      // Stores in the corners.
      const auto& crates = art_.props[static_cast<size_t>(PropKind::Crates)];
      if (!crates.empty()) {
        const int corners[4][2] = {{-2, -2}, {2, -2}, {-2, 2}, {2, 2}};
        for (int k = 0; k < 4; ++k) {
          const int x = sp.x + corners[k][0], y = sp.y + corners[k][1];
          if (sim_->structureAt(x, y).valid() || !map_.inBounds(x, y)) continue;
          put(crates[static_cast<size_t>(k) % crates.size()], x * 256.0f + 128 + (k % 2 ? 40 : -40), y * 256.0f + 128);
        }
      }
    }
  }
  for (const BattleFx::Wreck& w : fx_.wrecks()) {
    if (w.bodyFrame < 0) continue;  // a burning ruin: only fire and smoke
    const float wh = map_.heightAt(static_cast<float>(w.lx), static_cast<float>(w.ly));
    const Vec2f s = project(static_cast<float>(w.lx), static_cast<float>(w.ly), wh);
    if (!view.contains(s)) continue;
    float k = 1.0f;
    if (fog) {
      const uint8_t f = terrain_.fogAt(w.lx >> LEPTON_SHIFT, w.ly >> LEPTON_SHIFT);
      if (f == 0) continue;
      k = f < 128 ? 0.48f * f / 128.0f : 0.48f + 0.52f * (f - 128) / 127.0f;
    }
    // Burnt-out hulks: blackened, still faintly showing their markings.
    const uint8_t c = static_cast<uint8_t>(std::clamp(92.0f * k, 0.0f, 255.0f));
    const uint32_t tint = rgba8(c, static_cast<uint8_t>(c * 0.92f), static_cast<uint8_t>(c * 0.85f));
    const int64_t depth = iso::depthKey(w.lx, w.ly);
    drawList_.push_back({depth, order++, w.bodyFrame, s.x, s.y, w.team, tint, static_cast<float>(w.lx),
                         static_cast<float>(w.ly), wh});
    drawList_.back().blend = 0.07f;  // a wreck settles into the ground
    if (w.turretFrame >= 0) {
      const float tx = w.lx + w.turretShift.x, ty = w.ly + w.turretShift.y;
      const Vec2f t = groundToScreenWorld(tx, ty);
      // The turret sits on the hull: in 3D, at the hull's ground height.
      drawList_.push_back({depth, order++, w.turretFrame, t.x, t.y, w.team, tint, tx, ty, wh});
    }
  }
  auto& units = reg.pool<CUnit>();
  auto& turrets = reg.pool<CTurret>();
  auto& combats = reg.pool<CCombat>();
  for (size_t i = 0; i < units.size(); ++i) {
    const uint32_t e = units.entities()[i];
    const CUnit& u = units.data()[i];
    if (reg.pool<CGarrisoned>().has(e) && !sim_->inTrench(reg.handle(e))) continue;  // inside a building (men in trenches show)
    const CPosition& p = reg.pool<CPosition>().get(e);
    const UnitTypeDef& def = types_.get(u.type);
    uint32_t tint = kWhite;
    if (def.isStructure()) {
      if (u.type < trenchTypes_.size() && trenchTypes_[u.type]) continue;  // drawn on the ground layer
      // Enemy structures show once our side has seen them (dimmed while out of sight).
      if (fog && !sim_->allied(u.owner, player_) && u.owner < MAX_PLAYERS) {
        if (!structureKnown(e)) continue;
        if (!sim_->vision().visible(player_, p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT)) tint = rgba8(170, 170, 170);
      }
      // Fade in as it goes up.
      const uint32_t age = sim_->tickCount() - reg.pool<CStructure>().get(e).builtTick;
      if (age < 12) tint = (tint & 0x00FFFFFFu) | (static_cast<uint32_t>(60 + age * 16) << 24);
    } else if (fog && !sim_->allied(u.owner, player_) &&
               !sim_->vision().visible(player_, p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT)) {
      continue;
    }
    const CPrevPosition& prev = reg.pool<CPrevPosition>().get(e);
    float ulx = prev.x + (p.x - prev.x) * alpha_, uly = prev.y + (p.y - prev.y) * alpha_;
    float uh = surfaceHeight(ulx, uly);  // up on the deck when crossing a bridge
    const bool bridge = u.type < bridgeTypes_.size() && bridgeTypes_[u.type];
    if (bridge) {
      // Laid level across the river, its piers reaching down into the water;
      // drawn behind whatever crosses it.
      const size_t ti = static_cast<size_t>(p.y >> LEPTON_SHIFT) * map_.width() + (p.x >> LEPTON_SHIFT);
      const float level = ti < bridgeLevel_.size() && bridgeLevel_[ti] > 0 ? bridgeLevel_[ti] : static_cast<float>(map_.waterLevel());
      ulx = static_cast<float>(p.x);
      uly = static_cast<float>(p.y);
      uh = level - kBridgePierDepth * kUnitsPerFineVoxel;
    } else if (def.isStructure()) {
      uh = map_.heightAt(ulx, uly);
    }
    const Vec2f s = project(ulx, uly, uh);
    if (!view.contains(s)) continue;
    const CFacing& f = reg.pool<CFacing>().get(e);
    const CAnim& a = reg.pool<CAnim>().get(e);
    const UnitArt& art = unitArt_[u.type];
    // Structures sort by their front corner, so units behind them are hidden by them.
    const int64_t depth = bridge              ? iso::depthKey(p.x - 128, p.y - 128) - 1
                          : def.isStructure() ? iso::depthKey(p.x + def.footW * 96, p.y + def.footH * 96) - 1
                                              : iso::depthKey(p.x, p.y);
    const int facing = facingOf(f.angle);
    int anim = a.moving ? a.frame : 0;
    if (art.walkFrames > 0) {
      // Sprite art: time-driven walk cycle, each man out of step with the next.
      const int phase = static_cast<int>((e * 2654435761u) >> 28);
      anim = 0;
      if (a.moving) {
        anim = art.walkFirst + (static_cast<int>(time_ * 1000.0f / art.walkMs) + phase) % art.walkFrames;
      } else if (art.shootFrames > 0 && !def.weapons.empty()) {
        if (const CCombat* cb = combats.tryGet(e); cb && cb->target.valid()) {
          // Aim at the target; play the shot just after the weapon fires.
          const int cooldown = weapons_.get(def.weapons[0]).cooldown;
          const float since = (static_cast<float>(cooldown - cb->cooldown[0]) + alpha_) * (1000.0f / TICKS_PER_SECOND);
          const int step = static_cast<int>(since / art.shootMs);
          anim = art.shootFirst + (step >= 0 && step < art.shootFrames ? step : 0);
        }
      }
    }
    int bodyFrame = art.body.frame(facing, anim);
    if (const CGarrisoned* gar = reg.pool<CGarrisoned>().tryGet(e); gar && u.type < postArt_.size()) {
      // Manning a trench or a sandbag wall: drawn dug in, up to fire when
      // there is something to shoot at.
      const bool bags = reg.alive(gar->building) && types_.get(reg.get<CUnit>(gar->building).type).targetable;
      if (postArt_[u.type] & (bags ? 2 : 1)) {
        const CCombat* cb = combats.tryGet(e);
        const bool firing = cb && cb->target.valid();
        bodyFrame = (bags ? bagArt_ : dugArt_)[u.type].body.frame(facing, firing ? 1 : 0);
      }
    }
    if (def.isStructure()) {
      // Sandbag walls and wire join up with neighbours of the same kind.
      if (auto j = art_.joined.find(def.model); j != art_.joined.end()) {
        const CStructure& st = reg.pool<CStructure>().get(e);
        // (Row houses of every style join each other; the rest only their own kind.)
        const bool row = def.model.rfind("rowhouse", 0) == 0;
        auto same = [&](int x, int y) {
          const Entity o = sim_->structureAt(x, y);
          if (!o.valid()) return false;
          const std::string& om = types_.get(reg.get<CUnit>(o).type).model;
          return row ? om.rfind("rowhouse", 0) == 0 : om == def.model;
        };
        const int mask = (same(st.tx + 1, st.ty) ? 1 : 0) | (same(st.tx, st.ty + 1) ? 2 : 0) | (same(st.tx - 1, st.ty) ? 4 : 0) |
                         (same(st.tx, st.ty - 1) ? 8 : 0);
        bodyFrame = j->second[static_cast<size_t>(mask)];
      }
    }
    DrawItem body{depth, order++, bodyFrame, s.x, s.y, teamColor(u.owner), tint, ulx, uly, uh};
    if (def.isStructure() && !bridge) body.blend = def.footW * def.footH >= 2 ? 0.14f : 0.07f;  // set into the ground
    const float hullYaw = static_cast<float>(f.angle) * (6.2831853f / ANGLE_FULL);
    if (threeD && !def.isStructure() && art.body.facings > 1) {
      // Units turn smoothly; vehicles lean with the slope they stand on.
      body.yaw = hullYaw;
      body.turned = true;
      if (!def.infantry() && !reg.pool<CGarrisoned>().has(e)) lean(ulx, uly, hullYaw, std::max(40.0f, def.radius * 0.9f), body.pitch, body.roll);
    }
    drawList_.push_back(body);
    if (art.hasTurret) {
      const CTurret* t = turrets.tryGet(e);
      const int tf = t ? facingOf(t->angle) : facing;
      // The pivot turns with the hull's drawn facing, so it stays on the hull.
      const float ang = threeD && !def.isStructure() ? hullYaw : static_cast<float>(angleOfFacing(facing)) * (6.2831853f / ANGLE_FULL);
      const float lx = ulx + std::cos(ang) * art.turretOffset;
      const float ly = uly + std::sin(ang) * art.turretOffset;
      const Vec2f ts = groundToScreenWorld(lx, ly);
      DrawItem turret{depth, order++, art.turret.frame(tf, 0), std::round(ts.x), std::round(ts.y), teamColor(u.owner), tint,
                      lx, ly, uh};
      if (threeD) {
        turret.yaw = t ? static_cast<float>(t->angle) * (6.2831853f / ANGLE_FULL) : hullYaw;
        turret.turned = true;
        // On a leaning hull the turret rides up or down with the deck.
        turret.pitch = body.pitch;
        turret.roll = body.roll;
        turret.h += std::sin(body.pitch) * art.turretOffset / 256.0f * HEIGHT_UNITS_PER_TILE;
      }
      drawList_.push_back(turret);
    }
  }
  // Occupied buildings are fortified: sandbags go up along the front, then a
  // side, then all round, as more men move in.
  for (uint32_t b : reg.pool<CGarrison>().entities()) {
    const Entity bh = reg.handle(b);
    const int level = sim_->garrisonLevel(bh);
    if (level == 0 || !reg.has<CStructure>(bh) || sim_->isTrench(bh)) continue;
    const CGarrison& g = reg.get<CGarrison>(bh);
    if (g.occupants.empty() || !reg.alive(g.occupants.front())) continue;
    const uint8_t owner = reg.get<CUnit>(g.occupants.front()).owner;
    if (fog && !sim_->allied(owner, player_) && !sim_->vision().visible(player_, reg.get<CStructure>(bh).tx, reg.get<CStructure>(bh).ty)) continue;
    const int bagType = types_.find(players_[owner % MAX_PLAYERS].faction + "_sandbags");
    if (bagType < 0) continue;
    const UnitArt& bag = unitArt_[static_cast<size_t>(bagType)];
    const CStructure& st = reg.get<CStructure>(bh);
    // Bag walls along the sides: rows along X on the front and back, along Y on the sides.
    std::vector<std::pair<Vec2i, int>> spots;
    for (int x = st.tx; x < st.tx + st.w; ++x) spots.push_back({{x, st.ty + st.h}, 5});              // front
    if (level >= 2) for (int y = st.ty; y < st.ty + st.h; ++y) spots.push_back({{st.tx + st.w, y}, 10});  // a side
    if (level >= 3) {
      for (int y = st.ty; y < st.ty + st.h; ++y) spots.push_back({{st.tx - 1, y}, 10});
      for (int x = st.tx; x < st.tx + st.w; ++x) spots.push_back({{x, st.ty - 1}, 5});
    }
    const auto wall = art_.joined.find("sandbags");
    for (const auto& [t, mask] : spots) {
      if (!map_.inBounds(t.x, t.y) || sim_->structureAt(t.x, t.y).valid()) continue;
      const float lx = t.x * 256.0f + 128, ly = t.y * 256.0f + 128;
      const float gh = map_.heightAt(lx, ly);
      const Vec2f s = project(lx, ly, gh);
      if (!view.contains(s)) continue;
      const int frame = wall != art_.joined.end() ? wall->second[static_cast<size_t>(mask)] : bag.body.frame(0, 0);
      drawList_.push_back({iso::depthKey(static_cast<int32_t>(lx), static_cast<int32_t>(ly)), order++, frame, s.x, s.y,
                           teamColor(owner), kWhite, lx, ly, gh});
    }
  }
  if (threeD) {
    queueItems3D();
    return;
  }
  std::sort(drawList_.begin(), drawList_.end(), [](const DrawItem& a, const DrawItem& b) {
    return a.depth != b.depth ? a.depth < b.depth : a.order < b.order;
  });
  for (const DrawItem& d : drawList_) renderer_.sprite(d.frame, d.x, d.y, d.team, d.tint);
}

void GameScene::drawHealthBar(uint32_t e, bool selected) {
  Registry& reg = sim_->registry();
  const CUnit& u = reg.pool<CUnit>().get(e);
  const UnitTypeDef& def = types_.get(u.type);
  const Vec2f s = unitScreenWorld(e);
  const bool big = def.radius > 60;
  float w = big ? 24.0f : 10.0f;
  float y = std::floor(s.y) - (big ? 24.0f : 17.0f);
  if (def.isStructure()) {
    w = std::max(12.0f, (def.footW + def.footH) * 10.0f);
    y = std::floor(s.y) - (def.footW + def.footH) * 8.0f - 22.0f;
  }
  if (cam_.is3D()) {
    // Above the model's top, sized with the distance (within reason).
    const CPosition& p = reg.pool<CPosition>().get(e);
    const float lx = static_cast<float>(p.x), ly = static_cast<float>(p.y);
    const float h = def.isStructure() ? map_.heightAt(lx, ly) : surfaceHeight(lx, ly);
    const float k = std::clamp(cam_.scaleAt(worldPoint(lx, ly, h)), 0.7f, 1.6f);
    float tall = def.isStructure() ? 0.35f + (def.footW + def.footH) * 0.22f : big ? 0.55f : 0.42f;
    if (const int mesh = meshOf(unitArt_[u.type].body.first); mesh >= 0) tall = meshes_.meshHeight(mesh) + 0.08f;
    const Vec2f top = project(lx, ly, h + tall * HEIGHT_UNITS_PER_TILE);
    w = std::floor(w * k);
    y = std::floor(top.y) - 6.0f;
  }
  const float x = std::floor(s.x - w / 2);
  const float frac = std::clamp(static_cast<float>(u.health) / def.maxHealth, 0.0f, 1.0f);
  const uint32_t col = frac > 0.6f ? kSelectGreen : frac > 0.3f ? rgba8(230, 200, 60) : kAttackRed;
  renderer_.rect(x - 1, y - 1, w + 2, 4, rgba8(10, 10, 8, selected ? 255 : 200));
  renderer_.rect(x, y, std::max(1.0f, std::floor(w * frac)), 2, col);
}

void GameScene::updateCursor() {
  if (!cursorCross_) return;
  bool cross = attackMoveMode_;
  if (!cross && !selection_.empty() && hovered_.valid() && sim_->registry().alive(hovered_))
    cross = sim_->registry().get<CUnit>(hovered_).owner != player_;
  if (cross != crossShown_) {
    SDL_SetCursor(cross ? cursorCross_ : cursorArrow_);
    crossShown_ = cross;
  }
}

// ---------------------------------------------------------------------------
// Interface (Dear ImGui for now; the pixel-art HUD arrives with Milestone 5)
// ---------------------------------------------------------------------------
void GameScene::onImGui() {
  if (mode_ == Mode::Menu) {
    menus_->draw();
    encyclopedia_->draw();
    rulesEditor_->draw();
    drawDevTools();
    return;
  }
  showCards_ = settings_.showCards;
  showChips_ = settings_.showChips;
  if (settings_.atmosphere) {
    // A soft dark vignette round the battlefield.
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const ImGuiIO& io = ImGui::GetIO();
    const float w = io.DisplaySize.x - (sim_->cinematic(player_) ? 0.0f : Sidebar::kWidth), h = io.DisplaySize.y, e = std::min(w, h) * 0.22f;
    const ImU32 dark = IM_COL32(8, 7, 4, 120), clear = IM_COL32(8, 7, 4, 0);
    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, e), dark, dark, clear, clear);
    bg->AddRectFilledMultiColor(ImVec2(0, h - e), ImVec2(w, h), clear, clear, dark, dark);
    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(e, h), dark, clear, clear, dark);
    bg->AddRectFilledMultiColor(ImVec2(w - e, 0), ImVec2(w, h), clear, dark, dark, clear);
  }
  drawWorldOverlays();
  drawHud();
  // A tutorial's hint and the interface element it points at.
  ui::setHighlight(mode_ != Mode::Menu ? sim_->highlight(player_) : std::string());
  if (mode_ == Mode::Game && !sim_->cinematic(player_)) sidebar_->draw();
  if (mode_ == Mode::Game) editor_->imgui();
  drawObjectives();
  drawMission();
  drawDebrief();
  if (mode_ != Mode::Menu && !sim_->hint(player_).empty()) {
    const ImGuiIO& io = ImGui::GetIO();
    const float w = std::min(520.0f, io.DisplaySize.x - Sidebar::kWidth - 40);
    ui::hintPanel(sim_->hint(player_), (io.DisplaySize.x - Sidebar::kWidth - w) * 0.5f, 54, w);
  }
  encyclopedia_->draw();
  rulesEditor_->draw();
  menus_->draw();
  if (bannerTime_ > 0 && !banner_.empty()) {
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float scale = banner_ == "VICTORY" || banner_ == "DEFEAT" ? 3.0f : 1.8f;
    const ImVec2 ts = ImGui::CalcTextSize(banner_.c_str());
    const ImVec2 p((io.DisplaySize.x - Sidebar::kWidth - ts.x * scale) * 0.5f, io.DisplaySize.y * 0.28f);
    const uint8_t a = static_cast<uint8_t>(std::min(1.0f, bannerTime_) * 255);
    dl->AddRectFilled(ImVec2(p.x - 20, p.y - 10), ImVec2(p.x + ts.x * scale + 20, p.y + ts.y * scale + 10), IM_COL32(0, 0, 0, a / 2));
    dl->AddText(nullptr, ImGui::GetFontSize() * scale, p, IM_COL32(236, 200, 96, a), banner_.c_str());
  }
  if (showHelp_ && !missionInputBlocked()) drawHelp();
  if (showDebug_) drawDebug();
  if (formationPicker_) drawFormationPicker();
  drawDevTools();
}

// Ctrl+right-click: a small menu at the cursor to move or attack-move there
// in a chosen formation.
void GameScene::drawFormationPicker() {
  const float ps = app_.pixelScale();
  ImGui::SetNextWindowPos(ImVec2(pickerScreen_.x / ps, pickerScreen_.y / ps), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowBgAlpha(0.92f);
  ImGui::Begin("##formations", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
  static const char* names[] = {"Box", "Line", "Column", "Wedge", "Loose"};
  int pick = -1;
  bool attack = false;
  ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.36f, 1), "Move");
  ImGui::SameLine(110);
  ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Attack-move");
  for (int i = 0; i < 5; ++i) {
    ImGui::PushID(i);
    if (ImGui::Button(names[i], ImVec2(100, 0))) pick = i;
    ImGui::SameLine(110);
    if (ImGui::Button((std::string(names[i]) + "##a").c_str(), ImVec2(100, 0))) {
      pick = i;
      attack = true;
    }
    ImGui::PopID();
  }
  if (ImGui::Button("Cancel", ImVec2(210, 0))) formationPicker_ = false;
  const bool clickedAway = (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
                           !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  ImGui::End();
  if (pick >= 0) {
    orderMove(pickerTarget_, attack ? CommandType::AttackMove : CommandType::Move, pick);
    formationPicker_ = false;
  } else if (clickedAway) {
    formationPicker_ = false;
  }
}

// Mission objectives and scenario messages, under the HUD box.
// After a campaign mission: what next.
void GameScene::drawDebrief() {
  if (mode_ != Mode::Game || missionResult_ <= 0 || missionResult_ > 2 || bannerTime_ > 0 || menus_->wantsInput()) return;
  const CampaignInfo* c = currentCampaign();
  if (!c || missionIndex_ < 0) return;
  const bool won = missionResult_ == 1;
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - Sidebar::kWidth) * 0.5f, io.DisplaySize.y * 0.4f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(440, 0));
  ImGui::SetNextWindowBgAlpha(0.92f);
  ImGui::Begin("##debrief", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
  ImGui::TextColored(won ? ImVec4(0.93f, 0.78f, 0.38f, 1) : ImVec4(1, 0.5f, 0.4f, 1), "%s",
                     won ? "MISSION ACCOMPLISHED" : "MISSION FAILED");
  ImGui::TextUnformatted(c->missions[static_cast<size_t>(missionIndex_)].title.c_str());
  ImGui::Separator();
  const bool last = missionIndex_ + 1 >= static_cast<int>(c->missions.size());
  if (won && last) ImGui::TextWrapped("That was the last mission of %s.", c->title.c_str());
  const std::string id = c->id;
  const int next = missionIndex_ + 1;
  if (won && !last) {
    if (ImGui::Button(("Next: " + c->missions[static_cast<size_t>(next)].title).c_str(), ImVec2(-1, 30))) {
      toMainMenu();
      menus_->openBriefing(id, next);
    }
  }
  if (!won && ImGui::Button("Try again", ImVec2(-1, 30))) {
    restart();
    ImGui::End();
    return;
  }
  if (ImGui::Button(c->tutorial ? "Back to the tutorial" : "Back to the campaign", ImVec2(-1, 26))) {
    toMainMenu();
    menus_->openCampaign(id);
  }
  if (ImGui::Button("Stay on the battlefield", ImVec2(-1, 26))) missionResult_ = 3;
  ImGui::End();
}

void GameScene::drawObjectives() {
  std::vector<const Objective*> mine;
  for (const Objective& o : sim_->objectives())
    if ((o.player == ANY_PLAYER || sim_->allied(o.player, player_)) && o.state != 3) mine.push_back(&o);
  if (mine.empty() && messages_.empty()) return;
  ImGui::SetNextWindowPos(ImVec2(editor_->active() ? Editor::kPanelWidth + 16 : 8, hudBottom_), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.75f);
  ImGui::Begin("##objectives", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
  if (!mine.empty()) {
    ImGui::TextColored(ImVec4(0.93f, 0.78f, 0.38f, 1), "Objectives");
    for (const Objective* o : mine) {
      const ImVec4 c = o->state == 1 ? ImVec4(0.55f, 0.9f, 0.5f, 1) : o->state == 2 ? ImVec4(1, 0.45f, 0.35f, 1) : ImVec4(0.9f, 0.88f, 0.8f, 1);
      ImGui::TextColored(c, "%s %s%s", o->state == 1 ? "[x]" : o->state == 2 ? "[!]" : "[ ]", o->text.c_str(), o->optional ? "  (optional)" : "");
    }
  }
  for (const Message& m : messages_) {
    const float a = std::clamp(14.0f - m.age, 0.0f, 1.0f);
    ImGui::PushTextWrapPos(420);
    ImGui::TextColored(ImVec4(0.95f, 0.92f, 0.8f, a), "%s", m.text.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::End();
}

void GameScene::drawHud() {
  const bool editing = editor_->active();
  const bool transient = paused_ || speed_ != 1.0f || attackMoveMode_ || statusTime_ > 0;
  if (!settings_.showInfoPanel && !transient) {
    hudBottom_ = 8;
    return;
  }
  ImGui::SetNextWindowPos(ImVec2(editing ? Editor::kPanelWidth + 16 : 8, 8));
  ImGui::SetNextWindowBgAlpha(0.82f);
  ImGui::Begin("##hud", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
  if (settings_.showInfoPanel) {
    ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.36f, 1), "LEGION CLASSIC");
    ImGui::SameLine();
    ImGui::TextDisabled("Pioneer Engine 2.0  |  build 0.9.0");
    ImGui::Text("Map: %s (%dx%d)", map_.name.c_str(), map_.width(), map_.height());
    ImGui::Text("Commanding:");
    ImGui::SameLine();
    ImGui::TextColored(toImVec(teamColors_[player_]), "%s", players_[player_].name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(Tab)");
    ImGui::Text("Selected: %zu   Units: %zu", selection_.size(), sim_->unitCount());
  }
  if (paused_) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "PAUSED (Space)");
  else if (speed_ != 1.0f) ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Speed x%.2g", speed_);
  if (attackMoveMode_) ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "ATTACK-MOVE: left-click a destination (Esc cancels)");
  if (statusTime_ > 0) ImGui::TextColored(ImVec4(0.6f, 0.95f, 0.6f, 1), "%s", status_.c_str());
  if (settings_.showInfoPanel)
    ImGui::TextDisabled(editing ? "F2 close editor   F1 help   I hide" : "F2 editor   F1 help   F3 debug   I hide");
  hudBottom_ = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y + 6;
  ImGui::End();
}

void GameScene::drawHelp() {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - Sidebar::kWidth - 8, 8), ImGuiCond_Always, ImVec2(1, 0));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin("Controls (F1)", &showHelp_,
               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
  auto row = [](const char* key, const char* what) {
    ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.36f, 1), "%-16s", key);
    ImGui::SameLine();
    ImGui::TextUnformatted(what);
  };
  if (editor_->active()) {
    row("Left click/drag", "Use the current tool");
    row("Ctrl+Z / Ctrl+Y", "Undo / redo (terrain and units)");
    row("[ / ]", "Brush size");
    row(", / .", "Turn the units being placed / selected");
    row("Delete", "Delete selected units (undoable)");
    row("Ctrl+D", "Duplicate selected units");
    row("Esc", "Placing units: back to Select");
    row("Ctrl+S", "Save scenario");
    row("Right click", "Orders the selection (each side's own units)");
    row("F2", "Close the editor");
    ImGui::Separator();
  }
  row("Left click", "Select a unit (Shift: add/remove)");
  row("Left drag", "Box-select your units");
  row("Double-click", "Select all of that type on screen");
  row("Right click", "Move; on an enemy: attack it");
  row("  engineers", "Right-click a damaged friend: repair it");
  row("Right drag", "Lay out the front: where, how wide, facing which way");
  row("Z", "Next formation (or the buttons under Orders)");
  row("A, then click", "Attack-move (fight on the way)");
  row("Ctrl+right click", "Menu: move or attack-move in a chosen formation");
  row("Ctrl+1..9 / 1..9", "Set / recall control group");
  row("Ctrl+A", "Select all your units");
  row("S", "Stop");
  row("Tab", "Command (and see as) the other side");
  row("Arrows / edges", "Scroll the map");
  if (cam_.is3D()) {
    row("Middle drag", "Turn and tilt the view");
    row("Shift+middle drag", "Pan the map");
    row("Q / R", "Turn the view");
    row("Page Up / Down", "Tilt the view");
    row("Backspace", "Reset the view");
    row("Mouse wheel", "Zoom");
  } else {
    row("Middle drag", "Pan the map");
    row("Mouse wheel", "Zoom 0.5x / 1x / 2x");
  }
  row("Home", "Centre on selection");
  row("Space", "Pause");
  row("+ / -", "Game speed");
  row("Delete", "Remove selected units");
  row("F2", "Map editor");
  row("Esc / F10", "Menu: save, load, replays");
  row("F11 / Alt+Enter", "Fullscreen");
  row("F12", "Screenshot");
  ImGui::End();
}

void GameScene::drawDebug() {
  ImGui::SetNextWindowPos(ImVec2(editor_->active() ? Editor::kPanelWidth + 16 : 8, 170), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.88f);
  ImGui::Begin("Debug & spawn tools (F3)", &showDebug_, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::Text("%.0f fps  (%.2f ms)%s", app_.fps(), app_.frameMs(), app_.simBehind() ? "  SIM SLOWED" : "");
  ImGui::Text("Tick %u   units %zu   sprites %zu   draw calls %zu", sim_->tickCount(), sim_->unitCount(),
              renderer_.spritesThisFrame(), renderer_.drawCallsThisFrame());
  ImGui::Text("Map %dx%d   terrain chunks %zu/%zu   zoom %.1fx", map_.width(), map_.height(),
              terrain_.chunksDrawnLastFrame(), terrain_.chunkCount(), cam_.zoom());
  if (cam_.is3D())
    ImGui::Text("3D: %zu models (%.1f MB)  %zu drawn  %.0fk triangles  turn %.0f  tilt %.0f", meshes_.meshCount(),
                meshes_.vertexBytes() / 1048576.0, meshes_.instancesLastFrame(), meshes_.trianglesLastFrame() / 1000.0,
                cam_.yaw() * 57.2958f, cam_.pitch() * 57.2958f);
  const Vec2i m = mouseWorldLeptons();
  const int tx = m.x / LEPTONS_PER_TILE, ty = m.y / LEPTONS_PER_TILE;
  if (map_.inBounds(tx, ty)) {
    const TileInfo& ti = map_.info(tx, ty);
    ImGui::Text("Mouse tile %d,%d  height %.0f  slope %u  water %d", tx, ty,
                map_.heightAt(static_cast<float>(m.x), static_cast<float>(m.y)), ti.slope,
                std::max<int>(0, ti.waterDepth));
  }
  ImGui::Checkbox("Paused", &paused_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderFloat("Speed", &speed_, 0.25f, 8.0f, "x%.2f", ImGuiSliderFlags_Logarithmic);
  ImGui::Checkbox("Reveal map", &revealMap_);
  ImGui::SameLine();
  ImGui::Checkbox("All health bars", &allHealthBars_);
  const SimStats& st = sim_->stats();
  ImGui::Text("Sim ms: path %.2f  orders %.2f  move %.2f  hash %.2f  push %.2f", st.msPaths, st.msOrders, st.msMove,
              st.msSpatial, st.msSeparation);
  ImGui::Text("        vision %.2f  target %.2f  fire %.2f  shells %.2f", st.msVision, st.msTargeting, st.msWeapons,
              st.msProjectiles);
  ImGui::Text("Path searches %u (queue %u)   shells in flight %u   effects %zu / %zu", st.pathSearches, st.pathQueue,
              st.projectiles, fx_.particleCount(), fx_.decalCount());
  if (ImGui::Button("State hash")) lastHash_ = sim_->stateHash();
  if (lastHash_) {
    ImGui::SameLine();
    ImGui::Text("%016llx", static_cast<unsigned long long>(lastHash_));
  }

  ImGui::SeparatorText("Spawn at screen centre (preview of the Live Editor)");
  const Vec2f c = groundAt({static_cast<float>(app_.pixelWidth()) / 2, static_cast<float>(app_.pixelHeight()) / 2});
  const Vec2i at{static_cast<int32_t>(c.x), static_cast<int32_t>(c.y)};
  static int typeIndex = 0;
  ImGui::SetNextItemWidth(200);
  if (ImGui::BeginCombo("Unit", types_.get(typeIndex).id.c_str())) {
    for (int i = 0; i < static_cast<int>(types_.size()); ++i)
      if (ImGui::Selectable(types_.get(i).id.c_str(), i == typeIndex)) typeIndex = i;
    ImGui::EndCombo();
  }
  const std::string& id = types_.get(typeIndex).id;
  for (int n : {1, 10, 100, 1000, 10000}) {
    char label[32];
    std::snprintf(label, sizeof label, "+%d", n);
    if (ImGui::Button(label)) spawnAt(id, player_, at, n);
    ImGui::SameLine();
  }
  ImGui::NewLine();
  if (ImGui::Button("Remove all units")) {
    Command cmd;
    cmd.type = CommandType::Remove;
    Registry& reg = sim_->registry();
    for (uint32_t i : reg.pool<CUnit>().entities()) cmd.units.push_back(reg.handle(i));
    sim_->queue(std::move(cmd));
    selection_.clear();
  }
  ImGui::End();
}

}  // namespace legion

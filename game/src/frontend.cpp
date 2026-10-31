// Legion Classic - game flow: the main menu's attract battle, skirmishes,
// scenarios, saving and loading, and replays.
#include <imgui.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "editor.hpp"
#include "game_scene.hpp"
#include "menus.hpp"
#include "slave/core/log.hpp"

using namespace slave;

namespace legion {

namespace {
const uint32_t kSideColors[4] = {rgba8(196, 58, 44), rgba8(52, 96, 196), rgba8(66, 160, 72), rgba8(214, 184, 62)};

std::string nowString() {
  const std::time_t t = std::time(nullptr);
  char buf[64];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", std::localtime(&t));
  return buf;
}
}  // namespace

void GameScene::addTownBuildings(MapFile& f) const {
  static const char* kinds[] = {"neutral_cottage", "neutral_cottage_bombed", "neutral_townhouse",
                                "neutral_church", "neutral_ruin", "neutral_adobe"};
  for (const Map::Lot& l : f.map.lots) {
    const char* id = kinds[l.kind % 6];
    const int t = types_.find(id);
    if (t < 0) continue;
    const UnitTypeDef& d = types_.get(t);
    UnitPlacement u;
    u.type = id;
    u.owner = NEUTRAL_OWNER;
    // Footprint centre: odd sizes sit on the tile centre, even ones on its corner.
    u.x = l.tx * LEPTONS_PER_TILE + (d.footW % 2 ? LEPTONS_PER_TILE / 2 : 0);
    u.y = l.ty * LEPTONS_PER_TILE + (d.footH % 2 ? LEPTONS_PER_TILE / 2 : 0);
    u.facing = static_cast<uint16_t>(angleOfFacing(static_cast<int>((l.tx * 7 + l.ty * 3) % 4) * 4));
    f.units.push_back(u);
  }
}

std::string GameScene::scenarioJson() const {
  if (scenario_.empty()) return {};
  return scriptToJson(scenario_, &types_).dump();
}

void GameScene::scenarioChanged() {
  if (sim_) sim_->setScript(scenario_, true);
}

// The baseline a replay starts from: the map and state as they are now.
void GameScene::beginRecording() {
  if (!sim_) return;
  replayStart_ = SaveFile{};
  replayStart_.metaJson = metaJson(map_.name);
  replayStart_.mapBytes = encodeMap(map_, {}, playerList(), scenarioJson());
  ByteWriter w;
  sim_->saveState(w);
  replayStart_.stateBytes = std::move(w.bytes());
  sim_->startRecording();
  replayValid_ = true;
}

std::string GameScene::metaJson(const std::string& title) const {
  nlohmann::json j;
  j["title"] = title;
  j["map"] = map_.name;
  j["date"] = nowString();
  j["tick"] = sim_ ? sim_->tickCount() : 0;
  j["player"] = player_;
  const Vec2f c = cam_.center();
  j["camera"] = {c.x, c.y};
  j["revealMap"] = revealMap_;
  auto& sides = j["sides"] = nlohmann::json::array();
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    const PlayerSetup& p = players_[i];
    if (!p.active) continue;
    sides.push_back({{"name", p.name}, {"faction", p.faction}, {"human", p.human}, {"color", p.color}});
  }
  return j.dump();
}

// ---------------------------------------------------------------------------
// Starting games
// ---------------------------------------------------------------------------
void GameScene::startSkirmish(const SkirmishSetup& s) {
  autosaveReplay();
  lastSkirmish_ = s;
  lastScenarioPath_.clear();
  MapFile f;
  f.map.resize(s.mapSize, s.mapSize);
  f.map.generate(static_cast<Biome>(s.biome), s.seed);
  f.map.name = std::string(biomeName(static_cast<Biome>(s.biome))) + " skirmish";
  f.players.resize(MAX_PLAYERS);
  int slot = 0;
  for (int i = 0; i < 4; ++i) {
    const auto& side = s.sides[static_cast<size_t>(i)];
    PlayerSetup p;
    p.faction = side.faction;
    p.color = side.color;
    p.team = static_cast<uint8_t>(side.team);
    p.active = side.open;
    p.human = side.human;
    p.difficulty = static_cast<uint8_t>(side.difficulty);
    p.credits = s.credits;
    static const char* aiNames[3] = {"easy", "normal", "hard"};
    p.name = std::string(side.faction == "rus" ? "Russia" : "Germany") +
             (side.human ? "" : std::string(" (computer, ") + aiNames[std::clamp(side.difficulty, 0, 2)] + ")");
    f.players[static_cast<size_t>(i)] = p;
    if (side.open) ++slot;
  }
  for (int i = 4; i < MAX_PLAYERS; ++i) {
    f.players[static_cast<size_t>(i)].name = "Player " + std::to_string(i + 1);
    f.players[static_cast<size_t>(i)].team = static_cast<uint8_t>(i);
  }
  (void)slot;
  addTownBuildings(f);
  mode_ = Mode::Game;
  revealMap_ = false;
  paused_ = false;
  speed_ = 1.0f;
  replaceWorld(std::move(f));
  // Bases: west and east, then north and south.
  const int32_t T = LEPTONS_PER_TILE;
  const int W = map_.width(), H = map_.height();
  const Vec2i spots[4] = {{W * 17 / 100 * T, H / 2 * T}, {W * 83 / 100 * T, H / 2 * T},
                          {W / 2 * T, H * 17 / 100 * T}, {W / 2 * T, H * 83 / 100 * T}};
  for (uint8_t i = 0; i < 4; ++i) {
    if (!players_[i].active) continue;
    const Vec2i at = findLand(spots[i]);
    buildBase(i, at);
    players_[i].startX = at.x;
    players_[i].startY = at.y;
    // A small starting force in front of the base.
    const std::string f = players_[i].faction;
    const Vec2i front{at.x + (spots[i].x < W * T / 2 ? 9 : spots[i].x > W * T / 2 ? -9 : 0) * T,
                      at.y + (spots[i].y < H * T / 2 ? 9 : spots[i].y > H * T / 2 ? -9 : 0) * T};
    spawnAt(f + "_rifleman", i, front, 8);
    spawnAt(f + "_medium_tank", i, {front.x, front.y + 3 * T}, 2);
  }
  sim_->tick();
  centerOn({players_[player_].startX, players_[player_].startY});
  // Nobody human: watch the computers fight.
  bool anyHuman = false;
  for (uint8_t i = 0; i < 4; ++i) anyHuman = anyHuman || (players_[i].active && players_[i].human);
  revealMap_ = !anyHuman;
  beginRecording();
  showStatus("Good luck, commander.", 3);
}

bool GameScene::startScenario(const std::string& path, std::string& error) {
  MapFile f;
  if (!loadMap(path, f, error)) return false;
  autosaveReplay();
  lastScenarioPath_ = path;
  mode_ = Mode::Game;
  revealMap_ = false;
  paused_ = false;
  speed_ = 1.0f;
  currentMapPath_ = path;
  replaceWorld(std::move(f));
  if (!map_.description.empty()) {
    banner_ = map_.name;
    bannerTime_ = 4;
    messages_.push_back({map_.description, 0});
  }
  return true;
}

void GameScene::restart() {
  if (!lastScenarioPath_.empty()) {
    std::string err;
    if (startScenario(lastScenarioPath_, err)) return;
  }
  startSkirmish(lastSkirmish_);
}

// The menu's backdrop: two computer players fighting it out, seen from above.
void GameScene::startAttract() {
  SkirmishSetup s;
  s.seed = static_cast<uint64_t>(std::time(nullptr)) % 100000;
  s.biome = static_cast<int>(s.seed % static_cast<uint64_t>(Biome::Count));
  s.sides[0] = {true, "ger", false, 2, 0, kSideColors[0]};
  s.sides[1] = {true, "rus", false, 2, 1, kSideColors[1]};
  s.credits = 10000;
  const SkirmishSetup keep = lastSkirmish_;
  startSkirmish(s);
  lastSkirmish_ = keep;
  sim_->stopRecording();
  replayValid_ = false;
  revealMap_ = true;
  mode_ = Mode::Menu;
  speed_ = 2.0f;
  attractTime_ = 0;
  // Start over the first base; the camera moves to the fighting once it starts.
  attractFocus_ = {static_cast<float>(players_[0].startX + map_.widthLeptons() / 8), static_cast<float>(players_[0].startY)};
  centerOn({static_cast<int32_t>(attractFocus_.x), static_cast<int32_t>(attractFocus_.y)});
}

void GameScene::toMainMenu() {
  autosaveReplay();
  if (editor_) editor_->setActive(false);
  startAttract();
}

// ---------------------------------------------------------------------------
// Saving and loading
// ---------------------------------------------------------------------------
bool GameScene::saveGame(const std::string& path, std::string& error) {
  if (mode_ != Mode::Game) {
    error = "only a game in progress can be saved";
    return false;
  }
  SaveFile f;
  f.metaJson = metaJson(map_.name + ", " + std::to_string(sim_->tickCount() / TICKS_PER_SECOND / 60) + " min");
  f.mapBytes = encodeMap(map_, {}, playerList(), scenarioJson());
  ByteWriter w;
  sim_->saveState(w);
  f.stateBytes = std::move(w.bytes());
  if (!writeSaveFile(path, f, false, error)) return false;
  showStatus("Game saved", 2.5f);
  return true;
}

bool GameScene::loadFromSave(SaveFile& f, bool replay, std::string& error) {
  MapFile mf;
  if (!decodeMap(f.mapBytes, mf, error)) return false;
  const nlohmann::json meta = nlohmann::json::parse(f.metaJson, nullptr, false);
  mode_ = Mode::Game;
  replaceWorld(std::move(mf), true);
  ByteReader r(f.stateBytes);
  if (!sim_->loadState(r, error)) {
    toMainMenu();
    return false;
  }
  if (meta.is_object()) {
    player_ = static_cast<uint8_t>(meta.value("player", 0) % MAX_PLAYERS);
    revealMap_ = meta.value("revealMap", false);
    if (meta.contains("camera")) cam_.setCenter({meta["camera"].at(0).get<float>(), meta["camera"].at(1).get<float>()});
  }
  paused_ = false;
  speed_ = 1.0f;
  if (!replay) beginRecording();  // a loaded game can be recorded from here on
  return true;
}

bool GameScene::loadGame(const std::string& path, std::string& error) {
  SaveFile f;
  if (!readSaveFile(path, f, false, error)) return false;
  autosaveReplay();
  if (!loadFromSave(f, false, error)) return false;
  showStatus("Game loaded", 2.5f);
  return true;
}

// ---------------------------------------------------------------------------
// Replays
// ---------------------------------------------------------------------------
bool GameScene::saveReplay(const std::string& path, std::string& error) {
  if (!replayValid_ || !sim_ || !sim_->recording()) {
    error = "this game can't be replayed (the map was edited while playing)";
    return false;
  }
  SaveFile f = replayStart_;
  f.commands = sim_->commandLog();
  f.endTick = sim_->tickCount();
  nlohmann::json meta = nlohmann::json::parse(f.metaJson, nullptr, false);
  if (meta.is_object()) {
    meta["date"] = nowString();
    meta["endTick"] = f.endTick;
    f.metaJson = meta.dump();
  }
  return writeSaveFile(path, f, true, error);
}

void GameScene::autosaveReplay() {
  if (mode_ != Mode::Game || !sim_ || !replayValid_ || sim_->tickCount() < TICKS_PER_SECOND * 30) return;
  std::string err;
  if (!saveReplay(replaysDir_ + "last_game.lrep", err)) SLV_WARN("Couldn't keep the replay: %s", err.c_str());
}

bool GameScene::playReplay(const std::string& path, std::string& error) {
  SaveFile f;
  if (!readSaveFile(path, f, true, error)) return false;
  autosaveReplay();
  std::vector<Simulation::LoggedCommand> log = std::move(f.commands);
  const uint32_t end = f.endTick;
  if (!loadFromSave(f, true, error)) return false;
  sim_->stopRecording();
  replayValid_ = false;
  replayLog_ = std::move(log);
  replayNext_ = 0;
  replayEnd_ = end;
  mode_ = Mode::Replay;
  showStatus("Replay: Tab switches the side you watch; R reveals the map", 4);
  return true;
}

float GameScene::replayProgress() const {
  if (!sim_ || replayEnd_ == 0) return 0;
  return std::clamp(static_cast<float>(sim_->tickCount()) / static_cast<float>(replayEnd_), 0.0f, 1.0f);
}

}  // namespace legion

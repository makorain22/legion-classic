// Legion Classic - game flow: the main menu's attract battle, skirmishes,
// scenarios, saving and loading, and replays.
#include <imgui.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "editor.hpp"
#include "game_scene.hpp"
#include "menus.hpp"
#include "slave/core/log.hpp"
#include "slave/sim/factions.hpp"

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
    for (const Map::Lot& l : f.map.lots) {
    const char* id = Map::lotBuildingId(l.kind);
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
  // The rules it was played with: loading needs the same mods (replays the very same rules).
  j["mods"] = content_.modIds().size() > 1
                  ? std::vector<std::string>(content_.modIds().begin() + 1, content_.modIds().end())
                  : std::vector<std::string>{};
  j["rules"] = rulesHash_;
  if (rulesEditedLive_) j["rulesEdited"] = true;
  if (sim_) j["types"] = sim_->typeIds();  // so a later roster can renumber the units by id
  if (!campaignId_.empty()) {
    j["campaign"] = campaignId_;
    j["mission"] = missionIndex_;
  }
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
std::vector<GameScene::SkirmishMapInfo> GameScene::skirmishMaps() const {
  std::vector<SkirmishMapInfo> out;
  for (const std::string& dir : content_.all("maps/skirmish/")) {
    std::ifstream in(dir + "maps.json");
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) continue;
    for (const auto& m : j.value("maps", nlohmann::json::array())) {
      SkirmishMapInfo i;
      i.path = dir + m.value("file", std::string());
      i.name = m.value("name", m.value("file", std::string()));
      i.description = m.value("description", std::string());
      i.biome = m.value("biome", std::string());
      i.size = m.value("size", 128);
      auto same = std::find_if(out.begin(), out.end(), [&](const SkirmishMapInfo& o) { return o.name == i.name; });
      if (same != out.end()) *same = i;
      else out.push_back(i);
    }
  }
  return out;
}

void GameScene::startSkirmish(const SkirmishSetup& s) {
  autosaveReplay();
  lastSkirmish_ = s;
  lastScenarioPath_.clear();
  campaignId_.clear();
  missionIndex_ = -1;
  missionResult_ = 0;
  MapFile f;
  bool readyMade = false;
  if (!s.mapFile.empty()) {
    std::string err;
    if (loadMap(s.mapFile, f, err)) readyMade = true;
    else SLV_WARN("Skirmish map %s: %s - making a random one instead", s.mapFile.c_str(), err.c_str());
  }
  if (!readyMade) {
    f = MapFile{};
    f.map.resize(s.mapSize, s.mapSize);
    f.map.generate(static_cast<Biome>(s.biome), s.seed, s.layout);
    f.map.name = std::string(mapLayoutName(f.map.layout)) + " (" + biomeName(static_cast<Biome>(s.biome)) + ")";
  }
  f.players.clear();
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
    p.name = std::string(slave::factionName(side.faction)) +
             (side.human ? "" : std::string(" (computer, ") + aiNames[std::clamp(side.difficulty, 0, 2)] + ")");
    f.players[static_cast<size_t>(i)] = p;
    if (side.open) ++slot;
  }
  for (int i = 4; i < MAX_PLAYERS; ++i) {
    f.players[static_cast<size_t>(i)].name = "Player " + std::to_string(i + 1);
    f.players[static_cast<size_t>(i)].team = static_cast<uint8_t>(i);
  }
  (void)slot;
  // Where each side starts.
  const int W = f.map.width(), H = f.map.height();
  std::array<Vec2i, 4> starts{};  // tiles
  {
    const Vec2i fixed[4] = {{W * 17 / 100, H / 2}, {W * 83 / 100, H / 2}, {W / 2, H * 17 / 100}, {W / 2, H * 83 / 100}};
    for (int i = 0; i < 4; ++i) starts[static_cast<size_t>(i)] = fixed[i];
    // Teams: at most two, each gathered on its own side.
    std::vector<int> teams;
    for (int i = 0; i < 4; ++i)
      if (s.sides[static_cast<size_t>(i)].open &&
          std::find(teams.begin(), teams.end(), s.sides[static_cast<size_t>(i)].team) == teams.end())
        teams.push_back(s.sides[static_cast<size_t>(i)].team);
    if (s.startLayout == 0 && teams.size() == 2) {
      for (int k = 0; k < 2; ++k) {
        std::vector<int> members;
        for (int i = 0; i < 4; ++i)
          if (s.sides[static_cast<size_t>(i)].open && s.sides[static_cast<size_t>(i)].team == teams[static_cast<size_t>(k)]) members.push_back(i);
        const int x = k == 0 ? W * 15 / 100 : W * 85 / 100;
        for (size_t m = 0; m < members.size(); ++m)
          starts[static_cast<size_t>(members[m])] = {x, members.size() == 1 ? H / 2 : H * (m == 0 ? 30 : 70) / 100};
      }
    } else if (s.startLayout == 2) {
      std::array<int, 4> order{0, 1, 2, 3};
      uint32_t h = static_cast<uint32_t>(s.seed * 2654435761u);
      for (int i = 3; i > 0; --i) {
        h = h * 1664525u + 1013904223u;
        std::swap(order[static_cast<size_t>(i)], order[h % static_cast<uint32_t>(i + 1)]);
      }
      for (int i = 0; i < 4; ++i) starts[static_cast<size_t>(i)] = fixed[order[static_cast<size_t>(i)]];
    }
    // Keep villages out of the bases.
    std::erase_if(f.map.lots, [&](const Map::Lot& l) {
      for (int i = 0; i < 4; ++i)
        if (s.sides[static_cast<size_t>(i)].open && std::abs(l.tx - starts[static_cast<size_t>(i)].x) < 16 &&
            std::abs(l.ty - starts[static_cast<size_t>(i)].y) < 16)
          return true;
      return false;
    });
  }
  addTownBuildings(f);
  mode_ = Mode::Game;
  revealMap_ = false;
  paused_ = false;
  speed_ = 1.0f;
  replaceWorld(std::move(f));
  // Bases where the sides start.
  const int32_t T = LEPTONS_PER_TILE;
  for (uint8_t i = 0; i < 4; ++i) {
    if (!players_[i].active) continue;
    const Vec2i spot{starts[i].x * T, starts[i].y * T};
    const Vec2i at = findLand(spot);
    buildBase(i, at);
    players_[i].startX = at.x;
    players_[i].startY = at.y;
    // A small starting force on the parade ground inside the gate.
    const std::string f = players_[i].faction;
    const int32_t dx = map_.widthLeptons() / 2 - at.x, dy = map_.heightLeptons() / 2 - at.y;
    const int fx = std::abs(dx) >= std::abs(dy) ? (dx >= 0 ? 1 : -1) : 0, fy = fx == 0 ? (dy >= 0 ? 1 : -1) : 0;
    const Vec2i front{at.x + fx * 7 * T + (-fy) * 4 * T, at.y + fy * 7 * T + fx * 4 * T};
    spawnAt(f + "_rifleman", i, front, 10);
    spawnAt(f + "_medium_tank", i, {front.x - fy * 3 * T + fx * T, front.y + fx * 3 * T + fy * T}, 2);
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
  campaignId_.clear();
  missionIndex_ = -1;
  missionResult_ = 0;
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
  if (!campaignId_.empty() && missionIndex_ >= 0) {
    std::string err;
    const std::string c = campaignId_;
    if (startMission(c, missionIndex_, err)) return;
  }
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

// ---------------------------------------------------------------------------
// Campaigns
// ---------------------------------------------------------------------------
void GameScene::loadCampaigns() {
  std::string warn;
  campaigns_ = scanCampaigns(content_.all("campaigns/"), warn);
  if (!warn.empty()) SLV_WARN("%s", warn.c_str());
  progress_.load(app_.userDir() + "progress.json");
}

const CampaignInfo* GameScene::currentCampaign() const {
  for (const CampaignInfo& c : campaigns_)
    if (c.id == campaignId_) return &c;
  return nullptr;
}

bool GameScene::startMission(const std::string& campaign, int mission, std::string& error) {
  const CampaignInfo* c = nullptr;
  for (const CampaignInfo& x : campaigns_)
    if (x.id == campaign) c = &x;
  if (!c || mission < 0 || mission >= static_cast<int>(c->missions.size())) {
    error = "no such mission";
    return false;
  }
  // The flags earlier missions left are there from the first tick.
  auto f = progress_.flags.find(campaign);
  nextFlags_ = f != progress_.flags.end() ? f->second : std::map<std::string, int32_t>{};
  const std::string path = c->mapPath(static_cast<size_t>(mission));
  if (!startScenario(path, error)) {
    nextFlags_.clear();
    return false;
  }
  campaignId_ = campaign;
  missionIndex_ = mission;
  missionResult_ = 0;
  banner_ = c->missions[static_cast<size_t>(mission)].title;
  bannerTime_ = 4;
  return true;
}

void GameScene::resetProgress(const std::string& campaign) {
  progress_.done.erase(campaign);
  progress_.flags.erase(campaign);
  progress_.save(app_.userDir() + "progress.json");
}

void GameScene::missionOver(bool won) {
  if (campaignId_.empty() || missionIndex_ < 0 || missionResult_ != 0) return;
  missionResult_ = won ? 1 : 2;
  const CampaignInfo* c = currentCampaign();
  if (!won || !c) return;
  progress_.recordWin(campaignId_, c->missions[static_cast<size_t>(missionIndex_)].id, sim_->flags());
  progress_.save(app_.userDir() + "progress.json");
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

// Saves and replays carry the mods and rules they were made with.
bool GameScene::rulesMatch(const nlohmann::json& meta, bool replay, std::string& error) const {
  if (!meta.is_object() || !meta.contains("rules")) return true;  // made before mods existed
  std::vector<std::string> theirs;
  if (meta.contains("mods") && meta["mods"].is_array())
    for (const auto& m : meta["mods"])
      if (m.is_string()) theirs.push_back(m.get<std::string>());
  const std::vector<std::string> ours(content_.modIds().begin() + 1, content_.modIds().end());
  auto list = [](const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& m : v) s += (s.empty() ? "" : ", ") + m;
    return s.empty() ? std::string("no mods") : s;
  };
  if (theirs != ours) {
    error = std::string("this ") + (replay ? "replay" : "game") + " was made with " + list(theirs) +
            "; you have " + list(ours) + " enabled (Mods on the main menu)";
    return false;
  }
  if (replay && meta.value("rules", std::string()) != rulesHash_) {
    error = meta.value("rulesEdited", false)
                ? "the rules were changed during this game, so it can't be replayed"
                : "the mods have changed since this replay was recorded, so it can't be replayed exactly";
    return false;
  }
  return true;
}

bool GameScene::loadFromSave(SaveFile& f, bool replay, std::string& error) {
  MapFile mf;
  if (!decodeMap(f.mapBytes, mf, error)) return false;
  const nlohmann::json meta = nlohmann::json::parse(f.metaJson, nullptr, false);
  if (!rulesMatch(meta, replay, error)) return false;
  const bool sameRules = !meta.is_object() || meta.value("rules", rulesHash_) == rulesHash_;
  mode_ = Mode::Game;
  replaceWorld(std::move(mf), true);
  // The unit type list it was saved with. Saves from before 0.9.0 don't carry
  // one; theirs is today's list without the British, who came in 0.9.0 (mods
  // still add their types after the base game's in each file, as then).
  std::vector<std::string> savedTypes;
  if (meta.is_object() && meta.contains("types") && meta["types"].is_array()) {
    for (const auto& t : meta["types"]) savedTypes.push_back(t.is_string() ? t.get<std::string>() : std::string());
  } else {
    for (int t = 0; t < static_cast<int>(types_.size()); ++t)
      if (types_.get(t).faction != "gbr") savedTypes.push_back(types_.get(t).id);
  }
  ByteReader r(f.stateBytes);
  bool renumbered = false;
  if (!sim_->loadState(r, error, replay ? nullptr : &savedTypes, &renumbered)) {
    toMainMenu();
    return false;
  }
  if (renumbered && sameRules) sim_->rulesChanged();
  if (!sameRules) {
    sim_->rulesChanged();  // the mods were edited since: bring the units in line
    showStatus("The mods have changed since this game was saved", 5);
  }
  campaignId_.clear();
  missionIndex_ = -1;
  missionResult_ = 0;
  if (meta.is_object() && !replay && meta.contains("campaign")) {
    campaignId_ = meta.value("campaign", std::string());
    missionIndex_ = meta.value("mission", -1);
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

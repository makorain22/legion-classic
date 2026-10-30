#include "game_scene.hpp"

#include <glad/gl.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>

#include "editor.hpp"
#include "slave/core/log.hpp"
#include "slave/math/iso.hpp"

using namespace slave;

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
  if (cursorArrow_) SDL_DestroyCursor(cursorArrow_);
  if (cursorCross_) SDL_DestroyCursor(cursorCross_);
}

bool GameScene::init(const std::string& contentDir, std::string& error) {
  if (!weapons_.loadFile(contentDir + "rules/weapons.json", error)) return false;
  if (!types_.loadFile(contentDir + "rules/units.json", error)) return false;
  if (!types_.resolveWeapons(weapons_, error)) return false;
  if (!buildPlaceholderArt(art_, error)) return false;
  if (!renderer_.init(error)) return false;
  if (!terrain_.init(error)) return false;
  renderer_.setAtlas(art_.atlas, art_.frames);
  renderer_.setWhiteFrame(art_.white);

  for (const UnitTypeDef& t : types_.all()) {
    auto it = art_.units.find(t.model + "@" + t.faction);
    if (it == art_.units.end()) it = art_.units.find(t.model + "@ger");
    if (it == art_.units.end()) {
      error = "unit '" + t.id + "' uses unknown model '" + t.model + "'";
      return false;
    }
    unitArt_.push_back(it->second);
  }
  fx_.init(&art_, &unitArt_, &types_, &weapons_,
           [this](float lx, float ly) { return groundToScreenWorld(lx, ly); });
  if (!app_.offscreen()) {
    cursorArrow_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    cursorCross_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
  }

  mapsDir_ = app_.userDir() + "maps/";
  std::error_code ec;
  std::filesystem::create_directories(mapsDir_, ec);
  editor_ = std::make_unique<Editor>(*this);

  cam_.setViewport(app_.pixelWidth(), app_.pixelHeight());
  if (opt_.zoom >= 2.0f) cam_.zoomStep(+1, {0, 0}), cam_.zoomStep(+1, {0, 0});
  else if (opt_.zoom <= 0.5f) cam_.zoomStep(-1, {0, 0});

  if (!opt_.loadMap.empty()) {
    MapFile f;
    if (!loadMap(opt_.loadMap, f, error)) return false;
    currentMapPath_ = opt_.loadMap;
    replaceWorld(std::move(f));
  } else {
    MapFile f;
    f.map.resize(opt_.mapSize, opt_.mapSize);
    f.map.generateCountryside(opt_.seed);
    f.map.name = "Random countryside";
    replaceWorld(std::move(f));
    setupStartingArmies();
    sim_->tick();  // apply the spawn commands
  }
  if (opt_.startInEditor) editor_->setActive(true);
  SLV_INFO("Map %dx%d, %zu props, %zu units", map_.width(), map_.height(), map_.props().size(), sim_->unitCount());
  return true;
}

void GameScene::replaceWorld(MapFile&& file, bool keepCamera) {
  const Vec2f camera = cam_.center();
  selection_.clear();
  for (auto& g : groups_) g.clear();
  markers_.clear();
  fx_.clear();
  map_ = std::move(file.map);
  map_.recomputeInfo();
  sim_ = std::make_unique<Simulation>(map_, types_, weapons_, opt_.seed);
  terrain_.setMap(map_);
  players_ = defaultPlayers();
  for (size_t i = 0; i < file.players.size() && i < players_.size(); ++i) players_[i] = file.players[i];
  applyPlayers();
  if (!players_[player_].active) {
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i)
      if (players_[i].active) {
        player_ = i;
        break;
      }
  }
  for (const UnitPlacement& u : file.units) {
    const int t = types_.find(u.type);
    if (t < 0) {
      SLV_WARN("Map uses unknown unit type '%s' - skipped", u.type.c_str());
      continue;
    }
    Command c;
    c.type = CommandType::Spawn;
    c.unitType = static_cast<uint16_t>(t);
    c.player = u.owner;
    c.target = {u.x, u.y};
    c.facing = u.facing;
    sim_->queue(std::move(c));
  }
  sim_->tick();  // apply teams and spawns
  updateCameraBounds();
  if (keepCamera) cam_.setCenter(camera);
  else {
    const PlayerSetup& me = players_[player_];
    if (me.startX >= 0) centerOn({me.startX, me.startY});
    else centerOn({map_.widthLeptons() / 2, map_.heightLeptons() / 2});
  }
  if (editor_) editor_->onWorldReplaced();
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
  SLV_INFO("Snapshot restored in %.0f ms", (SDL_GetTicksNS() - t0) / 1e6);
  if (players_[me].active) player_ = me;
  showStatus("Snapshot restored", 2.5f);
}

std::vector<UnitPlacement> GameScene::exportUnits() {
  std::vector<UnitPlacement> out;
  Registry& reg = sim_->registry();
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    const CPosition& p = reg.pool<CPosition>().get(e);
    out.push_back({types_.get(u.type).id, u.owner, p.x, p.y, reg.pool<CFacing>().get(e).angle, u.health});
  }
  return out;
}

void GameScene::showStatus(const std::string& text, float seconds) {
  status_ = text;
  statusTime_ = seconds;
}

void GameScene::updateCameraBounds() {
  const float w = static_cast<float>(map_.width()), h = static_cast<float>(map_.height());
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

void GameScene::setupStartingArmies() {
  const int32_t T = LEPTONS_PER_TILE;
  const int mid = map_.height() / 2;
  // The demo battle starts with the armies a short drive apart.
  const int west = map_.width() * (opt_.demo ? 50 : 14) / 128, east = map_.width() * (opt_.demo ? 78 : 114) / 128;
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
  else centerOn({g.x + 6 * T, g.y});
}

void GameScene::centerOn(Vec2i l) { cam_.setCenter(groundToScreenWorld(static_cast<float>(l.x), static_cast<float>(l.y))); }

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
Vec2f GameScene::groundToScreenWorld(float lx, float ly) const {
  Vec2f s = iso::worldToScreen(lx, ly);
  s.y -= map_.heightAt(lx, ly) * kPxPerUnit;
  return s;
}

Vec2f GameScene::groundAt(Vec2f screenPx) const {
  // Start on flat ground, then refine using the height found there.
  const Vec2f w = cam_.screenToWorld(screenPx);
  Vec2f l = iso::screenToWorld(w);
  for (int i = 0; i < 8; ++i) {
    const float h = map_.heightAt(l.x, l.y) * kPxPerUnit;
    l = iso::screenToWorld({w.x, w.y + h});
  }
  return l;
}

Vec2f GameScene::unitScreenWorld(uint32_t e) const {
  Registry& reg = sim_->registry();
  const CPosition& p = reg.pool<CPosition>().get(e);
  const CPrevPosition& q = reg.pool<CPrevPosition>().get(e);
  return groundToScreenWorld(q.x + (p.x - q.x) * alpha_, q.y + (p.y - q.y) * alpha_);
}

Vec2i GameScene::mouseWorldLeptons() const {
  const Vec2f l = groundAt(mouse_);
  return {static_cast<int32_t>(std::lround(l.x)), static_cast<int32_t>(std::lround(l.y))};
}

Entity GameScene::pickUnit(Vec2f screenPx, bool ownOnly) const {
  Registry& reg = sim_->registry();
  const Vec2f w = cam_.screenToWorld(screenPx);
  Entity best;
  int64_t bestDepth = INT64_MIN;
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (ownOnly && !selectableOwner(u.owner)) continue;
    if (u.owner != player_) {
      const CPosition& lp = reg.pool<CPosition>().get(e);
      if (!watching(lp.x, lp.y)) continue;  // can't click what you can't see
    }
    const Vec2f s = unitScreenWorld(e);
    const float hw = std::max(7.0f, types_.get(u.type).radius * 0.2f);
    const float hh = hw * 0.85f;
    if (std::abs(w.x - s.x) > hw || std::abs(w.y - (s.y - hh * 0.6f)) > hh) continue;
    const CPosition& p = reg.pool<CPosition>().get(e);
    const int64_t d = iso::depthKey(p.x, p.y);
    if (d > bestDepth) {
      bestDepth = d;
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
    const Vec2f s = cam_.worldToScreen(unitScreenWorld(e));
    if (!box.contains({s.x, s.y - 6})) continue;
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

void GameScene::orderMove(Vec2i target, CommandType type) {
  pruneSelection();
  if (selection_.empty()) return;
  Command c;
  c.type = type;
  c.player = player_;
  c.target = target;
  c.units = selection_;
  queueOrder(std::move(c));
  markers_.push_back({groundToScreenWorld(static_cast<float>(target.x), static_cast<float>(target.y)), 0,
                      type == CommandType::AttackMove ? kAttackRed : kSelectGreen});
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
  const CPosition& p = sim_->registry().get<CPosition>(target);
  markers_.push_back({groundToScreenWorld(static_cast<float>(p.x), static_cast<float>(p.y)), 0, kAttackRed});
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

  // The editor gets first look at input while it is open.
  if (editor_->onEvent(e, uiMouse, uiKeys)) return;

  switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
      if (mmbDown_) cam_.pan({-e.motion.xrel * ps, -e.motion.yrel * ps});
      if (lmbDown_ && !dragging_ && (mouse_ - dragStart_).length() > 6.0f) dragging_ = true;
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
        const Entity hit = pickUnit(mouse_, false);
        const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
        if (hit.valid() && sim_->registry().get<CUnit>(hit).owner != player_) orderAttack(hit);
        else orderMove(mouseWorldLeptons(), ctrl ? CommandType::AttackMove : CommandType::Move);
      } else if (e.button.button == SDL_BUTTON_MIDDLE) {
        mmbDown_ = true;
      }
      break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (e.button.button == SDL_BUTTON_MIDDLE) mmbDown_ = false;
      if (e.button.button == SDL_BUTTON_LEFT && lmbDown_) {
        lmbDown_ = false;
        const bool add = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        if (dragging_) {
          selectInBox(dragStart_, mouse_, add);
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
      if (k == SDLK_ESCAPE) {
        if (attackMoveMode_) attackMoveMode_ = false;
        else selection_.clear();
      } else if (k == SDLK_A && !ctrl) {
        pruneSelection();
        attackMoveMode_ = !selection_.empty();
      }
      else if (k == SDLK_F1) showHelp_ = !showHelp_;
      else if (k == SDLK_F2) editor_->setActive(!editor_->active());
      else if (k == SDLK_F3) showDebug_ = !showDebug_;
      else if (k == SDLK_F12) {
        const std::time_t t = std::time(nullptr);
        char name[64];
        std::strftime(name, sizeof name, "screenshot_%Y%m%d_%H%M%S.png", std::localtime(&t));
        app_.requestScreenshot(app_.userDir() + name);
        showStatus(std::string("Screenshot saved to ") + app_.userDir() + name, 4);
      } else if (k == SDLK_SPACE) paused_ = !paused_;
      else if (k == SDLK_TAB) {
        for (int i = 1; i <= MAX_PLAYERS; ++i) {  // next active side
          const uint8_t p = static_cast<uint8_t>((player_ + i) % MAX_PLAYERS);
          if (players_[p].active) {
            setPlayer(p);
            break;
          }
        }
      } else if (k == SDLK_S && !ctrl) {
        pruneSelection();
        Command c;
        c.type = CommandType::Stop;
        c.units = selection_;
        sim_->queue(std::move(c));
      } else if (k == SDLK_A && ctrl) {
        selection_.clear();
        Registry& reg = sim_->registry();
        for (uint32_t i : reg.pool<CUnit>().entities())
          if (reg.pool<CUnit>().get(i).owner == player_) selection_.push_back(reg.handle(i));
      } else if (k == SDLK_DELETE && !editor_->active()) {
        Command c;
        c.type = CommandType::Remove;
        c.units = selection_;
        sim_->queue(std::move(c));
        selection_.clear();
      } else if (k == SDLK_HOME && !selection_.empty()) {
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
      } else if (k == SDLK_EQUALS || k == SDLK_KP_PLUS) {
        speed_ = std::min(8.0f, speed_ * 2);
      } else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
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
void GameScene::onTick() {
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
  editor_->onSimTick(sim_->spawnLog());
  const auto& events = sim_->events();
  for (const SimEvent& ev : events) {
    const bool seen = watching(ev.at.x, ev.at.y) || (ev.type == EventType::Fire && watching(ev.from.x, ev.from.y));
    fx_.onEvent(ev, seen, teamColors_);
  }
}

void GameScene::onUpdate(float dt, float alpha) {
  alpha_ = alpha;
  time_ += dt;
  cam_.setViewport(app_.pixelWidth(), app_.pixelHeight());

  Vec2f pan;
  const bool* keys = SDL_GetKeyboardState(nullptr);
  if (!ImGui::GetIO().WantCaptureKeyboard) {
    if (keys[SDL_SCANCODE_LEFT]) pan.x -= 1;
    if (keys[SDL_SCANCODE_RIGHT]) pan.x += 1;
    if (keys[SDL_SCANCODE_UP]) pan.y -= 1;
    if (keys[SDL_SCANCODE_DOWN]) pan.y += 1;
  }
  if (!app_.offscreen() && SDL_GetMouseFocus() == app_.window() && !mmbDown_ && !ImGui::GetIO().WantCaptureMouse) {
    const float w = static_cast<float>(app_.pixelWidth()), h = static_cast<float>(app_.pixelHeight());
    if (mouse_.x <= kEdgeScrollPx) pan.x -= 1;
    if (mouse_.x >= w - 1 - kEdgeScrollPx) pan.x += 1;
    if (mouse_.y <= kEdgeScrollPx) pan.y -= 1;
    if (mouse_.y >= h - 1 - kEdgeScrollPx) pan.y += 1;
  }
  if (pan.x != 0 || pan.y != 0) cam_.pan(pan * (kScrollSpeed * dt));

  ++frame_;
  if (opt_.editorDemo) runEditorDemo();
  if (opt_.liveDemo) runLiveDemo();
  if (opt_.editorSelfTest) runEditorSelfTest();
  editor_->update(dt);
  terrain_.update();
  if (fogEnabled()) terrain_.setFog(&sim_->vision().grid(player_), sim_->vision().revision() * 16u + player_);
  else terrain_.setFog(nullptr, 0);
  terrain_.updateFog(dt);
  fx_.update(dt * timeScale());
  hovered_ = app_.offscreen() || ImGui::GetIO().WantCaptureMouse ? Entity{} : pickUnit(mouse_, false);
  updateCursor();

  for (Marker& m : markers_) m.age += dt;
  if (attackMoveMode_) {
    pruneSelection();
    if (selection_.empty()) attackMoveMode_ = false;
  }
  std::erase_if(markers_, [](const Marker& m) { return m.age > 0.6f; });
  if (statusTime_ > 0) statusTime_ -= dt;
}

// A scripted editing session used by the automated screenshot tests: sculpts
// a hill, paints a track and a field, plants a wood, then undoes the last step.
void GameScene::runEditorDemo() {
  const Vec2f c = groundAt({app_.pixelWidth() * 0.62f, app_.pixelHeight() * 0.55f});
  const float T = static_cast<float>(LEPTONS_PER_TILE);
  auto stroke = [&](Tool tool, float radius, float strength, Vec2f from, Vec2f to, int steps) {
    editor_->setTool(tool);
    editor_->setBrush(radius, strength);
    editor_->beginStroke();
    for (int i = 0; i <= steps; ++i) {
      const float t = steps ? static_cast<float>(i) / steps : 0.0f;
      editor_->strokeAt({from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t}, 1.0f / 15.0f);
    }
    editor_->endStroke();
  };
  if (frame_ == 2) {
    editor_->setActive(true);
    stroke(Tool::Raise, 7, 60, {c.x, c.y}, {c.x, c.y}, 40);
    stroke(Tool::Smooth, 9, 80, {c.x, c.y}, {c.x, c.y}, 10);
    editor_->setPaintLayer(Layer::Road);
    stroke(Tool::Paint, 1.2f, 100, {c.x - 14 * T, c.y + 10 * T}, {c.x + 6 * T, c.y - 6 * T}, 60);
    editor_->setPaintLayer(Layer::Dirt);
    stroke(Tool::Paint, 4, 60, {c.x + 8 * T, c.y + 8 * T}, {c.x + 12 * T, c.y + 6 * T}, 12);
    stroke(Tool::Props, 5, 100, {c.x - 8 * T, c.y - 4 * T}, {c.x - 6 * T, c.y - 2 * T}, 30);
    stroke(Tool::Lower, 3, 100, {c.x + 10 * T, c.y - 10 * T}, {c.x + 10 * T, c.y - 10 * T}, 20);
    editor_->undo();  // takes the last dip back out
    editor_->save(mapsDir_ + "editor_demo.lmap");
  }
}

// Drives the real editor through place / undo / redo / delete / move / give
// and checks the battlefield after each step. Run with --editor-selftest.
void GameScene::runEditorSelfTest() {
  Registry& reg = sim_->registry();
  auto count = [&]() { return static_cast<int>(reg.pool<CUnit>().size()); };
  auto check = [&](bool ok, const char* what) {
    if (!ok) ++selfTestFailures_;
    SLV_INFO("EDITOR SELFTEST %s: %s (units %d)", ok ? "ok  " : "FAIL", what, count());
  };
  auto selectAll = [&]() {
    selection_.clear();
    for (uint32_t i : reg.pool<CUnit>().entities()) selection_.push_back(reg.handle(i));
  };
  static int placed = 0;
  // Steps run every 8 frames so each command has been applied by a tick.
  if (frame_ % 8 != 0) return;
  switch (frame_ / 8) {
    case 1: {
      editor_->setActive(true);
      Command rm;
      rm.type = CommandType::Remove;
      for (uint32_t i : reg.pool<CUnit>().entities()) rm.units.push_back(reg.handle(i));
      sim_->queue(std::move(rm));
      break;
    }
    case 2:
      check(count() == 0, "battlefield cleared");
      editor_->setPlaceUnit("ger_rifleman", 0, PlaceMode::Block, 10);
      mouse_ = {app_.pixelWidth() * 0.6f, app_.pixelHeight() * 0.5f};
      editor_->beginStroke();
      editor_->endStroke();
      break;
    case 3:
      placed = count();
      check(placed >= 6 && placed <= 10, "place a block of 10 (those on dry land)");
      editor_->undo();
      break;
    case 4: check(count() == 0, "undo the placement"); editor_->redo(); break;
    case 5: check(count() == placed, "redo the placement"); selectAll(); editor_->deleteSelection(); break;
    case 6: check(count() == 0, "delete the selection"); editor_->undo(); break;
    case 7: {
      check(count() == placed, "undo the delete brings them back");
      selectAll();
      before_.clear();
      for (uint32_t i : reg.pool<CUnit>().entities()) before_.push_back({reg.pool<CPosition>().get(i).x, reg.pool<CPosition>().get(i).y});
      // Pick a direction where every unit lands on dry ground.
      const int32_t T = LEPTONS_PER_TILE;
      moveDelta_ = {};
      for (Vec2i d : {Vec2i{3 * T, 0}, Vec2i{0, 3 * T}, Vec2i{-3 * T, 0}, Vec2i{0, -3 * T}}) {
        bool ok = true;
        for (const Vec2i& p : before_) ok = ok && map_.passable((p.x + d.x) >> LEPTON_SHIFT, (p.y + d.y) >> LEPTON_SHIFT, Locomotor::Foot);
        if (ok) {
          moveDelta_ = d;
          break;
        }
      }
      editor_->moveSelection(moveDelta_);
      break;
    }
    case 8: {
      bool ok = moveDelta_.x != 0 || moveDelta_.y != 0;
      size_t k = 0;
      for (uint32_t i : reg.pool<CUnit>().entities()) {
        const CPosition& p = reg.pool<CPosition>().get(i);
        // Separation may nudge units a little after they land.
        ok = ok && k < before_.size() && std::abs(p.x - before_[k].x - moveDelta_.x) < 64 &&
             std::abs(p.y - before_[k].y - moveDelta_.y) < 64;
        ++k;
      }
      check(ok, "move the selection 3 tiles");
      editor_->undo();
      break;
    }
    case 9: {
      bool ok = true;
      size_t k = 0;
      for (uint32_t i : reg.pool<CUnit>().entities()) {
        const CPosition& p = reg.pool<CPosition>().get(i);
        ok = ok && k < before_.size() && std::abs(p.x - before_[k].x) < 64 && std::abs(p.y - before_[k].y) < 64;
        ++k;
      }
      check(ok, "undo the move");
      selectAll();
      editor_->setSelectionOwner(1);
      break;
    }
    case 10: {
      bool all = true;
      for (uint32_t i : reg.pool<CUnit>().entities()) all = all && reg.pool<CUnit>().get(i).owner == 1;
      check(all && count() == placed, "give units to side 2");
      editor_->undo();
      break;
    }
    case 11: {
      bool all = true;
      for (uint32_t i : reg.pool<CUnit>().entities()) all = all && reg.pool<CUnit>().get(i).owner == 0;
      check(all, "undo the gift");
      editor_->undo();  // the placement itself, through two respawns' worth of remapping
      break;
    }
    case 12:
      check(count() == 0, "undo the original placement after delete/undo");
      takeSnapshot();  // an empty battlefield
      editor_->redo();
      break;
    case 13: check(count() == placed, "redo the placement once more"); restoreSnapshot(); break;
    case 14:
      check(count() == 0, "restore the empty snapshot");
      SLV_INFO("EDITOR SELFTEST %s (%d failures)", selfTestFailures_ ? "FAILED" : "PASSED", selfTestFailures_);
      quit_ = true;
      break;
    default: break;
  }
}

// A scripted Live Scenario Editor session for the automated screenshots:
// places two forces with the unit tools, sets them fighting, calls in
// artillery, then selects a tank to show the inspector.
void GameScene::runLiveDemo() {
  const float W = static_cast<float>(app_.pixelWidth()), H = static_cast<float>(app_.pixelHeight());
  auto clickAt = [&](float fx, float fy) {
    mouse_ = {W * fx, H * fy};
    editor_->beginStroke();
    editor_->strokeAt(groundAt(mouse_), 1.0f / 60.0f);
    editor_->endStroke();
  };
  if (frame_ == 2) {
    editor_->setActive(true);
    paused_ = false;
    // Clear the default armies so the scene is built entirely with the editor.
    Command rm;
    rm.type = CommandType::Remove;
    for (uint32_t i : sim_->registry().pool<CUnit>().entities()) rm.units.push_back(sim_->registry().handle(i));
    sim_->queue(std::move(rm));
    editor_->setPlaceFacing(14);
    editor_->setPlaceFacing(12);
    editor_->setPlaceUnit("ger_medium_tank", 0, PlaceMode::Block, 6);
    clickAt(0.78f, 0.80f);
    editor_->setPlaceUnit("ger_rifleman", 0, PlaceMode::Block, 24);
    clickAt(0.90f, 0.70f);
    editor_->setPlaceFacing(4);
    editor_->setPlaceUnit("rus_rifleman", 1, PlaceMode::Block, 40);
    clickAt(0.42f, 0.52f);
    editor_->setPlaceUnit("rus_medium_tank", 1, PlaceMode::Block, 4);
    clickAt(0.36f, 0.66f);
  }
  if (frame_ == 6) {
    Registry& reg = sim_->registry();
    Command g, r;
    g.type = r.type = CommandType::AttackMove;
    for (uint32_t i : reg.pool<CUnit>().entities()) (reg.pool<CUnit>().get(i).owner == 0 ? g : r).units.push_back(reg.handle(i));
    mouse_ = {W * 0.42f, H * 0.55f};
    g.target = mouseWorldLeptons();
    mouse_ = {W * 0.80f, H * 0.78f};
    r.target = mouseWorldLeptons();
    queueOrder(std::move(g));
    queueOrder(std::move(r));
  }
  if (frame_ == 150) {
    editor_->setTool(Tool::Artillery);
    editor_->setBrush(3, 40);
    clickAt(0.42f, 0.52f);
  }
  if (frame_ == 290) {
    editor_->setTool(Tool::SelectUnits);
    selection_.clear();
    Registry& reg = sim_->registry();
    for (uint32_t i : reg.pool<CUnit>().entities())
      if (types_.get(reg.pool<CUnit>().get(i).type).id == "ger_medium_tank") {
        selection_.push_back(reg.handle(i));
        break;
      }
    mouse_ = {W * 0.6f, H * 0.6f};
  }
  if (frame_ == 340) {  // second picture: the unit palette with a block of ghosts under the mouse
    editor_->setPlaceFacing(10);
    editor_->setPlaceUnit("rus_rifleman", 1, PlaceMode::Block, 30);
    mouse_ = {W * 0.62f, H * 0.30f};
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void GameScene::onRender() {
  glClearColor(0.05f, 0.05f, 0.045f, 1.0f);
  glClearDepth(1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  renderer_.resetStats();
  pruneSelection();

  terrain_.draw(cam_, time_);
  renderer_.beginWorld(cam_);

  const RectF view = cam_.visibleWorld(120);
  const bool fog = fogEnabled();
  const BattleFx::FogAt fogAt = [&](int tx, int ty) -> uint8_t { return fog ? terrain_.fogAt(tx, ty) : 255; };

  fx_.drawGround(renderer_, view, fogAt);
  for (Entity e : selection_) {
    const Vec2f s = unitScreenWorld(e.index);
    const bool big = types_.get(sim_->registry().get<CUnit>(e).type).radius > 60;
    renderer_.sprite(big ? art_.selectLarge : art_.selectSmall, s.x, s.y, kWhite, kSelectGreen);
  }
  drawUnits(view);
  fx_.drawAir(renderer_, view, fogAt);

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
    const uint8_t alpha = static_cast<uint8_t>(255 * (1.0f - m.age / 0.6f));
    renderer_.sprite(art_.moveMarker, m.pos.x, m.pos.y, kWhite, (m.color & 0x00FFFFFFu) | (uint32_t(alpha) << 24));
  }
  editor_->renderWorld(renderer_);
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

// Units, wrecks and props, sorted back to front. Vehicles with turrets are two
// sprites: the hull, then the turret on top at its own pivot and facing.
void GameScene::drawUnits(const RectF& view) {
  Registry& reg = sim_->registry();
  const bool fog = fogEnabled();
  drawList_.clear();
  uint32_t order = 0;
  for (const Prop& p : map_.props()) {
    const Vec2f flat = iso::worldToScreen(static_cast<float>(p.x), static_cast<float>(p.y));
    if (flat.x < view.x0 || flat.x > view.x1 || flat.y < view.y0 || flat.y > view.y1 + 400) continue;
    const Vec2f s = groundToScreenWorld(static_cast<float>(p.x), static_cast<float>(p.y));
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
    drawList_.push_back({iso::depthKey(p.x, p.y), order++, variants[p.variant % variants.size()], s.x, s.y, kWhite, tint});
  }
  for (const BattleFx::Wreck& w : fx_.wrecks()) {
    const Vec2f s = groundToScreenWorld(static_cast<float>(w.lx), static_cast<float>(w.ly));
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
    drawList_.push_back({depth, order++, w.bodyFrame, s.x, s.y, w.team, tint});
    if (w.turretFrame >= 0) {
      const Vec2f t = groundToScreenWorld(w.lx + w.turretShift.x, w.ly + w.turretShift.y);
      drawList_.push_back({depth, order++, w.turretFrame, t.x, t.y, w.team, tint});
    }
  }
  auto& units = reg.pool<CUnit>();
  auto& turrets = reg.pool<CTurret>();
  for (size_t i = 0; i < units.size(); ++i) {
    const uint32_t e = units.entities()[i];
    const CUnit& u = units.data()[i];
    const CPosition& p = reg.pool<CPosition>().get(e);
    if (fog && u.owner != player_ && !sim_->vision().visible(player_, p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT)) continue;
    const Vec2f s = unitScreenWorld(e);
    if (!view.contains(s)) continue;
    const CFacing& f = reg.pool<CFacing>().get(e);
    const CAnim& a = reg.pool<CAnim>().get(e);
    const UnitArt& art = unitArt_[u.type];
    const int64_t depth = iso::depthKey(p.x, p.y);
    const int facing = facingOf(f.angle);
    drawList_.push_back({depth, order++, art.body.frame(facing, a.moving ? a.frame : 0), s.x, s.y, teamColors_[u.owner], kWhite});
    if (art.hasTurret) {
      const CTurret* t = turrets.tryGet(e);
      const int tf = t ? facingOf(t->angle) : facing;
      // The pivot turns with the hull's drawn facing, so it stays on the hull.
      const float ang = static_cast<float>(angleOfFacing(facing)) * (6.2831853f / ANGLE_FULL);
      const CPrevPosition& q = reg.pool<CPrevPosition>().get(e);
      const float lx = q.x + (p.x - q.x) * alpha_ + std::cos(ang) * art.turretOffset;
      const float ly = q.y + (p.y - q.y) * alpha_ + std::sin(ang) * art.turretOffset;
      const Vec2f ts = groundToScreenWorld(lx, ly);
      drawList_.push_back({depth, order++, art.turret.frame(tf, 0), std::round(ts.x), std::round(ts.y), teamColors_[u.owner], kWhite});
    }
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
  const float w = big ? 28.0f : 12.0f;
  const float y = std::floor(s.y) - (big ? 30.0f : 24.0f);
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
  drawHud();
  editor_->imgui();
  if (showHelp_) drawHelp();
  if (showDebug_) drawDebug();
}

void GameScene::drawHud() {
  const bool editing = editor_->active();
  ImGui::SetNextWindowPos(ImVec2(editing ? Editor::kPanelWidth + 16 : 8, 8));
  ImGui::SetNextWindowBgAlpha(0.82f);
  ImGui::Begin("##hud", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
  ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.36f, 1), "LEGION CLASSIC");
  ImGui::SameLine();
  ImGui::TextDisabled("Slave Engine 1.0  |  Milestone 4");
  ImGui::Text("Map: %s (%dx%d)", map_.name.c_str(), map_.width(), map_.height());
  ImGui::Text("Commanding:");
  ImGui::SameLine();
  ImGui::TextColored(toImVec(teamColors_[player_]), "%s", players_[player_].name.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("(Tab)");
  ImGui::Text("Selected: %zu   Units: %zu", selection_.size(), sim_->unitCount());
  if (paused_) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "PAUSED (Space)");
  else if (speed_ != 1.0f) ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Speed x%.2g", speed_);
  if (attackMoveMode_) ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "ATTACK-MOVE: left-click a destination (Esc cancels)");
  if (statusTime_ > 0) ImGui::TextColored(ImVec4(0.6f, 0.95f, 0.6f, 1), "%s", status_.c_str());
  ImGui::TextDisabled(editing ? "F2 close editor   F1 help" : "F2 editor   F1 help   F3 debug");
  ImGui::End();
}

void GameScene::drawHelp() {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 8, 8), ImGuiCond_Always, ImVec2(1, 0));
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
  row("A, then click", "Attack-move (fight on the way)");
  row("Ctrl+right click", "Attack-move");
  row("Ctrl+1..9 / 1..9", "Set / recall control group");
  row("Ctrl+A", "Select all your units");
  row("S", "Stop");
  row("Tab", "Command (and see as) the other side");
  row("Arrows / edges", "Scroll the map");
  row("Middle drag", "Pan the map");
  row("Mouse wheel", "Zoom 0.5x / 1x / 2x");
  row("Home", "Centre on selection");
  row("Space", "Pause");
  row("+ / -", "Game speed");
  row("Delete", "Remove selected units");
  row("F2", "Map editor");
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

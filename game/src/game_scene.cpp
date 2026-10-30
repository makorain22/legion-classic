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
#include "slave/core/rng.hpp"
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
  if (!types_.loadFile(contentDir + "rules/buildings.json", error)) return false;
  if (!types_.resolveWeapons(weapons_, error)) return false;
  if (!types_.resolveLinks(error)) return false;
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
  trenchTypes_.assign(types_.size(), false);
  for (size_t i = 0; i < types_.size(); ++i) trenchTypes_[i] = types_.get(static_cast<int>(i)).model == "trench";
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
  sidebar_ = std::make_unique<Sidebar>(*this);

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
  for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
    Command c;
    c.type = CommandType::SetCredits;
    c.player = i;
    c.amount = players_[i].credits;
    sim_->queue(std::move(c));
  }
  sim_->tick();  // apply teams, supplies and spawns
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

// A starting base: construction yard, power, an oil depot (with its truck)
// and barracks, laid out around `centre`, facing the enemy.
void GameScene::buildBase(uint8_t player, Vec2i centre) {
  const int32_t T = LEPTONS_PER_TILE;
  const std::string f = players_[player].faction;
  const int dir = centre.x < map_.widthLeptons() / 2 ? 1 : -1;  // +1: enemy to the east
  // Placed one at a time, each on the nearest clear ground to its planned spot.
  auto add = [&](const std::string& id, int dx, int dy) {
    const int t = types_.find(f + "_" + id);
    if (t < 0) return;
    const Vec2i want{centre.x + dx * dir * T, centre.y + dy * T};
    for (int r = 0; r <= 6; ++r)
      for (int oy = -r; oy <= r; ++oy)
        for (int ox = -r; ox <= r; ++ox) {
          if (std::max(std::abs(ox), std::abs(oy)) != r) continue;
          const Vec2i at{want.x + ox * T, want.y + oy * T};
          if (!sim_->canPlace(t, sim_->footprintOrigin(t, at))) continue;
          Command c;
          c.type = CommandType::SpawnList;
          c.spawns.push_back({static_cast<uint16_t>(t), player, at, 0, -1});
          sim_->queue(std::move(c));
          sim_->tick();
          return;
        }
  };
  add("yard", 0, 0);
  add("power", -4, -4);
  add("power", -4, 4);
  add("refinery", 0, -6);
  add("barracks", 5, 3);
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
    const UnitTypeDef& d = types_.get(u.type);
    const CPosition& p = reg.pool<CPosition>().get(e);
    if (d.isStructure()) {
      // Structures: the ground under the mouse is inside the footprint (or the tall part above it).
      const Vec2f g = groundAt(screenPx);
      const CStructure& st = reg.pool<CStructure>().get(e);
      const bool onFoot = g.x >= st.tx * 256.0f && g.y >= st.ty * 256.0f && g.x < (st.tx + st.w) * 256.0f &&
                          g.y < (st.ty + st.h) * 256.0f;
      const Vec2f s = unitScreenWorld(e);
      const float hw = d.footW * 12.0f, hh = d.footH * 10.0f + 18.0f;
      const bool onBody = d.category != UnitCategory::Fortification && std::abs(w.x - s.x) < hw && w.y < s.y && w.y > s.y - hh;
      if (!onFoot && !onBody) continue;
    } else {
      const Vec2f s = unitScreenWorld(e);
      const float hw = std::max(7.0f, d.radius * 0.2f);
      const float hh = hw * 0.85f;
      if (std::abs(w.x - s.x) > hw || std::abs(w.y - (s.y - hh * 0.6f)) > hh) continue;
    }
    // Prefer units over the structure they stand in front of, then the frontmost.
    const int64_t depth = iso::depthKey(p.x, p.y) + (d.isStructure() ? 0 : (int64_t{1} << 40));
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
  c.formation = sidebar_ ? sidebar_->formation() : Formation::Box;
  c.forced = (SDL_GetModState() & SDL_KMOD_ALT) != 0;  // Alt: forced march, don't stop to fight
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

// Right mouse button released. A click: attack an enemy under the mouse,
// send trucks to oil, set a rally point for factories, or move. A drag: move
// into the chosen formation with its front along the dragged line.
void GameScene::rightClick(Vec2f start, bool dragged, Vec2f end) {
  pruneSelection();
  if (selection_.empty()) return;
  Registry& reg = sim_->registry();
  const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
  if (dragged) {
    const Vec2f a = groundAt(start), b = groundAt(end);
    const Vec2i mid{static_cast<int32_t>((a.x + b.x) * 0.5f), static_cast<int32_t>((a.y + b.y) * 0.5f)};
    // The front faces away from where the units are now.
    int64_t cx = 0, cy = 0, n = 0;
    for (Entity e : selection_)
      if (types_.get(reg.get<CUnit>(e).type).speed > 0) {
        cx += reg.get<CPosition>(e).x;
        cy += reg.get<CPosition>(e).y;
        ++n;
      }
    if (!n) return;
    cx /= n;
    cy /= n;
    float nx = -(b.y - a.y), ny = b.x - a.x;  // a normal to the line
    if (nx * (mid.x - cx) + ny * (mid.y - cy) < 0) {
      nx = -nx;
      ny = -ny;
    }
    Command c;
    c.type = ctrl ? CommandType::AttackMove : CommandType::Move;
    c.player = player_;
    c.target = mid;
    c.units = selection_;
    c.formation = sidebar_->formation() == Formation::Box ? Formation::Line : sidebar_->formation();
    c.formFacing = atan2Angle(static_cast<int32_t>(ny * 64), static_cast<int32_t>(nx * 64));
    c.formWidth = static_cast<int32_t>(std::hypot(b.x - a.x, b.y - a.y));
    c.forced = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    queueOrder(std::move(c));
    markers_.push_back({groundToScreenWorld(static_cast<float>(mid.x), static_cast<float>(mid.y)), 0,
                        ctrl ? kAttackRed : kSelectGreen});
    return;
  }
  const Entity hit = pickUnit(start, false);
  if (hit.valid() && !sim_->allied(reg.get<CUnit>(hit).owner, player_) && types_.get(reg.get<CUnit>(hit).type).targetable) {
    orderAttack(hit);
    return;
  }
  const Vec2i g = mouseWorldLeptons();
  std::vector<Entity> trucks, producers, deployers, rest;
  for (Entity e : selection_) {
    if (reg.has<CHarvester>(e)) trucks.push_back(e);
    else if (reg.has<CProducer>(e)) producers.push_back(e);
    else if (hit == e && types_.get(reg.get<CUnit>(e).type).deploysTo >= 0) deployers.push_back(e);
    else rest.push_back(e);
  }
  // Supply trucks: right-click oil (or a refinery) to put them to work.
  const int tx = g.x >> LEPTON_SHIFT, ty = g.y >> LEPTON_SHIFT;
  const bool onOil = map_.inBounds(tx, ty) && map_.oil(tx, ty) > 0;
  const bool onRefinery = hit.valid() && types_.get(reg.get<CUnit>(hit).type).role == "refinery";
  if (!trucks.empty() && (onOil || onRefinery)) {
    Command c;
    c.type = CommandType::Harvest;
    c.units = trucks;
    c.target = onOil ? g : Vec2i{-1, -1};
    sim_->queue(std::move(c));
    trucks.clear();
  }
  rest.insert(rest.end(), trucks.begin(), trucks.end());
  if (!producers.empty()) {
    Command c;
    c.type = CommandType::SetRally;
    c.units = producers;
    c.target = g;
    sim_->queue(std::move(c));
    markers_.push_back({groundToScreenWorld(static_cast<float>(g.x), static_cast<float>(g.y)), 0, rgba8(236, 200, 96)});
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

  // The editor gets first look at input while it is open, then the sidebar's
  // placement and sell modes.
  if (editor_->onEvent(e, uiMouse, uiKeys)) return;
  if (sidebar_->onEvent(e, uiMouse)) return;

  switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
      if (mmbDown_) cam_.pan({-e.motion.xrel * ps, -e.motion.yrel * ps});
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
      } else if (k == SDLK_D && !ctrl) {
        pruneSelection();
        Command c;
        c.type = CommandType::Deploy;
        c.units = selection_;
        sim_->queue(std::move(c));
      } else if (k == SDLK_DELETE && !editor_->active() && showDebug_) {  // debug only
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
    announce(ev);
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
    case EventType::PlayerDefeated: {
      banner_ = players_[ev.owner].name + " has been defeated";
      bannerTime_ = 6;
      // Last side standing (with its allies) wins.
      bool enemiesLeft = false;
      for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        if (players_[p].active && !sim_->allied(p, player_) && !sim_->playerState(p).defeated) enemiesLeft = true;
      if (ev.owner == player_) banner_ = "DEFEAT";
      else if (!enemiesLeft && !sim_->playerState(player_).defeated) banner_ = "VICTORY";
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
  if (opt_.economyDemo) runEconomyDemo();
  editor_->update(dt);
  sidebar_->update(dt);
  drawStructureEffects(dt * timeScale());
  if (bannerTime_ > 0) bannerTime_ -= dt;
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
  sidebar_->renderWorld(renderer_);
  // A formation front being dragged out: the line and an arrow showing which way it will face.
  if (rmbDragging_ && !selection_.empty()) {
    const Vec2f a = groundAt(rmbStart_), b = groundAt(mouse_);
    const int n = std::clamp(static_cast<int>(std::hypot(b.x - a.x, b.y - a.y) / 40.0f), 2, 200);
    for (int i = 0; i <= n; ++i) {
      const float t = static_cast<float>(i) / n;
      const Vec2f s = groundToScreenWorld(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
      renderer_.rect(std::floor(s.x) - 1, std::floor(s.y) - 1, 3, 3, kSelectGreen);
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
    if (fog && !sim_->allied(u.owner, player_) && terrain_.fogAt(st.tx, st.ty) == 0) continue;
    auto isTrench = [&](int x, int y) {
      const Entity o = sim_->structureAt(x, y);
      return o.valid() && trenchTypes_[reg.get<CUnit>(o).type];
    };
    const int mask = (isTrench(st.tx + 1, st.ty) ? 1 : 0) | (isTrench(st.tx, st.ty + 1) ? 2 : 0) |
                     (isTrench(st.tx - 1, st.ty) ? 4 : 0) | (isTrench(st.tx, st.ty - 1) ? 8 : 0);
    const Vec2f s = groundToScreenWorld(st.tx * 256.0f + 128, st.ty * 256.0f + 128);
    const uint32_t tint = fogTint(st.tx, st.ty, 1.0f);
    if (tint) renderer_.sprite(art_.trench[static_cast<size_t>(mask)], s.x, s.y, kWhite, tint);
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
    if (w.bodyFrame < 0) continue;  // a burning ruin: only fire and smoke
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
    const UnitTypeDef& def = types_.get(u.type);
    uint32_t tint = kWhite;
    if (def.isStructure()) {
      if (u.type < trenchTypes_.size() && trenchTypes_[u.type]) continue;  // drawn on the ground layer
      // Enemy structures stay on the map once seen (dimmed when out of sight).
      if (fog && !sim_->allied(u.owner, player_)) {
        const uint8_t fv = terrain_.fogAt(p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT);
        if (fv == 0) continue;
        const float k = fv < 128 ? 0.48f * fv / 128.0f : 0.48f + 0.52f * (fv - 128) / 127.0f;
        const uint8_t c = static_cast<uint8_t>(255 * k);
        tint = rgba8(c, c, c);
      }
      // Fade in as it goes up.
      const uint32_t age = sim_->tickCount() - reg.pool<CStructure>().get(e).builtTick;
      if (age < 12) tint = (tint & 0x00FFFFFFu) | (static_cast<uint32_t>(60 + age * 16) << 24);
    } else if (fog && !sim_->allied(u.owner, player_) &&
               !sim_->vision().visible(player_, p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT)) {
      continue;
    }
    const Vec2f s = unitScreenWorld(e);
    if (!view.contains(s)) continue;
    const CFacing& f = reg.pool<CFacing>().get(e);
    const CAnim& a = reg.pool<CAnim>().get(e);
    const UnitArt& art = unitArt_[u.type];
    // Structures sort by their front corner, so units behind them are hidden by them.
    const int64_t depth = def.isStructure() ? iso::depthKey(p.x + def.footW * 96, p.y + def.footH * 96) - 1 : iso::depthKey(p.x, p.y);
    const int facing = facingOf(f.angle);
    drawList_.push_back({depth, order++, art.body.frame(facing, a.moving ? a.frame : 0), s.x, s.y, teamColors_[u.owner], tint});
    if (art.hasTurret) {
      const CTurret* t = turrets.tryGet(e);
      const int tf = t ? facingOf(t->angle) : facing;
      // The pivot turns with the hull's drawn facing, so it stays on the hull.
      const float ang = static_cast<float>(angleOfFacing(facing)) * (6.2831853f / ANGLE_FULL);
      const CPrevPosition& q = reg.pool<CPrevPosition>().get(e);
      const float lx = q.x + (p.x - q.x) * alpha_ + std::cos(ang) * art.turretOffset;
      const float ly = q.y + (p.y - q.y) * alpha_ + std::sin(ang) * art.turretOffset;
      const Vec2f ts = groundToScreenWorld(lx, ly);
      drawList_.push_back({depth, order++, art.turret.frame(tf, 0), std::round(ts.x), std::round(ts.y), teamColors_[u.owner], tint});
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
  float w = big ? 24.0f : 10.0f;
  float y = std::floor(s.y) - (big ? 24.0f : 17.0f);
  if (def.isStructure()) {
    w = std::max(12.0f, (def.footW + def.footH) * 10.0f);
    y = std::floor(s.y) - (def.footW + def.footH) * 8.0f - 22.0f;
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
  drawHud();
  sidebar_->draw();
  editor_->imgui();
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
  ImGui::TextDisabled("Slave Engine 1.0  |  Milestone 5");
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

#include "game_scene.hpp"

#include <glad/gl.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "slave/core/log.hpp"
#include "slave/math/iso.hpp"

using namespace slave;

namespace legion {

namespace {
constexpr uint32_t kSelectGreen = rgba8(120, 236, 96);
constexpr float kEdgeScrollPx = 6.0f;
constexpr float kScrollSpeed = 1100.0f;  // screen pixels per second

ImVec4 toImVec(uint32_t c) {
  return ImVec4((c & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, ((c >> 16) & 0xff) / 255.0f, 1.0f);
}
}  // namespace

GameScene::GameScene(App& app, SceneOptions opt) : app_(app), opt_(opt) {
  teamColors_ = {rgba8(196, 58, 44),  rgba8(52, 96, 196),  rgba8(66, 160, 72),  rgba8(214, 184, 62),
                 rgba8(220, 120, 40), rgba8(132, 72, 180), rgba8(60, 170, 170), rgba8(210, 210, 200)};
  playerNames_ = {"Germany", "Russia", "Player 3", "Player 4", "Player 5", "Player 6", "Player 7", "Player 8"};
  showHelp_ = opt.showHelp;
  showDebug_ = opt.showDebug;
}

bool GameScene::init(const std::string& contentDir, std::string& error) {
  if (!types_.loadFile(contentDir + "rules/units.json", error)) return false;
  if (!buildPlaceholderArt(art_, error)) return false;
  if (!renderer_.init(error)) return false;
  renderer_.setAtlas(art_.atlas, art_.frames);
  renderer_.setWhiteFrame(art_.white);

  // Map each unit type to its sprite sheet.
  for (const UnitTypeDef& t : types_.all()) {
    auto it = art_.units.find(t.model + "@" + t.faction);
    if (it == art_.units.end()) it = art_.units.find(t.model + "@ger");
    if (it == art_.units.end()) {
      error = "unit '" + t.id + "' uses unknown model '" + t.model + "'";
      return false;
    }
    sheets_.push_back(it->second);
    unitSheet_.push_back(static_cast<int>(sheets_.size() - 1));
  }

  map_.resize(opt_.mapSize, opt_.mapSize);
  map_.generateCountryside(opt_.seed);
  terrain_.build(map_, art_, renderer_);
  sim_ = std::make_unique<Simulation>(map_, types_, opt_.seed);

  // Camera limits: the bounding box of the map's diamond.
  const float w = static_cast<float>(map_.width()), h = static_cast<float>(map_.height());
  cam_.setBounds({-h * iso::HALF_W, 0, w * iso::HALF_W, (w + h) * iso::HALF_H});
  cam_.setViewport(app_.pixelWidth(), app_.pixelHeight());
  if (opt_.zoom >= 2.0f) cam_.zoomStep(+1, {0, 0}), cam_.zoomStep(+1, {0, 0});
  else if (opt_.zoom <= 0.5f) cam_.zoomStep(-1, {0, 0});

  setupStartingArmies();
  sim_->tick();  // apply the spawn commands
  SLV_INFO("Map %dx%d, %zu props, %zu units", map_.width(), map_.height(), map_.props().size(),
           sim_->unitCount());
  return true;
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
  c.facing = player == 0 ? angleOfFacing(2) : angleOfFacing(10);  // face each other
  sim_->queue(std::move(c));
}

void GameScene::setupStartingArmies() {
  const int32_t T = LEPTONS_PER_TILE;
  const int mid = map_.height() / 2;
  const int west = map_.width() * 30 / 128, east = map_.width() * 98 / 128;
  spawnAt("ger_medium_tank", 0, {west * T, (mid - 4) * T}, 6);
  spawnAt("ger_scout_car", 0, {(west + 5) * T, (mid + 4) * T}, 4);
  spawnAt("ger_rifleman", 0, {(west - 4) * T, (mid + 4) * T}, 20);
  spawnAt("rus_medium_tank", 1, {east * T, (mid + 4) * T}, 6);
  spawnAt("rus_scout_car", 1, {(east - 5) * T, (mid - 4) * T}, 4);
  spawnAt("rus_rifleman", 1, {(east + 4) * T, (mid - 4) * T}, 20);
  if (opt_.stressUnits > 0) {
    spawnAt("ger_rifleman", 0, {map_.width() / 3 * T, map_.height() / 3 * T}, opt_.stressUnits / 2);
    spawnAt("rus_medium_tank", 1, {map_.width() * 2 / 3 * T, map_.height() * 2 / 3 * T},
            opt_.stressUnits - opt_.stressUnits / 2);
  }
  centerOn({(west + 6) * T, mid * T});
}

void GameScene::centerOn(Vec2i l) { cam_.setCenter(iso::worldToScreen(l)); }

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
Vec2f GameScene::unitScreenWorld(uint32_t e) const {
  Registry& reg = sim_->registry();
  const CPosition& p = reg.pool<CPosition>().get(e);
  const CPrevPosition& q = reg.pool<CPrevPosition>().get(e);
  const float x = q.x + (p.x - q.x) * alpha_;
  const float y = q.y + (p.y - q.y) * alpha_;
  return iso::worldToScreen(x, y);
}

Vec2i GameScene::mouseWorldLeptons() const {
  const Vec2f l = iso::screenToWorld(cam_.screenToWorld(mouse_));
  return {static_cast<int32_t>(std::lround(l.x)), static_cast<int32_t>(std::lround(l.y))};
}

Entity GameScene::pickUnit(Vec2f screenPx, bool ownOnly) const {
  Registry& reg = sim_->registry();
  const Vec2f w = cam_.screenToWorld(screenPx);
  Entity best;
  int64_t bestDepth = INT64_MIN;
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (ownOnly && u.owner != player_) continue;
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
    if (reg.pool<CUnit>().get(e).owner != player_) continue;
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
    if (u.owner != player_ || u.type != type) continue;
    if (view.contains(cam_.worldToScreen(unitScreenWorld(e)))) selection_.push_back(reg.handle(e));
  }
}

void GameScene::pruneSelection() {
  Registry& reg = sim_->registry();
  std::erase_if(selection_, [&](Entity e) { return !reg.alive(e); });
}

void GameScene::orderMove(Vec2i target) {
  pruneSelection();
  if (selection_.empty()) return;
  Command c;
  c.type = CommandType::Move;
  c.player = player_;
  c.target = target;
  c.units = selection_;
  sim_->queue(std::move(c));
  markers_.push_back({iso::worldToScreen(target), 0});
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
void GameScene::onEvent(const SDL_Event& e, bool uiMouse, bool uiKeys) {
  const float ps = app_.pixelScale();
  switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
      mouse_ = {e.motion.x * ps, e.motion.y * ps};
      if (mmbDown_) cam_.pan({-e.motion.xrel * ps, -e.motion.yrel * ps});
      if (lmbDown_ && !dragging_ && (mouse_ - dragStart_).length() > 6.0f) dragging_ = true;
      break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      mouse_ = {e.button.x * ps, e.button.y * ps};
      if (uiMouse) break;
      if (e.button.button == SDL_BUTTON_LEFT) {
        lmbDown_ = true;
        dragging_ = false;
        dragStart_ = mouse_;
      } else if (e.button.button == SDL_BUTTON_RIGHT) {
        orderMove(mouseWorldLeptons());
      } else if (e.button.button == SDL_BUTTON_MIDDLE) {
        mmbDown_ = true;
      }
      break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
      mouse_ = {e.button.x * ps, e.button.y * ps};
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
            selectSameTypeOnScreen(hit);  // double-click
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
      if (k == SDLK_ESCAPE) selection_.clear();
      else if (k == SDLK_F1) showHelp_ = !showHelp_;
      else if (k == SDLK_F3) showDebug_ = !showDebug_;
      else if (k == SDLK_F12) {
        const std::time_t t = std::time(nullptr);
        char name[64];
        std::strftime(name, sizeof name, "screenshot_%Y%m%d_%H%M%S.png", std::localtime(&t));
        app_.requestScreenshot(app_.userDir() + name);
        status_ = std::string("Screenshot saved to ") + app_.userDir() + name;
        statusTime_ = 4;
      } else if (k == SDLK_SPACE) paused_ = !paused_;
      else if (k == SDLK_TAB) {
        player_ = player_ == 0 ? 1 : 0;
        selection_.clear();
        status_ = "Now commanding " + playerNames_[player_];
        statusTime_ = 2.5f;
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
      } else if (k == SDLK_DELETE) {
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
  // Scripted demo for automated screenshots.
  if (opt_.demo && sim_->tickCount() == 3) {
    Registry& reg = sim_->registry();
    std::vector<Entity> ger, rus;
    for (uint32_t i : reg.pool<CUnit>().entities())
      (reg.pool<CUnit>().get(i).owner == 0 ? ger : rus).push_back(reg.handle(i));
    const int32_t T = LEPTONS_PER_TILE;
    const int mid = map_.height() / 2;
    selection_ = ger;
    orderMove({(map_.width() * 30 / 128 + 14) * T, (mid - 2) * T});
    Command c;
    c.type = CommandType::Move;
    c.units = rus;
    c.target = {(map_.width() * 98 / 128 - 30) * T, (mid + 2) * T};
    sim_->queue(std::move(c));
  }
  sim_->tick();
}

void GameScene::onUpdate(float dt, float alpha) {
  alpha_ = alpha;
  cam_.setViewport(app_.pixelWidth(), app_.pixelHeight());

  // Keyboard and edge scrolling.
  Vec2f pan;
  const bool* keys = SDL_GetKeyboardState(nullptr);
  if (!ImGui::GetIO().WantCaptureKeyboard) {
    if (keys[SDL_SCANCODE_LEFT]) pan.x -= 1;
    if (keys[SDL_SCANCODE_RIGHT]) pan.x += 1;
    if (keys[SDL_SCANCODE_UP]) pan.y -= 1;
    if (keys[SDL_SCANCODE_DOWN]) pan.y += 1;
  }
  if (!app_.offscreen() && SDL_GetMouseFocus() == app_.window() && !mmbDown_) {
    const float w = static_cast<float>(app_.pixelWidth()), h = static_cast<float>(app_.pixelHeight());
    if (mouse_.x <= kEdgeScrollPx) pan.x -= 1;
    if (mouse_.x >= w - 1 - kEdgeScrollPx) pan.x += 1;
    if (mouse_.y <= kEdgeScrollPx) pan.y -= 1;
    if (mouse_.y >= h - 1 - kEdgeScrollPx) pan.y += 1;
  }
  if (pan.x != 0 || pan.y != 0) cam_.pan(pan * (kScrollSpeed * dt));

  for (Marker& m : markers_) m.age += dt;
  std::erase_if(markers_, [](const Marker& m) { return m.age > 0.6f; });
  if (statusTime_ > 0) statusTime_ -= dt;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------
void GameScene::onRender() {
  glClearColor(0.04f, 0.04f, 0.035f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  renderer_.resetStats();
  pruneSelection();

  renderer_.beginWorld(cam_);
  terrain_.draw(renderer_, cam_);

  Registry& reg = sim_->registry();
  const RectF view = cam_.visibleWorld(90);

  // Selection ellipses go under the units.
  for (Entity e : selection_) {
    const Vec2f s = unitScreenWorld(e.index);
    const bool big = types_.get(reg.get<CUnit>(e).type).radius > 60;
    renderer_.sprite(big ? art_.selectLarge : art_.selectSmall, s.x, s.y, kWhite, kSelectGreen);
  }

  // Units and props, sorted back to front.
  drawList_.clear();
  uint32_t order = 0;
  for (const Prop& p : map_.props()) {
    const Vec2f s = iso::worldToScreen(static_cast<float>(p.x), static_cast<float>(p.y));
    if (!view.contains(s)) continue;
    const auto& variants = art_.props[static_cast<size_t>(p.kind)];
    drawList_.push_back({iso::depthKey(p.x, p.y), order++, variants[p.variant % variants.size()], s.x, s.y, kWhite});
  }
  auto& units = reg.pool<CUnit>();
  for (size_t i = 0; i < units.size(); ++i) {
    const uint32_t e = units.entities()[i];
    const CUnit& u = units.data()[i];
    const Vec2f s = unitScreenWorld(e);
    if (!view.contains(s)) continue;
    const CFacing& f = reg.pool<CFacing>().get(e);
    const CAnim& a = reg.pool<CAnim>().get(e);
    const SpriteSheet& sheet = sheets_[static_cast<size_t>(unitSheet_[u.type])];
    const int frame = sheet.frame(facingOf(f.angle), a.moving ? a.frame : 0);
    const CPosition& p = reg.pool<CPosition>().get(e);
    drawList_.push_back({iso::depthKey(p.x, p.y), order++, frame, s.x, s.y, teamColors_[u.owner]});
  }
  std::sort(drawList_.begin(), drawList_.end(), [](const DrawItem& a, const DrawItem& b) {
    return a.depth != b.depth ? a.depth < b.depth : a.order < b.order;
  });
  for (const DrawItem& d : drawList_) renderer_.sprite(d.frame, d.x, d.y, d.team);

  // Health bars over selected units.
  for (Entity e : selection_) {
    const Vec2f s = unitScreenWorld(e.index);
    const CUnit& u = reg.get<CUnit>(e);
    const UnitTypeDef& def = types_.get(u.type);
    const float w = def.radius > 60 ? 28.0f : 12.0f;
    const float y = std::floor(s.y) - (def.radius > 60 ? 30.0f : 24.0f);
    const float x = std::floor(s.x - w / 2);
    const float frac = std::clamp(static_cast<float>(u.health) / def.maxHealth, 0.0f, 1.0f);
    renderer_.rect(x - 1, y - 1, w + 2, 4, rgba8(10, 10, 8));
    renderer_.rect(x, y, std::floor(w * frac), 2, frac > 0.5f ? kSelectGreen : rgba8(230, 200, 60));
  }

  // Move-order markers: a shrinking green diamond.
  for (const Marker& m : markers_) {
    const uint8_t alpha = static_cast<uint8_t>(255 * (1.0f - m.age / 0.6f));
    renderer_.sprite(art_.moveMarker, m.pos.x, m.pos.y, kWhite, rgba8(120, 236, 96, alpha));
  }
  renderer_.flush();

  // Screen space: the drag-selection box.
  if (dragging_) {
    renderer_.beginScreen(app_.pixelWidth(), app_.pixelHeight());
    const RectF r = RectF::fromPoints(dragStart_, mouse_);
    renderer_.rect(r.x0, r.y0, r.width(), r.height(), rgba8(120, 236, 96, 28));
    renderer_.rectOutline(std::floor(r.x0), std::floor(r.y0), std::floor(r.width()), std::floor(r.height()), 1,
                          kSelectGreen);
    renderer_.flush();
  }
}

// ---------------------------------------------------------------------------
// Interface (Dear ImGui for now; the pixel-art HUD arrives with Milestone 5)
// ---------------------------------------------------------------------------
void GameScene::onImGui() {
  drawHud();
  if (showHelp_) drawHelp();
  if (showDebug_) drawDebug();
}

void GameScene::drawHud() {
  ImGui::SetNextWindowPos(ImVec2(8, 8));
  ImGui::SetNextWindowBgAlpha(0.82f);
  ImGui::Begin("##hud", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
  ImGui::TextColored(ImVec4(0.92f, 0.78f, 0.36f, 1), "LEGION CLASSIC");
  ImGui::SameLine();
  ImGui::TextDisabled("Slave Engine 1.0  |  Milestone 1");
  ImGui::Text("Commanding:");
  ImGui::SameLine();
  ImGui::TextColored(toImVec(teamColors_[player_]), "%s", playerNames_[player_].c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("(Tab to switch)");
  ImGui::Text("Selected: %zu   Units on map: %zu", selection_.size(), sim_->unitCount());
  if (paused_) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "PAUSED (Space)");
  else if (speed_ != 1.0f) ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Speed x%.2g", speed_);
  if (statusTime_ > 0) ImGui::TextColored(ImVec4(0.6f, 0.95f, 0.6f, 1), "%s", status_.c_str());
  ImGui::TextDisabled("F1 help   F3 debug & spawn tools");
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
  row("Left click", "Select a unit (Shift: add/remove)");
  row("Left drag", "Box-select your units");
  row("Double-click", "Select all of that type on screen");
  row("Right click", "Move selected units");
  row("Ctrl+1..9 / 1..9", "Set / recall control group");
  row("Ctrl+A", "Select all your units");
  row("S", "Stop");
  row("Tab", "Command the other side");
  row("Arrows / edges", "Scroll the map");
  row("Middle drag", "Pan the map");
  row("Mouse wheel", "Zoom 0.5x / 1x / 2x");
  row("Home", "Centre on selection");
  row("Space", "Pause");
  row("+ / -", "Game speed");
  row("Delete", "Remove selected units");
  row("F11 / Alt+Enter", "Fullscreen");
  row("F12", "Screenshot");
  ImGui::End();
}

void GameScene::drawDebug() {
  ImGui::SetNextWindowPos(ImVec2(8, 150), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.88f);
  ImGui::Begin("Debug & spawn tools (F3)", &showDebug_, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::Text("%.0f fps  (%.2f ms)%s", app_.fps(), app_.frameMs(), app_.simBehind() ? "  SIM SLOWED" : "");
  ImGui::Text("Tick %u   units %zu   sprites %zu   draw calls %zu", sim_->tickCount(), sim_->unitCount(),
              renderer_.spritesThisFrame(), renderer_.drawCallsThisFrame());
  ImGui::Text("Map %dx%d   chunks %zu/%zu   zoom %.1fx", map_.width(), map_.height(),
              terrain_.chunksDrawnLastFrame(), terrain_.chunkCount(), cam_.zoom());
  const Vec2i m = mouseWorldLeptons();
  ImGui::Text("Mouse tile %d, %d", m.x / LEPTONS_PER_TILE, m.y / LEPTONS_PER_TILE);
  ImGui::Checkbox("Paused", &paused_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::SliderFloat("Speed", &speed_, 0.25f, 8.0f, "x%.2f", ImGuiSliderFlags_Logarithmic);
  if (ImGui::Button("State hash")) lastHash_ = sim_->stateHash();
  if (lastHash_) {
    ImGui::SameLine();
    ImGui::Text("%016llx", static_cast<unsigned long long>(lastHash_));
  }

  ImGui::SeparatorText("Spawn at screen centre (preview of the Live Editor)");
  const Vec2f c = iso::screenToWorld(cam_.screenToWorld(
      {static_cast<float>(app_.pixelWidth()) / 2, static_cast<float>(app_.pixelHeight()) / 2}));
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

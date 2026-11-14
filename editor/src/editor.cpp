#include "editor.hpp"

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

#include "game_scene.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/core/rng.hpp"
#include "pioneer/map/map_io.hpp"

using namespace pioneer;

namespace legion {

namespace {
const char* kToolNames[] = {"Raise", "Lower", "Smooth", "Flatten", "Roughen", "Paint ground", "Place props",
                            "Erase props", "Logistics points", "Place units", "Select units", "Artillery", "Smite", "Heal", "Draw area"};
const char* kToolTips[] = {
    "Raise the ground into hills.",
    "Lower the ground into valleys and riverbeds.",
    "Soften bumps and sharp edges.",
    "Level the ground to the height where you started the stroke.",
    "Add natural unevenness.",
    "Paint grass, dirt, mud, roads and more. Edges blend naturally.",
    "Click to place one, drag to scatter trees, bushes or rocks.",
    "Remove trees, bushes and rocks inside the brush.",
    "Click to add a logistics point (a side that holds one gets an office there and convoys of money). Shift-click removes the nearest.",
    "Place units for any side: one per click, a brush of them, or a whole block.",
    "Select units of any side; drag them to move them; inspect and edit them.",
    "Call down a barrage of 105 mm shells. Hurts every side.",
    "Destroy every unit under the brush, with wrecks and smoke.",
    "Restore every unit under the brush to full health.",
    "Drag a rectangle on the map to make a named area for triggers.",
};
// Swatch colours for the ground layers (approximate, for the panel only).
const ImVec4 kLayerSwatch[LAYER_COUNT] = {
    {0.33f, 0.38f, 0.19f, 1}, {0.52f, 0.48f, 0.31f, 1}, {0.44f, 0.37f, 0.27f, 1}, {0.26f, 0.22f, 0.17f, 1},
    {0.62f, 0.57f, 0.45f, 1}, {0.42f, 0.41f, 0.38f, 1}, {0.83f, 0.85f, 0.88f, 1}, {0.56f, 0.50f, 0.39f, 1},
};
const int kNewSizes[] = {64, 128, 256, 512, 1024};

float falloff(float t, bool soft) {
  if (t >= 1.0f) return 0.0f;
  if (!soft) return 1.0f;
  const float k = 1.0f - t * t;
  return k * k;
}

uint32_t toolColor(Tool t) {
  switch (t) {
    case Tool::Raise:
    case Tool::Lower:
    case Tool::Smooth:
    case Tool::Flatten:
    case Tool::Roughen: return rgba8(250, 214, 96, 220);
    case Tool::Paint: return rgba8(120, 200, 255, 220);
    case Tool::Props: return rgba8(120, 236, 96, 220);
    case Tool::Erase: return rgba8(255, 90, 70, 220);
    case Tool::Oil: return rgba8(60, 50, 40, 240);
    case Tool::PlaceUnits: return rgba8(120, 236, 96, 220);
    case Tool::Artillery: return rgba8(255, 150, 60, 230);
    case Tool::Smite: return rgba8(255, 70, 50, 230);
    case Tool::Heal: return rgba8(110, 220, 255, 230);
    default: return kWhite;
  }
}
}  // namespace

Editor::Editor(GameScene& scene) : scene_(scene) {}
Editor::~Editor() = default;

void Editor::setActive(bool on) {
  if (!on && stroking_) endStroke();
  active_ = on;
  movePending_ = moving_ = false;
}

void Editor::setTool(Tool t) {
  if (stroking_) endStroke();
  tool_ = t;
  tab_ = t <= Tool::Oil ? 0 : t <= Tool::SelectUnits ? 1 : t == Tool::Area ? 4 : 3;
  tabRequest_ = tab_;
}

void Editor::onWorldReplaced() {
  history_.clear();
  stroking_ = false;
  fineHeights_.clear();
  pendingTags_.clear();
  remap_.clear();
  placing_.reset();
  movePending_ = moving_ = false;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
bool Editor::onEvent(const SDL_Event& e, bool uiMouse, bool uiKeys) {
  if (!active_) return false;
  switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (e.button.button == SDL_BUTTON_LEFT && !uiMouse) {
        if (tool_ == Tool::SelectUnits) {
          // Grabbing a selected unit starts a move; anything else is ordinary selection.
          const Entity hit = scene_.pickUnit(scene_.mouse(), true);
          auto& sel = scene_.selection();
          if (hit.valid() && std::find(sel.begin(), sel.end(), hit) != sel.end()) {
            movePending_ = true;
            moving_ = false;
            moveClicked_ = hit;
            moveStartMouse_ = scene_.mouse();
            moveStartGround_ = scene_.groundAt(scene_.mouse());
            return true;
          }
          return false;
        }
        beginStroke();
        return true;
      }
      return false;
    case SDL_EVENT_MOUSE_MOTION:
      if (movePending_ && !moving_ && (scene_.mouse() - moveStartMouse_).length() > 6.0f) moving_ = true;
      return false;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (e.button.button == SDL_BUTTON_LEFT && movePending_) {
        movePending_ = false;
        if (moving_) {
          moving_ = false;
          const Vec2f g = scene_.groundAt(scene_.mouse());
          moveSelection({static_cast<int32_t>(g.x - moveStartGround_.x), static_cast<int32_t>(g.y - moveStartGround_.y)});
        } else {
          auto& sel = scene_.selection();
          if (SDL_GetModState() & SDL_KMOD_SHIFT) std::erase(sel, moveClicked_);
          else sel = {moveClicked_};
        }
        return true;
      }
      if (e.button.button == SDL_BUTTON_LEFT && stroking_) {
        endStroke();
        return true;
      }
      return false;
    case SDL_EVENT_KEY_DOWN: {
      if (uiKeys) return false;
      const bool ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
      const bool shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
      if (ctrl && e.key.key == SDLK_Z) {
        shift ? redo() : undo();
        return true;
      }
      if (ctrl && e.key.key == SDLK_Y) {
        redo();
        return true;
      }
      if (ctrl && e.key.key == SDLK_S) {
        if (scene_.currentMapPath().empty()) showSaveAs_ = true;
        else save(scene_.currentMapPath());
        return true;
      }
      if (e.key.key == SDLK_G && tool_ == Tool::Paint) {
        gridPaint_ = !gridPaint_;
        return true;
      }
      if (e.key.key == SDLK_LEFTBRACKET) {
        radius_ = std::max(1.0f, radius_ - 1.0f);
        return true;
      }
      if (e.key.key == SDLK_RIGHTBRACKET) {
        radius_ = std::min(32.0f, radius_ + 1.0f);
        return true;
      }
      if (e.key.key == SDLK_COMMA || e.key.key == SDLK_PERIOD) {
        const int step = e.key.key == SDLK_PERIOD ? 1 : -1;
        if (tool_ == Tool::PlaceUnits) placeFacing_ = (placeFacing_ + step + 16) & 15;
        else rotateSelection(step);
        return true;
      }
      if (e.key.key == SDLK_DELETE) {
        deleteSelection();
        return true;
      }
      if (ctrl && e.key.key == SDLK_D) {
        duplicateSelection();
        return true;
      }
      if (e.key.key == SDLK_ESCAPE && tool_ == Tool::PlaceUnits) {
        setTool(Tool::SelectUnits);
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

void Editor::update(float dt) {
  if (!active_ || !stroking_) return;
  float mx, my;
  if (!(SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK) && !scene_.app().offscreen()) {
    endStroke();  // button released outside the window
    return;
  }
  strokeAt(scene_.groundAt(scene_.mouse()), dt);
}

// ---------------------------------------------------------------------------
// Strokes
// ---------------------------------------------------------------------------
void Editor::beginStroke() {
  stroking_ = true;
  ++strokeCounter_;
  if (!isTerrainTool()) {
    beginUnitStroke();
    return;
  }
  history_.begin(kToolNames[static_cast<int>(tool_)]);
  const Map& m = scene_.map();
  fineHeights_.assign(m.heights().begin(), m.heights().end());
  const Vec2f c = scene_.groundAt(scene_.mouse());
  flattenTarget_ = m.heightAt(c.x, c.y);
  propAccum_ = 1.0f;  // a single click places one prop
  if (tool_ == Tool::Props || tool_ == Tool::Erase) history_.touchProps(m);
  if (tool_ == Tool::Oil) oilClickDone_ = false;
}

void Editor::endStroke() {
  stroking_ = false;
  if (!isTerrainTool()) {
    endUnitStroke();
    return;
  }
  history_.end();
  scene_.terrainEdited();
}

void Editor::strokeAt(Vec2f c, float dt) {
  if (!stroking_) return;
  switch (tool_) {
    case Tool::Paint: applyPaint(c, dt); break;
    case Tool::Props: applyProps(c, dt); break;
    case Tool::Erase: applyErase(c); break;
    case Tool::Oil: applyOil(c, dt); break;
    case Tool::PlaceUnits: applyUnitBrush(c, dt); break;
    case Tool::SelectUnits: break;
    case Tool::Artillery:
    case Tool::Smite:
    case Tool::Heal: applyGodTool(c, dt); break;
    case Tool::Area:
      areaEnd_ = {static_cast<int32_t>(c.x) >> LEPTON_SHIFT, static_cast<int32_t>(c.y) >> LEPTON_SHIFT};
      break;
    default: applyHeightTool(c, dt); break;
  }
}

void Editor::applyHeightTool(Vec2f c, float dt) {
  Map& m = scene_.map();
  if (fineHeights_.size() != m.heights().size()) fineHeights_.assign(m.heights().begin(), m.heights().end());
  const float cx = c.x / LEPTONS_PER_TILE, cy = c.y / LEPTONS_PER_TILE;
  const int x0 = std::max(0, static_cast<int>(std::floor(cx - radius_)));
  const int y0 = std::max(0, static_cast<int>(std::floor(cy - radius_)));
  const int x1 = std::min(m.width(), static_cast<int>(std::ceil(cx + radius_)));
  const int y1 = std::min(m.height(), static_cast<int>(std::ceil(cy + radius_)));
  if (x0 > x1 || y0 > y1) return;
  history_.touchTerrain(m, x0, y0, x1, y1);

  const int stride = m.cornersX();
  const float rate = strength_ / 100.0f;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
      const float w = falloff(d / radius_, softEdge_);
      if (w <= 0) continue;
      float& h = fineHeights_[static_cast<size_t>(y) * stride + x];
      switch (tool_) {
        case Tool::Raise: h += rate * 260.0f * dt * w; break;
        case Tool::Lower: h -= rate * 260.0f * dt * w; break;
        case Tool::Smooth: {
          auto at = [&](int ax, int ay) {
            ax = std::clamp(ax, 0, m.width());
            ay = std::clamp(ay, 0, m.height());
            return fineHeights_[static_cast<size_t>(ay) * stride + ax];
          };
          const float avg = (at(x - 1, y) + at(x + 1, y) + at(x, y - 1) + at(x, y + 1)) * 0.25f;
          h += (avg - h) * std::min(1.0f, rate * 12.0f * dt * w);
          break;
        }
        case Tool::Flatten: h += (flattenTarget_ - h) * std::min(1.0f, rate * 10.0f * dt * w); break;
        case Tool::Roughen: {
          const float n = (hash2(x, y, strokeCounter_) & 1023) / 1023.0f - 0.5f;
          h += n * rate * 220.0f * dt * w;
          break;
        }
        default: break;
      }
      h = std::clamp(h, 0.0f, 8000.0f);
      m.setCorner(x, y, static_cast<uint16_t>(std::lround(h)));
    }
  afterTerrainEdit(x0 - 1, y0 - 1, x1, y1, true, false);
}

// The square of whole tiles the grid brush covers (inclusive).
static void gridSquare(Vec2f c, float radius, int& x0, int& y0, int& x1, int& y1) {
  const int n = std::max(1, static_cast<int>(std::lround(radius)));
  const int tx = static_cast<int>(std::floor(c.x / LEPTONS_PER_TILE)), ty = static_cast<int>(std::floor(c.y / LEPTONS_PER_TILE));
  x0 = tx - (n - 1) / 2;
  y0 = ty - (n - 1) / 2;
  x1 = x0 + n - 1;
  y1 = y0 + n - 1;
}

void Editor::applyPaint(Vec2f c, float dt) {
  Map& m = scene_.map();
  if (gridPaint_) {
    // Whole tiles, fully: crisp edges along the grid.
    int x0, y0, x1, y1;
    gridSquare(c, radius_, x0, y0, x1, y1);
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(m.width() - 1, x1);
    y1 = std::min(m.height() - 1, y1);
    if (x0 > x1 || y0 > y1) return;
    history_.touchTerrain(m, x0, y0, x1, y1);
    const int amount = std::max(1, static_cast<int>(strength_ / 100.0f * 2600.0f * dt));
    for (int ty = y0; ty <= y1; ++ty)
      for (int tx = x0; tx <= x1; ++tx)
        for (int j = 0; j < SPLAT_PER_TILE; ++j)
          for (int i = 0; i < SPLAT_PER_TILE; ++i) m.paint(tx * SPLAT_PER_TILE + i, ty * SPLAT_PER_TILE + j, paintLayer_, amount);
    afterTerrainEdit(x0, y0, x1, y1, false, true);
    scene_.terrain().markSplatDirty(x0 * SPLAT_PER_TILE, y0 * SPLAT_PER_TILE, (x1 + 1) * SPLAT_PER_TILE - 1, (y1 + 1) * SPLAT_PER_TILE - 1);
    return;
  }
  const float sx = c.x / LEPTONS_PER_TILE * SPLAT_PER_TILE, sy = c.y / LEPTONS_PER_TILE * SPLAT_PER_TILE;
  const float r = radius_ * SPLAT_PER_TILE;
  const int x0 = std::max(0, static_cast<int>(std::floor(sx - r)));
  const int y0 = std::max(0, static_cast<int>(std::floor(sy - r)));
  const int x1 = std::min(m.splatW() - 1, static_cast<int>(std::ceil(sx + r)));
  const int y1 = std::min(m.splatH() - 1, static_cast<int>(std::ceil(sy + r)));
  if (x0 > x1 || y0 > y1) return;
  history_.touchTerrain(m, x0 / SPLAT_PER_TILE, y0 / SPLAT_PER_TILE, x1 / SPLAT_PER_TILE, y1 / SPLAT_PER_TILE);
  const float rate = strength_ / 100.0f * 900.0f * dt;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const float d = std::sqrt((x + 0.5f - sx) * (x + 0.5f - sx) + (y + 0.5f - sy) * (y + 0.5f - sy));
      const float w = falloff(d / r, softEdge_);
      if (w <= 0) continue;
      const float amount = rate * w;
      int whole = static_cast<int>(amount);
      // Carry the fraction randomly, so slow strokes still make progress.
      if ((hash2(x, y, strokeCounter_ * 131u + static_cast<uint32_t>(SDL_GetTicks())) % 1000) / 1000.0f < amount - whole)
        ++whole;
      m.paint(x, y, paintLayer_, whole);
    }
  afterTerrainEdit(x0 / SPLAT_PER_TILE, y0 / SPLAT_PER_TILE, x1 / SPLAT_PER_TILE, y1 / SPLAT_PER_TILE, false, true);
  scene_.terrain().markSplatDirty(x0, y0, x1, y1);
}

void Editor::applyProps(Vec2f c, float dt) {
  Map& m = scene_.map();
  propAccum_ += propDensity_ * dt * std::max(1.0f, radius_ * 0.5f);
  static Rng rng(0xC0FFEE);
  while (propAccum_ >= 1.0f) {
    propAccum_ -= 1.0f;
    // First prop of a click goes exactly under the cursor; the rest scatter.
    float ox = 0, oy = 0;
    if (radius_ > 1.0f) {
      const float a = rng.below(36000) / 36000.0f * 6.2831853f;
      const float d = std::sqrt(rng.below(10000) / 10000.0f) * radius_ * LEPTONS_PER_TILE;
      ox = std::cos(a) * d;
      oy = std::sin(a) * d;
    }
    const int32_t x = static_cast<int32_t>(c.x + ox), y = static_cast<int32_t>(c.y + oy);
    const int tx = x / LEPTONS_PER_TILE, ty = y / LEPTONS_PER_TILE;
    if (!m.inBounds(tx, ty) || m.info(tx, ty).waterDepth > 4) continue;
    m.props().push_back({propKind_, static_cast<uint8_t>(rng.below(8)), x, y});
  }
}

void Editor::applyErase(Vec2f c) {
  Map& m = scene_.map();
  const float r = radius_ * LEPTONS_PER_TILE;
  std::erase_if(m.props(), [&](const Prop& p) {
    const float dx = p.x - c.x, dy = p.y - c.y;
    return dx * dx + dy * dy <= r * r;
  });
}

void Editor::applyOil(Vec2f c, float dt) {
  // One click, one change (not painted).
  if (oilClickDone_) return;
  oilClickDone_ = true;
  (void)dt;
  Map& m = scene_.map();
  const int tx = static_cast<int>(c.x) >> LEPTON_SHIFT, ty = static_cast<int>(c.y) >> LEPTON_SHIFT;
  auto& pts = m.supplyPoints;
  if ((SDL_GetModState() & SDL_KMOD_SHIFT) != 0) {
    if (pts.empty()) return;
    size_t best = 0;
    for (size_t i = 1; i < pts.size(); ++i)
      if (std::abs(pts[i].x - tx) + std::abs(pts[i].y - ty) < std::abs(pts[best].x - tx) + std::abs(pts[best].y - ty)) best = i;
    if (std::abs(pts[best].x - tx) + std::abs(pts[best].y - ty) <= 6) pts.erase(pts.begin() + static_cast<long>(best));
    return;
  }
  if (!m.inBounds(tx, ty) || m.info(tx, ty).waterDepth > -4) return;
  for (const Vec2i& p : pts)
    if (std::abs(p.x - tx) < 5 && std::abs(p.y - ty) < 5) return;  // too close to another
  pts.push_back({tx, ty});
}

void Editor::afterTerrainEdit(int tx0, int ty0, int tx1, int ty1, bool heights, bool splat) {
  Map& m = scene_.map();
  if (heights) {
    m.recomputeInfo(tx0, ty0, tx1, ty1);
    scene_.terrain().markHeightsDirty(tx0, ty0, tx1, ty1);
  }
  if (splat) m.recomputeSpeeds(tx0, ty0, tx1, ty1);  // mud and roads change how fast units go
}

void Editor::applyRegion(const History::Region& r) {
  Map& m = scene_.map();
  if (r.tx1 >= r.tx0) {
    m.recomputeInfo(r.tx0, r.ty0, r.tx1, r.ty1);
    scene_.terrain().markHeightsDirty(r.tx0, r.ty0, r.tx1, r.ty1);
    scene_.terrain().markSplatDirty(r.tx0 * SPLAT_PER_TILE, r.ty0 * SPLAT_PER_TILE,
                                    (r.tx1 + 1) * SPLAT_PER_TILE - 1, (r.ty1 + 1) * SPLAT_PER_TILE - 1);
  }
  if (r.water) {
    m.recomputeInfo();
    scene_.terrain().markWaterDirty();
  }
  fineHeights_.clear();
}

void Editor::undo() {
  if (stroking_ || !history_.canUndo()) return;
  const std::string label = history_.undoLabel();
  applyRegion(history_.undo(scene_.map()));
  scene_.terrainEdited();
  scene_.showStatus("Undo: " + label, 1.5f);
}

void Editor::redo() {
  if (stroking_ || !history_.canRedo()) return;
  const std::string label = history_.redoLabel();
  applyRegion(history_.redo(scene_.map()));
  scene_.terrainEdited();
  scene_.showStatus("Redo: " + label, 1.5f);
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
std::string Editor::sanitize(const std::string& name) const {
  std::string out;
  for (char ch : name) {
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') out += ch;
    else if (ch == ' ') out += '_';
  }
  return out.empty() ? "untitled" : out.substr(0, 60);
}

bool Editor::save(const std::string& path) {
  std::string err;
  if (!saveMap(path, scene_.map(), scene_.exportUnits(), scene_.playerList(), err, scene_.scenarioJson())) {
    lastError_ = err;
    scene_.showStatus("Save failed: " + err, 5);
    return false;
  }
  scene_.setCurrentMapPath(path);
  scene_.showStatus("Saved " + std::filesystem::path(path).filename().string(), 3);
  PNR_INFO("Saved map %s", path.c_str());
  return true;
}

bool Editor::load(const std::string& path) {
  MapFile f;
  std::string err;
  if (!loadMap(path, f, err)) {
    lastError_ = err;
    scene_.showStatus("Could not open map: " + err, 5);
    return false;
  }
  scene_.replaceWorld(std::move(f));
  scene_.setCurrentMapPath(path);
  scene_.showStatus("Opened " + std::filesystem::path(path).filename().string(), 3);
  return true;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void Editor::renderWorld(SpriteRenderer& r) {
  // In 3D this is called twice: in the 3D pass for the ghosts of units being
  // placed, and on the screen for brushes and areas.
  const bool ghostPass = scene_.is3D() ? r.in3D() : true;
  const bool flatPass = !r.in3D();
  if (active_ && flatPass) renderAreas(r);
  if (!active_ || ImGui::GetIO().WantCaptureMouse) return;
  const Vec2f c = scene_.groundAt(scene_.mouse());
  if (ghostPass) renderUnitGhosts(r, c);
  if (!flatPass) return;
  if (tool_ == Tool::Area) return;
  if (tool_ == Tool::SelectUnits) return;
  if (tool_ == Tool::PlaceUnits && placeMode_ != PlaceMode::Brush) return;
  const uint32_t col = toolColor(tool_);
  if (tool_ == Tool::Paint && gridPaint_) {
    // The tiles the grid brush will paint, outlined.
    int x0, y0, x1, y1;
    gridSquare(c, radius_, x0, y0, x1, y1);
    auto edge = [&](float ax, float ay, float bx, float by) {
      const int steps = std::max(2, static_cast<int>(std::hypot(bx - ax, by - ay) / 40.0f));
      for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        const Vec2f p = scene_.groundToScreenWorld(ax + (bx - ax) * t, ay + (by - ay) * t);
        r.rect(std::floor(p.x), std::floor(p.y), 2, 1, col);
      }
    };
    for (int x = x0; x <= x1 + 1; ++x) edge(x * 256.0f, y0 * 256.0f, x * 256.0f, (y1 + 1) * 256.0f);
    for (int y = y0; y <= y1 + 1; ++y) edge(x0 * 256.0f, y * 256.0f, (x1 + 1) * 256.0f, y * 256.0f);
    return;
  }
  const float rl = radius_ * LEPTONS_PER_TILE;
  const int n = std::clamp(static_cast<int>(radius_ * 14), 32, 220);
  for (int i = 0; i < n; ++i) {
    const float a = i * 6.2831853f / n;
    const Vec2f p = scene_.groundToScreenWorld(c.x + std::cos(a) * rl, c.y + std::sin(a) * rl);
    r.rect(std::floor(p.x) - 1, std::floor(p.y) - 1, 2, 2, col);
  }
  const Vec2f m = scene_.groundToScreenWorld(c.x, c.y);
  r.rect(std::floor(m.x) - 3, std::floor(m.y), 7, 1, col);
  r.rect(std::floor(m.x), std::floor(m.y) - 3, 1, 7, col);
}

void Editor::imgui() {
  if (!active_) return;
  drawToolPanel();
  drawAreaLabels();
  drawNewMapDialog();
  drawOpenDialog();
  drawSaveAsDialog();
}

void Editor::drawToolPanel() {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(8, 8));
  ImGui::SetNextWindowSize(ImVec2(panelWidth(), io.DisplaySize.y - 16));
  ImGui::SetNextWindowBgAlpha(0.92f);
  ImGui::Begin("Live Scenario Editor (F2)", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                   ImGuiWindowFlags_NoSavedSettings);
  Map& m = scene_.map();

  // --- File ---------------------------------------------------------------
  if (ImGui::Button("New...")) showNew_ = true;
  ImGui::SameLine();
  if (ImGui::Button("Open...")) {
    mapFiles_.clear();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(scene_.mapsDir(), ec))
      if (entry.path().extension() == ".lmap") mapFiles_.push_back(entry.path().filename().string());
    std::sort(mapFiles_.begin(), mapFiles_.end());
    showOpen_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Save")) {
    if (scene_.currentMapPath().empty()) showSaveAs_ = true;
    else save(scene_.currentMapPath());
  }
  ImGui::SameLine();
  if (ImGui::Button("Save as...")) {
    std::snprintf(nameBuf_, sizeof nameBuf_, "%s", m.name.c_str());
    showSaveAs_ = true;
  }
  ImGui::TextDisabled("%s  -  %dx%d tiles, %zu props, %zu units", m.name.c_str(), m.width(), m.height(),
                      m.props().size(), scene_.sim().unitCount());

  // --- Tabs ---------------------------------------------------------------
  if (ImGui::BeginTabBar("##tabs")) {
    const char* names[] = {"Terrain", "Units", "Sides", "Battle", "Script"};
    for (int i = 0; i < 5; ++i) {
      const ImGuiTabItemFlags flags = (tabRequest_ == i) ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem(names[i], nullptr, flags)) {
        if (tab_ != i && tabRequest_ < 0) {
          // Switching tabs picks that tab's main tool.
          tab_ = i;
          if (i == 0 && !isTerrainTool()) tool_ = Tool::Raise;
          if (i == 1 && tool_ != Tool::PlaceUnits && tool_ != Tool::SelectUnits) tool_ = Tool::SelectUnits;
          if (i == 3 && (tool_ < Tool::Artillery || tool_ == Tool::Area)) tool_ = Tool::SelectUnits;
          if (i == 4 && tool_ != Tool::Area) tool_ = Tool::SelectUnits;
        }
        switch (i) {
          case 0: drawTerrainTab(); break;
          case 1: drawUnitsTab(); break;
          case 2: drawSidesTab(); break;
          case 3: drawBattleTab(); break;
          default: drawScriptTab(); break;
        }
        ImGui::EndTabItem();
      }
    }
    tabRequest_ = -1;
    ImGui::EndTabBar();
  }

  // --- History (shared by every tab) ----------------------------------------
  ImGui::SeparatorText("History");
  ImGui::BeginDisabled(!history_.canUndo());
  if (ImGui::Button("Undo")) undo();
  ImGui::EndDisabled();
  if (history_.canUndo()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", history_.undoLabel().c_str());
  }
  ImGui::BeginDisabled(!history_.canRedo());
  if (ImGui::Button("Redo")) redo();
  ImGui::EndDisabled();
  if (history_.canRedo()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", history_.redoLabel().c_str());
  }
  ImGui::End();
}

void Editor::drawTerrainTab() {
  const ImGuiIO& io = ImGui::GetIO();
  Map& m = scene_.map();
  ImGui::SeparatorText("Tool");
  const float bw = (panelWidth() - 26) * 0.5f;
  for (int i = 0; i <= static_cast<int>(Tool::Oil); ++i) {
    const bool sel = static_cast<int>(tool_) == i;
    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.50f, 0.22f, 1));
    if (ImGui::Button(kToolNames[i], ImVec2(bw, 0))) tool_ = static_cast<Tool>(i);
    if (sel) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kToolTips[i]);
    if (i % 2 == 0) ImGui::SameLine();
  }

  ImGui::SeparatorText("Brush");
  ImGui::SetNextItemWidth(-60);
  ImGui::SliderFloat("Size", &radius_, 1.0f, 32.0f, "%.0f tiles");
  ImGui::SetNextItemWidth(-60);
  ImGui::SliderFloat("Strength", &strength_, 1.0f, 100.0f, "%.0f");
  ImGui::Checkbox("Soft edge", &softEdge_);
  ImGui::SameLine();
  ImGui::TextDisabled("[ ] resize");

  if (tool_ == Tool::Paint) {
    ImGui::Checkbox("Grid: whole tiles, size x size square", &gridPaint_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paints whole tiles snapped to the grid, for roads, fields and exact edges (G).");
    ImGui::SeparatorText("Ground");
    for (int i = 0; i < LAYER_COUNT; ++i) {
      ImGui::ColorButton("##sw", kLayerSwatch[i], ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18));
      ImGui::SameLine();
      if (ImGui::RadioButton(layerName(static_cast<Layer>(i)), static_cast<int>(paintLayer_) == i))
        paintLayer_ = static_cast<Layer>(i);
    }
  }
  if (tool_ == Tool::Props) {
    ImGui::SeparatorText("Prop");
    for (int i = 0; i < static_cast<int>(PropKind::Count); ++i)
      if (ImGui::RadioButton(propKindName(static_cast<PropKind>(i)), static_cast<int>(propKind_) == i))
        propKind_ = static_cast<PropKind>(i);
    ImGui::SetNextItemWidth(-60);
    ImGui::SliderFloat("Density", &propDensity_, 1.0f, 80.0f, "%.0f");
  }
  if (tool_ == Tool::Props || (tool_ == Tool::PlaceUnits && placeType_ >= 0 && scene_.types().get(placeType_).faction == "neutral")) {
    // Civilian buildings are scenery too: they belong to nobody.
    ImGui::SeparatorText("Civilian buildings");
    const auto& types = scene_.types();
    for (int i = 0; i < static_cast<int>(types.size()); ++i) {
      const UnitTypeDef& d = types.get(i);
      if (d.faction != "neutral") continue;
      const bool on = tool_ == Tool::PlaceUnits && placeType_ == i;
      if (ImGui::RadioButton((d.name + "##civ" + d.id).c_str(), on)) {
        placeType_ = i;
        placeMode_ = PlaceMode::Single;
        tool_ = Tool::PlaceUnits;
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%d x %d tiles%s", d.footW, d.footH,
                          d.garrison ? (", holds " + std::to_string(d.garrison) + " infantry").c_str() : "");
    }
    if (tool_ == Tool::PlaceUnits) ImGui::TextDisabled("Click to place; , and . turn it. Pick a tool above to stop.");
  }

  // --- Water ----------------------------------------------------------------
  ImGui::SeparatorText("Water");
  int level = m.waterLevel();
  ImGui::SetNextItemWidth(-60);
  const bool changed = ImGui::SliderInt("Level", &level, 0, 1500, level == 0 ? "no water" : "%d");
  if (ImGui::IsItemActivated()) {
    history_.begin("Water level");
    history_.touchWater(m);
  }
  if (changed) {
    m.setWaterLevel(static_cast<uint16_t>(level));
    m.recomputeInfo();
    scene_.terrain().markWaterDirty();
  }
  if (ImGui::IsItemDeactivated()) {
    history_.end();
    scene_.terrainEdited();
  }
  if (ImGui::Button("Water to screen-centre height")) {
    const Vec2f c = scene_.groundAt({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f});
    history_.begin("Water level");
    history_.touchWater(m);
    m.setWaterLevel(static_cast<uint16_t>(m.heightAt(c.x, c.y)));
    history_.end();
    scene_.terrainEdited();
    m.recomputeInfo();
    scene_.terrain().markWaterDirty();
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sets the water level to the ground height at the screen centre.");
}

void Editor::drawNewMapDialog() {
  if (showNew_) {
    ImGui::OpenPopup("New map");
    showNew_ = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("New map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  static const char* kSizeNames[] = {"64 x 64 (skirmish)", "128 x 128", "256 x 256", "512 x 512 (huge)",
                                     "1024 x 1024 (experimental)"};
  ImGui::BeginDisabled(newCustom_);
  ImGui::Combo("Size", &newSizeIndex_, kSizeNames, 5);
  ImGui::EndDisabled();
  ImGui::Checkbox("Width and height of my own", &newCustom_);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Long maps for missions that move along a road, a coast or a valley. Sizes go in steps of 32 tiles.");
  if (newCustom_) {
    ImGui::SetNextItemWidth(110);
    ImGui::InputInt("Width", &newW_, 32, 64);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::InputInt("Height", &newH_, 32, 64);
    newW_ = Map::roundSize(newW_);
    newH_ = Map::roundSize(newH_);
  }
  ImGui::Combo("Terrain", &newTerrain_, "Flat grass\0Random landscape\0");
  if (newTerrain_ == 1) {
    if (ImGui::BeginCombo("Biome", biomeName(static_cast<Biome>(newBiome_)))) {
      for (int b = 0; b < static_cast<int>(Biome::Count); ++b)
        if (ImGui::Selectable(biomeName(static_cast<Biome>(b)), b == newBiome_)) newBiome_ = b;
      ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("Landscape", newLayout_ < 0 ? "Random (from the seed)" : mapLayoutName(static_cast<MapLayout>(newLayout_)))) {
      if (ImGui::Selectable("Random (from the seed)", newLayout_ < 0)) newLayout_ = -1;
      for (int l = 0; l < static_cast<int>(MapLayout::Count); ++l)
        if (ImGui::Selectable(mapLayoutName(static_cast<MapLayout>(l)), l == newLayout_)) newLayout_ = l;
      ImGui::EndCombo();
    }
    ImGui::InputInt("Seed", &newSeed_);
    ImGui::Checkbox("Villages and towns", &newTowns_);
  }
  ImGui::TextDisabled("Unsaved changes to the current map will be lost.");
  if (ImGui::Button("Create", ImVec2(120, 0))) {
    MapFile f;
    const int size = kNewSizes[newSizeIndex_];
    if (newCustom_) f.map.resize(newW_, newH_);
    else f.map.resize(size, size);
    if (newTerrain_ == 1) {
      f.map.generate(static_cast<Biome>(newBiome_), static_cast<uint64_t>(newSeed_), newLayout_, newTowns_);
      f.map.name = std::string(mapLayoutName(f.map.layout)) + " " + std::to_string(newSeed_);
      if (newTowns_) scene_.addTownBuildings(f);
    } else {
      f.map.name = "New map";
    }
    scene_.replaceWorld(std::move(f));
    scene_.setCurrentMapPath("");
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void Editor::drawOpenDialog() {
  if (showOpen_) {
    ImGui::OpenPopup("Open map");
    showOpen_ = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Open map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::TextDisabled("Maps folder: %s", scene_.mapsDir().c_str());
  static int selected = -1;
  if (ImGui::BeginListBox("##maps", ImVec2(420, 220))) {
    for (int i = 0; i < static_cast<int>(mapFiles_.size()); ++i)
      if (ImGui::Selectable(mapFiles_[static_cast<size_t>(i)].c_str(), selected == i,
                            ImGuiSelectableFlags_AllowDoubleClick)) {
        selected = i;
        if (ImGui::IsMouseDoubleClicked(0) && load(scene_.mapsDir() + mapFiles_[static_cast<size_t>(i)]))
          ImGui::CloseCurrentPopup();
      }
    ImGui::EndListBox();
  }
  if (mapFiles_.empty()) ImGui::TextDisabled("No maps yet. Use Save as... to create one.");
  ImGui::BeginDisabled(selected < 0 || selected >= static_cast<int>(mapFiles_.size()));
  if (ImGui::Button("Open", ImVec2(120, 0)) && load(scene_.mapsDir() + mapFiles_[static_cast<size_t>(selected)]))
    ImGui::CloseCurrentPopup();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void Editor::drawSaveAsDialog() {
  if (showSaveAs_) {
    if (!nameBuf_[0]) std::snprintf(nameBuf_, sizeof nameBuf_, "%s", scene_.map().name.c_str());
    ImGui::OpenPopup("Save map as");
    showSaveAs_ = false;
  }
  ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Save map as", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::SetNextItemWidth(320);
  ImGui::InputText("File name", nameBuf_, sizeof nameBuf_);
  const std::string file = sanitize(nameBuf_) + ".lmap";
  ImGui::TextDisabled("Saves to %s%s", scene_.mapsDir().c_str(), file.c_str());
  const bool exists = std::filesystem::exists(scene_.mapsDir() + file);
  if (exists) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "A map with this name exists and will be replaced.");
  if (ImGui::Button("Save", ImVec2(120, 0))) {
    if (save(scene_.mapsDir() + file)) ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

}  // namespace legion

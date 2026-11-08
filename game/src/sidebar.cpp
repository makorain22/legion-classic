#include "sidebar.hpp"
#include "ui_style.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game_scene.hpp"
#include "slave/math/iso.hpp"

using namespace slave;

namespace legion {

namespace {
// Rough ground colours for the minimap, by dominant layer.
const uint32_t kLayerColor[LAYER_COUNT] = {
    rgba8(78, 92, 48),  rgba8(124, 116, 76), rgba8(108, 90, 66),  rgba8(64, 54, 42),
    rgba8(150, 138, 108), rgba8(104, 102, 96), rgba8(212, 216, 222), rgba8(136, 122, 96),
};
const char* kTabNames[4] = {"Build", "Defend", "Train", "Vehicles"};
const char* kTabIds[4] = {"build", "defend", "train", "vehicles"};
const char* kStanceNames[3] = {"Aggressive", "Hold", "Hold fire"};
const char* kStanceTips[3] = {
    "Fight back when shot, even on the march, and go after attackers out of reach.",
    "Hold ground: return fire from where they stand. Never move on their own.",
    "Don't shoot unless ordered to attack. For ambushes.",
};
const char* kFormationNames[5] = {"Box", "Line", "Col.", "Wedge", "Loose"};
const char* kFormationTips[5] = {
    "A compact block (default).",
    "A broad front, three ranks deep. Right-drag to set its width and facing.",
    "Two abreast: for roads, bridges and gaps.",
    "An arrowhead, point forward.",
    "Spread out: safer against artillery.",
};

uint32_t shade(uint32_t c, float k) {
  auto ch = [&](int s) { return static_cast<uint32_t>(std::clamp(((c >> s) & 0xFF) * k, 0.0f, 255.0f)) << s; };
  return ch(0) | ch(8) | ch(16) | 0xFF000000u;
}

// Draws a unit's sprite (body and turret) scaled into a box.
void drawIcon(ImDrawList* dl, const SpriteRenderer& r, const UnitArt& art, ImVec2 p0, ImVec2 size, ImU32 tint) {
  const ImTextureRef tex(static_cast<ImTextureID>(r.atlasTexture()));
  if (art.icon >= 0) {
    // A painted picture (art pack): fill the box.
    const AtlasFrame& ic = r.frame(art.icon);
    const float k = std::min(size.x / ic.w, size.y / ic.h);
    const ImVec2 o(p0.x + (size.x - ic.w * k) * 0.5f, p0.y + (size.y - ic.h * k) * 0.5f);
    dl->AddImage(tex, o, ImVec2(o.x + ic.w * k, o.y + ic.h * k), ImVec2(ic.u0, ic.v0), ImVec2(ic.u1, ic.v1), tint);
    return;
  }
  const AtlasFrame& body = r.frame(art.body.frame(14, 0));
  const float scale = std::min({size.x / body.w, size.y / body.h, 2.0f}) * 0.92f;
  const ImVec2 o(p0.x + (size.x - body.w * scale) * 0.5f, p0.y + (size.y - body.h * scale) * 0.5f);
  dl->AddImage(tex, o, ImVec2(o.x + body.w * scale, o.y + body.h * scale), ImVec2(body.u0, body.v0), ImVec2(body.u1, body.v1),
               tint);
  if (art.hasTurret) {
    const AtlasFrame& t = r.frame(art.turret.frame(14, 0));
    const float a = static_cast<float>(angleOfFacing(14)) * (6.2831853f / ANGLE_FULL);
    const Vec2f off = iso::worldToScreen(std::cos(a) * art.turretOffset, std::sin(a) * art.turretOffset);
    const ImVec2 ta(o.x + (body.anchorX + off.x - t.anchorX) * scale, o.y + (body.anchorY + off.y - t.anchorY) * scale);
    dl->AddImage(tex, ta, ImVec2(ta.x + t.w * scale, ta.y + t.h * scale), ImVec2(t.u0, t.v0), ImVec2(t.u1, t.v1), tint);
  }
}
}  // namespace

Sidebar::Sidebar(GameScene& scene) : scene_(scene) {}

void Sidebar::cancel() {
  placeType_ = -1;
  dragging_ = false;
  sellMode_ = false;
}

void Sidebar::startPlacing(int type) {
  placeType_ = type;
  sellMode_ = false;
  dragging_ = false;
}

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------
Vec2i Sidebar::snappedCentre(int type, Vec2f g) const {
  const UnitTypeDef& d = scene_.types().get(type);
  const Vec2i o = scene_.sim().footprintOrigin(type, {static_cast<int32_t>(g.x), static_cast<int32_t>(g.y)});
  return {o.x * 256 + d.footW * 128, o.y * 256 + d.footH * 128};
}

bool Sidebar::validAt(int type, Vec2i centre) const {
  const Simulation& sim = scene_.sim();
  const Vec2i o = sim.footprintOrigin(type, centre);
  return sim.canPlace(type, o) && sim.inBuildRange(scene_.player(), type, o);
}

std::vector<Vec2i> Sidebar::lineTiles(Vec2i a, Vec2i b) const {
  // Straight lines along X, Y or a diagonal, whichever is closest to the drag.
  std::vector<Vec2i> out;
  int dx = b.x - a.x, dy = b.y - a.y;
  const int n = std::max(std::abs(dx), std::abs(dy));
  if (std::abs(dx) > 2 * std::abs(dy)) dy = 0;
  else if (std::abs(dy) > 2 * std::abs(dx)) dx = 0;
  else {
    const int m = std::max(std::abs(dx), std::abs(dy));
    dx = dx < 0 ? -m : m;
    dy = dy < 0 ? -m : m;
  }
  for (int i = 0; i <= n && i < 64; ++i) {
    const int x = a.x + (n ? dx * i / n : 0), y = a.y + (n ? dy * i / n : 0);
    if (out.empty() || out.back() != Vec2i{x, y}) out.push_back({x, y});
  }
  return out;
}

bool Sidebar::placeAt(Vec2i g) {
  if (placeType_ < 0) return false;
  const UnitTypeDef& d = scene_.types().get(placeType_);
  const Vec2i c = snappedCentre(placeType_, {static_cast<float>(g.x), static_cast<float>(g.y)});
  if (!validAt(placeType_, c)) {
    scene_.showStatus("Can't build there", 2);
    return false;
  }
  Command cmd;
  cmd.type = d.category == UnitCategory::Fortification ? CommandType::PlaceWalls : CommandType::PlaceBuilding;
  cmd.player = scene_.player();
  cmd.unitType = static_cast<uint16_t>(placeType_);
  cmd.target = c;
  if (cmd.type == CommandType::PlaceWalls) cmd.spawns = {{0, 0, c, 0, -1}};
  scene_.sim().queue(std::move(cmd));
  if (d.category != UnitCategory::Fortification) placeType_ = -1;
  return true;
}

bool Sidebar::onEvent(const SDL_Event& e, bool uiMouse) {
  if (!busy()) return false;
  const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, up = e.type == SDL_EVENT_MOUSE_BUTTON_UP;
  if ((down || up) && uiMouse) return false;
  if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
    cancel();
    return true;
  }
  if (down && e.button.button == SDL_BUTTON_RIGHT) {
    cancel();
    return true;
  }
  const Vec2f g = scene_.groundAt(scene_.mouse());
  if (sellMode_) {
    if (down && e.button.button == SDL_BUTTON_LEFT) {
      const Entity hit = scene_.pickUnit(scene_.mouse(), false);
      Registry& reg = scene_.sim().registry();
      if (hit.valid() && reg.has<CStructure>(hit) && reg.get<CUnit>(hit).owner == scene_.player()) {
        Command s;
        s.type = CommandType::Sell;
        s.units = {hit};
        scene_.sim().queue(std::move(s));
      }
      return true;
    }
    return false;
  }
  const UnitTypeDef& d = scene_.types().get(placeType_);
  if (d.category == UnitCategory::Fortification) {
    // Drag out a line of fortifications; each tile is bought as it is placed.
    if (down && e.button.button == SDL_BUTTON_LEFT) {
      dragging_ = true;
      dragFrom_ = {static_cast<int32_t>(g.x) >> LEPTON_SHIFT, static_cast<int32_t>(g.y) >> LEPTON_SHIFT};
      return true;
    }
    if (up && e.button.button == SDL_BUTTON_LEFT && dragging_) {
      dragging_ = false;
      const Vec2i to{static_cast<int32_t>(g.x) >> LEPTON_SHIFT, static_cast<int32_t>(g.y) >> LEPTON_SHIFT};
      Command cmd;
      cmd.type = CommandType::PlaceWalls;
      cmd.player = scene_.player();
      cmd.unitType = static_cast<uint16_t>(placeType_);
      for (const Vec2i& t : lineTiles(dragFrom_, to)) cmd.spawns.push_back({0, 0, {t.x * 256 + 128, t.y * 256 + 128}, 0, -1});
      scene_.sim().queue(std::move(cmd));
      return true;
    }
    return down || up;
  }
  if (down && e.button.button == SDL_BUTTON_LEFT) {
    placeAt({static_cast<int32_t>(g.x), static_cast<int32_t>(g.y)});
    return true;
  }
  return false;
}

void Sidebar::renderWorld(SpriteRenderer& r) {
  if (placeType_ < 0 || ImGui::GetIO().WantCaptureMouse) return;
  if (scene_.is3D() != r.in3D()) return;  // 3D: laid on the ground in the 3D pass
  const bool threeD = r.in3D();
  auto ground = [&](float gx, float gy) {
    gx = std::clamp(gx, 0.0f, scene_.map().widthLeptons() - 1.0f);
    gy = std::clamp(gy, 0.0f, scene_.map().heightLeptons() - 1.0f);
    return scene_.map().heightAt(gx, gy) / HEIGHT_UNITS_PER_TILE + 0.03f;
  };
  const UnitTypeDef& d = scene_.types().get(placeType_);
  const Vec2f g = scene_.groundAt(scene_.mouse());
  std::vector<Vec2i> centres;
  if (d.category == UnitCategory::Fortification && dragging_) {
    const Vec2i to{static_cast<int32_t>(g.x) >> LEPTON_SHIFT, static_cast<int32_t>(g.y) >> LEPTON_SHIFT};
    for (const Vec2i& t : lineTiles(dragFrom_, to)) centres.push_back({t.x * 256 + 128, t.y * 256 + 128});
  } else {
    centres.push_back(snappedCentre(placeType_, g));
  }
  const UnitArt& art = scene_.unitArt(placeType_);
  int32_t cost = 0;
  for (const Vec2i& c : centres) {
    const bool ok = validAt(placeType_, c);
    cost += d.cost;
    // Footprint tiles, green or red.
    const Vec2i o = scene_.sim().footprintOrigin(placeType_, c);
    for (int y = o.y; y < o.y + d.footH; ++y)
      for (int x = o.x; x < o.x + d.footW; ++x) {
        const bool tileOk = scene_.map().buildable(x, y);
        const uint32_t tileTint = (ok && tileOk) ? rgba8(110, 230, 90, 170) : rgba8(240, 70, 50, 190);
        if (threeD) {
          r.decal(scene_.art().moveMarker, x * 256.0f + 128, y * 256.0f + 128, tileTint, kWhite, 2, ground);
          continue;
        }
        const Vec2f s = scene_.groundToScreenWorld(x * 256.0f + 128, y * 256.0f + 128);
        r.sprite(scene_.art().moveMarker, s.x, s.y, kWhite, tileTint);
      }
    if (threeD) {
      scene_.ghost(art.body.frame(0, 0), static_cast<float>(c.x), static_cast<float>(c.y), scene_.teamColor(scene_.player()),
                   ok ? rgba8(255, 255, 255, 150) : rgba8(255, 120, 100, 120));
      if (art.hasTurret)
        scene_.ghost(art.turret.frame(4, 0), static_cast<float>(c.x), static_cast<float>(c.y), scene_.teamColor(scene_.player()),
                     rgba8(255, 255, 255, 150));
      continue;
    }
    const Vec2f s = scene_.groundToScreenWorld(static_cast<float>(c.x), static_cast<float>(c.y));
    r.sprite(art.body.frame(0, 0), s.x, s.y, scene_.teamColor(scene_.player()),
             ok ? rgba8(255, 255, 255, 150) : rgba8(255, 120, 100, 120));
    if (art.hasTurret) r.sprite(art.turret.frame(4, 0), s.x, s.y, scene_.teamColor(scene_.player()), rgba8(255, 255, 255, 150));
  }
  (void)cost;
}

// ---------------------------------------------------------------------------
// Minimap
// ---------------------------------------------------------------------------
void Sidebar::rebuildMinimap() {
  const Map& m = scene_.map();
  const int W = m.width(), H = m.height();
  // The map is a diamond on screen; so is the minimap.
  const int mw = minimapW_, mh = minimapH_;
  if (static_cast<int>(terrainColors_.size()) != mw * mh || terrainRevision_ != m.revision()) {
    terrainColors_.assign(static_cast<size_t>(mw) * mh, 0xFF000000u);
    for (int py = 0; py < mh; ++py)
      for (int px = 0; px < mw; ++px) {
        const float sx = (px + 0.5f) / mw * (W + H) - H, sy = (py + 0.5f) / mh * (W + H);
        const int tx = static_cast<int>(std::floor((sx + sy) * 0.5f)), ty = static_cast<int>(std::floor((sy - sx) * 0.5f));
        if (!m.inBounds(tx, ty)) continue;
        const TileInfo& t = m.info(tx, ty);
        uint32_t c;
        if (t.waterDepth > 0) c = t.waterDepth > 20 ? rgba8(28, 44, 50) : rgba8(54, 70, 62);
        else {
          c = kLayerColor[static_cast<int>(m.dominantLayer(tx * SPLAT_PER_TILE, ty * SPLAT_PER_TILE))];
          // Light from the upper left, like the terrain.
          const int dh = m.corner(tx, ty) - m.corner(tx + 1, ty + 1);
          c = shade(c, std::clamp(1.0f + dh * 0.01f, 0.7f, 1.3f));
          if (m.oil(tx, ty)) c = shade(c, 0.35f);
        }
        terrainColors_[static_cast<size_t>(py) * mw + px] = c | 0xFF000000u;
      }
    terrainRevision_ = m.revision();
  }
  minimapPixels_ = terrainColors_;
  // Fog: what the commanded side hasn't seen is black, what it can't see now is dim.
  const bool fog = scene_.fogEnabled();
  auto toPixel = [&](int32_t lx, int32_t ly, int& px, int& py) {
    const float tx = lx / 256.0f, ty = ly / 256.0f;
    px = static_cast<int>((tx - ty + H) / (W + H) * mw);
    py = static_cast<int>((tx + ty) / (W + H) * mh);
    return px >= 0 && py >= 0 && px < mw && py < mh;
  };
  if (fog) {
    for (int py = 0; py < mh; ++py)
      for (int px = 0; px < mw; ++px) {
        const float sx = (px + 0.5f) / mw * (W + H) - H, sy = (py + 0.5f) / mh * (W + H);
        const int tx = static_cast<int>(std::floor((sx + sy) * 0.5f)), ty = static_cast<int>(std::floor((sy - sx) * 0.5f));
        if (!m.inBounds(tx, ty)) continue;
        const uint8_t f = scene_.terrain().fogAt(tx, ty);
        uint32_t& c = minimapPixels_[static_cast<size_t>(py) * mw + px];
        if (f == 0) c = 0xFF000000u;
        else if (f < 200) c = shade(c, 0.45f + 0.3f * f / 200.0f);
      }
  }
  // Units and structures.
  Registry& reg = scene_.sim().registry();
  const uint8_t me = scene_.player();
  auto& units = reg.pool<CUnit>();
  for (size_t i = 0; i < units.size(); ++i) {
    const uint32_t e = units.entities()[i];
    const CUnit& u = units.data()[i];
    const CPosition& p = reg.pool<CPosition>().get(e);
    const bool mine = scene_.sim().allied(u.owner, me);
    if (fog && !mine && !scene_.sim().vision().visible(me, p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT)) continue;
    const UnitTypeDef& d = scene_.types().get(u.type);
    if (d.category == UnitCategory::Fortification) continue;
    int px, py;
    if (!toPixel(p.x, p.y, px, py)) continue;
    const uint32_t c = scene_.teamColor(u.owner) | 0xFF000000u;
    const int r = d.isStructure() ? std::max(1, d.footW * mw / (W + H)) : miniZoom_ / 2;
    for (int y = py - r; y <= py + r; ++y)
      for (int x = px - r; x <= px + r; ++x)
        if (x >= 0 && y >= 0 && x < mw && y < mh) minimapPixels_[static_cast<size_t>(y) * mw + x] = d.isStructure() ? shade(c, 1.2f) : c;
  }
  // Supply points: gold, or the colour of the side whose depot holds them.
  for (size_t i = 0; i < m.supplyPoints.size(); ++i) {
    int px, py;
    if (!toPixel(m.supplyPoints[i].x * 256 + 128, m.supplyPoints[i].y * 256 + 128, px, py)) continue;
    const Entity depot = scene_.sim().depotOn(static_cast<int>(i));
    const uint32_t c = depot.valid() ? scene_.teamColor(reg.get<CUnit>(depot).owner) | 0xFF000000u : rgba8(236, 206, 96);
    const int r = std::max(1, miniZoom_);
    for (int y = py - r; y <= py + r; ++y)
      for (int x = px - r; x <= px + r; ++x)
        if (x >= 0 && y >= 0 && x < mw && y < mh && (std::abs(x - px) == r || std::abs(y - py) == r))
          minimapPixels_[static_cast<size_t>(y) * mw + x] = c;
  }
  if (minimap_.width() != mw || minimap_.height() != mh) minimap_.allocate(mw, mh, {true, false, false});
  minimap_.update(0, 0, mw, mh, minimapPixels_.data());
}

void Sidebar::drawMinimap(float width) {
  const Map& m = scene_.map();
  const int W = m.width(), H = m.height();
  const int mw = static_cast<int>(width), mh = static_cast<int>(width * 0.5f);
  // The texture has more pixels when zoomed in, so units stay sharp.
  const int tw = mw * miniZoom_, th = mh * miniZoom_;
  if (tw != minimapW_ || th != minimapH_) {
    minimapW_ = tw;
    minimapH_ = th;
    terrainColors_.clear();
    minimapTimer_ = 0;
  }
  if (minimapTimer_ <= 0) {
    rebuildMinimap();
    minimapTimer_ = 0.25f;
  }
  // Zoomed in, the minimap shows the part of the map around the camera.
  const float span = 1.0f / static_cast<float>(miniZoom_);
  auto norm = [&](Vec2f l) {
    const float tx = l.x / 256.0f, ty = l.y / 256.0f;
    return ImVec2((tx - ty + H) / (W + H), (tx + ty) / (W + H));
  };
  const float vw = static_cast<float>(scene_.app().pixelWidth()) - kWidth, vh = static_cast<float>(scene_.app().pixelHeight());
  const ImVec2 camN = norm(scene_.groundAt({vw * 0.5f, vh * 0.5f}));
  const float u0 = std::clamp(camN.x - span * 0.5f, 0.0f, 1.0f - span), v0 = std::clamp(camN.y - span * 0.5f, 0.0f, 1.0f - span);

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + mw, p0.y + mh), IM_COL32(10, 10, 8, 255));
  dl->AddImage(ImTextureRef(static_cast<ImTextureID>(minimap_.id())), p0, ImVec2(p0.x + mw, p0.y + mh), ImVec2(u0, v0),
               ImVec2(u0 + span, v0 + span));
  auto toMini = [&](Vec2f l) {
    const ImVec2 n = norm(l);
    return ImVec2(p0.x + (n.x - u0) / span * mw, p0.y + (n.y - v0) / span * mh);
  };
  const ImVec2 a = toMini(scene_.groundAt({0, 0})), b = toMini(scene_.groundAt({vw, 0}));
  const ImVec2 c = toMini(scene_.groundAt({vw, vh})), d = toMini(scene_.groundAt({0, vh}));
  dl->PushClipRect(p0, ImVec2(p0.x + mw, p0.y + mh), true);
  dl->AddQuad(a, b, c, d, IM_COL32(240, 230, 190, 220), 1.0f);
  dl->PopClipRect();
  dl->AddRect(ImVec2(p0.x - 1, p0.y - 1), ImVec2(p0.x + mw + 1, p0.y + mh + 1), IM_COL32(110, 100, 70, 255));

  ImGui::InvisibleButton("##minimap", ImVec2(static_cast<float>(mw), static_cast<float>(mh)),
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  ui::glowIfHighlighted("minimap");
  const bool hovered = ImGui::IsItemHovered();
  auto groundUnderMouse = [&]() {
    const ImVec2 mp = ImGui::GetIO().MousePos;
    const float nu = u0 + (mp.x - p0.x) / mw * span, nv = v0 + (mp.y - p0.y) / mh * span;
    const float sx = nu * (W + H) - H, sy = nv * (W + H);
    return Vec2i{static_cast<int32_t>((sx + sy) * 0.5f * 256), static_cast<int32_t>((sy - sx) * 0.5f * 256)};
  };
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) scene_.centerOn(groundUnderMouse());
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
    // Right-click on the minimap: send the selection there.
    Vec2i g = groundUnderMouse();
    g.x = std::clamp(g.x, 0, m.widthLeptons() - 1);
    g.y = std::clamp(g.y, 0, m.heightLeptons() - 1);
    scene_.orderMove(g, CommandType::Move);
  }
  if (hovered && ImGui::GetIO().MouseWheel != 0)
    miniZoom_ = std::clamp(ImGui::GetIO().MouseWheel > 0 ? miniZoom_ * 2 : miniZoom_ / 2, 1, 8);
  if (hovered) ImGui::SetTooltip("Click or drag: look there. Right-click: send the selection there. Wheel or +/-: zoom.");
  // Zoom buttons in the top-right corner of the minimap.
  const ImVec2 after = ImGui::GetCursorScreenPos();
  ImGui::SetCursorScreenPos(ImVec2(p0.x + mw - 46, p0.y + 2));
  if (ImGui::SmallButton("-##mz")) miniZoom_ = std::max(1, miniZoom_ / 2);
  ImGui::SameLine(0, 2);
  if (ImGui::SmallButton("+##mz")) miniZoom_ = std::min(8, miniZoom_ * 2);
  ImGui::SetCursorScreenPos(ImVec2(p0.x + 3, p0.y + 2));
  ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.6f, 0.8f), "%dx", miniZoom_);
  ImGui::SetCursorScreenPos(after);
}

// ---------------------------------------------------------------------------
// Production
// ---------------------------------------------------------------------------
void Sidebar::drawProduction(float width) {
  Simulation& sim = scene_.sim();
  const uint8_t me = scene_.player();
  const PlayerState& ps = sim.playerState(me);
  const std::string& faction = scene_.players()[me].faction;
  // Tabs: big plates with a pictogram each.
  const float tw = (width - 6) / 4.0f;
  const ui::Picto pictos[4] = {ui::Picto::Building, ui::Picto::Defence, ui::Picto::Infantry, ui::Picto::Vehicle};
  for (int i = 0; i < 4; ++i) {
    if (i) ImGui::SameLine(0, 2);
    // Flash a tab whose structure is ready to place.
    const bool ready = (i == 0 && ps.queues[0].ready) || (i == 1 && ps.queues[1].ready);
    if (ui::iconTab(kTabNames[i], pictos[i], kTabNames[i], ImVec2(tw, 50), tab_ == i, ready && tab_ != i)) tab_ = i;
    ui::glowIfHighlighted((std::string("tab.") + kTabIds[i]).c_str());
  }
  ImGui::Dummy(ImVec2(0, 2));
  // Which types belong on this tab, under their group headings.
  std::vector<int> items;
  std::vector<std::string> groups;
  const auto& types = scene_.types();
  for (int t = 0; t < static_cast<int>(types.size()); ++t) {
    const UnitTypeDef& d = types.get(t);
    if (d.builtAt.empty() || d.hidden || d.faction != faction) continue;
    const int tab = d.category == UnitCategory::Building ? 0
                    : (d.category == UnitCategory::Defense || d.category == UnitCategory::Fortification) ? 1
                    : d.category == UnitCategory::Infantry ? 2 : 3;
    if (tab != tab_) continue;
    if (sim.buildLocked(me, t)) continue;  // a tutorial hasn't got this far yet
    items.push_back(t);
    if (std::find(groups.begin(), groups.end(), d.group) == groups.end()) groups.push_back(d.group);
  }
  const float cw = (width - 4) * 0.5f, ch = 84.0f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const SpriteRenderer& r = scene_.renderer();
  ui::panel(dl, ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + width, ImGui::GetCursorScreenPos().y +
                                                              std::max(60.0f, ImGui::GetContentRegionAvail().y - 168)),
            true);
  ImGui::BeginChild("##cameos", ImVec2(width, std::max(60.0f, ImGui::GetContentRegionAvail().y - 168)), false);
  dl = ImGui::GetWindowDrawList();
  hoveredType_ = -1;
  for (const std::string& group : groups) {
    if (!group.empty()) {
      ui::sectionTitle(dl, ImGui::GetCursorScreenPos(), width - 2, group.c_str());
      ImGui::Dummy(ImVec2(width - 2, ImGui::GetTextLineHeight() + 6));
    }
    int column = 0;
    for (const int t : items) {
      const UnitTypeDef& d = types.get(t);
      if (d.group != group) continue;
      const ProductionQueue& q = ps.queues[static_cast<size_t>(queueFor(d.category))];
      const int queued = static_cast<int>(std::count(q.items.begin(), q.items.end(), t));
      const bool front = !q.items.empty() && q.items.front() == t;
      const bool isFort = d.category == UnitCategory::Fortification;
      const bool available = sim.canProduce(me, t);
      // Another structure in this line is being built or waits: this one can't start.
      const bool lineBusy = d.isStructure() && !isFort && !q.items.empty() && !front;

      if (column++ % 2) ImGui::SameLine(0, 4);
      ImGui::PushID(t);
      const ImVec2 p0 = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##cameo", ImVec2(cw - 2, ch), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
      ui::glowIfHighlighted(("build:" + d.id).c_str());
      const bool hovered = ImGui::IsItemHovered();
      if (hovered) hoveredType_ = t;
      const ImVec2 p1(p0.x + cw - 2, p0.y + ch);
      ui::panel(dl, p0, p1, false);
      dl->AddRectFilled(ImVec2(p0.x + 3, p0.y + 3), ImVec2(p1.x - 3, p1.y - 3),
                        hovered && available ? IM_COL32(52, 52, 40, 255) : IM_COL32(28, 29, 23, 255), 1.0f);
      drawIcon(dl, r, scene_.unitArt(t), ImVec2(p0.x + 4, p0.y + 4), ImVec2(cw - 10, ch - 20),
               available && !lineBusy ? IM_COL32_WHITE : IM_COL32(100, 100, 96, 190));
      // Name strip and price tag.
      dl->AddRectFilled(ImVec2(p0.x + 3, p1.y - 18), ImVec2(p1.x - 3, p1.y - 3), IM_COL32(16, 16, 12, 200));
      dl->AddText(ImVec2(p0.x + 6, p1.y - 17), available ? ui::kStencil : ui::kStencilDim, d.name.c_str());
      char cost[16];
      std::snprintf(cost, sizeof cost, "$%d", d.cost);
      const float cwid = ImGui::CalcTextSize(cost).x;
      dl->AddRectFilled(ImVec2(p1.x - cwid - 9, p0.y + 3), ImVec2(p1.x - 3, p0.y + 18), IM_COL32(16, 16, 12, 190), 2.0f);
      dl->AddText(ImVec2(p1.x - cwid - 6, p0.y + 3), ui::kBrass, cost);
      // Progress: a dark veil that lifts as the work proceeds.
      if (front && !q.ready) {
        const float frac = std::clamp(static_cast<float>(q.progress) / std::max(1, d.buildTime), 0.0f, 1.0f);
        dl->AddRectFilled(ImVec2(p0.x + 3, p0.y + 3), ImVec2(p1.x - 3, p0.y + 3 + (ch - 22) * (1.0f - frac)), IM_COL32(0, 0, 0, 150));
        dl->AddRectFilled(ImVec2(p0.x + 3, p1.y - 20), ImVec2(p0.x + 3 + (cw - 8) * frac, p1.y - 18), ui::kBrass);
      }
      if (front && q.ready) {
        const bool blink = static_cast<int>(ImGui::GetTime() * 3) % 2;
        dl->AddRect(p0, p1, blink ? ui::kGood : IM_COL32(60, 120, 50, 255), 2.0f, 0, 2.0f);
        dl->AddText(ImVec2(p0.x + 6, p0.y + 4), ui::kGood, "READY");
      }
      if (queued > 1 || (queued == 1 && !d.isStructure())) {
        char n[16];
        std::snprintf(n, sizeof n, "%d", queued);
        dl->AddRectFilled(ImVec2(p0.x + 3, p0.y + 3), ImVec2(p0.x + 10 + ImGui::CalcTextSize(n).x, p0.y + 18), IM_COL32(120, 30, 20, 220), 2.0f);
        dl->AddText(ImVec2(p0.x + 6, p0.y + 3), IM_COL32(255, 255, 255, 255), n);
      }
      if (placeType_ == t) dl->AddRect(p0, p1, ui::kBrass, 2.0f, 0, 2.0f);

      // Clicks.
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && available) {
        if (isFort) {
          startPlacing(t);
        } else if (front && q.ready) {
          startPlacing(t);
        } else if (!lineBusy) {
          Command c;
          c.type = CommandType::Produce;
          c.player = me;
          c.unitType = static_cast<uint16_t>(t);
          c.amount = ImGui::GetIO().KeyShift ? 5 : 1;
          sim.queue(std::move(c));
        }
      }
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && queued > 0) {
        Command c;
        c.type = CommandType::Produce;
        c.player = me;
        c.unitType = static_cast<uint16_t>(t);
        c.amount = ImGui::GetIO().KeyShift ? -5 : -1;
        sim.queue(std::move(c));
        if (placeType_ == t) cancel();
      }
      if (hovered) {
        ImGui::BeginTooltip();
        ImGui::Text("%s  -  $%d", d.name.c_str(), d.cost);
        if (!d.description.empty()) {
          ImGui::PushTextWrapPos(280);
          ImGui::TextDisabled("%s", d.description.c_str());
          ImGui::PopTextWrapPos();
        }
        if (d.isStructure() && d.power) ImGui::Text(d.power > 0 ? "Power +%d" : "Power %d", d.power);
        if (!d.isStructure()) ImGui::Text("%d hp, %s", d.maxHealth, d.infantry() ? "infantry" : "vehicle");
        for (int w : d.weapons) ImGui::BulletText("%s", scene_.weapons().get(w).name.c_str());
        if (d.cover) ImGui::Text("Infantry inside take %d%% less damage", d.cover);
        if (d.coverAround) ImGui::Text("Infantry beside it take %d%% less damage", d.coverAround);
        if (d.garrison) ImGui::Text("Holds %d infantry", d.garrison);
        if (!available) {
          ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Needs:");
          for (int need : d.prereqs) ImGui::BulletText("%s", types.get(need).name.c_str());
          if (d.builtAt == "barracks") ImGui::BulletText("a Barracks");
          if (d.builtAt == "factory") ImGui::BulletText("a Tank Works");
          if (d.builtAt == "yard") ImGui::BulletText("a Construction Yard");
        }
        ImGui::TextDisabled(isFort ? "Click, then drag a line on the map. Right-click cancels."
                                   : d.isStructure() ? "Click to build; click again when ready to place it."
                                                     : "Click to train (Shift: 5). Right-click to cancel.");
        ImGui::TextDisabled("K: this unit in the encyclopedia");
        ImGui::EndTooltip();
      }
      ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 2));
  }
  if (items.empty()) ImGui::TextDisabled("  Nothing to build here.");
  ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Orders for the selection
// ---------------------------------------------------------------------------
void Sidebar::drawOrders(float width) {
  Simulation& sim = scene_.sim();
  Registry& reg = sim.registry();
  scene_.pruneSelection();
  auto& sel = scene_.selection();
  std::vector<Entity> movers, deployers, producers, own;
  for (Entity e : sel) {
    const CUnit& u = reg.get<CUnit>(e);
    const UnitTypeDef& d = scene_.types().get(u.type);
    if (u.owner == scene_.player()) own.push_back(e);
    if (d.speed > 0) movers.push_back(e);
    if (d.deploysTo >= 0) deployers.push_back(e);
    if (reg.has<CProducer>(e)) producers.push_back(e);
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImGui::Dummy(ImVec2(0, 2));
  const std::string title = sel.empty() ? std::string("ORDERS") : "ORDERS - " + std::to_string(sel.size()) + " selected";
  ui::sectionTitle(dl, ImGui::GetCursorScreenPos(), width, title.c_str());
  ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight() + 6));
  const float bw = (width - 8) / 3.0f, bh = 24;
  // Stance.
  int common = -1;
  for (Entity e : sel) {
    const int s = static_cast<int>(reg.get<CUnit>(e).stance);
    common = common == -1 ? s : (common == s ? s : -2);
  }
  for (int s = 0; s < 3; ++s) {
    if (s) ImGui::SameLine(0, 4);
    const bool stanceClicked = ui::plateButton(kStanceNames[s], kStanceNames[s], ImVec2(bw, bh), common == s, false, !sel.empty());
    ui::glowIfHighlighted((std::string("order.stance.") + kStanceNames[s]).c_str());
    if (stanceClicked) {
      Command c;
      c.type = CommandType::SetStance;
      c.units = sel;
      c.stance = static_cast<Stance>(s);
      sim.queue(std::move(c));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", kStanceTips[s]);
  }
  // Formation used by the next move order.
  const float fw = (width - 16) / 5.0f;
  for (int f = 0; f < 5; ++f) {
    if (f) ImGui::SameLine(0, 4);
    const bool formClicked = ui::plateButton(kFormationNames[f], kFormationNames[f], ImVec2(fw, bh), static_cast<int>(formation_) == f);
    ui::glowIfHighlighted((std::string("order.formation.") + kFormationNames[f]).c_str());
    if (formClicked)
      formation_ = static_cast<Formation>(f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kFormationTips[f]);
  }
  // Special orders.
  const bool deployClicked = ui::plateButton("deploy", "Deploy", ImVec2(bw, bh), false, false, !deployers.empty());
  ui::glowIfHighlighted("order.deploy");
  if (deployClicked) {
    Command c;
    c.type = CommandType::Deploy;
    c.units = deployers;
    sim.queue(std::move(c));
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Unfold a construction truck into a Construction Yard (Ctrl+D).");
  ImGui::SameLine(0, 4);
  const bool evacuateClicked = ui::plateButton("evacuate", "Evacuate", ImVec2(bw, bh), false, false, scene_.selectionCanEvacuate());
  ui::glowIfHighlighted("order.evacuate");
  if (evacuateClicked)
    scene_.evacuateSelection();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Men in trenches stay dug in until evacuated; men in buildings come out too (E).");
  const bool sellClicked = ui::plateButton("sell", sellMode_ ? "Selling" : "Sell", ImVec2(bw, bh), sellMode_);
  ui::glowIfHighlighted("order.sell");
  if (sellClicked) {
    sellMode_ = !sellMode_;
    placeType_ = -1;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click your structures to sell them for half their cost. Esc stops.");
  ImGui::SameLine(0, 4);
  const bool encyclopediaClicked = ui::plateButton("book", "Encyclopedia", ImVec2(bw * 2 + 4, bh));
  ui::glowIfHighlighted("order.encyclopedia");
  if (encyclopediaClicked) scene_.openEncyclopedia(hoveredType_ >= 0 ? hoveredType_ : -1);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every unit and building, with its weapons and numbers (K).");
  if (!producers.empty()) ImGui::TextDisabled("Right-click the map: rally point");
}

void Sidebar::update(float dt) {
  minimapTimer_ -= dt;
  // A structure that stopped being ready (cancelled, editor) ends placement.
  if (placeType_ >= 0) {
    const UnitTypeDef& d = scene_.types().get(placeType_);
    if (d.category != UnitCategory::Fortification) {
      const ProductionQueue& q = scene_.sim().playerState(scene_.player()).queues[static_cast<size_t>(queueFor(d.category))];
      if (!q.ready || q.items.empty() || q.items.front() != placeType_) placeType_ = -1;
    }
  }
}

void Sidebar::draw() {
  const ImGuiIO& io = ImGui::GetIO();
  // The side we command sets the colours: field grey or khaki.
  ui::setFaction(scene_.players()[scene_.player()].faction.c_str());
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - kWidth, 0));
  ImGui::SetNextWindowSize(ImVec2(kWidth, io.DisplaySize.y));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(ui::kPlateDark));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
  ui::pushTheme();
  ImGui::Begin("##sidebar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  {
    // The frame: one steel plate, a thick dark edge towards the battlefield.
    const ImVec2 a = ImGui::GetWindowPos(), b(a.x + kWidth, a.y + io.DisplaySize.y);
    ui::panel(dl, a, b);
    dl->AddRectFilled(a, ImVec2(a.x + 4, b.y), ui::kPlateDark);
    dl->AddLine(ImVec2(a.x + 4, a.y), ImVec2(a.x + 4, b.y), ui::kBrassDim, 1.0f);
    ui::rivets(dl, ImVec2(a.x + 4, a.y), b, 6);
    // Painted markings low on the plate: a stencilled stripe and the emblem.
    ui::stencilStrip(dl, ImVec2(a.x + 12, b.y - 18), ImVec2(b.x - 40, b.y - 8));
    ui::emblem(dl, ImVec2(b.x - 24, b.y - 13), 18);
  }
  const float width = kWidth - 16;
  const uint8_t me = scene_.player();
  const PlayerState& ps = scene_.sim().playerState(me);
  // Header plate: supplies, income and power.
  {
    const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + width, a.y + 40);
    ui::panel(dl, a, b, true);
    char buf[48];
    std::snprintf(buf, sizeof buf, "$ %d", ps.credits);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.35f, ImVec2(a.x + 8, a.y + 3), ui::kBrass, buf);
    std::snprintf(buf, sizeof buf, "+%d / min", ps.incomePerMinute);
    dl->AddText(ImVec2(a.x + 8, a.y + 23), ui::kStencilDim, buf);
    std::snprintf(buf, sizeof buf, "Power %d / %d", ps.powerUsed, ps.powerMade);
    const float pw = ImGui::CalcTextSize(buf).x;
    dl->AddText(ImVec2(b.x - pw - 8, a.y + 5), ps.lowPower() ? ui::kBad : ui::kGood, buf);
    // Power gauge.
    const float made = static_cast<float>(std::max(1, ps.powerMade)), used = static_cast<float>(ps.powerUsed);
    const float gw = width * 0.44f, gx = b.x - gw - 8, k = gw / std::max(made, used);
    dl->AddRectFilled(ImVec2(gx, a.y + 25), ImVec2(gx + gw, a.y + 33), IM_COL32(12, 12, 10, 255));
    dl->AddRectFilled(ImVec2(gx, a.y + 25), ImVec2(gx + made * k, a.y + 29), IM_COL32(90, 150, 70, 255));
    dl->AddRectFilled(ImVec2(gx, a.y + 29), ImVec2(gx + used * k, a.y + 33),
                      ps.lowPower() ? IM_COL32(230, 80, 60, 255) : IM_COL32(220, 190, 90, 255));
    ImGui::InvisibleButton("##economy", ImVec2(width, 40));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Supplies, and income a minute (Supply Depots on supply points, plus the Construction Yard).\n"
                        "Power used / made: short of power, everything takes twice as long to build.");
  }
  ImGui::Dummy(ImVec2(0, 2));
  {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ui::panel(dl, ImVec2(a.x - 2, a.y - 2), ImVec2(a.x + width + 2, a.y + width * 0.5f + 2), true);
  }
  drawMinimap(width);
  ImGui::Dummy(ImVec2(0, 2));
  {
    const uint32_t tc = scene_.teamColor(me);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(a.x, a.y + 3), ImVec2(a.x + 10, a.y + 13), tc | 0xFF000000u, 2.0f);
    ui::emblem(dl, ImVec2(a.x + 23, a.y + 8), 13);
    ImGui::Dummy(ImVec2(32, 0));
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kStencil), "%s", scene_.players()[me].name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(Tab)");
  }
  drawProduction(width);
  drawOrders(width);
  ImGui::End();
  ui::popTheme();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

}  // namespace legion

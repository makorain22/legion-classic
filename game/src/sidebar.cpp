#include "sidebar.hpp"

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
        const Vec2f s = scene_.groundToScreenWorld(x * 256.0f + 128, y * 256.0f + 128);
        r.sprite(scene_.art().moveMarker, s.x, s.y, kWhite, (ok && tileOk) ? rgba8(110, 230, 90, 170) : rgba8(240, 70, 50, 190));
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
  // Tabs.
  const float tw = (width - 6) / 4.0f;
  for (int i = 0; i < 4; ++i) {
    if (i) ImGui::SameLine(0, 2);
    const bool sel = tab_ == i;
    // Flash a tab whose structure is ready to place.
    const bool ready = (i == 0 && ps.queues[0].ready) || (i == 1 && ps.queues[1].ready);
    ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4(0.55f, 0.45f, 0.22f, 1) : ready ? ImVec4(0.30f, 0.45f, 0.20f, 1)
                                                                                    : ImVec4(0.20f, 0.20f, 0.17f, 1));
    if (ImGui::Button(kTabNames[i], ImVec2(tw, 0))) tab_ = i;
    ImGui::PopStyleColor();
  }
  // Which types belong on this tab.
  std::vector<int> items;
  const auto& types = scene_.types();
  for (int t = 0; t < static_cast<int>(types.size()); ++t) {
    const UnitTypeDef& d = types.get(t);
    if (d.builtAt.empty() || d.faction != faction) continue;
    const int tab = d.category == UnitCategory::Building ? 0
                    : (d.category == UnitCategory::Defense || d.category == UnitCategory::Fortification) ? 1
                    : d.category == UnitCategory::Infantry ? 2 : 3;
    if (tab == tab_) items.push_back(t);
  }
  const float cw = (width - 4) * 0.5f, ch = 62.0f;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const SpriteRenderer& r = scene_.renderer();
  ImGui::BeginChild("##cameos", ImVec2(width, std::min(ImGui::GetContentRegionAvail().y - 150, (ch + 4) * 4.5f)), false);
  for (size_t i = 0; i < items.size(); ++i) {
    const int t = items[i];
    const UnitTypeDef& d = types.get(t);
    const ProductionQueue& q = ps.queues[static_cast<size_t>(queueFor(d.category))];
    const int queued = static_cast<int>(std::count(q.items.begin(), q.items.end(), t));
    const bool front = !q.items.empty() && q.items.front() == t;
    const bool isFort = d.category == UnitCategory::Fortification;
    const bool available = sim.canProduce(me, t);
    // Another structure in this line is being built or waits: this one can't start.
    const bool lineBusy = d.isStructure() && !isFort && !q.items.empty() && !front;

    if (i % 2) ImGui::SameLine(0, 4);
    ImGui::PushID(t);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##cameo", ImVec2(cw, ch), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 p1(p0.x + cw, p0.y + ch);
    dl->AddRectFilled(p0, p1, hovered && available ? IM_COL32(58, 56, 44, 255) : IM_COL32(36, 36, 30, 255), 2.0f);
    drawIcon(dl, r, scene_.unitArt(t), ImVec2(p0.x + 2, p0.y + 2), ImVec2(cw - 4, ch - 18),
             available && !lineBusy ? IM_COL32_WHITE : IM_COL32(110, 110, 110, 200));
    // Name and price.
    dl->AddText(ImVec2(p0.x + 4, p1.y - 15), available ? IM_COL32(220, 210, 180, 255) : IM_COL32(120, 116, 104, 255),
                d.name.c_str());
    char cost[16];
    std::snprintf(cost, sizeof cost, "$%d", d.cost);
    dl->AddText(ImVec2(p1.x - ImGui::CalcTextSize(cost).x - 4, p0.y + 3), IM_COL32(236, 200, 96, 255), cost);
    // Progress: a dark veil that lifts as the work proceeds.
    if (front && !q.ready) {
      const float frac = std::clamp(static_cast<float>(q.progress) / std::max(1, d.buildTime), 0.0f, 1.0f);
      dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + (ch - 16) * (1.0f - frac)), IM_COL32(0, 0, 0, 140));
      dl->AddRectFilled(ImVec2(p0.x, p1.y - 17), ImVec2(p0.x + cw * frac, p1.y - 15), IM_COL32(236, 200, 96, 255));
    }
    if (front && q.ready) {
      const bool blink = static_cast<int>(ImGui::GetTime() * 3) % 2;
      dl->AddRect(p0, p1, blink ? IM_COL32(120, 236, 96, 255) : IM_COL32(60, 120, 50, 255), 2.0f, 0, 2.0f);
      dl->AddText(ImVec2(p0.x + 4, p0.y + 3), IM_COL32(150, 255, 120, 255), "READY");
    }
    if (queued > 1 || (queued == 1 && !d.isStructure())) {
      char n[16];
      std::snprintf(n, sizeof n, "%d", queued);
      dl->AddText(ImVec2(p0.x + 4, p0.y + 3), IM_COL32(255, 255, 255, 255), n);
    }
    if (placeType_ == t) dl->AddRect(p0, p1, IM_COL32(236, 200, 96, 255), 2.0f, 0, 2.0f);

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
      if (d.isStructure() && d.power) ImGui::Text(d.power > 0 ? "Power +%d" : "Power %d", d.power);
      if (!d.isStructure()) ImGui::Text("%d hp, %s", d.maxHealth, d.infantry() ? "infantry" : "vehicle");
      for (int w : d.weapons) ImGui::BulletText("%s", scene_.weapons().get(w).name.c_str());
      if (d.cover) ImGui::Text("Infantry inside take %d%% less damage", d.cover);
      if (d.coverAround) ImGui::Text("Infantry beside it take %d%% less damage", d.coverAround);
      if (!available) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Needs:");
        if (!sim.canProduce(me, t)) {
          for (int need : d.prereqs) ImGui::BulletText("%s", types.get(need).name.c_str());
          if (d.builtAt == "barracks") ImGui::BulletText("a Barracks");
          if (d.builtAt == "factory") ImGui::BulletText("a Tank Works");
          if (d.builtAt == "yard") ImGui::BulletText("a Construction Yard");
        }
      }
      ImGui::TextDisabled(isFort ? "Click, then drag a line on the map. Right-click cancels."
                                 : d.isStructure() ? "Click to build; click again when ready to place it."
                                                   : "Click to train (Shift: 5). Right-click to cancel.");
      ImGui::EndTooltip();
    }
    ImGui::PopID();
  }
  if (items.empty()) ImGui::TextDisabled("Nothing to build here.");
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
  ImGui::SeparatorText(sel.empty() ? "Orders" : (std::to_string(sel.size()) + " selected").c_str());
  const float bw = (width - 8) / 3.0f;
  // Stance.
  if (!movers.empty() || !sel.empty()) {
    int common = -1;
    for (Entity e : sel) {
      const int s = static_cast<int>(reg.get<CUnit>(e).stance);
      common = common == -1 ? s : (common == s ? s : -2);
    }
    for (int s = 0; s < 3; ++s) {
      if (s) ImGui::SameLine(0, 4);
      ImGui::PushStyleColor(ImGuiCol_Button, common == s ? ImVec4(0.55f, 0.45f, 0.22f, 1) : ImVec4(0.20f, 0.20f, 0.17f, 1));
      ImGui::BeginDisabled(sel.empty());
      if (ImGui::Button(kStanceNames[s], ImVec2(bw, 0))) {
        Command c;
        c.type = CommandType::SetStance;
        c.units = sel;
        c.stance = static_cast<Stance>(s);
        sim.queue(std::move(c));
      }
      ImGui::EndDisabled();
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", kStanceTips[s]);
    }
  }
  // Formation used by the next move order.
  const float fw = (width - 16) / 5.0f;
  for (int f = 0; f < 5; ++f) {
    if (f) ImGui::SameLine(0, 4);
    const bool on = static_cast<int>(formation_) == f;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.55f, 0.45f, 0.22f, 1) : ImVec4(0.20f, 0.20f, 0.17f, 1));
    if (ImGui::Button(kFormationNames[f], ImVec2(fw, 0))) formation_ = static_cast<Formation>(f);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kFormationTips[f]);
  }
  // Special orders.
  if (!deployers.empty()) {
    if (ImGui::Button("Deploy (D)", ImVec2(bw * 1.5f, 0))) {
      Command c;
      c.type = CommandType::Deploy;
      c.units = deployers;
      sim.queue(std::move(c));
    }
    ImGui::SameLine(0, 4);
  }
  if (!producers.empty()) ImGui::TextDisabled("Right-click: rally point");
  ImGui::PushStyleColor(ImGuiCol_Button, sellMode_ ? ImVec4(0.6f, 0.3f, 0.2f, 1) : ImVec4(0.20f, 0.20f, 0.17f, 1));
  if (ImGui::Button(sellMode_ ? "Selling... (Esc)" : "Sell", ImVec2(bw, 0))) {
    sellMode_ = !sellMode_;
    placeType_ = -1;
  }
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click your structures to sell them for half their cost.");
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
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - kWidth, 0));
  ImGui::SetNextWindowSize(ImVec2(kWidth, io.DisplaySize.y));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.11f, 0.09f, 0.97f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
  ImGui::Begin("##sidebar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  const float width = kWidth - 12;
  const uint8_t me = scene_.player();
  const PlayerState& ps = scene_.sim().playerState(me);
  // Supplies and power.
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.78f, 0.38f, 1));
  ImGui::Text("$ %d", ps.credits);
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Supplies. Income: %d a minute (Supply Depots on supply points, plus the Construction Yard).",
                      ps.incomePerMinute);
  ImGui::SameLine();
  ImGui::TextDisabled("+%d", ps.incomePerMinute);
  ImGui::SameLine(width * 0.56f);
  const ImVec4 pc = ps.lowPower() ? ImVec4(1, 0.4f, 0.3f, 1) : ImVec4(0.6f, 0.85f, 0.5f, 1);
  ImGui::TextColored(pc, "Pwr %d/%d", ps.powerUsed, ps.powerMade);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Used / made. Short of power, everything takes twice as long to build.");
  // Power bar.
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float made = static_cast<float>(std::max(1, ps.powerMade)), used = static_cast<float>(ps.powerUsed);
    const float scale = width / std::max(made, used) ;
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + 4), IM_COL32(30, 30, 26, 255));
    dl->AddRectFilled(p0, ImVec2(p0.x + made * scale, p0.y + 4), IM_COL32(90, 150, 70, 255));
    dl->AddRectFilled(ImVec2(p0.x, p0.y + 4), ImVec2(p0.x + used * scale, p0.y + 7),
                      ps.lowPower() ? IM_COL32(230, 80, 60, 255) : IM_COL32(220, 190, 90, 255));
    ImGui::Dummy(ImVec2(width, 9));
  }
  drawMinimap(width);
  ImGui::Dummy(ImVec2(0, 2));
  ImGui::TextColored(ImVec4(scene_.teamColor(me) % 256 / 255.0f, ((scene_.teamColor(me) >> 8) & 255) / 255.0f,
                            ((scene_.teamColor(me) >> 16) & 255) / 255.0f, 1),
                     "%s", scene_.players()[me].name.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("(Tab)");
  drawProduction(width);
  drawOrders(width);
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

}  // namespace legion

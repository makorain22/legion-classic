// Legion Classic - call-ins on screen (Milestone 9.14): the Support tab and
// aiming a call-in, and drawing what the simulation flies in: planes and
// their shadows, warships off the coast, landing craft, parachutes, supply
// crates and the wrecks of planes brought down.
//
// The simulation computes every flight from ticks (sim/support.hpp); this
// only looks. Sounds that belong to a moment of a flight (the planes passing
// overhead, bombs falling) are started here when the moment comes.
#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "game_audio.hpp"
#include "game_scene.hpp"
#include "pioneer/math/iso.hpp"
#include "sidebar.hpp"
#include "ui_style.hpp"

using namespace pioneer;

namespace legion {

void drawUnitIcon(ImDrawList* dl, const SpriteRenderer& r, const UnitArt& art, ImVec2 p0, ImVec2 size, ImU32 tint);

namespace {

constexpr float kAltitude = BattleFx::kAirPx * HEIGHT_UNITS_PER_TILE / iso::HEIGHT_PX_PER_TILE;  // height units

const char* modelFor(const SupportPowerDef& d) {
  if (d.kind == SupportKind::Naval) return "warship";
  if (d.kind == SupportKind::Landing) return "landing_boat";
  switch (d.aircraft) {
    case AircraftRole::Fighter: return "plane_fighter";
    case AircraftRole::Attack: return "plane_attack";
    case AircraftRole::Bomber: return "plane_bomber";
    case AircraftRole::Transport: return "plane_transport";
    case AircraftRole::Recon: return "plane_recon";
    default: return "plane_bomber";
  }
}
const char* modelFor(AircraftRole r) {
  SupportPowerDef d;
  d.aircraft = r;
  return modelFor(d);
}

const UnitArt* artNamed(const SpriteCatalog& art, const std::string& model, const std::string& faction) {
  for (const std::string& k : {model + "@" + faction, model + "@ger"}) {
    auto it = art.units.find(k);
    if (it != art.units.end()) return &it->second;
  }
  return nullptr;
}

// The 16-way facing of a direction in leptons.
int facingTo(float dx, float dy) {
  return facingOf(atan2Angle(static_cast<int64_t>(dy), static_cast<int64_t>(dx)));
}

// Where a side's planes come from when no line is dragged (as the simulation reckons it).
Vec2f homeOf(Simulation& sim, uint8_t owner) {
  double sx = 0, sy = 0;
  int n = 0;
  Registry& reg = sim.registry();
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner != owner || !sim.types().get(u.type).isStructure()) continue;
    const CPosition& p = reg.pool<CPosition>().get(e);
    sx += p.x;
    sy += p.y;
    ++n;
  }
  if (n == 0) return {sim.map().widthLeptons() / 2.0f, sim.map().heightLeptons() / 2.0f};
  return {static_cast<float>(sx / n), static_cast<float>(sy / n)};
}

}  // namespace

// ---------------------------------------------------------------------------
// The Support tab

bool Sidebar::hasSupportTab() const {
  const std::string& faction = scene_.players()[scene_.player()].faction;
  const SupportRegistry& reg = scene_.sim().support();
  for (size_t i = 0; i < reg.size(); ++i) {
    const SupportPowerDef& d = reg.get(static_cast<int>(i));
    if (d.faction == faction || scene_.sim().supportState(scene_.player(), static_cast<int>(i)).gate == SupportState::Gate::On)
      return true;
  }
  return false;
}

void Sidebar::startSupport(int power) {
  cancel();
  supportPower_ = power;
}

void Sidebar::drawSupport(float width, float height) {
  Simulation& sim = scene_.sim();
  const uint8_t me = scene_.player();
  const std::string& faction = scene_.players()[me].faction;
  const SupportRegistry& reg = sim.support();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ui::panel(dl, ImGui::GetCursorScreenPos(),
            ImVec2(ImGui::GetCursorScreenPos().x + width, ImGui::GetCursorScreenPos().y + height), true);
  ImGui::BeginChild("##support", ImVec2(width, height), false);
  dl = ImGui::GetWindowDrawList();
  const float cw = (std::min(width, ImGui::GetContentRegionAvail().x) - 4) * 0.5f, ch = 84.0f;
  hoveredType_ = -1;
  int column = 0;
  for (size_t i = 0; i < reg.size(); ++i) {
    const int p = static_cast<int>(i);
    const SupportPowerDef& d = reg.get(p);
    const SupportState& st = sim.supportState(me, p);
    if (d.faction != faction && st.gate != SupportState::Gate::On) continue;
    const bool open = sim.supportOpen(me, p);
    // A call-in no structure of ours opens and no mission gave us: not shown at all.
    if (!open && d.prereqs.empty()) continue;
    const bool charged = open && st.charges > 0 && sim.tickCount() >= st.readyTick;
    const bool affordable = sim.playerState(me).credits >= d.cost;
    if (column++ % 2) ImGui::SameLine(0, 4);
    ImGui::PushID(p);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##callin", ImVec2(cw - 2, ch))) {
      if (charged && affordable) {
        startSupport(p);
        if (scene_.audio()) scene_.audio()->ui("ui_click");
      } else {
        if (scene_.audio()) scene_.audio()->ui("ui_denied");
        std::string why;
        sim.canCallSupport(me, p, {sim.map().widthLeptons() / 2, sim.map().heightLeptons() / 2}, &why);
        scene_.showStatus(d.name + ": " + why, 2.5f);
      }
    }
    ui::glowIfHighlighted(("support:" + d.id).c_str());
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 p1(p0.x + cw - 2, p0.y + ch);
    ui::panel(dl, p0, p1, false);
    dl->AddRectFilled(ImVec2(p0.x + 3, p0.y + 3), ImVec2(p1.x - 3, p1.y - 3),
                      hovered && charged ? IM_COL32(52, 52, 40, 255) : IM_COL32(28, 29, 23, 255), 1.0f);
    if (const UnitArt* art = artNamed(scene_.art(), modelFor(d), faction))
      drawUnitIcon(dl, scene_.renderer(), *art, ImVec2(p0.x + 4, p0.y + 4), ImVec2(cw - 10, ch - 22),
                   charged ? IM_COL32_WHITE : IM_COL32(100, 100, 96, 190));
    // Name strip.
    dl->AddRectFilled(ImVec2(p0.x + 3, p1.y - 18), ImVec2(p1.x - 3, p1.y - 3), IM_COL32(16, 16, 12, 200));
    {
      std::string name = d.name;
      while (name.size() > 4 && ImGui::CalcTextSize(name.c_str()).x > cw - 14) name = name.substr(0, name.size() - 4) + "...";
      dl->AddText(ImVec2(p0.x + 6, p1.y - 17), open ? ui::kStencil : ui::kStencilDim, name.c_str());
    }
    // Price tag and charges.
    char buf[24];
    if (d.cost > 0) std::snprintf(buf, sizeof buf, "$%d", d.cost);
    else std::snprintf(buf, sizeof buf, "free");
    const float bw = ImGui::CalcTextSize(buf).x;
    dl->AddRectFilled(ImVec2(p1.x - bw - 9, p0.y + 3), ImVec2(p1.x - 3, p0.y + 18), IM_COL32(16, 16, 12, 190), 2.0f);
    dl->AddText(ImVec2(p1.x - bw - 6, p0.y + 3), affordable ? ui::kBrass : ui::kBad, buf);
    for (int k = 0; k < d.charges; ++k) {
      const ImVec2 c(p0.x + 10 + k * 11.0f, p0.y + 11);
      if (k < st.charges) dl->AddCircleFilled(c, 4, open ? ui::kGood : ui::kStencilDim);
      else dl->AddCircle(c, 4, ui::kStencilDim, 0, 1.5f);
    }
    if (!open) {
      dl->AddRectFilled(ImVec2(p0.x + 3, p0.y + 20), ImVec2(p1.x - 3, p1.y - 18), IM_COL32(0, 0, 0, 120));
      std::string need = "OFF";
      if (!d.prereqs.empty()) need = "NEEDS " + scene_.types().get(d.prereqs[0]).name;
      while (need.size() > 4 && ImGui::CalcTextSize(need.c_str()).x > cw - 14) need = need.substr(0, need.size() - 4) + "...";
      dl->AddText(ImVec2(p0.x + 6, p0.y + 36), ui::kStencilDim, need.c_str());
    } else if (st.charges < d.charges) {
      // The next charge: a veil that lifts as it comes.
      const float total = static_cast<float>(d.recharge * TICKS_PER_SECOND);
      const float frac = std::clamp(1.0f - sim.supportRechargeLeft(me, p) / std::max(1.0f, total), 0.0f, 1.0f);
      if (st.charges == 0)
        dl->AddRectFilled(ImVec2(p0.x + 3, p0.y + 20), ImVec2(p1.x - 3, p0.y + 20 + (ch - 40) * (1.0f - frac)), IM_COL32(0, 0, 0, 150));
      dl->AddRectFilled(ImVec2(p0.x + 3, p1.y - 20), ImVec2(p0.x + 3 + (cw - 8) * frac, p1.y - 18), ui::kBrass);
    }
    if (open && sim.tickCount() < st.readyTick && st.charges > 0) {
      std::snprintf(buf, sizeof buf, "%us", (st.readyTick - sim.tickCount()) / TICKS_PER_SECOND + 1);
      dl->AddText(ImVec2(p0.x + 6, p0.y + 36), ui::kBrass, buf);
    }
    if (charged && affordable && st.charges == d.charges) {
      const bool blink = static_cast<int>(ImGui::GetTime() * 2) % 2;
      if (blink) dl->AddRect(p0, p1, IM_COL32(60, 120, 50, 255), 2.0f, 0, 1.5f);
    }
    if (supportPower_ == p) dl->AddRect(p0, p1, ui::kBrass, 2.0f, 0, 2.0f);
    if (hovered) {
      ImGui::BeginTooltip();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::kBrass), "%s", d.name.c_str());
      ImGui::PushTextWrapPos(320);
      ImGui::TextUnformatted(d.description.c_str());
      ImGui::PopTextWrapPos();
      ImGui::TextDisabled("%d charge%s, one more every %d s", d.charges, d.charges == 1 ? "" : "s", d.recharge);
      ImGui::EndTooltip();
    }
    ImGui::PopID();
  }
  if (column == 0) {
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextWrapped("No call-ins on this mission.");
  }
  ImGui::EndChild();
}

bool Sidebar::supportEvent(const SDL_Event& e) {
  const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, up = e.type == SDL_EVENT_MOUSE_BUTTON_UP;
  if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) {
    cancel();
    return true;
  }
  if (down && e.button.button == SDL_BUTTON_RIGHT) {
    cancel();
    return true;
  }
  const Vec2f g = scene_.groundAt(scene_.mouse());
  if (down && e.button.button == SDL_BUTTON_LEFT) {
    supportDragging_ = true;
    supportFrom_ = g;
    return true;
  }
  if (up && e.button.button == SDL_BUTTON_LEFT && supportDragging_) {
    supportDragging_ = false;
    const SupportPowerDef& d = scene_.sim().support().get(supportPower_);
    Command c;
    c.type = CommandType::CallSupport;
    c.player = scene_.player();
    c.unitType = static_cast<uint16_t>(supportPower_);
    c.target = {static_cast<int32_t>(supportFrom_.x), static_cast<int32_t>(supportFrom_.y)};
    const float dx = g.x - supportFrom_.x, dy = g.y - supportFrom_.y;
    if (supportUsesAircraft(d.kind) && std::hypot(dx, dy) > 1.5f * LEPTONS_PER_TILE) {
      // Dragged: the planes fly along the drag.
      c.amount = 1;
      c.facing = atan2Angle(static_cast<int64_t>(dy), static_cast<int64_t>(dx));
    }
    scene_.issue(std::move(c));
    const bool more = SDL_GetModState() & SDL_KMOD_SHIFT;
    if (!more) cancel();
    return true;
  }
  return down || up;
}

void Sidebar::renderSupport(SpriteRenderer& r) {
  if (scene_.is3D() != r.in3D()) return;
  Simulation& sim = scene_.sim();
  const SupportPowerDef& d = sim.support().get(supportPower_);
  const Vec2f mouse = scene_.groundAt(scene_.mouse());
  const Vec2f at = supportDragging_ ? supportFrom_ : mouse;
  const bool ok = sim.canCallSupport(scene_.player(), supportPower_, {static_cast<int32_t>(at.x), static_cast<int32_t>(at.y)});
  const uint32_t tint = ok ? rgba8(130, 235, 100, 200) : rgba8(240, 80, 60, 200);
  const Map& map = scene_.map();
  auto mark = [&](float lx, float ly, uint32_t c) {
    lx = std::clamp(lx, 0.0f, map.widthLeptons() - 1.0f);
    ly = std::clamp(ly, 0.0f, map.heightLeptons() - 1.0f);
    if (r.in3D()) {
      r.decal(scene_.art().moveMarker, lx, ly, c, kWhite, 1,
              [&](float gx, float gy) { return map.heightAt(gx, gy) / HEIGHT_UNITS_PER_TILE + 0.03f; });
      return;
    }
    const Vec2f s = scene_.groundToScreenWorld(lx, ly);
    r.sprite(scene_.art().moveMarker, s.x, s.y, kWhite, c);
  };
  if (d.kind == SupportKind::Bombing || d.kind == SupportKind::Strafing) {
    // The line the planes will lay their bombs or bullets along.
    Vec2f dir = supportDragging_ ? Vec2f{mouse.x - at.x, mouse.y - at.y} : Vec2f{0, 0};
    if (std::hypot(dir.x, dir.y) < 1.5f * LEPTONS_PER_TILE) {
      const Vec2f home = homeOf(sim, scene_.player());
      dir = {at.x - home.x, at.y - home.y};
      if (std::hypot(dir.x, dir.y) < 1) dir = {1, 0};
    }
    const float len = std::hypot(dir.x, dir.y);
    const float ux = dir.x / len, uy = dir.y / len, L = static_cast<float>(std::max(d.length, 2 * LEPTONS_PER_TILE));
    for (float t = -L / 2; t <= L / 2; t += 96) mark(at.x + ux * t, at.y + uy * t, tint);
    for (float t = 1; t <= 3; ++t) {  // an arrowhead: the way they fly
      mark(at.x + ux * (L / 2 - t * 90) - uy * t * 90, at.y + uy * (L / 2 - t * 90) + ux * t * 90, tint);
      mark(at.x + ux * (L / 2 - t * 90) + uy * t * 90, at.y + uy * (L / 2 - t * 90) - ux * t * 90, tint);
    }
  } else if (d.kind == SupportKind::Landing) {
    Vec2i sea, beach;
    if (sim.findLanding({static_cast<int32_t>(at.x), static_cast<int32_t>(at.y)}, d.seaRange, sea, beach)) {
      const float dx = static_cast<float>(beach.x - sea.x), dy = static_cast<float>(beach.y - sea.y), len = std::max(1.0f, std::hypot(dx, dy));
      for (float t = 0; t <= len; t += 128) mark(sea.x + dx / len * t, sea.y + dy / len * t, rgba8(120, 180, 255, 200));
      for (int k = 0; k < 16; ++k) {
        const float a = k * 0.3927f;
        mark(beach.x + std::cos(a) * 300, beach.y + std::sin(a) * 300, tint);
      }
    }
    mark(at.x, at.y, tint);
  } else {
    const float rad = static_cast<float>(std::max(d.radius, LEPTONS_PER_TILE));
    const int n = std::clamp(static_cast<int>(rad / 64), 12, 48);
    for (int k = 0; k < n; ++k) {
      const float a = k * 6.2831853f / n;
      mark(at.x + std::cos(a) * rad, at.y + std::sin(a) * rad, tint);
    }
    mark(at.x, at.y, tint);
  }
}

void Sidebar::supportTooltip() {
  if (supportPower_ < 0 || ImGui::GetIO().WantCaptureMouse) return;
  Simulation& sim = scene_.sim();
  if (static_cast<size_t>(supportPower_) >= sim.support().size()) {
    cancel();
    return;
  }
  const SupportPowerDef& d = sim.support().get(supportPower_);
  const Vec2f at = supportDragging_ ? supportFrom_ : scene_.groundAt(scene_.mouse());
  std::string why;
  const bool ok = sim.canCallSupport(scene_.player(), supportPower_, {static_cast<int32_t>(at.x), static_cast<int32_t>(at.y)}, &why);
  ImGui::BeginTooltip();
  ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ok ? ui::kGood : ui::kBad), "%s", d.name.c_str());
  if (!ok) ImGui::TextUnformatted(why.c_str());
  else if (d.kind == SupportKind::Bombing || d.kind == SupportKind::Strafing)
    ImGui::TextUnformatted(supportDragging_ ? "Release to call; they fly along the line" : "Click to call, or drag the line they fly");
  else
    ImGui::TextUnformatted("Click to call  (Shift: call more)");
  ImGui::TextDisabled("Right-click or Esc: cancel");
  ImGui::EndTooltip();
}

// ---------------------------------------------------------------------------
// What flies in

void GameScene::queueCallIns(const RectF& view, uint32_t& order) {
  if (!sim_) return;
  const double now = static_cast<double>(sim_->tickCount()) - 1.0 + alpha_;
  auto clampX = [&](float x) { return std::clamp(x, 0.0f, map_.widthLeptons() - 1.0f); };
  auto clampY = [&](float y) { return std::clamp(y, 0.0f, map_.heightLeptons() - 1.0f); };
  auto groundH = [&](float x, float y) { return map_.heightAt(clampX(x), clampY(y)); };
  const float water = static_cast<float>(map_.waterLevel());
  const bool threeD = cam_.is3D();
  // Planes and their shadows: shadows first under everything, planes over everything.
  auto put = [&](int frame, float lx, float ly, float h, uint32_t team, uint32_t tint, int64_t depth, float yaw) {
    const Vec2f s = project(lx, ly, h);
    if (!threeD && !view.contains(s)) return;
    DrawItem it{depth, order++, frame, s.x, s.y, team, tint, lx, ly, h};
    it.yaw = yaw;
    it.turned = threeD;
    drawList_.push_back(it);
  };
  constexpr int64_t kSky = INT64_MAX / 2, kUnder = INT64_MIN / 2;
  const int anim = static_cast<int>(time_ * 20.0f) & 1;  // the propellers
  for (const Strike& s : sim_->strikes()) {
    const std::string& faction = players_[s.owner % MAX_PLAYERS].faction;
    const uint32_t team = teamColor(s.owner);
    const float dx = static_cast<float>(s.to.x - s.from.x), dy = static_cast<float>(s.to.y - s.from.y);
    const float len = std::max(1.0f, std::hypot(dx, dy));
    const float yaw = std::atan2(dy, dx);
    const int facing = facingTo(dx, dy);
    if (s.kind == SupportKind::Naval) {
      // The ship lies broadside to the shore while its guns fire.
      const UnitArt* art = artNamed(art_, "warship", faction);
      if (!art || now < s.startTick - 15.0 * 4) continue;
      const int side = facingTo(-dy, dx);
      put(art->body.frame(side, 0), static_cast<float>(s.from.x), static_cast<float>(s.from.y), water, team, kWhite,
          iso::depthKey(s.from.x, s.from.y), yaw + 1.5708f);
      continue;
    }
    if (s.kind == SupportKind::Landing) {
      // In to the beach, twelve seconds there, and back out.
      const UnitArt* art = artNamed(art_, "landing_boat", faction);
      if (!art) continue;
      const double run = len / std::max(1, s.speed);
      const double t = now - s.startTick;
      double k = t < 0 ? 0 : t < run ? t / run : t < run + 180 ? 1.0 : std::max(0.0, 1.0 - (t - run - 180) / run);
      const bool back = t >= run + 180;
      for (int b = -1; b <= 1; ++b) {
        const float off = b * 180.0f;
        const float lx = s.from.x + dx * static_cast<float>(k) - dy / len * off - dx / len * std::abs(b) * 120.0f;
        const float ly = s.from.y + dy * static_cast<float>(k) + dx / len * off - dy / len * std::abs(b) * 120.0f;
        put(art->body.frame(back ? (facing + 8) & 15 : facing, back ? 0 : anim), lx, ly, std::max(water, groundH(lx, ly)), team, kWhite,
            iso::depthKey(static_cast<int32_t>(lx), static_cast<int32_t>(ly)), back ? yaw + 3.14159f : yaw);
      }
      continue;
    }
    const UnitArt* art = artNamed(art_, modelFor(s.aircraft), faction);
    if (!art) continue;
    const double toTarget = std::hypot(static_cast<double>(s.target.x - s.from.x), static_cast<double>(s.target.y - s.from.y));
    for (int i = 0; i < s.planes; ++i) {
      if (!(s.alive & (1u << i))) continue;
      int32_t x, y;
      if (!s.planeAt(i, now, x, y)) continue;
      const float lx = static_cast<float>(x), ly = static_cast<float>(y);
      // How high: ground-attack planes dive at the target, a plane coming down goes all the way.
      float alt = kAltitude;
      const double along = (now - s.startTick) * s.speed;
      const double fromTarget = std::abs(along - toTarget);
      if (s.kind == SupportKind::Attack || s.kind == SupportKind::Strafing)
        alt *= static_cast<float>(0.45 + 0.55 * std::min(1.0, fromTarget / (8.0 * LEPTONS_PER_TILE)));
      if (s.kind == SupportKind::Crash) {
        if (along > toTarget) continue;
        alt *= static_cast<float>(std::min(1.0, (toTarget - along) / (14.0 * LEPTONS_PER_TILE)));
      }
      const float gh = groundH(lx, ly);
      const int frame = art->body.frame(facing, anim);
      // The shadow, on the ground below and a little to the side the sun leaves it.
      const float sx = lx + alt * 0.9f, sy = ly + alt * 0.3f;
      put(frame, sx, sy, groundH(sx, sy), 0, rgba8(0, 0, 0, 52), kUnder, yaw);
      put(frame, lx, ly, gh + alt, team, s.aborted ? rgba8(220, 210, 200) : kWhite, kSky + order, yaw);
    }
  }
  // Crates under their parachutes, then on the ground waiting for someone.
  const UnitArt* chute = artNamed(art_, "parachute", "ger");
  const auto& crateArt = art_.props[static_cast<size_t>(PropKind::Crates)];
  for (const SupplyCrate& c : sim_->crates()) {
    const float lx = static_cast<float>(c.at.x), ly = static_cast<float>(c.at.y), gh = groundH(lx, ly);
    const double left = static_cast<double>(c.landTick) - now;
    const float h = left > 0 ? gh + static_cast<float>(left / 45.0) * kAltitude * 0.8f : gh;
    if (fogEnabled() && terrain_.fogAt(c.at.x >> LEPTON_SHIFT, c.at.y >> LEPTON_SHIFT) == 0) continue;
    if (!crateArt.empty()) put(crateArt[0], lx, ly, h, kWhite, kWhite, left > 0 ? kSky : iso::depthKey(c.at.x, c.at.y), 0);
    if (left > 0 && chute) put(chute->body.frame(0, 0), lx, ly, h, kWhite, kWhite, kSky + 1, 0);
  }
  // Paratroopers coming down.
  for (const Chute& c : chutes_) {
    const double t = (now - c.start) / 45.0;
    if (t < 0 || t >= 1) continue;
    const float gh = groundH(c.at.x, c.at.y);
    const float h = gh + static_cast<float>(1.0 - t) * kAltitude * 0.8f;
    if (c.manFrame >= 0) put(c.manFrame, c.at.x, c.at.y, h, c.team, kWhite, kSky, 0);
    if (chute) put(chute->body.frame(0, 0), c.at.x, c.at.y, h, kWhite, kWhite, kSky + 1, 0);
  }
}

void GameScene::updateCallIns() {
  if (!sim_) return;
  const uint32_t now = sim_->tickCount();
  std::erase_if(chutes_, [&](const Chute& c) { return now > c.start + 46; });
  // Planes passing overhead and bombs whistling down: started when the moment comes.
  for (const Strike& s : sim_->strikes()) {
    if (!supportUsesAircraft(s.kind) || s.alive == 0) continue;
    const double toTarget = std::hypot(static_cast<double>(s.target.x - s.from.x), static_cast<double>(s.target.y - s.from.y));
    const double over = s.startTick + toTarget / std::max(1, s.speed);  // the leader over the target
    const bool heavy = s.aircraft == AircraftRole::Bomber || s.aircraft == AircraftRole::Transport;
    const audio::Emitter at{true, s.target.x / static_cast<float>(LEPTONS_PER_TILE), s.target.y / static_cast<float>(LEPTONS_PER_TILE), 14.0f};
    if (!heardStrikes_.count(s.id) && now + (heavy ? 50 : 35) >= over) {
      heardStrikes_.insert(s.id);
      if (audio_) audio_->play(heavy ? "plane_heavy" : "plane_fighter", at);
    }
    if (s.kind == SupportKind::Bombing && !s.aborted && !bombsHeard_.count(s.id) && s.power >= 0) {
      const double first = over - sim_->support().get(s.power).length / 2.0 / std::max(1, s.speed);
      if (now + 2 >= first) {
        bombsHeard_.insert(s.id);
        if (audio_) audio_->play("bomb_fall", at);
      }
    }
  }
  if (heardStrikes_.size() > 256) heardStrikes_.clear();
  if (bombsHeard_.size() > 256) bombsHeard_.clear();
  // Planes reaching the ground: a fireball and a burning wreck.
  for (const DownedPlane& p : sim_->downedPlanes()) {
    const uint64_t key = (static_cast<uint64_t>(p.tick) << 32) ^ (static_cast<uint64_t>(p.at.x) << 16) ^ static_cast<uint64_t>(p.at.y);
    if (now < p.tick || crashed_.count(key)) continue;
    crashed_.insert(key);
    int frame = -1;
    if (const UnitArt* art = artNamed(art_, modelFor(p.role), players_[p.owner % MAX_PLAYERS].faction))
      frame = art->body.frame(static_cast<int>(key % 16), 0);
    fx_.planeCrash(p.at, frame, teamColor(p.owner), watching(p.at.x, p.at.y));
    if (audio_)
      audio_->play("explosion_large", audio::Emitter{true, p.at.x / static_cast<float>(LEPTONS_PER_TILE),
                                                      p.at.y / static_cast<float>(LEPTONS_PER_TILE), 14.0f});
  }
  if (crashed_.size() > 512) crashed_.clear();
}

void GameScene::callInEvent(const SimEvent& ev) {
  switch (ev.type) {
    case EventType::ChutesOpen: {
      // The men under their canopies, spread where they jump.
      const int rifle = types_.find(players_[ev.owner % MAX_PLAYERS].faction + "_rifleman");
      const int manFrame = rifle >= 0 ? unitArt_[static_cast<size_t>(rifle)].body.frame(4, 0) : -1;
      const float r = static_cast<float>(ev.weapon);
      for (int k = 0; k < std::min(ev.amount, 24); ++k) {
        Chute c;
        const float a = (k * 2.39996f), d = r * std::sqrt((k + 0.5f) / std::max(1, ev.amount));
        c.at = {ev.at.x + std::cos(a) * d, ev.at.y + std::sin(a) * d};
        c.start = sim_->tickCount() + static_cast<uint32_t>(k % 4);
        c.manFrame = manFrame;
        c.team = teamColor(ev.owner);
        chutes_.push_back(c);
      }
      break;
    }
    case EventType::SupportCalled:
      if (ev.owner != player_ || mode_ != Mode::Game) break;
      if (ev.unitType < sim_->support().size()) {
        const SupportPowerDef& d = sim_->support().get(ev.unitType);
        if (ev.amount) showStatus(d.name + ": on the way", 2.5f);
        else {
          std::string why;
          sim_->canCallSupport(player_, ev.unitType, ev.at, &why);
          showStatus(d.name + ": " + (why.empty() ? std::string("not now") : why), 3);
        }
      }
      break;
    case EventType::SupportReady:
      if (ev.owner == player_ && mode_ == Mode::Game && ev.unitType < sim_->support().size())
        showStatus(sim_->support().get(ev.unitType).name + " ready", 2.5f);
      break;
    case EventType::CrateTaken:
      if (ev.owner == player_ || revealMap_) {
        char txt[32];
        std::snprintf(txt, sizeof txt, "+$%d", ev.amount);
        floaters_.push_back({{static_cast<float>(ev.at.x), static_cast<float>(ev.at.y)}, 0, txt});
      }
      break;
    case EventType::TroopsLanded:
      if (ev.owner == player_ && mode_ == Mode::Game && audio_) audio_->say("ready");
      break;
    default:
      break;
  }
}

}  // namespace legion

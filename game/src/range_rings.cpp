// Range circles (user's request, 13 Nov 2026): a circle on the ground showing
// how far a unit's guns reach. Selected units and the unit under the mouse
// show theirs (setting 1, the default); setting 2 shows every unit in view;
// T cycles. Enemy circles appear only for units in sight right now. Scouts
// also show their sight as a fainter, dotted circle, since watching is their
// job. A defensive building being placed shows the reach it will have.
//
// The circle follows the ground (each point is projected at the ground's
// height), so it works the same in the 3D and the classic view. Presentation
// only: nothing here touches the simulation.

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "game_scene.hpp"
#include "sidebar.hpp"

namespace legion {
using namespace pioneer;

namespace {
constexpr float kTwoPi = 6.28318530718f;

ImU32 withAlpha(ImU32 c, float a) {
  const int base = static_cast<int>((c >> IM_COL32_A_SHIFT) & 0xFF);
  return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(base * a, 0.0f, 255.0f)) << IM_COL32_A_SHIFT);
}
}  // namespace

void GameScene::drawRangeRings(ImDrawList* dl) {
  if (settings_.rangeRings <= 0 && sidebar_->placingType() < 0) return;
  Registry& reg = sim_->registry();
  const float ps = app_.pixelScale();
  const float vw = static_cast<float>(app_.pixelWidth()) / ps, vh = static_cast<float>(app_.pixelHeight()) / ps;
  auto toScreen = [&](float lx, float ly) {
    lx = std::clamp(lx, 0.0f, map_.widthLeptons() - 1.0f);
    ly = std::clamp(ly, 0.0f, map_.heightLeptons() - 1.0f);
    const Vec2f s = cam_.worldToScreen(groundToScreenWorld(lx, ly));
    return ImVec2(s.x / ps, s.y / ps);
  };

  static const ImU32 kMine = IM_COL32(150, 236, 110, 200);
  static const ImU32 kAlly = IM_COL32(120, 190, 255, 190);
  static const ImU32 kFoe = IM_COL32(255, 96, 72, 210);
  std::vector<ImVec2> pts;

  // One circle of `radius` leptons round (cx, cy). `strength` fades a crowd
  // of circles (every unit in view) down so the battlefield stays readable.
  auto ring = [&](float cx, float cy, float radius, ImU32 col, float strength, bool dotted) {
    if (radius <= 0) return;
    // Skip circles wholly off screen: test the centre and four points of the rim.
    bool near = false;
    for (int k = 0; k < 5 && !near; ++k) {
      const float a = k * kTwoPi / 4;
      const ImVec2 s = k == 4 ? toScreen(cx, cy) : toScreen(cx + std::cos(a) * radius, cy + std::sin(a) * radius);
      near = s.x > -200 && s.y > -200 && s.x < vw + 200 && s.y < vh + 200;
    }
    if (!near) return;
    const int n = std::clamp(static_cast<int>(radius / 256.0f * 5.0f), 40, 160);
    pts.resize(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
      const float a = i * kTwoPi / n;
      pts[static_cast<size_t>(i)] = toScreen(cx + std::cos(a) * radius, cy + std::sin(a) * radius);
    }
    if (dotted) {
      for (int i = 0; i < n; i += 2)
        dl->AddLine(pts[static_cast<size_t>(i)], pts[static_cast<size_t>((i + 1) % n)], withAlpha(col, 0.55f * strength), 1.2f);
      return;
    }
    dl->AddConvexPolyFilled(pts.data(), n, withAlpha(col, 0.07f * strength));
    // A dark under-stroke keeps the line readable on snow and sand.
    dl->AddPolyline(pts.data(), n, IM_COL32(0, 0, 0, static_cast<int>(70 * strength)), ImDrawFlags_Closed, 3.0f);
    dl->AddPolyline(pts.data(), n, withAlpha(col, strength), ImDrawFlags_Closed, 1.6f);
  };

  // Where a unit or building stands, and the colour its circle takes.
  auto centreOf = [&](Entity e, float& cx, float& cy) {
    if (const CStructure* st = reg.tryGet<CStructure>(e)) {
      cx = (st->tx + st->w * 0.5f) * 256.0f;
      cy = (st->ty + st->h * 0.5f) * 256.0f;
      return;
    }
    const CPosition& p = reg.get<CPosition>(e);
    const CPrevPosition* q = reg.tryGet<CPrevPosition>(e);
    cx = q ? q->x + (p.x - q->x) * alpha_ : static_cast<float>(p.x);
    cy = q ? q->y + (p.y - q->y) * alpha_ : static_cast<float>(p.y);
  };
  auto colourOf = [&](int owner, ImU32& col) {
    if (owner < 0 || owner >= MAX_PLAYERS) return false;
    col = owner == player_ ? kMine : sim_->allied(owner, player_) ? kAlly : kFoe;
    return true;
  };
  auto unitRing = [&](Entity e, float strength) {
    if (!reg.alive(e) || !reg.has<CUnit>(e)) return;
    const CUnit& u = reg.get<CUnit>(e);
    if (u.health <= 0) return;
    ImU32 col;
    if (!colourOf(u.owner, col)) return;
    // Men inside a building fire from it; their own circles would only clutter.
    if (reg.has<CGarrisoned>(e) && !sim_->inTrench(e)) return;
    float cx = 0, cy = 0;
    centreOf(e, cx, cy);
    if (!sim_->allied(u.owner, player_) && !watching(static_cast<int32_t>(cx), static_cast<int32_t>(cy))) return;
    const UnitTypeDef& d = types_.get(u.type);
    ring(cx, cy, static_cast<float>(sim_->maxRange(d)), col, strength, false);
    if (d.scout) ring(cx, cy, static_cast<float>(d.sight), col, strength, true);
  };

  if (settings_.rangeRings == 2) {
    // Every unit in view, drawn lighter; the selection on top at full strength.
    auto& units = reg.pool<CUnit>();
    for (uint32_t idx : units.entities()) {
      const Entity e = reg.handle(idx);
      if (std::find(selection_.begin(), selection_.end(), e) != selection_.end()) continue;
      unitRing(e, 0.45f);
    }
  }
  if (settings_.rangeRings >= 1) {
    for (Entity e : selection_) unitRing(e, 1.0f);
    if (hovered_.valid() && std::find(selection_.begin(), selection_.end(), hovered_) == selection_.end())
      unitRing(hovered_, 0.85f);
  }

  // A gun pit, bunker or nest being placed: the reach it will have from there.
  const int placing = sidebar_->placingType();
  if (placing >= 0) {
    const UnitTypeDef& d = types_.get(placing);
    const int32_t reach = sim_->maxRange(d);
    if (reach > 0) {
      const Vec2i c = sidebar_->snappedCentre(placing, groundAt(mouse_));
      ring(static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(reach), kMine, 0.8f, false);
    }
  }
}

}  // namespace legion

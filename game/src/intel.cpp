// Legion Classic - intel instead of fog of war (Milestone 9.3).
//
// The whole map is always in view: ground, trees, villages, the lie of the
// land. What the enemy has is not. Enemy units show as themselves only where
// one of our side's units can see them; everywhere else their forces are
// gathered into chips, counters on a war-room table, standing roughly where
// they are: how many, roughly, but not what. Within earshot (twice a unit's
// sight) a chip also shows the kind of force. Enemy buildings show as chips
// too until one of ours has seen them; after that they stay on the map,
// dimmed while out of sight.

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "game_scene.hpp"
#include "pioneer/core/rng.hpp"

using namespace pioneer;

namespace legion {

uint8_t GameScene::intelAt(int tx, int ty) const {
  if (!fogEnabled()) return 255;
  return sim_->vision().visible(player_, tx, ty) ? 255 : 0;
}

bool GameScene::structureKnown(uint32_t e) const {
  if (!fogEnabled()) return true;
  Registry& reg = sim_->registry();
  const CUnit* u = reg.pool<CUnit>().tryGet(e);
  if (!u || u->owner >= MAX_PLAYERS || sim_->allied(u->owner, player_)) return true;  // ours, allies', the villages
  const auto it = spotted_.find(e);
  return it != spotted_.end() && it->second == reg.handle(e).generation;
}

// Remembers every enemy building our side has had in sight.
void GameScene::updateIntel() {
  Registry& reg = sim_->registry();
  const Vision& v = sim_->vision();
  if (spottedFor_ != player_) {
    // A new side (or a loaded game): what its scouts had explored is known.
    spotted_.clear();
    spottedFor_ = player_;
    for (uint32_t e : reg.pool<CStructure>().entities()) {
      const CStructure& st = reg.pool<CStructure>().get(e);
      if (v.explored(player_, st.tx + st.w / 2, st.ty + st.h / 2)) spotted_[e] = reg.handle(e).generation;
    }
  }
  if (frame_ % 10 != 0) return;
  for (uint32_t e : reg.pool<CStructure>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner >= MAX_PLAYERS || sim_->allied(u.owner, player_)) continue;
    const CStructure& st = reg.pool<CStructure>().get(e);
    bool seen = false;
    for (int y = st.ty; y < st.ty + st.h && !seen; ++y)
      for (int x = st.tx; x < st.tx + st.w && !seen; ++x) seen = v.visible(player_, x, y);
    if (seen) spotted_[e] = reg.handle(e).generation;
  }
  // Forget what has gone (indices are reused).
  if (frame_ % 300 == 0)
    std::erase_if(spotted_, [&](const auto& kv) { return !reg.alive(Entity{kv.first, kv.second}); });
}

// Every so often: the enemy forces our side can't see, gathered into chips by
// area. A chip's size says roughly how many; within earshot of our side its
// symbol says what the most valuable thing among them is; buildings not yet
// seen are square counters.
void GameScene::drawChips(ImDrawList* dl, const std::function<ImVec2(Vec2f)>& toScreen) {
  Registry& reg = sim_->registry();
  constexpr int kCell = 4;    // detection grid, tiles
  constexpr int kBucket = 8;  // chip gathering area, tiles (units)
  constexpr int kBase = 16;   // ... and for buildings: a base is a counter or two
  const int gw = map_.width() / kCell + 1, gh = map_.height() / kCell + 1;
  if (--chipTimer_ <= 0 || static_cast<int>(detect_.size()) != gw * gh) {
    chipTimer_ = 15;
    detect_.assign(static_cast<size_t>(gw) * gh, 0);
    auto& units = reg.pool<CUnit>();
    for (uint32_t e : units.entities()) {
      const CUnit& u = units.get(e);
      if (u.owner >= MAX_PLAYERS || !sim_->allied(u.owner, player_)) continue;
      const UnitTypeDef& d = types_.get(u.type);
      if (d.category == UnitCategory::Fortification) continue;
      const CPosition& p = reg.pool<CPosition>().get(e);
      const int r = (d.sight * 2 / LEPTONS_PER_TILE) / kCell + 1;
      const int cx = (p.x >> LEPTON_SHIFT) / kCell, cy = (p.y >> LEPTON_SHIFT) / kCell;
      for (int y = std::max(0, cy - r); y <= std::min(gh - 1, cy + r); ++y)
        for (int x = std::max(0, cx - r); x <= std::min(gw - 1, cx + r); ++x)
          if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) detect_[static_cast<size_t>(y) * gw + x] = 1;
    }
    chips_.clear();
    std::map<std::tuple<int, int, int>, Chip> buckets;  // (bucket x, y, kind)
    for (uint32_t e : units.entities()) {
      const CUnit& u = units.get(e);
      if (u.owner >= MAX_PLAYERS || sim_->allied(u.owner, player_) || u.health <= 0) continue;
      const UnitTypeDef& d = types_.get(u.type);
      if (reg.pool<CGarrisoned>().has(e) && !sim_->inTrench(reg.handle(e))) continue;  // counted with the building
      const CPosition& p = reg.pool<CPosition>().get(e);
      const int tx = p.x >> LEPTON_SHIFT, ty = p.y >> LEPTON_SHIFT;
      const bool structure = d.isStructure();
      if (structure ? structureKnown(e) : sim_->vision().visible(player_, tx, ty)) {
        continue;  // shown as itself (or, hidden in woodland, not at all)
      }
      if (!structure && sim_->concealed(e)) continue;  // men in the woods: no chip either, until a scout looks
      if (structure && d.category == UnitCategory::Fortification && d.footW * d.footH < 2) continue;  // wire, trenches
      const int b = structure ? kBase : kBucket;
      Chip& c = buckets[{tx / b, ty / b, structure ? 1 : 0}];
      c.x += p.x;
      c.y += p.y;
      ++c.count;
      c.owner = u.owner;
      c.structures = structure;
      c.identified = c.identified || detect_[static_cast<size_t>(ty / kCell) * gw + tx / kCell];
      if (d.cost > c.bestCost) {
        c.bestCost = d.cost;
        c.nato = d.nato;
      }
    }
    for (auto& [key, c] : buckets) {
      // Only roughly where: halfway between the middle of the force and the
      // middle of its area, nudged by a fixed amount per area.
      const int bx = std::get<0>(key), by = std::get<1>(key), bs = std::get<2>(key) ? kBase : kBucket;
      const int64_t mx = (bx * bs + bs / 2) * int64_t{LEPTONS_PER_TILE}, my = (by * bs + bs / 2) * int64_t{LEPTONS_PER_TILE};
      const uint32_t h = hash2(bx, by, 41);
      c.x = (c.x / c.count + mx) / 2 + static_cast<int64_t>(h % 384) - 192;
      c.y = (c.y / c.count + my) / 2 + static_cast<int64_t>((h >> 9) % 384) - 192;
      chips_.push_back(c);
    }
  }
  for (const Chip& c : chips_) {
    const float lx = std::clamp(static_cast<float>(c.x), 0.0f, map_.widthLeptons() - 1.0f);
    const float ly = std::clamp(static_cast<float>(c.y), 0.0f, map_.heightLeptons() - 1.0f);
    const ImVec2 s = toScreen(groundToScreenWorld(lx, ly));
    const uint32_t team = teamColor(c.owner);
    // In 3D, far chips are smaller.
    const float k = cam_.is3D() ? std::clamp(cam_.scaleAt(worldPoint(lx, ly, map_.heightAt(lx, ly))), 0.65f, 1.2f) : 1.0f;
    // Rough size only: a handful, a platoon or so, a company, more.
    const float r = (c.count < 5 ? 12.0f : c.count < 20 ? 16.0f : c.count < 100 ? 21.0f : 27.0f) * k;
    const int layers = c.count < 5 ? 1 : c.count < 20 ? 2 : c.count < 100 ? 3 : 4;
    const ImU32 edge = IM_COL32(230, 220, 190, c.identified ? 210 : 150);
    for (int i = layers - 1; i >= 0; --i) {
      const ImVec2 at(s.x, s.y - i * 3.0f);
      const ImU32 face = IM_COL32((team & 0xFF) * (i == 0 ? 3 : 2) / 4, ((team >> 8) & 0xFF) * (i == 0 ? 3 : 2) / 4,
                                  ((team >> 16) & 0xFF) * (i == 0 ? 3 : 2) / 4, c.identified ? 235 : 190);
      if (c.structures) {
        // Buildings: square counters.
        dl->AddRectFilled(ImVec2(at.x - r + 2, at.y - r * 0.6f + 2), ImVec2(at.x + r + 2, at.y + r * 0.6f + 2), IM_COL32(0, 0, 0, 120), 2);
        dl->AddRectFilled(ImVec2(at.x - r, at.y - r * 0.6f), ImVec2(at.x + r, at.y + r * 0.6f), face, 2);
        dl->AddRect(ImVec2(at.x - r, at.y - r * 0.6f), ImVec2(at.x + r, at.y + r * 0.6f), edge, 2, 0, 1.5f);
      } else {
        dl->AddEllipseFilled(ImVec2(at.x, at.y + 2), ImVec2(r, r * 0.55f), IM_COL32(0, 0, 0, 120));
        dl->AddEllipseFilled(at, ImVec2(r, r * 0.55f), face);
        dl->AddEllipse(at, ImVec2(r, r * 0.55f), edge, 0, 0, 1.5f);
      }
    }
    const ImVec2 top(s.x, s.y - (layers - 1) * 3.0f);
    if (c.identified && !c.structures) {
      drawNatoSymbol(dl, top, r / 18.0f, c.nato, Echelon::None, team, 1.0f, true);
    } else {
      const char* mark = c.structures ? "HQ?" : "?";
      const ImVec2 ms = ImGui::CalcTextSize(mark);
      dl->AddText(ImVec2(top.x - ms.x / 2, top.y - ms.y / 2), IM_COL32(240, 232, 205, 235), mark);
    }
    if (k < 0.8f) continue;  // far off: the counter alone
    const char* q = c.count < 5 ? "few" : c.count < 20 ? "some" : c.count < 100 ? "many" : "a host";
    if (c.structures) q = c.count < 3 ? "building" : "buildings";
    const ImVec2 ts = ImGui::CalcTextSize(q);
    dl->AddText(ImVec2(top.x - ts.x / 2, top.y + r * 0.6f + 1), IM_COL32(235, 225, 195, 200), q);
  }
}

}  // namespace legion

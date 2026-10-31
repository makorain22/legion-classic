// Legion Classic - what floats over the battlefield: NATO cards over bound
// units, the hover readout, garrison badges, supply point labels, and the
// R.U.S.E-style chips that stand in for enemy forces detected in the fog.
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "editor.hpp"
#include "game_scene.hpp"
#include "slave/core/rng.hpp"

using namespace slave;

namespace legion {

namespace {
const char* kPhonetic[] = {"Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel", "India", "Juliett",
                           "Kilo", "Lima", "Mike", "November", "Oscar", "Papa", "Quebec", "Romeo", "Sierra", "Tango",
                           "Uniform", "Victor", "Whiskey", "X-ray", "Yankee", "Zulu"};

std::string ordinal(int n) {
  const int m100 = n % 100, m10 = n % 10;
  const char* suf = (m100 >= 11 && m100 <= 13) ? "th" : m10 == 1 ? "st" : m10 == 2 ? "nd" : m10 == 3 ? "rd" : "th";
  return std::to_string(n) + suf;
}

ImU32 imcol(uint32_t rgba, float alpha = 1.0f) {
  const uint8_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF;
  return IM_COL32(r, g, b, static_cast<int>(alpha * 255));
}

ImU32 darker(uint32_t rgba, float k, float alpha) {
  const uint8_t r = rgba & 0xFF, g = (rgba >> 8) & 0xFF, b = (rgba >> 16) & 0xFF;
  return IM_COL32(static_cast<int>(r * k), static_cast<int>(g * k), static_cast<int>(b * k), static_cast<int>(alpha * 255));
}
}  // namespace

std::string GameScene::groupName(const CGroup& g) const {
  const int n = g.number;
  if (g.combined) {
    // Infantry and armour together.
    switch (g.echelon) {
      case Echelon::Platoon: return ordinal(n) + " Mechanised Platoon";
      case Echelon::Company: return ordinal(n) + " Battle Group";
      case Echelon::Battalion: return ordinal(n) + " Armoured Regiment";
      default: return ordinal(n) + " Battle Group";
    }
  }
  if (g.vehicles) {
    // Named for what they are: tanks, reconnaissance, guns...
    const std::string role = groupSymbol(g);
    const char* kind = role == "recon" ? "Reconnaissance" : role == "artillery" ? "Artillery"
                       : role == "antitank_armour" ? "Tank Destroyer" : role == "mech" ? "Mechanised"
                       : role == "hq" ? "Engineer" : "Armoured";
    const bool guns = role == "artillery";
    switch (g.echelon) {
      case Echelon::Platoon: return ordinal(n) + " " + kind + (guns ? " Battery" : " Platoon");
      case Echelon::Company: return ordinal(n) + " " + kind + (guns ? " Battalion" : " Company");
      case Echelon::Battalion: return ordinal(n) + " " + kind + (guns ? " Regiment" : " Battalion");
      default: return ordinal(n) + " " + kind + " Detachment";
    }
  }
  switch (g.echelon) {
    case Echelon::Fireteam: return std::string("Fireteam ") + kPhonetic[(n - 1) % 26] + (n > 26 ? " " + std::to_string((n - 1) / 26 + 1) : "");
    case Echelon::Squad: return ordinal(n) + " Squad";
    case Echelon::Platoon: return ordinal(n) + " Platoon";
    case Echelon::Company: return ordinal(n) + " Company";
    case Echelon::Battalion: return ordinal(n) + " Battalion";
    default: return "Detachment";
  }
}

// Draws a NATO-style unit symbol: a frame in the side's colour with the
// branch mark inside and the size mark above. `scale` 1 = 26 x 16 pixels.
void GameScene::drawNatoSymbol(ImDrawList* dl, ImVec2 c, float scale, const std::string& nato, Echelon e, uint32_t team,
                               float alpha, bool hostile) {
  const float w = 13 * scale, h = 8 * scale;
  const ImVec2 a(c.x - w, c.y - h), b(c.x + w, c.y + h);
  const ImU32 fill = darker(team, 0.55f, 0.85f * alpha), line = IM_COL32(235, 228, 205, static_cast<int>(245 * alpha));
  const float t = std::max(1.0f, 1.3f * scale);
  if (hostile) {  // enemies: a diamond, as on NATO maps
    const ImVec2 pts[4] = {ImVec2(c.x, c.y - h * 1.35f), ImVec2(c.x + w * 1.1f, c.y), ImVec2(c.x, c.y + h * 1.35f), ImVec2(c.x - w * 1.1f, c.y)};
    dl->AddConvexPolyFilled(pts, 4, fill);
    dl->AddPolyline(pts, 4, line, ImDrawFlags_Closed, t);
  } else {
    dl->AddRectFilled(a, b, fill);
    dl->AddRect(a, b, line, 0, 0, t);
  }
  const float iw = w * 0.8f, ih = h * 0.8f;
  auto cross = [&]() {
    dl->AddLine(ImVec2(c.x - iw, c.y - ih), ImVec2(c.x + iw, c.y + ih), line, t);
    dl->AddLine(ImVec2(c.x - iw, c.y + ih), ImVec2(c.x + iw, c.y - ih), line, t);
  };
  auto oval = [&]() { dl->AddEllipse(c, ImVec2(iw * 0.75f, ih * 0.65f), line, 0, 0, t); };
  if (nato == "infantry") cross();
  else if (nato == "mg") {
    cross();
    dl->AddLine(ImVec2(c.x, c.y - ih), ImVec2(c.x, c.y + ih), line, t);
  } else if (nato == "antitank") {
    dl->AddLine(ImVec2(c.x - iw, c.y + ih), ImVec2(c.x, c.y - ih), line, t);
    dl->AddLine(ImVec2(c.x, c.y - ih), ImVec2(c.x + iw, c.y + ih), line, t);
  } else if (nato == "antitank_armour") {
    oval();
    dl->AddLine(ImVec2(c.x - iw * 0.6f, c.y + ih * 0.5f), ImVec2(c.x, c.y - ih * 0.5f), line, t);
    dl->AddLine(ImVec2(c.x, c.y - ih * 0.5f), ImVec2(c.x + iw * 0.6f, c.y + ih * 0.5f), line, t);
  } else if (nato == "engineer") {
    dl->AddLine(ImVec2(c.x - iw, c.y - ih * 0.3f), ImVec2(c.x + iw, c.y - ih * 0.3f), line, t);
    for (float x : {-iw, 0.0f, iw}) dl->AddLine(ImVec2(c.x + x, c.y - ih * 0.3f), ImVec2(c.x + x, c.y + ih * 0.6f), line, t);
  } else if (nato == "sniper") {
    cross();
    dl->AddCircle(c, ih * 0.45f, line, 0, t);
  } else if (nato == "recon") {
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), line, t);
  } else if (nato == "mech") {
    cross();
    oval();
  } else if (nato == "armour") {
    oval();
  } else if (nato == "artillery") {
    dl->AddCircleFilled(c, ih * 0.4f, line);
  } else if (nato == "hq") {
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(a.x, b.y + h * 0.9f), line, t);
  }
  // Size above the frame: Ø team, • squad, ••• platoon, | company, || battalion.
  const float y = (hostile ? c.y - h * 1.35f : a.y) - 4 * scale;
  const float r = 1.6f * scale;
  switch (e) {
    case Echelon::Fireteam:
      dl->AddCircle(ImVec2(c.x, y), r * 1.5f, line, 0, t * 0.8f);
      dl->AddLine(ImVec2(c.x - r * 2, y + r * 2), ImVec2(c.x + r * 2, y - r * 2), line, t * 0.8f);
      break;
    case Echelon::Squad: dl->AddCircleFilled(ImVec2(c.x, y), r, line); break;
    case Echelon::Platoon:
      for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(ImVec2(c.x + i * r * 3, y), r, line);
      break;
    case Echelon::Company: dl->AddLine(ImVec2(c.x, y - r * 2), ImVec2(c.x, y + r * 1.5f), line, t * 1.2f); break;
    case Echelon::Battalion:
      for (int i : {-1, 1}) dl->AddLine(ImVec2(c.x + i * r * 1.4f, y - r * 2), ImVec2(c.x + i * r * 1.4f, y + r * 1.5f), line, t * 1.2f);
      break;
    default: break;
  }
}

// The symbol for a group: its most common kind of unit.
std::string GameScene::groupSymbol(const CGroup& g) const {
  if (g.combined) return "mech";  // infantry and armour: the mechanised symbol
  std::map<std::string, int> count;
  Registry& reg = sim_->registry();
  for (Entity m : g.members)
    if (reg.alive(m)) ++count[types_.get(reg.get<CUnit>(m).type).nato];
  std::string best;
  int n = -1;
  for (const auto& [k, v] : count)
    if (v > n) {
      best = k;
      n = v;
    }
  return best;
}

void GameScene::drawWorldOverlays() {
  if (mode_ == Mode::Menu) return;
  Registry& reg = sim_->registry();
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const float ps = app_.pixelScale();
  const bool fog = fogEnabled();
  const float zoom = cam_.zoom();
  const float vw = static_cast<float>(app_.pixelWidth()), vh = static_cast<float>(app_.pixelHeight());
  auto toScreen = [&](Vec2f world) {
    const Vec2f s = cam_.worldToScreen(world);
    return ImVec2(s.x / ps, s.y / ps);
  };
  auto onScreen = [&](ImVec2 p) { return p.x > -60 && p.y > -60 && p.x < vw / ps + 60 && p.y < vh / ps + 60; };
  // Nothing drawn under the sidebar.
  dl->PushClipRect(ImVec2(0, 0), ImVec2((vw - Sidebar::kWidth * ps) / ps, vh / ps), true);
  struct PopClip {
    ImDrawList* d;
    ~PopClip() { d->PopClipRect(); }
  } popClip{dl};

  // --- Placement grid ----------------------------------------------------------
  // While placing a building, the tiles around the cursor are outlined, so
  // bases and fortifications line up with the ground's shape.
  if (sidebar_->placingType() >= 0 || editor_->placingStructure()) {
    const Vec2f g = groundAt(mouse_);
    const int mx = static_cast<int>(g.x) >> LEPTON_SHIFT, my = static_cast<int>(g.y) >> LEPTON_SHIFT;
    constexpr int R = 9;
    for (int y = my - R; y <= my + R + 1; ++y)
      for (int x = mx - R; x <= mx + R + 1; ++x) {
        if (x < 0 || y < 0 || x > map_.width() || y > map_.height()) continue;
        const float d = std::sqrt(static_cast<float>((x - mx) * (x - mx) + (y - my) * (y - my))) / R;
        if (d > 1.0f) continue;
        const float a = (1.0f - d) * 0.55f;
        const ImVec2 p = toScreen(groundToScreenWorld(x * 256.0f, y * 256.0f));
        const bool buildable = map_.buildable(std::min(x, map_.width() - 1), std::min(y, map_.height() - 1));
        const ImU32 col = buildable ? IM_COL32(230, 225, 190, static_cast<int>(a * 255)) : IM_COL32(230, 90, 70, static_cast<int>(a * 255));
        if (x + 1 <= map_.width()) dl->AddLine(p, toScreen(groundToScreenWorld((x + 1) * 256.0f, y * 256.0f)), col, 1.0f);
        if (y + 1 <= map_.height()) dl->AddLine(p, toScreen(groundToScreenWorld(x * 256.0f, (y + 1) * 256.0f)), col, 1.0f);
      }
  }

  // --- Supply points ---------------------------------------------------------
  for (size_t i = 0; i < map_.supplyPoints.size(); ++i) {
    const Vec2i sp = map_.supplyPoints[i];
    if (fog && terrain_.fogAt(sp.x, sp.y) == 0) continue;
    const ImVec2 c = toScreen(groundToScreenWorld(sp.x * 256.0f + 128, sp.y * 256.0f + 128));
    if (!onScreen(c)) continue;
    const Entity depot = sim_->depotOn(static_cast<int>(i));
    const uint32_t col = depot.valid() ? teamColor(reg.get<CUnit>(depot).owner) : rgba8(214, 196, 120);
    const float r = 13 * zoom;
    dl->AddEllipse(c, ImVec2(r * 1.6f, r * 0.8f), imcol(col, 0.85f), 0, 0, 2.0f);
    if (!depot.valid()) {
      const char* label = "SUPPLY";
      const ImVec2 ts = ImGui::CalcTextSize(label);
      dl->AddText(ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), imcol(col, 0.95f), label);
    }
  }

  // --- Garrison badges -----------------------------------------------------------
  for (uint32_t b : reg.pool<CGarrison>().entities()) {
    const CGarrison& g = reg.pool<CGarrison>().get(b);
    if (g.occupants.empty() || !reg.alive(g.occupants.front()) || sim_->isTrench(reg.handle(b))) continue;
    const uint8_t owner = reg.get<CUnit>(g.occupants.front()).owner;
    const CPosition& p = reg.pool<CPosition>().get(b);
    if (fog && !sim_->allied(owner, player_) && !sim_->vision().visible(player_, p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT)) continue;
    const ImVec2 c = toScreen(unitScreenWorld(b));
    if (!onScreen(c)) continue;
    const int cap = types_.get(reg.pool<CUnit>().get(b).type).garrison;
    char txt[32];
    std::snprintf(txt, sizeof txt, "%zu/%d", g.occupants.size(), cap);
    const ImVec2 ts = ImGui::CalcTextSize(txt);
    const ImVec2 at(c.x - ts.x / 2 - 4, c.y - 46 * zoom - ts.y);
    dl->AddRectFilled(at, ImVec2(at.x + ts.x + 8, at.y + ts.y + 2), darker(teamColor(owner), 0.45f, 0.85f), 3);
    dl->AddText(ImVec2(at.x + 4, at.y + 1), IM_COL32(240, 232, 205, 255), txt);
  }

  // --- Unit cards over bound units -------------------------------------------------------
  // A card: the NATO symbol on a small plate with the unit's name, strength
  // and health. Clicking it selects the whole unit.
  cardHits_.clear();
  if (showCards_) {
    for (uint32_t gi : reg.pool<CGroup>().entities()) {
      const CGroup& g = reg.pool<CGroup>().get(gi);
      const bool mine = sim_->allied(g.owner, player_);
      if (!mine && !settings_.showEnemyCards) continue;
      float sx = 0, sy = 0, minY = 1e9f;
      int n = 0, hp = 0, hpMax = 0;
      bool selected = false;
      for (Entity m : g.members) {
        if (!reg.alive(m) || (reg.has<CGarrisoned>(m) && !sim_->inTrench(m))) continue;
        const CPosition& p = reg.get<CPosition>(m);
        if (fog && !mine && !sim_->vision().visible(player_, p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT)) continue;
        const Vec2f s = unitScreenWorld(m.index);
        sx += s.x;
        sy += s.y;
        minY = std::min(minY, s.y);
        ++n;
        const CUnit& cu = reg.get<CUnit>(m);
        hp += std::max(0, cu.health);
        hpMax += types_.get(cu.type).maxHealth;
        selected = selected || std::find(selection_.begin(), selection_.end(), m) != selection_.end();
      }
      if (n == 0) continue;
      const ImVec2 top = toScreen({sx / n, minY});
      const ImVec2 c(top.x, top.y - (g.vehicles ? 40 : 30) * zoom - 16);
      if (!onScreen(c)) continue;
      const float scale = (g.echelon >= Echelon::Company ? 1.45f : g.echelon >= Echelon::Platoon ? 1.3f
                           : g.echelon == Echelon::Squad ? 1.15f : 1.0f) * std::clamp(zoom, 0.8f, 1.4f);
      const float alpha = selected ? 1.0f : 0.88f;
      const std::string name = groupName(g);
      char strength[24];
      std::snprintf(strength, sizeof strength, "%zu", g.members.size());
      const float fs = ImGui::GetFontSize();
      const float tw = std::max(ImGui::CalcTextSize(name.c_str()).x, ImGui::CalcTextSize(strength).x + 30);
      const float symW = 30 * scale, h = std::max(22 * scale, fs * 2 + 6);
      ImVec2 a(c.x - (symW + tw + 14) * 0.5f, c.y - h * 0.5f), b(a.x + symW + tw + 14, a.y + h);
      // Cards of units standing close together stack instead of overlapping.
      for (int tries = 0; tries < 6; ++tries) {
        bool clash = false;
        for (const CardHit& o : cardHits_)
          if (a.x < o.x1 && b.x > o.x0 && a.y < o.y1 + 2 && b.y > o.y0 - 2) {
            const float up = b.y - (o.y0 - 3);
            a.y -= up;
            b.y -= up;
            clash = true;
            break;
          }
        if (!clash) break;
      }
      const float cy = (a.y + b.y) * 0.5f;
      // Plate.
      const uint32_t tc = teamColor(g.owner);
      dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x + 2, b.y + 2), IM_COL32(0, 0, 0, static_cast<int>(110 * alpha)), 3);
      dl->AddRectFilled(a, b, IM_COL32(24, 25, 20, static_cast<int>(215 * alpha)), 3);
      dl->AddRectFilled(a, ImVec2(a.x + 4, b.y), darker(tc, 1.0f, alpha), 3, ImDrawFlags_RoundCornersLeft);
      dl->AddRect(a, b, selected ? IM_COL32(236, 200, 96, 255) : IM_COL32(150, 140, 110, static_cast<int>(200 * alpha)), 3, 0,
                  selected ? 2.0f : 1.0f);
      drawNatoSymbol(dl, ImVec2(a.x + 8 + symW * 0.5f, cy), scale * 0.95f, groupSymbol(g), g.echelon, tc, alpha, !mine);
      const float tx = a.x + symW + 12;
      dl->AddText(ImVec2(tx, a.y + 2), selected ? IM_COL32(255, 236, 160, 255) : IM_COL32(232, 226, 205, static_cast<int>(240 * alpha)),
                  name.c_str());
      dl->AddText(ImVec2(tx, a.y + 3 + fs), IM_COL32(180, 172, 150, static_cast<int>(230 * alpha)), strength);
      // Health of the whole unit.
      const float frac = hpMax > 0 ? static_cast<float>(hp) / hpMax : 0;
      const float bx = tx + ImGui::CalcTextSize(strength).x + 6, bw = b.x - bx - 6, by = a.y + 3 + fs + fs * 0.35f;
      dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 4), IM_COL32(10, 10, 8, 230));
      dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * frac, by + 4),
                        frac > 0.6f ? IM_COL32(110, 200, 90, 255) : frac > 0.3f ? IM_COL32(230, 190, 70, 255) : IM_COL32(220, 80, 60, 255));
      cardHits_.push_back({a.x, a.y, b.x, b.y, reg.handle(gi)});
    }
  }

  // --- Chips: enemy forces detected in the fog ------------------------------------------
  if (fog && showChips_) drawChips(dl, toScreen);

  // --- What's under the mouse ---------------------------------------------------------
  if (hovered_.valid() && reg.alive(hovered_) && !ImGui::GetIO().WantCaptureMouse) {
    const CUnit& u = reg.get<CUnit>(hovered_);
    const UnitTypeDef& d = types_.get(u.type);
    ImGui::BeginTooltip();
    ImGui::TextColored(ImVec4(0.93f, 0.8f, 0.42f, 1), "%s", d.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", ownerName(u.owner).c_str());
    if (const CGroup* g = sim_->groupOf(hovered_)) ImGui::Text("%s, %zu strong", groupName(*g).c_str(), g->members.size());
    ImGui::TextDisabled("%d / %d", std::max(0, u.health), d.maxHealth);
    if (d.garrison > 0) {
      const CGarrison* gs = reg.tryGet<CGarrison>(hovered_);
      ImGui::TextDisabled("Holds %zu of %d infantry", gs ? gs->occupants.size() : size_t{0}, d.garrison);
    }
    ImGui::EndTooltip();
  }
}

// Every so often: which ground our side can hear and glimpse beyond its sight
// (twice each unit's sight), and the enemy forces there, gathered into chips
// by area. A chip's size says roughly how many; its symbol, the most valuable
// thing among them.
void GameScene::drawChips(ImDrawList* dl, const std::function<ImVec2(Vec2f)>& toScreen) {
  Registry& reg = sim_->registry();
  constexpr int kCell = 4;    // detection grid, tiles
  constexpr int kBucket = 7;  // chip gathering area, tiles
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
    std::map<std::pair<int, int>, Chip> buckets;
    for (uint32_t e : units.entities()) {
      const CUnit& u = units.get(e);
      if (u.owner >= MAX_PLAYERS || sim_->allied(u.owner, player_) || u.health <= 0) continue;
      const UnitTypeDef& d = types_.get(u.type);
      if (d.isStructure()) continue;  // buildings are remembered on the map once seen
      const CPosition& p = reg.pool<CPosition>().get(e);
      const int tx = p.x >> LEPTON_SHIFT, ty = p.y >> LEPTON_SHIFT;
      if (sim_->vision().visible(player_, tx, ty)) continue;  // seen: drawn as itself
      if (!detect_[static_cast<size_t>(ty / kCell) * gw + tx / kCell]) continue;
      Chip& c = buckets[{tx / kBucket, ty / kBucket}];
      c.x += p.x;
      c.y += p.y;
      ++c.count;
      c.owner = u.owner;
      if (d.cost > c.bestCost) {
        c.bestCost = d.cost;
        c.nato = d.nato;
      }
    }
    for (auto& [key, c] : buckets) {
      c.x /= c.count;
      c.y /= c.count;
      chips_.push_back(c);
    }
  }
  for (const Chip& c : chips_) {
    const ImVec2 s = toScreen(groundToScreenWorld(static_cast<float>(c.x), static_cast<float>(c.y)));
    // Rough size only: a handful, a platoon or so, a company, more.
    const float r = c.count < 5 ? 12.0f : c.count < 20 ? 16.0f : c.count < 100 ? 21.0f : 27.0f;
    const uint32_t team = teamColor(c.owner);
    // A stack of chips, like counters on a war-room table.
    const int layers = c.count < 5 ? 1 : c.count < 20 ? 2 : c.count < 100 ? 3 : 4;
    for (int i = layers - 1; i >= 0; --i) {
      const ImVec2 at(s.x, s.y - i * 3.0f);
      dl->AddEllipseFilled(ImVec2(at.x, at.y + 2), ImVec2(r, r * 0.55f), IM_COL32(0, 0, 0, 120));
      dl->AddEllipseFilled(at, ImVec2(r, r * 0.55f), darker(team, i == 0 ? 0.75f : 0.5f, 0.92f));
      dl->AddEllipse(at, ImVec2(r, r * 0.55f), IM_COL32(230, 220, 190, 200), 0, 0, 1.5f);
    }
    const ImVec2 top(s.x, s.y - (layers - 1) * 3.0f);
    drawNatoSymbol(dl, top, r / 18.0f, c.nato, Echelon::None, team, 1.0f, true);
    const char* q = c.count < 5 ? "?" : c.count < 20 ? "??" : c.count < 100 ? "???" : "????";
    const ImVec2 ts = ImGui::CalcTextSize(q);
    dl->AddText(ImVec2(top.x - ts.x / 2, top.y + r * 0.55f + 1), IM_COL32(235, 225, 195, 220), q);
  }
}

}  // namespace legion

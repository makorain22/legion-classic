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
  } else if (nato == "supply") {
    dl->AddLine(ImVec2(a.x, c.y + ih * 0.5f), ImVec2(b.x, c.y + ih * 0.5f), line, t);  // the bar across the foot of the frame
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
  drawMissionMarkers(dl);  // a mission's goals and warnings on the ground (9.9)
  drawRangeRings(dl);      // weapon range circles (13 Nov)

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

  // --- Logistics points -------------------------------------------------------
  // A free point carries its name; while someone holds it, a bar in their
  // colour shows how near the office is. (A taken point shows its office and flag.)
  for (size_t i = 0; i < map_.supplyPoints.size(); ++i) {
    const Vec2i sp = map_.supplyPoints[i];
    const ImVec2 c = toScreen(groundToScreenWorld(sp.x * 256.0f + 128, sp.y * 256.0f + 128));
    if (!onScreen(c)) continue;
    if (sim_->officeOn(static_cast<int>(i)).valid()) continue;
    const char* label = "LOGISTICS POINT";
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const ImVec2 at(c.x, c.y - 26);
    dl->AddRectFilled(ImVec2(at.x - ts.x / 2 - 5, at.y - ts.y / 2 - 2), ImVec2(at.x + ts.x / 2 + 5, at.y + ts.y / 2 + 2), IM_COL32(20, 20, 16, 190), 3);
    dl->AddText(ImVec2(at.x - ts.x / 2, at.y - ts.y / 2), imcol(rgba8(214, 196, 120), 0.95f), label);
    const Simulation::PointState& st = sim_->pointState(i);
    const bool seen = !fog || sim_->vision().visible(player_, sp.x, sp.y);
    if (st.side == 0xFF || st.progress == 0 || !seen) continue;
    const float frac = std::clamp(static_cast<float>(st.progress) / Simulation::kCaptureTicks, 0.0f, 1.0f);
    const float w = ts.x + 10, y = at.y + ts.y / 2 + 4;
    dl->AddRectFilled(ImVec2(at.x - w / 2, y), ImVec2(at.x + w / 2, y + 5), IM_COL32(12, 12, 10, 220));
    dl->AddRectFilled(ImVec2(at.x - w / 2 + 1, y + 1), ImVec2(at.x - w / 2 + 1 + (w - 2) * frac, y + 4), imcol(teamColor(st.side), 1.0f));
  }

  // --- Money paid in by a convoy: a figure that floats up from the headquarters ----
  for (const Floater& f : floaters_) {
    const ImVec2 c = toScreen(groundToScreenWorld(f.pos.x, f.pos.y));
    if (!onScreen(c)) continue;
    const float a = std::clamp(1.0f - f.age / 2.2f, 0.0f, 1.0f);
    const ImVec2 ts = ImGui::CalcTextSize(f.text.c_str());
    const ImVec2 at(c.x - ts.x / 2, c.y - 30 - f.age * 22);
    dl->AddText(ImVec2(at.x + 1, at.y + 1), IM_COL32(0, 0, 0, static_cast<int>(200 * a)), f.text.c_str());
    dl->AddText(at, imcol(rgba8(236, 206, 96), a), f.text.c_str());
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
  // and health. Clicking it selects the whole unit. Cards of units standing
  // close together merge into one stack card (9.3); while one of them is
  // selected, or the mouse is over the stack, it opens into a short list
  // above it, one card per unit.
  cardHits_.clear();
  if (showCards_) {
    struct Card {
      Entity group;
      ImVec2 anchor;  // above the unit's men
      float scale;
      std::string name, symbol;
      Echelon echelon;
      uint8_t owner;
      bool mine, selected, vehicles;
      int strength, hp, hpMax;
    };
    std::vector<Card> cards;
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
      cards.push_back({reg.handle(gi), c, scale, groupName(g), groupSymbol(g), g.echelon, g.owner, mine, selected,
                       g.vehicles, static_cast<int>(g.members.size()), hp, hpMax});
    }
    const float fs = ImGui::GetFontSize();
    // One plate: symbol, two lines of text and a health bar.
    auto plate = [&](ImVec2 c, float scale, const std::string& line1, const std::string& line2, const std::string& symbol,
                     Echelon e, uint8_t owner, bool mine, bool selected, float frac, float alpha, int behind) {
      const float tw = std::max(ImGui::CalcTextSize(line1.c_str()).x, ImGui::CalcTextSize(line2.c_str()).x + 30);
      const float symW = 30 * scale, h = std::max(22 * scale, fs * 2 + 6);
      const ImVec2 a(c.x - (symW + tw + 14) * 0.5f, c.y - h * 0.5f), b(a.x + symW + tw + 14, a.y + h);
      const uint32_t tc = teamColor(owner);
      // A stack: the plates behind peek out above and to the right.
      for (int k = std::min(behind, 3); k >= 1; --k) {
        const ImVec2 o(4.0f * k, -4.0f * k);
        dl->AddRectFilled(ImVec2(a.x + o.x, a.y + o.y), ImVec2(b.x + o.x, b.y + o.y), IM_COL32(30, 31, 25, static_cast<int>(200 * alpha)), 3);
        dl->AddRect(ImVec2(a.x + o.x, a.y + o.y), ImVec2(b.x + o.x, b.y + o.y), IM_COL32(150, 140, 110, static_cast<int>(170 * alpha)), 3);
      }
      const float cy = (a.y + b.y) * 0.5f;
      dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x + 2, b.y + 2), IM_COL32(0, 0, 0, static_cast<int>(110 * alpha)), 3);
      dl->AddRectFilled(a, b, IM_COL32(24, 25, 20, static_cast<int>(215 * alpha)), 3);
      dl->AddRectFilled(a, ImVec2(a.x + 4, b.y), darker(tc, 1.0f, alpha), 3, ImDrawFlags_RoundCornersLeft);
      dl->AddRect(a, b, selected ? IM_COL32(236, 200, 96, 255) : IM_COL32(150, 140, 110, static_cast<int>(200 * alpha)), 3, 0,
                  selected ? 2.0f : 1.0f);
      drawNatoSymbol(dl, ImVec2(a.x + 8 + symW * 0.5f, cy), scale * 0.95f, symbol, e, tc, alpha, !mine);
      const float tx = a.x + symW + 12;
      dl->AddText(ImVec2(tx, a.y + 2), selected ? IM_COL32(255, 236, 160, 255) : IM_COL32(232, 226, 205, static_cast<int>(240 * alpha)),
                  line1.c_str());
      dl->AddText(ImVec2(tx, a.y + 3 + fs), IM_COL32(180, 172, 150, static_cast<int>(230 * alpha)), line2.c_str());
      const float bx = tx + ImGui::CalcTextSize(line2.c_str()).x + 6, bw = b.x - bx - 6, by = a.y + 3 + fs + fs * 0.35f;
      dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 4), IM_COL32(10, 10, 8, 230));
      dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * frac, by + 4),
                        frac > 0.6f ? IM_COL32(110, 200, 90, 255) : frac > 0.3f ? IM_COL32(230, 190, 70, 255) : IM_COL32(220, 80, 60, 255));
      return std::pair<ImVec2, ImVec2>{a, ImVec2(b.x + std::min(behind, 3) * 4.0f, b.y)};
    };
    // Gather cards whose places are close on screen into stacks.
    std::vector<int> stackOf(cards.size(), -1);
    std::vector<std::vector<int>> stacks;
    const float near = 64.0f;
    for (size_t i = 0; i < cards.size(); ++i) {
      if (stackOf[i] >= 0) continue;
      stackOf[i] = static_cast<int>(stacks.size());
      stacks.push_back({static_cast<int>(i)});
      for (size_t k = 0; k < stacks.back().size(); ++k) {  // grows as neighbours join
        const Card& a = cards[static_cast<size_t>(stacks.back()[k])];
        for (size_t j = 0; j < cards.size(); ++j) {
          if (stackOf[j] >= 0 || cards[j].mine != a.mine) continue;
          if (std::abs(cards[j].anchor.x - a.anchor.x) < near * 1.6f && std::abs(cards[j].anchor.y - a.anchor.y) < near * 0.6f) {
            stackOf[j] = stackOf[i];
            stacks.back().push_back(static_cast<int>(j));
          }
        }
      }
    }
    const ImVec2 mouse(mouse_.x / ps, mouse_.y / ps);
    for (std::vector<int>& st : stacks) {
      if (st.size() == 1) {
        const Card& c = cards[static_cast<size_t>(st[0])];
        const auto [a, b] = plate(c.anchor, c.scale, c.name, std::to_string(c.strength), c.symbol, c.echelon, c.owner, c.mine,
                                  c.selected, c.hpMax > 0 ? static_cast<float>(c.hp) / c.hpMax : 0.0f, c.selected ? 1.0f : 0.88f, 0);
        cardHits_.push_back({a.x, a.y, b.x, b.y, c.group, {}});
        continue;
      }
      // The biggest unit on top; the stack stands where they all are.
      std::sort(st.begin(), st.end(), [&](int x, int y) {
        const Card &a = cards[static_cast<size_t>(x)], &b = cards[static_cast<size_t>(y)];
        return a.echelon != b.echelon ? a.echelon > b.echelon : a.strength > b.strength;
      });
      ImVec2 at(0, 0);
      int strength = 0, hp = 0, hpMax = 0;
      bool selected = false;
      std::vector<Entity> groups;
      for (int i : st) {
        const Card& c = cards[static_cast<size_t>(i)];
        at.x += c.anchor.x / st.size();
        at.y = std::min(at.y == 0 ? 1e9f : at.y, c.anchor.y);
        strength += c.strength;
        hp += c.hp;
        hpMax += c.hpMax;
        selected = selected || c.selected;
        groups.push_back(c.group);
      }
      const Card& top = cards[static_cast<size_t>(st[0])];
      char line1[48], line2[24];
      std::snprintf(line1, sizeof line1, "%zu units", st.size());
      std::snprintf(line2, sizeof line2, "%d", strength);
      const auto [a, b] = plate(at, top.scale, line1, line2, top.symbol, top.echelon, top.owner, top.mine, selected,
                                hpMax > 0 ? static_cast<float>(hp) / hpMax : 0.0f, selected ? 1.0f : 0.9f, static_cast<int>(st.size()) - 1);
      // Open while the mouse is on it, or on the list it opened last frame.
      const bool hover = (mouse.x >= a.x && mouse.x <= b.x + 4 && mouse.y >= a.y - 8 && mouse.y <= b.y) ||
                         (openStack_ == top.group && mouse.x >= openArea_[0] && mouse.x <= openArea_[2] &&
                          mouse.y >= openArea_[1] && mouse.y <= openArea_[3]);
      cardHits_.push_back({a.x, a.y, b.x, b.y, top.group, groups});
      if (openStack_ == top.group && !hover) openStack_ = kNullEntity;
      if (!selected && !hover) continue;
      float area[4] = {a.x, a.y, b.x + 4, b.y};
      // Opened: which units are in it, one card each, in a short column above.
      constexpr size_t kShow = 8;
      float y = a.y - 8;
      for (size_t k = 0; k < st.size() && k < kShow; ++k) {
        const Card& c = cards[static_cast<size_t>(st[k])];
        const float h = std::max(22 * 0.9f, fs * 2 + 6);
        const ImVec2 cc(at.x, y - h * 0.5f);
        const auto [ca, cb] = plate(cc, 0.9f, c.name, std::to_string(c.strength), c.symbol, c.echelon, c.owner, c.mine, c.selected,
                                    c.hpMax > 0 ? static_cast<float>(c.hp) / c.hpMax : 0.0f, 0.97f, 0);
        cardHits_.push_back({ca.x, ca.y, cb.x, cb.y, c.group, {}});
        area[0] = std::min(area[0], ca.x);
        area[1] = std::min(area[1], ca.y);
        area[2] = std::max(area[2], cb.x);
        y = ca.y - 3;
      }
      if (hover) {
        openStack_ = top.group;
        std::copy(area, area + 4, openArea_);
      }
      if (st.size() > kShow) {
        char more[32];
        std::snprintf(more, sizeof more, "+%zu more", st.size() - kShow);
        dl->AddText(ImVec2(at.x - ImGui::CalcTextSize(more).x / 2, y - fs - 2), IM_COL32(232, 226, 205, 230), more);
      }
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

}  // namespace legion

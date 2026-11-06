#include "encyclopedia.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game_scene.hpp"
#include "slave/math/iso.hpp"
#include "ui_style.hpp"

using namespace slave;

namespace legion {

namespace {

constexpr int kSideCount = 4;
const char* kSides[kSideCount] = {"Germany", "Soviet Union", "United Kingdom", "Civilian"};
const char* kSideIds[kSideCount] = {"ger", "rus", "gbr", "neutral"};
const char* kSideShort[kSideCount] = {"German", "Soviet", "British", "Civil"};
const char* kArmour[ARMOR_COUNT] = {"Infantry", "Light", "Medium", "Heavy", "Structure"};
const char* kArmourShort[ARMOR_COUNT] = {"Inf", "Light", "Med", "Heavy", "Bldg"};

// The unit drawn from its sprites (body and turret), turning slowly.
void drawUnit(ImDrawList* dl, const SpriteRenderer& r, const UnitArt& art, ImVec2 a, ImVec2 size, int facing) {
  const ImTextureRef tex(static_cast<ImTextureID>(r.atlasTexture()));
  const AtlasFrame& body = r.frame(art.body.frame(facing, 0));
  // Fit the picture: scale from the largest facing so it doesn't pulse as it turns.
  int maxW = 1, maxH = 1;
  for (int f = 0; f < 16; f += 2) {
    const AtlasFrame& b = r.frame(art.body.frame(f, 0));
    maxW = std::max(maxW, b.w);
    maxH = std::max(maxH, b.h);
  }
  const float k = std::min({size.x / maxW, size.y / maxH, 3.0f}) * 0.85f;
  const ImVec2 anchor(a.x + size.x * 0.5f, a.y + size.y * 0.62f);
  auto put = [&](const AtlasFrame& f, float ox, float oy) {
    const ImVec2 p(anchor.x + (ox - f.anchorX) * k, anchor.y + (oy - f.anchorY) * k);
    dl->AddImage(tex, p, ImVec2(p.x + f.w * k, p.y + f.h * k), ImVec2(f.u0, f.v0), ImVec2(f.u1, f.v1));
  };
  put(body, 0, 0);
  if (art.hasTurret) {
    const float ang = static_cast<float>(angleOfFacing(facing)) * (6.2831853f / ANGLE_FULL);
    const Vec2f off = iso::worldToScreen(std::cos(ang) * art.turretOffset, std::sin(ang) * art.turretOffset);
    put(r.frame(art.turret.frame(facing, 0)), off.x, off.y);
  }
}

}  // namespace

void Encyclopedia::open(int type) {
  open_ = true;
  if (type >= 0 && type < static_cast<int>(scene_.types().size())) {
    selected_ = type;
    const std::string& f = scene_.types().get(type).faction;
    side_ = kSideCount - 1;
    for (int i = 0; i < kSideCount; ++i)
      if (f == kSideIds[i]) side_ = i;
  }
}

void Encyclopedia::draw() {
  if (!open_) return;
  const auto& types = scene_.types();
  const ImGuiIO& io = ImGui::GetIO();
  const ImVec2 size(std::min(900.0f, io.DisplaySize.x - 40), std::min(600.0f, io.DisplaySize.y - 40));
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
  ui::pushTheme();
  auto col = [](ImU32 c, float a = 1.0f) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.w = a;
    return v;
  };
  ImGui::PushStyleColor(ImGuiCol_WindowBg, col(ui::kPlateDark, 0.98f));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, col(ui::kPlate));
  ImGui::PushStyleColor(ImGuiCol_TitleBg, col(ui::kPlateDark));
  ImGui::PushStyleColor(ImGuiCol_Header, col(ui::kBrassDim));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, col(ui::kPlateLight));
  ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1, 1, 1, 0.03f));
  if (!ImGui::Begin("Encyclopedia", &open_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    ImGui::PopStyleColor(6);
    ui::popTheme();
    return;
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const SpriteRenderer& r = scene_.renderer();

  // --- The list -------------------------------------------------------------
  ImGui::BeginChild("##list", ImVec2(250, 0), true);
  {
    const float bw = (ImGui::GetContentRegionAvail().x - 4 * (kSideCount - 1)) / kSideCount;
    for (int i = 0; i < kSideCount; ++i) {
      if (i) ImGui::SameLine(0, 4);
      if (ui::plateButton(kSides[i], kSideShort[i], ImVec2(bw, 24), side_ == i)) side_ = i;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kSides[i]);
    }
    ImGui::Dummy(ImVec2(0, 2));
    for (int c = 0; c < static_cast<int>(UnitCategory::Count); ++c) {
      std::vector<int> list;
      for (int t = 0; t < static_cast<int>(types.size()); ++t) {
        const UnitTypeDef& d = types.get(t);
        if (!d.hidden && d.faction == kSideIds[side_] && static_cast<int>(d.category) == c) list.push_back(t);
      }
      if (list.empty()) continue;
      ui::sectionTitle(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x,
                       categoryName(static_cast<UnitCategory>(c)));
      ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() + 6));
      for (int t : list) {
        if (selected_ < 0 || types.get(selected_).faction != kSideIds[side_]) selected_ = t;
        if (ImGui::Selectable((types.get(t).name + "##" + types.get(t).id).c_str(), selected_ == t)) selected_ = t;
      }
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();

  // --- The page --------------------------------------------------------------
  ImGui::BeginChild("##page", ImVec2(0, 0), false);
  if (selected_ >= 0 && selected_ < static_cast<int>(types.size())) {
    const UnitTypeDef& d = types.get(selected_);
    const UnitArt& art = scene_.unitArt(selected_);
    // Picture: the painted icon if there is one, and the unit itself turning.
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 box(ImGui::GetContentRegionAvail().x, 190);
    ui::panel(dl, p0, ImVec2(p0.x + box.x, p0.y + box.y), true);
    float pictureX = p0.x;
    if (art.icon >= 0) {
      const AtlasFrame& ic = r.frame(art.icon);
      const float k = std::min(124.0f / ic.w, 124.0f / ic.h);
      const ImVec2 a(p0.x + 16, p0.y + 54);
      ui::panel(dl, ImVec2(a.x - 4, a.y - 4), ImVec2(a.x + ic.w * k + 4, a.y + ic.h * k + 4));
      dl->AddImage(ImTextureRef(static_cast<ImTextureID>(r.atlasTexture())), a, ImVec2(a.x + ic.w * k, a.y + ic.h * k),
                   ImVec2(ic.u0, ic.v0), ImVec2(ic.u1, ic.v1));
      pictureX = a.x + ic.w * k + 10;
    }
    const int facing = d.isStructure() && !art.hasTurret ? 0 : static_cast<int>(ImGui::GetTime() * 1.6) % 16;
    drawUnit(dl, r, art, ImVec2(pictureX, p0.y + 6), ImVec2(p0.x + box.x - pictureX, box.y - 12), facing);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.5f, ImVec2(p0.x + 12, p0.y + 8), ui::kBrass, d.name.c_str());
    const std::string sub = std::string(kSides[side_]) + "  -  " + (d.group.empty() ? categoryName(d.category) : d.group);
    dl->AddText(ImVec2(p0.x + 12, p0.y + 32), ui::kStencilDim, sub.c_str());
    ImGui::Dummy(box);
    if (!d.description.empty()) {
      ImGui::Dummy(ImVec2(0, 2));
      ImGui::TextWrapped("%s", d.description.c_str());
    }
    ImGui::Dummy(ImVec2(0, 4));

    // Numbers.
    if (ImGui::BeginTable("##stats", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
      auto pair = [&](const char* l1, const std::string& v1, const char* l2, const std::string& v2) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.55f, 0.53f, 0.45f, 1), "%s", l1);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(v1.c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.55f, 0.53f, 0.45f, 1), "%s", l2);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(v2.c_str());
      };
      char a[64], b[64];
      std::snprintf(a, sizeof a, d.cost ? "$%d" : "-", d.cost);
      std::snprintf(b, sizeof b, "%.1f s", d.buildTime / 15.0);
      pair("Cost", a, "Build time", d.builtAt.empty() ? "-" : b);
      std::snprintf(a, sizeof a, "%d", d.maxHealth);
      pair("Hit points", a, "Armour", kArmour[static_cast<int>(d.armor)]);
      if (d.isStructure()) {
        std::snprintf(a, sizeof a, "%d x %d tiles", d.footW, d.footH);
        std::snprintf(b, sizeof b, d.power > 0 ? "+%d" : d.power < 0 ? "%d" : "-", d.power);
        pair("Footprint", a, "Power", b);
      } else {
        std::snprintf(a, sizeof a, "%.2f tiles/s", d.speed * 15.0 / 256.0);
        pair("Speed", a, "Moves", d.locomotor == Locomotor::Foot ? "on foot" : d.locomotor == Locomotor::Wheeled ? "on wheels" : "on tracks");
      }
      std::snprintf(a, sizeof a, "%.1f tiles", d.sight / 256.0);
      std::snprintf(b, sizeof b, "%d men", d.garrison);
      pair("Sight", a, "Holds", d.garrison ? b : "-");
      if (d.income || d.cover || d.coverAround) {
        std::snprintf(a, sizeof a, "%d a minute", d.income);
        std::snprintf(b, sizeof b, "%d%% %s", d.cover ? d.cover : d.coverAround, d.cover ? "inside" : "beside it");
        pair("Income", d.income ? a : "-", "Cover", (d.cover || d.coverAround) ? b : "-");
      }
      std::string needs;
      for (int n : d.prereqs) needs += (needs.empty() ? "" : ", ") + types.get(n).name;
      const std::string where = d.builtAt == "barracks" ? "Barracks" : d.builtAt == "factory" ? "Tank Works"
                                : d.builtAt == "yard" ? "Construction Yard" : "-";
      pair("Made at", where, "Needs", needs.empty() ? "-" : needs);
      ImGui::EndTable();
    }

    // Weapons.
    if (!d.weapons.empty()) {
      ImGui::Dummy(ImVec2(0, 4));
      ui::sectionTitle(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), ImGui::GetContentRegionAvail().x,
                       d.turret ? "WEAPONS  (in a turret)" : "WEAPONS");
      ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() + 6));
      if (ImGui::BeginTable("##weapons", 4 + ARMOR_COUNT, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Weapon", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Range");
        ImGui::TableSetupColumn("Reload");
        ImGui::TableSetupColumn("Splash");
        for (int k = 0; k < ARMOR_COUNT; ++k) ImGui::TableSetupColumn(kArmourShort[k]);
        ImGui::TableHeadersRow();
        const WeaponRegistry& wr = scene_.weapons();
        for (int w : d.weapons) {
          const WeaponDef& wd = wr.get(w);
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(wd.name.c_str());
          if (wd.burst > 1) {
            ImGui::SameLine();
            ImGui::TextDisabled("x%d", wd.burst);
          }
          ImGui::TableNextColumn();
          ImGui::Text("%.1f", wd.range / 256.0);
          ImGui::TableNextColumn();
          ImGui::Text("%.1f s", wd.cooldown / 15.0);
          ImGui::TableNextColumn();
          if (wd.splash) ImGui::Text("%.1f", wd.splash / 256.0);
          else ImGui::TextDisabled("-");
          // Damage a minute against each armour (accuracy and bursts counted).
          int best = 1;
          std::array<int, ARMOR_COUNT> dpm{};
          for (int k = 0; k < ARMOR_COUNT; ++k) {
            const int vs = wd.vsInfantry >= 0 && k == 0 ? wd.vsInfantry : w;
            const WeaponDef& use = wr.get(vs);
            dpm[static_cast<size_t>(k)] = static_cast<int>(wr.damageAgainst(vs, static_cast<ArmorClass>(k)) * use.burst *
                                                           (use.projectile == ProjectileKind::Instant ? use.accuracy / 100.0 : 1.0) *
                                                           900.0 / std::max(1, use.cooldown));
            best = std::max(best, dpm[static_cast<size_t>(k)]);
          }
          for (int k = 0; k < ARMOR_COUNT; ++k) {
            ImGui::TableNextColumn();
            const int v = dpm[static_cast<size_t>(k)];
            const float t = static_cast<float>(v) / best;
            const ImVec4 col = v == 0 ? ImVec4(0.4f, 0.38f, 0.33f, 1)
                                      : ImVec4(0.55f + 0.35f * t, 0.45f + 0.35f * t, 0.30f - 0.1f * t, 1);
            ImGui::TextColored(col, "%d", v);
          }
        }
        ImGui::EndTable();
      }
      ImGui::TextDisabled("Damage a minute against each kind of armour, before cover and armour facing.");
    }
  } else {
    ImGui::TextDisabled("Pick an entry on the left.");
  }
  ImGui::EndChild();
  ImGui::End();
  ImGui::PopStyleColor(6);
  ui::popTheme();
}

}  // namespace legion

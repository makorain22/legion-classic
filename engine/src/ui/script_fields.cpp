#include "pioneer/ui/script_fields.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "pioneer/sim/unit_types.hpp"
#include "pioneer/ui/fonts.hpp"

namespace pioneer::ui {

namespace {
// A list to pick one of by number, with an optional entry for "none" (-1).
bool pickIndex(const char* id, int32_t& v, size_t count, const char* none, const std::function<std::string(size_t)>& name) {
  bool changed = false;
  const std::string now = v >= 0 && static_cast<size_t>(v) < count ? name(static_cast<size_t>(v)) : (v < 0 && none ? none : "(choose)");
  if (ImGui::BeginCombo(id, now.c_str())) {
    if (none && ImGui::Selectable(none, v < 0)) {
      v = -1;
      changed = true;
    }
    for (size_t i = 0; i < count; ++i) {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable(name(i).c_str(), v == static_cast<int32_t>(i))) {
        v = static_cast<int32_t>(i);
        changed = true;
      }
      ImGui::PopID();
    }
    if (count == 0 && !none) ImGui::TextDisabled("(the mission has none yet)");
    ImGui::EndCombo();
  }
  return changed;
}

bool pickUnitType(const char* id, int32_t& v, bool optional, const UnitTypeRegistry* types) {
  if (!types) return ImGui::InputInt(id, &v);
  bool changed = false;
  const std::string now = v >= 0 && v < static_cast<int32_t>(types->size()) ? types->get(v).name : (optional ? "Any" : "(choose)");
  if (ImGui::BeginCombo(id, now.c_str(), ImGuiComboFlags_HeightLarge)) {
    static std::string filter;
    if (ImGui::IsWindowAppearing()) {
      filter.clear();
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "type to search", &filter);
    std::string f = filter;
    std::transform(f.begin(), f.end(), f.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (optional && f.empty() && ImGui::Selectable("Any", v < 0)) {
      v = -1;
      changed = true;
    }
    for (int32_t i = 0; i < static_cast<int32_t>(types->size()); ++i) {
      const UnitTypeDef& d = types->get(i);
      std::string hay = d.name + " " + d.id;
      std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (!f.empty() && hay.find(f) == std::string::npos) continue;
      ImGui::PushID(i);
      if (ImGui::Selectable((d.name + "##t").c_str(), v == i)) {
        v = i;
        changed = true;
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s", d.id.c_str());
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  return changed;
}

template <class T>
bool editAll(const script::Entry& e, T& item, const ScriptFieldContext& ctx) {
  using script::Kind;
  bool changed = false;
  const ScenarioScript empty;
  const ScenarioScript& s = ctx.script ? *ctx.script : empty;
  const float labelW = px(ctx.labelWidth);
  ImGui::PushID(e.id);
  for (const script::Field& f : e.fields) {
    ImGui::PushID(f.key);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(f.label);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s", f.help, f.optional ? "\n(may be left empty)" : "");
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(-1);
    if (f.slot == script::Slot::Text) {
      if (f.kind == Kind::Text) changed |= ImGui::InputTextMultiline("##v", &item.text, ImVec2(-1, ImGui::GetTextLineHeight() * 3.2f));
      else changed |= ImGui::InputTextWithHint("##v", f.optional ? "(none)" : f.label, &item.text);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.help);
      ImGui::PopID();
      continue;
    }
    int32_t v = script::get(item, f.slot);
    bool ch = false;
    switch (f.kind) {
      case Kind::Player:
      case Kind::PlayerOrAny: {
        const bool any = f.kind == Kind::PlayerOrAny;
        const std::string now = v == ANY_PLAYER ? (any ? "Everyone" : "(choose)") : "Side " + std::to_string(v + 1);
        if (ImGui::BeginCombo("##v", now.c_str())) {
          if (any && ImGui::Selectable("Everyone", v == ANY_PLAYER)) {
            v = ANY_PLAYER;
            ch = true;
          }
          for (int p = 0; p < 8; ++p)
            if (ImGui::Selectable(("Side " + std::to_string(p + 1)).c_str(), v == p)) {
              v = p;
              ch = true;
            }
          ImGui::EndCombo();
        }
        break;
      }
      case Kind::Area:
      case Kind::AreaOrMap:
        ch = pickIndex("##v", v, s.areas.size(), f.kind == Kind::AreaOrMap ? "(none / the whole map)" : nullptr, [&](size_t i) { return s.areas[i].name; });
        break;
      case Kind::Trigger:
        ch = pickIndex("##v", v, s.triggers.size(), nullptr, [&](size_t i) { return s.triggers[i].name; });
        break;
      case Kind::Phase:
        ch = pickIndex("##v", v, s.phases.size(), nullptr, [&](size_t i) { return std::to_string(i) + ": " + s.phases[i].name; });
        break;
      case Kind::Wave:
        ch = pickIndex("##v", v, s.waves.size(), nullptr, [&](size_t i) { return std::to_string(i) + ": " + s.waves[i].name; });
        break;
      case Kind::Path:
        ch = pickIndex("##v", v, s.paths.size(), nullptr, [&](size_t i) { return std::to_string(i) + ": " + s.paths[i].name; });
        break;
      case Kind::Goal:
        ch = pickIndex("##v", v, s.goals.size(), nullptr, [&](size_t i) { return std::to_string(i) + ": " + s.goals[i].text; });
        break;
      case Kind::UnitType:
        ch = pickUnitType("##v", v, f.optional, ctx.types);
        break;
      case Kind::Bool: {
        bool on = v != 0;
        if (ImGui::Checkbox("##v", &on)) {
          v = on ? 1 : 0;
          ch = true;
        }
        break;
      }
      case Kind::Choice: {
        std::vector<std::string> names;
        for (const char* p = f.choices; *p;) {
          const char* end = std::strchr(p, '|');
          names.emplace_back(end ? std::string(p, end) : std::string(p));
          if (!end) break;
          p = end + 1;
        }
        int32_t at = v - f.min;
        ch = pickIndex("##v", at, names.size(), nullptr, [&](size_t i) { return names[i]; });
        v = at + f.min;
        break;
      }
      case Kind::Percent:
        ch = ImGui::SliderInt("##v", &v, f.min, f.max, "%d%%", ImGuiSliderFlags_AlwaysClamp);
        break;
      case Kind::Order: {
        const std::string now = v < 0 ? "Any order" : orderId(v);
        if (ImGui::BeginCombo("##v", now.c_str())) {
          if (ImGui::Selectable("Any order", v < 0)) {
            v = -1;
            ch = true;
          }
          for (int o = 0; o < 32; ++o) {
            const char* id = orderId(o);
            if (!id || !*id) continue;
            if (ImGui::Selectable(id, v == o)) {
              v = o;
              ch = true;
            }
          }
          ImGui::EndCombo();
        }
        break;
      }
      default:  // numbers: plain, seconds, objective numbers
        ch = ImGui::InputInt("##v", &v, 1, f.kind == Kind::Seconds ? 30 : 10);
        if (ch) v = std::clamp(v, f.min, f.max);
        break;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.help);
    if (ch) {
      script::set(item, f.slot, v);
      changed = true;
    }
    ImGui::PopID();
  }
  ImGui::PopID();
  return changed;
}
}  // namespace

bool editFields(const script::Entry& e, Condition& c, const ScriptFieldContext& ctx) { return editAll(e, c, ctx); }
bool editFields(const script::Entry& e, Action& a, const ScriptFieldContext& ctx) { return editAll(e, a, ctx); }

}  // namespace pioneer::ui

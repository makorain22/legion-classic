// Slave Editor - the Script tab: named areas, triggers and objectives.
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdio>

#include "editor.hpp"
#include "game_scene.hpp"

using namespace slave;

namespace legion {

namespace {
const ImVec4 kGold(0.93f, 0.78f, 0.38f, 1);
uint32_t areaColor(int i, bool selected) {
  static const uint32_t c[] = {rgba8(240, 200, 90), rgba8(120, 200, 255), rgba8(150, 230, 110), rgba8(240, 120, 200),
                               rgba8(255, 150, 80)};
  const uint32_t base = c[i % 5];
  return selected ? base : (base & 0x00FFFFFFu) | 0xA0000000u;
}
}  // namespace

// Area outlines on the ground (and the one being drawn).
void Editor::renderAreas(SpriteRenderer& r) {
  auto outline = [&](int x0, int y0, int x1, int y1, uint32_t col) {
    const float ax = x0 * 256.0f, ay = y0 * 256.0f, bx = (x1 + 1) * 256.0f, by = (y1 + 1) * 256.0f;
    const Vec2f corners[4] = {{ax, ay}, {bx, ay}, {bx, by}, {ax, by}};
    for (int e = 0; e < 4; ++e) {
      const Vec2f p = corners[e], q = corners[(e + 1) % 4];
      const int n = std::max(2, static_cast<int>(std::hypot(q.x - p.x, q.y - p.y) / 48.0f));
      for (int i = 0; i <= n; ++i) {
        const float t = static_cast<float>(i) / n;
        const Vec2f s = scene_.groundToScreenWorld(p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t);
        r.rect(std::floor(s.x) - 1, std::floor(s.y) - 1, 2, 2, col);
      }
    }
  };
  const ScenarioScript& s = scene_.scenario();
  for (int i = 0; i < static_cast<int>(s.areas.size()); ++i) {
    const ScriptArea& a = s.areas[static_cast<size_t>(i)];
    outline(a.x0, a.y0, a.x1, a.y1, areaColor(i, i == selectedArea_));
  }
  if (drawingArea_)
    outline(std::min(areaStart_.x, areaEnd_.x), std::min(areaStart_.y, areaEnd_.y), std::max(areaStart_.x, areaEnd_.x),
            std::max(areaStart_.y, areaEnd_.y), rgba8(255, 255, 255));
}

void Editor::drawAreaLabels() {
  const ScenarioScript& s = scene_.scenario();
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  for (int i = 0; i < static_cast<int>(s.areas.size()); ++i) {
    const ScriptArea& a = s.areas[static_cast<size_t>(i)];
    const Vec2f w = scene_.groundToScreenWorld(a.x0 * 256.0f + 128, a.y0 * 256.0f + 128);
    const Vec2f p = scene_.camera().worldToScreen(w);
    const uint32_t c = areaColor(i, i == selectedArea_);
    dl->AddText(ImVec2(p.x - 10, p.y - 18), (c & 0x00FFFFFFu) | 0xFF000000u, a.name.c_str());
  }
}

bool Editor::playerCombo(const char* label, uint8_t& p, bool allowAny) {
  auto& players = scene_.players();
  const std::string current = p == ANY_PLAYER ? std::string("Everyone") : players[p % MAX_PLAYERS].name;
  bool changed = false;
  ImGui::SetNextItemWidth(150);
  if (ImGui::BeginCombo(label, current.c_str())) {
    if (allowAny && ImGui::Selectable("Everyone", p == ANY_PLAYER)) {
      p = ANY_PLAYER;
      changed = true;
    }
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i)
      if (ImGui::Selectable((std::to_string(i + 1) + ". " + players[i].name).c_str(), p == i)) {
        p = i;
        changed = true;
      }
    ImGui::EndCombo();
  }
  return changed;
}

bool Editor::areaCombo(const char* label, int32_t& area) {
  const ScenarioScript& s = scene_.scenario();
  const char* current = (area >= 0 && area < static_cast<int>(s.areas.size())) ? s.areas[static_cast<size_t>(area)].name.c_str() : "(choose an area)";
  bool changed = false;
  ImGui::SetNextItemWidth(150);
  if (ImGui::BeginCombo(label, current)) {
    for (int i = 0; i < static_cast<int>(s.areas.size()); ++i)
      if (ImGui::Selectable(s.areas[static_cast<size_t>(i)].name.c_str(), area == i)) {
        area = i;
        changed = true;
      }
    if (s.areas.empty()) ImGui::TextDisabled("No areas yet: draw one first.");
    ImGui::EndCombo();
  }
  return changed;
}

bool Editor::unitCombo(const char* label, int32_t& type, bool allowAny) {
  const auto& types = scene_.types();
  const char* current = type >= 0 && type < static_cast<int>(types.size()) ? types.get(type).id.c_str() : "Any unit";
  bool changed = false;
  ImGui::SetNextItemWidth(190);
  if (ImGui::BeginCombo(label, current)) {
    if (allowAny && ImGui::Selectable("Any unit", type < 0)) {
      type = -1;
      changed = true;
    }
    for (int i = 0; i < static_cast<int>(types.size()); ++i)
      if (ImGui::Selectable((types.get(i).name + "  (" + types.get(i).id + ")").c_str(), type == i)) {
        type = i;
        changed = true;
      }
    ImGui::EndCombo();
  }
  return changed;
}

bool Editor::triggerCombo(const char* label, int32_t& trigger) {
  const ScenarioScript& s = scene_.scenario();
  const char* current = (trigger >= 0 && trigger < static_cast<int>(s.triggers.size())) ? s.triggers[static_cast<size_t>(trigger)].name.c_str() : "(choose a trigger)";
  bool changed = false;
  ImGui::SetNextItemWidth(170);
  if (ImGui::BeginCombo(label, current)) {
    for (int i = 0; i < static_cast<int>(s.triggers.size()); ++i)
      if (ImGui::Selectable((std::to_string(i + 1) + ". " + s.triggers[static_cast<size_t>(i)].name).c_str(), trigger == i)) {
        trigger = i;
        changed = true;
      }
    ImGui::EndCombo();
  }
  return changed;
}

bool Editor::editCondition(Condition& c) {
  bool ch = false;
  int type = static_cast<int>(c.type);
  ImGui::SetNextItemWidth(170);
  if (ImGui::BeginCombo("##ctype", condName(c.type))) {
    for (int i = 0; i < static_cast<int>(CondType::Count); ++i)
      if (ImGui::Selectable(condName(static_cast<CondType>(i)), i == type)) {
        c.type = static_cast<CondType>(i);
        ch = true;
      }
    ImGui::EndCombo();
  }
  ImGui::Indent();
  switch (c.type) {
    case CondType::Timer:
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("seconds", &c.value);
      break;
    case CondType::UnitsInArea:
      ch |= playerCombo("side", c.player, false);
      ch |= areaCombo("area", c.area);
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("at least", &c.value);
      ch |= unitCombo("of", c.unitType, true);
      break;
    case CondType::NoUnitsInArea:
      ch |= playerCombo("side", c.player, false);
      ch |= areaCombo("area", c.area);
      ch |= unitCombo("of", c.unitType, true);
      break;
    case CondType::UnitCount:
      ch |= playerCombo("side", c.player, false);
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("at most", &c.value);
      ch |= unitCombo("of", c.unitType, true);
      break;
    case CondType::PlayerDefeated: ch |= playerCombo("side", c.player, false); break;
    case CondType::Supplies:
      ch |= playerCombo("side", c.player, false);
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("supplies", &c.value, 100, 1000);
      break;
    case CondType::TriggerFired: ch |= triggerCombo("trigger", c.trigger); break;
    default: break;
  }
  ImGui::Unindent();
  return ch;
}

bool Editor::editAction(Action& a) {
  bool ch = false;
  const int type = static_cast<int>(a.type);
  ImGui::SetNextItemWidth(170);
  if (ImGui::BeginCombo("##atype", actionName(a.type))) {
    for (int i = 0; i < static_cast<int>(ActionType::Count); ++i)
      if (ImGui::Selectable(actionName(static_cast<ActionType>(i)), i == type)) {
        a.type = static_cast<ActionType>(i);
        ch = true;
      }
    ImGui::EndCombo();
  }
  ImGui::Indent();
  auto text = [&](const char* hint) {
    ImGui::SetNextItemWidth(-1);
    ch |= ImGui::InputTextWithHint("##text", hint, &a.text);
  };
  switch (a.type) {
    case ActionType::Message:
      ch |= playerCombo("to", a.player, true);
      text("What to say");
      break;
    case ActionType::AddObjective: {
      ch |= playerCombo("for", a.player, true);
      text("The objective");
      bool optional = a.value == 1;
      if (ImGui::Checkbox("optional", &optional)) {
        a.value = optional ? 1 : 0;
        ch = true;
      }
      break;
    }
    case ActionType::CompleteObjective:
    case ActionType::FailObjective: {
      // Objectives are numbered in the order "Add objective" actions run; list them as a hint.
      ImGui::SetNextItemWidth(100);
      int n = a.value + 1;
      if (ImGui::InputInt("objective no.", &n)) {
        a.value = std::max(0, n - 1);
        ch = true;
      }
      int k = 0;
      for (const Trigger& t : scene_.scenario().triggers)
        for (const Action& x : t.actions)
          if (x.type == ActionType::AddObjective) {
            ++k;
            if (k == a.value + 1) ImGui::TextDisabled("= \"%s\"", x.text.c_str());
          }
      break;
    }
    case ActionType::Victory:
    case ActionType::Defeat: ch |= playerCombo("side", a.player, false); break;
    case ActionType::SpawnUnits:
      ch |= playerCombo("for", a.player, false);
      ch |= unitCombo("unit", a.unitType, false);
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("how many", &a.value);
      ch |= areaCombo("in", a.area);
      break;
    case ActionType::GiveSupplies:
      ch |= playerCombo("to", a.player, false);
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("supplies", &a.value, 500, 5000);
      break;
    case ActionType::RevealArea:
      ch |= playerCombo("to", a.player, true);
      ch |= areaCombo("area", a.area);
      break;
    case ActionType::AttackMove:
    case ActionType::MoveUnits:
      ch |= playerCombo("side", a.player, false);
      ch |= unitCombo("units", a.unitType, true);
      ch |= areaCombo("from", a.area);
      ch |= areaCombo("to", a.area2);
      break;
    case ActionType::ChangeOwner:
      ch |= playerCombo("from side", a.player, false);
      ch |= areaCombo("in", a.area);
      ch |= unitCombo("units", a.unitType, true);
      {
        uint8_t to = a.player2;
        if (playerCombo("to side", to, false)) {
          a.player2 = to;
          ch = true;
        }
      }
      break;
    case ActionType::KillUnits:
      ch |= playerCombo("side", a.player, true);
      ch |= areaCombo("in", a.area);
      ch |= unitCombo("units", a.unitType, true);
      break;
    case ActionType::EnableTrigger:
    case ActionType::DisableTrigger: ch |= triggerCombo("trigger", a.trigger); break;
    case ActionType::SetComputer: {
      ch |= playerCombo("side", a.player, false);
      int w = a.value + 1;
      ImGui::SetNextItemWidth(170);
      if (ImGui::Combo("played by", &w, "Human\0Computer: easy\0Computer: normal\0Computer: hard\0")) {
        a.value = w - 1;
        ch = true;
      }
      break;
    }
    case ActionType::CameraTo:
      ch |= playerCombo("for", a.player, true);
      ch |= areaCombo("to", a.area);
      break;
    default: break;
  }
  ImGui::Unindent();
  return ch;
}

void Editor::drawScriptTab() {
  ScenarioScript& s = scene_.scenario();
  bool changed = false;
  ImGui::TextWrapped("Turn a battle into a mission: name areas of the map, then add triggers. "
                     "When all of a trigger's conditions hold, its actions run.");

  // --- Areas ------------------------------------------------------------------
  ImGui::SeparatorText("Areas");
  const bool drawing = tool_ == Tool::Area;
  if (drawing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.50f, 0.22f, 1));
  if (ImGui::Button(drawing ? "Drag on the map..." : "Draw a new area")) tool_ = drawing ? Tool::SelectUnits : Tool::Area;
  if (drawing) ImGui::PopStyleColor();
  if (ImGui::BeginListBox("##areas", ImVec2(-1, std::min(5, std::max(2, static_cast<int>(s.areas.size()))) * 20.0f + 6))) {
    for (int i = 0; i < static_cast<int>(s.areas.size()); ++i) {
      const ScriptArea& a = s.areas[static_cast<size_t>(i)];
      char label[128];
      std::snprintf(label, sizeof label, "%s  (%dx%d)##a%d", a.name.c_str(), a.x1 - a.x0 + 1, a.y1 - a.y0 + 1, i);
      if (ImGui::Selectable(label, selectedArea_ == i)) selectedArea_ = i;
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) scene_.centerOn({(a.x0 + a.x1) * 128, (a.y0 + a.y1) * 128});
    }
    ImGui::EndListBox();
  }
  if (selectedArea_ >= 0 && selectedArea_ < static_cast<int>(s.areas.size())) {
    ScriptArea& a = s.areas[static_cast<size_t>(selectedArea_)];
    ImGui::SetNextItemWidth(-60);
    changed |= ImGui::InputText("Name", &a.name);
    if (ImGui::Button("Delete area")) {
      // Conditions and actions pointing past it shift down.
      const int gone = selectedArea_;
      s.areas.erase(s.areas.begin() + gone);
      for (Trigger& t : s.triggers) {
        for (Condition& c : t.conditions) c.area = c.area == gone ? -1 : c.area > gone ? c.area - 1 : c.area;
        for (Action& x : t.actions) {
          x.area = x.area == gone ? -1 : x.area > gone ? x.area - 1 : x.area;
          x.area2 = x.area2 == gone ? -1 : x.area2 > gone ? x.area2 - 1 : x.area2;
        }
      }
      selectedArea_ = -1;
      changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("double-click a name to look at it");
  }

  // --- Triggers -----------------------------------------------------------------
  ImGui::SeparatorText("Triggers");
  if (ImGui::Button("Add trigger")) {
    Trigger t;
    t.name = "Trigger " + std::to_string(s.triggers.size() + 1);
    t.conditions.push_back({});
    Action a;
    a.type = ActionType::Message;
    a.text = "Something happens.";
    t.actions.push_back(a);
    s.triggers.push_back(std::move(t));
    selectedTrigger_ = static_cast<int>(s.triggers.size()) - 1;
    changed = true;
  }
  if (selectedTrigger_ >= 0 && selectedTrigger_ < static_cast<int>(s.triggers.size())) {
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
      s.triggers.push_back(s.triggers[static_cast<size_t>(selectedTrigger_)]);
      s.triggers.back().name += " (copy)";
      selectedTrigger_ = static_cast<int>(s.triggers.size()) - 1;
      changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
      const int gone = selectedTrigger_;
      s.triggers.erase(s.triggers.begin() + gone);
      for (Trigger& t : s.triggers) {
        for (Condition& c : t.conditions) c.trigger = c.trigger == gone ? -1 : c.trigger > gone ? c.trigger - 1 : c.trigger;
        for (Action& x : t.actions) x.trigger = x.trigger == gone ? -1 : x.trigger > gone ? x.trigger - 1 : x.trigger;
      }
      selectedTrigger_ = -1;
      changed = true;
    }
  }
  if (ImGui::BeginListBox("##triggers", ImVec2(-1, std::min(7, std::max(2, static_cast<int>(s.triggers.size()))) * 20.0f + 6))) {
    for (int i = 0; i < static_cast<int>(s.triggers.size()); ++i) {
      const Trigger& t = s.triggers[static_cast<size_t>(i)];
      const bool fired = scene_.sim().triggerFired(i);
      char label[160];
      std::snprintf(label, sizeof label, "%d. %s%s%s##t%d", i + 1, t.name.c_str(), t.repeat ? "  (repeats)" : "",
                    fired ? "  - fired" : "", i);
      ImGui::PushStyleColor(ImGuiCol_Text, !t.enabled ? ImVec4(0.5f, 0.5f, 0.5f, 1) : fired ? ImVec4(0.6f, 0.9f, 0.55f, 1)
                                                                                          : ImGui::GetStyleColorVec4(ImGuiCol_Text));
      if (ImGui::Selectable(label, selectedTrigger_ == i)) selectedTrigger_ = i;
      ImGui::PopStyleColor();
    }
    ImGui::EndListBox();
  }
  if (selectedTrigger_ >= 0 && selectedTrigger_ < static_cast<int>(s.triggers.size())) {
    Trigger& t = s.triggers[static_cast<size_t>(selectedTrigger_)];
    ImGui::SetNextItemWidth(-60);
    changed |= ImGui::InputText("Name", &t.name);
    changed |= ImGui::Checkbox("On", &t.enabled);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Repeats", &t.repeat);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fire every time the conditions hold (at most once a second), not just once.");

    ImGui::TextColored(kGold, "When all of these hold:");
    for (size_t i = 0; i < t.conditions.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      changed |= editCondition(t.conditions[i]);
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) {
        t.conditions.erase(t.conditions.begin() + static_cast<long>(i));
        changed = true;
        ImGui::PopID();
        break;
      }
      ImGui::PopID();
    }
    if (ImGui::SmallButton("+ condition")) {
      t.conditions.push_back({});
      changed = true;
    }
    ImGui::TextColored(kGold, "Do this:");
    for (size_t i = 0; i < t.actions.size(); ++i) {
      ImGui::PushID(1000 + static_cast<int>(i));
      changed |= editAction(t.actions[i]);
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) {
        t.actions.erase(t.actions.begin() + static_cast<long>(i));
        changed = true;
        ImGui::PopID();
        break;
      }
      ImGui::PopID();
    }
    if (ImGui::SmallButton("+ action")) {
      t.actions.push_back({});
      changed = true;
    }
  }
  if (changed) scene_.scenarioChanged();
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextDisabled("Saved with the map. Test it with Battle > Snapshot / Restore.");
}

}  // namespace legion

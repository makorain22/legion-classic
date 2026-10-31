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

// --- Plain-English summaries --------------------------------------------------
namespace {
std::string sideName(GameScene& scene, uint8_t p) {
  return p == ANY_PLAYER ? std::string("everyone") : scene.players()[p % MAX_PLAYERS].name;
}
}  // namespace

std::string Editor::describeCondition(const Condition& c) const {
  auto& sc = const_cast<GameScene&>(scene_);
  const ScenarioScript& s = scene_.scenario();
  auto area = [&](int i) {
    return i >= 0 && i < static_cast<int>(s.areas.size()) ? "'" + s.areas[static_cast<size_t>(i)].name + "'" : std::string("(no area)");
  };
  auto units = [&](int t, bool plural = true) {
    return t >= 0 && t < static_cast<int>(scene_.types().size()) ? scene_.types().get(t).name + (plural ? "s" : "")
                                                                 : std::string(plural ? "units" : "unit");
  };
  switch (c.type) {
    case CondType::Always: return "at the start";
    case CondType::Timer: return std::to_string(c.value) + " seconds have passed";
    case CondType::UnitsInArea:
      return sideName(sc, c.player) + " has " + std::to_string(std::max(1, c.value)) + " or more " + units(c.unitType) + " in " + area(c.area);
    case CondType::NoUnitsInArea: return sideName(sc, c.player) + " has no " + units(c.unitType) + " left in " + area(c.area);
    case CondType::UnitCount:
      return sideName(sc, c.player) + " is down to " + std::to_string(c.value) + " " + units(c.unitType) + " or fewer";
    case CondType::PlayerDefeated: return sideName(sc, c.player) + " is defeated";
    case CondType::Supplies: return sideName(sc, c.player) + " has $" + std::to_string(c.value) + " or more";
    case CondType::TriggerFired:
      return "trigger " + (c.trigger >= 0 && c.trigger < static_cast<int>(s.triggers.size())
                               ? "'" + s.triggers[static_cast<size_t>(c.trigger)].name + "'" : std::string("(none)")) + " has fired";
    default: return condName(c.type);
  }
}

std::string Editor::describeAction(const Action& a) const {
  auto& sc = const_cast<GameScene&>(scene_);
  const ScenarioScript& s = scene_.scenario();
  auto area = [&](int i) {
    return i >= 0 && i < static_cast<int>(s.areas.size()) ? "'" + s.areas[static_cast<size_t>(i)].name + "'" : std::string("(no area)");
  };
  auto units = [&](int t) {
    return t >= 0 && t < static_cast<int>(scene_.types().size()) ? scene_.types().get(t).name + "s" : std::string("units");
  };
  auto trig = [&](int t) {
    return t >= 0 && t < static_cast<int>(s.triggers.size()) ? "'" + s.triggers[static_cast<size_t>(t)].name + "'" : std::string("(none)");
  };
  switch (a.type) {
    case ActionType::Message: return "tell " + sideName(sc, a.player) + ": \"" + a.text + "\"";
    case ActionType::AddObjective: return std::string(a.value == 1 ? "give an optional objective" : "give the objective") + " \"" + a.text + "\" to " + sideName(sc, a.player);
    case ActionType::CompleteObjective: return "mark objective " + std::to_string(a.value + 1) + " done";
    case ActionType::FailObjective: return "mark objective " + std::to_string(a.value + 1) + " failed";
    case ActionType::Victory: return sideName(sc, a.player) + " wins the battle";
    case ActionType::Defeat: return sideName(sc, a.player) + " loses the battle";
    case ActionType::SpawnUnits: return "bring in " + std::to_string(a.value) + " " + units(a.unitType) + " for " + sideName(sc, a.player) + " at " + area(a.area);
    case ActionType::GiveSupplies: return "give " + sideName(sc, a.player) + " $" + std::to_string(a.value);
    case ActionType::RevealArea: return "show " + area(a.area) + " to " + sideName(sc, a.player);
    case ActionType::AttackMove: return sideName(sc, a.player) + "'s " + units(a.unitType) + " in " + area(a.area) + " attack towards " + area(a.area2);
    case ActionType::MoveUnits: return sideName(sc, a.player) + "'s " + units(a.unitType) + " in " + area(a.area) + " move to " + area(a.area2);
    case ActionType::ChangeOwner: return sideName(sc, a.player) + "'s " + units(a.unitType) + " in " + area(a.area) + " go over to " + sideName(sc, a.player2);
    case ActionType::KillUnits: return "destroy " + sideName(sc, a.player) + "'s " + units(a.unitType) + " in " + area(a.area);
    case ActionType::EnableTrigger: return "switch on trigger " + trig(a.trigger);
    case ActionType::DisableTrigger: return "switch off trigger " + trig(a.trigger);
    case ActionType::SetComputer: return sideName(sc, a.player) + " is played by " + (a.value < 0 ? std::string("a human") : "the computer");
    case ActionType::CameraTo: return "move " + sideName(sc, a.player) + "'s view to " + area(a.area);
    default: return actionName(a.type);
  }
}

// Ready-made triggers for the usual mission pieces.
void Editor::addTemplate(int which) {
  ScenarioScript& s = scene_.scenario();
  const uint8_t me = 0, enemy = 1;
  int area = selectedArea_ >= 0 && selectedArea_ < static_cast<int>(s.areas.size()) ? selectedArea_ : (s.areas.empty() ? -1 : 0);
  // How many objectives exist already (they are numbered in order).
  int objectives = 0;
  for (const Trigger& t : s.triggers)
    for (const Action& x : t.actions) objectives += x.type == ActionType::AddObjective;
  Trigger t;
  Action a;
  Condition c;
  switch (which) {
    case 0: {  // briefing
      t.name = "Briefing";
      a.type = ActionType::Message;
      a.player = ANY_PLAYER;
      a.text = "Hold the line until reinforcements arrive.";
      t.actions.push_back(a);
      Action o;
      o.type = ActionType::AddObjective;
      o.player = me;
      o.text = "Destroy the enemy";
      t.actions.push_back(o);
      t.conditions.push_back(c);
      break;
    }
    case 1: {  // capture an area
      t.name = "Capture " + (area >= 0 ? s.areas[static_cast<size_t>(area)].name : std::string("the objective"));
      c.type = CondType::UnitsInArea;
      c.player = me;
      c.area = area;
      c.value = 3;
      t.conditions.push_back(c);
      Condition c2;
      c2.type = CondType::NoUnitsInArea;
      c2.player = enemy;
      c2.area = area;
      t.conditions.push_back(c2);
      Trigger brief;
      brief.name = t.name + " (objective)";
      Action o;
      o.type = ActionType::AddObjective;
      o.player = me;
      o.text = t.name;
      brief.actions.push_back(o);
      brief.conditions.push_back({});
      s.triggers.push_back(brief);
      a.type = ActionType::CompleteObjective;
      a.value = objectives;
      t.actions.push_back(a);
      Action m;
      m.type = ActionType::Message;
      m.player = me;
      m.text = "The area is ours.";
      t.actions.push_back(m);
      break;
    }
    case 2: {  // destroy the enemy -> victory
      t.name = "Victory when the enemy is beaten";
      c.type = CondType::PlayerDefeated;
      c.player = enemy;
      t.conditions.push_back(c);
      a.type = ActionType::Victory;
      a.player = me;
      t.actions.push_back(a);
      break;
    }
    case 3: {  // reinforcements on a timer
      t.name = "Reinforcements";
      c.type = CondType::Timer;
      c.value = 120;
      t.conditions.push_back(c);
      a.type = ActionType::SpawnUnits;
      a.player = me;
      a.unitType = scene_.types().find(scene_.players()[me].faction + "_medium_tank");
      a.value = 3;
      a.area = area;
      t.actions.push_back(a);
      Action m;
      m.type = ActionType::Message;
      m.player = me;
      m.text = "Reinforcements have arrived.";
      t.actions.push_back(m);
      break;
    }
    case 4: {  // enemy counter-attack
      t.name = "Enemy counter-attack";
      c.type = CondType::Timer;
      c.value = 300;
      t.conditions.push_back(c);
      a.type = ActionType::AttackMove;
      a.player = enemy;
      a.area = area;
      a.area2 = s.areas.size() > 1 ? 1 : area;
      t.actions.push_back(a);
      break;
    }
    default: {  // lose when everything is gone
      t.name = "Defeat when our force is gone";
      c.type = CondType::UnitCount;
      c.player = me;
      c.value = 0;
      t.conditions.push_back(c);
      a.type = ActionType::Defeat;
      a.player = me;
      t.actions.push_back(a);
      break;
    }
  }
  s.triggers.push_back(std::move(t));
  selectedTrigger_ = static_cast<int>(s.triggers.size()) - 1;
  editingCond_ = editingAction_ = -1;
  scriptPage_ = 2;
}

void Editor::drawAreaPage(bool& changed) {
  ScenarioScript& s = scene_.scenario();
  ImGui::TextWrapped("Areas are named rectangles of the map that triggers watch and act on: an objective, a "
                     "landing zone, the route of an attack.");
  const bool drawing = tool_ == Tool::Area;
  if (drawing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.50f, 0.22f, 1));
  if (ImGui::Button(drawing ? "Drag a rectangle on the map... (click to stop)" : "+ Draw a new area", ImVec2(-1, 0)))
    tool_ = drawing ? Tool::SelectUnits : Tool::Area;
  if (drawing) ImGui::PopStyleColor();
  if (ImGui::BeginListBox("##areas", ImVec2(-1, std::min(8, std::max(3, static_cast<int>(s.areas.size()))) * 20.0f + 6))) {
    for (int i = 0; i < static_cast<int>(s.areas.size()); ++i) {
      const ScriptArea& a = s.areas[static_cast<size_t>(i)];
      char label[128];
      std::snprintf(label, sizeof label, "%s  (%d x %d tiles)##a%d", a.name.c_str(), a.x1 - a.x0 + 1, a.y1 - a.y0 + 1, i);
      if (ImGui::Selectable(label, selectedArea_ == i)) {
        selectedArea_ = i;
        scene_.centerOn({(a.x0 + a.x1) * 128, (a.y0 + a.y1) * 128});
      }
    }
    if (s.areas.empty()) ImGui::TextDisabled("No areas yet.");
    ImGui::EndListBox();
  }
  if (selectedArea_ >= 0 && selectedArea_ < static_cast<int>(s.areas.size())) {
    ScriptArea& a = s.areas[static_cast<size_t>(selectedArea_)];
    ImGui::SetNextItemWidth(-60);
    changed |= ImGui::InputText("Name", &a.name);
    // Which triggers use it.
    int uses = 0;
    for (const Trigger& t : s.triggers) {
      for (const Condition& c : t.conditions) uses += c.area == selectedArea_;
      for (const Action& x : t.actions) uses += (x.area == selectedArea_) + (x.area2 == selectedArea_);
    }
    ImGui::TextDisabled(uses ? "Used %d times by triggers." : "Not used by any trigger yet.", uses);
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
  }
}

void Editor::drawTriggerPage(bool& changed) {
  ScenarioScript& s = scene_.scenario();
  if (ImGui::Button("+ Blank trigger")) {
    Trigger t;
    t.name = "Trigger " + std::to_string(s.triggers.size() + 1);
    t.conditions.push_back({});
    Action a;
    a.type = ActionType::Message;
    a.text = "Something happens.";
    t.actions.push_back(a);
    s.triggers.push_back(std::move(t));
    selectedTrigger_ = static_cast<int>(s.triggers.size()) - 1;
    editingCond_ = editingAction_ = -1;
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
  if (ImGui::BeginListBox("##triggers", ImVec2(-1, std::min(6, std::max(3, static_cast<int>(s.triggers.size()))) * 20.0f + 6))) {
    for (int i = 0; i < static_cast<int>(s.triggers.size()); ++i) {
      const Trigger& t = s.triggers[static_cast<size_t>(i)];
      const bool fired = scene_.sim().triggerFired(i);
      char label[160];
      std::snprintf(label, sizeof label, "%d. %s%s%s%s##t%d", i + 1, t.name.c_str(), t.repeat ? "  (repeats)" : "",
                    !t.enabled ? "  (off)" : "", fired ? "  - fired" : "", i);
      ImGui::PushStyleColor(ImGuiCol_Text, !t.enabled ? ImVec4(0.5f, 0.5f, 0.5f, 1) : fired ? ImVec4(0.6f, 0.9f, 0.55f, 1)
                                                                                          : ImGui::GetStyleColorVec4(ImGuiCol_Text));
      if (ImGui::Selectable(label, selectedTrigger_ == i)) {
        selectedTrigger_ = i;
        editingCond_ = editingAction_ = -1;
      }
      ImGui::PopStyleColor();
    }
    if (s.triggers.empty()) ImGui::TextDisabled("No triggers yet: try the Quick start page.");
    ImGui::EndListBox();
  }
  if (selectedTrigger_ < 0 || selectedTrigger_ >= static_cast<int>(s.triggers.size())) return;
  Trigger& t = s.triggers[static_cast<size_t>(selectedTrigger_)];
  ImGui::SetNextItemWidth(-60);
  changed |= ImGui::InputText("Name", &t.name);
  changed |= ImGui::Checkbox("Switched on", &t.enabled);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("A switched-off trigger waits until another trigger switches it on.");
  ImGui::SameLine();
  changed |= ImGui::Checkbox("Repeats", &t.repeat);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fire every time the conditions hold (at most once a second), not just once.");

  // One framed line per condition and action: the sentence, and its settings when opened.
  auto line = [&](bool when, int i, const std::string& text, bool editing) -> int {  // 1 edit, 2 remove
    int result = 0;
    ImGui::PushID((when ? 0 : 1000) + i);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
    ImGui::Dummy(ImVec2(4, 0));
    ImGui::SameLine();
    ImGui::TextColored(when ? ImVec4(0.55f, 0.75f, 0.95f, 1) : ImVec4(0.95f, 0.75f, 0.4f, 1), i == 0 ? (when ? "WHEN" : "THEN") : (when ? " AND" : " AND"));
    ImGui::SameLine();
    ImGui::PushTextWrapPos(x0 + w - 74);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SameLine(x0 + w - 66);
    if (ImGui::SmallButton(editing ? "done" : "edit")) result = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) result = 2;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this line");
    const ImVec2 p1(p0.x + w, ImGui::GetItemRectMax().y + 3);
    ImGui::GetWindowDrawList()->AddRect(p0, p1, editing ? IM_COL32(214, 176, 92, 200) : IM_COL32(90, 90, 70, 160), 3.0f);
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PopID();
    return result;
  };

  ImGui::Dummy(ImVec2(0, 4));
  for (int i = 0; i < static_cast<int>(t.conditions.size()); ++i) {
    const int r = line(true, i, describeCondition(t.conditions[static_cast<size_t>(i)]), editingCond_ == i);
    if (r == 1) editingCond_ = editingCond_ == i ? -1 : i;
    if (r == 2) {
      t.conditions.erase(t.conditions.begin() + i);
      editingCond_ = -1;
      changed = true;
      break;
    }
    if (editingCond_ == i) {
      ImGui::PushID(i);
      ImGui::Indent(14);
      changed |= editCondition(t.conditions[static_cast<size_t>(i)]);
      ImGui::Unindent(14);
      ImGui::PopID();
      ImGui::Dummy(ImVec2(0, 4));
    }
  }
  if (ImGui::Button("+ condition")) ImGui::OpenPopup("##addcond");
  if (ImGui::BeginPopup("##addcond")) {
    for (int i = 0; i < static_cast<int>(CondType::Count); ++i)
      if (ImGui::Selectable(condName(static_cast<CondType>(i)))) {
        Condition c;
        c.type = static_cast<CondType>(i);
        if (c.type == CondType::Timer) c.value = 60;
        if (c.type == CondType::UnitsInArea) c.value = 1;
        if (!s.areas.empty()) c.area = selectedArea_ >= 0 ? selectedArea_ : 0;
        t.conditions.push_back(c);
        editingCond_ = static_cast<int>(t.conditions.size()) - 1;
        changed = true;
      }
    ImGui::EndPopup();
  }
  ImGui::Dummy(ImVec2(0, 4));
  for (int i = 0; i < static_cast<int>(t.actions.size()); ++i) {
    const int r = line(false, i, describeAction(t.actions[static_cast<size_t>(i)]), editingAction_ == i);
    if (r == 1) editingAction_ = editingAction_ == i ? -1 : i;
    if (r == 2) {
      t.actions.erase(t.actions.begin() + i);
      editingAction_ = -1;
      changed = true;
      break;
    }
    if (editingAction_ == i) {
      ImGui::PushID(1000 + i);
      ImGui::Indent(14);
      changed |= editAction(t.actions[static_cast<size_t>(i)]);
      ImGui::Unindent(14);
      ImGui::PopID();
      ImGui::Dummy(ImVec2(0, 4));
    }
  }
  if (ImGui::Button("+ action")) ImGui::OpenPopup("##addaction");
  if (ImGui::BeginPopup("##addaction")) {
    for (int i = 0; i < static_cast<int>(ActionType::Count); ++i)
      if (ImGui::Selectable(actionName(static_cast<ActionType>(i)))) {
        Action a;
        a.type = static_cast<ActionType>(i);
        if (!s.areas.empty()) a.area = selectedArea_ >= 0 ? selectedArea_ : 0;
        if (a.type == ActionType::SpawnUnits) a.value = 1;
        t.actions.push_back(a);
        editingAction_ = static_cast<int>(t.actions.size()) - 1;
        changed = true;
      }
    ImGui::EndPopup();
  }
}

void Editor::drawScriptTab() {
  ScenarioScript& s = scene_.scenario();
  bool changed = false;
  const char* pages[4] = {"Quick start", "Areas", "Triggers", "Objectives"};
  const float pw = (ImGui::GetContentRegionAvail().x - 12) / 4.0f;
  for (int i = 0; i < 4; ++i) {
    if (i) ImGui::SameLine(0, 4);
    const bool on = scriptPage_ == i;
    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.55f, 0.45f, 0.22f, 1) : ImVec4(0.22f, 0.22f, 0.18f, 1));
    if (ImGui::Button(pages[i], ImVec2(pw, 0))) scriptPage_ = i;
    ImGui::PopStyleColor();
  }
  ImGui::Separator();
  if (scriptPage_ == 0) {
    ImGui::TextWrapped("A mission is a battle with rules. Three steps:");
    ImGui::TextColored(kGold, "1.");
    ImGui::SameLine();
    ImGui::TextWrapped("Name the places that matter: Areas page, then drag on the map.");
    ImGui::TextColored(kGold, "2.");
    ImGui::SameLine();
    ImGui::TextWrapped("Add triggers: WHEN something happens, THEN do something. Start from a ready-made one below "
                       "and change it on the Triggers page.");
    ImGui::TextColored(kGold, "3.");
    ImGui::SameLine();
    ImGui::TextWrapped("Test: Battle tab > Snapshot, play, then Restore.");
    ImGui::SeparatorText("Ready-made triggers");
    const char* names[6] = {"Briefing and main objective", "Capture an area (uses the selected area)",
                            "Victory when the enemy is beaten", "Reinforcements after 2 minutes",
                            "Enemy counter-attack after 5 minutes", "Defeat when our force is gone"};
    for (int i = 0; i < 6; ++i)
      if (ImGui::Button(names[i], ImVec2(-1, 0))) {
        addTemplate(i);
        changed = true;
      }
    ImGui::TextDisabled("Side 1 is 'us', side 2 the enemy; change them in each line.");
  } else if (scriptPage_ == 1) {
    drawAreaPage(changed);
  } else if (scriptPage_ == 2) {
    drawTriggerPage(changed);
  } else {
    ImGui::TextWrapped("Objectives appear in the order their 'add objective' actions run. "
                       "'Mark objective N done' refers to these numbers.");
    int k = 0;
    for (int ti = 0; ti < static_cast<int>(s.triggers.size()); ++ti)
      for (const Action& x : s.triggers[static_cast<size_t>(ti)].actions)
        if (x.type == ActionType::AddObjective) {
          ++k;
          ImGui::TextColored(kGold, "%d.", k);
          ImGui::SameLine();
          ImGui::TextWrapped("%s%s  (trigger '%s')", x.text.c_str(), x.value == 1 ? " [optional]" : "",
                             s.triggers[static_cast<size_t>(ti)].name.c_str());
        }
    if (k == 0) ImGui::TextDisabled("No objectives yet.");
  }
  if (changed) scene_.scenarioChanged();
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::TextDisabled("Saved with the map.");
}

}  // namespace legion

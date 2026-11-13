// Pioneer Editor - the Script tab: named areas, triggers and objectives.
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdio>

#include "editor.hpp"
#include "game_scene.hpp"

using namespace pioneer;

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
  ImGui::SameLine();
  ch |= ImGui::Checkbox("not", &c.negate);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("The condition holds when this is NOT the case.");
  ImGui::Indent();
  auto name = [&](const char* hint) {
    ImGui::SetNextItemWidth(170);
    ch |= ImGui::InputTextWithHint("##name", hint, &c.text);
  };
  auto num = [&](const char* label, int step = 1, int fast = 10) {
    ImGui::SetNextItemWidth(100);
    ch |= ImGui::InputInt(label, &c.value, step, fast);
  };
  switch (c.type) {
    case CondType::NamedAlive:
    case CondType::NamedDead: name("unit name"); break;
    case CondType::NamedInArea:
      name("unit name");
      ch |= areaCombo("area", c.area);
      num("at least");
      break;
    case CondType::NamedHealthBelow:
      name("unit name");
      num("percent health");
      break;
    case CondType::StructuresAtMost:
    case CondType::StructuresAtLeast:
    case CondType::UnitsInAreaAtMost:
      ch |= playerCombo("side", c.player, true);
      ch |= areaCombo("area", c.area);
      if (ImGui::SmallButton("whole map")) {
        c.area = -1;
        ch = true;
      }
      num(c.type == CondType::StructuresAtLeast ? "at least" : "at most");
      ch |= unitCombo("of", c.unitType, true);
      break;
    case CondType::AreaControlled:
      ch |= playerCombo("side", c.player, false);
      ch |= areaCombo("area", c.area);
      num("units at least");
      break;
    case CondType::CountdownDone: name("countdown"); break;
    case CondType::CountdownBelow:
      name("countdown");
      num("seconds left");
      break;
    case CondType::CounterAtLeast:
    case CondType::CounterAtMost:
      name("counter");
      num(c.type == CondType::CounterAtLeast ? "at least" : "at most");
      break;
    case CondType::KillsAtLeast:
    case CondType::LossesAtLeast:
      ch |= playerCombo("side", c.player, false);
      num("at least");
      break;
    case CondType::ObjectiveDone:
    case CondType::ObjectiveFailed: {
      ImGui::SetNextItemWidth(100);
      int n = c.value + 1;
      if (ImGui::InputInt("objective no.", &n)) {
        c.value = std::max(0, n - 1);
        ch = true;
      }
      break;
    }
    case CondType::DifficultyAtLeast:
      ImGui::SetNextItemWidth(170);
      ch |= ImGui::Combo("difficulty", &c.value, "Easy\0Normal\0Hard\0");
      break;
    case CondType::Chance: num("percent"); break;
    case CondType::SuppliesAtMost:
      ch |= playerCombo("side", c.player, false);
      num("supplies", 100, 1000);
      break;
    case CondType::HealthBelow:
      ch |= playerCombo("side", c.player, false);
      ch |= areaCombo("area", c.area);
      ch |= unitCombo("of", c.unitType, true);
      num("percent");
      break;
    case CondType::AlliedWith: {
      ch |= playerCombo("side", c.player, false);
      uint8_t other = static_cast<uint8_t>(std::clamp(c.value, 0, MAX_PLAYERS - 1));
      if (playerCombo("with", other, false)) {
        c.value = other;
        ch = true;
      }
      break;
    }
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
    case CondType::TriggerFired:
      ch |= triggerCombo("trigger", c.trigger);
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("seconds ago", &c.value);
      break;
    case CondType::UnitsAtLeast:
      ch |= playerCombo("side", c.player, false);
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("at least", &c.value);
      ch |= unitCombo("of", c.unitType, true);
      break;
    case CondType::InCover:
      ch |= playerCombo("side", c.player, false);
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("men at least", &c.value);
      break;
    case CondType::OrderGiven: {
      ch |= playerCombo("side", c.player, false);
      static const char* kOrders[] = {"any", "move", "attack_move", "attack", "stop", "produce", "place_building",
                                      "place_walls", "sell", "deploy", "set_stance", "repair", "occupy", "unload"};
      int cur = 0;
      for (int i = 1; i < static_cast<int>(sizeof(kOrders) / sizeof(kOrders[0])); ++i)
        if (orderFromId(kOrders[i]) == c.order) cur = i;
      ImGui::SetNextItemWidth(170);
      if (ImGui::Combo("order", &cur, kOrders, static_cast<int>(sizeof(kOrders) / sizeof(kOrders[0])))) {
        c.order = cur == 0 ? -1 : orderFromId(kOrders[cur]);
        ch = true;
      }
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("times", &c.value);
      break;
    }
    case CondType::FlagAtLeast:
      ImGui::SetNextItemWidth(170);
      ch |= ImGui::InputTextWithHint("##flag", "flag name", &c.text);
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("at least", &c.value);
      break;
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
  auto num = [&](const char* label, int step = 1, int fast = 10) {
    ImGui::SetNextItemWidth(100);
    ch |= ImGui::InputInt(label, &a.value, step, fast);
  };
  auto onOff = [&](const char* on, const char* off) {
    int v = a.value ? 0 : 1;
    ImGui::SetNextItemWidth(170);
    const std::string items = std::string(on) + '\0' + off + '\0' + '\0';
    if (ImGui::Combo("##onoff", &v, items.c_str())) {
      a.value = v == 0 ? 1 : 0;
      ch = true;
    }
  };
  // Units are picked by name, or by side, area and type.
  auto pickUnits = [&]() {
    int by = a.text.empty() && !pickByName_ ? 0 : 1;
    if (ImGui::RadioButton("by side and area", by == 0)) {
      a.text.clear();
      pickByName_ = false;
      ch = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("by name", by == 1)) {
      pickByName_ = true;
      by = 1;
    }
    if (by == 1) {
      ImGui::SetNextItemWidth(170);
      ch |= ImGui::InputTextWithHint("##uname", "unit name", &a.text);
    } else {
      ch |= playerCombo("side", a.player, true);
      ch |= areaCombo("in", a.area);
      ch |= unitCombo("units", a.unitType, true);
    }
  };
  auto scaledBox = [&]() {
    ch |= ImGui::Checkbox("more on hard, fewer on easy", &a.scaled);
  };
  switch (a.type) {
    case ActionType::Countdown:
      ch |= playerCombo("shown to", a.player, true);
      text("label, e.g. Columns across");
      num("seconds", 10, 60);
      break;
    case ActionType::StopCountdown: text("countdown label"); break;
    case ActionType::PauseCountdown:
      text("countdown label");
      onOff("pause", "run on");
      break;
    case ActionType::SetPlayArea:
      ch |= playerCombo("for", a.player, true);
      ch |= areaCombo("area", a.area);
      if (ImGui::SmallButton("the whole map")) {
        a.area = -1;
        ch = true;
      }
      break;
    case ActionType::NameUnits:
      ch |= playerCombo("side", a.player, true);
      ch |= areaCombo("in", a.area);
      ch |= unitCombo("units", a.unitType, true);
      text("the name");
      num("at most (0: all)");
      break;
    case ActionType::SetInvulnerable: pickUnits(); onOff("invulnerable", "can be hurt"); break;
    case ActionType::SetAiControl: pickUnits(); onOff("computer may command", "script only"); break;
    case ActionType::SetTargetable: pickUnits(); onOff("a target", "never targeted"); break;
    case ActionType::SetLocked: pickUnits(); onOff("locked from orders", "takes orders"); break;
    case ActionType::SetHealth: pickUnits(); num("percent health", 5, 25); break;
    case ActionType::DamageUnits: pickUnits(); num("percent of max", 5, 25); break;
    case ActionType::SetStance:
      pickUnits();
      ImGui::SetNextItemWidth(170);
      ch |= ImGui::Combo("stance", &a.value, "Aggressive\0Hold ground\0Hold fire\0");
      break;
    case ActionType::Patrol:
      pickUnits();
      ch |= areaCombo("between", a.area);
      ch |= areaCombo("and", a.area2);
      break;
    case ActionType::Garrison:
      pickUnits();
      ch |= areaCombo("into buildings in", a.area2);
      break;
    case ActionType::Evacuate:
      ch |= playerCombo("side", a.player, true);
      ch |= areaCombo("buildings in", a.area);
      break;
    case ActionType::BindUnits: pickUnits(); break;
    case ActionType::Barrage:
      ch |= playerCombo("fired by", a.player, true);
      ch |= areaCombo("on", a.area);
      num("shells", 5, 20);
      text("weapon id (empty: howitzer_105; also howitzer_150, rocket_salvo, gun_55in...)");
      break;
    case ActionType::FreezeWater:
    case ActionType::BreakIce:
    case ActionType::FloodArea:
    case ActionType::DrainArea:
      ch |= areaCombo("in", a.area);
      break;
    case ActionType::SetAlliance: {
      ch |= playerCombo("side", a.player, false);
      uint8_t other = a.player2;
      if (playerCombo("and", other, false)) {
        a.player2 = other;
        ch = true;
      }
      onOff("become allies", "become enemies");
      break;
    }
    case ActionType::AiAttack:
    case ActionType::AiDefend:
      ch |= playerCombo("computer side", a.player, false);
      ch |= areaCombo(a.type == ActionType::AiAttack ? "attack" : "defend", a.area);
      break;
    case ActionType::AiFree: ch |= playerCombo("computer side", a.player, true); break;
    case ActionType::AiBehaviour: {
      ch |= playerCombo("computer side", a.player, true);
      int b = std::max(0, aiBehaviourFromId(a.text));
      ImGui::SetNextItemWidth(170);
      if (ImGui::Combo("behaviour", &b, "build\0train\0attack\0raid\0capture\0fortify\0scout\0repair\0")) ch = true;
      if (a.text != kAiBehaviourIds[b]) {
        a.text = kAiBehaviourIds[b];
        ch = true;
      }
      onOff("allowed", "forbidden");
      break;
    }
    case ActionType::AiWaveSize:
      ch |= playerCombo("computer side", a.player, true);
      num("percent (0: usual)", 10, 50);
      break;
    case ActionType::SetCounter:
    case ActionType::AddCounter:
      text("counter name");
      num(a.type == ActionType::SetCounter ? "value" : "add");
      break;
    case ActionType::AddFlag:
      text("campaign flag");
      num("add");
      break;
    case ActionType::SpawnFromFlag:
      ch |= playerCombo("for", a.player, false);
      ch |= unitCombo("unit", a.unitType, false);
      ch |= areaCombo("in", a.area);
      text("campaign flag (its value is how many)");
      num("at most (0: 50)");
      break;
    case ActionType::SpawnNamed:
      ch |= playerCombo("for", a.player, false);
      ch |= unitCombo("unit", a.unitType, false);
      num("how many");
      ch |= areaCombo("in", a.area);
      text("their name");
      scaledBox();
      break;
    case ActionType::Transmission:
      ch |= playerCombo("to", a.player, true);
      ImGui::TextDisabled("\"Speaker: words\" - e.g. Major Adler: Hold the bridge.");
      ch |= ImGui::InputTextMultiline("##radio", &a.text, ImVec2(-1, 64));
      num("seconds (0: by length)");
      break;
    case ActionType::Cinematic:
      ch |= playerCombo("for", a.player, true);
      onOff("letterbox, no orders", "back to play");
      break;
    case ActionType::CameraPan:
      ch |= playerCombo("for", a.player, true);
      ch |= areaCombo("to", a.area);
      num("seconds");
      break;
    case ActionType::MarkArea:
      ch |= playerCombo("for", a.player, true);
      ch |= areaCombo("area", a.area);
      text("label");
      ImGui::SetNextItemWidth(170);
      ch |= ImGui::Combo("kind", &a.value, "Goal\0Danger\0Note\0");
      break;
    case ActionType::ClearMarker: text("marker label (empty: all)"); break;
    case ActionType::PlaySound:
      ch |= playerCombo("for", a.player, true);
      text("sound (audio.json name: radio, whistle, explosion_large...)");
      ch |= areaCombo("at", a.area);
      if (ImGui::SmallButton("everywhere")) {
        a.area = -1;
        ch = true;
      }
      break;
    case ActionType::PlayMusic:
      ch |= playerCombo("for", a.player, true);
      text("calm, battle, victory, defeat, menu, a title (e.g. Hold the Line); empty: automatic");
      break;
    case ActionType::SetIncomeRate:
      ch |= playerCombo("side", a.player, true);
      num("percent", 10, 50);
      break;
    case ActionType::AddLogisticsPoint: ch |= areaCombo("in the middle of", a.area); break;
    case ActionType::FireTrigger:
    case ActionType::ResetTrigger: ch |= triggerCombo("trigger", a.trigger); break;
    case ActionType::HideObjective: {
      ImGui::SetNextItemWidth(100);
      int n = a.value + 1;
      if (ImGui::InputInt("objective no.", &n)) {
        a.value = std::max(0, n - 1);
        ch = true;
      }
      break;
    }
    case ActionType::Message:
      ch |= playerCombo("to", a.player, true);
      text("What to say");
      break;
    case ActionType::AddObjective: {
      ch |= playerCombo("for", a.player, true);
      text("The objective");
      ch |= areaCombo("marked at", a.area);
      ImGui::SameLine();
      if (ImGui::SmallButton("no marker")) {
        a.area = -1;
        ch = true;
      }
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
      scaledBox();
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
      pickUnits();
      ch |= areaCombo("to", a.area2);
      break;
    case ActionType::ChangeOwner:
      pickUnits();
      {
        uint8_t to = a.player2;
        if (playerCombo("to side", to, false)) {
          a.player2 = to;
          ch = true;
        }
      }
      break;
    case ActionType::KillUnits: pickUnits(); break;
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
    case ActionType::Hint:
      ch |= playerCombo("for", a.player, true);
      ImGui::TextDisabled("First line: the title. Empty: close the panel.");
      ch |= ImGui::InputTextMultiline("##hint", &a.text, ImVec2(-1, 90));
      break;
    case ActionType::Highlight:
      ch |= playerCombo("for", a.player, true);
      text("tab.build, tab.defend, tab.train, tab.vehicles, build:<unit id>, order.evacuate, order.deploy, order.sell, minimap");
      break;
    case ActionType::LockBuild:
    case ActionType::UnlockBuild:
      ch |= playerCombo("side", a.player, true);
      ch |= unitCombo("what", a.unitType, true);
      break;
    case ActionType::SetFlag:
      text("flag name");
      ImGui::SetNextItemWidth(100);
      ch |= ImGui::InputInt("value", &a.value);
      break;
    default: break;
  }
  ImGui::Unindent();
  return ch;
}

// --- The add menus, in groups -------------------------------------------------
namespace {
const char* kCondGroups[] = {"Time and the mission", "Units and places", "Named units", "Sides"};
const char* kActionGroups[] = {"Story and objectives", "Radio, camera and sound", "Units", "Set pieces",
                               "Computer players", "The battlefield", "Clocks, counters and flags", "Triggers"};
}  // namespace
const char* Editor::condGroup(int g) { return kCondGroups[g]; }
const char* Editor::actionGroup(int g) { return kActionGroups[g]; }
std::vector<int> Editor::condOrder() {
  using C = CondType;
  auto I = [](C c) { return static_cast<int>(c); };
  return {-1, I(C::Always), I(C::Timer), I(C::TriggerFired), I(C::CountdownDone), I(C::CountdownBelow), I(C::CounterAtLeast),
          I(C::CounterAtMost), I(C::FlagAtLeast), I(C::ObjectiveDone), I(C::ObjectiveFailed), I(C::DifficultyAtLeast), I(C::Chance),
          -2, I(C::UnitsInArea), I(C::NoUnitsInArea), I(C::UnitsInAreaAtMost), I(C::AreaControlled), I(C::UnitsAtLeast),
          I(C::UnitCount), I(C::StructuresAtLeast), I(C::StructuresAtMost), I(C::HealthBelow), I(C::InCover),
          -3, I(C::NamedAlive), I(C::NamedDead), I(C::NamedInArea), I(C::NamedHealthBelow),
          -4, I(C::PlayerDefeated), I(C::Supplies), I(C::SuppliesAtMost), I(C::KillsAtLeast), I(C::LossesAtLeast),
          I(C::AlliedWith), I(C::OrderGiven)};
}
std::vector<int> Editor::actionOrder() {
  using A = ActionType;
  auto I = [](A a) { return static_cast<int>(a); };
  return {-1, I(A::Message), I(A::AddObjective), I(A::CompleteObjective), I(A::FailObjective), I(A::HideObjective), I(A::Victory),
          I(A::Defeat), I(A::Hint), I(A::Highlight),
          -2, I(A::Transmission), I(A::Cinematic), I(A::CameraPan), I(A::CameraTo), I(A::MarkArea), I(A::ClearMarker),
          I(A::PlaySound), I(A::PlayMusic),
          -3, I(A::SpawnUnits), I(A::SpawnNamed), I(A::SpawnFromFlag), I(A::MoveUnits), I(A::AttackMove), I(A::Patrol),
          I(A::Garrison), I(A::Evacuate), I(A::BindUnits), I(A::SetStance), I(A::ChangeOwner), I(A::KillUnits),
          I(A::DamageUnits), I(A::SetHealth), I(A::NameUnits),
          -4, I(A::SetInvulnerable), I(A::SetAiControl), I(A::SetTargetable), I(A::SetLocked),
          -5, I(A::AiAttack), I(A::AiDefend), I(A::AiFree), I(A::AiBehaviour), I(A::AiWaveSize), I(A::SetComputer),
          I(A::SetAlliance),
          -6, I(A::SetPlayArea), I(A::RevealArea), I(A::Barrage), I(A::FreezeWater), I(A::BreakIce), I(A::FloodArea), I(A::DrainArea), I(A::AddLogisticsPoint), I(A::GiveSupplies),
          I(A::SetIncomeRate), I(A::LockBuild), I(A::UnlockBuild),
          -7, I(A::Countdown), I(A::StopCountdown), I(A::PauseCountdown), I(A::SetCounter), I(A::AddCounter), I(A::SetFlag),
          I(A::AddFlag),
          -8, I(A::EnableTrigger), I(A::DisableTrigger), I(A::FireTrigger), I(A::ResetTrigger)};
}

// --- Plain-English summaries --------------------------------------------------
namespace {
std::string sideName(GameScene& scene, uint8_t p) {
  return p == ANY_PLAYER ? std::string("everyone") : scene.players()[p % MAX_PLAYERS].name;
}
}  // namespace

std::string Editor::describeCondition(const Condition& c) const {
  if (c.negate) {
    Condition plain = c;
    plain.negate = false;
    return "NOT (" + describeCondition(plain) + ")";
  }
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
                               ? "'" + s.triggers[static_cast<size_t>(c.trigger)].name + "'" : std::string("(none)")) + " has fired" +
             (c.value > 0 ? ", " + std::to_string(c.value) + " s ago" : std::string());
    case CondType::UnitsAtLeast:
      return sideName(sc, c.player) + " has " + std::to_string(std::max(1, c.value)) + " or more " + units(c.unitType);
    case CondType::InCover: return sideName(sc, c.player) + " has " + std::to_string(std::max(1, c.value)) + " or more men in cover";
    case CondType::OrderGiven:
      return sideName(sc, c.player) + " has given " + std::to_string(std::max(1, c.value)) + " '" + orderId(c.order) + "' order(s)";
    case CondType::FlagAtLeast: return "flag '" + c.text + "' is " + std::to_string(c.value) + " or more";
    case CondType::NamedAlive: return "'" + c.text + "' is alive";
    case CondType::NamedDead: return "'" + c.text + "' is dead";
    case CondType::NamedInArea: return std::to_string(std::max(1, c.value)) + " or more of '" + c.text + "' are in " + area(c.area);
    case CondType::NamedHealthBelow: return "'" + c.text + "' is below " + std::to_string(c.value) + "% health";
    case CondType::StructuresAtMost:
      return sideName(sc, c.player) + " has " + std::to_string(c.value) + " or fewer " + units(c.unitType) + " (structures) " + (c.area >= 0 ? "in " + area(c.area) : std::string("on the map"));
    case CondType::StructuresAtLeast:
      return sideName(sc, c.player) + " has " + std::to_string(std::max(1, c.value)) + " or more " + units(c.unitType) + " (structures) " + (c.area >= 0 ? "in " + area(c.area) : std::string("on the map"));
    case CondType::UnitsInAreaAtMost: return sideName(sc, c.player) + " has " + std::to_string(c.value) + " or fewer " + units(c.unitType) + " in " + area(c.area);
    case CondType::AreaControlled: return sideName(sc, c.player) + " holds " + area(c.area) + " (" + std::to_string(std::max(1, c.value)) + "+ units, no enemy)";
    case CondType::CountdownDone: return "countdown '" + c.text + "' has run out";
    case CondType::CountdownBelow: return "countdown '" + c.text + "' is under " + std::to_string(c.value) + " s";
    case CondType::CounterAtLeast: return "counter '" + c.text + "' is " + std::to_string(c.value) + " or more";
    case CondType::CounterAtMost: return "counter '" + c.text + "' is " + std::to_string(c.value) + " or less";
    case CondType::KillsAtLeast: return sideName(sc, c.player) + " has destroyed " + std::to_string(std::max(1, c.value)) + "+ enemies";
    case CondType::LossesAtLeast: return sideName(sc, c.player) + " has lost " + std::to_string(std::max(1, c.value)) + "+ units";
    case CondType::ObjectiveDone: return "objective " + std::to_string(c.value + 1) + " is done";
    case CondType::ObjectiveFailed: return "objective " + std::to_string(c.value + 1) + " has failed";
    case CondType::DifficultyAtLeast: return std::string("played on ") + (c.value <= 0 ? "easy" : c.value == 1 ? "normal" : "hard") + " or harder";
    case CondType::Chance: return std::to_string(c.value) + "% chance (each check)";
    case CondType::SuppliesAtMost: return sideName(sc, c.player) + " has $" + std::to_string(c.value) + " or less";
    case CondType::HealthBelow: return sideName(sc, c.player) + "'s " + units(c.unitType) + " in " + area(c.area) + " are below " + std::to_string(c.value) + "% health";
    case CondType::AlliedWith: return sideName(sc, c.player) + " is allied with " + sideName(sc, static_cast<uint8_t>(std::clamp(c.value, 0, MAX_PLAYERS - 1)));
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
  // Units an action picks: by name, or by side, type and area.
  auto who = [&]() {
    if (!a.text.empty() && actionTakesUnits(a.type)) return "'" + a.text + "'";
    return sideName(sc, a.player) + "'s " + units(a.unitType) + " in " + area(a.area);
  };
  auto yes = [&](const char* on, const char* off) { return std::string(a.value ? on : off); };
  switch (a.type) {
    case ActionType::Message: return "tell " + sideName(sc, a.player) + ": \"" + a.text + "\"";
    case ActionType::AddObjective: return std::string(a.value == 1 ? "give an optional objective" : "give the objective") + " \"" + a.text + "\" to " + sideName(sc, a.player);
    case ActionType::CompleteObjective: return "mark objective " + std::to_string(a.value + 1) + " done";
    case ActionType::FailObjective: return "mark objective " + std::to_string(a.value + 1) + " failed";
    case ActionType::Victory: return sideName(sc, a.player) + " wins the battle";
    case ActionType::Defeat: return sideName(sc, a.player) + " loses the battle";
    case ActionType::SpawnUnits:
      return "bring in " + std::to_string(a.value) + " " + units(a.unitType) + " for " + sideName(sc, a.player) + " at " + area(a.area) + (a.scaled ? " (by difficulty)" : "");
    case ActionType::GiveSupplies: return "give " + sideName(sc, a.player) + " $" + std::to_string(a.value);
    case ActionType::RevealArea: return "show " + area(a.area) + " to " + sideName(sc, a.player);
    case ActionType::AttackMove: return who() + " attack towards " + area(a.area2);
    case ActionType::MoveUnits: return who() + " move to " + area(a.area2);
    case ActionType::ChangeOwner: return who() + " go over to " + sideName(sc, a.player2);
    case ActionType::KillUnits: return "destroy " + who();
    case ActionType::EnableTrigger: return "switch on trigger " + trig(a.trigger);
    case ActionType::DisableTrigger: return "switch off trigger " + trig(a.trigger);
    case ActionType::SetComputer: return sideName(sc, a.player) + " is played by " + (a.value < 0 ? std::string("a human") : "the computer");
    case ActionType::CameraTo: return "move " + sideName(sc, a.player) + "'s view to " + area(a.area);
    case ActionType::Hint: return a.text.empty() ? "close the hint" : "hint for " + sideName(sc, a.player) + ": \"" + a.text.substr(0, a.text.find('\n')) + "\"";
    case ActionType::Highlight: return a.text.empty() ? "stop highlighting" : "highlight " + a.text;
    case ActionType::LockBuild: return sideName(sc, a.player) + " can't build " + units(a.unitType);
    case ActionType::UnlockBuild: return sideName(sc, a.player) + " can build " + units(a.unitType) + " again";
    case ActionType::SetFlag: return "set campaign flag '" + a.text + "' to " + std::to_string(a.value);
    case ActionType::Countdown: return "start the clock '" + a.text + "' at " + std::to_string(a.value) + " s";
    case ActionType::StopCountdown: return "take the clock '" + a.text + "' away";
    case ActionType::PauseCountdown: return (a.value ? "pause" : "restart") + std::string(" the clock '") + a.text + "'";
    case ActionType::SetPlayArea: return a.area < 0 ? "open the whole map to " + sideName(sc, a.player) : "confine " + sideName(sc, a.player) + " to " + area(a.area);
    case ActionType::NameUnits: return "call " + sideName(sc, a.player) + "'s " + units(a.unitType) + " in " + area(a.area) + " '" + a.text + "'";
    case ActionType::SetInvulnerable: return who() + yes(" can't be hurt", " can be hurt again");
    case ActionType::SetAiControl: return who() + yes(" go back to the computer", " are kept from the computer");
    case ActionType::SetTargetable: return who() + yes(" can be targeted again", " are never targeted");
    case ActionType::SetLocked: return who() + yes(" are locked from orders", " take orders again");
    case ActionType::SetHealth: return who() + " have " + std::to_string(a.value) + "% health";
    case ActionType::DamageUnits: return who() + " lose " + std::to_string(a.value) + "% health";
    case ActionType::SetStance: return who() + (a.value == 0 ? " turn aggressive" : a.value == 1 ? " hold their ground" : " hold fire");
    case ActionType::Patrol: return who() + " patrol between " + area(a.area) + " and " + area(a.area2);
    case ActionType::Garrison: return who() + " go into the buildings in " + area(a.area2);
    case ActionType::Evacuate: return sideName(sc, a.player) + "'s men come out of buildings in " + area(a.area);
    case ActionType::BindUnits: return who() + " bind into a unit";
    case ActionType::Barrage: return std::to_string(a.value) + " shells (" + (a.text.empty() ? std::string("howitzer_105") : a.text) + ") on " + area(a.area);
    case ActionType::FreezeWater: return "the water in " + area(a.area) + " freezes";
    case ActionType::BreakIce: return "the ice in " + area(a.area) + " breaks up";
    case ActionType::FloodArea: return "the land in " + area(a.area) + " floods";
    case ActionType::DrainArea: return "the land in " + area(a.area) + " is drained";
    case ActionType::SetAlliance: return sideName(sc, a.player) + yes(" allies with ", " turns on ") + sideName(sc, a.player2);
    case ActionType::AiAttack: return "the computer playing " + sideName(sc, a.player) + " attacks " + area(a.area);
    case ActionType::AiDefend: return "the computer playing " + sideName(sc, a.player) + " defends " + area(a.area);
    case ActionType::AiFree: return "the computer playing " + sideName(sc, a.player) + " chooses its own targets";
    case ActionType::AiBehaviour: return "the computer playing " + sideName(sc, a.player) + yes(" may ", " may not ") + a.text;
    case ActionType::AiWaveSize: return "the computer playing " + sideName(sc, a.player) + " attacks at " + (a.value > 0 ? std::to_string(a.value) + "%" : std::string("its usual")) + " strength";
    case ActionType::SetCounter: return "counter '" + a.text + "' = " + std::to_string(a.value);
    case ActionType::AddCounter: return "counter '" + a.text + "' + " + std::to_string(a.value);
    case ActionType::AddFlag: return "campaign flag '" + a.text + "' + " + std::to_string(a.value);
    case ActionType::SpawnFromFlag: return "bring in as many " + units(a.unitType) + " as flag '" + a.text + "' says, at " + area(a.area);
    case ActionType::SpawnNamed: return "bring in " + std::to_string(a.value) + " " + units(a.unitType) + " called '" + a.text + "' at " + area(a.area);
    case ActionType::Transmission: return "radio to " + sideName(sc, a.player) + ": \"" + a.text + "\"";
    case ActionType::Cinematic: return yes("cinematic on (letterbox) for ", "cinematic off for ") + sideName(sc, a.player);
    case ActionType::CameraPan: return "glide " + sideName(sc, a.player) + "'s view to " + area(a.area) + " in " + std::to_string(a.value) + " s";
    case ActionType::MarkArea: return std::string(a.value == 1 ? "danger" : a.value == 2 ? "note" : "goal") + " marker '" + a.text + "' on " + area(a.area);
    case ActionType::ClearMarker: return a.text.empty() ? std::string("clear all markers") : "clear marker '" + a.text + "'";
    case ActionType::PlaySound: return "play sound '" + a.text + "'" + (a.area >= 0 ? " at " + area(a.area) : std::string());
    case ActionType::PlayMusic: return a.text.empty() ? std::string("music follows the fighting again") : "play music '" + a.text + "'";
    case ActionType::SetIncomeRate: return sideName(sc, a.player) + "'s income runs at " + std::to_string(a.value) + "%";
    case ActionType::AddLogisticsPoint: return "a new logistics point in " + area(a.area);
    case ActionType::FireTrigger: return "run trigger " + trig(a.trigger) + " now";
    case ActionType::ResetTrigger: return "let trigger " + trig(a.trigger) + " fire again";
    case ActionType::HideObjective: return "take objective " + std::to_string(a.value + 1) + " off the list";
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
    case 6: {  // set piece: invulnerable, released later
      t.name = "Set piece in " + (area >= 0 ? s.areas[static_cast<size_t>(area)].name : std::string("the area"));
      t.conditions.push_back(c);
      Action name;
      name.type = ActionType::NameUnits;
      name.player = enemy;
      name.area = area;
      name.text = "Set piece";
      t.actions.push_back(name);
      for (ActionType ty : {ActionType::SetInvulnerable, ActionType::SetAiControl}) {
        Action x;
        x.type = ty;
        x.text = "Set piece";
        x.value = ty == ActionType::SetInvulnerable ? 1 : 0;
        t.actions.push_back(x);
      }
      s.triggers.push_back(t);
      Trigger rel;
      rel.name = "Release the set piece";
      Condition after;
      after.type = CondType::Timer;
      after.value = 120;
      rel.conditions.push_back(after);
      for (ActionType ty : {ActionType::SetInvulnerable, ActionType::SetAiControl}) {
        Action x;
        x.type = ty;
        x.text = "Set piece";
        x.value = ty == ActionType::SetInvulnerable ? 0 : 1;
        rel.actions.push_back(x);
      }
      t = std::move(rel);
      break;
    }
    case 7: {  // hold with a clock
      t.name = "Hold for ten minutes";
      t.conditions.push_back(c);
      a.type = ActionType::Countdown;
      a.player = me;
      a.text = "Hold until relieved";
      a.value = 600;
      t.actions.push_back(a);
      Action o;
      o.type = ActionType::AddObjective;
      o.player = me;
      o.text = "Hold " + (area >= 0 ? s.areas[static_cast<size_t>(area)].name : std::string("the line")) + " for ten minutes";
      o.area = area;
      t.actions.push_back(o);
      s.triggers.push_back(t);
      Trigger held;
      held.name = "Held";
      Condition done;
      done.type = CondType::CountdownDone;
      done.text = "Hold until relieved";
      held.conditions.push_back(done);
      Action ok;
      ok.type = ActionType::CompleteObjective;
      ok.value = objectives;
      held.actions.push_back(ok);
      Action v;
      v.type = ActionType::Victory;
      v.player = me;
      held.actions.push_back(v);
      t = std::move(held);
      break;
    }
    case 8: {  // cinematic opening
      t.name = "Opening scene";
      t.conditions.push_back(c);
      Action on;
      on.type = ActionType::Cinematic;
      on.player = me;
      on.value = 1;
      t.actions.push_back(on);
      Action pan;
      pan.type = ActionType::CameraPan;
      pan.player = me;
      pan.area = area;
      pan.value = 6;
      t.actions.push_back(pan);
      Action r1;
      r1.type = ActionType::Transmission;
      r1.player = me;
      r1.text = "Command: The enemy is massing beyond the river. You have until dawn.";
      t.actions.push_back(r1);
      s.triggers.push_back(t);
      Trigger end;
      end.name = "Opening scene ends";
      Condition after;
      after.type = CondType::Timer;
      after.value = 8;
      end.conditions.push_back(after);
      Action off;
      off.type = ActionType::Cinematic;
      off.player = me;
      off.value = 0;
      end.actions.push_back(off);
      t = std::move(end);
      break;
    }
    case 9: {  // the enemy computer attacks a place
      t.name = "Enemy attacks " + (area >= 0 ? s.areas[static_cast<size_t>(area)].name : std::string("the area"));
      c.type = CondType::Timer;
      c.value = 240;
      t.conditions.push_back(c);
      a.type = ActionType::AiAttack;
      a.player = enemy;
      a.area = area;
      t.actions.push_back(a);
      Action warn;
      warn.type = ActionType::Transmission;
      warn.player = me;
      warn.text = "Observation post: Enemy armour moving towards " + (area >= 0 ? s.areas[static_cast<size_t>(area)].name : std::string("us")) + "!";
      t.actions.push_back(warn);
      break;
    }
    case 10: {  // the play area grows
      t.name = "The operation widens";
      c.type = CondType::TriggerFired;
      c.trigger = s.triggers.empty() ? -1 : 0;
      t.conditions.push_back(c);
      a.type = ActionType::SetPlayArea;
      a.player = ANY_PLAYER;
      a.area = area;
      t.actions.push_back(a);
      Action pan;
      pan.type = ActionType::CameraPan;
      pan.player = me;
      pan.area = area;
      pan.value = 3;
      t.actions.push_back(pan);
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
  ImGui::SameLine();
  changed |= ImGui::Checkbox("Any one", &t.any);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fire when any one of the conditions holds (OR), not only when all of them do (AND).");

  // One framed line per condition and action: the sentence, and its settings when opened.
  auto line = [&](bool when, int i, const std::string& text, bool editing) -> int {  // 1 edit, 2 remove
    int result = 0;
    ImGui::PushID((when ? 0 : 1000) + i);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, x0 = ImGui::GetCursorPosX();
    ImGui::Dummy(ImVec2(4, 0));
    ImGui::SameLine();
    ImGui::TextColored(when ? ImVec4(0.55f, 0.75f, 0.95f, 1) : ImVec4(0.95f, 0.75f, 0.4f, 1), "%s",
                       i == 0 ? (when ? "WHEN" : "THEN") : (when ? (t.any ? "  OR" : " AND") : " AND"));
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
    for (int i : condOrder())
      if (i < 0) {
        ImGui::SeparatorText(condGroup(-i - 1));
      } else if (ImGui::Selectable(condName(static_cast<CondType>(i)))) {
        Condition c;
        c.type = static_cast<CondType>(i);
        if (c.type == CondType::Timer) c.value = 60;
        if (c.type == CondType::UnitsInArea || c.type == CondType::NamedInArea || c.type == CondType::AreaControlled) c.value = 1;
        if (c.type == CondType::Chance || c.type == CondType::NamedHealthBelow || c.type == CondType::HealthBelow) c.value = 50;
        if (c.type == CondType::CountdownBelow) c.value = 60;
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
    for (int i : actionOrder())
      if (i < 0) {
        ImGui::SeparatorText(actionGroup(-i - 1));
      } else if (ImGui::Selectable(actionName(static_cast<ActionType>(i)))) {
        Action a;
        a.type = static_cast<ActionType>(i);
        if (!s.areas.empty()) a.area = selectedArea_ >= 0 ? selectedArea_ : 0;
        if (a.type == ActionType::SpawnUnits || a.type == ActionType::SpawnNamed) a.value = 1;
        if (a.type == ActionType::Countdown) a.value = 300;
        if (a.type == ActionType::Barrage) a.value = 20;
        if (a.type == ActionType::SetHealth || a.type == ActionType::SetIncomeRate || a.type == ActionType::AiWaveSize) a.value = 50;
        if (a.type == ActionType::CameraPan) a.value = 3;
        if (a.type == ActionType::SetInvulnerable || a.type == ActionType::SetLocked || a.type == ActionType::Cinematic) a.value = 1;
        if (a.type == ActionType::AiBehaviour) a.text = "attack";
        if (a.type == ActionType::Transmission) a.text = "Major Adler: Hold the bridge until the columns are across.";
        if (a.type == ActionType::AddObjective) a.area = -1;
        pickByName_ = false;
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
    const char* names[11] = {"Briefing and main objective", "Capture an area (uses the selected area)",
                             "Victory when the enemy is beaten", "Reinforcements after 2 minutes",
                             "Enemy counter-attack after 5 minutes", "Defeat when our force is gone",
                             "Set piece: unkillable until released", "Hold for ten minutes (a clock)",
                             "Cinematic opening (letterbox, radio)", "The enemy computer attacks the area",
                             "The operation widens (play area)"};
    for (int i = 0; i < 11; ++i) {
      if (i == 6) ImGui::SeparatorText("Missions with a story");
      if (ImGui::Button(names[i], ImVec2(-1, 0))) {
        addTemplate(i);
        changed = true;
      }
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
  if (ImGui::Button(scene_.missionDebugShown() ? "Hide the mission debugger" : "Mission debugger", ImVec2(-1, 0)))
    scene_.toggleMissionDebug();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("While the battle runs: every trigger and when it fired, the clocks, counters and flags,\n"
                      "what each computer player has been told, and the named units.");
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("Saved with the map. Name units and make set pieces in the Units tab's inspector.");
  ImGui::PopTextWrapPos();
}

}  // namespace legion

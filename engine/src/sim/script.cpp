// Slave Engine - scenario triggers: file format and runtime.
#include "slave/sim/script.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "slave/sim/simulation.hpp"

namespace slave {

namespace {
const char* kCondNames[] = {"Always", "Timer", "Units in area", "No units in area", "Unit count at most",
                            "Player defeated", "Supplies at least", "Trigger fired", "Unit count at least",
                            "Men in cover", "Order given", "Campaign flag at least"};
const char* kCondIds[] = {"always", "timer", "units_in_area", "no_units_in_area", "unit_count_at_most",
                          "player_defeated", "supplies", "trigger_fired", "unit_count_at_least", "in_cover",
                          "order_given", "flag_at_least"};
const char* kOrderIds[] = {"move", "attack_move", "attack", "stop", "spawn", "remove", "spawn_list", "edit", "kill",
                           "strike", "set_team", "set_stance", "produce", "place_building", "place_walls", "sell",
                           "deploy", "harvest", "set_rally", "set_credits", "set_controller", "repair", "bind_group",
                           "unbind_group", "occupy", "unload"};
const char* kActionNames[] = {"Message", "Add objective", "Complete objective", "Fail objective", "Victory", "Defeat",
                              "Spawn units", "Give supplies", "Reveal area", "Attack-move units", "Move units",
                              "Change owner", "Destroy units", "Enable trigger", "Disable trigger",
                              "Set computer player", "Move camera", "Show hint", "Highlight interface",
                              "Lock building", "Unlock building", "Set campaign flag"};
const char* kActionIds[] = {"message", "add_objective", "complete_objective", "fail_objective", "victory", "defeat",
                            "spawn_units", "give_supplies", "reveal_area", "attack_move", "move_units",
                            "change_owner", "kill_units", "enable_trigger", "disable_trigger", "set_computer",
                            "camera_to", "hint", "highlight", "lock_build", "unlock_build", "set_flag"};
static_assert(sizeof(kCondIds) / sizeof(kCondIds[0]) == static_cast<size_t>(CondType::Count));
static_assert(sizeof(kActionIds) / sizeof(kActionIds[0]) == static_cast<size_t>(ActionType::Count));

template <size_t N>
int indexOf(const char* (&ids)[N], const std::string& s) {
  for (size_t i = 0; i < N; ++i)
    if (s == ids[i]) return static_cast<int>(i);
  return -1;
}
}  // namespace

const char* orderId(int t) {
  return t >= 0 && t < static_cast<int>(sizeof(kOrderIds) / sizeof(kOrderIds[0])) ? kOrderIds[t] : "any";
}
int orderFromId(const std::string& id) { return indexOf(kOrderIds, id); }

const char* condName(CondType t) { return kCondNames[static_cast<int>(t) % static_cast<int>(CondType::Count)]; }
const char* actionName(ActionType t) { return kActionNames[static_cast<int>(t) % static_cast<int>(ActionType::Count)]; }

nlohmann::json scriptToJson(const ScenarioScript& s, const UnitTypeRegistry* types) {
  auto typeId = [&](int t) -> nlohmann::json {
    if (t < 0 || !types || t >= static_cast<int>(types->size())) return nullptr;
    return types->get(t).id;
  };
  nlohmann::json j;
  auto& areas = j["areas"] = nlohmann::json::array();
  for (const ScriptArea& a : s.areas) areas.push_back({{"name", a.name}, {"rect", {a.x0, a.y0, a.x1, a.y1}}});
  auto& triggers = j["triggers"] = nlohmann::json::array();
  for (const Trigger& t : s.triggers) {
    nlohmann::json tj{{"name", t.name}, {"enabled", t.enabled}, {"repeat", t.repeat}};
    auto& cs = tj["conditions"] = nlohmann::json::array();
    for (const Condition& c : t.conditions)
    {
      nlohmann::json cj{{"type", kCondIds[static_cast<int>(c.type)]}, {"player", c.player}, {"area", c.area},
                        {"value", c.value}, {"unit", typeId(c.unitType)}, {"trigger", c.trigger}};
      if (c.type == CondType::OrderGiven) cj["order"] = orderId(c.order);
      if (!c.text.empty()) cj["text"] = c.text;
      cs.push_back(std::move(cj));
    }
    auto& as = tj["actions"] = nlohmann::json::array();
    for (const Action& a : t.actions)
      as.push_back({{"type", kActionIds[static_cast<int>(a.type)]}, {"player", a.player}, {"player2", a.player2},
                    {"area", a.area}, {"area2", a.area2}, {"value", a.value}, {"unit", typeId(a.unitType)},
                    {"trigger", a.trigger}, {"text", a.text}});
    triggers.push_back(std::move(tj));
  }
  return j;
}

ScenarioScript scriptFromJson(const nlohmann::json& j, const UnitTypeRegistry* types) {
  ScenarioScript s;
  if (!j.is_object()) return s;
  auto unitOf = [&](const nlohmann::json& v) {
    if (!v.is_string() || !types) return -1;
    return types->find(v.get<std::string>());
  };
  try {
    for (const auto& a : j.value("areas", nlohmann::json::array())) {
      ScriptArea area;
      area.name = a.value("name", "Area");
      const auto& r = a.at("rect");
      area.x0 = r.at(0).get<int32_t>();
      area.y0 = r.at(1).get<int32_t>();
      area.x1 = r.at(2).get<int32_t>();
      area.y1 = r.at(3).get<int32_t>();
      if (area.x1 < area.x0) std::swap(area.x0, area.x1);
      if (area.y1 < area.y0) std::swap(area.y0, area.y1);
      s.areas.push_back(std::move(area));
    }
    for (const auto& tj : j.value("triggers", nlohmann::json::array())) {
      Trigger t;
      t.name = tj.value("name", "Trigger");
      t.enabled = tj.value("enabled", true);
      t.repeat = tj.value("repeat", false);
      for (const auto& cj : tj.value("conditions", nlohmann::json::array())) {
        const int type = indexOf(kCondIds, cj.value("type", std::string()));
        if (type < 0) continue;  // a condition from a newer game: skip it
        Condition c;
        c.type = static_cast<CondType>(type);
        c.player = cj.value("player", uint8_t{0});
        c.area = cj.value("area", -1);
        c.value = cj.value("value", 0);
        c.unitType = cj.contains("unit") ? unitOf(cj["unit"]) : -1;
        c.trigger = cj.value("trigger", -1);
        c.order = cj.contains("order") ? orderFromId(cj.value("order", std::string())) : -1;
        c.text = cj.value("text", std::string());
        t.conditions.push_back(c);
      }
      for (const auto& aj : tj.value("actions", nlohmann::json::array())) {
        const int type = indexOf(kActionIds, aj.value("type", std::string()));
        if (type < 0) continue;
        Action a;
        a.type = static_cast<ActionType>(type);
        a.player = aj.value("player", ANY_PLAYER);
        a.player2 = aj.value("player2", uint8_t{0});
        a.area = aj.value("area", -1);
        a.area2 = aj.value("area2", -1);
        a.value = aj.value("value", 0);
        a.unitType = aj.contains("unit") ? unitOf(aj["unit"]) : -1;
        a.trigger = aj.value("trigger", -1);
        a.text = aj.value("text", std::string());
        t.actions.push_back(std::move(a));
      }
      s.triggers.push_back(std::move(t));
    }
  } catch (const nlohmann::json::exception&) {
    // A damaged script: keep whatever was read.
  }
  return s;
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------
void Simulation::setScript(const ScenarioScript& s, bool keepProgress) {
  script_ = s;
  std::vector<TriggerState> old = std::move(triggerState_);
  triggerState_.assign(script_.triggers.size(), {});
  for (size_t i = 0; i < triggerState_.size(); ++i) {
    triggerState_[i].enabled = script_.triggers[i].enabled;
    if (keepProgress && i < old.size()) {
      triggerState_[i].fires = old[i].fires;
      triggerState_[i].lastFire = old[i].lastFire;
    }
  }
  if (!keepProgress) objectives_.clear();
}

std::vector<Entity> Simulation::unitsIn(uint8_t player, int area, int unitType) const {
  std::vector<Entity> out;
  const ScriptArea* a = (area >= 0 && area < static_cast<int>(script_.areas.size())) ? &script_.areas[static_cast<size_t>(area)] : nullptr;
  if (area >= 0 && !a) return out;
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  for (size_t i = 0; i < units.size(); ++i) {
    const uint32_t e = units.entities()[i];
    const CUnit& u = units.data()[i];
    if (player != ANY_PLAYER && u.owner != player) continue;
    if (unitType >= 0 && u.type != unitType) continue;
    if (unitType < 0 && types_.get(u.type).category == UnitCategory::Fortification) continue;
    if (a) {
      const CPosition& p = pos.get(e);
      if (!a->contains(p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT)) continue;
    }
    out.push_back(reg_.handle(e));
  }
  return out;
}

int Simulation::countUnits(uint8_t player, int area, int unitType) const {
  return static_cast<int>(unitsIn(player, area, unitType).size());
}

bool Simulation::evaluate(const Condition& c, size_t trigger) const {
  switch (c.type) {
    case CondType::Always: return true;
    case CondType::Timer: {
      const TriggerState& ts = triggerState_[trigger];
      const uint32_t since = (script_.triggers[trigger].repeat && ts.fires) ? ts.lastFire : 0;
      return tick_ >= since + static_cast<uint32_t>(std::max(0, c.value)) * TICKS_PER_SECOND;
    }
    case CondType::UnitsInArea: return countUnits(c.player, c.area, c.unitType) >= std::max(1, c.value);
    case CondType::NoUnitsInArea: return countUnits(c.player, c.area, c.unitType) == 0;
    case CondType::UnitCount: return countUnits(c.player, -1, c.unitType) <= c.value;
    case CondType::PlayerDefeated: return c.player < MAX_PLAYERS && players_[c.player].defeated;
    case CondType::Supplies: return c.player < MAX_PLAYERS && players_[c.player].credits >= c.value;
    case CondType::TriggerFired:
      if (!triggerFired(c.trigger)) return false;
      return c.value <= 0 ||
             tick_ >= triggerState_[static_cast<size_t>(c.trigger)].lastFire + static_cast<uint32_t>(c.value) * TICKS_PER_SECOND;
    case CondType::UnitsAtLeast: return countUnits(c.player, -1, c.unitType) >= std::max(1, c.value);
    case CondType::InCover: {
      int n = 0;
      for (uint32_t e : reg_.pool<CGarrisoned>().entities())
        if (reg_.pool<CUnit>().has(e) && reg_.pool<CUnit>().get(e).owner == c.player) ++n;
      return n >= std::max(1, c.value);
    }
    case CondType::OrderGiven: {
      if (c.player >= MAX_PLAYERS) return false;
      const auto& n = ordersGiven_[c.player];
      uint32_t count = 0;
      if (c.order < 0) {
        for (uint32_t k : n) count += k;
      } else if (c.order < static_cast<int>(n.size())) {
        count = n[static_cast<size_t>(c.order)];
      }
      return count >= static_cast<uint32_t>(std::max(1, c.value));
    }
    case CondType::FlagAtLeast: return flag(c.text) >= c.value;
    default: return false;
  }
}

void Simulation::perform(const Action& a) {
  auto areaCentre = [&](int area) -> Vec2i {
    if (area < 0 || area >= static_cast<int>(script_.areas.size())) return {-1, -1};
    const ScriptArea& r = script_.areas[static_cast<size_t>(area)];
    return {(r.x0 + r.x1 + 1) * 128, (r.y0 + r.y1 + 1) * 128};
  };
  auto event = [&](EventType t, uint8_t owner) -> SimEvent& {
    SimEvent ev;
    ev.type = t;
    ev.owner = owner;
    events_.push_back(ev);
    return events_.back();
  };
  switch (a.type) {
    case ActionType::Message:
      event(EventType::ScriptMessage, a.player).text = a.text;
      break;
    case ActionType::AddObjective:
      objectives_.push_back({a.text, a.player, 0, a.value == 1});
      event(EventType::ObjectivesChanged, a.player);
      break;
    case ActionType::CompleteObjective:
    case ActionType::FailObjective:
      if (a.value >= 0 && a.value < static_cast<int>(objectives_.size())) {
        objectives_[static_cast<size_t>(a.value)].state = a.type == ActionType::CompleteObjective ? 1 : 2;
        event(EventType::ObjectivesChanged, objectives_[static_cast<size_t>(a.value)].player);
      }
      break;
    case ActionType::Victory:
      for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        if (a.player != ANY_PLAYER && !allied(p, a.player)) continue;
        if (players_[p].won) continue;
        players_[p].won = true;
        event(EventType::PlayerWon, p);
      }
      break;
    case ActionType::Defeat:
      for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        if (a.player != ANY_PLAYER && p != a.player) continue;
        if (players_[p].defeated) continue;
        players_[p].defeated = true;
        scriptDefeated_[p] = true;
        event(EventType::PlayerDefeated, p);
      }
      break;
    case ActionType::SpawnUnits: {
      if (a.unitType < 0 || a.unitType >= static_cast<int>(types_.size()) || a.player >= MAX_PLAYERS) break;
      const Vec2i c = areaCentre(a.area);
      if (c.x < 0) break;
      Command cmd;
      cmd.type = CommandType::Spawn;
      cmd.unitType = static_cast<uint16_t>(a.unitType);
      cmd.player = a.player;
      cmd.target = c;
      cmd.count = static_cast<uint16_t>(std::clamp(a.value, 1, 2000));
      cmd.facing = angleOfFacing(4);
      applyCommand(cmd);
      break;
    }
    case ActionType::GiveSupplies:
      if (a.player < MAX_PLAYERS) players_[a.player].credits = std::max(0, players_[a.player].credits + a.value);
      break;
    case ActionType::RevealArea:
      if (a.area >= 0 && a.area < static_cast<int>(script_.areas.size()))
        revealed_.push_back({a.player, a.area});
      break;
    case ActionType::AttackMove:
    case ActionType::MoveUnits: {
      const Vec2i to = areaCentre(a.area2);
      if (to.x < 0) break;
      Command cmd;
      cmd.units = unitsIn(a.player, a.area, a.unitType);
      if (cmd.units.empty()) break;
      orderMove(cmd.units, to, a.type == ActionType::AttackMove ? OrderType::AttackMove : OrderType::Move, cmd);
      break;
    }
    case ActionType::ChangeOwner:
      if (a.player2 < MAX_PLAYERS)
        for (Entity e : unitsIn(a.player, a.area, a.unitType)) {
          UnitEdit ed;
          ed.unit = e;
          ed.owner = a.player2;
          applyEdit(ed);
        }
      break;
    case ActionType::KillUnits:
      for (Entity e : unitsIn(a.player, a.area, a.unitType)) damage(e.index, INT32_MAX / 2);
      break;
    case ActionType::EnableTrigger:
    case ActionType::DisableTrigger:
      if (a.trigger >= 0 && a.trigger < static_cast<int>(triggerState_.size()))
        triggerState_[static_cast<size_t>(a.trigger)].enabled = a.type == ActionType::EnableTrigger;
      break;
    case ActionType::SetComputer:
      if (a.player < MAX_PLAYERS)
        ai_.setPlayer(a.player, a.value >= 0, static_cast<AiDifficulty>(std::clamp(a.value, 0, 2)));
      break;
    case ActionType::CameraTo:
      event(EventType::CameraTo, a.player).at = areaCentre(a.area);
      break;
    case ActionType::Hint:
    case ActionType::Highlight:
      for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        if (a.player != ANY_PLAYER && p != a.player) continue;
        (a.type == ActionType::Hint ? hints_ : highlights_)[p] = a.text;
      }
      event(a.type == ActionType::Hint ? EventType::Hint : EventType::Highlight, a.player).text = a.text;
      break;
    case ActionType::LockBuild:
    case ActionType::UnlockBuild:
      for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        if (a.player != ANY_PLAYER && p != a.player) continue;
        auto& locks = buildLocks_[p];
        locks.resize(types_.size(), 0);
        const uint8_t v = a.type == ActionType::LockBuild ? 1 : 0;
        if (a.unitType < 0) std::fill(locks.begin(), locks.end(), v);
        else if (a.unitType < static_cast<int>(locks.size())) locks[static_cast<size_t>(a.unitType)] = v;
      }
      break;
    case ActionType::SetFlag:
      if (!a.text.empty()) flags_[a.text] = a.value;
      break;
    default:
      break;
  }
}

void Simulation::stepScript() {
  if (script_.triggers.empty() || tick_ % 5 != 0) return;  // three checks a second is plenty
  for (size_t i = 0; i < script_.triggers.size(); ++i) {
    const Trigger& t = script_.triggers[i];
    TriggerState& ts = triggerState_[i];
    if (!ts.enabled || (ts.fires && !t.repeat)) continue;
    // A repeating trigger waits at least a second between firings.
    if (ts.fires && tick_ < ts.lastFire + TICKS_PER_SECOND) continue;
    bool all = true;
    for (const Condition& c : t.conditions) all = all && evaluate(c, i);
    if (!all) continue;
    ++ts.fires;
    ts.lastFire = tick_;
    for (const Action& a : t.actions) perform(a);
  }
}

}  // namespace slave

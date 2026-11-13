// Slave Engine - scenario triggers: file format and runtime.
#include "slave/sim/script.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "slave/sim/simulation.hpp"

namespace slave {

namespace {
const char* kCondNames[] = {"Always", "Timer", "Units in area", "No units in area", "Unit count at most",
                            "Player defeated", "Supplies at least", "Trigger fired", "Unit count at least",
                            "Men in cover", "Order given", "Campaign flag at least",
                            "Named unit alive", "Named unit dead", "Named unit in area", "Named unit hurt",
                            "Structures at most", "Structures at least", "Units in area at most", "Area controlled",
                            "Countdown ran out", "Countdown below", "Counter at least", "Counter at most",
                            "Kills at least", "Losses at least", "Objective done", "Objective failed",
                            "Difficulty at least", "Chance", "Supplies at most", "Health below", "Allied with"};
const char* kCondIds[] = {"always", "timer", "units_in_area", "no_units_in_area", "unit_count_at_most",
                          "player_defeated", "supplies", "trigger_fired", "unit_count_at_least", "in_cover",
                          "order_given", "flag_at_least",
                          "named_alive", "named_dead", "named_in_area", "named_health_below",
                          "structures_at_most", "structures_at_least", "units_in_area_at_most", "area_controlled",
                          "countdown_done", "countdown_below", "counter_at_least", "counter_at_most",
                          "kills_at_least", "losses_at_least", "objective_done", "objective_failed",
                          "difficulty_at_least", "chance", "supplies_at_most", "health_below", "allied_with"};
const char* kOrderIds[] = {"move", "attack_move", "attack", "stop", "spawn", "remove", "spawn_list", "edit", "kill",
                           "strike", "set_team", "set_stance", "produce", "place_building", "place_walls", "sell",
                           "deploy", "harvest", "set_rally", "set_credits", "set_controller", "repair", "bind_group",
                           "unbind_group", "occupy", "unload", "tag_units"};
const char* kActionNames[] = {"Message", "Add objective", "Complete objective", "Fail objective", "Victory", "Defeat",
                              "Spawn units", "Give supplies", "Reveal area", "Attack-move units", "Move units",
                              "Change owner", "Destroy units", "Enable trigger", "Disable trigger",
                              "Set computer player", "Move camera", "Show hint", "Highlight interface",
                              "Lock building", "Unlock building", "Set campaign flag",
                              "Start countdown", "Stop countdown", "Pause countdown", "Set play area", "Name units",
                              "Invulnerable", "Computer control", "Targetable", "Locked from orders", "Set health",
                              "Damage units", "Set stance", "Patrol", "Garrison", "Evacuate", "Bind units",
                              "Artillery barrage", "Set alliance", "Computer: attack area", "Computer: defend area",
                              "Computer: free hand", "Computer: behaviour", "Computer: attack size", "Set counter",
                              "Add to counter", "Add to campaign flag", "Spawn from campaign flag", "Radio message",
                              "Cinematic", "Pan camera", "Mark area", "Clear marker", "Play sound", "Play music",
                              "Income rate", "Add logistics point", "Fire trigger", "Reset trigger", "Hide objective",
                              "Spawn named units", "Freeze water", "Break the ice"};
const char* kActionIds[] = {"message", "add_objective", "complete_objective", "fail_objective", "victory", "defeat",
                            "spawn_units", "give_supplies", "reveal_area", "attack_move", "move_units",
                            "change_owner", "kill_units", "enable_trigger", "disable_trigger", "set_computer",
                            "camera_to", "hint", "highlight", "lock_build", "unlock_build", "set_flag",
                            "countdown", "stop_countdown", "pause_countdown", "set_play_area", "name_units",
                            "set_invulnerable", "set_ai_control", "set_targetable", "set_locked", "set_health",
                            "damage_units", "set_stance", "patrol", "garrison", "evacuate", "bind_units",
                            "barrage", "set_alliance", "ai_attack", "ai_defend", "ai_free", "ai_behaviour",
                            "ai_wave_size", "set_counter", "add_counter", "add_flag", "spawn_from_flag",
                            "transmission", "cinematic", "camera_pan", "mark_area", "clear_marker", "play_sound",
                            "play_music", "set_income_rate", "add_logistics_point", "fire_trigger", "reset_trigger",
                            "hide_objective", "spawn_named", "freeze_water", "break_ice"};
static_assert(sizeof(kCondIds) / sizeof(kCondIds[0]) == static_cast<size_t>(CondType::Count));
static_assert(sizeof(kCondNames) / sizeof(kCondNames[0]) == static_cast<size_t>(CondType::Count));
static_assert(sizeof(kActionIds) / sizeof(kActionIds[0]) == static_cast<size_t>(ActionType::Count));
static_assert(sizeof(kActionNames) / sizeof(kActionNames[0]) == static_cast<size_t>(ActionType::Count));

template <size_t N>
int indexOf(const char* (&ids)[N], const std::string& s) {
  for (size_t i = 0; i < N; ++i)
    if (s == ids[i]) return static_cast<int>(i);
  return -1;
}
}  // namespace

const char* const kAiBehaviourIds[8] = {"build", "train", "attack", "raid", "capture", "fortify", "scout", "repair"};
int aiBehaviourFromId(const std::string& id) {
  for (int i = 0; i < 8; ++i)
    if (id == kAiBehaviourIds[i]) return i;
  return -1;
}

bool actionTakesUnits(ActionType t) {
  switch (t) {
    case ActionType::AttackMove: case ActionType::MoveUnits: case ActionType::ChangeOwner: case ActionType::KillUnits:
    case ActionType::SetInvulnerable: case ActionType::SetAiControl: case ActionType::SetTargetable:
    case ActionType::SetLocked: case ActionType::SetHealth: case ActionType::DamageUnits: case ActionType::SetStance:
    case ActionType::Patrol: case ActionType::Garrison: case ActionType::BindUnits:
      return true;
    default:
      return false;
  }
}
bool actionUsesText(ActionType t) {
  switch (t) {
    case ActionType::Message: case ActionType::AddObjective: case ActionType::Hint: case ActionType::Highlight:
    case ActionType::SetFlag: case ActionType::Countdown: case ActionType::StopCountdown:
    case ActionType::PauseCountdown: case ActionType::NameUnits: case ActionType::Barrage:
    case ActionType::AiBehaviour: case ActionType::SetCounter: case ActionType::AddCounter: case ActionType::AddFlag:
    case ActionType::SpawnFromFlag: case ActionType::Transmission: case ActionType::MarkArea:
    case ActionType::ClearMarker: case ActionType::PlaySound: case ActionType::PlayMusic: case ActionType::SpawnNamed:
      return true;
    default:
      return false;
  }
}

const char* orderId(int t) {
  return t >= 0 && t < static_cast<int>(sizeof(kOrderIds) / sizeof(kOrderIds[0])) ? kOrderIds[t] : "any";
}
int orderFromId(const std::string& id) { return indexOf(kOrderIds, id); }

const char* condName(CondType t) { return kCondNames[static_cast<int>(t) % static_cast<int>(CondType::Count)]; }
const char* actionName(ActionType t) { return kActionNames[static_cast<int>(t) % static_cast<int>(ActionType::Count)]; }
const char* condId(CondType t) { return kCondIds[static_cast<int>(t) % static_cast<int>(CondType::Count)]; }
const char* actionId(ActionType t) { return kActionIds[static_cast<int>(t) % static_cast<int>(ActionType::Count)]; }

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
    if (t.any) tj["any"] = true;
    auto& cs = tj["conditions"] = nlohmann::json::array();
    for (const Condition& c : t.conditions)
    {
      nlohmann::json cj{{"type", kCondIds[static_cast<int>(c.type)]}, {"player", c.player}, {"area", c.area},
                        {"value", c.value}, {"unit", typeId(c.unitType)}, {"trigger", c.trigger}};
      if (c.type == CondType::OrderGiven) cj["order"] = orderId(c.order);
      if (!c.text.empty()) cj["text"] = c.text;
      if (c.negate) cj["not"] = true;
      cs.push_back(std::move(cj));
    }
    auto& as = tj["actions"] = nlohmann::json::array();
    for (const Action& a : t.actions) {
      nlohmann::json aj{{"type", kActionIds[static_cast<int>(a.type)]}, {"player", a.player}, {"player2", a.player2},
                        {"area", a.area}, {"area2", a.area2}, {"value", a.value}, {"unit", typeId(a.unitType)},
                        {"trigger", a.trigger}, {"text", a.text}};
      if (a.scaled) aj["scaled"] = true;
      as.push_back(std::move(aj));
    }
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
      t.any = tj.value("any", false);
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
        c.negate = cj.value("not", false);
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
        a.scaled = aj.value("scaled", false);
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


std::vector<Entity> Simulation::structuresIn(uint8_t player, int area, int unitType) const {
  std::vector<Entity> out;
  const ScriptArea* a = (area >= 0 && area < static_cast<int>(script_.areas.size())) ? &script_.areas[static_cast<size_t>(area)] : nullptr;
  if (area >= 0 && !a) return out;
  auto& structs = reg_.pool<CStructure>();
  for (size_t i = 0; i < structs.size(); ++i) {
    const uint32_t e = structs.entities()[i];
    const CUnit* u = reg_.pool<CUnit>().tryGet(e);
    if (!u || u->health <= 0) continue;
    if (player != ANY_PLAYER && u->owner != player) continue;
    if (unitType >= 0 && u->type != unitType) continue;
    if (unitType < 0 && types_.get(u->type).category == UnitCategory::Fortification) continue;
    if (a) {
      const CPosition& p = reg_.pool<CPosition>().get(e);
      if (!a->contains(p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT)) continue;
    }
    out.push_back(reg_.handle(e));
  }
  return out;
}

std::vector<Entity> Simulation::actionUnits(const Action& a) const {
  if (!a.text.empty() && actionTakesUnits(a.type)) return named(a.text);
  return unitsIn(a.player, a.area, a.unitType);
}

bool Simulation::evaluate(const Condition& c, size_t trigger) const {
  const bool v = test(c, trigger);
  return c.negate ? !v : v;
}

bool Simulation::test(const Condition& c, size_t trigger) const {
  auto inArea = [&](int area, Entity e) {
    if (area < 0) return true;
    if (area >= static_cast<int>(script_.areas.size())) return false;
    const CPosition& p = reg_.get<CPosition>(e);
    return script_.areas[static_cast<size_t>(area)].contains(p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT);
  };
  auto countdown = [&](const std::string& label) -> const MissionCountdown* {
    for (const MissionCountdown& d : countdowns_)
      if (d.label == label) return &d;
    return nullptr;
  };
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
    case CondType::NamedAlive: return !named(c.text).empty();
    case CondType::NamedDead: return named(c.text).empty();
    case CondType::NamedInArea: {
      int n = 0;
      for (Entity e : named(c.text)) n += inArea(c.area, e);
      return n >= std::max(1, c.value);
    }
    case CondType::NamedHealthBelow:
      for (Entity e : named(c.text)) {
        const CUnit& u = reg_.get<CUnit>(e);
        if (static_cast<int64_t>(u.health) * 100 < static_cast<int64_t>(types_.get(u.type).maxHealth) * c.value) return true;
      }
      return false;
    case CondType::StructuresAtMost: return static_cast<int>(structuresIn(c.player, c.area, c.unitType).size()) <= c.value;
    case CondType::StructuresAtLeast:
      return static_cast<int>(structuresIn(c.player, c.area, c.unitType).size()) >= std::max(1, c.value);
    case CondType::UnitsInAreaAtMost: return countUnits(c.player, c.area, c.unitType) <= c.value;
    case CondType::AreaControlled: {
      int ours = 0;
      for (Entity e : unitsIn(ANY_PLAYER, c.area, -1)) {
        const uint8_t o = reg_.get<CUnit>(e).owner;
        if (o >= MAX_PLAYERS) continue;
        if (!allied(o, c.player)) return false;
        ++ours;
      }
      return ours >= std::max(1, c.value);
    }
    case CondType::CountdownDone: {
      const MissionCountdown* d = countdown(c.text);
      return d && d->done;
    }
    case CondType::CountdownBelow: {
      const MissionCountdown* d = countdown(c.text);
      return d && !d->done && countdownLeft(c.text) <= c.value;
    }
    case CondType::CounterAtLeast: return counter(c.text) >= c.value;
    case CondType::CounterAtMost: return counter(c.text) <= c.value;
    case CondType::KillsAtLeast: return kills(c.player) >= std::max(1, c.value);
    case CondType::LossesAtLeast: return losses(c.player) >= std::max(1, c.value);
    case CondType::ObjectiveDone:
    case CondType::ObjectiveFailed:
      return c.value >= 0 && c.value < static_cast<int>(objectives_.size()) &&
             objectives_[static_cast<size_t>(c.value)].state == (c.type == CondType::ObjectiveDone ? 1 : 2);
    case CondType::DifficultyAtLeast: return difficulty_ >= c.value;
    case CondType::Chance:
      return static_cast<int>(hash32(tick_ * 2654435761u ^ (static_cast<uint32_t>(trigger) * 40503u + 977u)) % 100) <
             c.value;
    case CondType::SuppliesAtMost: return c.player < MAX_PLAYERS && players_[c.player].credits <= c.value;
    case CondType::HealthBelow: {
      int64_t have = 0, full = 0;
      for (Entity e : unitsIn(c.player, c.area, c.unitType)) {
        const CUnit& u = reg_.get<CUnit>(e);
        have += std::max(0, u.health);
        full += types_.get(u.type).maxHealth;
      }
      return have * 100 < full * c.value || full == 0;
    }
    case CondType::AlliedWith: return c.value >= 0 && c.value < MAX_PLAYERS && allied(c.player, static_cast<uint8_t>(c.value));
    default: return false;
  }
}

void Simulation::perform(const Action& a, size_t depth) {
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
  auto forPlayers = [&](uint8_t player, auto&& fn) {
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
      if (player == ANY_PLAYER || p == player) fn(p);
  };
  // Units spawned in a block round the centre of `area` (as the Spawn command lays them out).
  auto spawnBlock = [&](int unitType, uint8_t player, int area, int count) {
    std::vector<Entity> out;
    if (unitType < 0 || unitType >= static_cast<int>(types_.size()) || player >= MAX_PLAYERS) return out;
    const Vec2i c = areaCentre(area);
    if (c.x < 0) return out;
    count = std::clamp(count, 1, 2000);
    const int32_t spacing = std::max(128, types_.get(unitType).radius * 2);
    int32_t side = 1;
    while (side * side < count) ++side;
    for (int32_t i = 0; i < count; ++i) {
      const int32_t gx = i % side, gy = i / side;
      Vec2i p{c.x + (gx * 2 - (side - 1)) * spacing / 2, c.y + (gy * 2 - (side - 1)) * spacing / 2};
      p.x = std::clamp(p.x, 64, map_.widthLeptons() - 64);
      p.y = std::clamp(p.y, 64, map_.heightLeptons() - 64);
      const Entity e = spawnUnit(static_cast<uint16_t>(unitType), player, p, angleOfFacing(4));
      if (e.valid()) out.push_back(e);
    }
    return out;
  };
  auto scaled = [&](int n) {
    return a.scaled ? std::max(1, static_cast<int>(static_cast<int64_t>(n) * difficultyScale(difficulty_) / 100)) : n;
  };
  auto byOwner = [&](const std::vector<Entity>& units, auto&& fn) {  // commands go out per owner
    std::map<uint8_t, std::vector<Entity>> split;
    for (Entity e : units) split[reg_.get<CUnit>(e).owner].push_back(e);
    for (auto& [owner, list] : split) fn(owner, list);
  };
  const bool wasScripting = scriptCommand_;
  scriptCommand_ = true;
  switch (a.type) {
    case ActionType::Message:
      event(EventType::ScriptMessage, a.player).text = a.text;
      break;
    case ActionType::AddObjective:
      objectives_.push_back({a.text, a.player, 0, a.value == 1, a.area});
      event(EventType::ObjectivesChanged, a.player);
      break;
    case ActionType::CompleteObjective:
    case ActionType::FailObjective:
    case ActionType::HideObjective:
      if (a.value >= 0 && a.value < static_cast<int>(objectives_.size())) {
        objectives_[static_cast<size_t>(a.value)].state =
            a.type == ActionType::CompleteObjective ? 1 : a.type == ActionType::FailObjective ? 2 : 3;
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
    case ActionType::SpawnUnits:
      spawnBlock(a.unitType, a.player, a.area, scaled(a.value));
      break;
    case ActionType::SpawnNamed: {
      const int32_t name = nameIndex(a.text);
      for (Entity e : spawnBlock(a.unitType, a.player, a.area, scaled(a.value))) setUnitName(e, name);
      break;
    }
    case ActionType::SpawnFromFlag: {
      const int n = std::min(flag(a.text), a.value > 0 ? a.value : 50);
      if (n > 0) spawnBlock(a.unitType, a.player, a.area, n);
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
      cmd.units = actionUnits(a);
      if (cmd.units.empty()) break;
      std::erase_if(patrols_, [&](Patrol& pt) {
        std::erase_if(pt.units, [&](Entity e) { return std::find(cmd.units.begin(), cmd.units.end(), e) != cmd.units.end(); });
        return pt.units.empty();
      });
      orderMove(cmd.units, to, a.type == ActionType::AttackMove ? OrderType::AttackMove : OrderType::Move, cmd);
      break;
    }
    case ActionType::ChangeOwner:
      if (a.player2 < MAX_PLAYERS)
        for (Entity e : actionUnits(a)) {
          UnitEdit ed;
          ed.unit = e;
          ed.owner = a.player2;
          applyEdit(ed);
        }
      break;
    case ActionType::KillUnits:
      scriptDamage_ = true;
      for (Entity e : actionUnits(a)) damage(e.index, INT32_MAX / 2);
      scriptDamage_ = false;
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
    case ActionType::AddFlag:
      if (!a.text.empty()) flags_[a.text] += a.value;
      break;
    case ActionType::SetCounter:
      if (!a.text.empty()) counters_[a.text] = a.value;
      break;
    case ActionType::AddCounter:
      if (!a.text.empty()) counters_[a.text] += a.value;
      break;
    // --- Countdowns ---------------------------------------------------------------
    case ActionType::Countdown: {
      if (a.text.empty()) break;
      MissionCountdown* d = nullptr;
      for (MissionCountdown& c : countdowns_)
        if (c.label == a.text) d = &c;
      if (!d) {
        countdowns_.push_back({});
        d = &countdowns_.back();
        d->label = a.text;
      }
      d->player = a.player;
      d->endTick = tick_ + static_cast<uint32_t>(std::max(0, a.value)) * TICKS_PER_SECOND;
      d->left = 0;
      d->paused = false;
      d->done = false;
      break;
    }
    case ActionType::StopCountdown:
      std::erase_if(countdowns_, [&](const MissionCountdown& c) { return c.label == a.text; });
      break;
    case ActionType::PauseCountdown:
      for (MissionCountdown& c : countdowns_) {
        if (c.label != a.text || c.done) continue;
        if (a.value && !c.paused) {
          c.left = c.endTick > tick_ ? c.endTick - tick_ : 0;
          c.paused = true;
        } else if (!a.value && c.paused) {
          c.endTick = tick_ + c.left;
          c.paused = false;
        }
      }
      break;
    // --- The battlefield --------------------------------------------------------------
    case ActionType::SetPlayArea: {
      TileRect r;
      if (a.area >= 0 && a.area < static_cast<int>(script_.areas.size())) {
        const ScriptArea& s = script_.areas[static_cast<size_t>(a.area)];
        r = {std::max(0, s.x0), std::max(0, s.y0), std::min(map_.width() - 1, s.x1), std::min(map_.height() - 1, s.y1)};
        if (r.empty()) break;
      }
      setPlayArea(a.player, r);
      event(EventType::PlayAreaChanged, a.player);
      break;
    }
    case ActionType::AddLogisticsPoint: {
      const Vec2i c = areaCentre(a.area);
      if (c.x < 0) break;
      map_.supplyPoints.push_back({c.x >> LEPTON_SHIFT, c.y >> LEPTON_SHIFT});
      points_.resize(map_.supplyPoints.size());
      break;
    }
    case ActionType::Barrage: {
      const Vec2i c = areaCentre(a.area);
      if (c.x < 0) break;
      int w = weapons_.find(a.text.empty() ? std::string("howitzer_105") : a.text);
      if (w < 0) w = weapons_.find("artillery_105");
      if (w < 0) break;
      const ScriptArea& s = script_.areas[static_cast<size_t>(a.area)];
      Command cmd;
      cmd.type = CommandType::Strike;
      cmd.player = a.player < MAX_PLAYERS ? a.player : NEUTRAL_OWNER;
      cmd.weapon = static_cast<uint16_t>(w);
      cmd.count = static_cast<uint16_t>(std::clamp(a.value, 1, 200));
      cmd.target = c;
      cmd.radius = std::max(s.x1 - s.x0 + 1, s.y1 - s.y0 + 1) * LEPTONS_PER_TILE / 2;
      applyCommandBody(cmd);
      break;
    }
    case ActionType::FreezeWater:
    case ActionType::BreakIce: {
      if (a.area < 0 || a.area >= static_cast<int>(script_.areas.size())) break;
      const ScriptArea& s = script_.areas[static_cast<size_t>(a.area)];
      const bool frozen = a.type == ActionType::FreezeWater;
      if (map_.setIce(s.x0, s.y0, s.x1, s.y1, frozen) && !frozen) drownIn(s.x0, s.y0, s.x1, s.y1);
      break;
    }
    case ActionType::SetAlliance: {
      if (a.player >= MAX_PLAYERS || a.player2 >= MAX_PLAYERS || a.player == a.player2) break;
      if (a.value) {
        team_[a.player] = team_[a.player2];
      } else if (allied(a.player, a.player2)) {
        for (uint8_t t = 0; t < 255; ++t)
          if (std::find(team_.begin(), team_.end(), t) == team_.end()) {
            team_[a.player] = t;
            break;
          }
      }
      break;
    }
    case ActionType::SetIncomeRate:
      forPlayers(a.player, [&](uint8_t p) { incomeRate_[p] = std::clamp(a.value, 0, 1000); });
      break;
    // --- Units --------------------------------------------------------------------------
    case ActionType::NameUnits: {
      const int32_t name = nameIndex(a.text);
      int n = 0;
      for (Entity e : unitsIn(a.player, a.area, a.unitType)) {
        if (a.value > 0 && n >= a.value) break;
        setUnitName(e, name);
        ++n;
      }
      break;
    }
    case ActionType::SetInvulnerable:
      for (Entity e : actionUnits(a)) setScriptFlags(e, kScriptInvulnerable, a.value ? kScriptInvulnerable : 0);
      break;
    case ActionType::SetAiControl:
      for (Entity e : actionUnits(a)) setScriptFlags(e, kScriptNoAi, a.value ? 0 : kScriptNoAi);
      break;
    case ActionType::SetTargetable:
      for (Entity e : actionUnits(a)) setScriptFlags(e, kScriptUntargetable, a.value ? 0 : kScriptUntargetable);
      break;
    case ActionType::SetLocked:
      for (Entity e : actionUnits(a)) setScriptFlags(e, kScriptLocked, a.value ? kScriptLocked : 0);
      break;
    case ActionType::SetHealth:
      for (Entity e : actionUnits(a)) {
        CUnit& u = reg_.get<CUnit>(e);
        if (u.health <= 0) continue;
        const int32_t maxH = types_.get(u.type).maxHealth;
        u.health = std::clamp(static_cast<int32_t>(static_cast<int64_t>(maxH) * a.value / 100), 1, maxH);
      }
      break;
    case ActionType::DamageUnits:
      scriptDamage_ = true;
      for (Entity e : actionUnits(a)) {
        const CUnit& u = reg_.get<CUnit>(e);
        damage(e.index, static_cast<int32_t>(std::min<int64_t>(INT32_MAX / 2, static_cast<int64_t>(types_.get(u.type).maxHealth) * std::max(0, a.value))));
      }
      scriptDamage_ = false;
      break;
    case ActionType::SetStance:
      for (Entity e : actionUnits(a)) reg_.get<CUnit>(e).stance = static_cast<Stance>(std::clamp(a.value, 0, static_cast<int>(Stance::Count) - 1));
      break;
    case ActionType::Patrol: {
      const Vec2i pa = areaCentre(a.area), pb = areaCentre(a.area2);
      if (pa.x < 0 || pb.x < 0) break;
      std::vector<Entity> units = actionUnits(a);
      if (units.empty()) break;
      std::erase_if(patrols_, [&](Patrol& pt) {
        std::erase_if(pt.units, [&](Entity e) { return std::find(units.begin(), units.end(), e) != units.end(); });
        return pt.units.empty();
      });
      patrols_.push_back({units, pa, pb, 0});
      Command cmd;
      orderMove(units, pb, OrderType::AttackMove, cmd);
      break;
    }
    case ActionType::Garrison: {
      std::vector<Entity> men;
      for (Entity e : actionUnits(a))
        if (types_.get(reg_.get<CUnit>(e).type).infantry() && !reg_.has<CGarrisoned>(e)) men.push_back(e);
      if (men.empty()) break;
      const uint8_t owner = reg_.get<CUnit>(men.front()).owner;
      size_t next = 0;
      // Every building, bunker or trench in the area with room, nearest first to the men.
      std::vector<Entity> places;
      auto& structs = reg_.pool<CStructure>();
      const ScriptArea* ar = (a.area2 >= 0 && a.area2 < static_cast<int>(script_.areas.size())) ? &script_.areas[static_cast<size_t>(a.area2)] : nullptr;
      if (!ar) break;
      for (size_t i = 0; i < structs.size(); ++i) {
        const uint32_t s = structs.entities()[i];
        const CUnit& su = reg_.pool<CUnit>().get(s);
        if (su.health <= 0 || types_.get(su.type).garrison <= 0) continue;
        if (su.owner < MAX_PLAYERS && su.owner != owner) continue;
        const CPosition& sp = reg_.pool<CPosition>().get(s);
        if (!ar->contains(sp.x >> LEPTON_SHIFT, sp.y >> LEPTON_SHIFT)) continue;
        places.push_back(reg_.handle(s));
      }
      for (Entity s : places) {
        if (next >= men.size()) break;
        const CGarrison* g = reg_.pool<CGarrison>().tryGet(s.index);
        const int room = types_.get(reg_.get<CUnit>(s).type).garrison - (g ? static_cast<int>(g->occupants.size()) : 0);
        if (room <= 0) continue;
        Command cmd;
        cmd.type = CommandType::Occupy;
        cmd.player = owner;
        cmd.targetUnit = s;
        for (int k = 0; k < room && next < men.size(); ++k) cmd.units.push_back(men[next++]);
        applyCommand(cmd);
      }
      break;
    }
    case ActionType::Evacuate: {
      std::vector<Entity> holds;
      for (Entity s : structuresIn(a.player, a.area, -1))
        if (reg_.pool<CGarrison>().has(s.index)) holds.push_back(s);
      if (holds.empty()) break;
      byOwner(holds, [&](uint8_t owner, const std::vector<Entity>& list) {
        Command cmd;
        cmd.type = CommandType::Unload;
        cmd.player = owner;
        cmd.units = list;
        applyCommand(cmd);
      });
      break;
    }
    case ActionType::BindUnits:
      byOwner(actionUnits(a), [&](uint8_t owner, const std::vector<Entity>& list) {
        Command cmd;
        cmd.type = CommandType::BindGroup;
        cmd.player = owner;
        cmd.units = list;
        applyCommand(cmd);
      });
      break;
    // --- The computer players -------------------------------------------------------------
    case ActionType::AiAttack:
    case ActionType::AiDefend:
      if (a.player < MAX_PLAYERS && areaCentre(a.area).x >= 0)
        ai_.setOrder(a.player, a.type == ActionType::AiAttack ? 1 : 2, areaCentre(a.area));
      break;
    case ActionType::AiFree:
      forPlayers(a.player, [&](uint8_t p) { ai_.setOrder(p, 0, {-1, -1}); });
      break;
    case ActionType::AiBehaviour: {
      const int bit = aiBehaviourFromId(a.text);
      if (bit >= 0) forPlayers(a.player, [&](uint8_t p) { ai_.setBehaviour(p, bit, a.value != 0); });
      break;
    }
    case ActionType::AiWaveSize:
      forPlayers(a.player, [&](uint8_t p) { ai_.setWaveScale(p, static_cast<int16_t>(std::clamp(a.value, 0, 1000))); });
      break;
    // --- Presentation ------------------------------------------------------------------------
    case ActionType::Transmission: {
      SimEvent& ev = event(EventType::Transmission, a.player);
      ev.text = a.text;
      ev.amount = a.value;
      break;
    }
    case ActionType::Cinematic:
      forPlayers(a.player, [&](uint8_t p) { cinematic_[p] = a.value != 0; });
      event(EventType::CinematicChanged, a.player).amount = a.value != 0;
      break;
    case ActionType::CameraPan: {
      SimEvent& ev = event(EventType::CameraPan, a.player);
      ev.at = areaCentre(a.area);
      ev.amount = std::max(0, a.value);
      break;
    }
    case ActionType::MarkArea:
      std::erase_if(markers_, [&](const MissionMarker& m) { return m.label == a.text && m.player == a.player; });
      if (a.area >= 0 && a.area < static_cast<int>(script_.areas.size()))
        markers_.push_back({a.text, a.player, a.area, static_cast<uint8_t>(std::clamp(a.value, 0, 2))});
      break;
    case ActionType::ClearMarker:
      std::erase_if(markers_, [&](const MissionMarker& m) { return a.text.empty() || m.label == a.text; });
      break;
    case ActionType::PlaySound: {
      SimEvent& ev = event(EventType::PlaySound, a.player);
      ev.text = a.text;
      ev.at = areaCentre(a.area);
      break;
    }
    case ActionType::PlayMusic:
      event(EventType::PlayMusic, a.player).text = a.text;
      break;
    // --- Triggers ---------------------------------------------------------------------------
    case ActionType::FireTrigger:
      if (a.trigger >= 0 && a.trigger < static_cast<int>(triggerState_.size()) && depth < 8) {
        TriggerState& ts = triggerState_[static_cast<size_t>(a.trigger)];
        ++ts.fires;
        ts.lastFire = tick_;
        const std::vector<Action> acts = script_.triggers[static_cast<size_t>(a.trigger)].actions;  // a copy: actions may edit state
        for (const Action& b : acts) perform(b, depth + 1);
      }
      break;
    case ActionType::ResetTrigger:
      if (a.trigger >= 0 && a.trigger < static_cast<int>(triggerState_.size())) {
        TriggerState& ts = triggerState_[static_cast<size_t>(a.trigger)];
        ts.fires = 0;
        ts.lastFire = tick_;
        ts.enabled = true;
      }
      break;
    default:
      break;
  }
  scriptCommand_ = wasScripting;
}

void Simulation::stepScript() {
  if (script_.triggers.empty() || tick_ % 5 != 0) return;  // three checks a second is plenty
  for (size_t i = 0; i < script_.triggers.size(); ++i) {
    const Trigger& t = script_.triggers[i];
    TriggerState& ts = triggerState_[i];
    if (!ts.enabled || (ts.fires && !t.repeat)) continue;
    // A repeating trigger waits at least a second between firings.
    if (ts.fires && tick_ < ts.lastFire + TICKS_PER_SECOND) continue;
    bool ok;
    if (t.any) {
      ok = false;
      for (const Condition& c : t.conditions) ok = ok || evaluate(c, i);
    } else {
      ok = true;
      for (const Condition& c : t.conditions) ok = ok && evaluate(c, i);
    }
    if (!ok) continue;
    ++ts.fires;
    ts.lastFire = tick_;
    const std::vector<Action> acts = t.actions;  // a copy: an action may change the script's state
    for (const Action& a : acts) perform(a);
  }
}

}  // namespace slave

// Pioneer Engine - unit abilities (Milestone 9.15). See sim/abilities.hpp.
#include "pioneer/sim/abilities.hpp"

#include <algorithm>

#include <nlohmann/json.hpp>

#include "pioneer/sim/simulation.hpp"
#include "pioneer/sim/weapons.hpp"

namespace pioneer {

namespace {
const char* const kKinds[] = {"throw", "barrage", "sprint", "dig_in", "charge", "rally"};
static_assert(sizeof(kKinds) / sizeof(kKinds[0]) == static_cast<size_t>(AbilityKind::Count));
int32_t ticks(double seconds) { return static_cast<int32_t>(seconds * TICKS_PER_SECOND + 0.5); }
int32_t leptons(double tiles) { return static_cast<int32_t>(tiles * LEPTONS_PER_TILE + 0.5); }
}  // namespace

const char* abilityKindId(AbilityKind k) { return k < AbilityKind::Count ? kKinds[static_cast<int>(k)] : "throw"; }
bool abilityKindFromId(const std::string& id, AbilityKind& out) {
  for (int i = 0; i < static_cast<int>(AbilityKind::Count); ++i)
    if (id == kKinds[i]) {
      out = static_cast<AbilityKind>(i);
      return true;
    }
  return false;
}
bool abilityNeedsTarget(AbilityKind k) { return k == AbilityKind::Throw || k == AbilityKind::Barrage || k == AbilityKind::Charge; }

bool AbilityRegistry::loadJson(const nlohmann::json& list, std::string& error) {
  defs_.clear();
  if (list.is_null()) return true;
  if (!list.is_array()) {
    error = "\"abilities\" should be a list";
    return false;
  }
  try {
    for (const nlohmann::json& j : list) {
      AbilityDef d;
      d.id = j.at("id").get<std::string>();
      const std::string where = "ability '" + d.id + "': ";
      d.name = j.value("name", d.id);
      d.description = j.value("description", std::string());
      d.icon = j.value("icon", d.name);
      const std::string kind = j.value("kind", std::string("throw"));
      if (!abilityKindFromId(kind, d.kind)) {
        error = where + "\"kind\" '" + kind + "' is not one of throw, barrage, sprint, dig_in, charge, rally";
        return false;
      }
      d.weaponId = j.value("weapon", std::string());
      if (j.contains("range")) d.range = std::clamp(leptons(j["range"].get<double>()), 64, 40 * LEPTONS_PER_TILE);
      if (j.contains("cooldown")) d.cooldown = std::clamp(ticks(j["cooldown"].get<double>()), 1, 3600 * TICKS_PER_SECOND);
      d.charges = std::clamp(j.value("charges", 0), 0, 99);
      d.cost = std::max(0, j.value("cost", 0));
      d.shots = std::clamp(j.value("shots", 1), 1, 50);
      if (j.contains("duration")) d.duration = std::clamp(ticks(j["duration"].get<double>()), 1, 600 * TICKS_PER_SECOND);
      d.percent = std::clamp(j.value("percent", d.percent), 0, 300);
      if (j.contains("radius")) d.radius = std::clamp(leptons(j["radius"].get<double>()), 64, 20 * LEPTONS_PER_TILE);
      for (const AbilityDef& have : defs_)
        if (have.id == d.id) {
          error = where + "given twice";
          return false;
        }
      defs_.push_back(std::move(d));
    }
  } catch (const std::exception& ex) {
    error = std::string("abilities: ") + ex.what();
    return false;
  }
  return true;
}

bool AbilityRegistry::resolve(const WeaponRegistry& weapons, std::string& error) {
  for (AbilityDef& d : defs_) {
    d.weapon = -1;
    if (!d.weaponId.empty()) {
      d.weapon = weapons.find(d.weaponId);
      if (d.weapon < 0) {
        error = "ability '" + d.id + "': weapon '" + d.weaponId + "' is not in weapons.json";
        return false;
      }
    }
    if ((d.kind == AbilityKind::Throw || d.kind == AbilityKind::Barrage) && d.weapon < 0) {
      error = "ability '" + d.id + "': a " + abilityKindId(d.kind) + " needs a \"weapon\"";
      return false;
    }
  }
  return true;
}

int AbilityRegistry::find(const std::string& id) const {
  for (size_t i = 0; i < defs_.size(); ++i)
    if (defs_[i].id == id) return static_cast<int>(i);
  return -1;
}

// ---------------------------------------------------------------------------
// The simulation's side

CAbilities& Simulation::abilitiesOf(Entity e) {
  if (CAbilities* a = reg_.tryGet<CAbilities>(e)) return *a;
  reg_.add<CAbilities>(e, CAbilities{});
  return reg_.get<CAbilities>(e);
}

int32_t Simulation::abilityCooldown(uint32_t unit, int slot) const {
  const CAbilities* a = reg_.pool<CAbilities>().tryGet(unit);
  return a && slot >= 0 && slot < kMaxAbilities ? a->cooldown[static_cast<size_t>(slot)] : 0;
}

bool Simulation::abilityReady(uint32_t unit, int slot, std::string* why) const {
  auto no = [&](const char* w) {
    if (why) *why = w;
    return false;
  };
  const CUnit* u = reg_.pool<CUnit>().tryGet(unit);
  if (!u || u->health <= 0) return no("gone");
  const UnitTypeDef& d = types_.get(u->type);
  if (slot < 0 || static_cast<size_t>(slot) >= d.abilities.size()) return no("it has no such ability");
  const AbilityDef& ab = types_.abilities().get(d.abilities[static_cast<size_t>(slot)]);
  const CAbilities* a = reg_.pool<CAbilities>().tryGet(unit);
  if (a && a->cooldown[static_cast<size_t>(slot)] > 0) return no("not ready yet");
  if (a && ab.charges > 0 && a->used[static_cast<size_t>(slot)] >= ab.charges) return no("used up");
  if (a && a->active) {
    const AbilityDef& now = types_.abilities().get(d.abilities[static_cast<size_t>(std::min<int>(a->active - 1, static_cast<int>(d.abilities.size()) - 1))]);
    if (now.kind != AbilityKind::DigIn) return no("busy");
  }
  if (u->owner < MAX_PLAYERS && players_[u->owner].credits < ab.cost) return no("not enough supplies");
  if (reg_.pool<CGarrisoned>().has(unit) && ab.kind != AbilityKind::Throw && ab.kind != AbilityKind::Rally)
    return no("not from inside");
  return true;
}

int32_t Simulation::abilitySpeedPercent(uint32_t unit) const {
  const CAbilities* a = reg_.pool<CAbilities>().tryGet(unit);
  if (!a || !a->active || tick_ >= a->activeUntil) return 100;
  const CUnit& u = reg_.pool<CUnit>().get(unit);
  const UnitTypeDef& d = types_.get(u.type);
  const AbilityDef& ab = types_.abilities().get(d.abilities[static_cast<size_t>(a->active - 1)]);
  return ab.kind == AbilityKind::Sprint || ab.kind == AbilityKind::Charge ? 100 + ab.percent : 100;
}

int32_t Simulation::abilityShelterPercent(uint32_t unit) const {
  const CAbilities* a = reg_.pool<CAbilities>().tryGet(unit);
  if (!a || !a->active) return 100;
  const CUnit& u = reg_.pool<CUnit>().get(unit);
  const UnitTypeDef& d = types_.get(u.type);
  const AbilityDef& ab = types_.abilities().get(d.abilities[static_cast<size_t>(a->active - 1)]);
  return ab.kind == AbilityKind::DigIn ? std::max(10, 100 - ab.percent) : 100;
}

bool Simulation::charging(uint32_t unit) const {
  const CAbilities* a = reg_.pool<CAbilities>().tryGet(unit);
  if (!a || !a->active || tick_ >= a->activeUntil) return false;
  const CUnit& u = reg_.pool<CUnit>().get(unit);
  const UnitTypeDef& d = types_.get(u.type);
  return types_.abilities().get(d.abilities[static_cast<size_t>(a->active - 1)]).kind == AbilityKind::Charge;
}

void Simulation::useAbility(const Command& cmd) {
  const int ability = cmd.unitType;
  if (ability < 0 || static_cast<size_t>(ability) >= types_.abilities().size()) return;
  const Vec2i at{std::clamp(cmd.target.x, 0, map_.widthLeptons() - 1), std::clamp(cmd.target.y, 0, map_.heightLeptons() - 1)};
  for (Entity e : cmd.units) {
    if (!reg_.alive(e) || !reg_.has<CUnit>(e)) continue;
    const CUnit& u = reg_.get<CUnit>(e);
    if (u.owner != cmd.player && cmd.player < MAX_PLAYERS) continue;
    const int slot = types_.abilitySlot(u.type, ability);
    if (slot < 0 || !abilityReady(e.index, slot)) continue;
    const AbilityDef& ab = types_.abilities().get(ability);
    if (ab.kind == AbilityKind::Throw || ab.kind == AbilityKind::Barrage) {
      // Out of reach: walk up first, then use it (stepAbilities).
      const CPosition& p = reg_.get<CPosition>(e);
      const uint32_t d = distance(at.x - p.x, at.y - p.y);
      if (d > static_cast<uint32_t>(ab.range)) {
        if (types_.get(u.type).speed <= 0 || reg_.has<CGarrisoned>(e)) continue;
        const int32_t back = ab.range - 96;
        const Vec2i stand{at.x + static_cast<int32_t>(static_cast<int64_t>(p.x - at.x) * back / std::max<uint32_t>(1, d)),
                          at.y + static_cast<int32_t>(static_cast<int64_t>(p.y - at.y) * back / std::max<uint32_t>(1, d))};
        setOrderIdle(e);
        COrder& o = reg_.get<COrder>(e);
        o.type = OrderType::Move;
        o.goal = stand;
        requestPath(e, stand);
        CAbilities& a = abilitiesOf(e);
        a.pending = static_cast<uint8_t>(slot + 1);
        a.pendingAt = at;
        continue;
      }
    }
    performAbility(e.index, slot, at);
  }
}

bool Simulation::performAbility(uint32_t unit, int slot, Vec2i at) {
  const Entity e = reg_.handle(unit);
  const CUnit& u = reg_.pool<CUnit>().get(unit);
  const UnitTypeDef& d = types_.get(u.type);
  const int ability = d.abilities[static_cast<size_t>(slot)];
  const AbilityDef& ab = types_.abilities().get(ability);
  const CPosition& p = reg_.pool<CPosition>().get(unit);
  CAbilities& a = abilitiesOf(e);
  a.pending = 0;
  a.cooldown[static_cast<size_t>(slot)] = static_cast<uint16_t>(std::min(ab.cooldown, 65535));
  a.used[static_cast<size_t>(slot)] = static_cast<uint8_t>(std::min(255, a.used[static_cast<size_t>(slot)] + 1));
  if (u.owner < MAX_PLAYERS) players_[u.owner].credits -= ab.cost;
  switch (ab.kind) {
    case AbilityKind::Throw:
      fireShot(unit, ab.weapon, {p.x, p.y}, at, kNullEntity);
      break;
    case AbilityKind::Barrage:
      a.active = static_cast<uint8_t>(slot + 1);
      a.activeUntil = tick_ + static_cast<uint32_t>(ab.duration);
      a.pendingAt = at;
      a.shotsLeft = static_cast<uint8_t>(std::min(ab.shots, 255));
      break;
    case AbilityKind::Sprint:
      a.active = static_cast<uint8_t>(slot + 1);
      a.activeUntil = tick_ + static_cast<uint32_t>(ab.duration);
      break;
    case AbilityKind::DigIn:
      a.active = static_cast<uint8_t>(slot + 1);
      a.activeUntil = UINT32_MAX;  // until it moves
      setOrderIdle(e);
      break;
    case AbilityKind::Charge: {
      a.active = static_cast<uint8_t>(slot + 1);
      a.activeUntil = tick_ + static_cast<uint32_t>(ab.duration);
      if (CCondition* c = reg_.pool<CCondition>().tryGet(unit)) {
        c->suppression = 0;
        c->posture = Posture::Standing;
        c->morale = std::max<uint16_t>(c->morale, 900);
      }
      Command go;
      go.type = CommandType::AttackMove;
      go.player = u.owner;
      go.units = {e};
      go.target = at;
      orderMove(go.units, at, OrderType::AttackMove, go);
      break;
    }
    case AbilityKind::Rally: {
      std::vector<uint32_t> near;
      auto& units = reg_.pool<CUnit>();
      spatial_.query(p.x, p.y, ab.radius, [&](uint32_t o) {
        if (!units.has(o)) return;  // (commands run before the hash is rebuilt: some may be gone)
        const CUnit& v = units.get(o);
        if (v.health > 0 && v.owner == u.owner && types_.get(v.type).infantry()) near.push_back(o);
      });
      for (uint32_t o : near) {
        CCondition* c = reg_.pool<CCondition>().tryGet(o);
        if (!c) continue;
        c->suppression = 0;
        c->posture = Posture::Standing;
        c->morale = std::max<uint16_t>(c->morale, 800);
        if (c->flags & CCondition::kRouting) {
          c->flags &= static_cast<uint8_t>(~CCondition::kRouting);
          setOrderIdle(reg_.handle(o));
        }
      }
      break;
    }
    default:
      break;
  }
  SimEvent ev;
  ev.type = EventType::AbilityUsed;
  ev.owner = u.owner;
  ev.unitType = static_cast<uint16_t>(ability);
  ev.from = {p.x, p.y};
  ev.at = abilityNeedsTarget(ab.kind) ? at : Vec2i{p.x, p.y};
  events_.push_back(ev);
  return true;
}

void Simulation::stepAbilities() {
  struct Use {
    uint32_t unit;
    int slot;
    Vec2i at;
  };
  std::vector<Use> uses;
  struct Round {
    uint32_t unit;
    int weapon;
    Vec2i from, at;
  };
  std::vector<Round> rounds;
  reg_.each<CAbilities, CUnit, CPosition>([&](uint32_t ei, CAbilities& a, CUnit& u, CPosition& p) {
    for (uint16_t& c : a.cooldown)
      if (c > 0) --c;
    if (u.health <= 0) return;
    const UnitTypeDef& d = types_.get(u.type);
    // Walking up to throw: in reach now? Given another order meanwhile: forget it.
    if (a.pending) {
      const int slot = a.pending - 1;
      const COrder* o = reg_.pool<COrder>().tryGet(ei);
      if (static_cast<size_t>(slot) >= d.abilities.size() || !o || (o->type != OrderType::Move && o->type != OrderType::Idle)) {
        a.pending = 0;
      } else {
        const AbilityDef& ab = types_.abilities().get(d.abilities[static_cast<size_t>(slot)]);
        if (distance(a.pendingAt.x - p.x, a.pendingAt.y - p.y) <= static_cast<uint32_t>(ab.range)) uses.push_back({ei, slot, a.pendingAt});
        else if (o->type == OrderType::Idle) a.pending = 0;  // stopped short (blocked): give up
      }
    }
    if (!a.active) return;
    const int slot = a.active - 1;
    if (static_cast<size_t>(slot) >= d.abilities.size()) {
      a.active = 0;
      return;
    }
    const AbilityDef& ab = types_.abilities().get(d.abilities[static_cast<size_t>(slot)]);
    if (ab.kind == AbilityKind::DigIn) {
      const CMover* m = reg_.pool<CMover>().tryGet(ei);
      if (m && (m->active || m->request)) a.active = 0;  // on the move: out of the hole
      return;
    }
    if (ab.kind == AbilityKind::Barrage && a.shotsLeft > 0) {
      const uint32_t start = a.activeUntil - static_cast<uint32_t>(ab.duration);
      const uint32_t every = std::max<uint32_t>(1, static_cast<uint32_t>(ab.duration / std::max(1, ab.shots)));
      if ((tick_ - start) % every == 0) {
        rounds.push_back({ei, ab.weapon, {p.x, p.y}, a.pendingAt});
        --a.shotsLeft;
      }
    }
    if (tick_ >= a.activeUntil) {
      a.active = 0;
      a.shotsLeft = 0;
    }
  });
  for (const Use& u : uses)
    if (reg_.alive(reg_.handle(u.unit)) && abilityReady(u.unit, u.slot)) performAbility(u.unit, u.slot, u.at);
  for (const Round& r : rounds) fireShot(r.unit, r.weapon, r.from, r.at, kNullEntity);
}

}  // namespace pioneer

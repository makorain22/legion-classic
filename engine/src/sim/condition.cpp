// Pioneer Engine - the soldier's condition under fire (Milestone 9.15):
// suppression, prone and pinned, morale and routing, the ambush volley,
// observers for the guns, minefields, and how far one sees at night and in
// bad weather.
//
// Infantry under fire gets its head down: suppression rises with every round
// that lands close (high explosive most), and falls away when the firing
// stops. From 400 the men go prone (slower, harder to hit, slower to fire);
// from 850 they are pinned (they won't move, and fire only now and then).
// Morale wears down under heavy suppression and when friends die close by;
// a unit that breaks runs for home until it steadies.
//
// A unit that has lain hidden for ten seconds (holding its fire, or in the
// woods) gets one ambush volley: three times the damage, six times against
// what its weapons are made for (an anti-tank team on a tank).
#include <algorithm>

#include "pioneer/sim/simulation.hpp"

namespace pioneer {

namespace {
constexpr uint16_t kProneAt = 400, kPinnedAt = 850, kMaxSuppression = 1000;
constexpr uint32_t kAmbushReadyTicks = 10 * TICKS_PER_SECOND;
constexpr uint32_t kVolleyTicks = 45;  // the ambush volley: hits within three seconds of the first shot
constexpr uint8_t kVolley = 32;        // CCondition flag: the ambush volley is under way
}  // namespace

void Simulation::suppress(uint32_t victim, int32_t amount) {
  if (amount <= 0) return;
  const CUnit* u = reg_.pool<CUnit>().tryGet(victim);
  if (!u || u->health <= 0 || !types_.get(u->type).infantry()) return;
  if (charging(victim)) return;  // deaf to it
  if (reg_.pool<CGarrisoned>().has(victim)) amount /= 2;  // walls and parapets
  CCondition& c = conditionOf(reg_.handle(victim));
  c.suppression = static_cast<uint16_t>(std::min<int32_t>(kMaxSuppression, c.suppression + amount));
  c.spare = (c.spare & 0xFFFF0000u) | (tick_ & 0xFFFFu);  // when last under fire (low half; the high half: hidden since)
}

int32_t Simulation::conditionSpeedPercent(uint32_t e) const {
  const CCondition* c = reg_.pool<CCondition>().tryGet(e);
  if (!c) return 100;
  if (c->flags & CCondition::kRouting) return 115;  // running for it
  return c->posture == Posture::Pinned ? 0 : c->posture == Posture::Prone ? 55 : 100;
}
int32_t Simulation::conditionFirePercent(uint32_t e) const {
  const CCondition* c = reg_.pool<CCondition>().tryGet(e);
  if (!c) return 100;
  return c->posture == Posture::Pinned ? 400 : c->posture == Posture::Prone ? 150 : 100;
}
int32_t Simulation::conditionShelterPercent(uint32_t e) const {
  const CCondition* c = reg_.pool<CCondition>().tryGet(e);
  if (!c) return 100;
  return c->posture == Posture::Pinned ? 55 : c->posture == Posture::Prone ? 70 : 100;
}
bool Simulation::routing(uint32_t e) const {
  const CCondition* c = reg_.pool<CCondition>().tryGet(e);
  return c && (c->flags & CCondition::kRouting);
}

// Three or six times the damage while a unit's ambush volley lasts.
int32_t Simulation::ambushPercent(Entity shooter, int weapon, ArmorClass armor) const {
  if (!shooter.valid() || !reg_.alive(shooter)) return 100;
  const CCondition* c = reg_.pool<CCondition>().tryGet(shooter.index);
  if (!c || !(c->flags & kVolley) || tick_ - c->revealedTick > kVolleyTicks) return 100;
  const WeaponDef& w = weapons_.get(weapon);
  return weapons_.warhead(w.warhead).versus[static_cast<size_t>(armor)] >= 75 ? 600 : 300;
}

// The first shot from an ambush opens the volley.
void Simulation::openFire(uint32_t shooter, const WeaponDef& wd) {
  CCondition* c = reg_.pool<CCondition>().tryGet(shooter);
  if (c && (c->flags & CCondition::kAmbush)) {
    c->flags = static_cast<uint8_t>((c->flags & ~CCondition::kAmbush) | kVolley);
    c->revealedTick = tick_;
    c->spare &= 0xFFFFu;
    SimEvent ev;
    ev.type = EventType::Ambush;
    ev.owner = reg_.pool<CUnit>().get(shooter).owner;
    ev.at = {reg_.pool<CPosition>().get(shooter).x, reg_.pool<CPosition>().get(shooter).y};
    events_.push_back(ev);
  }
  // Firing gives the shooter away: a gun firing indirectly to every enemy, for a few seconds (counter-battery fire
  // answers it); a man hidden in the woods to those who can see the spot.
  if (wd.indirect() || concealed(shooter)) {
    CCondition& cond = conditionOf(reg_.handle(shooter));
    cond.revealedTick = tick_;
    if (wd.indirect()) cond.flags |= CCondition::kGunFlash;
    else cond.flags &= static_cast<uint8_t>(~CCondition::kGunFlash);
  }
}

// Suppression from a round landing at `at`: men within twice its burst (or a
// near miss's reach) get their heads down, the closer the more.
void Simulation::suppressAround(int weapon, uint8_t owner, Vec2i at) {
  const WeaponDef& wd = weapons_.get(weapon);
  const bool he = weapons_.warhead(wd.warhead).id == "he";
  const int32_t reach = std::max(wd.splash * 2, 192);
  const int32_t base = std::clamp(wd.damage * (he ? 3 : 4), 25, 450);
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  std::vector<std::pair<uint32_t, int32_t>> hit;
  spatial_.query(at.x, at.y, reach, [&](uint32_t o) {
    if (!units.has(o)) return;
    const CUnit& v = units.get(o);
    if (v.health <= 0 || v.owner >= MAX_PLAYERS || allied(v.owner, owner) || !types_.get(v.type).infantry()) return;
    const CPosition& q = pos.get(o);
    const uint32_t d = distance(q.x - at.x, q.y - at.y);
    if (d > static_cast<uint32_t>(reach)) return;
    hit.push_back({o, base * (reach * 2 - static_cast<int32_t>(d)) / (reach * 2)});
  });
  for (const auto& [o, amount] : hit) suppress(o, amount);
}

// Friends dying close by shake the men round them.
void Simulation::shaken(Vec2i at, uint8_t owner) {
  auto& units = reg_.pool<CUnit>();
  std::vector<uint32_t> near;
  spatial_.query(at.x, at.y, 4 * LEPTONS_PER_TILE, [&](uint32_t o) {
    if (!units.has(o)) return;
    const CUnit& v = units.get(o);
    if (v.health > 0 && v.owner == owner && types_.get(v.type).infantry()) near.push_back(o);
  });
  for (uint32_t o : near) {
    CCondition& c = conditionOf(reg_.handle(o));
    c.morale = static_cast<uint16_t>(std::max(0, c.morale - 90));
  }
}

// Is an observer of `owner`'s side watching `at`?
bool Simulation::spotted(uint8_t owner, Vec2i at) const {
  bool seen = false;
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  spatial_.query(at.x, at.y, 16 * LEPTONS_PER_TILE, [&](uint32_t o) {
    if (seen) return;
    if (!units.has(o)) return;
    const CUnit& v = units.get(o);
    const UnitTypeDef& d = types_.get(v.type);
    if (!d.spotter || v.health <= 0 || !allied(v.owner, owner)) return;
    const CPosition& q = pos.get(o);
    seen = distance(q.x - at.x, q.y - at.y) <= static_cast<uint32_t>(d.sight);
  });
  return seen;
}

int32_t Simulation::weatherSightPercent() const {
  // The hour, in whole seconds after midnight.
  const int64_t since = tick_ > env_.setTick ? static_cast<int64_t>(tick_ - env_.setTick) / TICKS_PER_SECOND : 0;
  int64_t s = (static_cast<int64_t>(env_.daySeconds) + since * env_.rate) % (24 * 3600);
  if (s < 0) s += 24 * 3600;
  const int64_t m = s / 60;  // minutes
  int32_t light = 100;
  if (m < 4 * 60 + 30 || m >= 21 * 60 + 30) light = 55;                         // night
  else if (m < 6 * 60 + 30 || m >= 19 * 60 + 30) light = 75;                   // dawn and dusk
  static const int32_t kWeather[] = {100, 100, 95, 80, 65, 80, 55, 50};       // clear .. mist (core/weather.hpp)
  const bool turned = env_.changeTicks == 0 || tick_ - env_.changeTick >= env_.changeTicks / 2;
  const uint8_t w = turned ? env_.weather : env_.previous;
  const int32_t sky = kWeather[std::min<size_t>(w, sizeof(kWeather) / sizeof(kWeather[0]) - 1)];
  return std::max<int32_t>(30, light * sky / 100);
}

void Simulation::stepCondition() {
  // Suppression wears off, faster once the firing has stopped; the posture
  // follows it; morale wears down under heavy fire and comes back in quiet.
  std::vector<Entity> broke, steadied;
  reg_.each<CCondition>([&](uint32_t ei, CCondition& c) {
    const uint16_t lastFire = static_cast<uint16_t>(c.spare & 0xFFFFu);
    const bool quiet = static_cast<uint16_t>(static_cast<uint16_t>(tick_) - lastFire) > 30;
    if (c.suppression > 0) c.suppression = static_cast<uint16_t>(std::max(0, c.suppression - (quiet ? 6 : 1)));
    const Posture before = c.posture;
    if (c.posture == Posture::Standing || c.posture == Posture::Prone || c.posture == Posture::Pinned) {
      if (c.suppression >= kPinnedAt) c.posture = Posture::Pinned;
      else if (c.suppression >= kProneAt) c.posture = Posture::Prone;
      else if (c.suppression < kProneAt - 150) c.posture = Posture::Standing;  // a little slack, so they don't bob
      else if (c.posture == Posture::Pinned) c.posture = Posture::Prone;
    }
    (void)before;
    if ((tick_ + ei) % TICKS_PER_SECOND == 0) {
      if (c.suppression > 600) c.morale = static_cast<uint16_t>(std::max(0, c.morale - (c.suppression - 600) / 8));
      else if (c.suppression < 200) c.morale = static_cast<uint16_t>(std::min(1000, c.morale + 25));
      if (!(c.flags & CCondition::kRouting) && c.morale < 150 && !reg_.pool<CGarrisoned>().has(ei)) {
        c.flags |= CCondition::kRouting;
        c.suppression = 0;
        c.posture = Posture::Standing;
        broke.push_back(reg_.handle(ei));
      } else if ((c.flags & CCondition::kRouting) && c.morale >= 500) {
        c.flags &= static_cast<uint8_t>(~CCondition::kRouting);
        steadied.push_back(reg_.handle(ei));
      }
    }
    if ((c.flags & kVolley) && tick_ - c.revealedTick > kVolleyTicks) c.flags &= static_cast<uint8_t>(~kVolley);
  });
  if (!broke.empty()) {
    retreat(broke);
    for (Entity e : broke) {
      SimEvent ev;
      ev.type = EventType::UnitBroke;
      ev.owner = reg_.get<CUnit>(e).owner;
      ev.unitType = reg_.get<CUnit>(e).type;
      ev.at = {reg_.get<CPosition>(e).x, reg_.get<CPosition>(e).y};
      events_.push_back(ev);
    }
  }
  for (Entity e : steadied) {
    // Steady again where they stopped; morale comes back the rest of the way in quiet.
    if (reg_.has<COrder>(e) && reg_.get<COrder>(e).forced) setOrderIdle(e);
  }

  // Ambushes, once a second: units holding their fire or hidden in the woods,
  // unseen by any enemy for ten seconds, are ready.
  if (tick_ % TICKS_PER_SECOND == 3) {
    std::vector<std::pair<Entity, bool>> lying;
    reg_.each<CCombat, CUnit>([&](uint32_t ei, CCombat&, CUnit& u) {
      if (u.health <= 0 || u.owner >= MAX_PLAYERS) return;
      const UnitTypeDef& d = types_.get(u.type);
      if (d.weapons.empty() || d.isStructure()) return;
      const bool waiting = u.stance == Stance::HoldFire || concealed(ei);
      const CCondition* c = reg_.pool<CCondition>().tryGet(ei);
      if (!waiting) {
        if (c && (c->flags & CCondition::kAmbush)) lying.push_back({reg_.handle(ei), false});
        return;
      }
      bool seen = false;
      for (uint8_t q = 0; q < MAX_PLAYERS && !seen; ++q)
        if (!allied(q, u.owner) && unitVisibleTo(q, ei)) seen = true;
      if (seen && (!c || (!(c->flags & CCondition::kAmbush) && (c->spare >> 16) == 0))) return;
      lying.push_back({reg_.handle(ei), !seen});
    });
    for (const auto& [e, hidden] : lying) {
      CCondition& c = conditionOf(e);
      if (!hidden) {
        c.flags &= static_cast<uint8_t>(~CCondition::kAmbush);
        c.spare &= 0xFFFFu;
        continue;
      }
      // The high half of `spare`: the second it went to ground (+1, so 0 means "not hiding").
      const uint32_t since = c.spare >> 16;
      const uint32_t nowSec = (tick_ / TICKS_PER_SECOND + 1) & 0xFFFFu;
      if (since == 0) c.spare = (c.spare & 0xFFFFu) | (nowSec << 16);
      else if (((nowSec - since) & 0xFFFFu) * TICKS_PER_SECOND >= kAmbushReadyTicks) c.flags |= CCondition::kAmbush;
    }
  }
}

// Minefields, every five ticks: a charge goes off under the first enemy of the
// kind it is laid for; an enemy engineer right beside it lifts it instead.
void Simulation::stepMines() {
  if (tick_ % 5 != 1) return;
  bool any = false;
  for (const UnitTypeDef& d : types_.all()) any = any || d.isMine();
  if (!any) return;
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  struct Blast {
    uint32_t mine;
    int weapon;
    uint8_t owner;
    Vec2i at;
    Entity victim;
  };
  std::vector<Blast> blasts;
  std::vector<std::pair<uint32_t, uint8_t>> lifted;
  for (uint32_t s : reg_.pool<CStructure>().entities()) {
    const CUnit& mu = units.get(s);
    const UnitTypeDef& md = types_.get(mu.type);
    if (!md.isMine() || mu.health <= 0) continue;
    const CPosition& mp = pos.get(s);
    uint32_t victim = UINT32_MAX, engineer = UINT32_MAX;
    spatial_.query(mp.x, mp.y, 384, [&](uint32_t o) {
      if (o == s) return;
      if (!units.has(o)) return;
    const CUnit& v = units.get(o);
      const UnitTypeDef& vd = types_.get(v.type);
      if (v.health <= 0 || vd.isStructure() || v.owner >= MAX_PLAYERS || allied(v.owner, mu.owner)) return;
      const CPosition& q = pos.get(o);
      const uint32_t d = distance(q.x - mp.x, q.y - mp.y);
      if (vd.repair > 0 && vd.infantry() && d <= 384) engineer = o;
      const bool kind = md.mineAgainst == 2 || (md.mineAgainst == 0) == vd.infantry();
      if (kind && d <= static_cast<uint32_t>(150 + (vd.infantry() ? 0 : vd.radius / 2))) victim = o;
    });
    if (engineer != UINT32_MAX && victim == UINT32_MAX) lifted.push_back({s, units.get(engineer).owner});
    else if (victim != UINT32_MAX) blasts.push_back({s, md.mineWeapon, mu.owner, {mp.x, mp.y}, reg_.handle(victim)});
  }
  for (const Blast& b : blasts) {
    impact(b.weapon, b.owner, b.at, b.victim, b.at, kNullEntity);
    dead_.push_back(b.mine);
  }
  for (const auto& [m, who] : lifted) {
    SimEvent ev;
    ev.type = EventType::MineCleared;
    ev.owner = who;
    ev.unitType = units.get(m).type;
    ev.at = {pos.get(m).x, pos.get(m).y};
    events_.push_back(ev);
    dead_.push_back(m);
  }
}

}  // namespace pioneer

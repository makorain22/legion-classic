// Slave Engine - targeting, turrets, firing, projectiles and damage.
#include <algorithm>

#include "slave/sim/simulation.hpp"

namespace slave {

namespace {
// Turns `a` one facing step toward `desired` when its wait runs out.
// Returns true once `a` points the same way (to the nearest 1/16 turn).
bool turnToward(Angle& a, Angle desired, uint8_t& wait, int32_t turnTicks) {
  if (facingOf(a) == facingOf(desired)) return true;
  if (turnTicks <= 0) {
    a = angleOfFacing(facingOf(desired));
    return true;
  }
  if (wait > 0) {
    --wait;
    return false;
  }
  a = angleOfFacing(facingOf(a) + (angleDelta(a, desired) > 0 ? 1 : -1));
  wait = static_cast<uint8_t>(turnTicks - 1);
  return facingOf(a) == facingOf(desired);
}
}  // namespace

void Simulation::stepTargeting() {
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  reg_.each<CCombat, CUnit, CPosition, COrder>([&](uint32_t ei, CCombat& c, CUnit& u, CPosition& p, COrder& o) {
    if (u.health <= 0) return;
    const bool targetOk = reg_.alive(c.target) && units.has(c.target.index) && units.get(c.target.index).health > 0;
    if (!targetOk) c.target = kNullEntity;
    if (o.type == OrderType::Attack) {
      c.target = (reg_.alive(o.target) && units.has(o.target.index)) ? o.target : kNullEntity;
      return;
    }
    if ((tick_ + ei) % 4 != 0) return;  // scan for targets 4 times a second, staggered

    const UnitTypeDef& def = types_.get(u.type);
    if (o.type == OrderType::Move && !def.turret) {  // marching infantry don't stop to shoot
      c.target = kNullEntity;
      return;
    }
    const int32_t range = maxRange(def);
    const int32_t scan = o.type == OrderType::AttackMove ? std::max(range, def.sight) : range;

    if (c.target.valid()) {  // keep shooting the same target while it stays reachable
      const CPosition& tp = pos.get(c.target.index);
      if (distance(tp.x - p.x, tp.y - p.y) <= static_cast<uint32_t>(scan + 256) && canSee(u.owner, tp.x, tp.y)) return;
      c.target = kNullEntity;
    }
    uint32_t best = UINT32_MAX;
    uint32_t bestIdx = UINT32_MAX;
    spatial_.query(p.x, p.y, scan, [&](uint32_t o2) {
      const CUnit& v = units.get(o2);
      if (v.owner == u.owner || v.health <= 0) return;
      const CPosition& q = pos.get(o2);
      const uint32_t d = distance(q.x - p.x, q.y - p.y);
      if (d > static_cast<uint32_t>(scan) || d >= best) return;
      if (!canSee(u.owner, q.x, q.y)) return;
      const ArmorClass armor = types_.get(v.type).armor;
      bool useful = false;
      for (int w : def.weapons) useful = useful || weapons_.damageAgainst(w, armor) > 0;
      if (!useful) return;
      best = d;
      bestIdx = o2;
    });
    if (bestIdx != UINT32_MAX) c.target = reg_.handle(bestIdx);
  });
}

void Simulation::stepWeapons() {
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  auto& turrets = reg_.pool<CTurret>();
  struct Shot {
    uint32_t shooter;
    int weapon;
    Vec2i from, at;
    Entity target;
  };
  std::vector<Shot> shots;

  reg_.each<CCombat, CUnit, CPosition, CFacing, CMover>(
      [&](uint32_t ei, CCombat& c, CUnit& u, CPosition& p, CFacing& f, CMover& m) {
        if (u.health <= 0) return;
        for (uint16_t& cd : c.cooldown)
          if (cd > 0) --cd;
        const UnitTypeDef& def = types_.get(u.type);
        CTurret* t = turrets.tryGet(ei);

        if (!c.target.valid() || !reg_.alive(c.target) || !units.has(c.target.index)) {
          if (t) turnToward(t->angle, f.angle, t->wait, def.turretTurnTicks);  // turret back to the front
          return;
        }
        const CPosition& tp = pos.get(c.target.index);
        const Angle desired = atan2Angle(tp.y - p.y, tp.x - p.x);
        bool aligned;
        if (t) {
          aligned = turnToward(t->angle, desired, t->wait, def.turretTurnTicks);
        } else {
          if (m.active) return;  // no turret: must stop to fire
          aligned = turnToward(f.angle, desired, f.turnWait, def.turnTicks);
        }
        if (!aligned || !canSee(u.owner, tp.x, tp.y)) return;

        const uint32_t d = distance(tp.x - p.x, tp.y - p.y);
        const ArmorClass armor = types_.get(units.get(c.target.index).type).armor;
        for (size_t slot = 0; slot < def.weapons.size(); ++slot) {
          const int w = def.weapons[slot];
          const WeaponDef& wd = weapons_.get(w);
          if (c.cooldown[slot] > 0 || d > static_cast<uint32_t>(wd.range)) continue;
          if (weapons_.damageAgainst(w, armor) <= 0) continue;
          c.cooldown[slot] = static_cast<uint16_t>(wd.cooldown);
          shots.push_back({ei, w, {p.x, p.y}, {tp.x, tp.y}, c.target});
        }
      });

  // Resolve shots after the loop (creating projectiles adds components).
  for (const Shot& s : shots) {
    const WeaponDef& wd = weapons_.get(s.weapon);
    const uint8_t owner = units.get(s.shooter).owner;
    SimEvent fire;
    fire.type = EventType::Fire;
    fire.owner = owner;
    fire.weapon = static_cast<uint16_t>(s.weapon);
    fire.from = s.from;
    fire.at = s.at;
    events_.push_back(fire);
    if (wd.projectile == ProjectileKind::Instant) {
      if (static_cast<int32_t>(rng_.below(100)) < wd.accuracy) {
        impact(s.weapon, owner, s.at, s.target);
      } else {
        const Vec2i miss{s.at.x + rng_.range(-110, 110), s.at.y + rng_.range(-110, 110)};
        impact(s.weapon, owner, miss, kNullEntity);
      }
    } else {
      // Shells scatter more the farther they fly.
      const uint32_t d = distance(s.at.x - s.from.x, s.at.y - s.from.y);
      const int32_t spread = static_cast<int32_t>(d / 20);
      Vec2i aim = s.at;
      if (spread > 0) {
        aim.x += rng_.range(-spread, spread);
        aim.y += rng_.range(-spread, spread);
      }
      const Entity pe = reg_.create();
      CProjectile pr;
      pr.from = s.from;
      pr.to = aim;
      pr.x = s.from.x;
      pr.y = s.from.y;
      pr.total = static_cast<int32_t>(std::max<uint32_t>(1, distance(aim.x - s.from.x, aim.y - s.from.y)));
      pr.weapon = static_cast<uint16_t>(s.weapon);
      pr.owner = owner;
      reg_.add<CProjectile>(pe, pr);
    }
  }
}

void Simulation::stepProjectiles() {
  struct Hit {
    uint32_t projectile;
    int weapon;
    uint8_t owner;
    Vec2i at;
  };
  std::vector<Hit> hits;
  reg_.each<CProjectile>([&](uint32_t ei, CProjectile& pr) {
    pr.travelled += weapons_.get(pr.weapon).speed;
    if (pr.travelled >= pr.total) {
      pr.x = pr.to.x;
      pr.y = pr.to.y;
      hits.push_back({ei, pr.weapon, pr.owner, pr.to});
      return;
    }
    pr.x = pr.from.x + static_cast<int32_t>(static_cast<int64_t>(pr.to.x - pr.from.x) * pr.travelled / pr.total);
    pr.y = pr.from.y + static_cast<int32_t>(static_cast<int64_t>(pr.to.y - pr.from.y) * pr.travelled / pr.total);
  });
  for (const Hit& h : hits) {
    impact(h.weapon, h.owner, h.at, kNullEntity);
    dead_.push_back(h.projectile);
  }
}

void Simulation::impact(int weapon, uint8_t owner, Vec2i at, Entity directTarget) {
  const WeaponDef& wd = weapons_.get(weapon);
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  bool hit = false;
  if (wd.splash > 0) {
    // Full damage at the centre, half at the edge; own units take half.
    spatial_.query(at.x, at.y, wd.splash, [&](uint32_t o) {
      const CUnit& v = units.get(o);
      if (v.health <= 0) return;
      const CPosition& q = pos.get(o);
      const uint32_t d = distance(q.x - at.x, q.y - at.y);
      if (d > static_cast<uint32_t>(wd.splash)) return;
      int32_t dmg = weapons_.damageAgainst(weapon, types_.get(v.type).armor) *
                    (wd.splash * 2 - static_cast<int32_t>(d)) / (wd.splash * 2);
      if (v.owner == owner) dmg /= 2;
      damage(o, dmg);
      hit = true;
    });
  } else {
    uint32_t victim = UINT32_MAX;
    if (directTarget.valid() && reg_.alive(directTarget) && units.has(directTarget.index)) {
      victim = directTarget.index;
    } else {
      uint32_t best = 72;  // a stray round hits whoever is right there
      spatial_.query(at.x, at.y, 72, [&](uint32_t o) {
        const CPosition& q = pos.get(o);
        const uint32_t d = distance(q.x - at.x, q.y - at.y);
        if (d < best && units.get(o).health > 0) {
          best = d;
          victim = o;
        }
      });
    }
    if (victim != UINT32_MAX) {
      damage(victim, weapons_.damageAgainst(weapon, types_.get(units.get(victim).type).armor));
      hit = true;
    }
  }
  SimEvent ev;
  ev.type = EventType::Impact;
  ev.owner = owner;
  ev.weapon = static_cast<uint16_t>(weapon);
  ev.at = at;
  ev.hit = hit;
  events_.push_back(ev);
}

void Simulation::damage(uint32_t victim, int32_t amount) {
  if (amount <= 0) return;
  CUnit& u = reg_.pool<CUnit>().get(victim);
  if (u.health <= 0) return;
  u.health -= amount;
  if (u.health > 0) return;
  dead_.push_back(victim);
  const CPosition& p = reg_.pool<CPosition>().get(victim);
  SimEvent ev;
  ev.type = EventType::UnitDied;
  ev.owner = u.owner;
  ev.unitType = u.type;
  ev.at = {p.x, p.y};
  ev.facing = reg_.pool<CFacing>().get(victim).angle;
  if (const CTurret* t = reg_.pool<CTurret>().tryGet(victim)) ev.turret = t->angle;
  else ev.turret = ev.facing;
  events_.push_back(ev);
}

void Simulation::removeDead() {
  if (dead_.empty()) return;
  std::sort(dead_.begin(), dead_.end());
  dead_.erase(std::unique(dead_.begin(), dead_.end()), dead_.end());
  for (uint32_t i : dead_) reg_.destroy(reg_.handle(i));
  dead_.clear();
}

}  // namespace slave

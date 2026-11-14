// Pioneer Engine - scouts keep their distance (Milestone 9.3).
//
// A scout that is standing watch (no orders of its own) and sees an armed
// enemy come within kThreat falls back kFallBack tiles straight away from it,
// then stands watch again from there: it keeps the enemy in sight without
// trading shots, and without leaving the area. Orders the player gives it
// (move, attack) are never overridden.

#include <algorithm>

#include "pioneer/sim/simulation.hpp"

namespace pioneer {

namespace {
constexpr int32_t kThreat = 6 * LEPTONS_PER_TILE;
constexpr int32_t kFallBack = 5 * LEPTONS_PER_TILE;
}  // namespace

void Simulation::stepScouts() {
  if (tick_ % 6 != 1) return;
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  std::vector<std::pair<Entity, Vec2i>> fallBack;
  for (uint32_t e : units.entities()) {
    const CUnit& u = units.get(e);
    if (u.owner >= MAX_PLAYERS || u.health <= 0) continue;
    const UnitTypeDef& def = types_.get(u.type);
    if (!def.scout || def.speed <= 0) continue;
    const COrder* o = reg_.pool<COrder>().tryGet(e);
    if (!o || o->type != OrderType::Idle || o->forced || reg_.pool<CGarrisoned>().has(e)) continue;
    const CPosition& p = pos.get(e);
    // The nearest armed enemy we can see, close enough to be a threat.
    uint32_t bestD = UINT32_MAX;
    Vec2i threat{0, 0};
    spatial_.query(p.x, p.y, kThreat + structureReach_, [&](uint32_t o2) {
      const CUnit& v = units.get(o2);
      if (allied(v.owner, u.owner) || v.owner >= MAX_PLAYERS || v.health <= 0) return;
      const UnitTypeDef& vd = types_.get(v.type);
      if (vd.weapons.empty()) return;
      const CPosition& q = pos.get(o2);
      const uint32_t d = distance(q.x - p.x, q.y - p.y);
      if (d > static_cast<uint32_t>(kThreat) || d >= bestD || !unitVisibleTo(u.owner, o2)) return;
      bestD = d;
      threat = {q.x, q.y};
    });
    if (bestD == UINT32_MAX) continue;
    // Straight away from it; if that is blocked, the nearest open ground that way.
    const int32_t dx = p.x - threat.x, dy = p.y - threat.y;
    const uint32_t len = std::max<uint32_t>(1, distance(dx, dy));
    Vec2i to{p.x + static_cast<int32_t>(static_cast<int64_t>(dx) * kFallBack / len),
             p.y + static_cast<int32_t>(static_cast<int64_t>(dy) * kFallBack / len)};
    to.x = std::clamp(to.x, def.radius, map_.widthLeptons() - 1 - def.radius);
    to.y = std::clamp(to.y, def.radius, map_.heightLeptons() - 1 - def.radius);
    fallBack.push_back({reg_.handle(e), pathfinder_.nearestReachable({p.x, p.y}, to, def.locomotor)});
  }
  for (const auto& [e, to] : fallBack) {
    COrder& o = reg_.get<COrder>(e);
    o = COrder{};
    o.type = OrderType::Move;
    o.goal = to;
    if (CCombat* c = reg_.pool<CCombat>().tryGet(e.index)) c->target = kNullEntity;
    requestPath(e, to);
    SimEvent ev;
    ev.type = EventType::ScoutFallsBack;
    ev.owner = reg_.get<CUnit>(e).owner;
    ev.at = to;
    events_.push_back(ev);
  }
}

}  // namespace pioneer

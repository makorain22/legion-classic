// Slave Engine - orders, path following, collision and vision.
#include <algorithm>

#include "slave/sim/simulation.hpp"

namespace slave {

namespace {
constexpr int kPathBudgetPerTick = 60000;  // A* node expansions per tick, all units together
constexpr int kStuckCheckTicks = 30;
constexpr int kChaseReplanTicks = 15;
}  // namespace

void Simulation::requestPath(Entity e, Vec2i goal) {
  if (types_.get(reg_.get<CUnit>(e).type).speed <= 0) return;  // structures stay put
  CMover& m = reg_.get<CMover>(e);
  if (tick_ < m.retryTick) return;  // backing off after failed requests
  m.request = nextRequest_++;
  m.requestGoal = goal;
  pathQueue_.push_back({e, goal, m.request});
}

void Simulation::setPath(Entity e, std::vector<Vec2i> path) {
  CMover& m = reg_.get<CMover>(e);
  m.path = std::move(path);
  m.index = 0;
  m.active = !m.path.empty();
  m.request = 0;
  m.bestDist = INT32_MAX;
  m.stuckTicks = 0;
}

// Path requests are served in order, a bounded amount of work per tick, so a
// thousand units ordered at once never freeze the game.
void Simulation::processPathQueue() {
  int budget = kPathBudgetPerTick;
  while (pathQueueHead_ < pathQueue_.size() && budget > 0) {
    const PathRequest r = pathQueue_[pathQueueHead_++];
    if (!reg_.alive(r.unit)) continue;
    CMover* m = reg_.tryGet<CMover>(r.unit);
    if (!m || m->request != r.id) continue;  // superseded by a newer order
    const CPosition& p = reg_.get<CPosition>(r.unit);
    const Locomotor loco = types_.get(reg_.get<CUnit>(r.unit).type).locomotor;
    std::vector<Vec2i> path;
    pathfinder_.findPath({p.x, p.y}, r.goal, loco, path, 150000);
    ++stats_.pathSearches;
    stats_.pathExpansions += pathfinder_.expansionsLastCall();
    budget -= static_cast<int>(pathfinder_.expansionsLastCall()) + 400;  // every search costs something
    if (path.empty()) {
      m->request = 0;
      m->active = false;
      // Back off: 1 s, 2 s, 3 s... then give up on the order.
      ++m->failures;
      m->retryTick = tick_ + TICKS_PER_SECOND * m->failures;
      if (m->failures >= 4) {
        setOrderIdle(r.unit);
        m->failures = 0;
      }
    } else {
      m->failures = 0;
      setPath(r.unit, std::move(path));
    }
  }
  if (pathQueueHead_ >= pathQueue_.size()) {
    pathQueue_.clear();
    pathQueueHead_ = 0;
  }
  stats_.pathQueue = static_cast<uint32_t>(pathQueue_.size() - pathQueueHead_);
}

// High-level behaviour per order: chasing attack targets, resuming an
// attack-move after a fight, going idle on arrival.
void Simulation::stepOrders() {
  reg_.each<COrder, CMover, CPosition, CUnit>([&](uint32_t ei, COrder& o, CMover& m, CPosition& p, CUnit& u) {
    const Entity e = reg_.handle(ei);
    const UnitTypeDef& def = types_.get(u.type);
    switch (o.type) {
      case OrderType::Idle:
        break;
      case OrderType::Move:
        if (!m.active && !m.request) {
          // Arrived: turn to face the formation's front, then stand.
          CFacing& f = reg_.get<CFacing>(e);
          if (o.faceTo < 0 || def.speed <= 0 || turnToward(f.angle, static_cast<Angle>(o.faceTo), f.turnWait, def.turnTicks)) {
            o.type = OrderType::Idle;
            o.faceTo = -1;
            o.forced = false;
          }
        }
        break;
      case OrderType::Attack: {
        if (!reg_.alive(o.target) || !reg_.has<CUnit>(o.target)) {
          setOrderIdle(e);
          break;
        }
        const CPosition& tp = reg_.get<CPosition>(o.target);
        const int32_t d = effectiveDistance(distance(tp.x - p.x, tp.y - p.y), o.target.index);
        if (def.speed <= 0) break;  // a gun emplacement fires if it can, but never moves
        if (d <= maxRange(def) && canSee(u.owner, tp.x, tp.y)) {
          m.active = false;  // in range: stand and fire
          m.request = 0;
        } else if ((!m.active && !m.request) ||
                   ((tick_ + ei) % kChaseReplanTicks == 0 && distance(tp.x - o.chasedAt.x, tp.y - o.chasedAt.y) > 512)) {
          o.chasedAt = {tp.x, tp.y};
          requestPath(e, o.chasedAt);
        }
        break;
      }
      case OrderType::AttackMove: {
        const CCombat* c = reg_.pool<CCombat>().tryGet(ei);
        if (c && reg_.alive(c->target)) {
          const CPosition& tp = reg_.get<CPosition>(c->target);
          const int32_t d = effectiveDistance(distance(tp.x - p.x, tp.y - p.y), c->target.index);
          if (d <= maxRange(def)) {
            // In range: halt and fight. (Turrets still fire on the move under a
            // plain Move order; attack-move means "stop and engage".)
            m.active = false;
            m.request = 0;
          } else if ((!m.active && !m.request) || (tick_ + ei) % kChaseReplanTicks == 0) {
            o.chasedAt = {tp.x, tp.y};
            requestPath(e, o.chasedAt);
          }
        } else if (!m.active && !m.request) {
          if (distance(o.goal.x - p.x, o.goal.y - p.y) <= 192) o.type = OrderType::Move;  // arrived: face the front
          else requestPath(e, o.goal);  // resume the advance
        }
        break;
      }
    }
  });
}

void Simulation::stepMovement() {
  reg_.each<CMover, CPosition, CPrevPosition, CFacing, CUnit, CAnim>(
      [&](uint32_t ei, CMover& m, CPosition& pos, CPrevPosition& prev, CFacing& facing, CUnit& unit, CAnim& anim) {
        prev.x = pos.x;
        prev.y = pos.y;
        anim.moving = false;
        if (!m.active) return;
        const UnitTypeDef& def = types_.get(unit.type);
        if (m.index >= m.path.size()) {
          m.active = false;
          return;
        }
        const Vec2i wp = m.path[m.index];
        const int32_t dx = wp.x - pos.x, dy = wp.y - pos.y;
        const uint32_t dist = distance(dx, dy);

        // Stuck detection: if we haven't got closer for a while, re-plan.
        if (static_cast<int32_t>(dist) + 24 < m.bestDist) {
          m.bestDist = static_cast<int32_t>(dist);
          m.stuckTicks = 0;
        } else if (++m.stuckTicks > kStuckCheckTicks) {
          m.stuckTicks = 0;
          m.bestDist = INT32_MAX;
          if (m.repaths < 3 && !m.path.empty()) {
            ++m.repaths;
            requestPath(reg_.handle(ei), m.path.back());
          } else {
            m.active = false;
          }
          return;
        }

        if (dist <= static_cast<uint32_t>(def.speed)) {
          pos.x = wp.x;
          pos.y = wp.y;
          anim.moving = dist > 0;
          ++m.index;
          m.bestDist = INT32_MAX;
          if (m.index >= m.path.size()) {
            m.active = false;
            m.repaths = 0;
          }
          return;
        }

        // Turn in visible 1/16 steps before driving, like the classics.
        const Angle desired = atan2Angle(dy, dx);
        const int32_t want = facingOf(desired), have = facingOf(facing.angle);
        if (want != have) {
          if (def.turnTicks == 0) {
            facing.angle = angleOfFacing(want);
          } else {
            if (facing.turnWait > 0) {
              --facing.turnWait;
              return;
            }
            facing.angle = angleOfFacing(have + (angleDelta(facing.angle, desired) > 0 ? 1 : -1));
            facing.turnWait = static_cast<uint8_t>(def.turnTicks - 1);
            // Vehicles may creep forward while turning gently, not while pivoting hard.
            if (std::abs(angleDelta(facing.angle, desired)) > ANGLE_PER_FACING) return;
          }
        }

        const int32_t nx = pos.x + static_cast<int32_t>(static_cast<int64_t>(dx) * def.speed / dist);
        const int32_t ny = pos.y + static_cast<int32_t>(static_cast<int64_t>(dy) * def.speed / dist);
        const int ctx = pos.x >> LEPTON_SHIFT, cty = pos.y >> LEPTON_SHIFT;
        const int ntx = nx >> LEPTON_SHIFT, nty = ny >> LEPTON_SHIFT;
        if ((ntx != ctx || nty != cty) && !map_.passable(ntx, nty, def.locomotor)) {
          m.stuckTicks = kStuckCheckTicks;  // terrain changed under us: re-plan next tick
          return;
        }
        pos.x = nx;
        pos.y = ny;
        anim.moving = true;
        if (++anim.timer >= def.animTicks) {
          anim.timer = 0;
          anim.frame = static_cast<uint8_t>((anim.frame + 1) & 3);
        }
      });
}

void Simulation::rebuildSpatial() {
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  spatial_.beginBuild();
  for (uint32_t e : units.entities()) {
    const CPosition& p = pos.get(e);
    spatial_.count(p.x, p.y);
  }
  spatial_.endCount();
  for (uint32_t e : units.entities()) {
    const CPosition& p = pos.get(e);
    spatial_.insert(e, p.x, p.y);
  }
}

// Units push apart so they don't stack. Pushes are summed first and applied
// afterwards, so the result doesn't depend on processing order within a tick.
void Simulation::stepSeparation() {
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  const auto& ents = units.entities();
  pushX_.assign(ents.size(), 0);
  pushY_.assign(ents.size(), 0);
  for (size_t i = 0; i < ents.size(); ++i) {
    const uint32_t e = ents[i];
    const CPosition& p = pos.get(e);
    const UnitTypeDef& da = types_.get(units.get(e).type);
    if (da.isStructure()) continue;  // never pushed; their tiles are blocked anyway
    const int32_t ra = da.radius;
    int32_t px = 0, py = 0;
    spatial_.query(p.x, p.y, ra + 120, [&](uint32_t o) {
      if (o == e) return;
      const CPosition& q = pos.get(o);
      const UnitTypeDef& db = types_.get(units.get(o).type);
      if (db.isStructure()) return;
      const int32_t rb = db.radius;
      // Vehicles keep almost their full width apart; infantry may bunch up.
      const int32_t want = (ra > 60 && rb > 60) ? (ra + rb) * 9 / 10 : (ra + rb) * 7 / 10;
      int32_t dx = p.x - q.x, dy = p.y - q.y;
      if (std::abs(dx) >= want || std::abs(dy) >= want) return;
      uint32_t d = distance(dx, dy);
      if (d >= static_cast<uint32_t>(want)) return;
      if (d == 0) {  // exactly on top: separate along a direction picked by id
        dx = (e < o) ? 1 : -1;
        dy = static_cast<int32_t>(e % 3) - 1;
        d = 1;
      }
      const int32_t overlap = want - static_cast<int32_t>(d);
      // Lighter units (smaller radius) get pushed more.
      const int32_t share = overlap * rb / (ra + rb);
      px += static_cast<int32_t>(static_cast<int64_t>(dx) * share / static_cast<int32_t>(d));
      py += static_cast<int32_t>(static_cast<int64_t>(dy) * share / static_cast<int32_t>(d));
    });
    const int32_t cap = ra > 60 ? 28 : 20;
    pushX_[i] = std::clamp(px / 2, -cap, cap);
    pushY_[i] = std::clamp(py / 2, -cap, cap);
  }
  for (size_t i = 0; i < ents.size(); ++i) {
    if (!pushX_[i] && !pushY_[i]) continue;
    CPosition& p = pos.get(ents[i]);
    const Locomotor loco = types_.get(units.get(ents[i]).type).locomotor;
    const int32_t nx = std::clamp(p.x + pushX_[i], 0, map_.widthLeptons() - 1);
    const int32_t ny = std::clamp(p.y + pushY_[i], 0, map_.heightLeptons() - 1);
    if (map_.passable(nx >> LEPTON_SHIFT, ny >> LEPTON_SHIFT, loco)) {
      p.x = nx;
      p.y = ny;
    }
  }
}

void Simulation::stepVision() {
  vision_.begin();
  // Allies share what they see.
  std::array<std::array<uint8_t, MAX_PLAYERS>, MAX_PLAYERS> viewers{};
  std::array<uint8_t, MAX_PLAYERS> viewerCount{};
  for (int a = 0; a < MAX_PLAYERS; ++a)
    for (int b = 0; b < MAX_PLAYERS; ++b)
      if (team_[static_cast<size_t>(a)] == team_[static_cast<size_t>(b)])
        viewers[static_cast<size_t>(a)][viewerCount[static_cast<size_t>(a)]++] = static_cast<uint8_t>(b);
  reg_.each<CUnit, CPosition>([&](uint32_t, CUnit& u, CPosition& p) {
    const size_t o = u.owner % MAX_PLAYERS;
    const int r = types_.get(u.type).sight >> LEPTON_SHIFT;
    for (uint8_t i = 0; i < viewerCount[o]; ++i) vision_.stamp(viewers[o][i], p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT, r);
  });
  vision_.end();
}

bool Simulation::canSee(uint8_t player, int32_t x, int32_t y) const {
  return vision_.visible(player, x >> LEPTON_SHIFT, y >> LEPTON_SHIFT);
}

}  // namespace slave

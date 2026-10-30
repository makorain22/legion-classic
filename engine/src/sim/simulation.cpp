#include "slave/sim/simulation.hpp"

#include <algorithm>
#include <chrono>

namespace slave {

Simulation::Simulation(const Map& map, const UnitTypeRegistry& types, const WeaponRegistry& weapons, uint64_t seed)
    : map_(map), types_(types), weapons_(weapons), rng_(seed), pathfinder_(map) {
  spatial_.reset(map.widthLeptons(), map.heightLeptons());
  vision_.resize(map.width(), map.height());
}

int32_t Simulation::maxRange(const UnitTypeDef& def) const {
  int32_t r = 0;
  for (int w : def.weapons) {
    r = std::max(r, weapons_.get(w).range);
    if (weapons_.get(w).vsInfantry >= 0) r = std::max(r, weapons_.get(weapons_.get(w).vsInfantry).range);
  }
  return r;
}

Entity Simulation::spawnUnit(uint16_t type, uint8_t player, Vec2i pos, Angle facing) {
  const UnitTypeDef& def = types_.get(type);
  pos.x = std::clamp(pos.x, def.radius, map_.widthLeptons() - 1 - def.radius);
  pos.y = std::clamp(pos.y, def.radius, map_.heightLeptons() - 1 - def.radius);
  // Never place a unit in water or on a cliff: move it to the nearest ground it can stand on.
  const int tx = pos.x >> LEPTON_SHIFT, ty = pos.y >> LEPTON_SHIFT;
  if (!map_.passable(tx, ty, def.locomotor)) {
    bool found = false;
    for (int r = 1; r <= 8 && !found; ++r)
      for (int dy = -r; dy <= r && !found; ++dy)
        for (int dx = -r; dx <= r && !found; ++dx) {
          if (std::max(std::abs(dx), std::abs(dy)) != r || !map_.passable(tx + dx, ty + dy, def.locomotor)) continue;
          pos = {(tx + dx) * LEPTONS_PER_TILE + (pos.x & 255), (ty + dy) * LEPTONS_PER_TILE + (pos.y & 255)};
          found = true;
        }
    if (!found) return kNullEntity;
  }
  const Entity e = reg_.create();
  const Angle a = angleOfFacing(facingOf(facing));
  reg_.add<CPosition>(e, {pos.x, pos.y});
  reg_.add<CPrevPosition>(e, {pos.x, pos.y});
  reg_.add<CFacing>(e, {a, 0});
  reg_.add<CUnit>(e, {type, player, def.maxHealth});
  reg_.add<CMover>(e, {});
  reg_.add<COrder>(e, {});
  reg_.add<CAnim>(e, {});
  if (!def.weapons.empty()) reg_.add<CCombat>(e, {});
  if (def.turret) reg_.add<CTurret>(e, {a, 0});
  return e;
}

void Simulation::tick() {
  events_.clear();
  spawnLog_.clear();
  pathfinder_.refresh();

  std::vector<Command> cmds;
  cmds.swap(pending_);
  for (const Command& c : cmds) applyCommand(c);

  // Timing is for the profiler only; it never feeds back into the game.
  using Clock = std::chrono::steady_clock;
  auto t = Clock::now();
  auto lap = [&t](float& out) {
    const auto now = Clock::now();
    out = std::chrono::duration<float, std::milli>(now - t).count();
    t = now;
  };
  processPathQueue();
  lap(stats_.msPaths);
  stepOrders();
  lap(stats_.msOrders);
  stepMovement();
  lap(stats_.msMove);
  rebuildSpatial();
  lap(stats_.msSpatial);
  stepSeparation();
  lap(stats_.msSeparation);
  if (tick_ % 4 == 0) stepVision();
  lap(stats_.msVision);
  stepTargeting();
  lap(stats_.msTargeting);
  stepWeapons();
  lap(stats_.msWeapons);
  stepProjectiles();
  lap(stats_.msProjectiles);
  removeDead();
  stats_.projectiles = static_cast<uint32_t>(reg_.pool<CProjectile>().size());
  ++tick_;
}

void Simulation::setOrderIdle(Entity e) {
  if (auto* o = reg_.tryGet<COrder>(e)) *o = COrder{};
  if (auto* m = reg_.tryGet<CMover>(e)) {
    m->active = false;
    m->request = 0;
    m->path.clear();
  }
  if (auto* c = reg_.tryGet<CCombat>(e)) c->target = kNullEntity;
}

void Simulation::applyCommand(const Command& cmd) {
  switch (cmd.type) {
    case CommandType::Move:
      orderMove(cmd.units, cmd.target, OrderType::Move);
      break;
    case CommandType::AttackMove:
      orderMove(cmd.units, cmd.target, OrderType::AttackMove);
      break;
    case CommandType::Attack: {
      if (!reg_.alive(cmd.targetUnit) || !reg_.has<CUnit>(cmd.targetUnit)) break;
      for (Entity e : cmd.units) {
        if (!reg_.alive(e) || !reg_.has<CCombat>(e) || e == cmd.targetUnit) continue;
        COrder& o = reg_.get<COrder>(e);
        o = COrder{};
        o.type = OrderType::Attack;
        o.target = cmd.targetUnit;
        reg_.get<CCombat>(e).target = cmd.targetUnit;
        const CPosition& tp = reg_.get<CPosition>(cmd.targetUnit);
        o.chasedAt = {tp.x, tp.y};
        requestPath(e, o.chasedAt);
      }
      break;
    }
    case CommandType::Stop:
      for (Entity e : cmd.units)
        if (reg_.alive(e)) setOrderIdle(e);
      break;
    case CommandType::Spawn: {
      if (cmd.unitType >= types_.size()) break;
      const int32_t spacing = std::max(128, types_.get(cmd.unitType).radius * 2);
      int32_t side = 1;
      while (side * side < cmd.count) ++side;
      for (int32_t i = 0; i < cmd.count; ++i) {
        const int32_t gx = i % side, gy = i / side;
        const Vec2i p{cmd.target.x + (gx * 2 - (side - 1)) * spacing / 2,
                      cmd.target.y + (gy * 2 - (side - 1)) * spacing / 2};
        spawnUnit(cmd.unitType, cmd.player, p, cmd.facing);
      }
      break;
    }
    case CommandType::Remove:
      for (Entity e : cmd.units)
        if (reg_.alive(e) && reg_.has<CUnit>(e)) reg_.destroy(e);
      break;
    case CommandType::SpawnList:
      for (size_t i = 0; i < cmd.spawns.size(); ++i) {
        const SpawnSpec& s = cmd.spawns[i];
        if (s.type >= types_.size()) continue;
        const Entity e = spawnUnit(s.type, s.owner, s.pos, s.facing);
        if (!e.valid()) continue;
        if (s.health > 0) reg_.get<CUnit>(e).health = std::min(s.health, types_.get(s.type).maxHealth);
        spawnLog_.push_back({cmd.tag, cmd.indexBase + static_cast<uint32_t>(i), e});
      }
      break;
    case CommandType::Edit:
      for (const UnitEdit& ed : cmd.edits) applyEdit(ed);
      break;
    case CommandType::Kill:
      for (Entity e : cmd.units)
        if (reg_.alive(e) && reg_.has<CUnit>(e)) damage(e.index, INT32_MAX / 2);
      break;
    case CommandType::Strike: {
      if (cmd.weapon >= weapons_.size()) break;
      const int32_t r = std::max<int32_t>(cmd.radius, 0);
      for (int i = 0; i < cmd.count; ++i) {
        Vec2i at = cmd.target;
        if (r > 0) {  // spread evenly over the disc, not bunched at the centre
          do {
            at = {cmd.target.x + rng_.range(-r, r), cmd.target.y + rng_.range(-r, r)};
          } while (distance(at.x - cmd.target.x, at.y - cmd.target.y) > static_cast<uint32_t>(r));
        }
        at.x = std::clamp(at.x, 0, map_.widthLeptons() - 1);
        at.y = std::clamp(at.y, 0, map_.heightLeptons() - 1);
        const Entity pe = reg_.create();
        CProjectile pr;
        pr.from = pr.to = at;  // falls from the sky: no direction, no rear-armour bonus
        pr.x = at.x;
        pr.y = at.y;
        pr.total = 1;
        pr.weapon = cmd.weapon;
        pr.owner = NEUTRAL_OWNER;
        pr.delay = static_cast<uint16_t>(rng_.below(static_cast<uint32_t>(cmd.count) * 3 + 1));
        reg_.add<CProjectile>(pe, pr);
      }
      break;
    }
    case CommandType::SetTeam:
      if (cmd.player < MAX_PLAYERS) team_[cmd.player] = static_cast<uint8_t>(cmd.count);
      break;
  }
}

// Group move. Units are split by movement type; each group shares one A*
// path from its centre, and every unit takes its own formation slot at the
// end. Units with a clear straight line skip pathfinding entirely.
void Simulation::applyEdit(const UnitEdit& ed) {
  if (!reg_.alive(ed.unit) || !reg_.has<CUnit>(ed.unit)) return;
  CUnit& u = reg_.get<CUnit>(ed.unit);
  const UnitTypeDef& def = types_.get(u.type);
  if (ed.move) {
    const Vec2i p{std::clamp(ed.pos.x, def.radius, map_.widthLeptons() - 1 - def.radius),
                  std::clamp(ed.pos.y, def.radius, map_.heightLeptons() - 1 - def.radius)};
    if (map_.passable(p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT, def.locomotor)) {
      reg_.get<CPosition>(ed.unit) = {p.x, p.y};
      reg_.get<CPrevPosition>(ed.unit) = {p.x, p.y};  // a jump, not a slide
      setOrderIdle(ed.unit);
    }
  }
  if (ed.facing >= 0) {
    const Angle a = angleOfFacing(facingOf(static_cast<Angle>(ed.facing)));
    reg_.get<CFacing>(ed.unit).angle = a;
    if (CTurret* t = reg_.tryGet<CTurret>(ed.unit)) t->angle = a;
  }
  if (ed.owner >= 0 && ed.owner < MAX_PLAYERS && ed.owner != u.owner) {
    u.owner = static_cast<uint8_t>(ed.owner);
    setOrderIdle(ed.unit);  // a defector forgets its old orders and targets
    if (CCombat* c = reg_.tryGet<CCombat>(ed.unit)) c->target = kNullEntity;
  }
  if (ed.health >= 0) u.health = std::clamp(ed.health, 1, def.maxHealth);
}

void Simulation::orderMove(const std::vector<Entity>& units, Vec2i target, OrderType type) {
  std::array<std::vector<Entity>, 3> groups;
  for (Entity e : units)
    if (reg_.alive(e) && reg_.has<CMover>(e))
      groups[static_cast<size_t>(types_.get(reg_.get<CUnit>(e).type).locomotor)].push_back(e);

  for (int li = 0; li < 3; ++li) {
    std::vector<Entity>& g = groups[static_cast<size_t>(li)];
    if (g.empty()) continue;
    const Locomotor loco = static_cast<Locomotor>(li);

    // Formation slots: a square block around the target, nearest first.
    int32_t spacing = 128;
    for (Entity e : g) spacing = std::max(spacing, types_.get(reg_.get<CUnit>(e).type).radius * 2);
    const int32_t n = static_cast<int32_t>(g.size());
    int32_t side = 1;
    while (side * side < n) ++side;
    std::vector<Vec2i> slots;
    for (int32_t gy = 0; gy < side; ++gy)
      for (int32_t gx = 0; gx < side; ++gx)
        slots.push_back({target.x + (gx * 2 - (side - 1)) * spacing / 2, target.y + (gy * 2 - (side - 1)) * spacing / 2});
    std::stable_sort(slots.begin(), slots.end(), [&](Vec2i a, Vec2i b) {
      return distance(a.x - target.x, a.y - target.y) < distance(b.x - target.x, b.y - target.y);
    });
    slots.resize(static_cast<size_t>(n));

    // Leader: the unit nearest the group's centre. Its path is shared.
    int64_t cx = 0, cy = 0;
    for (Entity e : g) {
      cx += reg_.get<CPosition>(e).x;
      cy += reg_.get<CPosition>(e).y;
    }
    cx /= n;
    cy /= n;
    Entity leader = g.front();
    uint32_t bestD = UINT32_MAX;
    for (Entity e : g) {
      const CPosition& p = reg_.get<CPosition>(e);
      const uint32_t d = distance(static_cast<int32_t>(p.x - cx), static_cast<int32_t>(p.y - cy));
      if (d < bestD) {
        bestD = d;
        leader = e;
      }
    }
    const CPosition& lp = reg_.get<CPosition>(leader);
    const Vec2i lpos{lp.x, lp.y};
    for (Vec2i& s : slots) s = pathfinder_.nearestReachable(lpos, s, loco);
    std::vector<Vec2i> groupPath;
    pathfinder_.findPath(lpos, target, loco, groupPath, 400000);
    ++stats_.pathSearches;
    stats_.pathExpansions += pathfinder_.expansionsLastCall();

    // Assign slots: farthest units choose first (small groups), or by
    // row-major order (huge groups, where matching would be too slow).
    std::vector<Entity> order = g;
    std::vector<bool> taken(slots.size(), false);
    auto rowMajor = [](Vec2i a, Vec2i b) { return a.y != b.y ? a.y < b.y : a.x < b.x; };
    std::vector<size_t> assigned(order.size());
    if (n > 400) {
      std::stable_sort(order.begin(), order.end(), [&](Entity a, Entity b) {
        const CPosition &pa = reg_.get<CPosition>(a), &pb = reg_.get<CPosition>(b);
        return rowMajor({pa.x, pa.y}, {pb.x, pb.y});
      });
      std::vector<size_t> idx(slots.size());
      for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
      std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return rowMajor(slots[a], slots[b]); });
      for (size_t i = 0; i < order.size(); ++i) assigned[i] = idx[i];
    } else {
      std::stable_sort(order.begin(), order.end(), [&](Entity a, Entity b) {
        const CPosition &pa = reg_.get<CPosition>(a), &pb = reg_.get<CPosition>(b);
        return distance(pa.x - target.x, pa.y - target.y) > distance(pb.x - target.x, pb.y - target.y);
      });
      for (size_t k = 0; k < order.size(); ++k) {
        const CPosition& p = reg_.get<CPosition>(order[k]);
        size_t best = 0;
        uint32_t bd = UINT32_MAX;
        for (size_t i = 0; i < slots.size(); ++i) {
          if (taken[i]) continue;
          const uint32_t d = distance(slots[i].x - p.x, slots[i].y - p.y);
          if (d < bd) {
            bd = d;
            best = i;
          }
        }
        taken[best] = true;
        assigned[k] = best;
      }
    }

    for (size_t k = 0; k < order.size(); ++k) {
      const Entity e = order[k];
      const Vec2i slot = slots[assigned[k]];
      const CPosition& p = reg_.get<CPosition>(e);
      const Vec2i pos{p.x, p.y};
      COrder& o = reg_.get<COrder>(e);
      o = COrder{};
      o.type = type;
      o.goal = slot;
      if (auto* c = reg_.tryGet<CCombat>(e)) c->target = kNullEntity;

      if (pathfinder_.lineClear(pos, slot, loco)) {
        setPath(e, {slot});
        continue;
      }
      // Join the shared path at the farthest early waypoint we can see.
      int join = -1;
      const int look = std::min<int>(static_cast<int>(groupPath.size()) - 1, 6);
      for (int j = look; j >= 0; --j)
        if (pathfinder_.lineClear(pos, groupPath[static_cast<size_t>(j)], loco)) {
          join = j;
          break;
        }
      if (join >= 0) {
        std::vector<Vec2i> path(groupPath.begin() + join, groupPath.end());
        if (!path.empty() && path.back() != slot) path.push_back(slot);
        setPath(e, std::move(path));
      } else {
        requestPath(e, slot);
      }
    }
  }
}

uint64_t Simulation::stateHash() {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a
  auto mix = [&h](uint64_t v) {
    for (int i = 0; i < 8; ++i) {
      h ^= (v >> (i * 8)) & 0xff;
      h *= 1099511628211ULL;
    }
  };
  mix(tick_);
  mix(rng_.state());
  for (uint8_t t : team_) mix(t);
  reg_.each<CUnit, CPosition, CFacing>([&](uint32_t e, CUnit& u, CPosition& p, CFacing& f) {
    mix(e);
    mix(u.type | (uint64_t{u.owner} << 16) | (uint64_t(uint32_t(u.health)) << 32));
    mix(uint64_t(uint32_t(p.x)) | (uint64_t(uint32_t(p.y)) << 32));
    mix(f.angle);
    if (const CTurret* t = reg_.pool<CTurret>().tryGet(e)) mix(t->angle);
  });
  reg_.each<CProjectile>([&](uint32_t e, CProjectile& pr) {
    mix(e);
    mix(uint64_t(uint32_t(pr.x)) | (uint64_t(uint32_t(pr.y)) << 32));
  });
  return h;
}

}  // namespace slave

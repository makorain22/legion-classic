#include "slave/sim/simulation.hpp"

#include <algorithm>
#include <chrono>

namespace slave {

Simulation::Simulation(Map& map, const UnitTypeRegistry& types, const WeaponRegistry& weapons, uint64_t seed)
    : map_(map), types_(types), weapons_(weapons), rng_(seed), pathfinder_(map) {
  spatial_.reset(map.widthLeptons(), map.heightLeptons());
  vision_.resize(map.width(), map.height());
  map_.clearBlocks();  // structures re-mark their tiles as they are created
  tileStructure_.assign(static_cast<size_t>(map.width()) * map.height(), UINT32_MAX);
  cover_.assign(static_cast<size_t>(map.width()) * map.height(), 0);
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
  if (def.isStructure()) {
    // Structures go exactly where asked, on the grid, or not at all.
    const Vec2i o = footprintOrigin(type, pos);
    if (!canPlace(type, o)) return kNullEntity;
    const Entity e = reg_.create();
    const Vec2i c{o.x * 256 + def.footW * 128, o.y * 256 + def.footH * 128};
    const Angle a = angleOfFacing(facingOf(facing));
    reg_.add<CPosition>(e, {c.x, c.y});
    reg_.add<CPrevPosition>(e, {c.x, c.y});
    reg_.add<CFacing>(e, {a, 0});
    reg_.add<CUnit>(e, {type, player, def.maxHealth});
    reg_.add<CMover>(e, {});
    reg_.add<COrder>(e, {});
    reg_.add<CAnim>(e, {});
    reg_.add<CStructure>(e, {o.x, o.y, def.footW, def.footH, tick_});
    if (def.role == "barracks" || def.role == "factory") reg_.add<CProducer>(e, {});
    if (!def.weapons.empty()) reg_.add<CCombat>(e, {});
    if (def.turret) reg_.add<CTurret>(e, {angleOfFacing(4), 0});
    occupy(e.index, def, o);
    SimEvent ev;
    ev.type = EventType::StructurePlaced;
    ev.owner = player;
    ev.unitType = type;
    ev.at = c;
    events_.push_back(ev);
    // A refinery comes with its first supply truck.
    if (def.freeUnit >= 0) {
      const Entity t = spawnUnit(static_cast<uint16_t>(def.freeUnit), player, exitOf(e), angleOfFacing(4));
      (void)t;
    }
    return e;
  }
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
  if (def.capacity > 0) {  // supply trucks start work at once
    reg_.add<CHarvester>(e, {});
    reg_.get<COrder>(e).type = OrderType::Harvest;
  }
  return e;
}

void Simulation::tick() {
  events_.clear();
  spawnLog_.clear();
  pathfinder_.refresh();

  if (ai_.any()) ai_.update(*this);  // the computer players give their orders like anyone else
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
  stepGarrisons();
  lap(stats_.msOrders);
  stepEconomy();
  stepScript();
  lap(stats_.msEconomy);
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
  stepGroups();
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
  // Men inside a building come out to go anywhere.
  if (cmd.type == CommandType::Move || cmd.type == CommandType::AttackMove || cmd.type == CommandType::Repair)
    for (Entity e : cmd.units)
      if (reg_.alive(e) && reg_.has<CGarrisoned>(e)) leaveBuilding(e);
  switch (cmd.type) {
    case CommandType::Move:
      orderMove(cmd.units, cmd.target, OrderType::Move, cmd);
      break;
    case CommandType::AttackMove:
      orderMove(cmd.units, cmd.target, OrderType::AttackMove, cmd);
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
        if (reg_.alive(e) && reg_.has<CUnit>(e)) destroyEntity(e.index);
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
    case CommandType::SetStance:
      for (Entity e : cmd.units)
        if (reg_.alive(e) && reg_.has<CUnit>(e)) reg_.get<CUnit>(e).stance = cmd.stance;
      break;
    case CommandType::Produce:
    case CommandType::PlaceBuilding:
    case CommandType::PlaceWalls:
    case CommandType::Sell:
    case CommandType::Deploy:
    case CommandType::Harvest:
    case CommandType::SetRally:
    case CommandType::SetCredits:
    case CommandType::Repair:
      applyEconomyCommand(cmd);
      break;
    case CommandType::BindGroup:
      if (cmd.player < MAX_PLAYERS) bindUnits(cmd.units, cmd.player);
      break;
    case CommandType::UnbindGroup: {
      std::vector<Entity> groups;
      for (Entity e : cmd.units)
        if (const CGroupMember* m = reg_.tryGet<CGroupMember>(e))
          if (std::find(groups.begin(), groups.end(), m->group) == groups.end()) groups.push_back(m->group);
      for (Entity g : groups) disband(g);
      break;
    }
    case CommandType::Occupy:
      applyOccupy(cmd);
      break;
    case CommandType::Unload:
      applyUnload(cmd);
      break;
    case CommandType::SetController:
      if (cmd.player < MAX_PLAYERS)
        ai_.setPlayer(cmd.player, cmd.amount >= 0, static_cast<AiDifficulty>(std::clamp(cmd.amount, 0, 2)));
      break;
    case CommandType::SetTeam:
      if (cmd.player < MAX_PLAYERS) team_[cmd.player] = static_cast<uint8_t>(cmd.count);
      break;
  }
}

void Simulation::applyEdit(const UnitEdit& ed) {
  if (!reg_.alive(ed.unit) || !reg_.has<CUnit>(ed.unit)) return;
  CUnit& u = reg_.get<CUnit>(ed.unit);
  const UnitTypeDef& def = types_.get(u.type);
  if (ed.move && def.speed > 0) {
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

// Group move. Units are split by movement type; each group shares one A*
// path from its centre, and every unit takes its own slot in the chosen
// formation at the end. Armour forms up in front, wheeled vehicles behind it,
// infantry at the back. Units with a clear straight line skip pathfinding.
namespace {
// Formation slots in the formation's own frame: x across the front (right is
// positive), y backwards from the front rank. Leptons.
std::vector<Vec2i> formationShape(Formation f, int32_t n, int32_t spacing, int32_t width) {
  std::vector<Vec2i> out;
  out.reserve(static_cast<size_t>(n));
  if (f == Formation::Loose) spacing = spacing * 9 / 5;
  int32_t cols = 1;
  switch (f) {
    case Formation::Line: cols = std::max<int32_t>(1, (n + 2) / 3); break;  // three ranks
    case Formation::Column: cols = std::min<int32_t>(2, n); break;
    case Formation::Wedge: break;
    default: while (cols * cols < n) ++cols; break;
  }
  if (width > 0 && f != Formation::Wedge && f != Formation::Column)
    cols = std::clamp<int32_t>(width / spacing + 1, 1, n);  // a dragged front line sets the width
  if (f == Formation::Wedge) {
    // Row r holds 2r+1 units, centred: the point leads.
    for (int32_t r = 0; static_cast<int32_t>(out.size()) < n; ++r)
      for (int32_t i = -r; i <= r && static_cast<int32_t>(out.size()) < n; ++i)
        out.push_back({i * spacing, r * spacing * 4 / 5});
    return out;
  }
  const int32_t rows = (n + cols - 1) / cols;
  for (int32_t r = 0; r < rows; ++r) {
    const int32_t inRow = std::min(cols, n - r * cols);  // last rank centred
    for (int32_t c = 0; c < inRow; ++c) out.push_back({(c * 2 - (inRow - 1)) * spacing / 2, r * spacing});
  }
  return out;
}
}  // namespace

void Simulation::orderMove(const std::vector<Entity>& units, Vec2i target, OrderType type, const Command& cmd) {
  std::array<std::vector<Entity>, 3> groups;
  int64_t cx = 0, cy = 0;
  int32_t count = 0;
  for (Entity e : units)
    if (reg_.alive(e) && reg_.has<CMover>(e) && types_.get(reg_.get<CUnit>(e).type).speed > 0) {
      groups[static_cast<size_t>(types_.get(reg_.get<CUnit>(e).type).locomotor)].push_back(e);
      cx += reg_.get<CPosition>(e).x;
      cy += reg_.get<CPosition>(e).y;
      ++count;
    }
  if (count == 0) return;
  cx /= count;
  cy /= count;

  // Which way the formation faces: as dragged, or the way the group travels.
  Angle facing;
  if (cmd.formFacing >= 0) facing = static_cast<Angle>(cmd.formFacing & ANGLE_MASK);
  else if (target.x == cx && target.y == cy) facing = 0;
  else facing = atan2Angle(static_cast<int32_t>(target.y - cy), static_cast<int32_t>(target.x - cx));
  const int32_t fx = cosQ16(facing), fy = sinQ16(facing);  // forward, Q16.16
  auto toWorld = [&](Vec2i centre, Vec2i local) {
    // right = (-fy, fx); local y runs backwards.
    const int64_t wx = (static_cast<int64_t>(-fy) * local.x - static_cast<int64_t>(fx) * local.y) / 65536;
    const int64_t wy = (static_cast<int64_t>(fx) * local.x - static_cast<int64_t>(fy) * local.y) / 65536;
    return Vec2i{centre.x + static_cast<int32_t>(wx), centre.y + static_cast<int32_t>(wy)};
  };

  // Front to back: tracked, wheeled, foot.
  const int order3[3] = {static_cast<int>(Locomotor::Tracked), static_cast<int>(Locomotor::Wheeled),
                         static_cast<int>(Locomotor::Foot)};
  int32_t depthSoFar = 0, centreShift = 0;
  bool first = true;
  for (int li : order3) {
    std::vector<Entity>& g = groups[static_cast<size_t>(li)];
    if (g.empty()) continue;
    const Locomotor loco = static_cast<Locomotor>(li);
    const int32_t n = static_cast<int32_t>(g.size());
    int32_t spacing = 128;
    for (Entity e : g) spacing = std::max(spacing, types_.get(reg_.get<CUnit>(e).type).radius * 2);

    std::vector<Vec2i> local = formationShape(cmd.formation, n, spacing, cmd.formWidth);
    int32_t depth = 0;
    for (const Vec2i& l : local) depth = std::max(depth, l.y);
    // The first group's front rank sits on the target (or, without a dragged
    // front, the first group is centred on it); later groups line up behind.
    if (first) centreShift = cmd.formFacing < 0 ? -depth / 2 : 0;
    else depthSoFar += spacing;
    for (Vec2i& l : local) l.y += depthSoFar + centreShift;
    depthSoFar += depth;
    first = false;

    // Leader: the unit nearest the group's centre. Its path is shared.
    int64_t gx = 0, gy = 0;
    for (Entity e : g) {
      gx += reg_.get<CPosition>(e).x;
      gy += reg_.get<CPosition>(e).y;
    }
    gx /= n;
    gy /= n;
    Entity leader = g.front();
    uint32_t bestD = UINT32_MAX;
    for (Entity e : g) {
      const CPosition& p = reg_.get<CPosition>(e);
      const uint32_t d = distance(static_cast<int32_t>(p.x - gx), static_cast<int32_t>(p.y - gy));
      if (d < bestD) {
        bestD = d;
        leader = e;
      }
    }
    const CPosition& lp = reg_.get<CPosition>(leader);
    const Vec2i lpos{lp.x, lp.y};
    std::vector<Vec2i> slots(local.size());
    for (size_t i = 0; i < local.size(); ++i) slots[i] = pathfinder_.nearestReachable(lpos, toWorld(target, local[i]), loco);
    std::vector<Vec2i> groupPath;
    pathfinder_.findPath(lpos, target, loco, groupPath, 400000);
    ++stats_.pathSearches;
    stats_.pathExpansions += pathfinder_.expansionsLastCall();

    // Assign slots so paths don't cross: small groups pick greedily (farthest
    // unit first); big groups match rank-and-file order in the formation frame.
    std::vector<Entity> order = g;
    std::vector<size_t> assigned(order.size());
    if (n > 300) {
      auto key = [&](int32_t x, int32_t y) {  // (row from the front, column from the left)
        const int64_t ax = (static_cast<int64_t>(x - target.x) * -fy + static_cast<int64_t>(y - target.y) * fx) / 65536;
        const int64_t ay = -(static_cast<int64_t>(x - target.x) * fx + static_cast<int64_t>(y - target.y) * fy) / 65536;
        return std::pair<int64_t, int64_t>{ay / spacing, ax};
      };
      std::stable_sort(order.begin(), order.end(), [&](Entity a, Entity b) {
        const CPosition &pa = reg_.get<CPosition>(a), &pb = reg_.get<CPosition>(b);
        return key(pa.x, pa.y) < key(pb.x, pb.y);
      });
      std::vector<size_t> idx(local.size());
      for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
      std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        return std::pair<int64_t, int64_t>{local[a].y / spacing, local[a].x} < std::pair<int64_t, int64_t>{local[b].y / spacing, local[b].x};
      });
      for (size_t i = 0; i < order.size(); ++i) assigned[i] = idx[i];
    } else {
      std::vector<bool> taken(slots.size(), false);
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
      o.faceTo = facing;
      o.forced = cmd.forced;
      if (auto* c = reg_.tryGet<CCombat>(e)) c->target = kNullEntity;
      reg_.get<CMover>(e).repaths = 0;
      reg_.get<CMover>(e).failures = 0;
      reg_.get<CMover>(e).retryTick = 0;

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
    mix(u.type | (uint64_t{u.owner} << 16) | (uint64_t(uint32_t(u.health)) << 32) | (uint64_t(u.stance) << 24));
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

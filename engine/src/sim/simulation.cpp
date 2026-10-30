#include "slave/sim/simulation.hpp"

#include <algorithm>

namespace slave {

Simulation::Simulation(const Map& map, const UnitTypeRegistry& types, uint64_t seed)
    : map_(map), types_(types), rng_(seed) {}

Vec2i Simulation::clampToMap(Vec2i p, int32_t margin) const {
  return {std::clamp(p.x, margin, map_.widthLeptons() - 1 - margin),
          std::clamp(p.y, margin, map_.heightLeptons() - 1 - margin)};
}

Entity Simulation::spawnUnit(uint16_t type, uint8_t player, Vec2i pos, Angle facing) {
  const UnitTypeDef& def = types_.get(type);
  pos = clampToMap(pos, def.radius);
  const Entity e = reg_.create();
  reg_.add<CPosition>(e, {pos.x, pos.y});
  reg_.add<CPrevPosition>(e, {pos.x, pos.y});
  reg_.add<CFacing>(e, {angleOfFacing(facingOf(facing)), 0});
  reg_.add<CUnit>(e, {type, player, def.maxHealth});
  reg_.add<CMoveOrder>(e, {});
  reg_.add<CAnim>(e, {});
  return e;
}

void Simulation::tick() {
  // 1. Commands queued since the last tick, in arrival order.
  std::vector<Command> cmds;
  cmds.swap(pending_);
  for (const Command& c : cmds) applyCommand(c);

  // 2. Movement.
  stepMovement();

  ++tick_;
}

void Simulation::applyCommand(const Command& cmd) {
  switch (cmd.type) {
    case CommandType::Move:
      applyMove(cmd);
      break;
    case CommandType::Stop:
      for (Entity e : cmd.units)
        if (auto* m = reg_.tryGet<CMoveOrder>(e)) m->active = false;
      break;
    case CommandType::Spawn: {
      if (cmd.unitType >= types_.size()) break;
      // Spawn in a square block centred on the target, one unit per slot.
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
      for (Entity e : cmd.units) reg_.destroy(e);
      break;
  }
}

// Group move: units keep a tidy square formation around the target point.
// Each unit takes the formation slot nearest to it, so groups don't cross over.
void Simulation::applyMove(const Command& cmd) {
  std::vector<Entity> units;
  units.reserve(cmd.units.size());
  for (Entity e : cmd.units)
    if (reg_.alive(e) && reg_.has<CMoveOrder>(e)) units.push_back(e);
  if (units.empty()) return;

  int32_t spacing = 128;
  for (Entity e : units)
    spacing = std::max(spacing, types_.get(reg_.get<CUnit>(e).type).radius * 2);

  const int32_t n = static_cast<int32_t>(units.size());
  int32_t side = 1;
  while (side * side < n) ++side;

  // Formation slots, nearest to the target first.
  struct Slot { Vec2i p; bool taken; };
  std::vector<Slot> slots;
  slots.reserve(static_cast<size_t>(side) * side);
  for (int32_t gy = 0; gy < side; ++gy)
    for (int32_t gx = 0; gx < side; ++gx)
      slots.push_back({clampToMap({cmd.target.x + (gx * 2 - (side - 1)) * spacing / 2,
                                   cmd.target.y + (gy * 2 - (side - 1)) * spacing / 2},
                                  64),
                       false});
  std::stable_sort(slots.begin(), slots.end(), [&](const Slot& a, const Slot& b) {
    return distance(a.p.x - cmd.target.x, a.p.y - cmd.target.y) <
           distance(b.p.x - cmd.target.x, b.p.y - cmd.target.y);
  });

  // Units farthest from the target choose first (they have the longest trip).
  std::stable_sort(units.begin(), units.end(), [&](Entity a, Entity b) {
    const auto& pa = reg_.get<CPosition>(a);
    const auto& pb = reg_.get<CPosition>(b);
    return distance(pa.x - cmd.target.x, pa.y - cmd.target.y) >
           distance(pb.x - cmd.target.x, pb.y - cmd.target.y);
  });

  // Huge groups (live-editor armies): nearest-slot matching is O(n^2), so
  // instead match units to slots in the same row-major order.
  if (n > 400) {
    auto rowMajor = [&](Vec2i a, Vec2i b) { return a.y != b.y ? a.y < b.y : a.x < b.x; };
    std::stable_sort(units.begin(), units.end(), [&](Entity a, Entity b) {
      const auto& pa = reg_.get<CPosition>(a);
      const auto& pb = reg_.get<CPosition>(b);
      return rowMajor({pa.x, pa.y}, {pb.x, pb.y});
    });
    slots.resize(static_cast<size_t>(n));
    std::stable_sort(slots.begin(), slots.end(),
                     [&](const Slot& a, const Slot& b) { return rowMajor(a.p, b.p); });
    for (size_t i = 0; i < units.size(); ++i) {
      auto& m = reg_.get<CMoveOrder>(units[i]);
      m.tx = slots[i].p.x;
      m.ty = slots[i].p.y;
      m.active = true;
    }
    return;
  }

  for (Entity e : units) {
    const auto& p = reg_.get<CPosition>(e);
    size_t best = 0;
    uint32_t bestDist = UINT32_MAX;
    for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
      if (slots[i].taken) continue;
      const uint32_t d = distance(slots[i].p.x - p.x, slots[i].p.y - p.y);
      if (d < bestDist) {
        bestDist = d;
        best = i;
      }
    }
    slots[best].taken = true;
    auto& m = reg_.get<CMoveOrder>(e);
    m.tx = slots[best].p.x;
    m.ty = slots[best].p.y;
    m.active = true;
  }
}

void Simulation::stepMovement() {
  reg_.each<CMoveOrder, CPosition, CPrevPosition, CFacing, CUnit, CAnim>(
      [&](uint32_t, CMoveOrder& order, CPosition& pos, CPrevPosition& prev, CFacing& facing,
          CUnit& unit, CAnim& anim) {
        prev.x = pos.x;
        prev.y = pos.y;
        anim.moving = false;
        if (!order.active) return;

        const UnitTypeDef& def = types_.get(unit.type);
        const int32_t dx = order.tx - pos.x;
        const int32_t dy = order.ty - pos.y;
        const uint32_t dist = distance(dx, dy);
        if (dist <= static_cast<uint32_t>(def.speed)) {
          pos.x = order.tx;
          pos.y = order.ty;
          order.active = false;
          anim.moving = dist > 0;
          return;
        }

        // Turn in visible 1/16 steps before driving, like the classics.
        const Angle desired = atan2Angle(dy, dx);
        const int32_t wantFacing = facingOf(desired);
        const int32_t haveFacing = facingOf(facing.angle);
        if (wantFacing != haveFacing) {
          if (def.turnTicks == 0) {
            facing.angle = angleOfFacing(wantFacing);
          } else {
            if (facing.turnWait > 0) {
              --facing.turnWait;
              return;
            }
            const int32_t delta = angleDelta(facing.angle, desired);
            facing.angle = angleOfFacing(haveFacing + (delta > 0 ? 1 : -1));
            facing.turnWait = static_cast<uint8_t>(def.turnTicks - 1);
            if (facingOf(facing.angle) != wantFacing) return;
          }
        }

        // Drive straight at the target, unless the next tile is a cliff or deep
        // water for this kind of unit. (Pathfinding around obstacles: Milestone 3.)
        const int32_t nx = pos.x + static_cast<int32_t>(static_cast<int64_t>(dx) * def.speed / dist);
        const int32_t ny = pos.y + static_cast<int32_t>(static_cast<int64_t>(dy) * def.speed / dist);
        const int ctx = pos.x >> LEPTON_SHIFT, cty = pos.y >> LEPTON_SHIFT;
        const int ntx = nx >> LEPTON_SHIFT, nty = ny >> LEPTON_SHIFT;
        if ((ntx != ctx || nty != cty) && !map_.passable(ntx, nty, def.locomotor)) {
          order.active = false;
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
  reg_.each<CUnit, CPosition, CFacing>([&](uint32_t e, CUnit& u, CPosition& p, CFacing& f) {
    mix(e);
    mix(u.type | (uint64_t{u.owner} << 16) | (uint64_t(uint32_t(u.health)) << 32));
    mix(uint64_t(uint32_t(p.x)) | (uint64_t(uint32_t(p.y)) << 32));
    mix(f.angle);
  });
  return h;
}

}  // namespace slave

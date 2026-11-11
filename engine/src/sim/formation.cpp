// Slave Engine - formations (Milestone 9.5).
//
// A group ordered to move marches as one body. The order lays out a slot for
// every unit in the chosen shape (armour in front, wheels behind it, men at
// the back), finds one route for the centre of that shape and creates a
// CFormation: a frame that travels along the route at the pace of its slowest
// member, turning with the route. Each tick every member heads for its slot
// in the moving frame, so the shape holds on the way instead of only at the
// end. The frame waits when members fall behind, squeezes its slots together
// where the ground is too narrow and closes with enemies near its route as
// a body. On an attack-move it halts when it makes contact and the ranks
// behind come up into the firing line; if the fight drags on it presses on
// slowly towards where it was sent, those in contact firing as it goes. When it arrives it turns
// to its final front and lets its members go to their final slots.
//
// Everything is integer maths in a fixed order, like the rest of the
// simulation, so replays and saved games stay exact.
#include <algorithm>

#include "slave/sim/simulation.hpp"

namespace slave {

namespace {
constexpr int32_t kT = LEPTONS_PER_TILE;
constexpr int32_t kJoin = 6 * kT;    // this near its slot when ordered: the body waits for it from the start
constexpr int32_t kRejoin = 2 * kT;  // a straggler this close has caught up
constexpr int32_t kDrop = 7 * kT;    // this far behind, a member no longer holds the body up
constexpr int32_t kTight = kT;       // lag up to here: full pace
constexpr int32_t kWait = 3 * kT;    // lag from here: the body stands and waits
constexpr int32_t kWheel = 40;       // most the frame turns in a tick (4096 = a full turn)
constexpr int32_t kBlend = 3 * kT;   // this near the end, the frame turns to its final front
constexpr int32_t kPursue = 8 * kT;  // attack-move: how far off its route a formation goes after an enemy
constexpr uint16_t kStall = 6 * TICKS_PER_SECOND;  // waiting this long, the laggards are left to catch up
constexpr int32_t kReversePace = 60;               // percent, as in movement.cpp
constexpr uint16_t kStandFight = 6 * TICKS_PER_SECOND;  // in contact this long, the body presses on while it fights
constexpr int32_t kPressPace = 40;                 // percent of its pace while pressing on
constexpr int32_t kSettled = 64;                   // this near its final slot when the march ends, a unit stays put
constexpr int32_t kLineWidth = 40;                 // most units abreast in a line that wasn't dragged out
constexpr int32_t kEdge = 96;                      // a formation keeps this far inside the map's edge

// A slot offset turned into the world: x across the front (right of `facing`
// is positive), y backwards.
Vec2i rotate(Vec2i local, Angle facing) {
  const int64_t fx = cosQ16(facing), fy = sinQ16(facing);
  return {static_cast<int32_t>((-fy * local.x - fx * local.y) / 65536),
          static_cast<int32_t>((fx * local.x - fy * local.y) / 65536)};
}

// Formation slots in the formation's own frame: x across the front, y
// backwards from the front rank. Leptons.
std::vector<Vec2i> formationShape(Formation f, int32_t n, int32_t spacing, int32_t depth, int32_t width) {
  std::vector<Vec2i> out;
  out.reserve(static_cast<size_t>(n));
  if (f == Formation::Loose) {
    spacing = spacing * 9 / 5;
    depth = depth * 9 / 5;
  }
  if (f == Formation::Wedge) {
    if (n <= 9) {
      // A chevron: the point leads and the arms trail back on both sides.
      out.push_back({0, 0});
      for (int32_t i = 1; static_cast<int32_t>(out.size()) < n; ++i) {
        out.push_back({-i * spacing, i * depth * 4 / 5});
        if (static_cast<int32_t>(out.size()) < n) out.push_back({i * spacing, i * depth * 4 / 5});
      }
      return out;
    }
    // Bigger: a filled arrowhead, row r holding 2r+1 units (no wider than a line would be).
    for (int32_t r = 0; static_cast<int32_t>(out.size()) < n; ++r) {
      const int32_t half = std::min(r, kLineWidth / 2);
      for (int32_t i = -half; i <= half && static_cast<int32_t>(out.size()) < n; ++i)
        out.push_back({i * spacing, r * depth * 4 / 5});
    }
    return out;
  }
  int32_t cols = 1;
  switch (f) {
    case Formation::Line: {  // one rank for a handful, two for a platoon, three beyond
      const int32_t ranks = n <= 6 ? 1 : n <= 16 ? 2 : 3;
      cols = std::min<int32_t>((n + ranks - 1) / ranks, kLineWidth);  // a very big body gets more ranks instead
      break;
    }
    case Formation::Column: cols = std::min<int32_t>(2, n); break;
    default: while (cols * cols < n) ++cols; break;
  }
  if (width > 0 && f != Formation::Column)
    cols = std::clamp<int32_t>(width / spacing + 1, 1, n);  // a dragged front line sets the width
  const int32_t rows = (n + cols - 1) / cols;
  for (int32_t r = 0; r < rows; ++r) {
    const int32_t inRow = std::min(cols, n - r * cols);  // last rank centred
    for (int32_t c = 0; c < inRow; ++c) out.push_back({(c * 2 - (inRow - 1)) * spacing / 2, r * depth});
  }
  return out;
}

// A point on the formation's route `along` leptons ahead of its centre
// (behind it when negative), and which way the route runs there. Behind the
// centre is the way it came; before the start and past the end the route is
// taken to run straight on.
Vec2i routePoint(const CFormation& F, int32_t along, Angle& dir) {
  Vec2i p = F.anchor;
  dir = F.facing;
  int32_t left = std::abs(along);
  if (along >= 0) {
    for (size_t i = F.index; i < F.path.size() && left > 0; ++i) {
      const int32_t dx = F.path[i].x - p.x, dy = F.path[i].y - p.y;
      const int32_t d = static_cast<int32_t>(distance(dx, dy));
      if (d == 0) continue;
      dir = atan2Angle(dy, dx);
      if (d >= left)
        return {p.x + static_cast<int32_t>(static_cast<int64_t>(dx) * left / d),
                p.y + static_cast<int32_t>(static_cast<int64_t>(dy) * left / d)};
      left -= d;
      p = F.path[i];
    }
    return {p.x + static_cast<int32_t>(static_cast<int64_t>(cosQ16(dir)) * left / 65536),
            p.y + static_cast<int32_t>(static_cast<int64_t>(sinQ16(dir)) * left / 65536)};
  }
  size_t i = std::min<size_t>(F.index, F.path.size());
  bool fromStart = false;
  while (left > 0) {
    Vec2i q;
    if (i > 0) q = F.path[--i];
    else if (!fromStart) {
      q = F.start;
      fromStart = true;
    } else break;
    const int32_t dx = p.x - q.x, dy = p.y - q.y;
    const int32_t d = static_cast<int32_t>(distance(dx, dy));
    if (d == 0) continue;
    dir = atan2Angle(dy, dx);
    if (d >= left)
      return {p.x - static_cast<int32_t>(static_cast<int64_t>(dx) * left / d),
              p.y - static_cast<int32_t>(static_cast<int64_t>(dy) * left / d)};
    left -= d;
    p = q;
  }
  return {p.x - static_cast<int32_t>(static_cast<int64_t>(cosQ16(dir)) * left / 65536),
          p.y - static_cast<int32_t>(static_cast<int64_t>(sinQ16(dir)) * left / 65536)};
}
}  // namespace

// Where a formation ordered to `target` really goes: moved in from the edge
// of the map just far enough for every slot to be on it.
Vec2i Simulation::fitFormation(const FormationLayout& L, Vec2i target, Angle facing) const {
  int32_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  for (const Vec2i& l : L.local) {
    const Vec2i r = rotate(l, facing);
    x0 = std::min(x0, r.x);
    x1 = std::max(x1, r.x);
    y0 = std::min(y0, r.y);
    y1 = std::max(y1, r.y);
  }
  const int32_t w = map_.widthLeptons(), h = map_.heightLeptons();
  if (x1 - x0 < w - 2 * kEdge) target.x = std::clamp(target.x, kEdge - x0, w - 1 - kEdge - x1);
  if (y1 - y0 < h - 2 * kEdge) target.y = std::clamp(target.y, kEdge - y0, h - 1 - kEdge - y1);
  return target;
}

bool Simulation::layoutFormation(const std::vector<Entity>& units, Vec2i target, Formation f, int32_t formFacing,
                                 int32_t formWidth, FormationLayout& out) const {
  std::array<std::vector<Entity>, 3> groups;
  int64_t cx = 0, cy = 0;
  int32_t count = 0;
  for (Entity e : units)
    if (reg_.alive(e) && reg_.has<CMover>(e) && reg_.has<CUnit>(e) && types_.get(reg_.get<CUnit>(e).type).speed > 0) {
      groups[static_cast<size_t>(types_.get(reg_.get<CUnit>(e).type).locomotor)].push_back(e);
      cx += reg_.get<CPosition>(e).x;
      cy += reg_.get<CPosition>(e).y;
      ++count;
    }
  if (count == 0) return false;
  out.centroid = {static_cast<int32_t>(cx / count), static_cast<int32_t>(cy / count)};

  // Which way the formation faces: as dragged, or the way the group travels.
  if (formFacing >= 0) out.facing = static_cast<Angle>(formFacing & ANGLE_MASK);
  else if (target == out.centroid) out.facing = 0;
  else out.facing = atan2Angle(target.y - out.centroid.y, target.x - out.centroid.x);

  // Front to back: tracked, wheeled, foot. Men stand close; vehicles keep
  // about a hull's width between them.
  const int order3[3] = {static_cast<int>(Locomotor::Tracked), static_cast<int>(Locomotor::Wheeled),
                         static_cast<int>(Locomotor::Foot)};
  int32_t depthSoFar = 0;
  bool first = true;
  for (int li : order3) {
    const std::vector<Entity>& g = groups[static_cast<size_t>(li)];
    if (g.empty()) continue;
    const int32_t n = static_cast<int32_t>(g.size());
    // Men stand a couple of paces apart. Vehicles keep a hull's width
    // between them side by side and more nose to tail, a hull being longer
    // than it is wide.
    int32_t spacing = 150, pitch = 150;
    for (Entity e : g) {
      const int32_t r = types_.get(reg_.get<CUnit>(e).type).radius;
      spacing = std::max(spacing, r * 3);
      pitch = std::max(pitch, li == static_cast<int>(Locomotor::Foot) ? r * 3 : r * 4);
    }
    std::vector<Vec2i> local = formationShape(f, n, spacing, pitch, formWidth);
    int32_t depth = 0;
    for (const Vec2i& l : local) depth = std::max(depth, l.y);
    if (!first) depthSoFar += pitch;
    for (Vec2i& l : local) l.y += depthSoFar;
    depthSoFar += depth;
    first = false;
    out.units.insert(out.units.end(), g.begin(), g.end());
    out.local.insert(out.local.end(), local.begin(), local.end());
    out.blockEnd.push_back(out.local.size());
    out.blockLoco.push_back(static_cast<Locomotor>(li));
    out.blockSpacing.push_back(f == Formation::Loose ? pitch * 9 / 5 : pitch);
  }
  // A dragged front: the first rank stands on the line. Otherwise the whole
  // formation is centred on the spot that was clicked.
  const int32_t shift = formFacing < 0 ? -depthSoFar / 2 : 0;
  for (Vec2i& l : out.local) l.y += shift;
  out.centre = {0, depthSoFar / 2 + shift};
  return true;
}

std::vector<Vec2i> Simulation::formationPreview(const std::vector<Entity>& units, Vec2i target, Formation f,
                                                int32_t formFacing, int32_t formWidth) const {
  FormationLayout L;
  std::vector<Vec2i> out;
  if (!layoutFormation(units, target, f, formFacing, formWidth, L)) return out;
  out.reserve(L.local.size());
  if (L.local.size() > 1) target = fitFormation(L, target, L.facing);
  for (const Vec2i& l : L.local) {
    const Vec2i r = rotate(l, L.facing);
    out.push_back({target.x + r.x, target.y + r.y});
  }
  return out;
}

const CFormation* Simulation::formationOf(Entity unit) const {
  if (!reg_.alive(unit)) return nullptr;
  const CFormationMember* fm = reg_.pool<CFormationMember>().tryGet(unit.index);
  if (!fm || !fm->formation.valid() || !reg_.alive(fm->formation)) return nullptr;
  return reg_.pool<CFormation>().tryGet(fm->formation.index);
}

Formation Simulation::shapeOf(Entity unit) const {
  if (!reg_.alive(unit)) return Formation::Box;
  const CFormationMember* fm = reg_.pool<CFormationMember>().tryGet(unit.index);
  return fm ? static_cast<Formation>(fm->shape % static_cast<uint8_t>(Formation::Count)) : Formation::Box;
}

// Group move: lays the group out, finds the route of its centre and sets the
// formation marching. A unit on its own just goes.
void Simulation::orderMove(const std::vector<Entity>& units, Vec2i target, OrderType type, const Command& cmd) {
  FormationLayout L;
  if (!layoutFormation(units, target, cmd.formation, cmd.formFacing, cmd.formWidth, L)) return;
  const size_t n = L.units.size();
  auto& fms = reg_.pool<CFormationMember>();
  auto place = [](Vec2i c, Vec2i local, Angle a) {
    const Vec2i r = rotate(local, a);
    return Vec2i{c.x + r.x, c.y + r.y};
  };
  auto resetMover = [&](Entity e) {
    CMover& m = reg_.get<CMover>(e);
    m.path.clear();
    m.index = 0;
    m.active = false;
    m.request = 0;
    m.repaths = 0;
    m.failures = 0;
    m.retryTick = 0;
    m.bestDist = INT32_MAX;
    m.stuckTicks = 0;
    if (auto* c = reg_.tryGet<CCombat>(e)) c->target = kNullEntity;
  };

  if (n == 1) {
    const Entity e = L.units.front();
    const UnitTypeDef& def = types_.get(reg_.get<CUnit>(e).type);
    const CPosition& p = reg_.get<CPosition>(e);
    const Vec2i pos{p.x, p.y};
    const Vec2i slot = pathfinder_.nearestReachable(pos, target, def.locomotor);
    COrder& o = reg_.get<COrder>(e);
    o = COrder{};
    o.type = type;
    o.goal = slot;
    // A vehicle backing up a short way keeps the facing it has (its front to the enemy).
    o.faceTo = (cmd.formFacing < 0 && wouldReverse(e, slot)) ? -1 : static_cast<int32_t>(L.facing);
    o.forced = cmd.forced;
    resetMover(e);
    if (CFormationMember* fm = fms.tryGet(e.index)) {
      fm->formation = kNullEntity;
      fm->flags = 0;
      fm->shape = static_cast<uint8_t>(cmd.formation);
    }
    if (pathfinder_.lineClear(pos, slot, def.locomotor)) setPath(e, {slot});
    else requestPath(e, slot);
    return;
  }

  // The route is found for the choosiest way of moving in the group.
  bool has[3] = {false, false, false};
  for (Locomotor l : L.blockLoco) has[static_cast<size_t>(l)] = true;
  const Locomotor routeLoco = has[static_cast<size_t>(Locomotor::Wheeled)]   ? Locomotor::Wheeled
                              : has[static_cast<size_t>(Locomotor::Tracked)] ? Locomotor::Tracked
                                                                             : Locomotor::Foot;
  // The frame starts on the unit nearest the group's centre (of that kind).
  Entity leader = L.units.front();
  uint32_t bestD = UINT32_MAX;
  for (Entity e : L.units) {
    if (types_.get(reg_.get<CUnit>(e).type).locomotor != routeLoco) continue;
    const CPosition& p = reg_.get<CPosition>(e);
    const uint32_t d = distance(p.x - L.centroid.x, p.y - L.centroid.y);
    if (d < bestD) {
      bestD = d;
      leader = e;
    }
  }
  Vec2i lpos{reg_.get<CPosition>(leader).x, reg_.get<CPosition>(leader).y};

  // Backing up: when every vehicle in the group would reverse this move, the
  // formation keeps the front it has and backs away as a body.
  Angle facing = L.facing;
  bool reverse = false;
  if (cmd.formFacing < 0) {
    int vehicles = 0, backing = 0;
    for (Entity e : L.units) {
      const UnitTypeDef& def = types_.get(reg_.get<CUnit>(e).type);
      if (def.locomotor == Locomotor::Foot || def.turnTicks <= 0) continue;
      ++vehicles;
      const CPosition& p = reg_.get<CPosition>(e);
      if (wouldReverse(e, {p.x + target.x - L.centroid.x, p.y + target.y - L.centroid.y})) ++backing;
    }
    reverse = vehicles > 0 && backing == vehicles;
    if (reverse) facing = angleOfFacing(facingOf(reg_.get<CFacing>(leader).angle));
  }

  target = fitFormation(L, target, facing);
  // A group strung out along its way forms on its leading elements: the
  // frame starts far enough forward for its front rank to be level with
  // whoever is furthest on, so nobody is called back to fall in and the
  // rest catch up. (A group standing together forms where it stands.)
  if (!reverse && target != L.centroid) {
    const Angle way = atan2Angle(target.y - L.centroid.y, target.x - L.centroid.x);
    const int64_t wx = cosQ16(way), wy = sinQ16(way);
    int64_t lead = 0;
    for (Entity e : L.units) {
      const CPosition& p = reg_.get<CPosition>(e);
      lead = std::max(lead, (static_cast<int64_t>(p.x - L.centroid.x) * wx + static_cast<int64_t>(p.y - L.centroid.y) * wy) / 65536);
    }
    int32_t front = INT32_MAX;
    for (const Vec2i& l : L.local) front = std::min(front, l.y);
    const int64_t forward = lead - (L.centre.y - front);
    if (forward > 2 * kT) {
      const Vec2i on{L.centroid.x + static_cast<int32_t>(wx * forward / 65536), L.centroid.y + static_cast<int32_t>(wy * forward / 65536)};
      lpos = pathfinder_.nearestReachable(lpos, on, routeLoco);
    }
  }
  const Vec2i dest = pathfinder_.nearestReachable(lpos, place(target, L.centre, facing), routeLoco);
  std::vector<Vec2i> route;
  pathfinder_.findPath(lpos, dest, routeLoco, route, 400000);
  ++stats_.pathSearches;
  stats_.pathExpansions += pathfinder_.expansionsLastCall();

  // The frame starts facing the way it sets off.
  Angle facing0 = facing;
  if (!reverse)
    for (const Vec2i& wp : route)
      if (distance(wp.x - lpos.x, wp.y - lpos.y) > 96) {
        facing0 = atan2Angle(wp.y - lpos.y, wp.x - lpos.x);
        break;
      }

  // Who takes which slot: matched in the starting frame, block by block, so
  // nobody has to cross the formation to fall in.
  std::vector<Vec2i> start(n);
  for (size_t i = 0; i < n; ++i) start[i] = place(lpos, {L.local[i].x - L.centre.x, L.local[i].y - L.centre.y}, facing0);
  std::vector<size_t> slotOf(n);
  const int64_t fx0 = cosQ16(facing0), fy0 = sinQ16(facing0);
  size_t b0 = 0;
  for (size_t b = 0; b < L.blockEnd.size(); ++b) {
    const size_t b1 = L.blockEnd[b];
    std::vector<size_t> order(b1 - b0);
    for (size_t i = 0; i < order.size(); ++i) order[i] = b0 + i;
    if (order.size() > 300) {
      // Big blocks: rank and file. Units sorted by where they stand in the
      // frame take the slots in the same order.
      const int32_t spacing = std::max(1, L.blockSpacing[b]);
      auto key = [&](Vec2i w) {  // (row from the front, column from the left)
        const int64_t ax = (static_cast<int64_t>(w.x - lpos.x) * -fy0 + static_cast<int64_t>(w.y - lpos.y) * fx0) / 65536;
        const int64_t ay = -(static_cast<int64_t>(w.x - lpos.x) * fx0 + static_cast<int64_t>(w.y - lpos.y) * fy0) / 65536;
        return std::pair<int64_t, int64_t>{ay / spacing, ax};
      };
      std::vector<size_t> slots = order;
      std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t c) {
        const CPosition &pa = reg_.get<CPosition>(L.units[a]), &pc = reg_.get<CPosition>(L.units[c]);
        return key({pa.x, pa.y}) < key({pc.x, pc.y});
      });
      std::stable_sort(slots.begin(), slots.end(), [&](size_t a, size_t c) {
        return std::pair<int64_t, int64_t>{L.local[a].y / spacing, L.local[a].x} <
               std::pair<int64_t, int64_t>{L.local[c].y / spacing, L.local[c].x};
      });
      for (size_t i = 0; i < order.size(); ++i) slotOf[order[i]] = slots[i];
    } else {
      // Small blocks: the unit farthest out picks first, each the nearest free slot.
      std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t c) {
        const CPosition &pa = reg_.get<CPosition>(L.units[a]), &pc = reg_.get<CPosition>(L.units[c]);
        return distance(pa.x - lpos.x, pa.y - lpos.y) > distance(pc.x - lpos.x, pc.y - lpos.y);
      });
      std::vector<bool> taken(b1 - b0, false);
      for (size_t u : order) {
        const CPosition& p = reg_.get<CPosition>(L.units[u]);
        size_t best = 0;
        uint32_t bd = UINT32_MAX;
        for (size_t s = b0; s < b1; ++s) {
          if (taken[s - b0]) continue;
          const uint32_t d = distance(start[s].x - p.x, start[s].y - p.y);
          if (d < bd) {
            bd = d;
            best = s;
          }
        }
        taken[best - b0] = true;
        slotOf[u] = best;
      }
    }
    b0 = b1;
  }

  const Entity fe = reg_.create();
  CFormation F;
  F.owner = reg_.get<CUnit>(leader).owner;
  F.shape = static_cast<uint8_t>(cmd.formation);
  F.loco = static_cast<uint8_t>(routeLoco);
  F.reverse = reverse;
  F.path = std::move(route);
  F.start = lpos;
  F.anchor = lpos;
  F.dest = dest;
  F.leash = lpos;
  F.facing = facing0;
  F.finalFacing = facing;
  F.members.reserve(n);
  b0 = 0;
  size_t block = 0;
  for (size_t i = 0; i < n; ++i) {
    while (i >= L.blockEnd[block]) ++block;
    const Entity e = L.units[i];
    const size_t s = slotOf[i];
    const CPosition& p = reg_.get<CPosition>(e);
    const Vec2i goal = pathfinder_.nearestReachable({p.x, p.y}, place(target, L.local[s], facing), L.blockLoco[block]);
    COrder& o = reg_.get<COrder>(e);
    o = COrder{};
    o.type = type;
    o.goal = goal;
    o.faceTo = facing;
    o.forced = cmd.forced;
    resetMover(e);
    CFormationMember fm;
    fm.formation = fe;
    fm.offset = {L.local[s].x - L.centre.x, L.local[s].y - L.centre.y};
    fm.goal = goal;
    fm.shape = static_cast<uint8_t>(cmd.formation);
    fm.heading = facing0;
    fm.flags = distance(start[s].x - p.x, start[s].y - p.y) <= static_cast<uint32_t>(kJoin) ? CFormationMember::kJoined : 0;
    fms.add(e.index, fm);
    F.members.push_back(e);
  }
  reg_.add<CFormation>(fe, std::move(F));
}

// The march is over (or called off): every member goes to its own final slot.
void Simulation::releaseFormation(CFormation& f) {
  auto& fms = reg_.pool<CFormationMember>();
  for (Entity e : f.members) {
    if (!reg_.alive(e)) continue;
    CFormationMember* fm = fms.tryGet(e.index);
    if (!fm) continue;
    fm->formation = kNullEntity;
    fm->flags = 0;
    const COrder& o = reg_.get<COrder>(e);
    if ((o.type != OrderType::Move && o.type != OrderType::AttackMove) || o.goal != fm->goal) continue;
    const CPosition& p = reg_.get<CPosition>(e);
    const Locomotor loco = types_.get(reg_.get<CUnit>(e).type).locomotor;
    CMover& m = reg_.get<CMover>(e);
    if (m.request) continue;  // already finding its own way there
    if (distance(o.goal.x - p.x, o.goal.y - p.y) <= kSettled) {
      m.active = false;  // as good as on its slot: no shuffling about at the end
      m.path.clear();
    } else if (pathfinder_.lineClear({p.x, p.y}, o.goal, loco)) {
      setPath(e, {o.goal});
    } else {
      requestPath(e, o.goal);
    }
  }
  f.members.clear();
}

void Simulation::stepFormations() {
  auto& forms = reg_.pool<CFormation>();
  if (forms.size() == 0) return;
  auto& fms = reg_.pool<CFormationMember>();
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  auto& orders = reg_.pool<COrder>();
  auto& movers = reg_.pool<CMover>();
  auto& combats = reg_.pool<CCombat>();
  auto& garrisoned = reg_.pool<CGarrisoned>();
  std::vector<Entity> finished;
  std::vector<Vec2i> slots;

  const std::vector<uint32_t> list(forms.entities().begin(), forms.entities().end());
  for (uint32_t fi : list) {
    CFormation& F = forms.get(fi);
    const Entity self = reg_.handle(fi);
    const Locomotor routeLoco = static_cast<Locomotor>(F.loco);

    // Members that died, were given other orders or went indoors have left.
    size_t keep = 0;
    for (Entity e : F.members) {
      if (!reg_.alive(e) || !units.has(e.index)) continue;
      CFormationMember* fm = fms.tryGet(e.index);
      if (!fm || fm->formation != self) continue;
      const COrder& o = orders.get(e.index);
      if ((o.type != OrderType::Move && o.type != OrderType::AttackMove) || o.goal != fm->goal ||
          units.get(e.index).health <= 0 || garrisoned.has(e.index)) {
        fm->formation = kNullEntity;
        fm->flags = 0;
        continue;
      }
      F.members[keep++] = e;
    }
    F.members.resize(keep);
    if (F.members.empty()) {
      finished.push_back(self);
      continue;
    }

    // Where each member's slot is now. A slot on ground its unit can't stand
    // on is pulled in towards the centre: the formation narrows for a gap.
    // A column on its route is not a stiff frame: every rank follows the
    // road the head took, so it bends round corners and files over bridges.
    const bool snake = static_cast<Formation>(F.shape) == Formation::Column && !F.reverse && !F.pursuing;
    slots.resize(F.members.size());
    for (size_t i = 0; i < F.members.size(); ++i) {
      const uint32_t e = F.members[i].index;
      const Locomotor loco = types_.get(units.get(e).type).locomotor;
      CFormationMember& fm = fms.get(e);
      const Vec2i off = fm.offset;
      Vec2i s = F.anchor;
      fm.heading = F.facing;
      if (snake) {
        Angle dir = F.facing;
        const Vec2i on = routePoint(F, -off.y, dir);
        fm.heading = dir;
        s = on;
        for (int shrink = 0; shrink < 3; ++shrink) {
          const Vec2i r = rotate({shrink == 2 ? 0 : off.x >> shrink, 0}, dir);
          const Vec2i w{std::clamp(on.x + r.x, 0, map_.widthLeptons() - 1), std::clamp(on.y + r.y, 0, map_.heightLeptons() - 1)};
          if (map_.passable(w.x >> LEPTON_SHIFT, w.y >> LEPTON_SHIFT, loco)) {
            s = w;
            break;
          }
        }
        if (!map_.passable(s.x >> LEPTON_SHIFT, s.y >> LEPTON_SHIFT, loco)) s = F.anchor;
        slots[i] = s;
        continue;
      }
      for (int shrink = 0; shrink < 3; ++shrink) {
        const Vec2i r = rotate({off.x >> shrink, off.y >> shrink}, F.facing);
        const Vec2i w{std::clamp(F.anchor.x + r.x, 0, map_.widthLeptons() - 1),
                      std::clamp(F.anchor.y + r.y, 0, map_.heightLeptons() - 1)};
        if (map_.passable(w.x >> LEPTON_SHIFT, w.y >> LEPTON_SHIFT, loco)) {
          s = w;
          break;
        }
      }
      slots[i] = s;
    }

    // Fighting (attack-move, or men who were shot at on the march): the body
    // halts while any member has an enemy in range, and closes with one that
    // is near its route but out of reach.
    bool engaged = false, haveChase = false;
    Vec2i chase{0, 0};
    std::vector<uint8_t> busy(F.members.size(), 0);
    for (size_t i = 0; i < F.members.size(); ++i) {
      const uint32_t e = F.members[i].index;
      if (orders.get(e).type != OrderType::AttackMove) continue;
      const CCombat* c = combats.tryGet(e);
      if (!c || !reg_.alive(c->target) || !units.has(c->target.index) || units.get(c->target.index).health <= 0) continue;
      const CPosition& p = pos.get(e);
      const CPosition& tp = pos.get(c->target.index);
      const int32_t d = effectiveDistance(distance(tp.x - p.x, tp.y - p.y), c->target.index);
      if (d <= maxRange(types_.get(units.get(e).type))) {
        engaged = true;
        busy[i] = 1;
      } else if (!haveChase) {
        const Vec2i from = F.pursuing ? F.leash : F.anchor;
        if (distance(tp.x - from.x, tp.y - from.y) <= static_cast<uint32_t>(kPursue)) {
          haveChase = true;
          chase = {tp.x, tp.y};
        }
      }
    }

    // In a fight the ranks behind don't stand and watch: every armed member
    // with nothing in reach closes with the enemy on its own (its own target,
    // or where the others are shooting) until it can fire too. When the
    // fight is over they fall in again and the body goes on.
    F.fight = engaged ? static_cast<uint16_t>(std::min<int32_t>(F.fight + 1, 60000)) : static_cast<uint16_t>(std::max<int32_t>(0, F.fight - 2));
    const bool pressing = engaged && F.fight > kStandFight;
    if (engaged && !pressing) {
      int64_t cx = 0, cy = 0, n = 0;
      for (size_t i = 0; i < F.members.size(); ++i) {
        if (!busy[i]) continue;
        const CPosition& tp = pos.get(combats.get(F.members[i].index).target.index);
        cx += tp.x;
        cy += tp.y;
        ++n;
      }
      const Vec2i contact{static_cast<int32_t>(cx / n), static_cast<int32_t>(cy / n)};
      for (size_t i = 0; i < F.members.size(); ++i) {
        const uint32_t e = F.members[i].index;
        if (busy[i] || orders.get(e).type != OrderType::AttackMove) continue;
        const UnitTypeDef& def = types_.get(units.get(e).type);
        if (def.weapons.empty()) continue;  // lorries and engineers keep their place
        Vec2i to = contact;
        if (const CCombat* c = combats.tryGet(e))
          if (reg_.alive(c->target) && units.has(c->target.index) && units.get(c->target.index).health > 0) {
            const CPosition& tp = pos.get(c->target.index);
            to = {tp.x, tp.y};
          }
        // Not past the body's leash: nobody runs off across the map after a scout car.
        if (distance(to.x - F.anchor.x, to.y - F.anchor.y) > static_cast<uint32_t>(kPursue + 6 * kT)) continue;
        slots[i] = pathfinder_.nearestReachable({pos.get(e).x, pos.get(e).y}, to, def.locomotor);
      }
    }

    // Pace and cohesion.
    int32_t pace = INT32_MAX, paceAll = INT32_MAX, maxLag = 0;
    for (size_t i = 0; i < F.members.size(); ++i) {
      const uint32_t e = F.members[i].index;
      const UnitTypeDef& def = types_.get(units.get(e).type);
      CFormationMember& fm = fms.get(e);
      const CPosition& p = pos.get(e);
      const int32_t lag = static_cast<int32_t>(distance(slots[i].x - p.x, slots[i].y - p.y));
      if (!(fm.flags & CFormationMember::kJoined) && lag <= kRejoin) fm.flags |= CFormationMember::kJoined;
      else if ((fm.flags & CFormationMember::kJoined) && lag > kDrop) fm.flags &= static_cast<uint8_t>(~CFormationMember::kJoined);
      int32_t sp = def.speed * map_.speedPercent(p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT, def.locomotor) / 100;
      if (F.reverse && def.locomotor != Locomotor::Foot && def.turnTicks > 0) sp = sp * kReversePace / 100;
      sp = std::max(1, sp);
      paceAll = std::min(paceAll, sp);
      if ((fm.flags & CFormationMember::kJoined) && !(pressing && busy[i])) {  // (those in the fight don't hold the body back)
        pace = std::min(pace, sp);
        maxLag = std::max(maxLag, lag);
      }
    }
    if (pace == INT32_MAX) pace = paceAll;  // nobody has fallen in yet: set off, they catch up
    const int32_t factor = maxLag <= kTight ? 100 : maxLag >= kWait ? 0 : (kWait - maxLag) * 100 / (kWait - kTight);
    const bool standing = engaged && !pressing;
    int32_t step = standing ? 0 : pace * factor / 100 * (pressing ? kPressPace : 100) / 100;
    if (!standing && factor > 0) step = std::max(step, std::min(pace, 6));
    if (pressing) haveChase = false;  // it has an enemy already

    const bool atEnd = !F.pursuing && !haveChase && F.index >= F.path.size();
    if (!standing && step == 0) {
      // Waiting for laggards: after a while they are left to catch up.
      if (++F.stall > kStall) {
        F.stall = 0;
        for (size_t i = 0; i < F.members.size(); ++i) {
          const uint32_t e = F.members[i].index;
          const CPosition& p = pos.get(e);
          if (distance(slots[i].x - p.x, slots[i].y - p.y) > static_cast<uint32_t>(kTight))
            fms.get(e).flags &= static_cast<uint8_t>(~CFormationMember::kJoined);
        }
      }
    } else if (!atEnd) {
      F.stall = 0;
    }

    // Move the frame.
    const Vec2i before = F.anchor;
    Angle desired = F.facing;
    if (standing) {
      // stand
    } else if (haveChase) {
      if (!F.pursuing) {
        F.pursuing = true;
        F.leash = F.anchor;
      }
      const int32_t dx = chase.x - F.anchor.x, dy = chase.y - F.anchor.y;
      const int32_t d = static_cast<int32_t>(distance(dx, dy));
      if (d > 0 && step > 0) {
        const Vec2i next{F.anchor.x + static_cast<int32_t>(static_cast<int64_t>(dx) * std::min(step, d) / d),
                         F.anchor.y + static_cast<int32_t>(static_cast<int64_t>(dy) * std::min(step, d) / d)};
        if (map_.passable(next.x >> LEPTON_SHIFT, next.y >> LEPTON_SHIFT, routeLoco)) F.anchor = next;
      }
      if (d > 64 && !F.reverse) desired = atan2Angle(dy, dx);
    } else {
      if (F.pursuing) {
        // The fight is over: back on the way to where it was sent.
        F.pursuing = false;
        F.path.clear();
        F.index = 0;
        F.start = F.anchor;
        pathfinder_.findPath(F.anchor, F.dest, routeLoco, F.path, 200000);
        ++stats_.pathSearches;
        stats_.pathExpansions += pathfinder_.expansionsLastCall();
      }
      int32_t left = step;
      while (left > 0 && F.index < F.path.size()) {
        const Vec2i wp = F.path[F.index];
        const int32_t dx = wp.x - F.anchor.x, dy = wp.y - F.anchor.y;
        const int32_t d = static_cast<int32_t>(distance(dx, dy));
        if (d <= left) {
          F.anchor = wp;
          left -= d;
          ++F.index;
        } else {
          F.anchor.x += static_cast<int32_t>(static_cast<int64_t>(dx) * left / d);
          F.anchor.y += static_cast<int32_t>(static_cast<int64_t>(dy) * left / d);
          left = 0;
        }
      }
      // Route left, and the way the frame should face.
      int64_t remaining = 0;
      Vec2i from = F.anchor;
      for (size_t i = F.index; i < F.path.size(); ++i) {
        remaining += distance(F.path[i].x - from.x, F.path[i].y - from.y);
        from = F.path[i];
      }
      F.remaining = static_cast<int32_t>(std::min<int64_t>(remaining, INT32_MAX));
      if (F.reverse || F.remaining <= kBlend) {
        desired = F.finalFacing;
      } else {
        for (size_t i = F.index; i < F.path.size(); ++i)
          if (distance(F.path[i].x - F.anchor.x, F.path[i].y - F.anchor.y) > 64) {
            desired = atan2Angle(F.path[i].y - F.anchor.y, F.path[i].x - F.anchor.x);
            break;
          }
      }
    }
    F.moving = F.anchor != before;
    if (!standing) {
      // The frame wheels only as fast as its members keep up.
      const int32_t turn = std::max(factor > 0 ? 6 : 0, kWheel * factor / 100);
      const int32_t delta = angleDelta(F.facing, desired);
      F.facing = wrapAngle(static_cast<int32_t>(F.facing) + std::clamp(delta, -turn, turn));
    }

    // Arrived and turned to its front (or waited long enough for that).
    if (atEnd && !standing) {
      if (snake || std::abs(angleDelta(F.facing, F.finalFacing)) <= 8 || ++F.stall > kStall) {
        releaseFormation(F);
        finished.push_back(self);
        continue;
      }
    }

    // Send every member to its slot. In the open that is a straight line,
    // renewed every tick; round an obstacle it finds a path, and tries the
    // straight line again as soon as it is clear.
    for (size_t i = 0; i < F.members.size(); ++i) {
      if (busy[i]) continue;  // shooting from where it stands
      const Entity e = F.members[i];
      const uint32_t ei = e.index;
      CFormationMember& fm = fms.get(ei);
      CMover& m = movers.get(ei);
      const CPosition& p = pos.get(ei);
      const Locomotor loco = types_.get(units.get(ei).type).locomotor;
      const Vec2i slot = slots[i];
      if (pathfinder_.lineClear({p.x, p.y}, slot, loco)) {
        fm.flags &= static_cast<uint8_t>(~CFormationMember::kDetour);
        m.request = 0;
        m.index = 0;
        m.bestDist = INT32_MAX;
        m.stuckTicks = 0;
        if (p.x == slot.x && p.y == slot.y) {
          m.active = false;
          m.path.clear();
        } else {
          m.path.assign(1, slot);
          m.active = true;
        }
      } else if (!m.request &&
                 (!(fm.flags & CFormationMember::kDetour) || !m.active || (tick_ + ei) % 20 == 0)) {
        fm.flags |= CFormationMember::kDetour;
        requestPath(e, slot);
      }
    }
  }
  for (Entity f : finished) reg_.destroy(f);
}

}  // namespace slave

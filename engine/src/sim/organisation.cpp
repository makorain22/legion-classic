// Slave Engine - how armies organise themselves: fireteams, squads, platoons
// and companies (and armoured platoons, companies and battalions); infantry
// in buildings, bunkers and trenches; and the supplies a depot on a supply
// point earns.
#include <algorithm>

#include "slave/sim/simulation.hpp"

namespace slave {

namespace {
// Size at which each echelon forms, by kind (index = Echelon).
constexpr int32_t kInfantrySize[6] = {0, 5, 10, 20, 100, 1 << 30};
constexpr int32_t kVehicleSize[6] = {0, 1 << 30, 1 << 30, 5, 15, 50};
// Extra firepower (percent) of a bound unit's members.
constexpr int32_t kInfantryBonus[6] = {0, 5, 10, 15, 30, 30};
constexpr int32_t kVehicleBonus[6] = {0, 0, 0, 3, 6, 10};
constexpr int32_t kCombinedBonus[6] = {0, 0, 0, 8, 12, 15};  // combined arms fight best together

constexpr int32_t kBindRadius = 3 * 256;  // a fireteam forms from men this close together
constexpr uint32_t kAutoBindTicks = 15;

Echelon naturalEchelon(bool vehicles, int32_t n) {
  const int32_t* size = vehicles ? kVehicleSize : kInfantrySize;
  Echelon best = Echelon::None;
  for (int e = 1; e <= 5; ++e)
    if (n >= size[e]) best = static_cast<Echelon>(e);
  return best;
}

// The echelon below (the one a unit drops to when worn down).
Echelon lower(bool vehicles, Echelon e) {
  for (int i = static_cast<int>(e) - 1; i >= 1; --i) {
    const int32_t* size = vehicles ? kVehicleSize : kInfantrySize;
    if (size[i] < (1 << 30)) return static_cast<Echelon>(i);
  }
  return Echelon::None;
}
}  // namespace

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------
const CGroup* Simulation::groupOf(Entity unit) const {
  if (!reg_.alive(unit)) return nullptr;
  const CGroupMember* m = reg_.pool<CGroupMember>().tryGet(unit.index);
  if (!m || !reg_.alive(m->group)) return nullptr;
  return reg_.pool<CGroup>().tryGet(m->group.index);
}

int32_t Simulation::groupBonus(Entity shooter) const {
  const CGroup* g = groupOf(shooter);
  if (!g) return 0;
  return (g->combined ? kCombinedBonus : g->vehicles ? kVehicleBonus : kInfantryBonus)[static_cast<int>(g->echelon)];
}

int32_t Simulation::echelonSize(bool vehicles, Echelon e) {
  return (vehicles ? kVehicleSize : kInfantrySize)[static_cast<int>(e)];
}

uint16_t Simulation::nextGroupNumber(uint8_t owner, bool vehicles, Echelon e) {
  uint16_t& n = groupNumbers_[static_cast<size_t>(owner % MAX_PLAYERS) * 2 + (vehicles ? 1 : 0)][static_cast<size_t>(e)];
  return ++n;
}

Entity Simulation::makeGroup(const std::vector<Entity>& members, uint8_t owner, bool vehicles, Echelon e) {
  const Entity g = reg_.create();
  CGroup grp;
  grp.owner = owner;
  grp.vehicles = vehicles;
  grp.echelon = e;
  grp.number = nextGroupNumber(owner, vehicles, e);
  grp.members = members;
  reg_.add<CGroup>(g, std::move(grp));
  for (Entity m : members) {
    if (CGroupMember* old = reg_.pool<CGroupMember>().tryGet(m.index)) old->group = g;
    else reg_.add<CGroupMember>(m, {g});
  }
  SimEvent ev;
  ev.type = EventType::GroupFormed;
  ev.owner = owner;
  ev.amount = static_cast<int32_t>(members.size());
  events_.push_back(ev);
  return g;
}

void Simulation::disband(Entity group) {
  if (!reg_.alive(group)) return;
  if (const CGroup* g = reg_.pool<CGroup>().tryGet(group.index))
    for (Entity m : g->members)
      if (reg_.alive(m) && reg_.has<CGroupMember>(m) && reg_.get<CGroupMember>(m).group == group) reg_.remove<CGroupMember>(m);
  reg_.destroy(group);
}

// How big a battle group counts: a vehicle each, and a vehicle for every four men.
int32_t Simulation::groupStrength(const std::vector<Entity>& members, bool combined) const {
  if (!combined) return static_cast<int32_t>(members.size());
  int32_t veh = 0, men = 0;
  for (Entity m : members)
    if (reg_.alive(m) && reg_.has<CUnit>(m)) (types_.get(reg_.get<CUnit>(m).type).infantry() ? men : veh) += 1;
  return veh + (men + 3) / 4;
}

void Simulation::bindUnits(const std::vector<Entity>& units, uint8_t owner) {
  std::vector<Entity> men, machines;
  for (Entity e : units) {
    if (!reg_.alive(e) || !reg_.has<CUnit>(e)) continue;
    const CUnit& u = reg_.get<CUnit>(e);
    const UnitTypeDef& d = types_.get(u.type);
    if (u.owner != owner || d.isStructure() || d.speed <= 0) continue;
    auto& list = d.infantry() ? men : machines;
    if (std::find(list.begin(), list.end(), e) == list.end()) list.push_back(e);
  }
  auto bind = [&](std::vector<Entity> picked, bool vehicles, bool combined) {
    const Echelon e = naturalEchelon(vehicles, groupStrength(picked, combined));
    if (e == Echelon::None) return false;
    // Whatever groups these came from are absorbed into the new one.
    std::vector<Entity> old;
    for (Entity m : picked)
      if (const CGroupMember* gm = reg_.pool<CGroupMember>().tryGet(m.index))
        if (std::find(old.begin(), old.end(), gm->group) == old.end()) old.push_back(gm->group);
    for (Entity g : old)
      if (CGroup* og = reg_.pool<CGroup>().tryGet(g.index))
        std::erase_if(og->members, [&](Entity m) { return std::find(picked.begin(), picked.end(), m) != picked.end(); });
    std::sort(picked.begin(), picked.end(), [](Entity a, Entity b) { return a.index < b.index; });
    const Entity g = makeGroup(picked, owner, vehicles, e);
    reg_.get<CGroup>(g).combined = combined;
    for (Entity og : old) refreshGroup(og);
    return true;
  };
  // Infantry and vehicles together: one combined-arms battle group.
  if (!men.empty() && !machines.empty()) {
    std::vector<Entity> all = men;
    all.insert(all.end(), machines.begin(), machines.end());
    if (bind(all, true, true)) return;
  }
  if (!men.empty()) bind(men, false, false);
  // Vehicles alone: one card per role (tanks, reconnaissance, artillery...).
  std::vector<std::string> roles;
  for (Entity e : machines) {
    const std::string& r = types_.get(reg_.get<CUnit>(e).type).nato;
    if (std::find(roles.begin(), roles.end(), r) == roles.end()) roles.push_back(r);
  }
  std::sort(roles.begin(), roles.end());
  std::vector<Entity> rest;  // kinds too few to make a unit of their own
  for (const std::string& r : roles) {
    std::vector<Entity> picked;
    for (Entity e : machines)
      if (types_.get(reg_.get<CUnit>(e).type).nato == r) picked.push_back(e);
    if (!bind(picked, true, false)) rest.insert(rest.end(), picked.begin(), picked.end());
  }
  // ...make one mixed unit together if there are enough of them (a raiding
  // party of cars, half-tracks and light tanks, say).
  if (rest.size() > 1) bind(rest, true, false);
}

// Drops the dead and the departed; demotes a unit worn down past its size;
// disbands one with a single man left.
void Simulation::refreshGroup(Entity group) {
  if (!reg_.alive(group)) return;
  CGroup* g = reg_.pool<CGroup>().tryGet(group.index);
  if (!g) return;
  std::erase_if(g->members, [&](Entity m) {
    return !reg_.alive(m) || !reg_.has<CGroupMember>(m) || reg_.get<CGroupMember>(m).group != group ||
           reg_.get<CUnit>(m).health <= 0;
  });
  if (g->members.size() <= 1) {
    disband(group);
    return;
  }
  const int32_t n = groupStrength(g->members, g->combined);
  // A company stays a company until it is down to a platoon's strength, and so on.
  while (g->echelon != Echelon::None) {
    const Echelon below = lower(g->vehicles, g->echelon);
    if (below == Echelon::None || n > echelonSize(g->vehicles, below)) break;
    g->echelon = below;
    g->number = nextGroupNumber(g->owner, g->vehicles, below);
  }
}

void Simulation::stepGroups() {
  // Casualties.
  if (!dead_.empty() || tick_ % kAutoBindTicks == 0) {
    std::vector<Entity> groups;
    for (uint32_t gi : reg_.pool<CGroup>().entities()) groups.push_back(reg_.handle(gi));
    for (Entity g : groups) refreshGroup(g);
  }
  if (tick_ % kAutoBindTicks != 0) return;
  // Fireteams form by themselves: five men of the same kind, unattached,
  // close together.
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  auto& members = reg_.pool<CGroupMember>();
  auto& garrisoned = reg_.pool<CGarrisoned>();
  std::vector<uint32_t> loose;
  for (uint32_t e : units.entities()) {
    const CUnit& u = units.get(e);
    if (u.owner >= MAX_PLAYERS || u.health <= 0 || members.has(e)) continue;
    const UnitTypeDef& d = types_.get(u.type);
    if (!d.infantry() || d.speed <= 0) continue;
    loose.push_back(e);
  }
  if (loose.size() < 5) return;
  std::sort(loose.begin(), loose.end());
  std::vector<uint8_t> taken(reg_.capacity(), 0);
  std::vector<std::pair<uint32_t, uint32_t>> near;  // distance, entity
  for (uint32_t e : loose) {
    if (taken[e] || members.has(e)) continue;
    const CUnit& u = units.get(e);
    const CPosition& p = pos.get(e);
    near.clear();
    spatial_.query(p.x, p.y, kBindRadius, [&](uint32_t o) {
      if (taken[o] || members.has(o) || garrisoned.has(o) != garrisoned.has(e)) return;
      const CUnit& v = units.get(o);
      if (v.owner != u.owner || v.type != u.type || v.health <= 0) return;
      const CPosition& q = pos.get(o);
      const uint32_t d = distance(q.x - p.x, q.y - p.y);
      if (d <= static_cast<uint32_t>(kBindRadius)) near.push_back({d, o});
    });
    if (near.size() < 5) continue;
    std::sort(near.begin(), near.end());
    std::vector<Entity> team;
    for (size_t i = 0; i < 5; ++i) {
      team.push_back(reg_.handle(near[i].second));
      taken[near[i].second] = 1;
    }
    std::sort(team.begin(), team.end(), [](Entity a, Entity b) { return a.index < b.index; });
    makeGroup(team, u.owner, false, Echelon::Fireteam);
  }
}

// ---------------------------------------------------------------------------
// Garrisons: infantry in buildings and bunkers, and manning trench lines
// ---------------------------------------------------------------------------
bool Simulation::canGarrison(Entity unit, Entity building) const {
  if (!reg_.alive(unit) || !reg_.alive(building) || !reg_.has<CUnit>(unit) || !reg_.has<CUnit>(building)) return false;
  const UnitTypeDef& ud = types_.get(reg_.get<CUnit>(unit).type);
  const UnitTypeDef& bd = types_.get(reg_.get<CUnit>(building).type);
  if (!ud.infantry() || bd.garrison <= 0 || reg_.get<CUnit>(building).health <= 0) return false;
  const uint8_t bo = reg_.get<CUnit>(building).owner;
  // Civilian buildings take anyone; military ones only their own side.
  if (bo < MAX_PLAYERS && !allied(bo, reg_.get<CUnit>(unit).owner)) return false;
  const CGarrison* g = reg_.pool<CGarrison>().tryGet(building.index);
  const size_t inside = g ? g->occupants.size() : 0;
  // Only one side's men in a building at a time.
  if (g && !g->occupants.empty() && reg_.alive(g->occupants.front()) &&
      !allied(reg_.get<CUnit>(g->occupants.front()).owner, reg_.get<CUnit>(unit).owner))
    return false;
  return static_cast<int32_t>(inside) < bd.garrison;
}

bool Simulation::isTrench(Entity b) const {
  if (!reg_.alive(b) || !reg_.has<CUnit>(b) || !reg_.has<CStructure>(b)) return false;
  const UnitTypeDef& d = types_.get(reg_.get<CUnit>(b).type);
  return d.category == UnitCategory::Fortification && d.cover > 0 && d.garrison > 0;
}

bool Simulation::inTrench(Entity u) const {
  const CGarrisoned* g = reg_.alive(u) ? reg_.pool<CGarrisoned>().tryGet(u.index) : nullptr;
  return g && isTrench(g->building);
}

int Simulation::fieldworkMask(Entity b) const {
  if (!reg_.alive(b) || !reg_.has<CStructure>(b)) return 0;
  const CStructure& s = reg_.get<CStructure>(b);
  const CUnit& u = reg_.get<CUnit>(b);
  auto same = [&](int x, int y) {
    const Entity o = structureAt(x, y);
    return o.valid() && reg_.get<CUnit>(o).type == u.type && reg_.get<CUnit>(o).owner == u.owner;
  };
  return (same(s.tx + 1, s.ty) ? 1 : 0) | (same(s.tx, s.ty + 1) ? 2 : 0) | (same(s.tx - 1, s.ty) ? 4 : 0) |
         (same(s.tx, s.ty - 1) ? 8 : 0);
}

Vec2i Simulation::fieldworkSlot(Entity b, size_t i) const {
  const CPosition& bp = reg_.get<CPosition>(b);
  const int mask = fieldworkMask(b);
  // Points along the line's arms out from the centre, then the centre, then
  // closer in: the men spread out along the channel or the wall.
  static constexpr int32_t kDir[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  int32_t pts[9][2];
  int n = 0;
  for (int a = 0; a < 4; ++a)
    if (mask & (1 << a)) pts[n][0] = kDir[a][0] * 92, pts[n][1] = kDir[a][1] * 92, ++n;
  pts[n][0] = 0, pts[n][1] = 0, ++n;
  for (int a = 0; a < 4; ++a)
    if (mask & (1 << a)) pts[n][0] = kDir[a][0] * 46, pts[n][1] = kDir[a][1] * 46, ++n;
  if (mask == 0) {  // a lone pit: three men round its middle
    static constexpr int32_t kPit[3][2] = {{-34, -18}, {30, -14}, {0, 32}};
    pts[0][0] = kPit[0][0], pts[0][1] = kPit[0][1];
    pts[1][0] = kPit[1][0], pts[1][1] = kPit[1][1];
    pts[2][0] = kPit[2][0], pts[2][1] = kPit[2][1];
    n = 3;
  }
  const int k = static_cast<int>(i % static_cast<size_t>(std::min(n, 3)));
  int32_t x = pts[k][0], y = pts[k][1];
  // A trench is dug into the ground and cannot be shot at; a sandbag wall
  // stands on it and can.
  if (types_.get(reg_.get<CUnit>(b).type).targetable) {
    // Behind a wall of bags: step back off it, away from the camera, so the
    // bags stand between the men and the front.
    const bool alongX = (mask & 5) != 0 && (x != 0 || !(mask & 10));
    if (alongX) y -= 46;
    else x -= 46;
    if (mask == 0) x -= 20, y -= 20;
  }
  return {bp.x + x, bp.y + y};
}

int Simulation::garrisonLevel(Entity building) const {
  if (!reg_.alive(building) || !reg_.has<CUnit>(building)) return 0;
  const CGarrison* g = reg_.pool<CGarrison>().tryGet(building.index);
  const int32_t cap = types_.get(reg_.get<CUnit>(building).type).garrison;
  if (!g || g->occupants.empty() || cap <= 0) return 0;
  const int32_t n = static_cast<int32_t>(g->occupants.size());
  return n * 3 >= cap * 2 ? 3 : n * 3 >= cap ? 2 : 1;
}

void Simulation::enterBuilding(Entity u, Entity b) {
  if (!canGarrison(u, b)) return;
  if (!reg_.has<CGarrison>(b)) reg_.add<CGarrison>(b, {});
  reg_.get<CGarrison>(b).occupants.push_back(u);
  reg_.add<CGarrisoned>(u, {b});
  setOrderIdle(u);
  const CPosition& bp = reg_.get<CPosition>(b);
  Vec2i at{bp.x, bp.y};
  if (isTrench(b)) at = fieldworkSlot(b, reg_.get<CGarrison>(b).occupants.size() - 1);
  reg_.get<CPosition>(u) = {at.x, at.y};
  reg_.get<CPrevPosition>(u) = {at.x, at.y};
  CMover& m = reg_.get<CMover>(u);
  m.active = false;
  m.request = 0;
}

void Simulation::leaveBuilding(Entity u) {
  if (!reg_.alive(u) || !reg_.has<CGarrisoned>(u)) return;
  const Entity b = reg_.get<CGarrisoned>(u).building;
  reg_.remove<CGarrisoned>(u);
  Vec2i out = reg_.get<CPosition>(u).x ? Vec2i{reg_.get<CPosition>(u).x, reg_.get<CPosition>(u).y} : Vec2i{0, 0};
  if (reg_.alive(b)) {
    if (CGarrison* g = reg_.pool<CGarrison>().tryGet(b.index)) std::erase(g->occupants, u);
    // Out of a trench a man simply climbs out where he is.
    if (!(reg_.has<CStructure>(b) && types_.get(reg_.get<CUnit>(b).type).category == UnitCategory::Fortification)) out = exitOf(b);
  }
  reg_.get<CPosition>(u) = {out.x, out.y};
  reg_.get<CPrevPosition>(u) = {out.x, out.y};
}

// The trench line a tile belongs to: every trench piece joined to it.
std::vector<Vec2i> Simulation::trenchLine(Entity piece) const {
  std::vector<Vec2i> tiles;
  if (!reg_.alive(piece) || !reg_.has<CStructure>(piece)) return tiles;
  const uint16_t type = reg_.get<CUnit>(piece).type;
  const uint8_t owner = reg_.get<CUnit>(piece).owner;
  const CStructure& s = reg_.get<CStructure>(piece);
  std::vector<uint8_t> seen(tileStructure_.size(), 0);
  std::vector<Vec2i> stack{{s.tx, s.ty}};
  while (!stack.empty() && tiles.size() < 400) {
    const Vec2i t = stack.back();
    stack.pop_back();
    if (!map_.inBounds(t.x, t.y)) continue;
    const size_t i = static_cast<size_t>(t.y) * map_.width() + t.x;
    if (seen[i]) continue;
    seen[i] = 1;
    const uint32_t e = tileStructure_[i];
    if (e == UINT32_MAX || !reg_.pool<CUnit>().has(e) || reg_.pool<CUnit>().get(e).type != type ||
        reg_.pool<CUnit>().get(e).owner != owner)
      continue;  // only our own side's pieces of the line
    tiles.push_back(t);
    stack.push_back({t.x + 1, t.y});
    stack.push_back({t.x - 1, t.y});
    stack.push_back({t.x, t.y + 1});
    stack.push_back({t.x, t.y - 1});
  }
  return tiles;
}

void Simulation::applyOccupy(const Command& cmd) {
  const Entity target = cmd.targetUnit;
  if (!reg_.alive(target) || !reg_.has<CUnit>(target)) return;
  const UnitTypeDef& td = types_.get(reg_.get<CUnit>(target).type);
  std::vector<Entity> men;
  for (Entity e : cmd.units)
    if (reg_.alive(e) && reg_.has<CUnit>(e) && types_.get(reg_.get<CUnit>(e).type).infantry() &&
        reg_.get<CUnit>(e).owner == cmd.player)
      men.push_back(e);
  if (men.empty()) return;
  if (isTrench(target)) {
    // Man the trench line: up to three men a piece, nearest pieces first.
    std::vector<Vec2i> tiles = trenchLine(target);
    const CStructure& s = reg_.get<CStructure>(target);
    std::sort(tiles.begin(), tiles.end(), [&](Vec2i a, Vec2i b) {
      const int da = std::abs(a.x - s.tx) + std::abs(a.y - s.ty), db = std::abs(b.x - s.tx) + std::abs(b.y - s.ty);
      return da != db ? da < db : (a.y != b.y ? a.y < b.y : a.x < b.x);
    });
    // Places taken already: men inside and men on their way.
    std::vector<int32_t> taken(tiles.size(), 0);
    std::vector<Entity> pieces(tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i) {
      pieces[i] = reg_.handle(tileStructure_[static_cast<size_t>(tiles[i].y) * map_.width() + tiles[i].x]);
      if (const CGarrison* g = reg_.pool<CGarrison>().tryGet(pieces[i].index)) taken[i] += static_cast<int32_t>(g->occupants.size());
    }
    for (uint32_t e : reg_.pool<COrder>().entities()) {
      const COrder& o = reg_.pool<COrder>().get(e);
      if (o.type != OrderType::Occupy) continue;
      if (std::find(men.begin(), men.end(), reg_.handle(e)) != men.end()) continue;
      for (size_t i = 0; i < pieces.size(); ++i)
        if (o.target == pieces[i]) ++taken[i];
    }
    const int32_t cap = td.garrison;
    for (Entity e : men) {
      if (reg_.has<CGarrisoned>(e) && isTrench(reg_.get<CGarrisoned>(e).building)) continue;  // already in
      size_t pick = tiles.size();
      for (size_t i = 0; i < tiles.size(); ++i)
        if (taken[i] < cap) {
          pick = i;
          break;
        }
      if (pick == tiles.size()) break;  // the line is full
      ++taken[pick];
      leaveBuilding(e);
      setOrderIdle(e);
      COrder& o = reg_.get<COrder>(e);
      o.type = OrderType::Occupy;
      o.target = pieces[pick];
      // Into the trench; behind the wall of bags, to the place he will hold.
      o.chasedAt = td.targetable ? fieldworkSlot(pieces[pick], static_cast<size_t>(taken[pick] - 1))
                                 : Vec2i{tiles[pick].x * 256 + 128, tiles[pick].y * 256 + 128};
      requestPath(e, o.chasedAt);
    }
    return;
  }
  for (Entity e : men) {
    if (!canGarrison(e, target)) continue;
    if (reg_.has<CGarrisoned>(e) && reg_.get<CGarrisoned>(e).building == target) continue;
    leaveBuilding(e);
    setOrderIdle(e);
    COrder& o = reg_.get<COrder>(e);
    o.type = OrderType::Occupy;
    o.target = target;
    const CPosition& tp = reg_.get<CPosition>(target);
    o.chasedAt = {tp.x, tp.y};
    requestPath(e, exitOf(target));
  }
}

void Simulation::applyUnload(const Command& cmd) {
  for (Entity e : cmd.units) {
    if (!reg_.alive(e)) continue;
    if (const CGarrison* g = reg_.pool<CGarrison>().tryGet(e.index)) {
      const std::vector<Entity> out = g->occupants;
      for (Entity u : out) leaveBuilding(u);
    } else if (reg_.has<CGarrisoned>(e)) {
      leaveBuilding(e);
    }
  }
}

void Simulation::stepGarrisons() {
  auto& orders = reg_.pool<COrder>();
  auto& pos = reg_.pool<CPosition>();
  // Men walking to a building go in when they reach it.
  std::vector<std::pair<Entity, Entity>> arrivals;
  for (uint32_t e : orders.entities()) {
    COrder& o = orders.get(e);
    if (o.type != OrderType::Occupy) continue;
    const Entity u = reg_.handle(e);
    if (!canGarrison(u, o.target)) {
      setOrderIdle(u);
      continue;
    }
    const CPosition& p = pos.get(e);
    // Close enough when standing next to the footprint (on it, for a trench).
    const CStructure& st = reg_.get<CStructure>(o.target);
    const int32_t gx = std::max({0, st.tx * 256 - p.x, p.x - (st.tx + st.w) * 256});
    const int32_t gy = std::max({0, st.ty * 256 - p.y, p.y - (st.ty + st.h) * 256});
    const bool trench = isTrench(o.target);
    // A wall of bags is manned from beside it as well as on it.
    const bool bags = trench && types_.get(reg_.get<CUnit>(o.target).type).targetable;
    const bool there = bags ? std::max(gx, gy) <= 176 || distance(p.x - o.chasedAt.x, p.y - o.chasedAt.y) <= 96
                            : std::max(gx, gy) <= (trench ? 96 : 320);
    if (there) {
      arrivals.push_back({u, o.target});
    } else {
      const CMover& m = reg_.get<CMover>(u);
      if (!m.active && !m.request && (tick_ + e) % 30 == 0) requestPath(u, trench ? o.chasedAt : exitOf(o.target));
    }
  }
  for (auto [u, b] : arrivals) enterBuilding(u, b);
  // Occupants who died or buildings that fell.
  std::vector<Entity> evict;
  for (uint32_t b : reg_.pool<CGarrison>().entities()) {
    CGarrison& g = reg_.pool<CGarrison>().get(b);
    std::erase_if(g.occupants, [&](Entity u) { return !reg_.alive(u) || !reg_.has<CGarrisoned>(u); });
  }
  for (uint32_t u : reg_.pool<CGarrisoned>().entities()) {
    const Entity b = reg_.pool<CGarrisoned>().get(u).building;
    if (!reg_.alive(b) || reg_.pool<CUnit>().get(b.index).health <= 0) evict.push_back(reg_.handle(u));
    else if (!isTrench(b)) {
      // Stay put inside.
      const CPosition& bp = pos.get(b.index);
      pos.get(u) = {bp.x, bp.y};
    }
  }
  // Men in a trench keep to its firing places, closing up when one leaves.
  for (uint32_t b : reg_.pool<CGarrison>().entities()) {
    const Entity bh = reg_.handle(b);
    if (!isTrench(bh)) continue;
    const CPosition& bp = pos.get(b);
    const auto& occ = reg_.pool<CGarrison>().get(b).occupants;
    (void)bp;
    for (size_t i = 0; i < occ.size(); ++i)
      if (reg_.alive(occ[i])) {
        const Vec2i at = fieldworkSlot(bh, i);
        pos.get(occ[i].index) = {at.x, at.y};
      }
  }
  for (Entity u : evict) {
    leaveBuilding(u);
    // Caught in the collapse.
    CUnit& cu = reg_.get<CUnit>(u);
    damage(u.index, types_.get(cu.type).maxHealth * 30, kNullEntity);
  }
}

// Damage reaching men inside: half (a little less the more fortified it is).
int32_t Simulation::shelterPercent(uint32_t victim) const {
  const CGarrisoned* g = reg_.pool<CGarrisoned>().tryGet(victim);
  if (!g) return 100;
  if (isTrench(g->building)) return 100 - types_.get(reg_.get<CUnit>(g->building).type).cover;  // dug in
  const int level = garrisonLevel(g->building);
  return level >= 3 ? 40 : level == 2 ? 45 : 50;
}

// ---------------------------------------------------------------------------
// Income from buildings (logistics points and convoys: logistics.cpp)
// ---------------------------------------------------------------------------
void Simulation::stepIncome() {
  if (tick_ % TICKS_PER_SECOND != 0) return;
  std::array<int32_t, MAX_PLAYERS> perMinute{};
  auto& units = reg_.pool<CUnit>();
  for (uint32_t e : reg_.pool<CStructure>().entities()) {
    const CUnit& u = units.get(e);
    const UnitTypeDef& d = types_.get(u.type);
    if (d.income <= 0 || u.owner >= MAX_PLAYERS || u.health <= 0) continue;
    perMinute[u.owner] += d.income;
  }
  for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
    PlayerState& ps = players_[p];
    ps.incomePerMinute = static_cast<int32_t>(static_cast<int64_t>(perMinute[p]) * incomeRate_[p] / 100);
    ps.incomeAcc += ps.incomePerMinute;
    ps.credits += ps.incomeAcc / 60;
    ps.incomeAcc %= 60;
  }
}

}  // namespace slave

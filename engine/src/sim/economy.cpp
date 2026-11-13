// Slave Engine - structures, the economy and production.
//
// Structures sit on the tile grid and mark their tiles in the map's block
// layer, so pathfinding goes around them. Supplies come from income buildings
// and from logistics points (logistics.cpp). Each side has four production
// lines (buildings, defences, infantry, vehicles); work is paid for as it
// progresses and runs at half speed when the side lacks power.
#include <algorithm>

#include "slave/sim/simulation.hpp"

namespace slave {

Queue queueFor(UnitCategory c) {
  switch (c) {
    case UnitCategory::Infantry: return Queue::Infantry;
    case UnitCategory::Vehicle: return Queue::Vehicles;
    case UnitCategory::Building: return Queue::Buildings;
    default: return Queue::Defenses;
  }
}

// ---------------------------------------------------------------------------
// Structures on the grid
// ---------------------------------------------------------------------------
Vec2i Simulation::footprintOrigin(int type, Vec2i centre) const {
  const UnitTypeDef& d = types_.get(type);
  // Round so the footprint's centre lands as near the point as the grid allows.
  return {(centre.x - d.footW * 128 + 128) >> LEPTON_SHIFT, (centre.y - d.footH * 128 + 128) >> LEPTON_SHIFT};
}

bool Simulation::canPlace(int type, Vec2i o, bool anySlope, bool ignoreUnits) const {
  const UnitTypeDef& d = types_.get(type);
  if (!d.isStructure()) return false;
  // A logistics point is kept clear for the office of whoever takes it.
  if (d.role != "office" && !d.bridge && pointUnder(type, o) >= 0) return false;
  for (int y = o.y; y < o.y + d.footH; ++y)
    for (int x = o.x; x < o.x + d.footW; ++x) {
      if (!map_.inBounds(x, y) || (map_.block(x, y) & Map::kOccupied)) return false;
      const TileInfo& t = map_.info(x, y);
      // Fortifications may go on rougher ground than buildings; nothing goes in deep water.
      // (Buildings level their plot, so a little more slope than they once allowed is fine.)
      const int maxSlope = d.category == UnitCategory::Fortification ? 40 : 30;
      if (d.bridge) continue;  // bridges go over water and down banks
      if ((t.slope > maxSlope && !anySlope) || t.waterDepth > -4) return false;
    }
  if (ignoreUnits) return true;
  // Not on top of units (structures can't crush a squad).
  auto& units = reg_.pool<CUnit>();
  for (uint32_t e : units.entities()) {
    if (types_.get(units.get(e).type).isStructure()) continue;
    const CPosition& p = reg_.pool<CPosition>().get(e);
    const int tx = p.x >> LEPTON_SHIFT, ty = p.y >> LEPTON_SHIFT;
    if (tx >= o.x && ty >= o.y && tx < o.x + d.footW && ty < o.y + d.footH) return false;
  }
  return true;
}

bool Simulation::inBuildRange(uint8_t player, int type, Vec2i o) const {
  const UnitTypeDef& d = types_.get(type);
  auto& units = reg_.pool<CUnit>();
  auto& structs = reg_.pool<CStructure>();
  for (uint32_t e : structs.entities()) {
    if (!units.has(e) || !allied(units.get(e).owner, player) || units.get(e).owner != player) continue;
    const UnitTypeDef& sd = types_.get(units.get(e).type);
    if (sd.buildRadius <= 0) continue;
    const CStructure& s = structs.get(e);
    // Gap between the two rectangles, in tiles.
    const int gx = std::max({0, s.tx - (o.x + d.footW), o.x - (s.tx + s.w)});
    const int gy = std::max({0, s.ty - (o.y + d.footH), o.y - (s.ty + s.h)});
    if (std::max(gx, gy) <= sd.buildRadius) return true;
  }
  return false;
}

bool Simulation::canProduce(uint8_t player, int type) const {
  const UnitTypeDef& d = types_.get(type);
  if (d.builtAt.empty() || d.hidden || buildLocked(player, type)) return false;
  if (!producerFor(player, d.builtAt).valid()) return false;
  if (roomFor(player, type) <= 0) return false;  // as many as its yards allow already stand
  auto& units = reg_.pool<CUnit>();
  for (int need : d.prereqs) {
    bool have = false;
    for (uint32_t e : reg_.pool<CStructure>().entities())
      if (units.has(e) && units.get(e).owner == player && units.get(e).type == need) {
        have = true;
        break;
      }
    if (!have) return false;
  }
  return true;
}

Entity Simulation::structureAt(int tx, int ty) const {
  if (!map_.inBounds(tx, ty)) return kNullEntity;
  const uint32_t e = tileStructure_[static_cast<size_t>(ty) * map_.width() + tx];
  return e == UINT32_MAX ? kNullEntity : reg_.handle(e);
}

uint8_t Simulation::coverAt(int tx, int ty) const {
  if (!map_.inBounds(tx, ty)) return 0;
  return cover_[static_cast<size_t>(ty) * map_.width() + tx];
}

void Simulation::occupy(uint32_t e, const UnitTypeDef& def, Vec2i o) {
  for (int y = o.y; y < o.y + def.footH; ++y)
    for (int x = o.x; x < o.x + def.footW; ++x) {
      map_.setBlock(x, y, static_cast<uint8_t>(Map::kOccupied | def.blocks | (def.bridge ? Map::kDeck : 0)));
      tileStructure_[static_cast<size_t>(y) * map_.width() + x] = e;
    }
  if (def.cover || def.coverAround) coverDirty_ = true;
}

void Simulation::levelPlot(uint32_t e, const UnitTypeDef& def, Vec2i o) {
  // Bridges keep their banks; trenches, wire and sandbags follow the ground.
  if (def.bridge || (def.category == UnitCategory::Fortification && def.footW * def.footH < 2)) return;
  const int x1 = o.x + def.footW, y1 = o.y + def.footH;  // corner range of the plot
  // A corner is left alone if it touches another building's tile (its plot is set).
  auto foreign = [&](int cx, int cy) {
    for (int ty = cy - 1; ty <= cy; ++ty)
      for (int tx = cx - 1; tx <= cx; ++tx) {
        if (!map_.inBounds(tx, ty)) continue;
        const uint32_t s = tileStructure_[static_cast<size_t>(ty) * map_.width() + tx];
        if (s != UINT32_MAX && s != e) return true;
      }
    return false;
  };
  int sum = 0, n = 0;
  for (int cy = o.y; cy <= y1; ++cy)
    for (int cx = o.x; cx <= x1; ++cx) {
      sum += map_.corner(cx, cy);
      ++n;
    }
  const int level = (sum + n / 2) / n;
  // The plot itself is flat; round it, the ground may slope at most
  // kSkirt height units per tile away from it (so placing again changes nothing).
  constexpr int kSkirt = 14, kReach = 3;
  bool changed = false;
  for (int cy = o.y - kReach; cy <= y1 + kReach; ++cy)
    for (int cx = o.x - kReach; cx <= x1 + kReach; ++cx) {
      if (cx < 0 || cy < 0 || cx > map_.width() || cy > map_.height() || foreign(cx, cy)) continue;
      const int d = std::max({o.x - cx, cx - x1, o.y - cy, cy - y1, 0});
      const int h = map_.corner(cx, cy);
      if (d > 0 && map_.waterLevel() && h < map_.waterLevel()) continue;  // leave the river bed alone
      const int want = std::clamp(h, level - kSkirt * d, level + kSkirt * d);
      if (want != h) {
        map_.setCorner(cx, cy, static_cast<uint16_t>(want));
        changed = true;
      }
    }
  if (!changed) return;
  const Map::TileRect r{o.x - kReach - 1, o.y - kReach - 1, x1 + kReach, y1 + kReach};
  map_.recomputeInfo(r.x0, r.y0, r.x1, r.y1);
  map_.noteHeightEdit(r);
}

// Removes an entity, freeing its tiles if it is a structure.
void Simulation::destroyEntity(uint32_t e) {
  const Entity h = reg_.handle(e);
  if (!reg_.alive(h)) return;
  // A man who dies inside a building leaves its occupant list now: nothing
  // that reads the list between ticks (the renderer, the interface) may find
  // a deleted entity on it.
  if (const CGarrisoned* g = reg_.pool<CGarrisoned>().tryGet(e)) {
    if (reg_.alive(g->building))
      if (CGarrison* list = reg_.pool<CGarrison>().tryGet(g->building.index)) std::erase(list->occupants, h);
  }
  std::vector<uint32_t> sinking;
  if (const CStructure* s = reg_.pool<CStructure>().tryGet(e)) {
    for (int y = s->ty; y < s->ty + s->h; ++y)
      for (int x = s->tx; x < s->tx + s->w; ++x) {
        if (!map_.inBounds(x, y)) continue;
        const size_t i = static_cast<size_t>(y) * map_.width() + x;
        if (tileStructure_[i] != e) continue;
        tileStructure_[i] = UINT32_MAX;
        map_.setBlock(x, y, 0);
      }
    const UnitTypeDef& def = types_.get(reg_.get<CUnit>(h).type);
    if (def.cover || def.coverAround) coverDirty_ = true;
    if (def.bridge) {
      // Whatever was on the span goes into the river with it.
      auto& units = reg_.pool<CUnit>();
      for (uint32_t o : units.entities()) {
        if (o == e) continue;
        const UnitTypeDef& od = types_.get(units.get(o).type);
        if (od.isStructure()) continue;
        const CPosition& op = reg_.pool<CPosition>().get(o);
        const int tx = op.x >> LEPTON_SHIFT, ty = op.y >> LEPTON_SHIFT;
        if (tx >= s->tx && ty >= s->ty && tx < s->tx + s->w && ty < s->ty + s->h && !map_.passable(tx, ty, od.locomotor))
          sinking.push_back(o);
      }
    }
  }
  reg_.destroy(h);
  for (uint32_t o : sinking) damage(o, types_.get(reg_.pool<CUnit>().get(o).type).maxHealth * 1000, kNullEntity);
}

void Simulation::drownIn(int x0, int y0, int x1, int y1) {
  std::vector<uint32_t> sinking;
  auto& units = reg_.pool<CUnit>();
  for (uint32_t o : units.entities()) {
    const UnitTypeDef& od = types_.get(units.get(o).type);
    if (od.isStructure() || units.get(o).health <= 0 || reg_.pool<CGarrisoned>().has(o)) continue;
    const CPosition& op = reg_.pool<CPosition>().get(o);
    const int tx = op.x >> LEPTON_SHIFT, ty = op.y >> LEPTON_SHIFT;
    if (tx >= x0 && ty >= y0 && tx <= x1 && ty <= y1 && !map_.passable(tx, ty, od.locomotor)) sinking.push_back(o);
  }
  for (uint32_t o : sinking) damage(o, types_.get(units.get(o).type).maxHealth * 1000, kNullEntity);
}

void Simulation::recomputeCover() {
  coverDirty_ = false;
  std::fill(cover_.begin(), cover_.end(), uint8_t{0});
  auto& units = reg_.pool<CUnit>();
  auto& structs = reg_.pool<CStructure>();
  const int W = map_.width(), H = map_.height();
  for (uint32_t e : structs.entities()) {
    const UnitTypeDef& d = types_.get(units.get(e).type);
    if (!d.cover && !d.coverAround) continue;
    const CStructure& s = structs.get(e);
    for (int y = s.ty - 1; y <= s.ty + s.h; ++y)
      for (int x = s.tx - 1; x <= s.tx + s.w; ++x) {
        if (x < 0 || y < 0 || x >= W || y >= H) continue;
        const bool inside = x >= s.tx && y >= s.ty && x < s.tx + s.w && y < s.ty + s.h;
        uint8_t& c = cover_[static_cast<size_t>(y) * W + x];
        c = std::max(c, inside ? d.cover : d.coverAround);
      }
  }
}

// Distance from a shooter to a target, measured to the edge of a structure.
int32_t Simulation::effectiveDistance(uint32_t d, uint32_t target) const {
  const UnitTypeDef& def = types_.get(reg_.pool<CUnit>().get(target).type);
  if (!def.isStructure()) return static_cast<int32_t>(d);
  return std::max<int32_t>(0, static_cast<int32_t>(d) - def.radius * 3 / 4);
}

Entity Simulation::producerFor(uint8_t player, const std::string& role) const {
  auto& units = reg_.pool<CUnit>();
  auto& structs = reg_.pool<CStructure>();
  Entity first;
  for (uint32_t e : structs.entities()) {
    if (!units.has(e) || units.get(e).owner != player) continue;
    if (types_.get(units.get(e).type).role != role) continue;
    const CProducer* pr = reg_.pool<CProducer>().tryGet(e);
    if (pr && pr->primary) return reg_.handle(e);
    if (!first.valid()) first = reg_.handle(e);
  }
  return first;
}

// Where a structure's units come out and trucks unload: the first open tile
// just outside it, trying the front (south-east) edge from the middle out,
// then the sides, then the back. Neighbours can't wall a depot off this way.
Vec2i Simulation::exitOf(Entity structure) const {
  const CStructure& s = reg_.get<CStructure>(structure);
  auto open = [&](int x, int y) { return map_.passable(x, y, Locomotor::Wheeled); };
  auto at = [](int x, int y) { return Vec2i{x * LEPTONS_PER_TILE + 128, y * LEPTONS_PER_TILE + 128}; };
  const int mid = s.tx + s.w / 2;
  for (int d = 0; d <= s.w; ++d)  // front row, centre first
    for (int x : {mid + d, mid - d - 1})
      if (x >= s.tx - 1 && x <= s.tx + s.w && open(x, s.ty + s.h)) return at(x, s.ty + s.h);
  for (int y = s.ty + s.h - 1; y >= s.ty; --y)  // the two sides
    for (int x : {s.tx + s.w, s.tx - 1})
      if (open(x, y)) return at(x, y);
  for (int x = s.tx - 1; x <= s.tx + s.w; ++x)  // the back
    if (open(x, s.ty - 1)) return at(x, s.ty - 1);
  return at(mid, s.ty + s.h);
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
void Simulation::applyEconomyCommand(const Command& cmd) {
  const uint8_t pl = cmd.player % MAX_PLAYERS;
  PlayerState& ps = players_[pl];
  auto refuse = [&](uint16_t type) {
    SimEvent ev;
    ev.type = EventType::CannotBuild;
    ev.owner = pl;
    ev.unitType = type;
    events_.push_back(ev);
  };
  switch (cmd.type) {
    case CommandType::SetCredits:
      ps.credits = std::max(0, cmd.amount);
      break;

    case CommandType::Produce: {
      if (cmd.unitType >= types_.size()) break;
      const UnitTypeDef& d = types_.get(cmd.unitType);
      if (d.category == UnitCategory::Fortification) break;  // bought with PlaceWalls
      ProductionQueue& q = ps.queues[static_cast<size_t>(queueFor(d.category))];
      if (cmd.amount > 0) {
        if (!canProduce(pl, cmd.unitType)) {
          refuse(cmd.unitType);
          break;
        }
        // Structures are built one at a time; units queue up to 20.
        const bool structure = d.isStructure();
        for (int i = 0; i < cmd.amount && q.items.size() < (structure ? 1u : 20u); ++i) q.items.push_back(cmd.unitType);
      } else {
        // Cancel: remove the last queued item of this type; refund what was paid if it was in work.
        for (int n = 0; n < std::max(1, -cmd.amount); ++n) {
          auto it = std::find(q.items.rbegin(), q.items.rend(), cmd.unitType);
          if (it == q.items.rend()) break;
          const bool front = std::next(it).base() == q.items.begin();
          if (front) {
            ps.credits += q.paid;
            q.paid = 0;
            q.progress = 0;
            q.ready = false;
          }
          q.items.erase(std::next(it).base());
        }
      }
      break;
    }

    case CommandType::PlaceBuilding: {
      if (cmd.unitType >= types_.size()) break;
      ProductionQueue& q = ps.queues[static_cast<size_t>(queueFor(types_.get(cmd.unitType).category))];
      if (!q.ready || q.items.empty() || q.items.front() != cmd.unitType) break;
      const Vec2i o = footprintOrigin(cmd.unitType, cmd.target);
      if (!canPlace(cmd.unitType, o) || !inBuildRange(pl, cmd.unitType, o) || roomFor(pl, cmd.unitType) <= 0 ||
          (!ai_.player(pl).enabled && !inPlayArea(pl, cmd.target.x >> LEPTON_SHIFT, cmd.target.y >> LEPTON_SHIFT))) {
        refuse(cmd.unitType);
        break;
      }
      const UnitTypeDef& d = types_.get(cmd.unitType);
      const Entity e = spawnUnit(cmd.unitType, pl, {o.x * 256 + d.footW * 128, o.y * 256 + d.footH * 128}, 0);
      if (!e.valid()) {
        refuse(cmd.unitType);
        break;
      }
      q.items.erase(q.items.begin());
      q.progress = q.paid = 0;
      q.ready = false;
      break;
    }

    case CommandType::PlaceWalls: {
      if (cmd.unitType >= types_.size()) break;
      const UnitTypeDef& d = types_.get(cmd.unitType);
      if (d.category != UnitCategory::Fortification || !canProduce(pl, cmd.unitType)) {
        refuse(cmd.unitType);
        break;
      }
      for (const SpawnSpec& s : cmd.spawns) {
        if (ps.credits < d.cost) {
          refuse(cmd.unitType);
          break;
        }
        const Vec2i o = footprintOrigin(cmd.unitType, s.pos);
        if (!canPlace(cmd.unitType, o) || !inBuildRange(pl, cmd.unitType, o)) continue;
        if (!ai_.player(pl).enabled && !inPlayArea(pl, s.pos.x >> LEPTON_SHIFT, s.pos.y >> LEPTON_SHIFT)) continue;
        if (spawnUnit(cmd.unitType, pl, {o.x * 256 + d.footW * 128, o.y * 256 + d.footH * 128}, s.facing).valid())
          ps.credits -= d.cost;
      }
      break;
    }

    case CommandType::Sell:
      for (Entity e : cmd.units) {
        if (!reg_.alive(e) || !reg_.has<CStructure>(e)) continue;
        const CUnit& u = reg_.get<CUnit>(e);
        const UnitTypeDef& d = types_.get(u.type);
        // Refund half, less for a damaged structure.
        players_[u.owner % MAX_PLAYERS].credits +=
            d.cost / 2 * std::max(0, u.health) / std::max(1, d.maxHealth);
        SimEvent ev;
        ev.type = EventType::StructureSold;
        ev.owner = u.owner;
        ev.unitType = u.type;
        const CPosition& p = reg_.get<CPosition>(e);
        ev.at = {p.x, p.y};
        events_.push_back(ev);
        destroyEntity(e.index);
      }
      break;

    case CommandType::Deploy:
      for (Entity e : cmd.units) {
        if (!reg_.alive(e) || !reg_.has<CUnit>(e)) continue;
        const CUnit u = reg_.get<CUnit>(e);
        const UnitTypeDef& d = types_.get(u.type);
        if (d.deploysTo < 0) continue;
        const CPosition p = reg_.get<CPosition>(e);
        const Vec2i o = footprintOrigin(d.deploysTo, {p.x, p.y});
        // The truck itself is in the way: take it off the field first.
        const uint32_t idx = e.index;
        destroyEntity(idx);
        const UnitTypeDef& sd = types_.get(d.deploysTo);
        if (!canPlace(d.deploysTo, o) ||
            !spawnUnit(static_cast<uint16_t>(d.deploysTo), u.owner, {o.x * 256 + sd.footW * 128, o.y * 256 + sd.footH * 128}, 0)
                 .valid()) {
          // No room: put the truck back where it was.
          const Entity back = spawnUnit(u.type, u.owner, {p.x, p.y}, 0);
          if (back.valid()) reg_.get<CUnit>(back).health = u.health;
          refuse(u.type);
        }
      }
      break;

    case CommandType::Harvest:
      for (Entity e : cmd.units) {
        if (!reg_.alive(e) || !reg_.has<CHarvester>(e)) continue;
        setOrderIdle(e);
        reg_.get<COrder>(e).type = OrderType::Harvest;
        CHarvester& h = reg_.get<CHarvester>(e);
        h.idleTicks = 0;
        h.timer = 0;
        // Told which office to work for: it does, if that office has room for a truck.
        const Entity office = cmd.targetUnit;
        if (office.valid() && reg_.alive(office) && reg_.has<CUnit>(office) && office != h.office) {
          const CUnit& ou = reg_.get<CUnit>(office);
          const UnitTypeDef& od = types_.get(ou.type);
          if (ou.owner == reg_.get<CUnit>(e).owner && od.role == "office" && officeTrucks(office) < std::max(1, od.convoy))
            h.office = office;
        }
        h.state = h.cargo > 0 ? HarvestState::ToYard : h.office.valid() ? HarvestState::ToOffice : HarvestState::Seek;
      }
      break;

    case CommandType::SetRally:
      for (Entity e : cmd.units) {
        if (!reg_.alive(e) || !reg_.has<CProducer>(e)) continue;
        CProducer& pr = reg_.get<CProducer>(e);
        pr.hasRally = true;
        pr.rally = cmd.target;
        // The last structure given a rally point becomes the primary producer.
        const std::string& role = types_.get(reg_.get<CUnit>(e).type).role;
        for (uint32_t o : reg_.pool<CProducer>().entities())
          if (reg_.get<CUnit>(reg_.handle(o)).owner == reg_.get<CUnit>(e).owner &&
              types_.get(reg_.get<CUnit>(reg_.handle(o)).type).role == role)
            reg_.pool<CProducer>().get(o).primary = false;
        pr.primary = true;
      }
      break;

    case CommandType::Repair:
      for (Entity e : cmd.units) {
        if (!reg_.alive(e) || !reg_.has<CUnit>(e) || types_.get(reg_.get<CUnit>(e).type).repair <= 0) continue;
        if (!canRepair(e, cmd.targetUnit)) continue;
        setOrderIdle(e);
        COrder& o = reg_.get<COrder>(e);
        o.type = OrderType::Repair;
        o.target = cmd.targetUnit;
        const CPosition& tp = reg_.get<CPosition>(cmd.targetUnit);
        o.chasedAt = {tp.x, tp.y};
        requestPath(e, o.chasedAt);
      }
      break;

    default:
      break;
  }
}

// An engineer can mend a damaged, friendly structure or vehicle (not people).
bool Simulation::canRepair(Entity engineer, Entity target) const {
  if (!reg_.alive(target) || !reg_.has<CUnit>(target) || engineer == target) return false;
  const CUnit& t = reg_.get<CUnit>(target);
  const UnitTypeDef& td = types_.get(t.type);
  if (td.infantry() || !td.targetable || t.health <= 0 || t.health >= td.maxHealth) return false;
  return allied(reg_.get<CUnit>(engineer).owner, t.owner);
}

// Engineers: walk up to what they were told to mend and patch it up; idle
// ones look around for anything friendly that is damaged.
void Simulation::stepRepairs() {
  auto& units = reg_.pool<CUnit>();
  auto& orders = reg_.pool<COrder>();
  auto& pos = reg_.pool<CPosition>();
  for (uint32_t ei : units.entities()) {
    const CUnit& u = units.get(ei);
    const UnitTypeDef& def = types_.get(u.type);
    if (def.repair <= 0 || u.health <= 0 || !orders.has(ei)) continue;
    COrder& o = orders.get(ei);
    const Entity e = reg_.handle(ei);
    const CPosition& p = pos.get(ei);
    if (const CGarrisoned* g = reg_.pool<CGarrisoned>().tryGet(ei)) {
      // A pioneer manning a work keeps it in repair, and the pieces of the
      // same line or the walls right round it once it is whole.
      Entity fix = canRepair(e, g->building) ? g->building : kNullEntity;
      if (!fix.valid() && reg_.alive(g->building) && reg_.has<CStructure>(g->building)) {
        const CStructure& s = reg_.get<CStructure>(g->building);
        int32_t worst = INT32_MAX;
        for (int y = s.ty - 2; y < s.ty + s.h + 2; ++y)
          for (int x = s.tx - 2; x < s.tx + s.w + 2; ++x) {
            const Entity t = structureAt(x, y);
            if (!t.valid() || t == fix || !canRepair(e, t)) continue;
            if (types_.get(units.get(t.index).type).category != UnitCategory::Fortification && t != g->building) continue;
            const CUnit& tu = units.get(t.index);
            const int32_t left = tu.health * 100 / std::max(1, types_.get(tu.type).maxHealth);
            if (left < worst) worst = left, fix = t;
          }
      }
      if (fix.valid()) {
        CUnit& t = units.get(fix.index);
        t.health = std::min(types_.get(t.type).maxHealth, t.health + def.repair);
      }
      continue;
    }
    if (o.type == OrderType::Idle && (tick_ + ei) % 15 == 0) {
      // Look for the nearest damaged friend in sight.
      Entity best;
      uint32_t bestD = static_cast<uint32_t>(def.sight);
      for (uint32_t oi : units.entities()) {
        const Entity t = reg_.handle(oi);
        if (!canRepair(e, t)) continue;
        const CPosition& tp = pos.get(oi);
        const uint32_t d = static_cast<uint32_t>(effectiveDistance(distance(tp.x - p.x, tp.y - p.y), oi));
        if (d < bestD) {
          bestD = d;
          best = t;
        }
      }
      if (best.valid()) {
        o.type = OrderType::Repair;
        o.target = best;
        const CPosition& tp = pos.get(best.index);
        o.chasedAt = {tp.x, tp.y};
        requestPath(e, o.chasedAt);
      }
      continue;
    }
    if (o.type != OrderType::Repair) continue;
    if (!canRepair(e, o.target)) {  // done, gone or captured
      setOrderIdle(e);
      continue;
    }
    const CPosition& tp = pos.get(o.target.index);
    const int32_t d = effectiveDistance(distance(tp.x - p.x, tp.y - p.y), o.target.index);
    CMover& m = reg_.get<CMover>(e);
    const int32_t reach = types_.get(units.get(o.target.index).type).radius / 2 + 160;
    if (d <= reach) {
      m.active = false;
      m.request = 0;
      CUnit& t = units.get(o.target.index);
      t.health = std::min(types_.get(t.type).maxHealth, t.health + def.repair);
      CFacing& f = reg_.get<CFacing>(e);
      turnToward(f.angle, atan2Angle(tp.y - p.y, tp.x - p.x), f.turnWait, def.turnTicks);
    } else if ((!m.active && !m.request) ||
               ((tick_ + ei) % 30 == 0 && distance(tp.x - o.chasedAt.x, tp.y - o.chasedAt.y) > 512)) {
      o.chasedAt = {tp.x, tp.y};
      requestPath(e, o.chasedAt);
    }
  }
}

// ---------------------------------------------------------------------------
// Per tick
// ---------------------------------------------------------------------------
void Simulation::stepEconomy() {
  if (coverDirty_) recomputeCover();
  // Power, from the structures standing now.
  for (PlayerState& ps : players_) ps.powerMade = ps.powerUsed = 0;
  auto& units = reg_.pool<CUnit>();
  structureReach_ = 0;
  auras_.clear();
  for (uint32_t e : reg_.pool<CStructure>().entities()) {
    const CUnit& u = units.get(e);
    const UnitTypeDef& sd = types_.get(u.type);
    structureReach_ = std::max(structureReach_, sd.radius * 3 / 4);
    if (sd.auraFireRate > 0 && sd.auraRadius > 0 && u.health > 0 && u.owner < MAX_PLAYERS) {
      const CPosition& sp = reg_.pool<CPosition>().get(e);
      auras_.push_back({u.owner, {sp.x, sp.y}, sd.auraRadius, sd.auraFireRate});
    }
    const int32_t pw = sd.power;
    PlayerState& ps = players_[u.owner % MAX_PLAYERS];
    if (pw > 0) ps.powerMade += pw;
    else ps.powerUsed -= pw;
  }
  for (uint8_t p = 0; p < MAX_PLAYERS; ++p) stepProduction(p);
  stepPoints();
  stepConvoys();
  stepIncome();
  stepRepairs();
  if (tick_ % 15 == 0) checkDefeat();
}

// How fast a line works, in halves of the normal speed: every further
// building of the kind that does the work (a second barracks, tank works,
// construction yard...) adds half again, up to three times the speed with
// five of them.
int32_t Simulation::lineSpeed(uint8_t player, int type) const {
  const std::string& role = types_.get(type).builtAt;
  if (role.empty()) return 2;
  auto& units = reg_.pool<CUnit>();
  int32_t n = 0;
  for (uint32_t e : reg_.pool<CStructure>().entities()) {
    const CUnit& u = units.get(e);
    n += u.owner == player && u.health > 0 && types_.get(u.type).role == role;
  }
  return 2 + std::clamp(n - 1, 0, 4);
}

void Simulation::stepProduction(uint8_t pl) {
  PlayerState& ps = players_[pl];
  for (ProductionQueue& q : ps.queues) {
    if (q.items.empty() || q.ready) continue;
    const uint16_t type = q.items.front();
    const UnitTypeDef& d = types_.get(type);
    if (!canProduce(pl, type)) continue;  // lost the factory or a prerequisite: wait
    // Short of power, work goes at half speed.
    if (ps.lowPower() && (tick_ & 1)) continue;
    const int32_t total = std::max(1, d.buildTime);
    // More buildings on the job: more steps a tick (a half is every other turn).
    const int32_t halves = lineSpeed(pl, type);
    const int32_t steps = halves / 2 + ((halves & 1) && ((tick_ >> 1) & 1) ? 1 : 0);
    for (int32_t step = 0; step < steps; ++step) {
      const int32_t due = static_cast<int32_t>(static_cast<int64_t>(d.cost) * (q.progress + 1) / total) - q.paid;
      if (due > ps.credits) break;  // out of supplies: production stalls
      ps.credits -= due;
      q.paid += due;
      if (++q.progress < total) continue;
      SimEvent ev;
      ev.type = EventType::UnitReady;
      ev.owner = pl;
      ev.unitType = type;
      events_.push_back(ev);
      if (d.isStructure()) {
        q.ready = true;  // waits for the player to place it
      } else {
        q.items.erase(q.items.begin());
        q.progress = q.paid = 0;
        completeUnit(pl, type);
      }
      break;  // the next order starts on the next tick
    }
  }
}

void Simulation::completeUnit(uint8_t pl, uint16_t type) {
  const UnitTypeDef& d = types_.get(type);
  const Entity from = producerFor(pl, d.builtAt);
  if (!from.valid()) return;
  const Entity e = spawnUnit(type, pl, exitOf(from), angleOfFacing(4));
  if (!e.valid()) return;
  const CProducer* pr = reg_.tryGet<CProducer>(from);
  if (reg_.has<CHarvester>(e)) return;  // trucks go straight to work
  if (pr && pr->hasRally) {
    Command m;
    m.units = {e};
    orderMove(m.units, pr->rally, OrderType::Move, m);
  }
}

void Simulation::checkDefeat() {
  std::array<int, MAX_PLAYERS> owned{};
  std::array<bool, MAX_PLAYERS> everHad{};
  auto& units = reg_.pool<CUnit>();
  for (uint32_t e : units.entities()) {
    const CUnit& u = units.get(e);
    if (u.owner >= MAX_PLAYERS) continue;
    // Walls and trenches alone don't keep a side in the game.
    if (types_.get(u.type).category == UnitCategory::Fortification) continue;
    ++owned[u.owner];
  }
  for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
    everHad[p] = playedOnce_[p] || owned[p] > 0;
    playedOnce_[p] = everHad[p];
    PlayerState& ps = players_[p];
    if (!ps.defeated && everHad[p] && owned[p] == 0) {
      ps.defeated = true;
      SimEvent ev;
      ev.type = EventType::PlayerDefeated;
      ev.owner = p;
      events_.push_back(ev);
    }
    if (owned[p] > 0 && !scriptDefeated_[p]) ps.defeated = false;  // the editor can bring a side back
  }
}

}  // namespace slave

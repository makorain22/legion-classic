// Pioneer Engine - logistics (Milestone 9.6).
//
// Money comes from three places. Buildings in the base pay a steady income
// (post office, logistics warehouse, strategic fuel depot; so many of each
// per construction yard). Out on the map lie a few logistics points, far
// from every base: a side whose troops hold one alone for a while gets a
// logistics office there, and with it three supply trucks that shuttle
// between the office and the side's headquarters, paying in a large sum
// every time one arrives. The office and the trucks can be destroyed; a
// point whose office is gone is free to take again. Lost trucks are replaced
// from a logistics warehouse.
#include <algorithm>

#include "pioneer/sim/simulation.hpp"

namespace pioneer {

namespace {
constexpr int32_t kT = LEPTONS_PER_TILE;
constexpr uint32_t kPointStep = 3;                       // ticks between looks at the points
constexpr uint16_t kLoadTicks = 12 * TICKS_PER_SECOND;   // loading at the office
constexpr uint16_t kUnloadTicks = 2 * TICKS_PER_SECOND;  // paying in at the yard
constexpr uint32_t kDockReach = 320;                     // leptons from a door to load or unload
constexpr uint16_t kLookAgain = 5 * TICKS_PER_SECOND;    // nothing to do: wait this long before looking again
constexpr uint16_t kBackToWork = 8 * TICKS_PER_SECOND;   // a truck left standing goes back to its run after this
}  // namespace

Entity Simulation::officeOn(int point) const {
  if (point < 0 || point >= static_cast<int>(map_.supplyPoints.size())) return kNullEntity;
  const Vec2i p = map_.supplyPoints[static_cast<size_t>(point)];
  const Entity s = structureAt(p.x, p.y);
  if (!s.valid() || !reg_.alive(s)) return kNullEntity;
  return types_.get(reg_.get<CUnit>(s).type).role == "office" ? s : kNullEntity;
}

int Simulation::pointUnder(int type, Vec2i o) const {
  const UnitTypeDef& d = types_.get(type);
  for (size_t i = 0; i < map_.supplyPoints.size(); ++i) {
    const Vec2i p = map_.supplyPoints[i];
    if (p.x + 1 >= o.x && p.x - 1 < o.x + d.footW && p.y + 1 >= o.y && p.y - 1 < o.y + d.footH) return static_cast<int>(i);
  }
  return -1;
}

int Simulation::officeTrucks(Entity office) const {
  int n = 0;
  auto& trucks = reg_.pool<CHarvester>();
  for (const CHarvester& h : trucks.data()) n += h.office == office;
  return n;
}

int32_t Simulation::roomFor(uint8_t player, int type) const {
  const UnitTypeDef& d = types_.get(type);
  if (d.perYard <= 0) return INT32_MAX;
  auto& units = reg_.pool<CUnit>();
  int32_t yards = 0, have = 0;
  for (uint32_t e : reg_.pool<CStructure>().entities()) {
    const CUnit& u = units.get(e);
    if (u.owner != player) continue;
    yards += types_.get(u.type).role == "yard";
    have += u.type == type;
  }
  return d.perYard * yards - have;
}

int32_t Simulation::fireRateBonus(uint8_t owner, int32_t x, int32_t y) const {
  int32_t best = 0;
  for (const Aura& a : auras_)
    if (a.fireRate > best && allied(a.owner, owner) && distance(a.at.x - x, a.at.y - y) <= static_cast<uint32_t>(a.radius))
      best = a.fireRate;
  return best;
}

// Who holds each free logistics point, and for how long. A point is held by
// the one side (allies count together) with armed troops within reach of it
// while no enemy is; when it has been held long enough the office goes up.
void Simulation::stepPoints() {
  const size_t n = map_.supplyPoints.size();
  if (points_.size() != n) points_.assign(n, PointState{});
  if (n == 0 || tick_ % kPointStep != 0) return;
  auto& units = reg_.pool<CUnit>();
  auto& pos = reg_.pool<CPosition>();
  auto& garrisoned = reg_.pool<CGarrisoned>();

  struct Presence {
    std::array<uint16_t, MAX_PLAYERS> count{};
    std::array<int32_t, MAX_PLAYERS> type{};  // a unit type of that side standing there (for its faction)
  };
  std::vector<Presence> here(n);
  std::vector<uint8_t> open(n, 0);
  bool any = false;
  for (size_t i = 0; i < n; ++i) {
    open[i] = !officeOn(static_cast<int>(i)).valid();
    any = any || open[i];
    if (!open[i]) points_[i] = PointState{};
  }
  if (!any) return;
  for (uint32_t e : units.entities()) {
    const CUnit& u = units.get(e);
    if (u.owner >= MAX_PLAYERS || u.health <= 0) continue;
    const UnitTypeDef& d = types_.get(u.type);
    if (d.isStructure() || d.weapons.empty() || garrisoned.has(e)) continue;
    const CPosition& p = pos.get(e);
    for (size_t i = 0; i < n; ++i) {
      if (!open[i]) continue;
      const Vec2i sp = map_.supplyPoints[i];
      if (std::abs(p.x - (sp.x * kT + 128)) > kCaptureReach || std::abs(p.y - (sp.y * kT + 128)) > kCaptureReach) continue;
      if (distance(p.x - (sp.x * kT + 128), p.y - (sp.y * kT + 128)) > static_cast<uint32_t>(kCaptureReach)) continue;
      if (here[i].count[u.owner]++ == 0) here[i].type[u.owner] = u.type;
    }
  }
  for (size_t i = 0; i < n; ++i) {
    if (!open[i]) continue;
    PointState& st = points_[i];
    const Presence& pr = here[i];
    // The strongest side there; contested if anyone not allied to it is there too.
    int side = -1;
    for (int p = 0; p < MAX_PLAYERS; ++p)
      if (pr.count[static_cast<size_t>(p)] > 0 && (side < 0 || pr.count[static_cast<size_t>(p)] > pr.count[static_cast<size_t>(side)])) side = p;
    bool contested = false;
    for (int p = 0; p < MAX_PLAYERS && side >= 0; ++p)
      if (pr.count[static_cast<size_t>(p)] > 0 && !allied(static_cast<uint8_t>(p), static_cast<uint8_t>(side))) contested = true;
    if (side < 0) {
      // Nobody there: the hold lapses, twice as fast as it was gained.
      st.progress = static_cast<uint16_t>(std::max<int32_t>(0, st.progress - 2 * static_cast<int32_t>(kPointStep)));
      if (st.progress == 0) st.side = 0xFF;
      continue;
    }
    if (contested) continue;  // nobody gains while it is fought over
    if (st.side == 0xFF || !allied(st.side, static_cast<uint8_t>(side))) {
      st.side = static_cast<uint8_t>(side);
      st.progress = 0;
    }
    st.progress = static_cast<uint16_t>(std::min<int32_t>(kCaptureTicks, st.progress + static_cast<int32_t>(kPointStep)));
    if (st.progress < kCaptureTicks) continue;

    // Held: the office goes up, in the middle of the point if the ground allows.
    // (It goes to the strongest of the allies standing there.)
    const uint8_t owner = static_cast<uint8_t>(side);
    const std::string& faction = types_.get(pr.type[owner]).faction;
    int office = -1;
    for (int t = 0; t < static_cast<int>(types_.size()) && office < 0; ++t)
      if (types_.get(t).faction == faction && types_.get(t).role == "office" && !types_.get(t).hidden) office = t;
    if (office < 0) continue;  // this army has no logistics office
    const UnitTypeDef& od = types_.get(office);
    const Vec2i sp = map_.supplyPoints[i];
    Vec2i at{-1, -1};
    for (int ring = 0; ring <= 1 && at.x < 0; ++ring)
      for (int oy = -1; oy <= 1 && at.x < 0; ++oy)
        for (int ox = -1; ox <= 1 && at.x < 0; ++ox) {
          if (std::max(std::abs(ox), std::abs(oy)) != ring) continue;
          const Vec2i o{sp.x - od.footW / 2 + ox, sp.y - od.footH / 2 + oy};
          if (sp.x < o.x || sp.y < o.y || sp.x >= o.x + od.footW || sp.y >= o.y + od.footH) continue;
          if (canPlace(office, o, true, true)) at = o;
        }
    if (at.x < 0) continue;  // no room for it (the map maker put something in the way)
    // Anyone standing where it will go steps aside first.
    std::vector<Entity> inside;
    for (uint32_t e : units.entities()) {
      if (types_.get(units.get(e).type).isStructure()) continue;
      const CPosition& p = pos.get(e);
      const int tx = p.x >> LEPTON_SHIFT, ty = p.y >> LEPTON_SHIFT;
      if (tx >= at.x && ty >= at.y && tx < at.x + od.footW && ty < at.y + od.footH) inside.push_back(reg_.handle(e));
    }
    if (!inside.empty()) {
      if (tick_ % TICKS_PER_SECOND < kPointStep)
        for (Entity e : inside) {
          if (!reg_.has<CMover>(e) || types_.get(reg_.get<CUnit>(e).type).speed <= 0) continue;
          const COrder& o = reg_.get<COrder>(e);
          if (o.type != OrderType::Idle) continue;  // already going somewhere
          const CPosition& p = pos.get(e.index);
          const Vec2i c{at.x * kT + od.footW * 128, at.y * kT + od.footH * 128};
          int32_t dx = p.x - c.x, dy = p.y - c.y;
          if (dx == 0 && dy == 0) dy = 1;
          const int32_t len = static_cast<int32_t>(std::max<uint32_t>(1, distance(dx, dy)));
          const int32_t out = (std::max(od.footW, od.footH) * 128) + 2 * kT;
          Command mv;
          mv.units = {e};
          orderMove(mv.units, {c.x + static_cast<int32_t>(static_cast<int64_t>(dx) * out / len), c.y + static_cast<int32_t>(static_cast<int64_t>(dy) * out / len)},
                    OrderType::Move, mv);
        }
      continue;
    }
    const Entity e = spawnUnit(static_cast<uint16_t>(office), owner, {at.x * kT + od.footW * 128, at.y * kT + od.footH * 128}, 0);
    if (!e.valid()) continue;
    st = PointState{};
    SimEvent ev;
    ev.type = EventType::PointCaptured;
    ev.owner = owner;
    ev.unitType = static_cast<uint16_t>(office);
    ev.at = {sp.x * kT + 128, sp.y * kT + 128};
    events_.push_back(ev);
  }
}

// Supply trucks: out to their office, load, back to the headquarters, pay in.
void Simulation::stepConvoys() {
  auto& units = reg_.pool<CUnit>();
  auto& structs = reg_.pool<CStructure>();
  auto ownStructure = [&](Entity s, uint8_t owner, const char* role) {
    if (!s.valid() || !reg_.alive(s) || !units.has(s.index) || !structs.has(s.index)) return false;
    const CUnit& su = units.get(s.index);
    return su.owner == owner && su.health > 0 && types_.get(su.type).role == role;
  };
  reg_.each<CHarvester, COrder, CMover, CPosition, CUnit>(
      [&](uint32_t ei, CHarvester& h, COrder& o, CMover& m, CPosition& p, CUnit& u) {
        if (u.health <= 0) return;
        const Entity self = reg_.handle(ei);
        if (o.type != OrderType::Harvest) {
          // Sent somewhere by hand. Left standing afterwards, it goes back to its run.
          if (o.type != OrderType::Idle) h.timer = 0;
          else if (++h.timer >= kBackToWork) {
            o.type = OrderType::Harvest;
            h.timer = 0;
            h.idleTicks = 0;
            h.state = h.cargo > 0 ? HarvestState::ToYard : HarvestState::Seek;
          }
          return;
        }
        if (h.idleTicks > 0) {
          --h.idleTicks;
          return;
        }
        const UnitTypeDef& def = types_.get(u.type);
        if (h.office.valid() && !ownStructure(h.office, u.owner, "office")) {
          h.office = kNullEntity;  // destroyed (or no longer ours)
          if (h.state == HarvestState::ToOffice || h.state == HarvestState::Loading) h.state = HarvestState::Seek;
        }
        auto goTo = [&](Vec2i door) {
          if (!m.active && !m.request) requestPath(self, door);
        };
        auto arrived = [&](Vec2i door) {
          if (distance(door.x - p.x, door.y - p.y) > kDockReach) return false;
          m.active = false;
          m.request = 0;
          m.path.clear();
          return true;
        };
        switch (h.state) {
          case HarvestState::Seek: {
            if (h.cargo > 0) {
              h.state = HarvestState::ToYard;
              break;
            }
            if (!h.office.valid()) {
              // The nearest office of ours that is short of a truck.
              uint32_t bd = UINT32_MAX;
              for (uint32_t s : structs.entities()) {
                const CUnit& su = units.get(s);
                const UnitTypeDef& sd = types_.get(su.type);
                if (su.owner != u.owner || su.health <= 0 || sd.role != "office") continue;
                if (officeTrucks(reg_.handle(s)) >= std::max(1, sd.convoy)) continue;
                const CPosition& sp = reg_.pool<CPosition>().get(s);
                const uint32_t d = distance(sp.x - p.x, sp.y - p.y);
                if (d < bd) {
                  bd = d;
                  h.office = reg_.handle(s);
                }
              }
            }
            if (!h.office.valid()) {
              h.idleTicks = kLookAgain;
              break;
            }
            h.state = HarvestState::ToOffice;
            break;
          }
          case HarvestState::ToOffice: {
            const Vec2i door = exitOf(h.office);
            if (arrived(door)) {
              h.state = HarvestState::Loading;
              h.timer = 0;
            } else {
              goTo(door);
              if (m.failures >= 3) h.idleTicks = kLookAgain;  // no way there just now
            }
            break;
          }
          case HarvestState::Loading:
            if (++h.timer < kLoadTicks) break;
            h.timer = 0;
            h.cargo = std::max(1, def.capacity);
            h.yard = kNullEntity;
            h.state = HarvestState::ToYard;
            break;
          case HarvestState::ToYard: {
            if (!ownStructure(h.yard, u.owner, "yard")) {
              h.yard = kNullEntity;
              uint32_t bd = UINT32_MAX;
              for (uint32_t s : structs.entities()) {
                const CUnit& su = units.get(s);
                if (su.owner != u.owner || su.health <= 0 || types_.get(su.type).role != "yard") continue;
                const CPosition& sp = reg_.pool<CPosition>().get(s);
                const uint32_t d = distance(sp.x - p.x, sp.y - p.y);
                if (d < bd) {
                  bd = d;
                  h.yard = reg_.handle(s);
                }
              }
              if (!h.yard.valid()) {
                h.idleTicks = kLookAgain;  // no headquarters to bring it to: wait for one
                break;
              }
              m.active = false;
              m.request = 0;
              m.path.clear();
            }
            const Vec2i door = exitOf(h.yard);
            if (arrived(door)) {
              h.state = HarvestState::Unloading;
              h.timer = 0;
            } else {
              goTo(door);
              if (m.failures >= 3) h.idleTicks = kLookAgain;
            }
            break;
          }
          case HarvestState::Unloading: {
            if (++h.timer < kUnloadTicks) break;
            h.timer = 0;
            players_[u.owner % MAX_PLAYERS].credits += h.cargo;
            SimEvent ev;
            ev.type = EventType::ConvoyArrived;
            ev.owner = u.owner;
            ev.amount = h.cargo;
            ev.at = {p.x, p.y};
            events_.push_back(ev);
            h.cargo = 0;
            h.state = h.office.valid() ? HarvestState::ToOffice : HarvestState::Seek;
            break;
          }
        }
      });
}

}  // namespace pioneer

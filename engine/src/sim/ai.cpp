// Slave Engine - the computer player. See ai.hpp.
#include "slave/sim/ai.hpp"
#include "slave/core/rng.hpp"

#include <algorithm>
#include <string>

#include "slave/core/bytes.hpp"
#include "slave/sim/simulation.hpp"

namespace slave {

namespace {
struct Tuning {
  uint32_t thinkTicks;   // how often it thinks
  int waveSize;          // first attack wave
  int waveGrowth;        // added per wave
  uint32_t waveTimeout;  // ticks: attack with what it has after this long
  int bunkers, atGuns, towers;
  int refineries, barracks, factories;
  int trucksPerRefinery;
  int32_t reserve;       // supplies kept back from army production
  int engineers;         // kept at home to repair
};
const Tuning kTuning[3] = {
    {45, 18, 6, 15 * 360, 1, 0, 0, 1, 1, 1, 1, 1500, 0},  // easy
    {20, 30, 9, 15 * 240, 2, 1, 1, 2, 1, 1, 2, 600, 1},  // normal
    {10, 42, 12, 15 * 180, 3, 2, 1, 3, 2, 2, 2, 300, 2},  // hard
};

int32_t dist(Vec2i a, Vec2i b) { return static_cast<int32_t>(distance(a.x - b.x, a.y - b.y)); }
}  // namespace

void SkirmishAi::setPlayer(uint8_t p, bool enabled, AiDifficulty d) {
  if (p >= 8) return;
  players_[p].enabled = enabled;
  players_[p].difficulty = d;
}

bool SkirmishAi::any() const {
  for (const AiPlayer& p : players_)
    if (p.enabled) return true;
  return false;
}

void SkirmishAi::update(Simulation& sim) {
  for (uint8_t p = 0; p < 8; ++p) {
    AiPlayer& ai = players_[p];
    if (!ai.enabled || sim.players_[p].defeated) continue;
    if (sim.tick_ < ai.nextThink) continue;
    ai.nextThink = sim.tick_ + kTuning[static_cast<int>(ai.difficulty)].thinkTicks;
    think(sim, p);
  }
}

void SkirmishAi::think(Simulation& sim, uint8_t p) {
  AiPlayer& ai = players_[p];
  Registry& reg = sim.reg_;
  // Find the base: the first yard (or anything we own if we have no yard).
  Vec2i base{-1, -1};
  for (uint32_t e : reg.pool<CStructure>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner == p && sim.types_.get(u.type).role == "yard") {
      const CPosition& c = reg.pool<CPosition>().get(e);
      base = {c.x, c.y};
      break;
    }
  }
  if (base.x < 0)
    for (uint32_t e : reg.pool<CUnit>().entities())
      if (reg.pool<CUnit>().get(e).owner == p) {
        const CPosition& c = reg.pool<CPosition>().get(e);
        base = {c.x, c.y};
        break;
      }
  if (base.x < 0) return;
  ai.base = base;
  // Which way is the enemy? Toward the average of enemy structures (or units).
  int64_t ex = 0, ey = 0, n = 0;
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (sim.allied(u.owner, p) || u.owner >= MAX_PLAYERS) continue;
    const CPosition& c = reg.pool<CPosition>().get(e);
    ex += c.x;
    ey += c.y;
    ++n;
  }
  if (n) {
    const int32_t dx = static_cast<int32_t>(ex / n - base.x), dy = static_cast<int32_t>(ey / n - base.y);
    const uint32_t len = std::max<uint32_t>(1, distance(dx, dy));
    ai.toward = {static_cast<int32_t>(static_cast<int64_t>(dx) * 256 / len), static_cast<int32_t>(static_cast<int64_t>(dy) * 256 / len)};
  }
  ai.staging = {base.x + ai.toward.x * 9, base.y + ai.toward.y * 9};
  ai.staging.x = std::clamp(ai.staging.x, 512, sim.map_.widthLeptons() - 512);
  ai.staging.y = std::clamp(ai.staging.y, 512, sim.map_.heightLeptons() - 512);
  manageEconomy(sim, p);
  manageArmy(sim, p);
}

// Finds a spot for a structure near the base (or toward the enemy, for
// defences), leaving a lane around it so the base never walls itself in, and
// keeping off oil.
bool SkirmishAi::placeStructure(Simulation& sim, uint8_t p, int type, bool defensive) {
  const AiPlayer& ai = players_[p];
  const UnitTypeDef& d = sim.types_.get(type);
  const Map& m = sim.map_;
  const Vec2i want = defensive ? Vec2i{ai.base.x + ai.toward.x * 8, ai.base.y + ai.toward.y * 8} : ai.base;
  if (d.role == "depot") {
    // Supply depots go on the nearest free supply point.
    std::vector<std::pair<int32_t, Vec2i>> sites;
    for (const Vec2i& sp : m.supplyPoints) sites.push_back({dist({sp.x * 256 + 128, sp.y * 256 + 128}, ai.base), sp});
    std::sort(sites.begin(), sites.end(), [](const auto& a, const auto& b) {
      return a.first != b.first ? a.first < b.first : (a.second.y != b.second.y ? a.second.y < b.second.y : a.second.x < b.second.x);
    });
    for (const auto& [score, sp] : sites) {
      if (score > 70 * LEPTONS_PER_TILE) break;
      for (int oy = -d.footH; oy <= 1; ++oy)
        for (int ox = -d.footW; ox <= 1; ++ox) {
          const Vec2i o{sp.x + ox, sp.y + oy};
          if (!sim.canPlace(type, o)) continue;
          Command c;
          c.type = CommandType::PlaceBuilding;
          c.player = p;
          c.unitType = static_cast<uint16_t>(type);
          c.target = {o.x * 256 + d.footW * 128, o.y * 256 + d.footH * 128};
          sim.queueInternal(std::move(c));
          return true;
        }
    }
    return false;
  }
  Vec2i best{-1, -1};
  int32_t bestScore = INT32_MAX;
  const int cx = want.x >> LEPTON_SHIFT, cy = want.y >> LEPTON_SHIFT;
  for (int r = 0; r <= 16; ++r) {
    for (int oy = -r; oy <= r; ++oy)
      for (int ox = -r; ox <= r; ++ox) {
        if (std::max(std::abs(ox), std::abs(oy)) != r) continue;
        const Vec2i o{cx + ox - d.footW / 2, cy + oy - d.footH / 2};
        if (!sim.canPlace(type, o) || !sim.inBuildRange(p, type, o)) continue;
        // A one-tile lane all round, and no oil underneath.
        bool clear = true;
        for (int y = o.y - 1; y <= o.y + d.footH && clear; ++y)
          for (int x = o.x - 1; x <= o.x + d.footW && clear; ++x) {
            if (!m.inBounds(x, y)) continue;
            const bool inside = x >= o.x && y >= o.y && x < o.x + d.footW && y < o.y + d.footH;
            if ((m.block(x, y) & Map::kOccupied) && !inside) clear = false;
            if (inside && m.oil(x, y)) clear = false;
          }
        if (!clear) continue;
        const Vec2i c{o.x * 256 + d.footW * 128, o.y * 256 + d.footH * 128};
        const int32_t score = dist(c, want);
        if (score < bestScore) {
          bestScore = score;
          best = c;
        }
      }
    if (best.x >= 0 && r >= 3) break;  // good enough: nearest ring with a spot
  }
  if (best.x < 0) return false;
  Command c;
  c.type = CommandType::PlaceBuilding;
  c.player = p;
  c.unitType = static_cast<uint16_t>(type);
  c.target = best;
  sim.queueInternal(std::move(c));
  return true;
}

void SkirmishAi::manageEconomy(Simulation& sim, uint8_t p) {
  AiPlayer& ai = players_[p];
  const Tuning& tune = kTuning[static_cast<int>(ai.difficulty)];
  Registry& reg = sim.reg_;
  const PlayerState& ps = sim.players_[p];
  const UnitTypeRegistry& types = sim.types_;

  // What do we have?
  std::string faction;
  int roleCount[6] = {};  // yard, power, refinery, barracks, factory, other
  int bunkers = 0, atGuns = 0, towers = 0, trucks = 0, mcvs = 0;
  std::vector<Entity> idleTrucks, mcvList;
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner != p) continue;
    const UnitTypeDef& d = types.get(u.type);
    if (faction.empty()) faction = d.faction;
    if (d.isStructure()) {
      const std::string& r = d.role;
      roleCount[r == "yard" ? 0 : r == "power" ? 1 : (r == "refinery" || r == "depot") ? 2 : r == "barracks" ? 3 : r == "factory" ? 4 : 5]++;
      if (d.model == "bunker") ++bunkers;
      if (d.model == "atgun") ++atGuns;
      if (d.model == "tower") ++towers;
    } else if (d.capacity > 0) {
      ++trucks;
      if (reg.pool<COrder>().get(e).type == OrderType::Idle) idleTrucks.push_back(reg.handle(e));
    } else if (d.deploysTo >= 0) {
      ++mcvs;
      mcvList.push_back(reg.handle(e));
    }
  }
  auto typeOf = [&](const char* model) {
    for (int t = 0; t < static_cast<int>(types.size()); ++t)
      if (types.get(t).faction == faction && types.get(t).model == model) return t;
    return -1;
  };
  auto produce = [&](int type, int amount = 1) {
    if (type < 0 || !sim.canProduce(p, type)) return false;
    Command c;
    c.type = CommandType::Produce;
    c.player = p;
    c.unitType = static_cast<uint16_t>(type);
    c.amount = amount;
    sim.queueInternal(std::move(c));
    return true;
  };

  // Put idle trucks back to work.
  if (!idleTrucks.empty()) {
    Command c;
    c.type = CommandType::Harvest;
    c.player = p;
    c.units = idleTrucks;
    c.target = {-1, -1};
    sim.queueInternal(std::move(c));
  }
  // No yard: unfold a construction truck, or build one.
  if (roleCount[0] == 0) {
    if (!mcvList.empty()) {
      Command c;
      c.type = CommandType::Deploy;
      c.player = p;
      c.units = mcvList;
      sim.queueInternal(std::move(c));
    } else if (roleCount[4] > 0 && ps.queues[static_cast<size_t>(Queue::Vehicles)].items.empty()) {
      produce(typeOf("mcv"));
    }
    return;
  }

  // Place anything that is ready.
  for (Queue q : {Queue::Buildings, Queue::Defenses}) {
    const ProductionQueue& pq = ps.queues[static_cast<size_t>(q)];
    if (pq.ready && !pq.items.empty()) {
      const int t = pq.items.front();
      if (!placeStructure(sim, p, t, q == Queue::Defenses)) {
        // Nowhere to put it: give it up so the line isn't blocked forever.
        Command c;
        c.type = CommandType::Produce;
        c.player = p;
        c.unitType = static_cast<uint16_t>(t);
        c.amount = -1;
        sim.queueInternal(std::move(c));
      }
    }
  }

  // Next structure: power first whenever we run short, then the build order.
  const ProductionQueue& bq = ps.queues[static_cast<size_t>(Queue::Buildings)];
  if (bq.items.empty()) {
    const int powerType = typeOf("power");
    const int margin = ps.powerMade - ps.powerUsed;
    int pick = -1;
    if (margin < 30 && powerType >= 0) pick = powerType;
    else if (roleCount[2] < 1) pick = typeOf("refinery");
    else if (roleCount[3] < 1) pick = typeOf("barracks");
    else if (roleCount[4] < 1) pick = typeOf("factory");
    else if (roleCount[2] < tune.refineries + 1 && ps.credits > 1200) pick = typeOf("refinery");
    else if (roleCount[3] < tune.barracks && ps.credits > 1200) pick = typeOf("barracks");
    else if (roleCount[4] < tune.factories && ps.credits > 2500) pick = typeOf("factory");
    if (pick >= 0) produce(pick);
  }
  const ProductionQueue& dq = ps.queues[static_cast<size_t>(Queue::Defenses)];
  if (dq.items.empty() && ps.credits > 900) {
    if (bunkers < tune.bunkers) produce(typeOf("bunker"));
    else if (atGuns < tune.atGuns) produce(typeOf("atgun"));
    else if (towers < tune.towers) produce(typeOf("tower"));
  }
  // Trucks: a couple per depot.
  const ProductionQueue& vq = ps.queues[static_cast<size_t>(Queue::Vehicles)];
  (void)vq;
  (void)trucks;
  (void)mcvs;
}

void SkirmishAi::manageArmy(Simulation& sim, uint8_t p) {
  AiPlayer& ai = players_[p];
  const Tuning& tune = kTuning[static_cast<int>(ai.difficulty)];
  Registry& reg = sim.reg_;
  const PlayerState& ps = sim.players_[p];
  const UnitTypeRegistry& types = sim.types_;

  // Train a mixed army when there is money to spare.
  std::string faction;
  std::vector<Entity> army, producers;
  int engineers = 0;
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner != p) continue;
    const UnitTypeDef& d = types.get(u.type);
    if (faction.empty()) faction = d.faction;
    if (reg.pool<CProducer>().has(e)) producers.push_back(reg.handle(e));
    if (d.repair > 0) ++engineers;
    if (d.isStructure() || d.capacity > 0 || d.deploysTo >= 0 || d.weapons.empty()) continue;
    army.push_back(reg.handle(e));
  }
  if (ps.credits > tune.reserve) {
    std::vector<int> infantry, vehicles;
    for (int t = 0; t < static_cast<int>(types.size()); ++t) {
      const UnitTypeDef& d = types.get(t);
      if (d.faction != faction || d.weapons.empty() || d.isStructure() || !sim.canProduce(p, t)) continue;
      (d.category == UnitCategory::Infantry ? infantry : vehicles).push_back(t);
    }
    const ProductionQueue& iq = ps.queues[static_cast<size_t>(Queue::Infantry)];
    const ProductionQueue& vq = ps.queues[static_cast<size_t>(Queue::Vehicles)];
    // Pick deterministically, weighted by each type's aiWeight.
    auto weighted = [&](const std::vector<int>& list, uint32_t salt) {
      int32_t total = 0;
      for (int t : list) total += std::max(0, types.get(t).aiWeight);
      if (total <= 0) return -1;
      int32_t r = static_cast<int32_t>(hash32(sim.tick_ * 2654435761u + p * 97u + salt) % static_cast<uint32_t>(total));
      for (int t : list) {
        r -= std::max(0, types.get(t).aiWeight);
        if (r < 0) return t;
      }
      return list.back();
    };
    if (!infantry.empty() && iq.items.size() < 10) {
      const int t = weighted(infantry, 1);
      if (t >= 0) {
        Command c;
        c.type = CommandType::Produce;
        c.player = p;
        c.unitType = static_cast<uint16_t>(t);
        c.amount = 5;  // infantry come as fireteams
        sim.queueInternal(std::move(c));
      }
    }
    if (!vehicles.empty() && vq.items.size() < 2) {
      const int t = weighted(vehicles, 2);
      if (t >= 0) {
        Command c;
        c.type = CommandType::Produce;
        c.player = p;
        c.unitType = static_cast<uint16_t>(t);
        c.amount = 1;
        sim.queueInternal(std::move(c));
      }
    }
    // A couple of engineers to patch up the base.
    if (engineers < tune.engineers && iq.items.empty()) {
      for (int t = 0; t < static_cast<int>(types.size()); ++t) {
        const UnitTypeDef& d = types.get(t);
        if (d.faction == faction && d.repair > 0 && sim.canProduce(p, t)) {
          Command c;
          c.type = CommandType::Produce;
          c.player = p;
          c.unitType = static_cast<uint16_t>(t);
          c.amount = 1;
          sim.queueInternal(std::move(c));
          break;
        }
      }
    }
  }
  // New troops gather at the staging point.
  for (Entity e : producers) {
    const CProducer& pr = reg.get<CProducer>(e);
    if (!pr.hasRally || dist(pr.rally, ai.staging) > 512) {
      Command c;
      c.type = CommandType::SetRally;
      c.player = p;
      c.units = {e};
      c.target = ai.staging;
      sim.queueInternal(std::move(c));
    }
  }

  // Forget fallen wave members.
  std::erase_if(ai.wave, [&](Entity e) { return !reg.alive(e); });
  auto inWave = [&](Entity e) { return std::find(ai.wave.begin(), ai.wave.end(), e) != ai.wave.end(); };

  // Defend: enemies near our structures pull in everyone not attacking.
  Vec2i threat{-1, -1};
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner >= MAX_PLAYERS || sim.allied(u.owner, p) || types.get(u.type).isStructure()) continue;
    const CPosition& c = reg.pool<CPosition>().get(e);
    if (!sim.canSee(p, c.x, c.y)) continue;
    if (dist({c.x, c.y}, ai.base) < 18 * LEPTONS_PER_TILE) {
      threat = {c.x, c.y};
      break;
    }
  }
  std::vector<Entity> home;
  for (Entity e : army)
    if (!inWave(e)) home.push_back(e);
  if (threat.x >= 0 && !home.empty()) {
    Command c;
    c.type = CommandType::AttackMove;
    c.player = p;
    c.target = threat;
    for (Entity e : home)
      if (reg.get<COrder>(e).type == OrderType::Idle || reg.get<COrder>(e).type == OrderType::Move) c.units.push_back(e);
    if (!c.units.empty()) sim.queueInternal(std::move(c));
    return;
  }

  // Attack: once enough troops have gathered (or it's been long enough).
  const int needed = tune.waveSize + tune.waveGrowth * static_cast<int>(ai.waves);
  const bool due = sim.tick_ > ai.lastWaveTick + tune.waveTimeout && home.size() >= static_cast<size_t>(needed / 2);
  if (ai.wave.empty() && (static_cast<int>(home.size()) >= needed || due)) {
    ai.wave = home;
    ++ai.waves;
    ai.lastWaveTick = sim.tick_;
    ai.waveTarget = {-1, -1};
  }
  if (!ai.wave.empty()) {
    // Target: the nearest enemy structure to the wave's centre; failing that, the nearest enemy unit.
    int64_t wx = 0, wy = 0;
    for (Entity e : ai.wave) {
      wx += reg.get<CPosition>(e).x;
      wy += reg.get<CPosition>(e).y;
    }
    const Vec2i centre{static_cast<int32_t>(wx / static_cast<int64_t>(ai.wave.size())),
                       static_cast<int32_t>(wy / static_cast<int64_t>(ai.wave.size()))};
    Vec2i target{-1, -1};
    Entity targetUnit;
    int32_t best = INT32_MAX;
    for (int pass = 0; pass < 2 && target.x < 0; ++pass)
      for (uint32_t e : reg.pool<CUnit>().entities()) {
        const CUnit& u = reg.pool<CUnit>().get(e);
        if (u.owner >= MAX_PLAYERS || sim.allied(u.owner, p)) continue;
        const UnitTypeDef& d = types.get(u.type);
        if (d.category == UnitCategory::Fortification || (pass == 0) != d.isStructure()) continue;
        const CPosition& c = reg.pool<CPosition>().get(e);
        const int32_t dd = dist({c.x, c.y}, centre);
        if (dd < best) {
          best = dd;
          target = {c.x, c.y};
          targetUnit = reg.handle(e);
        }
      }
    if (target.x < 0) {
      ai.wave.clear();  // nothing left to attack
      return;
    }
    // Re-issue when the target changes or units have gone idle.
    std::vector<Entity> idle;
    for (Entity e : ai.wave)
      if (reg.get<COrder>(e).type == OrderType::Idle) idle.push_back(e);
    if (dist(target, ai.waveTarget) > 3 * LEPTONS_PER_TILE || !idle.empty()) {
      const bool newTarget = dist(target, ai.waveTarget) > 3 * LEPTONS_PER_TILE;
      // Units standing idle near the target: those that can hurt it attack it
      // directly (attack-move won't pick a building on its own if it's tough),
      // the rest move on to the nearest enemy troops.
      std::vector<Entity> movers = newTarget ? ai.wave : idle;
      if (!newTarget && reg.alive(targetUnit)) {
        const CUnit& tu = reg.get<CUnit>(targetUnit);
        Command atk;
        atk.type = CommandType::Attack;
        atk.player = p;
        atk.targetUnit = targetUnit;
        std::vector<Entity> rest;
        for (Entity e : movers) {
          const UnitTypeDef& d = types.get(reg.get<CUnit>(e).type);
          bool useful = false;
          for (int w : d.weapons)
            useful = useful || sim.weapons_.damageCenti(sim.weapons_.forTarget(w, types.get(tu.type).armor), types.get(tu.type).armor) > 100;
          const CPosition& ep = reg.get<CPosition>(e);
          if (useful && dist({ep.x, ep.y}, target) < 14 * LEPTONS_PER_TILE) atk.units.push_back(e);
          else rest.push_back(e);
        }
        if (!atk.units.empty()) sim.queueInternal(std::move(atk));
        movers = std::move(rest);
      }
      if (!movers.empty()) {
        Command c;
        c.type = CommandType::AttackMove;
        c.player = p;
        c.target = target;
        c.units = movers;
        c.formation = Formation::Wedge;
        sim.queueInternal(std::move(c));
      }
      ai.waveTarget = target;
      ai.waveTargetUnit = targetUnit;
    }
    // A wave worn down to a handful falls back.
    if (ai.wave.size() * 4 < static_cast<size_t>(needed)) {
      Command c;
      c.type = CommandType::Move;
      c.player = p;
      c.target = ai.staging;
      c.units = ai.wave;
      sim.queueInternal(std::move(c));
      ai.wave.clear();
    }
  }
}

void SkirmishAi::save(ByteWriter& w) const {
  for (const AiPlayer& a : players_) {
    w.u8(a.enabled ? 1 : 0);
    w.u8(static_cast<uint8_t>(a.difficulty));
    w.u32(a.nextThink);
    w.pod(a.base);
    w.pod(a.staging);
    w.pod(a.toward);
    w.u32(a.waves);
    w.u32(a.lastWaveTick);
    w.podVector(a.wave);
    w.pod(a.waveTarget);
    w.pod(a.waveTargetUnit);
  }
}

bool SkirmishAi::load(ByteReader& r) {
  for (AiPlayer& a : players_) {
    a.enabled = r.u8() != 0;
    a.difficulty = static_cast<AiDifficulty>(std::min<uint8_t>(r.u8(), 2));
    a.nextThink = r.u32();
    a.base = r.pod<Vec2i>();
    a.staging = r.pod<Vec2i>();
    a.toward = r.pod<Vec2i>();
    a.waves = r.u32();
    a.lastWaveTick = r.u32();
    a.wave = r.podVector<Entity>();
    a.waveTarget = r.pod<Vec2i>();
    a.waveTargetUnit = r.pod<Entity>();
  }
  return r.ok();
}

}  // namespace slave

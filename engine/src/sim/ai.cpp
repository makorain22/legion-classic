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
  int waveSize;          // strength of the first attack (a man counts 1, a vehicle 4)
  int waveGrowth;        // added per attack
  uint32_t waveTimeout;  // ticks: attack with what it has after this long
  int bunkers, atGuns, towers;
  int refineries, barracks, factories;
  int32_t reserve;       // supplies kept back from army production
  int engineers;         // kept at home to repair
  int guard;             // men in the home guard
  int maxYards;          // construction yards it will run
  int trenchPieces;      // trench line across the approach
};
const Tuning kTuning[3] = {
    {45, 24, 6, 15 * 360, 1, 0, 0, 2, 1, 1, 1500, 0, 5, 1, 0},   // easy
    {20, 36, 9, 15 * 240, 2, 1, 1, 3, 1, 1, 600, 1, 8, 2, 5},    // normal
    {10, 48, 12, 15 * 180, 3, 2, 2, 5, 2, 2, 300, 2, 12, 3, 9},  // hard
};

// How each side fights.
struct Doctrine {
  int infantryShare;  // percent of army orders that are infantry
  int waveScale;      // percent: attack size
  bool stream;        // reinforcements join an attack under way
  int fortify;        // 0..3: how much it digs in
  int raidSize;       // vehicles in a raiding party
  int regroupLoss;    // percent of the launch strength left when it falls back
  int guns;           // extra howitzer emplacements at home
};
Doctrine doctrineFor(const std::string& faction) {
  if (faction == "rus") return {65, 120, true, 1, 4, 30, 0};  // mass, momentum, artillery
  if (faction == "gbr") return {50, 140, false, 2, 3, 40, 1};  // methodical: big set-piece attacks, guns, spare lives
  return {50, 130, false, 3, 4, 30, 0};  // quality, combined arms, fortification
}
// Preferences within the roster, on top of each unit's aiWeight.
int modelWeight(const std::string& faction, const std::string& model) {
  if (faction == "rus") {
    if (model == "rifleman") return 3;
    if (model == "tank") return 3;  // T-34s in numbers
    if (model == "artillery") return 2;
    if (model == "heavy_tank" || model == "tank_destroyer") return 1;
    return 1;
  }
  if (faction == "gbr") {
    if (model == "artillery") return 3;  // the Royal Artillery: Sextons in every attack
    if (model == "tank" || model == "heavy_tank" || model == "tank_destroyer") return 2;
    if (model == "mg_team" || model == "halftrack") return 2;  // Bren and carrier
    return 1;
  }
  if (model == "tank" || model == "heavy_tank" || model == "tank_destroyer") return 2;
  if (model == "mg_team" || model == "at_team" || model == "halftrack") return 2;
  return 1;
}
bool fast(const std::string& model) { return model == "scoutcar" || model == "halftrack" || model == "light_tank"; }

int32_t dist(Vec2i a, Vec2i b) { return static_cast<int32_t>(distance(a.x - b.x, a.y - b.y)); }
constexpr int32_t T = LEPTONS_PER_TILE;
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
  for (int pass = 0; pass < 2 && n == 0; ++pass)
    for (uint32_t e : reg.pool<CUnit>().entities()) {
      const CUnit& u = reg.pool<CUnit>().get(e);
      if (sim.allied(u.owner, p) || u.owner >= MAX_PLAYERS) continue;
      if (pass == 0 && !sim.types_.get(u.type).isStructure()) continue;
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
  ai.staging = {base.x + ai.toward.x * 8, base.y + ai.toward.y * 8};  // behind the field works
  ai.staging.x = std::clamp(ai.staging.x, 512, sim.map_.widthLeptons() - 512);
  ai.staging.y = std::clamp(ai.staging.y, 512, sim.map_.heightLeptons() - 512);
  const bool saving = manageEconomy(sim, p);
  if (sim.tick_ >= ai.lastFortify + 15 * 20) {
    ai.lastFortify = sim.tick_;
    fortify(sim, p);
  }
  manageArmy(sim, p, saving);
}

// Finds a spot for a structure near `want`, inside build range, leaving a lane
// around it so the base never walls itself in.
bool SkirmishAi::placeNear(Simulation& sim, uint8_t p, int type, Vec2i want, int radius) {
  const UnitTypeDef& d = sim.types_.get(type);
  const Map& m = sim.map_;
  Vec2i best{-1, -1};
  int32_t bestScore = INT32_MAX;
  const int cx = want.x >> LEPTON_SHIFT, cy = want.y >> LEPTON_SHIFT;
  for (int r = 0; r <= radius; ++r) {
    for (int oy = -r; oy <= r; ++oy)
      for (int ox = -r; ox <= r; ++ox) {
        if (std::max(std::abs(ox), std::abs(oy)) != r) continue;
        const Vec2i o{cx + ox - d.footW / 2, cy + oy - d.footH / 2};
        if (!sim.canPlace(type, o) || !sim.inBuildRange(p, type, o)) continue;
        bool clear = true;
        for (int y = o.y - 1; y <= o.y + d.footH && clear; ++y)
          for (int x = o.x - 1; x <= o.x + d.footW && clear; ++x) {
            if (!m.inBounds(x, y)) continue;
            const bool inside = x >= o.x && y >= o.y && x < o.x + d.footW && y < o.y + d.footH;
            if ((m.block(x, y) & Map::kOccupied) && !inside) clear = false;
          }
        if (!clear) continue;
        const Vec2i c{o.x * 256 + d.footW * 128, o.y * 256 + d.footH * 128};
        const int32_t score = dist(c, want);
        if (score < bestScore) {
          bestScore = score;
          best = c;
        }
      }
    if (best.x >= 0 && r >= 2) break;  // good enough: nearest ring with a spot
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

bool SkirmishAi::placeStructure(Simulation& sim, uint8_t p, int type, bool defensive) {
  const AiPlayer& ai = players_[p];
  const UnitTypeDef& d = sim.types_.get(type);
  const Map& m = sim.map_;
  if (d.role == "depot") {
    // Supply depots: the nearest free supply point, reaching further as the
    // game goes on - from any of our yards.
    std::vector<Vec2i> yards;
    for (uint32_t e : sim.reg_.pool<CStructure>().entities()) {
      const CUnit& u = sim.reg_.pool<CUnit>().get(e);
      if (u.owner == p && sim.types_.get(u.type).role == "yard")
        yards.push_back({sim.reg_.pool<CPosition>().get(e).x, sim.reg_.pool<CPosition>().get(e).y});
    }
    if (yards.empty()) yards.push_back(ai.base);
    const int32_t reach = std::min<int32_t>(30 * T + static_cast<int32_t>(sim.tick_ / 90) * T, 400 * T);
    std::vector<std::pair<int32_t, Vec2i>> sites;
    for (const Vec2i& sp : m.supplyPoints) {
      int32_t best = INT32_MAX;
      for (const Vec2i& y : yards) best = std::min(best, dist({sp.x * 256 + 128, sp.y * 256 + 128}, y));
      sites.push_back({best, sp});
    }
    std::sort(sites.begin(), sites.end(), [](const auto& a, const auto& b) {
      return a.first != b.first ? a.first < b.first : (a.second.y != b.second.y ? a.second.y < b.second.y : a.second.x < b.second.x);
    });
    for (const auto& [score, sp] : sites) {
      if (score > reach) break;
      // Centred in the compound first.
      for (int ring = 0; ring <= 2; ++ring)
        for (int oy = -d.footH; oy <= 1; ++oy)
          for (int ox = -d.footW; ox <= 1; ++ox) {
            if (std::max(std::abs(ox + d.footW / 2), std::abs(oy + d.footH / 2)) != ring) continue;
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
  // Production and power in rows behind the headquarters; defences out front.
  const Vec2i side{-ai.toward.y, ai.toward.x};
  Vec2i want = ai.base;
  if (defensive) {
    // Spread along the front: left, right, centre in turn.
    const int slot = static_cast<int>(sim.tick_ / 300 % 3) - 1;
    want = {ai.base.x + ai.toward.x * 10 + side.x * slot * 6, ai.base.y + ai.toward.y * 10 + side.y * slot * 6};
  } else if (d.role == "power") {
    want = {ai.base.x - ai.toward.x * 6 - side.x * 4, ai.base.y - ai.toward.y * 6 - side.y * 4};
  } else if (d.role == "barracks" || d.role == "factory") {
    want = {ai.base.x + ai.toward.x * 4 + side.x * (d.role == "factory" ? 6 : -6),
            ai.base.y + ai.toward.y * 4 + side.y * (d.role == "factory" ? 6 : -6)};
  }
  return placeNear(sim, p, type, want, 16);
}

bool SkirmishAi::manageEconomy(Simulation& sim, uint8_t p) {
  AiPlayer& ai = players_[p];
  const Tuning& tune = kTuning[static_cast<int>(ai.difficulty)];
  Registry& reg = sim.reg_;
  const PlayerState& ps = sim.players_[p];
  const UnitTypeRegistry& types = sim.types_;

  // What do we have?
  std::string faction;
  int roleCount[6] = {};  // yard, power, depot, barracks, factory, other
  int bunkers = 0, atGuns = 0, towers = 0, nests = 0, guns = 0;
  std::vector<Entity> mcvList;
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
      if (d.model == "mg_nest") ++nests;
      if (d.model == "howitzer_pit") ++guns;
    } else if (d.deploysTo >= 0) {
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

  // Construction trucks: unfold where we have no yard, or drive out to a new
  // site and unfold there.
  if (!mcvList.empty()) {
    if (roleCount[0] == 0 || ai.expandTo.x < 0) {
      Command c;
      c.type = CommandType::Deploy;
      c.player = p;
      c.units = mcvList;
      sim.queueInternal(std::move(c));
    } else {
      for (Entity m : mcvList) {
        const CPosition& mp = reg.get<CPosition>(m);
        if (dist({mp.x, mp.y}, ai.expandTo) < 4 * T) {
          Command c;
          c.type = CommandType::Deploy;
          c.player = p;
          c.units = {m};
          sim.queueInternal(std::move(c));
          ai.expandTo = {-1, -1};
        } else if (reg.get<COrder>(m).type == OrderType::Idle) {
          Command c;
          c.type = CommandType::Move;
          c.player = p;
          c.units = {m};
          c.target = ai.expandTo;
          sim.queueInternal(std::move(c));
        }
      }
    }
  }
  if (roleCount[0] == 0) {
    if (mcvList.empty() && roleCount[4] > 0 && ps.queues[static_cast<size_t>(Queue::Vehicles)].items.empty()) produce(typeOf("mcv"));
    return true;
  }

  // Place anything that is ready.
  for (Queue q : {Queue::Buildings, Queue::Defenses}) {
    const ProductionQueue& pq = ps.queues[static_cast<size_t>(q)];
    if (pq.ready && !pq.items.empty()) {
      const int t = pq.items.front();
      bool placed = false;
      if (q == Queue::Defenses && types.get(t).model == "mg_nest") {
        // MG nests guard our far-flung depots first.
        for (uint32_t e : reg.pool<CStructure>().entities()) {
          const CUnit& u = reg.pool<CUnit>().get(e);
          if (u.owner != p || types.get(u.type).role != "depot") continue;
          const CPosition& c = reg.pool<CPosition>().get(e);
          if (dist({c.x, c.y}, ai.base) < 22 * T) continue;
          bool guarded = false;
          for (uint32_t o : reg.pool<CStructure>().entities()) {
            const CUnit& ou = reg.pool<CUnit>().get(o);
            if (ou.owner != p || !types.get(ou.type).weapons.size()) continue;
            const CPosition& oc = reg.pool<CPosition>().get(o);
            if (dist({oc.x, oc.y}, {c.x, c.y}) < 7 * T) guarded = true;
          }
          if (guarded) continue;
          const Vec2i want{c.x + ai.toward.x * 4, c.y + ai.toward.y * 4};
          if (placeNear(sim, p, t, want, 6)) {
            placed = true;
            break;
          }
        }
      }
      if (!placed && !placeStructure(sim, p, t, q == Queue::Defenses)) {
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

  // Next structure: power first whenever we run short, then the build order;
  // more depots as the game goes on.
  const ProductionQueue& bq = ps.queues[static_cast<size_t>(Queue::Buildings)];
  const int minutes = static_cast<int>(sim.tick_ / (TICKS_PER_SECOND * 60));
  if (bq.items.empty()) {
    const int powerType = typeOf("power");
    const int margin = ps.powerMade - ps.powerUsed;
    int pick = -1;
    if (margin < 30 && powerType >= 0) pick = powerType;
    else if (roleCount[2] < 1) pick = typeOf("refinery");
    else if (roleCount[3] < 1) pick = typeOf("barracks");
    else if (roleCount[4] < 1) pick = typeOf("factory");
    else if (roleCount[2] < std::min(tune.refineries + minutes / 3, 10)) pick = typeOf("refinery");
    else if (roleCount[3] < tune.barracks && ps.credits > 1200) pick = typeOf("barracks");
    else if (roleCount[4] < tune.factories && ps.credits > 2500) pick = typeOf("factory");
    if (pick >= 0) produce(pick);
  }
  // A second (and third) yard further out, once the base is up.
  const int mcvType = typeOf("mcv");
  const auto& vItems = ps.queues[static_cast<size_t>(Queue::Vehicles)].items;
  const bool mcvQueued = std::find(vItems.begin(), vItems.end(), mcvType) != vItems.end();
  if (roleCount[0] < tune.maxYards && roleCount[4] > 0 && roleCount[2] >= 3 && mcvList.empty() && !mcvQueued && minutes >= 6) {
    // Somewhere with free supply points, away from our base, not toward the enemy.
    Vec2i best{-1, -1};
    int32_t bestScore = INT32_MIN;
    for (const Vec2i& sp : sim.map_.supplyPoints) {
      const Vec2i c{sp.x * 256 + 128, sp.y * 256 + 128};
      bool taken = false;
      for (size_t i = 0; i < sim.map_.supplyPoints.size(); ++i)
        if (sim.map_.supplyPoints[i] == sp && sim.depotOn(static_cast<int>(i)).valid()) taken = true;
      const int32_t d = dist(c, ai.base);
      if (taken || d < 28 * T || d > 70 * T) continue;
      // Ahead is risky: score sideways and rearward sites higher.
      const int64_t ahead = (static_cast<int64_t>(c.x - ai.base.x) * ai.toward.x + static_cast<int64_t>(c.y - ai.base.y) * ai.toward.y) / 256;
      const int32_t score = -static_cast<int32_t>(ahead) - d / 4;
      if (score > bestScore) {
        bestScore = score;
        best = c;
      }
    }
    if (best.x >= 0 && produce(mcvType)) ai.expandTo = {best.x - ai.toward.x * 3, best.y - ai.toward.y * 3};
  }
  const ProductionQueue& dq = ps.queues[static_cast<size_t>(Queue::Defenses)];
  const Doctrine doc = doctrineFor(faction);
  // A watchtower unlocks the heavy kit (guns, snipers, heavy tanks): a side
  // that lives by its guns puts one up as soon as it has a barracks.
  if (dq.items.empty() && towers == 0 && roleCount[3] > 0 && ps.credits > 300 && (doc.guns > 0 || minutes >= 8))
    produce(typeOf("tower"));
  else if (dq.items.empty() && ps.credits > 700) {
    const int farDepots = std::max(0, roleCount[2] - 1);
    if (nests < std::min(farDepots, 6) + (doc.fortify >= 2 ? 2 : 1)) produce(typeOf("mg_nest"));
    else if (bunkers < tune.bunkers) produce(typeOf("bunker"));
    else if (atGuns < tune.atGuns + (doc.fortify >= 3 ? 1 : 0)) produce(typeOf("atgun"));
    else if (towers < tune.towers) produce(typeOf("tower"));
    else if (guns < (ai.difficulty == AiDifficulty::Hard ? 1 : 0) + (ai.difficulty != AiDifficulty::Easy ? doc.guns : 0) &&
             ps.credits > 2500)
      produce(typeOf("howitzer_pit"));
  }
  // Expansion comes first: while a depot, a yard or power is on order, the
  // army holds back new orders so the work isn't starved of supplies.
  return (!bq.items.empty() && !bq.ready) || mcvQueued;
}

// Field works across the approach: a trench line for the home guard, wire in
// front of it, tank traps on the way in.
void SkirmishAi::fortify(Simulation& sim, uint8_t p) {
  AiPlayer& ai = players_[p];
  const Tuning& tune = kTuning[static_cast<int>(ai.difficulty)];
  Registry& reg = sim.reg_;
  const UnitTypeRegistry& types = sim.types_;
  std::string faction;
  int trenches = 0, wire = 0, traps = 0;
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner != p) continue;
    const UnitTypeDef& d = types.get(u.type);
    if (faction.empty()) faction = d.faction;
    trenches += d.model == "trench";
    wire += d.model == "wire";
    traps += d.model == "traps";
  }
  const Doctrine doc = doctrineFor(faction);
  if (doc.fortify == 0 || tune.trenchPieces == 0 || sim.players_[p].credits < 60) return;  // field works are cheap
  auto typeOf = [&](const char* model) {
    for (int t = 0; t < static_cast<int>(types.size()); ++t)
      if (types.get(t).faction == faction && types.get(t).model == model) return t;
    return -1;
  };
  const Vec2i side{-ai.toward.y, ai.toward.x};
  // A line `n` pieces long across the approach, `ahead` tiles out.
  auto line = [&](const char* model, int ahead, int n, int gap) {
    const int t = typeOf(model);
    if (t < 0 || !sim.canProduce(p, t)) return false;
    Command c;
    c.type = CommandType::PlaceWalls;
    c.player = p;
    c.unitType = static_cast<uint16_t>(t);
    for (int i = -n / 2; i <= n / 2; ++i) {
      if (gap > 0 && std::abs(i) < gap) continue;  // a way through
      const Vec2i at{ai.base.x + ai.toward.x * ahead + side.x * i, ai.base.y + ai.toward.y * ahead + side.y * i};
      const Vec2i o = sim.footprintOrigin(t, at);
      if (!sim.canPlace(t, o) || !sim.inBuildRange(p, t, o)) continue;  // (out of range would be refused)
      c.spawns.push_back({static_cast<uint16_t>(t), p, {o.x * 256 + 128, o.y * 256 + 128}, 0, -1});
    }
    if (c.spawns.empty()) return false;  // already built (or no room): on to the next line
    sim.queueInternal(std::move(c));
    return true;
  };
  const int pieces = tune.trenchPieces + (doc.fortify >= 3 ? 4 : 0);
  if (trenches < pieces && line("trench", 11, pieces + 1, 0)) return;
  if (doc.fortify >= 2 && wire < pieces && line("wire", 13, pieces + 4, 3)) return;
  if (doc.fortify >= 3 && traps < 6) line("traps", 15, 12, 4);
}

void SkirmishAi::manageArmy(Simulation& sim, uint8_t p, bool saving) {
  AiPlayer& ai = players_[p];
  const Tuning& tune = kTuning[static_cast<int>(ai.difficulty)];
  Registry& reg = sim.reg_;
  const PlayerState& ps = sim.players_[p];
  const UnitTypeRegistry& types = sim.types_;

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
  const Doctrine doc = doctrineFor(faction);
  auto modelOf = [&](Entity e) -> const std::string& { return types.get(reg.get<CUnit>(e).type).model; };
  auto isInfantry = [&](Entity e) { return types.get(reg.get<CUnit>(e).type).infantry(); };
  auto strength = [&](const std::vector<Entity>& units) {
    int32_t s = 0;
    for (Entity e : units) s += isInfantry(e) ? 1 : 4;
    return s;
  };
  auto centreOf = [&](const std::vector<Entity>& units) {
    int64_t x = 0, y = 0;
    for (Entity e : units) {
      x += reg.get<CPosition>(e).x;
      y += reg.get<CPosition>(e).y;
    }
    const int64_t n = std::max<int64_t>(1, static_cast<int64_t>(units.size()));
    return Vec2i{static_cast<int32_t>(x / n), static_cast<int32_t>(y / n)};
  };
  auto order = [&](CommandType type, const std::vector<Entity>& units, Vec2i target, Formation f = Formation::Box) {
    if (units.empty()) return;
    Command c;
    c.type = type;
    c.player = p;
    c.units = units;
    c.target = target;
    c.formation = f;
    sim.queueInternal(std::move(c));
  };
  auto bind = [&](const std::vector<Entity>& units) {
    if (units.size() < 5) return;
    Command c;
    c.type = CommandType::BindGroup;
    c.player = p;
    c.units = units;
    sim.queueInternal(std::move(c));
  };

  // --- Production, to the side's doctrine --------------------------------------
  if (ps.credits > tune.reserve) {
    std::vector<int> infantry, vehicles;
    for (int t = 0; t < static_cast<int>(types.size()); ++t) {
      const UnitTypeDef& d = types.get(t);
      if (d.faction != faction || d.weapons.empty() || d.isStructure() || !sim.canProduce(p, t)) continue;
      (d.category == UnitCategory::Infantry ? infantry : vehicles).push_back(t);
    }
    const ProductionQueue& iq = ps.queues[static_cast<size_t>(Queue::Infantry)];
    const ProductionQueue& vq = ps.queues[static_cast<size_t>(Queue::Vehicles)];
    // Short of raiders: favour the fast vehicles for a while.
    int fastCount = 0;
    for (Entity e : army) fastCount += fast(modelOf(e));
    const int fastBoost = fastCount < doc.raidSize ? 3 : 1;
    auto weightOf = [&](int t) {
      const UnitTypeDef& d = types.get(t);
      return std::max(0, d.aiWeight) * modelWeight(faction, d.model) * (fast(d.model) ? fastBoost : 1);
    };
    auto weighted = [&](const std::vector<int>& list, uint32_t salt) {
      int32_t total = 0;
      for (int t : list) total += weightOf(t);
      if (total <= 0) return -1;
      int32_t r = static_cast<int32_t>(hash32(sim.tick_ * 2654435761u + p * 97u + salt) % static_cast<uint32_t>(total));
      for (int t : list) {
        r -= weightOf(t);
        if (r < 0) return t;
      }
      return list.back();
    };
    const bool wantInfantry = static_cast<int>(hash32(sim.tick_ * 7919u + p) % 100) < doc.infantryShare;
    auto produce = [&](int t, int amount) {
      Command c;
      c.type = CommandType::Produce;
      c.player = p;
      c.unitType = static_cast<uint16_t>(t);
      c.amount = amount;
      sim.queueInternal(std::move(c));
    };
    if (!infantry.empty() && iq.items.size() < (saving ? 3u : wantInfantry ? 15u : 5u)) {
      const int t = weighted(infantry, 1);
      if (t >= 0) produce(t, 5);  // infantry come as fireteams
    }
    if (!vehicles.empty() && !saving && vq.items.size() < (wantInfantry ? 1u : 3u)) {
      const int t = weighted(vehicles, 2);
      if (t >= 0) produce(t, 1);
    }
    if (engineers < tune.engineers && iq.items.empty())
      for (int t = 0; t < static_cast<int>(types.size()); ++t) {
        const UnitTypeDef& d = types.get(t);
        if (d.faction == faction && d.repair > 0 && sim.canProduce(p, t)) {
          produce(t, 1);
          break;
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

  // --- Who is where ----------------------------------------------------------------
  auto alive = [&](Entity e) { return reg.alive(e) && reg.has<CUnit>(e) && reg.get<CUnit>(e).owner == p; };
  std::erase_if(ai.wave, [&](Entity e) { return !alive(e); });
  std::erase_if(ai.raiders, [&](Entity e) { return !alive(e); });
  std::erase_if(ai.guard, [&](Entity e) { return !alive(e); });
  auto in = [](const std::vector<Entity>& v, Entity e) { return std::find(v.begin(), v.end(), e) != v.end(); };

  // Damaged vehicles head home for repairs (engineers patch them up there).
  std::vector<Entity> limping;
  for (Entity e : army) {
    if (isInfantry(e)) continue;
    const CUnit& u = reg.get<CUnit>(e);
    if (u.health * 100 < types.get(u.type).maxHealth * 35 && dist({reg.get<CPosition>(e).x, reg.get<CPosition>(e).y}, ai.base) > 10 * T) {
      limping.push_back(e);
      std::erase(ai.wave, e);
      std::erase(ai.raiders, e);
    }
  }
  if (!limping.empty() && sim.tick_ % (TICKS_PER_SECOND * 6) < tune.thinkTicks) order(CommandType::Move, limping, ai.base);

  std::vector<Entity> home;
  for (Entity e : army)
    if (!in(ai.wave, e) && !in(ai.raiders, e) && !in(ai.guard, e) && !in(limping, e)) home.push_back(e);

  // The home guard: infantry kept back to man the base's trenches and bunkers.
  for (Entity e : home)
    if (static_cast<int>(ai.guard.size()) < tune.guard * (2 + std::min(doc.fortify, 2)) / 4 && isInfantry(e) && modelOf(e) != "sniper") ai.guard.push_back(e);
  std::erase_if(home, [&](Entity e) { return in(ai.guard, e); });
  {
    // Idle guards go into the nearest trench or bunker with room.
    std::vector<Entity> idle;
    for (Entity e : ai.guard)
      if (!reg.has<CGarrisoned>(e) && reg.get<COrder>(e).type == OrderType::Idle) idle.push_back(e);
    if (!idle.empty()) {
      Entity best;
      int32_t bestD = INT32_MAX;
      for (uint32_t s : reg.pool<CStructure>().entities()) {
        const CUnit& su = reg.pool<CUnit>().get(s);
        if (su.owner != p) continue;
        const UnitTypeDef& sd = types.get(su.type);
        if (sd.garrison <= 0) continue;
        const CGarrison* g = reg.pool<CGarrison>().tryGet(s);
        if (g && static_cast<int32_t>(g->occupants.size()) >= sd.garrison) continue;
        const CPosition& sp = reg.pool<CPosition>().get(s);
        const int32_t d = dist({sp.x, sp.y}, ai.staging);
        if (d < bestD && dist({sp.x, sp.y}, ai.base) < 24 * T) {
          bestD = d;
          best = reg.handle(s);
        }
      }
      if (best.valid()) {
        Command c;
        c.type = CommandType::Occupy;
        c.player = p;
        c.units = idle;
        c.targetUnit = best;
        sim.queueInternal(std::move(c));
      }
    }
  }

  // --- Defence: enemies near any of our structures ---------------------------------
  Vec2i threat{-1, -1};
  int threatSize = 0;
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner >= MAX_PLAYERS || sim.allied(u.owner, p) || types.get(u.type).isStructure()) continue;
    const CPosition& c = reg.pool<CPosition>().get(e);
    if (!sim.canSee(p, c.x, c.y)) continue;
    if (dist({c.x, c.y}, ai.base) < 20 * T) {
      if (threat.x < 0) threat = {c.x, c.y};
      threatSize += types.get(u.type).infantry() ? 1 : 4;
    }
  }
  if (threat.x >= 0) {
    std::vector<Entity> responders;
    for (Entity e : home)
      if (reg.get<COrder>(e).type == OrderType::Idle || reg.get<COrder>(e).type == OrderType::Move) responders.push_back(e);
    // A real attack on the base brings the main force home if it isn't committed.
    if (threatSize > strength(home) && ai.phase != 1)
      for (Entity e : ai.wave) responders.push_back(e);
    order(CommandType::AttackMove, responders, threat);
    if (threatSize > strength(home) && ai.phase != 1) {
      ai.wave.clear();
      ai.phase = 0;
    }
  }

  // --- Raiders: fast vehicles hunting depots and stragglers ---------------------------
  if (ai.raiders.empty() && sim.tick_ >= ai.raidReady && threat.x < 0) {
    std::vector<Entity> pick;
    for (Entity e : home)
      if (!isInfantry(e) && fast(modelOf(e)) && static_cast<int>(pick.size()) < doc.raidSize) pick.push_back(e);
    if (static_cast<int>(pick.size()) >= std::max(2, doc.raidSize - 1)) {
      ai.raiders = pick;
      ai.raidTarget = {-1, -1};
      bind(pick);
      std::erase_if(home, [&](Entity e) { return in(ai.raiders, e); });
    }
  }
  if (!ai.raiders.empty()) {
    const Vec2i rc = centreOf(ai.raiders);
    // Target: the enemy depot (else any structure) with the fewest defenders near it.
    Vec2i best{-1, -1};
    int32_t bestScore = INT32_MAX;
    for (uint32_t e : reg.pool<CStructure>().entities()) {
      const CUnit& u = reg.pool<CUnit>().get(e);
      if (u.owner >= MAX_PLAYERS || sim.allied(u.owner, p) || u.health <= 0) continue;
      const UnitTypeDef& d = types.get(u.type);
      if (d.category == UnitCategory::Fortification) continue;
      const CPosition& c = reg.pool<CPosition>().get(e);
      int defenders = 0;
      for (uint32_t o : reg.pool<CUnit>().entities()) {
        const CUnit& ou = reg.pool<CUnit>().get(o);
        if (ou.owner != u.owner || types.get(ou.type).weapons.empty()) continue;
        const CPosition& oc = reg.pool<CPosition>().get(o);
        if (std::abs(oc.x - c.x) < 9 * T && std::abs(oc.y - c.y) < 9 * T) defenders += types.get(ou.type).isStructure() ? 6 : types.get(ou.type).infantry() ? 1 : 4;
      }
      const int32_t score = defenders * 8 * T + dist({c.x, c.y}, rc) / 2 - (d.role == "depot" ? 30 * T : 0);
      if (score < bestScore) {
        bestScore = score;
        best = {c.x, c.y};
      }
    }
    const bool spent = strength(ai.raiders) * 2 < doc.raidSize * 4;  // half gone: home
    if (best.x < 0 || spent) {
      order(CommandType::Move, ai.raiders, ai.staging);
      ai.raiders.clear();
      ai.raidReady = sim.tick_ + TICKS_PER_SECOND * 50;
    } else {
      bool idle = false;
      for (Entity e : ai.raiders) idle = idle || reg.get<COrder>(e).type == OrderType::Idle;
      if (dist(best, ai.raidTarget) > 3 * T || idle) {
        order(CommandType::AttackMove, ai.raiders, best, Formation::Column);
        ai.raidTarget = best;
      }
    }
  }

  // --- The main force ----------------------------------------------------------------
  const int32_t needed = (tune.waveSize + tune.waveGrowth * static_cast<int>(std::min<uint32_t>(ai.waves, 8))) * doc.waveScale / 100;
  // Fast vehicles stay back for raiding.
  std::erase_if(home, [&](Entity e) { return fast(modelOf(e)); });
  if (ai.phase == 0) {
    // Gathering. Attack once strong enough (or it's been long enough).
    const bool due = sim.tick_ > ai.lastWaveTick + tune.waveTimeout && strength(home) >= needed / 2;
    if ((strength(home) >= needed || due) && threat.x < 0 && !home.empty()) {
      ai.wave = home;
      home.clear();
      ++ai.waves;
      ai.lastWaveTick = sim.tick_;
      ai.phaseTick = sim.tick_;
      ai.phase = 1;
      ai.launchStrength = strength(ai.wave);
      ai.waveTarget = {-1, -1};
      bind(ai.wave);  // into a battle group
      // Every other attack goes in by a flank.
      const Vec2i c = centreOf(ai.wave);
      const Vec2i side{-ai.toward.y, ai.toward.x};
      const int s = (ai.waves % 2 == 0) ? 1 : -1;
      ai.flank = (ai.waves % 3 != 1) ? Vec2i{c.x + ai.toward.x * 16 + side.x * s * 14, c.y + ai.toward.y * 16 + side.y * s * 14}
                                     : Vec2i{-1, -1};
      if (ai.flank.x >= 0) {
        ai.flank.x = std::clamp(ai.flank.x, 3 * T, sim.map_.widthLeptons() - 3 * T);
        ai.flank.y = std::clamp(ai.flank.y, 3 * T, sim.map_.heightLeptons() - 3 * T);
      }
    }
  }
  if (ai.phase != 0 && ai.wave.empty()) ai.phase = 0;
  if (ai.phase == 1 || ai.phase == 2) {
    // Fresh troops: the Soviets feed them into the attack as they arrive; the
    // Germans send them on to the regrouping point, or forward as a bound
    // second echelon once there are enough of them.
    const int32_t echelon = doc.stream || ai.phase == 2 ? 5 : std::max(12, needed / 3);
    if (!home.empty() && strength(home) >= echelon) {
      std::vector<Entity> fresh;
      for (Entity e : home)
        if (reg.get<COrder>(e).type == OrderType::Idle) fresh.push_back(e);
      if (!fresh.empty()) {
        if (!doc.stream) bind(fresh);
        order(CommandType::AttackMove, fresh, ai.phase == 2 && ai.forward.x >= 0 ? ai.forward : centreOf(ai.wave), Formation::Wedge);
        for (Entity e : fresh) ai.wave.push_back(e);
      }
    }
  }
  if (ai.phase == 1 && !ai.wave.empty()) {
    const Vec2i centre = centreOf(ai.wave);
    // Badly hurt: fall back a little, dig in and regroup - don't run home.
    if (strength(ai.wave) * 100 < ai.launchStrength * doc.regroupLoss) {
      ai.phase = 2;
      ai.phaseTick = sim.tick_;
      ai.forward = {centre.x - ai.toward.x * 7, centre.y - ai.toward.y * 7};
      ai.forward.x = std::clamp(ai.forward.x, 2 * T, sim.map_.widthLeptons() - 2 * T);
      ai.forward.y = std::clamp(ai.forward.y, 2 * T, sim.map_.heightLeptons() - 2 * T);
      order(CommandType::Move, ai.wave, ai.forward, Formation::Line);
      return;
    }
    // A flanking approach first.
    if (ai.flank.x >= 0) {
      if (dist(centre, ai.flank) < 5 * T || sim.tick_ > ai.phaseTick + TICKS_PER_SECOND * 50) {
        ai.flank = {-1, -1};
        ai.waveTarget = {-1, -1};
      } else {
        if (ai.waveTarget != ai.flank) {
          order(CommandType::AttackMove, ai.wave, ai.flank, Formation::Wedge);
          ai.waveTarget = ai.flank;
        }
        return;
      }
    }
    // Target: the nearest enemy structure to the force; failing that, the nearest enemy unit.
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
      ai.phase = 0;
      return;
    }
    std::vector<Entity> idle;
    for (Entity e : ai.wave)
      if (reg.get<COrder>(e).type == OrderType::Idle) idle.push_back(e);
    const bool newTarget = dist(target, ai.waveTarget) > 3 * T;
    if (newTarget || !idle.empty()) {
      std::vector<Entity> movers = newTarget ? ai.wave : idle;
      if (!newTarget && reg.alive(targetUnit)) {
        // Units idle near the target attack it directly if they can hurt it.
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
          if (useful && dist({ep.x, ep.y}, target) < 14 * T) atk.units.push_back(e);
          else rest.push_back(e);
        }
        if (!atk.units.empty()) sim.queueInternal(std::move(atk));
        movers = std::move(rest);
      }
      order(CommandType::AttackMove, movers, target, Formation::Wedge);
      ai.waveTarget = target;
      ai.waveTargetUnit = targetUnit;
    }
  } else if (ai.phase == 2 && !ai.wave.empty()) {
    // Regrouping: infantry take cover in buildings or trenches nearby; go
    // again once strong enough, or after a minute.
    if (sim.tick_ % (TICKS_PER_SECOND * 10) < tune.thinkTicks) {
      std::vector<Entity> men;
      for (Entity e : ai.wave)
        if (isInfantry(e) && !reg.has<CGarrisoned>(e) && reg.get<COrder>(e).type == OrderType::Idle) men.push_back(e);
      if (!men.empty()) {
        Entity best;
        int32_t bestD = 8 * T;
        for (uint32_t s : reg.pool<CStructure>().entities()) {
          const CUnit& su = reg.pool<CUnit>().get(s);
          const UnitTypeDef& sd = types.get(su.type);
          if (sd.garrison <= 0 || (su.owner < MAX_PLAYERS && su.owner != p)) continue;
          const CPosition& sp = reg.pool<CPosition>().get(s);
          const int32_t d = dist({sp.x, sp.y}, ai.forward);
          if (d < bestD) {
            bestD = d;
            best = reg.handle(s);
          }
        }
        if (best.valid()) {
          Command c;
          c.type = CommandType::Occupy;
          c.player = p;
          c.units = men;
          c.targetUnit = best;
          sim.queueInternal(std::move(c));
        }
      }
    }
    const uint32_t waited = sim.tick_ - ai.phaseTick;
    if ((waited > TICKS_PER_SECOND * 20 && strength(ai.wave) >= needed * 7 / 10) || waited > TICKS_PER_SECOND * 60) {
      // Out of cover and on again.
      Command out;
      out.type = CommandType::Unload;
      out.player = p;
      out.units = ai.wave;
      sim.queueInternal(std::move(out));
      ai.phase = 1;
      ai.phaseTick = sim.tick_;
      ai.launchStrength = strength(ai.wave);
      ai.waveTarget = {-1, -1};
      ai.flank = {-1, -1};
      bind(ai.wave);
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
    w.u8(a.phase);
    w.u32(a.phaseTick);
    w.u32(static_cast<uint32_t>(a.launchStrength));
    w.pod(a.flank);
    w.pod(a.forward);
    w.podVector(a.raiders);
    w.pod(a.raidTarget);
    w.u32(a.raidReady);
    w.podVector(a.guard);
    w.u32(a.lastFortify);
    w.pod(a.expandTo);
  }
}

bool SkirmishAi::load(ByteReader& r, int version) {
  for (AiPlayer& a : players_) {
    a = AiPlayer{};
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
    if (version >= 3) {
      a.phase = r.u8();
      a.phaseTick = r.u32();
      a.launchStrength = static_cast<int32_t>(r.u32());
      a.flank = r.pod<Vec2i>();
      a.forward = r.pod<Vec2i>();
      a.raiders = r.podVector<Entity>();
      a.raidTarget = r.pod<Vec2i>();
      a.raidReady = r.u32();
      a.guard = r.podVector<Entity>();
      a.lastFortify = r.u32();
      a.expandTo = r.pod<Vec2i>();
    } else if (!a.wave.empty()) {
      a.phase = 1;
    }
  }
  return r.ok();
}

}  // namespace slave

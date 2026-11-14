// Pioneer Engine - the computer player. See ai.hpp.
#include "pioneer/sim/ai.hpp"
#include "pioneer/core/rng.hpp"

#include <algorithm>
#include <string>

#include "pioneer/core/bytes.hpp"
#include "pioneer/sim/simulation.hpp"

namespace pioneer {

namespace {
struct Tuning {
  uint32_t thinkTicks;   // how often it thinks
  int waveSize;          // strength of the first attack (a man counts 1, a vehicle 4)
  int waveGrowth;        // added per attack
  uint32_t waveTimeout;  // ticks: attack with what it has after this long
  int bunkers, atGuns, towers;
  int refineries, barracks, factories;  // refineries: income buildings it starts with (more as the game goes on)
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
  if (faction == "rus") return {65, 120, true, 1, 6, 30, 0};  // mass, momentum, artillery
  if (faction == "gbr") return {50, 140, false, 2, 5, 40, 1};  // methodical: big set-piece attacks, guns, spare lives
  return {50, 130, false, 3, 5, 30, 0};  // quality, combined arms, fortification
}
// Preferences within the roster, on top of each unit's aiWeight.
int modelWeight(const std::string& faction, const std::string& model) {
  if (faction == "rus") {
    if (model == "rifleman") return 3;
    if (model == "levy") return 4;  // conscripts by the hundred
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
  ai.hasBase = base.x >= 0;
  if (base.x < 0)
    for (uint32_t e : reg.pool<CUnit>().entities())
      if (reg.pool<CUnit>().get(e).owner == p && sim.aiMayCommand(e)) {
        const CPosition& c = reg.pool<CPosition>().get(e);
        base = {c.x, c.y};
        break;
      }
  if (base.x < 0) return;
  ai.base = base;
  // Who is the enemy? Each hostile side stands where its buildings are (its
  // units, if it has none). The nearest is the main enemy, and stays so until
  // it is beaten: the main force goes for that one, the raiders for the rest.
  struct Foe {
    int64_t x = 0, y = 0, n = 0;
  };
  Foe built[MAX_PLAYERS], any[MAX_PLAYERS];
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner >= MAX_PLAYERS || sim.allied(u.owner, p)) continue;
    const UnitTypeDef& d = sim.types_.get(u.type);
    if (d.category == UnitCategory::Fortification) continue;  // wire and trenches alone are nobody
    const CPosition& c = reg.pool<CPosition>().get(e);
    Foe& f = (d.isStructure() ? built : any)[u.owner];
    f.x += c.x;
    f.y += c.y;
    ++f.n;
  }
  auto standsAt = [&](uint8_t q, Vec2i& at) {
    const Foe& f = built[q].n ? built[q] : any[q];
    if (!f.n) return false;
    at = {static_cast<int32_t>(f.x / f.n), static_cast<int32_t>(f.y / f.n)};
    return true;
  };
  Vec2i enemyAt{-1, -1};
  if (ai.mainEnemy >= MAX_PLAYERS || !standsAt(ai.mainEnemy, enemyAt)) {
    ai.mainEnemy = 0xFF;
    int32_t nearest = INT32_MAX;
    for (uint8_t q = 0; q < MAX_PLAYERS; ++q) {
      Vec2i at;
      if (!standsAt(q, at)) continue;
      const int32_t d = dist(at, base);
      if (d < nearest) {
        nearest = d;
        ai.mainEnemy = q;
        enemyAt = at;
      }
    }
  }
  ai.enemyAt = enemyAt;
  if (enemyAt.x >= 0) {
    const int32_t dx = enemyAt.x - base.x, dy = enemyAt.y - base.y;
    const uint32_t len = std::max<uint32_t>(1, distance(dx, dy));
    ai.toward = {static_cast<int32_t>(static_cast<int64_t>(dx) * 256 / len), static_cast<int32_t>(static_cast<int64_t>(dy) * 256 / len)};
  }
  ai.staging = {base.x + ai.toward.x * 8, base.y + ai.toward.y * 8};  // behind the field works
  ai.staging.x = std::clamp(ai.staging.x, 512, sim.map_.widthLeptons() - 512);
  ai.staging.y = std::clamp(ai.staging.y, 512, sim.map_.heightLeptons() - 512);
  if (ai.order == 2 && ai.orderAt.x >= 0) ai.staging = ai.orderAt;  // a script has it hold a place
  const bool saving = allowed(p, kAiBuild) ? manageEconomy(sim, p) : false;
  if (allowed(p, kAiFortify) && sim.tick_ >= ai.lastFortify + 15 * 20) {
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
  } else if (d.role == "post" || d.role == "warehouse" || d.role == "fuel") {
    // The money-makers stand behind the headquarters, away from the enemy.
    const int s = d.role == "post" ? -1 : d.role == "warehouse" ? 1 : 0;
    want = {ai.base.x - ai.toward.x * (d.role == "fuel" ? 9 : 5) + side.x * s * 7, ai.base.y - ai.toward.y * (d.role == "fuel" ? 9 : 5) + side.y * s * 7};
  }
  if (placeNear(sim, p, type, want, 16)) return true;
  if (defensive) return false;
  // No room round the first base: any other yard of ours will do.
  for (uint32_t e : sim.reg_.pool<CStructure>().entities()) {
    const CUnit& u = sim.reg_.pool<CUnit>().get(e);
    if (u.owner != p || sim.types_.get(u.type).role != "yard") continue;
    const CPosition& c = sim.reg_.pool<CPosition>().get(e);
    if (dist({c.x, c.y}, ai.base) < 6 * T) continue;
    if (placeNear(sim, p, type, {c.x - ai.toward.x * 5, c.y - ai.toward.y * 5}, 12)) return true;
  }
  return false;
}

bool SkirmishAi::manageEconomy(Simulation& sim, uint8_t p) {
  AiPlayer& ai = players_[p];
  const Tuning& tune = kTuning[static_cast<int>(ai.difficulty)];
  Registry& reg = sim.reg_;
  const PlayerState& ps = sim.players_[p];
  const UnitTypeRegistry& types = sim.types_;

  // What do we have?
  std::string faction;
  int roleCount[6] = {};  // yard, power, income buildings, barracks, factory, other
  int posts = 0, warehouses = 0, fuels = 0, offices = 0, trucks = 0;
  int bunkers = 0, atGuns = 0, towers = 0, nests = 0, guns = 0;
  std::vector<Entity> mcvList;
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner != p) continue;
    const UnitTypeDef& d = types.get(u.type);
    if (faction.empty()) faction = d.faction;
    if (d.isStructure()) {
      const std::string& r = d.role;
      const bool earns = r == "post" || r == "warehouse" || r == "fuel";
      roleCount[r == "yard" ? 0 : r == "power" ? 1 : earns ? 2 : r == "barracks" ? 3 : r == "factory" ? 4 : 5]++;
      posts += r == "post";
      warehouses += r == "warehouse";
      fuels += r == "fuel";
      offices += r == "office";
      if (d.model == "bunker") ++bunkers;
      if (d.model == "atgun") ++atGuns;
      if (d.model == "tower") ++towers;
      if (d.model == "mg_nest") ++nests;
      if (d.model == "howitzer_pit") ++guns;
    } else if (d.deploysTo >= 0) {
      mcvList.push_back(reg.handle(e));
    } else if (d.capacity > 0) {
      ++trucks;
    }
  }
  auto typeByRole = [&](const char* role) {
    for (int t = 0; t < static_cast<int>(types.size()); ++t)
      if (types.get(t).faction == faction && types.get(t).role == role && !types.get(t).builtAt.empty()) return t;
    return -1;
  };
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
        // MG nests guard our logistics offices first.
        for (uint32_t e : reg.pool<CStructure>().entities()) {
          const CUnit& u = reg.pool<CUnit>().get(e);
          if (u.owner != p || types.get(u.type).role != "office") continue;
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
    // The first of whatever it has none of, in this order; then more income
    // as the game goes on (as many as its yards allow), then more production.
    const int post = typeByRole("post"), warehouse = typeByRole("warehouse"), fuel = typeByRole("fuel");
    const bool grow = roleCount[2] < std::min(tune.refineries + minutes / 3, 14);
    std::vector<int> wants;
    if (margin < 30 && powerType >= 0) wants.push_back(powerType);
    if (posts < 1) wants.push_back(post);
    if (roleCount[3] < 1) wants.push_back(typeOf("barracks"));
    if (warehouses < 1) wants.push_back(warehouse);
    if (roleCount[4] < 1) wants.push_back(typeOf("factory"));
    if (fuels < 1 && ps.credits > 800) wants.push_back(fuel);
    if (grow) {
      // The best earner there is room for.
      wants.push_back(fuel);
      wants.push_back(warehouse);
      wants.push_back(post);
    }
    if (roleCount[3] < tune.barracks && ps.credits > 1200) wants.push_back(typeOf("barracks"));
    if (roleCount[4] < tune.factories && ps.credits > 2500) wants.push_back(typeOf("factory"));
    // Money piling up faster than the lines can spend it: more works and
    // barracks (each further one speeds its line up).
    if (ai.difficulty != AiDifficulty::Easy) {
      if (roleCount[4] < tune.factories + 2 && ps.credits > 5000) wants.push_back(typeOf("factory"));
      if (roleCount[3] < tune.barracks + 1 && ps.credits > 5000) wants.push_back(typeOf("barracks"));
    }
    for (int pick : wants)
      if (pick >= 0 && produce(pick)) break;
  }
  // Supply trucks lost on the road are replaced, one at a time.
  {
    int truckType = -1;
    for (int t = 0; t < static_cast<int>(types.size()) && truckType < 0; ++t)
      if (types.get(t).faction == faction && types.get(t).capacity > 0 && !types.get(t).isStructure()) truckType = t;
    const auto& vq = ps.queues[static_cast<size_t>(Queue::Vehicles)].items;
    const bool queued = truckType >= 0 && std::find(vq.begin(), vq.end(), truckType) != vq.end();
    if (truckType >= 0 && !queued && trucks < offices * 3) produce(truckType);
  }
  // A second (and third) yard further out, once the base is up.
  const int mcvType = typeOf("mcv");
  const auto& vItems = ps.queues[static_cast<size_t>(Queue::Vehicles)].items;
  const bool mcvQueued = std::find(vItems.begin(), vItems.end(), mcvType) != vItems.end();
  if (roleCount[0] < tune.maxYards && roleCount[4] > 0 && roleCount[2] >= 3 && mcvList.empty() && !mcvQueued && minutes >= 6) {
    // Every yard allows more post offices, warehouses and another fuel depot.
    // Somewhere off to a side or behind, well away from the base, not toward the enemy.
    const Vec2i side{-ai.toward.y, ai.toward.x};
    Vec2i best{-1, -1};
    int32_t bestScore = INT32_MIN;
    for (int k = 0; k < 6; ++k) {
      const int sideways = (k % 2 ? 1 : -1) * (k < 4 ? 26 : 16), back = k < 2 ? 0 : k < 4 ? 12 : 24;
      Vec2i c{ai.base.x + side.x * sideways - ai.toward.x * back, ai.base.y + side.y * sideways - ai.toward.y * back};
      if (c.x < 8 * T || c.y < 8 * T || c.x > sim.map_.widthLeptons() - 8 * T || c.y > sim.map_.heightLeptons() - 8 * T) continue;
      c = sim.pathfinder_.nearestReachable(ai.base, c, Locomotor::Wheeled);
      const int32_t d = dist(c, ai.base);
      if (d < 16 * T) continue;
      // Not on top of another yard, ours or anyone's.
      bool crowded = false;
      for (uint32_t e : reg.pool<CStructure>().entities()) {
        if (types.get(reg.pool<CUnit>().get(e).type).role != "yard") continue;
        const CPosition& yc = reg.pool<CPosition>().get(e);
        if (dist({yc.x, yc.y}, c) < 14 * T) crowded = true;
      }
      if (crowded) continue;
      const int32_t score = d - k * T - static_cast<int32_t>(hash32(static_cast<uint32_t>(p) * 31u + static_cast<uint32_t>(k)) % 4) * T;
      if (score > bestScore) {
        bestScore = score;
        best = c;
      }
    }
    if (best.x >= 0 && produce(mcvType)) ai.expandTo = best;
  }
  const ProductionQueue& dq = ps.queues[static_cast<size_t>(Queue::Defenses)];
  const Doctrine doc = doctrineFor(faction);
  // A watchtower unlocks the heavy kit (guns, snipers, heavy tanks): a side
  // that lives by its guns puts one up as soon as it has a barracks.
  if (dq.items.empty() && towers == 0 && roleCount[3] > 0 && ps.credits > 300 && (doc.guns > 0 || minutes >= 8))
    produce(typeOf("tower"));
  else if (dq.items.empty() && ps.credits > 700) {
    if (nests < std::min(offices, 6) + (doc.fortify >= 2 ? 2 : 1)) produce(typeOf("mg_nest"));
    else if (bunkers < tune.bunkers) produce(typeOf("bunker"));
    else if (atGuns < tune.atGuns + (doc.fortify >= 3 ? 1 : 0)) produce(typeOf("atgun"));
    else if (towers < tune.towers) produce(typeOf("tower"));
    else if (guns < (ai.difficulty == AiDifficulty::Hard ? 1 : 0) + (ai.difficulty != AiDifficulty::Easy ? doc.guns : 0) &&
             ps.credits > 2500)
      produce(typeOf("howitzer_pit"));
  }
  // Expansion comes first: while a building, a yard or power is on order, the
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
    if (!sim.aiMayCommand(e)) continue;  // a script's set piece: not ours to command
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
  // A big force is bound as several battle groups of about sixty, vehicles
  // and men shared out among them: big enough for a company's firepower,
  // small enough to march and fight as one body. Each takes a stretch of the
  // front (they are dealt out from one flank to the other).
  const Vec2i across{-ai.toward.y, ai.toward.x};
  auto bindGroups = [&](const std::vector<Entity>& units) {
    const size_t k = (units.size() + 59) / 60;
    if (k <= 1) {
      bind(units);
      return;
    }
    std::vector<Entity> men, vehicles;
    for (Entity e : units) (isInfantry(e) ? men : vehicles).push_back(e);
    auto byFlank = [&](Entity a, Entity b) {
      const CPosition& pa = reg.get<CPosition>(a);
      const CPosition& pb = reg.get<CPosition>(b);
      const int64_t da = static_cast<int64_t>(pa.x) * across.x + static_cast<int64_t>(pa.y) * across.y;
      const int64_t db = static_cast<int64_t>(pb.x) * across.x + static_cast<int64_t>(pb.y) * across.y;
      return da != db ? da < db : a.index < b.index;
    };
    std::sort(men.begin(), men.end(), byFlank);
    std::sort(vehicles.begin(), vehicles.end(), byFlank);
    for (size_t i = 0; i < k; ++i) {
      std::vector<Entity> group(men.begin() + static_cast<ptrdiff_t>(i * men.size() / k), men.begin() + static_cast<ptrdiff_t>((i + 1) * men.size() / k));
      group.insert(group.end(), vehicles.begin() + static_cast<ptrdiff_t>(i * vehicles.size() / k),
                   vehicles.begin() + static_cast<ptrdiff_t>((i + 1) * vehicles.size() / k));
      bind(group);
    }
  };
  // Keeps a force bound: once `percent` of it stands outside a proper unit
  // (new arrivals, men still in the fireteams they formed by themselves,
  // vehicles on their own) it is bound again - the whole of it afresh, or
  // only those outside (a force on the march keeps the units it has).
  const bool bindNow = sim.tick_ % (TICKS_PER_SECOND * 8) < tune.thinkTicks;
  auto keepBound = [&](const std::vector<Entity>& units, int percent, bool afresh) {
    if (!bindNow || units.size() < 5) return;
    std::vector<Entity> loose;
    for (Entity e : units) {
      const CGroup* g = sim.groupOf(e);
      if (!g || (!g->combined && g->echelon <= Echelon::Fireteam)) loose.push_back(e);
    }
    if (loose.empty() || static_cast<int>(loose.size()) * 100 < static_cast<int>(units.size()) * percent) return;
    // Would binding make a squad or a battle group of them?
    const std::vector<Entity>& set = afresh ? units : loose;
    int men = 0, vehicles = 0;
    for (Entity e : set) (isInfantry(e) ? men : vehicles) += 1;
    const bool worth = men >= Simulation::echelonSize(false, Echelon::Squad) ||
                       (vehicles > 0 && vehicles + (men + 3) / 4 >= Simulation::echelonSize(true, Echelon::Platoon));
    if (worth) bindGroups(set);
  };
  // Orders a force unit by unit: every bound unit marches as its own body,
  // side by side on a front across the way to the target.
  auto orderUnits = [&](CommandType type, const std::vector<Entity>& units, Vec2i target, Formation f) {
    std::vector<Entity> keys;
    std::vector<std::vector<Entity>> bodies;
    for (Entity e : units) {
      const CGroupMember* gm = reg.pool<CGroupMember>().tryGet(e.index);
      const Entity g = gm && reg.alive(gm->group) ? gm->group : kNullEntity;
      size_t i = 0;
      while (i < keys.size() && !(keys[i] == g)) ++i;
      if (i == keys.size()) {
        keys.push_back(g);
        bodies.emplace_back();
      }
      bodies[i].push_back(e);
    }
    if (bodies.size() <= 1) {
      order(type, units, target, f);
      return;
    }
    const Vec2i c = centreOf(units);
    const int32_t dx = target.x - c.x, dy = target.y - c.y;
    const int32_t len = static_cast<int32_t>(std::max<uint32_t>(1, distance(dx, dy)));
    const Vec2i right{static_cast<int32_t>(-static_cast<int64_t>(dy) * 256 / len), static_cast<int32_t>(static_cast<int64_t>(dx) * 256 / len)};
    std::vector<std::pair<int64_t, size_t>> rank;  // from the left flank to the right, as they stand now
    for (size_t i = 0; i < bodies.size(); ++i) {
      const Vec2i bc = centreOf(bodies[i]);
      rank.push_back({static_cast<int64_t>(bc.x - c.x) * right.x + static_cast<int64_t>(bc.y - c.y) * right.y, i});
    }
    std::sort(rank.begin(), rank.end());
    const int32_t n = static_cast<int32_t>(bodies.size());
    const int32_t gap = std::min(8, 40 / n);  // tiles between neighbours: a front of at most forty tiles
    for (int32_t k = 0; k < n; ++k) {
      const int32_t off = (2 * k - (n - 1)) * gap / 2;
      Vec2i t{target.x + right.x * off, target.y + right.y * off};
      t.x = std::clamp(t.x, 2 * T, sim.map_.widthLeptons() - 2 * T);
      t.y = std::clamp(t.y, 2 * T, sim.map_.heightLeptons() - 2 * T);
      order(type, bodies[rank[static_cast<size_t>(k)].second], t, f);
    }
  };
  // Where a force's front is: the middle of the half of it nearest the enemy
  // (reinforcements still on their way up don't drag the point back).
  auto frontOf = [&](const std::vector<Entity>& units) {
    if (units.size() < 8 || ai.enemyAt.x < 0) return centreOf(units);
    std::vector<std::pair<int32_t, Entity>> byRange;
    for (Entity e : units) {
      const CPosition& c = reg.get<CPosition>(e);
      byRange.push_back({dist({c.x, c.y}, ai.enemyAt), e});
    }
    std::sort(byRange.begin(), byRange.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first < b.first : a.second.index < b.second.index; });
    std::vector<Entity> front;
    for (size_t i = 0; i < byRange.size() / 2; ++i) front.push_back(byRange[i].second);
    return centreOf(front);
  };

  // --- Production, to the side's doctrine --------------------------------------
  if (ps.credits > tune.reserve && allowed(p, kAiTrain)) {
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
      if (t >= 0) produce(t, types.get(t).model == "levy" ? 10 : 5);  // infantry come as fireteams, levies in crowds
    }
    // Always one scout car to keep watch out in front (never on easy: it fights blind).
    int scouts = 0, scoutType = -1;
    for (Entity e : army) scouts += types.get(reg.get<CUnit>(e).type).scout;
    for (int t : vehicles)
      if (types.get(t).scout) scoutType = t;
    if (ai.difficulty != AiDifficulty::Easy && scouts == 0 && scoutType >= 0 && sim.tick_ > TICKS_PER_SECOND * 90 &&
        std::find(vq.items.begin(), vq.items.end(), scoutType) == vq.items.end())
      produce(scoutType, 1);
    // With money piling up the works are kept busy; otherwise an order or two ahead.
    const size_t ahead = ps.credits > 4000 ? 4u : wantInfantry ? 1u : 3u;
    if (!vehicles.empty() && !saving && vq.items.size() < ahead) {
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
  std::erase_if(ai.capture, [&](Entity e) { return !alive(e); });
  if (!alive(ai.scout)) ai.scout = kNullEntity;
  auto in = [](const std::vector<Entity>& v, Entity e) { return std::find(v.begin(), v.end(), e) != v.end(); };

  // Damaged vehicles head home for repairs (engineers patch them up there).
  std::vector<Entity> limping;
  for (Entity e : army) {
    if (isInfantry(e) || !allowed(p, kAiRepair) || !ai.hasBase) continue;
    const CUnit& u = reg.get<CUnit>(e);
    if (u.health * 100 < types.get(u.type).maxHealth * 35 && dist({reg.get<CPosition>(e).x, reg.get<CPosition>(e).y}, ai.base) > 10 * T) {
      limping.push_back(e);
      std::erase(ai.wave, e);
      std::erase(ai.raiders, e);
      std::erase(ai.capture, e);
    }
  }
  if (!limping.empty() && sim.tick_ % (TICKS_PER_SECOND * 6) < tune.thinkTicks) order(CommandType::Move, limping, ai.base);

  std::vector<Entity> home;
  for (Entity e : army)
    if (!in(ai.wave, e) && !in(ai.raiders, e) && !in(ai.guard, e) && !in(ai.capture, e) && !in(limping, e) && e != ai.scout) home.push_back(e);

  // The home guard: infantry kept back to man the base's trenches and bunkers
  // (a force without a base - a mission's - has no home to guard).
  if (!ai.hasBase) {
    for (Entity e : ai.guard) home.push_back(e);
    ai.guard.clear();
  }
  for (Entity e : home)
    if (ai.hasBase && static_cast<int>(ai.guard.size()) < tune.guard * (2 + std::min(doc.fortify, 2)) / 4 && isInfantry(e) && modelOf(e) != "sniper") ai.guard.push_back(e);
  std::erase_if(home, [&](Entity e) { return in(ai.guard, e); });
  keepBound(ai.guard, 30, true);  // the home guard is a unit too
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
    if (!sim.unitVisibleTo(p, e)) continue;
    if (dist({c.x, c.y}, ai.base) < 20 * T || (ai.order == 2 && dist({c.x, c.y}, ai.orderAt) < 16 * T)) {
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

  // --- The scout: one reconnaissance vehicle out in front, from watch point to watch point
  // It stands and watches (and so falls back by itself when the enemy comes
  // close); what it sees, the guns can shoot at.
  if (ai.difficulty != AiDifficulty::Easy && allowed(p, kAiScout)) {
    if (!ai.scout.valid())
      for (Entity e : home)
        if (types.get(reg.get<CUnit>(e).type).scout) {
          ai.scout = e;
          ai.scoutTick = 0;
          break;
        }
    if (ai.scout.valid()) {
      std::erase(home, ai.scout);
      const bool standing = reg.get<COrder>(ai.scout).type == OrderType::Idle;
      const bool due = ai.scoutTick == 0 || (standing && sim.tick_ >= ai.scoutTick + TICKS_PER_SECOND * 75);
      if (due && !in(limping, ai.scout) && ai.enemyAt.x >= 0) {
        // Its posts: halfway to the main enemy; the nearest logistics point
        // that isn't ours; out on either flank of the way there.
        const Vec2i side{-ai.toward.y, ai.toward.x};
        const Vec2i half{ai.base.x + (ai.enemyAt.x - ai.base.x) * 55 / 100, ai.base.y + (ai.enemyAt.y - ai.base.y) * 55 / 100};
        ai.scoutPost = static_cast<uint8_t>((ai.scoutPost + 1) % 4);
        Vec2i post = half;
        if (ai.scoutPost == 1) {
          int32_t bestD = INT32_MAX;
          for (size_t i = 0; i < sim.map_.supplyPoints.size(); ++i) {
            const Entity office = sim.officeOn(static_cast<int>(i));
            if (office.valid() && sim.allied(reg.get<CUnit>(office).owner, p)) continue;
            const Vec2i c{sim.map_.supplyPoints[i].x * T + 128, sim.map_.supplyPoints[i].y * T + 128};
            if (dist(c, ai.base) < bestD) {
              bestD = dist(c, ai.base);
              post = {c.x - ai.toward.x * 5, c.y - ai.toward.y * 5};  // short of it, looking on
            }
          }
        } else if (ai.scoutPost >= 2) {
          const int sgn = ai.scoutPost == 2 ? 1 : -1;
          post = {half.x + side.x * sgn * 18, half.y + side.y * sgn * 18};
        }
        post.x = std::clamp(post.x, 3 * T, sim.map_.widthLeptons() - 3 * T);
        post.y = std::clamp(post.y, 3 * T, sim.map_.heightLeptons() - 3 * T);
        post = sim.pathfinder_.nearestReachable(ai.base, post, Locomotor::Wheeled);
        order(CommandType::Move, {ai.scout}, post);
        ai.scoutTick = sim.tick_;
      }
    }
  }

  // --- Logistics points: a party goes out to take the nearest free one ----------------
  {
    const int minutes = static_cast<int>(sim.tick_ / (TICKS_PER_SECOND * 60));
    const int32_t reach = (45 + minutes * 10) * T;
    Vec2i want{-1, -1};
    int32_t bestD = INT32_MAX;
    bool atTarget = false;
    for (size_t i = 0; i < sim.map_.supplyPoints.size(); ++i) {
      if (sim.officeOn(static_cast<int>(i)).valid()) continue;  // taken (an enemy's office is the raiders' business)
      const Vec2i c{sim.map_.supplyPoints[i].x * T + 128, sim.map_.supplyPoints[i].y * T + 128};
      const int32_t d = dist(c, ai.base);
      if (c == ai.captureTarget) atTarget = true;
      if (d < bestD && d <= reach) {
        bestD = d;
        want = c;
      }
    }
    const int partySize = ai.difficulty == AiDifficulty::Easy ? 4 : 6;
    if (!ai.capture.empty()) {
      if (strength(ai.capture) < 2) {
        // Too few left to hold anything: back into the pool; try again later.
        ai.capture.clear();
        ai.captureTarget = {-1, -1};
        ai.captureReady = sim.tick_ + TICKS_PER_SECOND * 60;
      } else if (!atTarget) {
        // Taken (by us or someone else): on to the next, or home.
        ai.captureTarget = want;
        if (want.x >= 0) order(CommandType::AttackMove, ai.capture, want, Formation::Column);
        else {
          order(CommandType::Move, ai.capture, ai.staging);
          ai.capture.clear();
          ai.captureReady = sim.tick_ + TICKS_PER_SECOND * 30;
        }
      } else {
        // On the way or standing on it; anyone who stopped short goes on.
        std::vector<Entity> idle;
        for (Entity e : ai.capture) {
          const CPosition& c = reg.get<CPosition>(e);
          if (reg.get<COrder>(e).type == OrderType::Idle && dist({c.x, c.y}, ai.captureTarget) > 2 * T) idle.push_back(e);
        }
        if (!idle.empty() && sim.tick_ % (TICKS_PER_SECOND * 4) < tune.thinkTicks) order(CommandType::AttackMove, idle, ai.captureTarget, Formation::Column);
      }
    } else if (want.x >= 0 && sim.tick_ >= ai.captureReady && threat.x < 0 && allowed(p, kAiCapture) &&
               (ai.difficulty != AiDifficulty::Easy || minutes >= 4)) {
      std::vector<Entity> pick;
      for (Entity e : home)
        if (isInfantry(e) && modelOf(e) != "sniper" && static_cast<int>(pick.size()) < partySize &&
            (reg.get<COrder>(e).type == OrderType::Idle || reg.get<COrder>(e).type == OrderType::Move))
          pick.push_back(e);
      if (static_cast<int>(pick.size()) >= partySize - 1) {
        // A half-track or light tank goes along if one is to hand.
        for (Entity e : home)
          if (!isInfantry(e) && (modelOf(e) == "halftrack" || modelOf(e) == "light_tank")) {
            pick.push_back(e);
            break;
          }
        ai.capture = pick;
        ai.captureTarget = want;
        bind(pick);
        order(CommandType::AttackMove, pick, want, Formation::Column);
        std::erase_if(home, [&](Entity e) { return in(ai.capture, e); });
      }
    }
  }

  // --- Raiders: fast vehicles hunting offices, convoys and stragglers -------------------
  // With more than one enemy the raiders leave the main enemy to the main
  // force and take the others in turn, a different one each time they set out.
  std::vector<uint8_t> foes;  // every hostile side still standing
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner >= MAX_PLAYERS || sim.allied(u.owner, p) || types.get(u.type).category == UnitCategory::Fortification) continue;
    if (std::find(foes.begin(), foes.end(), u.owner) == foes.end()) foes.push_back(u.owner);
  }
  std::sort(foes.begin(), foes.end());
  auto pickRaidEnemy = [&] {
    std::vector<uint8_t> others;
    for (uint8_t q : foes)
      if (q != ai.mainEnemy) others.push_back(q);
    ai.raidEnemy = others.empty() ? ai.mainEnemy : others[ai.raids % others.size()];
  };
  if (ai.raiders.empty() && sim.tick_ >= ai.raidReady && threat.x < 0 && allowed(p, kAiRaid)) {
    std::vector<Entity> pick;
    for (Entity e : home)
      if (!isInfantry(e) && fast(modelOf(e)) && static_cast<int>(pick.size()) < doc.raidSize) pick.push_back(e);
    // Enough to bind into an armoured platoon (easy sends what it has).
    if (static_cast<int>(pick.size()) >= (ai.difficulty == AiDifficulty::Easy ? 3 : std::min(doc.raidSize, 5))) {
      ai.raiders = pick;
      ai.raidTarget = {-1, -1};
      pickRaidEnemy();
      ++ai.raids;
      bind(pick);
      std::erase_if(home, [&](Entity e) { return in(ai.raiders, e); });
    }
  }
  if (!ai.raiders.empty()) {
    if (std::find(foes.begin(), foes.end(), ai.raidEnemy) == foes.end()) pickRaidEnemy();  // that one is beaten: the next
    const Vec2i rc = centreOf(ai.raiders);
    // Target: what its income hangs on - a logistics office above all, then
    // the post offices, warehouses and fuel depots, then anything - where the
    // fewest defenders stand.
    Vec2i best{-1, -1};
    int32_t bestScore = INT32_MAX;
    for (int pass = 0; pass < 2 && best.x < 0; ++pass)  // the raid enemy's; failing that, anyone's
      for (uint32_t e : reg.pool<CStructure>().entities()) {
        const CUnit& u = reg.pool<CUnit>().get(e);
        if (u.owner >= MAX_PLAYERS || sim.allied(u.owner, p) || u.health <= 0) continue;
        if (pass == 0 && u.owner != ai.raidEnemy) continue;
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
        const int32_t worth = d.role == "office" ? 30 * T : (d.role == "post" || d.role == "warehouse" || d.role == "fuel") ? 10 * T : 0;
        const int32_t score = defenders * 8 * T + dist({c.x, c.y}, rc) / 2 - worth;
        if (score < bestScore) {
          bestScore = score;
          best = {c.x, c.y};
        }
      }
    // A supply truck in sight nearby is better still: no convoy, no money.
    Entity truck;
    int32_t truckD = 12 * T;
    for (uint32_t e : reg.pool<CHarvester>().entities()) {
      const CUnit& u = reg.pool<CUnit>().get(e);
      if (u.owner >= MAX_PLAYERS || sim.allied(u.owner, p) || u.health <= 0) continue;
      const CPosition& c = reg.pool<CPosition>().get(e);
      const int32_t d = dist({c.x, c.y}, rc);
      if (d < truckD && sim.unitVisibleTo(p, e)) {
        truckD = d;
        truck = reg.handle(e);
      }
    }
    const bool spent = strength(ai.raiders) * 2 < static_cast<int32_t>(ai.difficulty == AiDifficulty::Easy ? 3 : std::min(doc.raidSize, 5)) * 4;  // half gone: home
    if (best.x < 0 || spent) {
      order(CommandType::Move, ai.raiders, ai.staging);
      ai.raiders.clear();
      ai.raidReady = sim.tick_ + TICKS_PER_SECOND * 50;
    } else if (truck.valid()) {
      if (sim.tick_ % (TICKS_PER_SECOND * 3) < tune.thinkTicks) {
        Command atk;
        atk.type = CommandType::Attack;
        atk.player = p;
        atk.units = ai.raiders;
        atk.targetUnit = truck;
        sim.queueInternal(std::move(atk));
        ai.raidTarget = {-1, -1};  // back to the buildings afterwards
      }
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
  int32_t needed = (tune.waveSize + tune.waveGrowth * static_cast<int>(std::min<uint32_t>(ai.waves, 8))) * doc.waveScale / 100;
  if (ai.waveScale > 0) needed = std::max<int32_t>(1, needed * ai.waveScale / 100);
  // A script may hold it back (no attacks, or a place to defend), or send it at a place.
  const bool mayAttack = allowed(p, kAiAttack) && ai.order != 2;
  if (!mayAttack && ai.phase != 0 && !ai.wave.empty()) {
    orderUnits(CommandType::Move, ai.wave, ai.staging, Formation::Box);
    ai.wave.clear();
    ai.phase = 0;
  }
  // Fast vehicles stay back for raiding.
  std::erase_if(home, [&](Entity e) { return fast(modelOf(e)); });
  // Whatever stands at the staging point is a unit already while it gathers,
  // so it fights as one if the enemy comes to it.
  keepBound(home, 25, true);
  if (ai.order == 2 && sim.tick_ % (TICKS_PER_SECOND * 5) < tune.thinkTicks) {
    // Defending a place: whoever wandered off (after a fight) goes back to it.
    std::vector<Entity> away;
    for (Entity e : home) {
      const CPosition& c = reg.get<CPosition>(e);
      if (reg.get<COrder>(e).type == OrderType::Idle && !reg.has<CGarrisoned>(e) && dist({c.x, c.y}, ai.orderAt) > 9 * T) away.push_back(e);
    }
    orderUnits(CommandType::Move, away, ai.orderAt, Formation::Line);
  }
  if (ai.phase == 0) {
    // Gathering. Attack once strong enough (or it's been long enough, or a script says go).
    const bool due = sim.tick_ > ai.lastWaveTick + tune.waveTimeout && strength(home) >= needed / 2;
    const bool told = ai.order == 1 && ai.orderFresh;
    if (mayAttack && (strength(home) >= needed || due || told) && (threat.x < 0 || told) && !home.empty()) {
      ai.orderFresh = 0;
      ai.wave = home;
      home.clear();
      ++ai.waves;
      ai.lastWaveTick = sim.tick_;
      ai.phaseTick = sim.tick_;
      ai.phase = 1;
      ai.launchStrength = strength(ai.wave);
      ai.waveTarget = {-1, -1};
      ai.waveTargetUnit = kNullEntity;
      bindGroups(ai.wave);  // into battle groups
      // Every other attack goes in by a flank.
      const Vec2i c = centreOf(ai.wave);
      const Vec2i side{-ai.toward.y, ai.toward.x};
      const int s = (ai.waves % 2 == 0) ? 1 : -1;
      ai.flank = (ai.waves % 3 != 1 && ai.order != 1) ? Vec2i{c.x + ai.toward.x * 16 + side.x * s * 14, c.y + ai.toward.y * 16 + side.y * s * 14}
                                     : Vec2i{-1, -1};
      if (ai.flank.x >= 0) {
        ai.flank.x = std::clamp(ai.flank.x, 3 * T, sim.map_.widthLeptons() - 3 * T);
        ai.flank.y = std::clamp(ai.flank.y, 3 * T, sim.map_.heightLeptons() - 3 * T);
      }
      return;  // its orders go out the next time round, to the units it has just bound
    }
  }
  if (ai.phase != 0 && ai.wave.empty()) ai.phase = 0;
  if (ai.phase == 1 || ai.phase == 2) {
    // Fresh troops: the Soviets feed them into the attack as they arrive; the
    // Germans send them on to the regrouping point, or forward as a bound
    // second echelon once there are enough of them.
    // (Never in dribs and drabs: a batch is at least a strong platoon, so it
    // arrives as a unit that can fight its way in.)
    const int32_t echelon = ai.phase == 2 ? 5 : doc.stream ? std::max(10, needed / 5) : std::max(12, needed / 3);
    if (!home.empty() && strength(home) >= echelon) {
      std::vector<Entity> fresh;
      for (Entity e : home)
        if (reg.get<COrder>(e).type == OrderType::Idle) fresh.push_back(e);
      if (!fresh.empty()) {
        bindGroups(fresh);  // a unit of their own from the start
        order(CommandType::AttackMove, fresh, ai.phase == 2 && ai.forward.x >= 0 ? ai.forward : frontOf(ai.wave), Formation::Wedge);
        for (Entity e : fresh) ai.wave.push_back(e);
      }
    }
  }
  // The main force stays one battle group: bound again when enough of it is
  // new, or the old unit has been shot to pieces.
  if (ai.phase != 0) keepBound(ai.wave, 15, false);
  if (ai.phase == 1 && !ai.wave.empty()) {
    const Vec2i centre = frontOf(ai.wave);
    // Badly hurt: fall back a little, dig in and regroup - don't run home.
    if (strength(ai.wave) * 100 < ai.launchStrength * doc.regroupLoss) {
      ai.phase = 2;
      ai.phaseTick = sim.tick_;
      ai.forward = {centre.x - ai.toward.x * 7, centre.y - ai.toward.y * 7};
      ai.forward.x = std::clamp(ai.forward.x, 2 * T, sim.map_.widthLeptons() - 2 * T);
      ai.forward.y = std::clamp(ai.forward.y, 2 * T, sim.map_.heightLeptons() - 2 * T);
      orderUnits(CommandType::Move, ai.wave, ai.forward, Formation::Line);
      return;
    }
    // A flanking approach first.
    if (ai.flank.x >= 0) {
      if (dist(centre, ai.flank) < 5 * T || sim.tick_ > ai.phaseTick + TICKS_PER_SECOND * 50) {
        ai.flank = {-1, -1};
        ai.waveTarget = {-1, -1};
      } else {
        if (ai.waveTarget != ai.flank) {
          orderUnits(CommandType::AttackMove, ai.wave, ai.flank, Formation::Wedge);
          ai.waveTarget = ai.flank;
          ai.waveTargetUnit = kNullEntity;
        }
        return;
      }
    }
    // Target: the main enemy's nearest building to the force; failing that,
    // its nearest unit; with the main enemy gone from the map, anyone's.
    Vec2i target{-1, -1};
    Entity targetUnit;
    int32_t best = INT32_MAX;
    // It finishes what it started: the building it is going for stays the
    // target until it is down (a force that changed its mind with every step
    // would form up again and again and never arrive).
    if (ai.order == 1 && ai.orderAt.x >= 0) {
      target = ai.orderAt;  // where the script sent it; it fights whatever it meets on the way
    } else if (ai.waveTarget.x >= 0 && reg.alive(ai.waveTargetUnit) && reg.has<CStructure>(ai.waveTargetUnit)) {
      const CUnit& u = reg.get<CUnit>(ai.waveTargetUnit);
      if (u.owner < MAX_PLAYERS && !sim.allied(u.owner, p) && u.health > 0) {
        const CPosition& c = reg.get<CPosition>(ai.waveTargetUnit);
        target = {c.x, c.y};
        targetUnit = ai.waveTargetUnit;
      }
    }
    for (int pass = 0; pass < 4 && target.x < 0; ++pass)
      for (uint32_t e : reg.pool<CUnit>().entities()) {
        const CUnit& u = reg.pool<CUnit>().get(e);
        if (u.owner >= MAX_PLAYERS || sim.allied(u.owner, p)) continue;
        if (pass < 2 && u.owner != ai.mainEnemy) continue;
        const UnitTypeDef& d = types.get(u.type);
        if (d.category == UnitCategory::Fortification || (pass % 2 == 0) != d.isStructure()) continue;
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
      if (newTarget) orderUnits(CommandType::AttackMove, movers, target, Formation::Wedge);
      else order(CommandType::AttackMove, movers, target, Formation::Wedge);  // stragglers come up to it
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
      ai.waveTargetUnit = kNullEntity;
      ai.flank = {-1, -1};
      bindGroups(ai.wave);
    }
  }
}

void SkirmishAi::setOrder(uint8_t p, uint8_t order, Vec2i at) {
  if (p >= 8) return;
  AiPlayer& a = players_[p];
  a.order = order;
  a.orderAt = order ? at : Vec2i{-1, -1};
  a.orderFresh = order == 1;
  a.waveTarget = {-1, -1};  // the main force picks its target afresh
  a.waveTargetUnit = kNullEntity;
  if (order == 1) a.flank = {-1, -1};
}

void SkirmishAi::setBehaviour(uint8_t p, int bit, bool allowed) {
  if (p >= 8 || bit < 0 || bit >= 8) return;
  if (allowed) players_[p].disabled = static_cast<uint8_t>(players_[p].disabled & ~(1u << bit));
  else players_[p].disabled = static_cast<uint8_t>(players_[p].disabled | (1u << bit));
}

void SkirmishAi::setWaveScale(uint8_t p, int16_t percent) {
  if (p < 8) players_[p].waveScale = percent;
}

void SkirmishAi::clearOrders() {
  for (AiPlayer& a : players_) {
    a.order = 0;
    a.orderAt = {-1, -1};
    a.disabled = 0;
    a.waveScale = 0;
    a.orderFresh = 0;
  }
}

void SkirmishAi::saveOrders(ByteWriter& w) const {
  for (const AiPlayer& a : players_) {
    w.u8(a.order);
    w.pod(a.orderAt);
    w.u8(a.disabled);
    w.u16(static_cast<uint16_t>(a.waveScale));
    w.u8(a.orderFresh);
  }
}

bool SkirmishAi::loadOrders(ByteReader& r) {
  for (AiPlayer& a : players_) {
    a.order = r.u8();
    a.orderAt = r.pod<Vec2i>();
    a.disabled = r.u8();
    a.waveScale = static_cast<int16_t>(r.u16());
    a.orderFresh = r.u8();
  }
  return r.ok();
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
    w.podVector(a.capture);
    w.pod(a.captureTarget);
    w.u32(a.captureReady);
    w.u8(a.mainEnemy);
    w.u8(a.raidEnemy);
    w.u32(a.raids);
    w.pod(a.scout);
    w.u8(a.scoutPost);
    w.u32(a.scoutTick);
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
      if (version >= 6) {
        a.capture = r.podVector<Entity>();
        a.captureTarget = r.pod<Vec2i>();
        a.captureReady = r.u32();
      }
      if (version >= 7) {
        a.mainEnemy = r.u8();
        a.raidEnemy = r.u8();
        a.raids = r.u32();
        a.scout = r.pod<Entity>();
        a.scoutPost = r.u8();
        a.scoutTick = r.u32();
      }
    } else if (!a.wave.empty()) {
      a.phase = 1;
    }
  }
  return r.ok();
}

}  // namespace pioneer

// Slave Engine - saving and loading the simulation state, and commands.
//
// The format is a straight binary dump in a fixed order, versioned by the save
// file around it. Component pools are written with their entities in storage
// order, so a loaded game iterates units in exactly the same order and the
// simulation continues identically (the tests check the state hash).
#include <nlohmann/json.hpp>

#include "slave/sim/simulation.hpp"

namespace slave {

namespace {
constexpr uint32_t kStateMagic = 0x53494D35;  // "SIM5": formations
constexpr uint32_t kStateMagicV4 = 0x53494D34;  // "SIM4": hints, build locks, orders given, campaign flags
constexpr uint32_t kStateMagicV3 = 0x53494D33;  // "SIM3": AI doctrine state (phases, raiders, guard)
constexpr uint32_t kStateMagicV2 = 0x53494D32;  // "SIM2": groups, garrisons, supply income

template <class T>
void savePool(ByteWriter& w, Registry& reg) {
  auto& p = reg.pool<T>();
  w.podVector(p.entities());
  w.podVector(p.data());
}
template <class T>
void loadPool(ByteReader& r, Registry& reg) {
  std::vector<uint32_t> ents = r.podVector<uint32_t>();
  std::vector<T> data = r.podVector<T>();
  if (ents.size() != data.size()) r.fail();
  reg.pool<T>().restore(std::move(ents), std::move(data));
}

void writeQueue(ByteWriter& w, const ProductionQueue& q) {
  w.podVector(q.items);
  w.i32(q.progress);
  w.i32(q.paid);
  w.u8(q.ready ? 1 : 0);
}
void readQueue(ByteReader& r, ProductionQueue& q) {
  q.items = r.podVector<uint16_t>();
  q.progress = r.i32();
  q.paid = r.i32();
  q.ready = r.u8() != 0;
}
}  // namespace

void writeCommand(ByteWriter& w, const Command& c) {
  w.u8(static_cast<uint8_t>(c.type));
  w.u8(c.player);
  w.pod(c.target);
  w.pod(c.targetUnit);
  w.u16(c.unitType);
  w.u16(c.count);
  w.u16(c.facing);
  w.podVector(c.units);
  w.u32(c.tag);
  w.u32(c.indexBase);
  w.u16(c.weapon);
  w.i32(c.radius);
  w.podVector(c.spawns);
  w.podVector(c.edits);
  w.u8(static_cast<uint8_t>(c.formation));
  w.i32(c.formFacing);
  w.i32(c.formWidth);
  w.u8(c.forced ? 1 : 0);
  w.u8(static_cast<uint8_t>(c.stance));
  w.i32(c.amount);
}

Command readCommand(ByteReader& r) {
  Command c;
  c.type = static_cast<CommandType>(r.u8());
  c.player = r.u8();
  c.target = r.pod<Vec2i>();
  c.targetUnit = r.pod<Entity>();
  c.unitType = r.u16();
  c.count = r.u16();
  c.facing = r.u16();
  c.units = r.podVector<Entity>();
  c.tag = r.u32();
  c.indexBase = r.u32();
  c.weapon = r.u16();
  c.radius = r.i32();
  c.spawns = r.podVector<SpawnSpec>();
  c.edits = r.podVector<UnitEdit>();
  c.formation = static_cast<Formation>(r.u8() % static_cast<uint8_t>(Formation::Count));
  c.formFacing = r.i32();
  c.formWidth = r.i32();
  c.forced = r.u8() != 0;
  c.stance = static_cast<Stance>(r.u8() % static_cast<uint8_t>(Stance::Count));
  c.amount = r.i32();
  return c;
}

void Simulation::saveState(ByteWriter& w) const {
  w.u32(kStateMagic);
  w.u32(static_cast<uint32_t>(map_.width()));
  w.u32(static_cast<uint32_t>(map_.height()));
  w.u32(tick_);
  w.u64(rng_.state());
  w.u64(rng_.increment());
  w.u32(static_cast<uint32_t>(pending_.size()));
  for (const Command& c : pending_) writeCommand(w, c);

  // Entities and components.
  const Registry::State rs = reg_.state();
  w.podVector(rs.generations);
  w.podVector(rs.used);
  w.podVector(rs.freeList);
  w.u64(rs.alive);
  savePool<CPosition>(w, reg_);
  savePool<CPrevPosition>(w, reg_);
  savePool<CFacing>(w, reg_);
  savePool<CUnit>(w, reg_);
  savePool<CTurret>(w, reg_);
  savePool<COrder>(w, reg_);
  savePool<CCombat>(w, reg_);
  savePool<CAnim>(w, reg_);
  savePool<CProjectile>(w, reg_);
  savePool<CStructure>(w, reg_);
  savePool<CProducer>(w, reg_);
  savePool<CHarvester>(w, reg_);
  savePool<CGroupMember>(w, reg_);
  savePool<CGarrisoned>(w, reg_);
  {  // Groups and garrisons hold lists, so they are written field by field.
    auto& g = reg_.pool<CGroup>();
    w.podVector(g.entities());
    for (const CGroup& c : g.data()) {
      w.u8(c.owner);
      w.u8(c.combined ? 2 : c.vehicles ? 1 : 0);
      w.u8(static_cast<uint8_t>(c.echelon));
      w.u16(c.number);
      w.podVector(c.members);
    }
    auto& gs = reg_.pool<CGarrison>();
    w.podVector(gs.entities());
    for (const CGarrison& c : gs.data()) w.podVector(c.occupants);
  }
  w.pod(groupNumbers_);
  {  // Movers hold paths, so they are written field by field.
    auto& p = reg_.pool<CMover>();
    w.podVector(p.entities());
    for (const CMover& m : p.data()) {
      w.podVector(m.path);
      w.u32(m.index);
      w.u8(m.active ? 1 : 0);
      w.u32(m.request);
      w.pod(m.requestGoal);
      w.i32(m.bestDist);
      w.u16(m.stuckTicks);
      w.u8(m.repaths);
      w.u8(m.failures);
      w.u32(m.retryTick);
    }
  }

  // Path requests waiting for their turn.
  std::vector<PathRequest> waiting(pathQueue_.begin() + static_cast<long>(pathQueueHead_), pathQueue_.end());
  w.podVector(waiting);
  w.u32(nextRequest_);

  // Sides.
  w.pod(team_);
  for (const PlayerState& ps : players_) {
    w.i32(ps.credits);
    w.i32(ps.powerMade);
    w.i32(ps.powerUsed);
    for (const ProductionQueue& q : ps.queues) writeQueue(w, q);
    w.u8(ps.defeated ? 1 : 0);
    w.u8(ps.won ? 1 : 0);
    w.i32(ps.incomeAcc);
  }
  w.pod(playedOnce_);
  w.pod(scriptDefeated_);

  // The ground: structure tiles, cover, the map's block layer.
  w.podVector(tileStructure_);
  w.podVector(cover_);
  w.u8(coverDirty_ ? 1 : 0);
  w.i32(structureReach_);
  w.podVector(const_cast<Map&>(map_).blockData());

  // Fog of war.
  Vision& v = const_cast<Vision&>(vision_);
  for (const auto& g : v.grids()) w.podVector(g);
  w.pod(v.revealFlags());
  w.pod(v.activeFlags());

  // Script.
  w.str(scriptToJson(script_, &types_).dump());
  w.podVector(triggerState_);
  w.u32(static_cast<uint32_t>(objectives_.size()));
  for (const Objective& o : objectives_) {
    w.str(o.text);
    w.u8(o.player);
    w.u8(o.state);
    w.u8(o.optional ? 1 : 0);
  }
  w.podVector(revealed_);

  // Computer players.
  ai_.save(w);

  // Tutorials and campaigns.
  for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
    w.str(hints_[p]);
    w.str(highlights_[p]);
    w.podVector(buildLocks_[p]);
    w.pod(ordersGiven_[p]);
  }
  w.u32(static_cast<uint32_t>(flags_.size()));
  for (const auto& [k, v] : flags_) {
    w.str(k);
    w.i32(v);
  }

  // Formations on the march, and what shape each unit was last given.
  savePool<CFormationMember>(w, reg_);
  {
    auto& fp = reg_.pool<CFormation>();
    w.podVector(fp.entities());
    for (const CFormation& f : fp.data()) {
      w.u8(f.owner);
      w.u8(f.shape);
      w.u8(f.loco);
      w.u8(static_cast<uint8_t>((f.reverse ? 1 : 0) | (f.moving ? 2 : 0) | (f.pursuing ? 4 : 0)));
      w.podVector(f.path);
      w.u32(f.index);
      w.pod(f.start);
      w.pod(f.anchor);
      w.pod(f.dest);
      w.pod(f.leash);
      w.u16(f.facing);
      w.u16(f.finalFacing);
      w.i32(f.remaining);
      w.u16(f.stall);
      w.podVector(f.members);
    }
  }
}

std::vector<std::string> Simulation::typeIds() const {
  std::vector<std::string> ids;
  ids.reserve(types_.size());
  for (int t = 0; t < static_cast<int>(types_.size()); ++t) ids.push_back(types_.get(t).id);
  return ids;
}

bool Simulation::loadState(ByteReader& r, std::string& error, const std::vector<std::string>* savedTypes,
                           bool* remapped) {
  if (remapped) *remapped = false;
  const uint32_t magic = r.u32();
  const int version = magic == kStateMagic ? 5 : magic == kStateMagicV4 ? 4 : magic == kStateMagicV3 ? 3 : magic == kStateMagicV2 ? 2 : 0;
  if (version == 0) {
    error = "not a saved game state";
    return false;
  }
  if (static_cast<int>(r.u32()) != map_.width() || static_cast<int>(r.u32()) != map_.height()) {
    error = "saved state doesn't match the map";
    return false;
  }
  tick_ = r.u32();
  const uint64_t st = r.u64(), inc = r.u64();
  rng_.restore(st, inc);
  pending_.clear();
  const uint32_t nPending = r.u32();
  for (uint32_t i = 0; i < nPending && r.ok(); ++i) pending_.push_back(readCommand(r));

  Registry::State rs;
  rs.generations = r.podVector<uint32_t>();
  rs.used = r.podVector<uint8_t>();
  rs.freeList = r.podVector<uint32_t>();
  rs.alive = r.u64();
  reg_.clear();
  reg_.restoreState(std::move(rs));
  loadPool<CPosition>(r, reg_);
  loadPool<CPrevPosition>(r, reg_);
  loadPool<CFacing>(r, reg_);
  loadPool<CUnit>(r, reg_);
  loadPool<CTurret>(r, reg_);
  loadPool<COrder>(r, reg_);
  loadPool<CCombat>(r, reg_);
  loadPool<CAnim>(r, reg_);
  loadPool<CProjectile>(r, reg_);
  loadPool<CStructure>(r, reg_);
  loadPool<CProducer>(r, reg_);
  loadPool<CHarvester>(r, reg_);
  loadPool<CGroupMember>(r, reg_);
  loadPool<CGarrisoned>(r, reg_);
  {
    std::vector<uint32_t> ents = r.podVector<uint32_t>();
    std::vector<CGroup> groups(ents.size());
    for (CGroup& c : groups) {
      c.owner = r.u8();
      const uint8_t kind = r.u8();
      c.vehicles = kind != 0;
      c.combined = kind == 2;
      c.echelon = static_cast<Echelon>(r.u8());
      c.number = r.u16();
      c.members = r.podVector<Entity>();
    }
    reg_.pool<CGroup>().restore(std::move(ents), std::move(groups));
    std::vector<uint32_t> gents = r.podVector<uint32_t>();
    std::vector<CGarrison> gars(gents.size());
    for (CGarrison& c : gars) c.occupants = r.podVector<Entity>();
    reg_.pool<CGarrison>().restore(std::move(gents), std::move(gars));
  }
  groupNumbers_ = r.pod<decltype(groupNumbers_)>();
  {
    std::vector<uint32_t> ents = r.podVector<uint32_t>();
    std::vector<CMover> movers(ents.size());
    for (CMover& m : movers) {
      m.path = r.podVector<Vec2i>();
      m.index = r.u32();
      m.active = r.u8() != 0;
      m.request = r.u32();
      m.requestGoal = r.pod<Vec2i>();
      m.bestDist = r.i32();
      m.stuckTicks = r.u16();
      m.repaths = r.u8();
      m.failures = r.u8();
      m.retryTick = r.u32();
    }
    reg_.pool<CMover>().restore(std::move(ents), std::move(movers));
  }

  pathQueue_ = r.podVector<PathRequest>();
  pathQueueHead_ = 0;
  nextRequest_ = r.u32();

  team_ = r.pod<std::array<uint8_t, MAX_PLAYERS>>();
  for (PlayerState& ps : players_) {
    ps.credits = r.i32();
    ps.powerMade = r.i32();
    ps.powerUsed = r.i32();
    for (ProductionQueue& q : ps.queues) readQueue(r, q);
    ps.defeated = r.u8() != 0;
    ps.won = r.u8() != 0;
    ps.incomeAcc = r.i32();
  }
  playedOnce_ = r.pod<std::array<bool, MAX_PLAYERS>>();
  scriptDefeated_ = r.pod<std::array<bool, MAX_PLAYERS>>();

  tileStructure_ = r.podVector<uint32_t>();
  cover_ = r.podVector<uint8_t>();
  coverDirty_ = r.u8() != 0;
  structureReach_ = r.i32();
  std::vector<uint8_t> blocks = r.podVector<uint8_t>();
  const size_t tiles = static_cast<size_t>(map_.width()) * map_.height();
  if (blocks.size() != tiles || tileStructure_.size() != tiles || cover_.size() != tiles) {
    error = "saved state is damaged (tile layers)";
    return false;
  }
  map_.blockData() = std::move(blocks);
  map_.blocksChanged();  // paths are rebuilt around the structures

  for (auto& g : vision_.grids()) {
    g = r.podVector<uint8_t>();
    if (g.size() != tiles) g.assign(tiles, 0);
  }
  vision_.revealFlags() = r.pod<std::array<bool, Vision::kMaxPlayers>>();
  vision_.activeFlags() = r.pod<std::array<bool, Vision::kMaxPlayers>>();
  vision_.touch();

  const std::string scriptText = r.str();
  const nlohmann::json sj = nlohmann::json::parse(scriptText, nullptr, false);
  script_ = sj.is_discarded() ? ScenarioScript{} : scriptFromJson(sj, &types_);
  triggerState_ = r.podVector<TriggerState>();
  triggerState_.resize(script_.triggers.size());
  objectives_.clear();
  const uint32_t nObj = r.u32();
  for (uint32_t i = 0; i < nObj && r.ok() && i < 1000; ++i) {
    Objective o;
    o.text = r.str();
    o.player = r.u8();
    o.state = r.u8();
    o.optional = r.u8() != 0;
    objectives_.push_back(std::move(o));
  }
  revealed_ = r.podVector<RevealedArea>();
  if (!ai_.load(r, version) || !r.ok()) {
    error = "saved state is damaged";
    return false;
  }
  for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
    hints_[p].clear();
    highlights_[p].clear();
    buildLocks_[p].clear();
    ordersGiven_[p] = {};
  }
  flags_.clear();
  if (version >= 4) {
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
      hints_[p] = r.str();
      highlights_[p] = r.str();
      buildLocks_[p] = r.podVector<uint8_t>();
      ordersGiven_[p] = r.pod<std::array<uint32_t, 32>>();
    }
    const uint32_t nf = r.u32();
    for (uint32_t i = 0; i < nf && r.ok() && i < 10000; ++i) {
      std::string k = r.str();
      flags_[k] = r.i32();
    }
    if (!r.ok()) {
      error = "saved state is damaged";
      return false;
    }
  }
  if (version >= 5) {
    loadPool<CFormationMember>(r, reg_);
    std::vector<uint32_t> ents = r.podVector<uint32_t>();
    std::vector<CFormation> forms(ents.size());
    for (CFormation& f : forms) {
      f.owner = r.u8();
      f.shape = r.u8();
      f.loco = static_cast<uint8_t>(r.u8() % 3);
      const uint8_t bits = r.u8();
      f.reverse = (bits & 1) != 0;
      f.moving = (bits & 2) != 0;
      f.pursuing = (bits & 4) != 0;
      f.path = r.podVector<Vec2i>();
      f.index = r.u32();
      f.start = r.pod<Vec2i>();
      f.anchor = r.pod<Vec2i>();
      f.dest = r.pod<Vec2i>();
      f.leash = r.pod<Vec2i>();
      f.facing = r.u16();
      f.finalFacing = r.u16();
      f.remaining = r.i32();
      f.stall = r.u16();
      f.members = r.podVector<Entity>();
    }
    reg_.pool<CFormation>().restore(std::move(ents), std::move(forms));
    if (!r.ok()) {
      error = "saved state is damaged";
      return false;
    }
  }
  // Saved with the unit types in another order (a newer roster, other mods)?
  // Renumber by id; a type these rules don't have at all refuses the load.
  if (savedTypes) {
    std::vector<int> to(savedTypes->size(), -1);
    bool same = savedTypes->size() <= types_.size();
    for (size_t i = 0; i < savedTypes->size(); ++i) {
      to[i] = types_.find((*savedTypes)[i]);
      if (to[i] != static_cast<int>(i)) same = false;
    }
    if (!same) {
      auto map = [&](uint16_t& type) {
        if (type >= to.size()) return true;  // caught by the check below
        if (to[type] < 0) {
          error = "the game uses a unit type these rules don't have (" + (*savedTypes)[type] +
                  "; enable the mods it was made with)";
          return false;
        }
        type = static_cast<uint16_t>(to[type]);
        return true;
      };
      for (CUnit& u : reg_.pool<CUnit>().data())
        if (!map(u.type)) return false;
      for (PlayerState& ps : players_)
        for (ProductionQueue& q : ps.queues)
          for (uint16_t& t : q.items)
            if (!map(t)) return false;
      if (remapped) *remapped = true;
    }
  }
  // Made with other rules (mods)? Refuse rather than read past the end of a table.
  for (const CUnit& u : reg_.pool<CUnit>().data())
    if (u.type >= types_.size()) {
      error = "the game uses unit types these rules don't have (enable the mods it was made with)";
      return false;
    }
  for (const CProjectile& p : reg_.pool<CProjectile>().data())
    if (p.weapon >= weapons_.size()) {
      error = "the game uses weapons these rules don't have (enable the mods it was made with)";
      return false;
    }
  events_.clear();
  spawnLog_.clear();
  dead_.clear();
  pathfinder_.refresh();
  return true;
}

}  // namespace slave

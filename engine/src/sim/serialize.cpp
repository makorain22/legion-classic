// Pioneer Engine - saving and loading the simulation state, and commands.
//
// The state is a list of tagged sections, one per system (see
// stateSections()). Component pools are written with their entities in
// storage order, so a loaded game iterates units in exactly the same order
// and the simulation continues identically (the tests check the state hash).
// Padding inside plain structs is blanked, so equal states give equal bytes.
#include <algorithm>
#include <cstring>

#include <nlohmann/json.hpp>

#include "pioneer/core/chunks.hpp"

#include "pioneer/sim/simulation.hpp"

namespace pioneer {

namespace {
constexpr uint32_t kStateMagic = 0x53494D39;  // "SIM9": tagged chunks, one section per system (Pioneer 2.0)
constexpr uint32_t kStateMagicV8 = 0x53494D38;  // "SIM8": the mission engine (play area, countdowns, named units...)
constexpr uint32_t kStateMagicV7 = 0x53494D37;  // "SIM7": the computer's main enemy, raid rotation and scout
constexpr uint32_t kStateMagicV6 = 0x53494D36;  // "SIM6": logistics points
constexpr uint32_t kStateMagicV5 = 0x53494D35;  // "SIM5": formations
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

// Plain component pools, with the tag each is saved under.
#define PNR_POD_POOLS(X)    \
  X(CPosition, "CPOS")      \
  X(CPrevPosition, "CPRV")  \
  X(CFacing, "CFAC")        \
  X(CUnit, "CUNI")          \
  X(CTurret, "CTUR")        \
  X(COrder, "CORD")         \
  X(CCombat, "CCOM")        \
  X(CAnim, "CANI")          \
  X(CProjectile, "CPRJ")    \
  X(CStructure, "CSTR")     \
  X(CProducer, "CPRD")      \
  X(CHarvester, "CHAR")     \
  X(CGroupMember, "CGRM")   \
  X(CGarrisoned, "CGAR")    \
  X(CCondition, "CCND")

template <class T>
void savePoolTagged(ByteWriter& w, Registry& reg, uint32_t tag) {
  auto& p = reg.pool<T>();
  w.u32(tag);
  w.u32(static_cast<uint32_t>(sizeof(T)));
  w.podVector(p.entities());
  if constexpr (reflect::isPacked<T>) w.raw(p.data().data(), p.data().size() * sizeof(T));
  else w.masked(p.data().data(), sizeof(T), p.data().size(), reflect::podMask<T>().data());
}
// `itemSize`: the size one item had when it was saved. A component that has
// grown since keeps its defaults in the new fields (they are at its end).
template <class T>
void restorePoolTagged(Registry& reg, std::vector<uint32_t> ents, const uint8_t* bytes, uint32_t itemSize) {
  std::vector<T> data(ents.size());
  const size_t take = std::min<size_t>(itemSize, sizeof(T));
  for (size_t i = 0; i < data.size(); ++i) std::memcpy(static_cast<void*>(&data[i]), bytes + i * itemSize, take);
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
  w.str(c.text);
}

Command readCommand(ByteReader& r, bool withText) {
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
  if (withText) c.text = r.str();
  return c;
}

// ---------------------------------------------------------------------------
// Sections. The state is saved as tagged chunks (core/chunks.hpp), one per
// system. Each system writes and reads its own section here; a state saved
// before the chunked format (SIM2..SIM8) holds the same sections one after
// another with no table, and is read by the same code.
// ---------------------------------------------------------------------------
namespace sect {
constexpr uint32_t Head = fourcc("HEAD"), Cmds = fourcc("CMDS"), Ecs = fourcc("ECS "), Groups = fourcc("GRPS"),
                   Movers = fourcc("MOVE"), Paths = fourcc("PATH"), Sides = fourcc("SIDE"), Tiles = fourcc("TILE"),
                   Vision_ = fourcc("VISN"), Script = fourcc("SCRP"), Ai = fourcc("AI  "), Tutorial = fourcc("TUTR"),
                   Formations = fourcc("FORM"), Logistics = fourcc("LOGI"), Mission = fourcc("MISN"),
                   Vision2 = fourcc("VIS2"), Mission2 = fourcc("MIS2");
}

const std::vector<Simulation::SectionInfo>& Simulation::stateSections() {
  // In the order they are written and read. `since`: the old state version
  // that first had the section (older saves get its defaults).
  static const std::vector<SectionInfo> list = {
      {sect::Head, "HEAD", "map size, tick, random numbers", true, 2},
      {sect::Cmds, "CMDS", "commands waiting for the next tick", true, 2},
      {sect::Ecs, "ECS", "entities and their plain components", true, 2},
      {sect::Groups, "GRPS", "bound units and garrisons", true, 2},
      {sect::Movers, "MOVE", "routes units are following", true, 2},
      {sect::Paths, "PATH", "route requests waiting their turn", true, 2},
      {sect::Sides, "SIDE", "teams, supplies, production lines", true, 2},
      {sect::Tiles, "TILE", "structures on the ground, cover, blocked tiles", true, 2},
      {sect::Vision_, "VISN", "what each side sees and has seen", true, 2},
      {sect::Script, "SCRP", "the scenario script, objectives", true, 2},
      {sect::Ai, "AI", "the computer players", true, 2},
      {sect::Tutorial, "TUTR", "hints, build locks, orders given, campaign flags", false, 4},
      {sect::Formations, "FORM", "formations on the march", false, 5},
      {sect::Logistics, "LOGI", "logistics points", false, 6},
      {sect::Mission, "MISN", "the mission engine: play area, clocks, named units", false, 8},
      {sect::Vision2, "VIS2", "vision rules (woodland, line of sight, weather) and smoke", false, 9},
      {sect::Mission2, "MIS2", "the mission model: phase, waves on their way, routes, goals", false, 9},
  };
  return list;
}

void Simulation::saveSection(uint32_t tag, ByteWriter& w) const {
  switch (tag) {
    case sect::Head:
      w.u32(static_cast<uint32_t>(map_.width()));
      w.u32(static_cast<uint32_t>(map_.height()));
      w.u32(tick_);
      w.u64(rng_.state());
      w.u64(rng_.increment());
      break;
    case sect::Cmds:
      w.u32(static_cast<uint32_t>(pending_.size()));
      for (const Command& c : pending_) writeCommand(w, c);
      break;
    case sect::Ecs: {
      const Registry::State rs = reg_.state();
      w.podVector(rs.generations);
      w.podVector(rs.used);
      w.podVector(rs.freeList);
      w.u64(rs.alive);
      // Plain components, each under its own tag with the size of one item:
      // a reader skips pools it doesn't know and copes with a component that
      // has grown since.
      uint32_t count = 0;
#define PNR_COUNT(T, TAG) ++count;
      PNR_POD_POOLS(PNR_COUNT)
#undef PNR_COUNT
      w.u32(count);
#define PNR_SAVE(T, TAG) savePoolTagged<T>(w, reg_, fourcc(TAG));
      PNR_POD_POOLS(PNR_SAVE)
#undef PNR_SAVE
      break;
    }
    case sect::Groups: {  // Groups and garrisons hold lists, so they are written field by field.
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
      w.pod(groupNumbers_);
      break;
    }
    case sect::Movers: {  // Movers hold paths, so they are written field by field.
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
      break;
    }
    case sect::Paths: {
      std::vector<PathRequest> waiting(pathQueue_.begin() + static_cast<long>(pathQueueHead_), pathQueue_.end());
      w.podVector(waiting);
      w.u32(nextRequest_);
      break;
    }
    case sect::Sides:
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
      break;
    case sect::Tiles:
      w.podVector(tileStructure_);
      w.podVector(cover_);
      w.u8(coverDirty_ ? 1 : 0);
      w.i32(structureReach_);
      w.podVector(const_cast<Map&>(map_).blockData());
      break;
    case sect::Vision_: {
      Vision& v = const_cast<Vision&>(vision_);
      for (const auto& g : v.grids()) w.podVector(g);
      w.pod(v.revealFlags());
      w.pod(v.activeFlags());
      break;
    }
    case sect::Script:
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
      break;
    case sect::Ai:
      ai_.save(w);
      break;
    case sect::Tutorial:
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
      break;
    case sect::Formations: {  // formations on the march, and what shape each unit was last given
      savePool<CFormationMember>(w, reg_);
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
        w.u16(f.fight);
        w.podVector(f.members);
      }
      break;
    }
    case sect::Logistics:  // who is taking each point, and how far along
      w.u32(static_cast<uint32_t>(points_.size()));
      for (const PointState& ps : points_) {
        w.u8(ps.side);
        w.u16(ps.progress);
      }
      break;
    case sect::Mission:
      saveMission(w);
      break;
    case sect::Mission2:
      w.i32(phase_);
      w.u32(phaseTick_);
      w.podVector(waveRuns_);
      w.u32(static_cast<uint32_t>(routes_.size()));
      for (const Route& rt : routes_) {
        w.podVector(rt.units);
        w.i32(rt.path);
        w.u32(rt.next);
        w.u8(rt.attack ? 1 : 0);
      }
      w.podVector(goalState_);
      break;
    case sect::Vision2:
      w.u16(visionRules_.flags);
      w.u16(visionRules_.sightPercent);
      w.podVector(smoke_);
      break;
    default:
      break;
  }
}

void Simulation::saveState(ByteWriter& w) const {
  w.u32(kStateMagic);
  ChunkWriter chunks(w);
  for (const SectionInfo& s : stateSections()) {
    chunks.begin(s.tag, 1, s.required ? chunk::kRequired : 0);
    saveSection(s.tag, w);
    chunks.end();
  }
}

Simulation::StateDigest Simulation::digest() const {
  StateDigest d;
  d.tick = tick_;
  uint64_t total = 1469598103934665603ULL;
  for (const SectionInfo& s : stateSections()) {
    ByteWriter w;
    saveSection(s.tag, w);
    const uint64_t h = hashBytes(w.bytes().data(), w.bytes().size());
    d.sections.push_back({s.tag, h, static_cast<uint32_t>(w.bytes().size())});
    total = hashBytes(reinterpret_cast<const uint8_t*>(&h), sizeof(h), total);
  }
  d.total = total;
  return d;
}

std::vector<std::string> Simulation::digestDifference(const StateDigest& a, const StateDigest& b) {
  std::vector<std::string> out;
  for (const SectionInfo& s : stateSections()) {
    const StateDigest::Section* x = nullptr;
    const StateDigest::Section* y = nullptr;
    for (const auto& q : a.sections)
      if (q.tag == s.tag) x = &q;
    for (const auto& q : b.sections)
      if (q.tag == s.tag) y = &q;
    if ((x == nullptr) != (y == nullptr) || (x && y && x->hash != y->hash)) out.push_back(s.name);
  }
  return out;
}

std::vector<std::string> Simulation::typeIds() const {
  std::vector<std::string> ids;
  ids.reserve(types_.size());
  for (int t = 0; t < static_cast<int>(types_.size()); ++t) ids.push_back(types_.get(t).id);
  return ids;
}

// Reads one section. `version`: the state format (2..8: the old single
// stream; 9: chunks).
bool Simulation::loadSection(uint32_t tag, ByteReader& r, int version, std::string& error) {
  const size_t tiles = static_cast<size_t>(map_.width()) * map_.height();
  auto damaged = [&](const char* where) {
    error = std::string("saved state is damaged (") + where + ")";
    return false;
  };
  switch (tag) {
    case sect::Head: {
      if (static_cast<int>(r.u32()) != map_.width() || static_cast<int>(r.u32()) != map_.height()) {
        error = "saved state doesn't match the map";
        return false;
      }
      tick_ = r.u32();
      const uint64_t st = r.u64(), inc = r.u64();
      rng_.restore(st, inc);
      return r.ok() || damaged("header");
    }
    case sect::Cmds: {
      pending_.clear();
      const uint32_t nPending = r.u32();
      for (uint32_t i = 0; i < nPending && r.ok(); ++i) pending_.push_back(readCommand(r, version >= 8));
      return r.ok() || damaged("commands");
    }
    case sect::Ecs: {
      Registry::State rs;
      rs.generations = r.podVector<uint32_t>();
      rs.used = r.podVector<uint8_t>();
      rs.freeList = r.podVector<uint32_t>();
      rs.alive = r.u64();
      reg_.clear();
      reg_.restoreState(std::move(rs));
      if (version >= 9) {
        const uint32_t pools = r.u32();
        for (uint32_t i = 0; i < pools && r.ok(); ++i) {
          const uint32_t poolTag = r.u32();
          const uint32_t itemSize = r.u32();
          std::vector<uint32_t> ents = r.podVector<uint32_t>();
          const uint64_t bytes = static_cast<uint64_t>(ents.size()) * itemSize;
          if (!r.ok() || bytes > r.remaining()) return damaged("components");
          const uint8_t* data = r.cursor();
          r.skip(static_cast<size_t>(bytes));
          bool known = false;
#define PNR_LOAD(T, TAG)                                                \
  if (poolTag == fourcc(TAG)) {                                         \
    restorePoolTagged<T>(reg_, std::move(ents), data, itemSize);        \
    known = true;                                                       \
  }
          PNR_POD_POOLS(PNR_LOAD)
#undef PNR_LOAD
          if (!known) loadNotes_.push_back("components '" + fourccName(poolTag) + "' are not known to this version and were left out");
        }
      } else {
        // The old stream: the fourteen pools it had, in its order.
#define PNR_LOAD(T, TAG) \
  if (fourcc(TAG) != fourcc("CCND")) loadPool<T>(r, reg_);
        PNR_POD_POOLS(PNR_LOAD)
#undef PNR_LOAD
      }
      return r.ok() || damaged("entities");
    }
    case sect::Groups: {
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
      groupNumbers_ = r.pod<decltype(groupNumbers_)>();
      return r.ok() || damaged("groups");
    }
    case sect::Movers: {
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
      return r.ok() || damaged("routes");
    }
    case sect::Paths:
      pathQueue_ = r.podVector<PathRequest>();
      pathQueueHead_ = 0;
      nextRequest_ = r.u32();
      return r.ok() || damaged("route requests");
    case sect::Sides:
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
      return r.ok() || damaged("sides");
    case sect::Tiles: {
      tileStructure_ = r.podVector<uint32_t>();
      cover_ = r.podVector<uint8_t>();
      coverDirty_ = r.u8() != 0;
      structureReach_ = r.i32();
      std::vector<uint8_t> blocks = r.podVector<uint8_t>();
      if (blocks.size() != tiles || tileStructure_.size() != tiles || cover_.size() != tiles) return damaged("tile layers");
      map_.blockData() = std::move(blocks);
      map_.blocksChanged();  // paths are rebuilt around the structures
      return r.ok() || damaged("tile layers");
    }
    case sect::Vision_:
      for (auto& g : vision_.grids()) {
        g = r.podVector<uint8_t>();
        if (g.size() != tiles) g.assign(tiles, 0);
      }
      vision_.revealFlags() = r.pod<std::array<bool, Vision::kMaxPlayers>>();
      vision_.activeFlags() = r.pod<std::array<bool, Vision::kMaxPlayers>>();
      vision_.touch();
      return r.ok() || damaged("vision");
    case sect::Script: {
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
      return r.ok() || damaged("script");
    }
    case sect::Ai:
      return (ai_.load(r, std::min(version, 8)) && r.ok()) || damaged("computer players");
    case sect::Tutorial: {
      for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        hints_[p] = r.str();
        highlights_[p] = r.str();
        buildLocks_[p] = r.podVector<uint8_t>();
        ordersGiven_[p] = r.pod<std::array<uint32_t, 32>>();
      }
      flags_.clear();
      const uint32_t nf = r.u32();
      for (uint32_t i = 0; i < nf && r.ok() && i < 10000; ++i) {
        std::string k = r.str();
        flags_[k] = r.i32();
      }
      return r.ok() || damaged("campaign flags");
    }
    case sect::Formations: {
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
        if (version >= 6) f.fight = r.u16();
        f.members = r.podVector<Entity>();
      }
      reg_.pool<CFormation>().restore(std::move(ents), std::move(forms));
      return r.ok() || damaged("formations");
    }
    case sect::Logistics: {
      const uint32_t n = r.u32();
      if (n > 4096) return damaged("logistics points");
      points_.resize(n);
      for (PointState& ps : points_) {
        ps.side = r.u8();
        ps.progress = r.u16();
      }
      return r.ok() || damaged("logistics points");
    }
    case sect::Mission:
      return loadMission(r) || damaged("mission");
    case sect::Mission2: {
      phase_ = r.i32();
      phaseTick_ = r.u32();
      waveRuns_ = r.podVector<WaveRun>(256);
      routes_.clear();
      const uint32_t n = r.u32();
      if (n > 4096) return damaged("routes");
      routes_.resize(n);
      for (Route& rt : routes_) {
        rt.units = r.podVector<Entity>();
        rt.path = r.i32();
        rt.next = r.u32();
        rt.attack = r.u8() != 0;
      }
      goalState_ = r.podVector<GoalState>(4096);
      goalState_.resize(script_.goals.size());
      return r.ok() || damaged("mission model");
    }
    case sect::Vision2:
      visionRules_.flags = r.u16();
      visionRules_.sightPercent = static_cast<uint16_t>(std::clamp<int>(r.u16(), 10, 200));
      smoke_ = r.podVector<SmokeCloud>(256);
      groundDirty_ = true;
      vision_.setAnySmoke(true);  // the layer is laid again at the next look round
      return r.ok() || damaged("vision rules");
    default:
      return true;
  }
}

// A section the saved state doesn't have: its system starts fresh.
void Simulation::resetSection(uint32_t tag) {
  switch (tag) {
    case sect::Tutorial:
      for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        hints_[p].clear();
        highlights_[p].clear();
        buildLocks_[p].clear();
        ordersGiven_[p] = {};
      }
      flags_.clear();
      break;
    case sect::Formations:
      break;  // the pools were emptied with the registry
    case sect::Logistics:
      points_.clear();
      // Before 9.6 supply trucks hauled oil: none were in the rules, but be safe.
      for (CHarvester& h : reg_.pool<CHarvester>().data()) h = CHarvester{};
      break;
    case sect::Mission:
      resetMission();
      break;
    case sect::Mission2:
      phase_ = -1;
      phaseTick_ = 0;
      waveRuns_.clear();
      routes_.clear();
      goalState_.assign(script_.goals.size(), {});
      break;
    case sect::Vision2:
      visionRules_ = VisionRules{};
      smoke_.clear();
      groundDirty_ = true;
      vision_.setAnySmoke(true);
      break;
    default:
      break;
  }
}

bool Simulation::loadState(ByteReader& r, std::string& error, const std::vector<std::string>* savedTypes,
                           bool* remapped) {
  if (remapped) *remapped = false;
  loadNotes_.clear();
  const uint32_t magic = r.u32();
  const int version = magic == kStateMagic ? 9 : magic == kStateMagicV8 ? 8 : magic == kStateMagicV7 ? 7 : magic == kStateMagicV6 ? 6 : magic == kStateMagicV5 ? 5 : magic == kStateMagicV4 ? 4 : magic == kStateMagicV3 ? 3 : magic == kStateMagicV2 ? 2 : 0;
  if (version == 0) {
    error = "not a saved game state";
    return false;
  }
  if (version >= 9) {
    ChunkReader chunks;
    if (!chunks.parse(r)) {
      error = "saved state is damaged (sections)";
      return false;
    }
    for (const SectionInfo& s : stateSections()) {
      const ChunkInfo* c = chunks.find(s.tag);
      if (!c) {
        if (!s.required) {
          resetSection(s.tag);
          continue;
        }
        error = std::string("saved state is damaged (no ") + s.name + " section)";
        return false;
      }
      if (c->version > 1) {
        error = std::string("the ") + s.name + " section was written by a newer version of the game";
        return false;
      }
      ByteReader sub = c->reader();
      if (!loadSection(s.tag, sub, version, error)) return false;
      if (!sub.ok()) {
        error = std::string("saved state is damaged (") + s.name + ")";
        return false;
      }
    }
    for (const ChunkInfo& c : chunks.chunks()) {
      bool known = false;
      for (const SectionInfo& s : stateSections()) known = known || s.tag == c.tag;
      if (known) continue;
      if (c.flags & chunk::kRequired) {
        error = "the saved state needs a newer version of the game (section " + c.name() + ")";
        return false;
      }
      loadNotes_.push_back("section '" + c.name() + "' is not known to this version and was left out");
    }
  } else {
    for (const SectionInfo& s : stateSections()) {
      if (version < s.since) {
        resetSection(s.tag);
        continue;
      }
      if (!loadSection(s.tag, r, version, error)) return false;
    }
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

}  // namespace pioneer

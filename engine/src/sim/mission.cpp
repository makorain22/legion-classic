// Slave Engine - the mission engine (9.9): play area, countdowns, markers,
// counters, named units and script flags, patrols, kills and losses. The
// trigger language that drives them is in script.cpp.
#include <algorithm>

#include "slave/sim/simulation.hpp"

namespace slave {

// --- Play area ------------------------------------------------------------------
TileRect Simulation::playArea(uint8_t player) const {
  if (player < MAX_PLAYERS && !playArea_[player].empty()) return playArea_[player];
  return {0, 0, map_.width() - 1, map_.height() - 1};
}

bool Simulation::inPlayArea(uint8_t player, int tx, int ty) const { return playArea(player).contains(tx, ty); }

Vec2i Simulation::clampToPlayArea(uint8_t player, Vec2i p) const {
  if (!playAreaLimited(player)) return p;
  const TileRect r = playArea(player);
  return {std::clamp(p.x, r.x0 * LEPTONS_PER_TILE + 64, (r.x1 + 1) * LEPTONS_PER_TILE - 64),
          std::clamp(p.y, r.y0 * LEPTONS_PER_TILE + 64, (r.y1 + 1) * LEPTONS_PER_TILE - 64)};
}

void Simulation::setPlayArea(uint8_t player, TileRect r) {
  if (r.x0 <= 0 && r.y0 <= 0 && r.x1 >= map_.width() - 1 && r.y1 >= map_.height() - 1) r = TileRect{};  // all of it: no limit
  for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
    if (player == ANY_PLAYER || p == player) playArea_[p] = r;
}

int32_t Simulation::countdownLeft(const std::string& label) const {
  for (const MissionCountdown& d : countdowns_) {
    if (d.label != label) continue;
    if (d.done) return 0;
    const uint32_t left = d.paused ? d.left : (d.endTick > tick_ ? d.endTick - tick_ : 0);
    return static_cast<int32_t>((left + TICKS_PER_SECOND - 1) / TICKS_PER_SECOND);
  }
  return -1;
}

// --- Named units and script flags -------------------------------------------------
const std::string& Simulation::unitName(Entity e) const {
  static const std::string none;
  if (!reg_.alive(e)) return none;
  const CScripted* s = reg_.pool<CScripted>().tryGet(e.index);
  return s && s->name < names_.size() ? names_[s->name] : none;
}

uint8_t Simulation::scriptFlags(Entity e) const {
  if (!reg_.alive(e)) return 0;
  const CScripted* s = reg_.pool<CScripted>().tryGet(e.index);
  return s ? s->flags : 0;
}

bool Simulation::invulnerable(uint32_t e) const {
  const CScripted* s = reg_.pool<CScripted>().tryGet(e);
  return s && (s->flags & kScriptInvulnerable);
}

bool Simulation::aiMayCommand(uint32_t e) const {
  const CScripted* s = reg_.pool<CScripted>().tryGet(e);
  return !s || !(s->flags & kScriptNoAi);
}

std::vector<Entity> Simulation::named(const std::string& name) const {
  std::vector<Entity> out;
  if (name.empty()) return out;
  int32_t idx = -1;
  for (size_t i = 1; i < names_.size(); ++i)
    if (names_[i] == name) idx = static_cast<int32_t>(i);
  if (idx < 0) return out;
  auto& pool = reg_.pool<CScripted>();
  for (size_t i = 0; i < pool.size(); ++i) {
    if (pool.data()[i].name != idx) continue;
    const uint32_t e = pool.entities()[i];
    const CUnit* u = reg_.pool<CUnit>().tryGet(e);
    if (u && u->health > 0) out.push_back(reg_.handle(e));
  }
  std::sort(out.begin(), out.end(), [](Entity a, Entity b) { return a.index < b.index; });
  return out;
}

int32_t Simulation::nameIndex(const std::string& name) {
  if (name.empty()) return 0;
  for (size_t i = 1; i < names_.size(); ++i)
    if (names_[i] == name) return static_cast<int32_t>(i);
  if (names_.size() >= 65535) return 0;
  names_.push_back(name);
  return static_cast<int32_t>(names_.size()) - 1;
}

void Simulation::setScriptFlags(Entity e, uint8_t mask, uint8_t bits) {
  if (!reg_.alive(e) || !reg_.has<CUnit>(e)) return;
  auto& pool = reg_.pool<CScripted>();
  CScripted* s = pool.tryGet(e.index);
  if (!s) {
    if (!(bits & mask)) return;
    s = &pool.add(e.index);
  }
  s->flags = static_cast<uint8_t>((s->flags & ~mask) | (bits & mask));
  if (!s->flags && !s->name) pool.remove(e.index);
}

void Simulation::setUnitName(Entity e, int32_t name) {
  if (!reg_.alive(e) || !reg_.has<CUnit>(e)) return;
  auto& pool = reg_.pool<CScripted>();
  CScripted* s = pool.tryGet(e.index);
  if (!s) {
    if (!name) return;
    s = &pool.add(e.index);
  }
  s->name = static_cast<uint16_t>(std::clamp(name, 0, 65535));
  if (!s->flags && !s->name) pool.remove(e.index);
}

bool Simulation::commandable(const Command& cmd, Entity e) const {
  if (scriptCommand_ || cmd.player >= MAX_PLAYERS) return true;
  const uint8_t f = scriptFlags(e);
  if (!f) return true;
  if (ai_.player(cmd.player).enabled) return !(f & kScriptNoAi);
  return !(f & kScriptLocked);
}

// --- Every tick -----------------------------------------------------------------------
void Simulation::stepMission() {
  for (MissionCountdown& d : countdowns_) {
    if (d.done || d.paused || tick_ < d.endTick) continue;
    d.done = true;
    SimEvent ev;
    ev.type = EventType::CountdownDone;
    ev.owner = d.player;
    ev.text = d.label;
    events_.push_back(ev);
  }
  // Patrols: once every walker of a patrol has arrived (or stopped), the other way.
  if (tick_ % TICKS_PER_SECOND == 0 && !patrols_.empty()) {
    for (Patrol& pt : patrols_) {
      std::erase_if(pt.units, [&](Entity e) { return !reg_.alive(e) || !reg_.has<CUnit>(e) || reg_.get<CUnit>(e).health <= 0; });
      if (pt.units.empty()) continue;
      bool moving = false, onOrder = false;
      for (Entity e : pt.units) {
        const COrder& o = reg_.get<COrder>(e);
        if (o.type == OrderType::Move || o.type == OrderType::AttackMove) moving = true;
        if (o.type == OrderType::Idle || o.type == OrderType::Move || o.type == OrderType::AttackMove) onOrder = true;
      }
      if (moving || !onOrder) continue;  // still walking, or busy with something else (an attack)
      pt.leg ^= 1;
      Command cmd;
      orderMove(pt.units, pt.leg ? pt.a : pt.b, OrderType::AttackMove, cmd);
    }
    std::erase_if(patrols_, [](const Patrol& pt) { return pt.units.empty(); });
  }
}

void Simulation::resetMission() {
  playArea_ = {};
  countdowns_.clear();
  markers_.clear();
  counters_.clear();
  kills_ = {};
  losses_ = {};
  incomeRate_.fill(100);
  cinematic_ = {};
  names_.assign(1, std::string());
  patrols_.clear();
  ai_.clearOrders();
}

// --- Saving (state version 8) ------------------------------------------------------
void Simulation::saveMission(ByteWriter& w) const {
  w.u32(0x4D495353);  // "MISS"
  w.pod(playArea_);
  w.u32(static_cast<uint32_t>(countdowns_.size()));
  for (const MissionCountdown& d : countdowns_) {
    w.str(d.label);
    w.u8(d.player);
    w.u32(d.endTick);
    w.u32(d.left);
    w.u8(static_cast<uint8_t>((d.paused ? 1 : 0) | (d.done ? 2 : 0)));
  }
  w.u32(static_cast<uint32_t>(markers_.size()));
  for (const MissionMarker& m : markers_) {
    w.str(m.label);
    w.u8(m.player);
    w.i32(m.area);
    w.u8(m.kind);
  }
  w.u32(static_cast<uint32_t>(counters_.size()));
  for (const auto& [k, v] : counters_) {
    w.str(k);
    w.i32(v);
  }
  w.pod(kills_);
  w.pod(losses_);
  w.pod(incomeRate_);
  w.pod(cinematic_);
  w.u8(difficulty_);
  w.u32(static_cast<uint32_t>(names_.size()));
  for (const std::string& n : names_) w.str(n);
  {
    auto& p = reg_.pool<CScripted>();
    w.podVector(p.entities());
    w.podVector(p.data());
  }
  w.u32(static_cast<uint32_t>(patrols_.size()));
  for (const Patrol& pt : patrols_) {
    w.podVector(pt.units);
    w.pod(pt.a);
    w.pod(pt.b);
    w.u8(pt.leg);
  }
  std::vector<int32_t> objectiveAreas;
  for (const Objective& o : objectives_) objectiveAreas.push_back(o.area);
  w.podVector(objectiveAreas);
  ai_.saveOrders(w);
}

bool Simulation::loadMission(ByteReader& r) {
  resetMission();
  if (r.u32() != 0x4D495353) return false;
  playArea_ = r.pod<std::array<TileRect, MAX_PLAYERS>>();
  const uint32_t nc = r.u32();
  if (nc > 4096) return false;
  for (uint32_t i = 0; i < nc && r.ok(); ++i) {
    MissionCountdown d;
    d.label = r.str();
    d.player = r.u8();
    d.endTick = r.u32();
    d.left = r.u32();
    const uint8_t f = r.u8();
    d.paused = f & 1;
    d.done = f & 2;
    countdowns_.push_back(std::move(d));
  }
  const uint32_t nm = r.u32();
  if (nm > 4096) return false;
  for (uint32_t i = 0; i < nm && r.ok(); ++i) {
    MissionMarker m;
    m.label = r.str();
    m.player = r.u8();
    m.area = r.i32();
    m.kind = r.u8();
    markers_.push_back(std::move(m));
  }
  const uint32_t nk = r.u32();
  if (nk > 65536) return false;
  for (uint32_t i = 0; i < nk && r.ok(); ++i) {
    std::string k = r.str();
    counters_[k] = r.i32();
  }
  kills_ = r.pod<std::array<int32_t, MAX_PLAYERS>>();
  losses_ = r.pod<std::array<int32_t, MAX_PLAYERS>>();
  incomeRate_ = r.pod<std::array<int32_t, MAX_PLAYERS>>();
  cinematic_ = r.pod<std::array<bool, MAX_PLAYERS>>();
  difficulty_ = std::min<uint8_t>(r.u8(), 2);
  const uint32_t nn = r.u32();
  if (nn == 0 || nn > 65536) return false;
  names_.clear();
  for (uint32_t i = 0; i < nn && r.ok(); ++i) names_.push_back(r.str());
  {
    std::vector<uint32_t> ents = r.podVector<uint32_t>();
    std::vector<CScripted> data = r.podVector<CScripted>();
    if (ents.size() != data.size()) return false;
    reg_.pool<CScripted>().restore(std::move(ents), std::move(data));
  }
  const uint32_t np = r.u32();
  if (np > 65536) return false;
  for (uint32_t i = 0; i < np && r.ok(); ++i) {
    Patrol pt;
    pt.units = r.podVector<Entity>();
    pt.a = r.pod<Vec2i>();
    pt.b = r.pod<Vec2i>();
    pt.leg = r.u8();
    patrols_.push_back(std::move(pt));
  }
  const std::vector<int32_t> objectiveAreas = r.podVector<int32_t>();
  for (size_t i = 0; i < objectiveAreas.size() && i < objectives_.size(); ++i) objectives_[i].area = objectiveAreas[i];
  return ai_.loadOrders(r) && r.ok();
}

}  // namespace slave

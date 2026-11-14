// Pioneer Engine - the mission engine (9.9): play area, countdowns, markers,
// counters, named units and script flags, patrols, kills and losses. The
// trigger language that drives them is in script.cpp.
#include <algorithm>

#include "pioneer/sim/simulation.hpp"

namespace pioneer {

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
  // Waves that are due arrive.
  for (size_t i = 0; i < waveRuns_.size(); ++i) {
    if (tick_ < waveRuns_[i].nextTick) continue;
    const int32_t wi = waveRuns_[i].wave;
    if (wi < 0 || wi >= static_cast<int32_t>(script_.waves.size())) {
      waveRuns_[i].left = 0;
      continue;
    }
    const MissionWave w = script_.waves[static_cast<size_t>(wi)];  // a copy: its actions may change the script
    launchWave(w);
    WaveRun& run = waveRuns_[i];  // (launching may have added runs: look it up again)
    --run.left;
    run.nextTick = tick_ + static_cast<uint32_t>(std::max(1, w.interval)) * TICKS_PER_SECOND;
  }
  std::erase_if(waveRuns_, [](const WaveRun& r) { return r.left <= 0; });
  // Routes: when everybody on one has arrived at a point (or stopped), on to the next.
  if (tick_ % TICKS_PER_SECOND == 0 && !routes_.empty()) {
    for (Route& rt : routes_) {
      std::erase_if(rt.units, [&](Entity e) { return !reg_.alive(e) || !reg_.has<CUnit>(e) || reg_.get<CUnit>(e).health <= 0; });
      if (rt.units.empty() || rt.path < 0 || rt.path >= static_cast<int32_t>(script_.paths.size())) {
        rt.units.clear();
        continue;
      }
      const MissionPath& path = script_.paths[static_cast<size_t>(rt.path)];
      bool moving = false, onOrder = false;
      for (Entity e : rt.units) {
        const COrder& o = reg_.get<COrder>(e);
        if (o.type == OrderType::Move || o.type == OrderType::AttackMove) moving = true;
        if (o.type == OrderType::Idle || o.type == OrderType::Move || o.type == OrderType::AttackMove) onOrder = true;
      }
      if (moving || !onOrder) continue;
      if (++rt.next >= path.points.size()) {
        if (!path.loop || path.points.empty()) {
          rt.units.clear();  // arrived at the end
          continue;
        }
        rt.next = 0;
      }
      const Vec2i p = path.points[rt.next];
      Command cmd;
      scriptCommand_ = true;
      orderMove(rt.units, {p.x * LEPTONS_PER_TILE + 128, p.y * LEPTONS_PER_TILE + 128}, rt.attack ? OrderType::AttackMove : OrderType::Move, cmd);
      scriptCommand_ = false;
    }
    std::erase_if(routes_, [](const Route& rt) { return rt.units.empty(); });
  }
}

// --- The mission model -----------------------------------------------------------
void Simulation::enterPhase(int32_t p) {
  if (p < 0 || p >= static_cast<int32_t>(script_.phases.size())) return;
  const int32_t left = phase_;
  phase_ = p;
  phaseTick_ = tick_;
  const MissionPhase ph = script_.phases[static_cast<size_t>(p)];  // a copy: its actions may change the script
  SimEvent ev;
  ev.type = EventType::PhaseChanged;
  ev.owner = ANY_PLAYER;
  ev.amount = p;
  ev.text = ph.name;
  events_.push_back(ev);
  if (!ph.music.empty()) {
    SimEvent m;
    m.type = EventType::PlayMusic;
    m.owner = ANY_PLAYER;
    m.text = ph.music;
    events_.push_back(m);
  }
  // Goals of the phase just left that only asked for something to be kept safe are met.
  for (size_t i = 0; i < script_.goals.size() && i < goalState_.size(); ++i) {
    const MissionGoal& g = script_.goals[i];
    GoalState& gs = goalState_[i];
    if (gs.state != 1 || g.type != "protect" || g.phase < 0 || g.phase != left) continue;
    gs.state = 2;
    if (gs.objective >= 0 && gs.objective < static_cast<int32_t>(objectives_.size())) objectives_[static_cast<size_t>(gs.objective)].state = 1;
    SimEvent oc;
    oc.type = EventType::ObjectivesChanged;
    oc.owner = g.player;
    events_.push_back(oc);
    const std::vector<Action> acts = g.onDone;
    for (const Action& a : acts) perform(a);
  }
  for (const Action& a : ph.onEnter) perform(a);
}

void Simulation::stepPhases() {
  // The mission begins in its first phase.
  if (!script_.phases.empty() && phase_ < 0) enterPhase(0);
  // A phase begins when the one it follows is current and its conditions hold.
  for (int32_t p = 0; p < static_cast<int32_t>(script_.phases.size()); ++p) {
    if (p == phase_) continue;
    const MissionPhase& ph = script_.phases[static_cast<size_t>(p)];
    if (ph.enter.empty()) continue;  // only by a Go to phase action
    const int32_t from = ph.from == -1 ? p - 1 : ph.from;
    if (ph.from != -2 && from != phase_) continue;
    bool ok = !ph.any;
    for (const Condition& c : ph.enter) ok = ph.any ? (ok || evaluate(c, SIZE_MAX)) : (ok && evaluate(c, SIZE_MAX));
    if (!ok) continue;
    enterPhase(p);
    break;
  }
  // The model's goals.
  if (goalState_.size() != script_.goals.size()) goalState_.resize(script_.goals.size());
  for (size_t i = 0; i < script_.goals.size(); ++i) {
    const MissionGoal g = script_.goals[i];  // a copy: its actions may change the script
    GoalState gs = goalState_[i];
    auto objective = [&](uint8_t state) {
      if (gs.objective >= 0 && gs.objective < static_cast<int32_t>(objectives_.size())) objectives_[static_cast<size_t>(gs.objective)].state = state;
      SimEvent ev;
      ev.type = EventType::ObjectivesChanged;
      ev.owner = g.player;
      events_.push_back(ev);
    };
    if (gs.state == 0) {
      if (g.phase >= 0 && g.phase != phase_) continue;
      Objective o;
      o.text = g.text;
      o.player = g.player;
      o.optional = g.optional;
      o.area = g.area;
      objectives_.push_back(std::move(o));
      gs.objective = static_cast<int32_t>(objectives_.size()) - 1;
      gs.since = tick_;
      gs.state = 1;
      goalState_[i] = gs;
      objective(0);
      continue;
    }
    if (gs.state != 1) continue;
    const int type = goalTypeFromId(g.type);
    const uint8_t who = g.player;
    auto controls = [&]() {
      if (g.area < 0 || g.area >= static_cast<int32_t>(script_.areas.size())) return false;
      bool own = false, enemy = false;
      for (Entity e : unitsIn(ANY_PLAYER, g.area, -1)) {
        const CUnit& u = reg_.get<CUnit>(e);
        if (u.health <= 0 || u.owner >= MAX_PLAYERS || types_.get(u.type).isStructure()) continue;
        (allied(u.owner, who) ? own : enemy) = true;
      }
      return own && !enemy;
    };
    bool done = false, failed = false;
    switch (type) {
      case 0:  // destroy
        done = named(g.unit).empty() && tick_ > gs.since;
        break;
      case 1:  // reach
        done = g.area >= 0 && countUnits(who, g.area, -1) >= std::max(1, g.value);
        break;
      case 2:  // clear
        done = controls();
        break;
      case 3:  // hold
        if (controls()) {
          if (!gs.held) gs.held = tick_;
          done = tick_ >= gs.held + static_cast<uint32_t>(std::max(0, g.value)) * TICKS_PER_SECOND;
        } else {
          gs.held = 0;
        }
        break;
      case 4:  // survive
        done = tick_ >= gs.since + static_cast<uint32_t>(std::max(0, g.value)) * TICKS_PER_SECOND;
        break;
      case 5: {  // escort
        const std::vector<Entity> units = named(g.unit);
        int in = 0;
        for (Entity e : units) {
          const CPosition& p = reg_.get<CPosition>(e);
          in += g.area >= 0 && g.area < static_cast<int32_t>(script_.areas.size()) &&
                script_.areas[static_cast<size_t>(g.area)].contains(p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT);
        }
        done = in >= std::max(1, g.value);
        failed = !done && units.empty() && tick_ > gs.since;
        break;
      }
      case 6:  // protect
        failed = named(g.unit).empty() && tick_ > gs.since;
        break;
      default:
        break;
    }
    if (done || failed) gs.state = done ? 2 : 3;
    goalState_[i] = gs;
    if (!done && !failed) continue;
    objective(done ? 1 : 2);
    for (const Action& a : done ? g.onDone : g.onFail) perform(a);
  }
}

void Simulation::sendWave(int32_t wave) {
  if (wave < 0 || wave >= static_cast<int32_t>(script_.waves.size()) || waveRuns_.size() >= 256) return;
  const MissionWave& w = script_.waves[static_cast<size_t>(wave)];
  waveRuns_.push_back({wave, tick_ + static_cast<uint32_t>(std::max(0, w.delay)) * TICKS_PER_SECOND, std::clamp(w.times, 1, 1000)});
}

// A wave arrives: its units are created under one name, then given what the
// wave says (flags, stance, binding, orders) through the ordinary actions.
void Simulation::launchWave(const MissionWave& w) {
  const std::string tag = w.unitName.empty() ? "wave:" + w.name : w.unitName;
  const size_t before = named(tag).size();
  for (const MissionWave::Entry& e : w.units) {
    const int32_t n = e.count[std::min<uint8_t>(difficulty_, 2)];
    if (e.unitType < 0 || n <= 0) continue;
    Action a;
    a.type = ActionType::SpawnNamed;
    a.player = w.player;
    a.area = w.area;
    a.unitType = e.unitType;
    a.value = n;
    a.text = tag;
    perform(a);
  }
  auto byName = [&](ActionType t, int32_t value) {
    Action a;
    a.type = t;
    a.text = tag;
    a.value = value;
    return a;
  };
  if (w.flags & kScriptInvulnerable) perform(byName(ActionType::SetInvulnerable, 1));
  if (w.flags & kScriptNoAi) perform(byName(ActionType::SetAiControl, 0));
  if (w.flags & kScriptUntargetable) perform(byName(ActionType::SetTargetable, 0));
  if (w.flags & kScriptLocked) perform(byName(ActionType::SetLocked, 1));
  if (w.stance >= 0 && w.stance < static_cast<int32_t>(Stance::Count)) perform(byName(ActionType::SetStance, w.stance));
  if (w.bind) perform(byName(ActionType::BindUnits, 0));
  if (w.order == MissionWave::AttackTo || w.order == MissionWave::MoveTo) {
    Action a = byName(w.order == MissionWave::AttackTo ? ActionType::AttackMove : ActionType::MoveUnits, 0);
    a.area2 = w.target;
    if (w.target >= 0) perform(a);
  } else if (w.order == MissionWave::AlongPath || w.order == MissionWave::AttackAlongPath) {
    followPath(named(tag), w.path, w.order == MissionWave::AttackAlongPath);
  }
  const std::vector<Entity> units = named(tag);
  SimEvent ev;
  ev.type = EventType::WaveArrived;
  ev.owner = w.player;
  ev.text = w.name;
  ev.amount = static_cast<int32_t>(units.size() - std::min(before, units.size()));
  if (!units.empty()) {
    const CPosition& p = reg_.get<CPosition>(units.back());
    ev.at = {p.x, p.y};
  }
  events_.push_back(ev);
}

void Simulation::followPath(const std::vector<Entity>& units, int32_t path, bool attack) {
  if (units.empty() || path < 0 || path >= static_cast<int32_t>(script_.paths.size())) return;
  const MissionPath& p = script_.paths[static_cast<size_t>(path)];
  if (p.points.empty()) return;
  // Units put on a new route leave the one they were on.
  for (Route& rt : routes_)
    std::erase_if(rt.units, [&](Entity e) { return std::find(units.begin(), units.end(), e) != units.end(); });
  std::erase_if(routes_, [](const Route& rt) { return rt.units.empty(); });
  Route rt;
  rt.units = units;
  rt.path = path;
  rt.next = 0;
  rt.attack = attack;
  Command cmd;
  scriptCommand_ = true;
  orderMove(rt.units, {p.points[0].x * LEPTONS_PER_TILE + 128, p.points[0].y * LEPTONS_PER_TILE + 128},
            attack ? OrderType::AttackMove : OrderType::Move, cmd);
  scriptCommand_ = false;
  routes_.push_back(std::move(rt));
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
  phase_ = -1;
  phaseTick_ = 0;
  waveRuns_.clear();
  routes_.clear();
  goalState_.assign(script_.goals.size(), {});
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

}  // namespace pioneer

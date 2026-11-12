#include "slave/sim/simulation.hpp"

#include <algorithm>
#include <chrono>

namespace slave {

Simulation::Simulation(Map& map, const UnitTypeRegistry& types, const WeaponRegistry& weapons, uint64_t seed)
    : map_(map), types_(types), weapons_(weapons), rng_(seed), pathfinder_(map) {
  spatial_.reset(map.widthLeptons(), map.heightLeptons());
  vision_.resize(map.width(), map.height());
  map_.clearBlocks();  // structures re-mark their tiles as they are created
  tileStructure_.assign(static_cast<size_t>(map.width()) * map.height(), UINT32_MAX);
  cover_.assign(static_cast<size_t>(map.width()) * map.height(), 0);
}

int32_t Simulation::maxRange(const UnitTypeDef& def) const {
  int32_t r = 0;
  for (int w : def.weapons) {
    r = std::max(r, weapons_.get(w).range);
    if (weapons_.get(w).vsInfantry >= 0) r = std::max(r, weapons_.get(weapons_.get(w).vsInfantry).range);
  }
  return r;
}

void Simulation::rulesChanged() {
  std::vector<uint32_t> units(reg_.pool<CUnit>().entities().begin(), reg_.pool<CUnit>().entities().end());
  for (uint32_t i : units) {
    const Entity e = reg_.handle(i);
    if (!reg_.alive(e)) continue;
    CUnit& u = reg_.get<CUnit>(e);
    if (u.type >= types_.size()) continue;
    const UnitTypeDef& def = types_.get(u.type);
    u.health = std::min(u.health, def.maxHealth);
    const bool armed = !def.weapons.empty();
    if (armed && !reg_.has<CCombat>(e)) reg_.add<CCombat>(e, {});
    else if (!armed && reg_.has<CCombat>(e)) reg_.remove<CCombat>(e);
    if (def.turret && !reg_.has<CTurret>(e)) reg_.add<CTurret>(e, {reg_.get<CFacing>(e).angle, 0});
    else if (!def.turret && reg_.has<CTurret>(e)) reg_.remove<CTurret>(e);
    if (def.isStructure()) {
      const bool makes = def.role == "barracks" || def.role == "factory" || def.role == "post" || def.role == "warehouse";
      if (makes && !reg_.has<CProducer>(e)) reg_.add<CProducer>(e, {});
      else if (!makes && reg_.has<CProducer>(e)) reg_.remove<CProducer>(e);
    } else if (def.capacity > 0 && !reg_.has<CHarvester>(e)) {
      reg_.add<CHarvester>(e, {});
    } else if (def.capacity == 0 && reg_.has<CHarvester>(e)) {
      reg_.remove<CHarvester>(e);
      if (reg_.get<COrder>(e).type == OrderType::Harvest) reg_.get<COrder>(e).type = OrderType::Idle;
    }
  }
  coverDirty_ = true;
}

Entity Simulation::spawnUnit(uint16_t type, uint8_t player, Vec2i pos, Angle facing) {
  const UnitTypeDef& def = types_.get(type);
  if (def.isStructure()) {
    // Structures go exactly where asked, on the grid, or not at all.
    const Vec2i o = footprintOrigin(type, pos);
    // Villages from the map stand where the map put them: their neighbours'
    // levelled plots may have steepened the ground between them.
    if (!canPlace(type, o, def.faction == "neutral")) return kNullEntity;
    const Entity e = reg_.create();
    const Vec2i c{o.x * 256 + def.footW * 128, o.y * 256 + def.footH * 128};
    const Angle a = angleOfFacing(facingOf(facing));
    reg_.add<CPosition>(e, {c.x, c.y});
    reg_.add<CPrevPosition>(e, {c.x, c.y});
    reg_.add<CFacing>(e, {a, 0});
    reg_.add<CUnit>(e, {type, player, def.maxHealth});
    reg_.add<CMover>(e, {});
    reg_.add<COrder>(e, {});
    reg_.add<CAnim>(e, {});
    reg_.add<CStructure>(e, {o.x, o.y, def.footW, def.footH, tick_});
    if (def.role == "barracks" || def.role == "factory" || def.role == "post" || def.role == "warehouse") reg_.add<CProducer>(e, {});
    if (!def.weapons.empty()) reg_.add<CCombat>(e, {});
    if (def.turret) reg_.add<CTurret>(e, {angleOfFacing(4), 0});
    occupy(e.index, def, o);
    levelPlot(e.index, def, o);
    SimEvent ev;
    ev.type = EventType::StructurePlaced;
    ev.owner = player;
    ev.unitType = type;
    ev.at = c;
    events_.push_back(ev);
    // A logistics office comes with its supply trucks, which set off one after another.
    // (Trucks of its side that have lost their office take up the run first.)
    int spare = 0;
    if (def.freeUnit >= 0) {
      auto& trucks = reg_.pool<CHarvester>();
      for (uint32_t t : trucks.entities())
        if (reg_.pool<CUnit>().get(t).owner == player && !reg_.alive(trucks.get(t).office)) ++spare;
    }
    if (def.freeUnit >= 0)
      for (int i = spare; i < std::max(1, def.convoy); ++i) {
        const Entity t = spawnUnit(static_cast<uint16_t>(def.freeUnit), player, exitOf(e), angleOfFacing(4));
        if (!t.valid() || !reg_.has<CHarvester>(t)) continue;
        CHarvester& h = reg_.get<CHarvester>(t);
        h.office = e;
        h.state = HarvestState::Loading;
        h.timer = static_cast<uint16_t>(std::max(0, 9 - i * 2) * TICKS_PER_SECOND);  // the first is nearly loaded
      }
    return e;
  }
  pos.x = std::clamp(pos.x, def.radius, map_.widthLeptons() - 1 - def.radius);
  pos.y = std::clamp(pos.y, def.radius, map_.heightLeptons() - 1 - def.radius);
  // Never place a unit in water or on a cliff: move it to the nearest ground it can stand on.
  const int tx = pos.x >> LEPTON_SHIFT, ty = pos.y >> LEPTON_SHIFT;
  if (!map_.passable(tx, ty, def.locomotor)) {
    bool found = false;
    for (int r = 1; r <= 8 && !found; ++r)
      for (int dy = -r; dy <= r && !found; ++dy)
        for (int dx = -r; dx <= r && !found; ++dx) {
          if (std::max(std::abs(dx), std::abs(dy)) != r || !map_.passable(tx + dx, ty + dy, def.locomotor)) continue;
          pos = {(tx + dx) * LEPTONS_PER_TILE + (pos.x & 255), (ty + dy) * LEPTONS_PER_TILE + (pos.y & 255)};
          found = true;
        }
    if (!found) return kNullEntity;
  }
  const Entity e = reg_.create();
  const Angle a = angleOfFacing(facingOf(facing));
  reg_.add<CPosition>(e, {pos.x, pos.y});
  reg_.add<CPrevPosition>(e, {pos.x, pos.y});
  reg_.add<CFacing>(e, {a, 0});
  reg_.add<CUnit>(e, {type, player, def.maxHealth});
  reg_.add<CMover>(e, {});
  reg_.add<COrder>(e, {});
  reg_.add<CAnim>(e, {});
  if (!def.weapons.empty()) reg_.add<CCombat>(e, {});
  if (def.turret) reg_.add<CTurret>(e, {a, 0});
  if (def.capacity > 0) {  // supply trucks go to work at once: to an office of their side that is short of one
    reg_.add<CHarvester>(e, {});
    reg_.get<COrder>(e).type = OrderType::Harvest;
  }
  return e;
}

void Simulation::tick() {
  events_.clear();
  spawnLog_.clear();
  pathfinder_.refresh();

  if (ai_.any()) ai_.update(*this);  // the computer players give their orders like anyone else
  std::vector<Command> cmds;
  cmds.swap(pending_);
  for (const Command& c : cmds) applyCommand(c);

  // Timing is for the profiler only; it never feeds back into the game.
  using Clock = std::chrono::steady_clock;
  auto t = Clock::now();
  auto lap = [&t](float& out) {
    const auto now = Clock::now();
    out = std::chrono::duration<float, std::milli>(now - t).count();
    t = now;
  };
  processPathQueue();
  lap(stats_.msPaths);
  stepOrders();
  stepFormations();
  stepGarrisons();
  lap(stats_.msOrders);
  stepEconomy();
  stepScript();
  stepMission();
  lap(stats_.msEconomy);
  stepMovement();
  lap(stats_.msMove);
  rebuildSpatial();
  lap(stats_.msSpatial);
  stepSeparation();
  lap(stats_.msSeparation);
  if (tick_ % 4 == 0) stepVision();
  lap(stats_.msVision);
  stepTargeting();
  stepScouts();
  lap(stats_.msTargeting);
  stepWeapons();
  lap(stats_.msWeapons);
  stepProjectiles();
  lap(stats_.msProjectiles);
  stepGroups();
  removeDead();
  stats_.projectiles = static_cast<uint32_t>(reg_.pool<CProjectile>().size());
  ++tick_;
}

void Simulation::setOrderIdle(Entity e) {
  if (auto* o = reg_.tryGet<COrder>(e)) *o = COrder{};
  if (auto* m = reg_.tryGet<CMover>(e)) {
    m->active = false;
    m->request = 0;
    m->path.clear();
  }
  if (auto* c = reg_.tryGet<CCombat>(e)) c->target = kNullEntity;
}

void Simulation::applyCommand(const Command& cmd) {
  // What humans do, for tutorials waiting on an order (replays count the same).
  if (cmd.player < MAX_PLAYERS && !ai_.player(cmd.player).enabled && !scriptCommand_ && static_cast<size_t>(cmd.type) < 32)
    ++ordersGiven_[cmd.player][static_cast<size_t>(cmd.type)];
  // Set pieces: units a script holds back are left out of their side's orders
  // (locked from the player, or kept from the computer); a side confined to a
  // play area sends nobody out of it.
  const bool unitOrder = cmd.type == CommandType::Move || cmd.type == CommandType::AttackMove ||
                         cmd.type == CommandType::Attack || cmd.type == CommandType::Stop ||
                         cmd.type == CommandType::SetStance || cmd.type == CommandType::Deploy ||
                         cmd.type == CommandType::Harvest || cmd.type == CommandType::Repair ||
                         cmd.type == CommandType::BindGroup || cmd.type == CommandType::UnbindGroup ||
                         cmd.type == CommandType::Occupy || cmd.type == CommandType::Unload ||
                         cmd.type == CommandType::Sell;
  const bool human = cmd.player < MAX_PLAYERS && !ai_.player(cmd.player).enabled && !scriptCommand_;
  const bool confine = human && playAreaLimited(cmd.player) &&
                       (cmd.type == CommandType::Move || cmd.type == CommandType::AttackMove);
  if (!scriptCommand_ && unitOrder && !reg_.pool<CScripted>().entities().empty()) {
    bool any = false;
    for (Entity e : cmd.units) any = any || !commandable(cmd, e);
    if (any) {
      Command c = cmd;
      std::erase_if(c.units, [&](Entity e) { return !commandable(cmd, e); });
      if (c.units.empty()) return;
      if (confine) c.target = clampToPlayArea(cmd.player, c.target);
      applyCommand(c);
      return;
    }
  }
  if (confine) {
    const Vec2i t = clampToPlayArea(cmd.player, cmd.target);
    if (!(t == cmd.target)) {
      Command c = cmd;
      c.target = t;
      applyCommand(c);
      return;
    }
  }
  // Men inside a building come out to go anywhere; men in a trench stay
  // dug in until told to evacuate.
  std::vector<Entity> movers;
  const std::vector<Entity>* units = &cmd.units;
  if (cmd.type == CommandType::Move || cmd.type == CommandType::AttackMove || cmd.type == CommandType::Repair) {
    for (Entity e : cmd.units) {
      if (inTrench(e)) continue;
      if (reg_.alive(e) && reg_.has<CGarrisoned>(e)) leaveBuilding(e);
      movers.push_back(e);
    }
    if (movers.size() != cmd.units.size()) units = &movers;
  }
  if (units != &cmd.units) {
    Command c = cmd;
    c.units = movers;
    if (c.units.empty()) return;
    applyCommandBody(c);
    return;
  }
  applyCommandBody(cmd);
}

void Simulation::applyCommandBody(const Command& cmd) {
  switch (cmd.type) {
    case CommandType::Move:
      orderMove(cmd.units, cmd.target, OrderType::Move, cmd);
      break;
    case CommandType::AttackMove:
      orderMove(cmd.units, cmd.target, OrderType::AttackMove, cmd);
      break;
    case CommandType::Attack: {
      if (!reg_.alive(cmd.targetUnit) || !reg_.has<CUnit>(cmd.targetUnit)) break;
      for (Entity e : cmd.units) {
        if (!reg_.alive(e) || !reg_.has<CCombat>(e) || e == cmd.targetUnit) continue;
        COrder& o = reg_.get<COrder>(e);
        o = COrder{};
        o.type = OrderType::Attack;
        o.target = cmd.targetUnit;
        reg_.get<CCombat>(e).target = cmd.targetUnit;
        const CPosition& tp = reg_.get<CPosition>(cmd.targetUnit);
        o.chasedAt = {tp.x, tp.y};
        requestPath(e, o.chasedAt);
      }
      break;
    }
    case CommandType::Stop:
      for (Entity e : cmd.units)
        if (reg_.alive(e)) setOrderIdle(e);
      break;
    case CommandType::Spawn: {
      if (cmd.unitType >= types_.size()) break;
      const int32_t spacing = std::max(128, types_.get(cmd.unitType).radius * 2);
      int32_t side = 1;
      while (side * side < cmd.count) ++side;
      for (int32_t i = 0; i < cmd.count; ++i) {
        const int32_t gx = i % side, gy = i / side;
        const Vec2i p{cmd.target.x + (gx * 2 - (side - 1)) * spacing / 2,
                      cmd.target.y + (gy * 2 - (side - 1)) * spacing / 2};
        spawnUnit(cmd.unitType, cmd.player, p, cmd.facing);
      }
      break;
    }
    case CommandType::Remove:
      for (Entity e : cmd.units)
        if (reg_.alive(e) && reg_.has<CUnit>(e)) destroyEntity(e.index);
      break;
    case CommandType::SpawnList:
      for (size_t i = 0; i < cmd.spawns.size(); ++i) {
        const SpawnSpec& s = cmd.spawns[i];
        if (s.type >= types_.size()) continue;
        const Entity e = spawnUnit(s.type, s.owner, s.pos, s.facing);
        if (!e.valid()) continue;
        if (s.health > 0) reg_.get<CUnit>(e).health = std::min(s.health, types_.get(s.type).maxHealth);
        spawnLog_.push_back({cmd.tag, cmd.indexBase + static_cast<uint32_t>(i), e});
      }
      break;
    case CommandType::Edit:
      for (const UnitEdit& ed : cmd.edits) applyEdit(ed);
      break;
    case CommandType::Kill:
      for (Entity e : cmd.units)
        if (reg_.alive(e) && reg_.has<CUnit>(e)) damage(e.index, INT32_MAX / 2);
      break;
    case CommandType::Strike: {
      if (cmd.weapon >= weapons_.size()) break;
      const int32_t r = std::max<int32_t>(cmd.radius, 0);
      for (int i = 0; i < cmd.count; ++i) {
        Vec2i at = cmd.target;
        if (r > 0) {  // spread evenly over the disc, not bunched at the centre
          do {
            at = {cmd.target.x + rng_.range(-r, r), cmd.target.y + rng_.range(-r, r)};
          } while (distance(at.x - cmd.target.x, at.y - cmd.target.y) > static_cast<uint32_t>(r));
        }
        at.x = std::clamp(at.x, 0, map_.widthLeptons() - 1);
        at.y = std::clamp(at.y, 0, map_.heightLeptons() - 1);
        const Entity pe = reg_.create();
        CProjectile pr;
        pr.from = pr.to = at;  // falls from the sky: no direction, no rear-armour bonus
        pr.x = at.x;
        pr.y = at.y;
        pr.total = 1;
        pr.weapon = cmd.weapon;
        pr.owner = NEUTRAL_OWNER;
        pr.delay = static_cast<uint16_t>(rng_.below(static_cast<uint32_t>(cmd.count) * 3 + 1));
        reg_.add<CProjectile>(pe, pr);
      }
      break;
    }
    case CommandType::SetStance:
      for (Entity e : cmd.units)
        if (reg_.alive(e) && reg_.has<CUnit>(e)) reg_.get<CUnit>(e).stance = cmd.stance;
      break;
    case CommandType::Produce:
    case CommandType::PlaceBuilding:
    case CommandType::PlaceWalls:
    case CommandType::Sell:
    case CommandType::Deploy:
    case CommandType::Harvest:
    case CommandType::SetRally:
    case CommandType::SetCredits:
    case CommandType::Repair:
      applyEconomyCommand(cmd);
      break;
    case CommandType::BindGroup:
      if (cmd.player < MAX_PLAYERS) bindUnits(cmd.units, cmd.player);
      break;
    case CommandType::UnbindGroup: {
      std::vector<Entity> groups;
      for (Entity e : cmd.units)
        if (const CGroupMember* m = reg_.tryGet<CGroupMember>(e))
          if (std::find(groups.begin(), groups.end(), m->group) == groups.end()) groups.push_back(m->group);
      for (Entity g : groups) disband(g);
      break;
    }
    case CommandType::Occupy:
      applyOccupy(cmd);
      break;
    case CommandType::Unload:
      applyUnload(cmd);
      break;
    case CommandType::SetController:
      if (cmd.player < MAX_PLAYERS)
        ai_.setPlayer(cmd.player, cmd.amount >= 0, static_cast<AiDifficulty>(std::clamp(cmd.amount, 0, 2)));
      break;
    case CommandType::SetTeam:
      if (cmd.player < MAX_PLAYERS) team_[cmd.player] = static_cast<uint8_t>(cmd.count);
      break;
    case CommandType::TagUnits: {
      const int32_t name = cmd.text.empty() ? -1 : nameIndex(cmd.text == "-" ? std::string() : cmd.text);
      for (Entity e : cmd.units) {
        if (!reg_.alive(e) || !reg_.has<CUnit>(e)) continue;
        if (name >= 0) setUnitName(e, name);
        setScriptFlags(e, static_cast<uint8_t>(cmd.count), static_cast<uint8_t>(cmd.amount));
      }
      break;
    }
  }
}

void Simulation::applyEdit(const UnitEdit& ed) {
  if (!reg_.alive(ed.unit) || !reg_.has<CUnit>(ed.unit)) return;
  CUnit& u = reg_.get<CUnit>(ed.unit);
  const UnitTypeDef& def = types_.get(u.type);
  if (ed.move && def.speed > 0) {
    const Vec2i p{std::clamp(ed.pos.x, def.radius, map_.widthLeptons() - 1 - def.radius),
                  std::clamp(ed.pos.y, def.radius, map_.heightLeptons() - 1 - def.radius)};
    if (map_.passable(p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT, def.locomotor)) {
      reg_.get<CPosition>(ed.unit) = {p.x, p.y};
      reg_.get<CPrevPosition>(ed.unit) = {p.x, p.y};  // a jump, not a slide
      setOrderIdle(ed.unit);
    }
  }
  if (ed.facing >= 0) {
    const Angle a = angleOfFacing(facingOf(static_cast<Angle>(ed.facing)));
    reg_.get<CFacing>(ed.unit).angle = a;
    if (CTurret* t = reg_.tryGet<CTurret>(ed.unit)) t->angle = a;
  }
  if (ed.owner >= 0 && ed.owner < MAX_PLAYERS && ed.owner != u.owner) {
    u.owner = static_cast<uint8_t>(ed.owner);
    setOrderIdle(ed.unit);  // a defector forgets its old orders and targets
    if (CCombat* c = reg_.tryGet<CCombat>(ed.unit)) c->target = kNullEntity;
  }
  if (ed.health >= 0) u.health = std::clamp(ed.health, 1, def.maxHealth);
}

uint64_t Simulation::stateHash() {
  uint64_t h = 1469598103934665603ULL;  // FNV-1a
  auto mix = [&h](uint64_t v) {
    for (int i = 0; i < 8; ++i) {
      h ^= (v >> (i * 8)) & 0xff;
      h *= 1099511628211ULL;
    }
  };
  mix(tick_);
  mix(rng_.state());
  for (uint8_t t : team_) mix(t);
  reg_.each<CUnit, CPosition, CFacing>([&](uint32_t e, CUnit& u, CPosition& p, CFacing& f) {
    mix(e);
    mix(u.type | (uint64_t{u.owner} << 16) | (uint64_t(uint32_t(u.health)) << 32) | (uint64_t(u.stance) << 24));
    mix(uint64_t(uint32_t(p.x)) | (uint64_t(uint32_t(p.y)) << 32));
    mix(f.angle);
    if (const CTurret* t = reg_.pool<CTurret>().tryGet(e)) mix(t->angle);
  });
  reg_.each<CProjectile>([&](uint32_t e, CProjectile& pr) {
    mix(e);
    mix(uint64_t(uint32_t(pr.x)) | (uint64_t(uint32_t(pr.y)) << 32));
  });
  reg_.each<CScripted>([&](uint32_t e, CScripted& s) { mix(e ^ (uint64_t{s.name} << 32) ^ (uint64_t{s.flags} << 48)); });
  for (const MissionCountdown& d : countdowns_) mix(d.endTick ^ (uint64_t{d.done} << 40));
  for (const auto& [k, v] : counters_) mix(static_cast<uint32_t>(v) ^ (k.size() << 20));
  return h;
}

}  // namespace slave

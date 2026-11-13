// A mission loaded the way the game loads it (units, names and set-piece
// flags, sides, credits, the script), for tests that play missions.
#pragma once
#include <doctest/doctest.h>

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "pioneer/map/map_io.hpp"
#include "pioneer/sim/script.hpp"
#include "pioneer/sim/simulation.hpp"

namespace testkit {
using namespace pioneer;
inline const std::string kContent = LEGION_CONTENT_DIR;

struct Rules {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  Rules() {
    std::string err;
    const std::string dir = kContent + "rules/";
    REQUIRE(weapons.loadFile(dir + "weapons.json", err));
    REQUIRE(types.loadFile(dir + "units.json", err));
    REQUIRE(types.loadFile(dir + "buildings.json", err));
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
  }
};

// A mission set up the way the game starts one (GameScene::replaceWorld).
struct Battle {
  const Rules& R;
  MapFile f;
  std::unique_ptr<Simulation> sim;
  ScenarioScript script;
  explicit Battle(const Rules& r) : R(r) {}
  bool load(const std::string& path, std::map<std::string, int32_t> flags = {}) {
    std::string err;
    if (!loadMap(path, f, err)) {
      MESSAGE(path << ": " << err);
      return false;
    }
    sim = std::make_unique<Simulation>(f.map, R.types, R.weapons, 7);
    sim->setFlags(std::move(flags));
    for (uint8_t i = 0; i < MAX_PLAYERS && i < f.players.size(); ++i) {
      Command c;
      c.type = CommandType::SetTeam;
      c.player = i;
      c.count = f.players[i].team;
      sim->queue(c);
    }
    Command spawns;
    spawns.type = CommandType::SpawnList;
    for (const UnitPlacement& u : f.units) {
      const int t = R.types.find(u.type);
      if (t < 0) {
        MESSAGE("unknown unit " << u.type);
        return false;
      }
      spawns.spawns.push_back({static_cast<uint16_t>(t), u.owner, {u.x, u.y}, u.facing, u.health});
    }
    spawns.tag = 0x504C4344;
    sim->queue(spawns);
    // Names, set-piece flags and stances of placed units, in the same tick (as the game does).
    for (size_t i = 0; i < f.units.size(); ++i) {
      const UnitPlacement& u = f.units[i];
      if (u.name.empty() && !u.flags && !u.stance) continue;
      Command c;
      c.type = CommandType::TagUnits;
      c.player = NEUTRAL_OWNER;
      c.tag = 0x504C4344;
      c.indexBase = static_cast<uint32_t>(i);
      c.text = u.name;
      c.count = 0xFF;
      c.amount = u.flags;
      c.radius = u.stance ? u.stance + 1 : 0;
      sim->queue(c);
    }
    for (uint8_t i = 0; i < f.players.size() && i < MAX_PLAYERS; ++i) {
      if (f.players[i].active && !f.players[i].human) {
        Command c;
        c.type = CommandType::SetController;
        c.player = i;
        c.amount = f.players[i].difficulty;
        sim->queue(c);
      }
      Command c;
      c.type = CommandType::SetCredits;
      c.player = i;
      c.amount = f.players[i].credits;
      sim->queue(c);
    }
    script = scriptFromJson(nlohmann::json::parse(f.scriptJson), &R.types);
    sim->setScript(script, false);
    sim->tick();
    return true;
  }
  std::vector<Entity> units(uint8_t owner, const std::function<bool(const UnitTypeDef&, Entity)>& keep = {}) {
    std::vector<Entity> out;
    Registry& reg = sim->registry();
    for (uint32_t e : reg.pool<CUnit>().entities()) {
      const CUnit& u = reg.pool<CUnit>().get(e);
      if (u.owner != owner) continue;
      const UnitTypeDef& d = R.types.get(u.type);
      if (keep && !keep(d, reg.handle(e))) continue;
      out.push_back(reg.handle(e));
    }
    return out;
  }
  std::vector<Entity> ofType(uint8_t owner, const char* id) {
    return units(owner, [&](const UnitTypeDef& d, Entity) { return d.id == id; });
  }
  void order(CommandType t, std::vector<Entity> who, Vec2i tile, Entity target = kNullEntity) {
    Command c;
    c.type = t;
    c.player = 0;
    c.units = std::move(who);
    c.target = {tile.x * 256 + 128, tile.y * 256 + 128};
    c.targetUnit = target;
    sim->queue(c);
  }
  // Runs until `done` or `seconds` of game time pass. Returns whether done.
  bool runUntil(const std::function<bool()>& done, int seconds, const std::function<void()>& each = {}) {
    for (int i = 0; i < seconds * 15; ++i) {
      if (done()) return true;
      if (each && i % 30 == 0) each();
      sim->tick();
    }
    return done();
  }
  int trigger(const std::string& name) const {
    for (size_t i = 0; i < script.triggers.size(); ++i)
      if (script.triggers[i].name == name) return static_cast<int>(i);
    FAIL("no trigger " << name);
    return -1;
  }
  bool fired(const std::string& name) const { return sim->triggerFired(trigger(name)); }
  bool won() const { return sim->playerState(0).won; }
  // Builds a structure for player 0: orders it and places it near (x, y) when ready.
  bool build(const char* id, Vec2i near, int seconds = 120) {
    const int type = R.types.find(id);
    Command p;
    p.type = CommandType::Produce;
    p.player = 0;
    p.unitType = static_cast<uint16_t>(type);
    p.amount = 1;
    sim->queue(p);
    const bool ready = runUntil([&] {
      for (const ProductionQueue& q : sim->playerState(0).queues)
        if (q.ready && !q.items.empty() && q.items.front() == type) return true;
      return false;
    }, seconds);
    if (!ready) return false;
    for (int r = 0; r < 14; ++r)
      for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
          if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
          const Vec2i at{(near.x + dx) * 256, (near.y + dy) * 256};
          if (!sim->canPlace(type, sim->footprintOrigin(type, at))) continue;
          const size_t before = ofType(0, id).size();
          Command c;
          c.type = CommandType::PlaceBuilding;
          c.player = 0;
          c.unitType = static_cast<uint16_t>(type);
          c.target = at;
          sim->queue(c);
          sim->tick();
          sim->tick();
          if (ofType(0, id).size() > before) return true;
        }
    return false;
  }
};

}  // namespace testkit

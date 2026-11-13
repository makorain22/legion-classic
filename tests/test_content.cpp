// Milestone 7 content: the full unit roster, salvo fire, engineers, biomes and
// civilian buildings.
#include <doctest/doctest.h>

#include <map>
#include <set>

#include <nlohmann/json.hpp>

#include "slave/map/map_io.hpp"
#include "slave/sim/script.hpp"
#include "slave/sim/simulation.hpp"

using namespace slave;

namespace {
struct Rules {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  Rules() {
    std::string err;
    const std::string dir = LEGION_RULES_DIR;
    REQUIRE(weapons.loadFile(dir + "weapons.json", err));
    REQUIRE(types.loadFile(dir + "units.json", err));
    REQUIRE(types.loadFile(dir + "buildings.json", err));
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
  }
  uint16_t t(const char* id) const {
    const int i = types.find(id);
    REQUIRE(i >= 0);
    return static_cast<uint16_t>(i);
  }
};

Entity spawn(Simulation& sim, uint16_t type, uint8_t owner, Vec2i at, Angle facing = 0) {
  Command c;
  c.type = CommandType::SpawnList;
  c.spawns = {{type, owner, at, facing, -1}};
  sim.queue(c);
  sim.tick();
  for (const auto& r : sim.spawnLog()) return r.entity;
  return kNullEntity;
}

constexpr int32_t T = LEPTONS_PER_TILE;
}  // namespace

TEST_CASE("every army fields the full roster") {
  Rules R;
  for (const char* f : {"ger", "rus", "gbr"}) {
    std::set<std::string> have;
    for (size_t i = 0; i < R.types.size(); ++i) {
      const UnitTypeDef& d = R.types.get(static_cast<int>(i));
      if (d.faction == f && !d.isStructure()) have.insert(d.id.substr(4));
    }
    for (const char* u : {"rifleman", "mg_team", "at_team", "engineer", "sniper", "scout_car", "halftrack", "light_tank",
                          "medium_tank", "heavy_tank", "tank_destroyer", "artillery", "mcv"})
      CHECK_MESSAGE(have.count(u) == 1, f << "_" << u);
  }
  // Heavy tanks out-gun medium ones; tank destroyers out-range them; snipers out-range rifles.
  auto range = [&](const char* id) {
    int32_t r = 0;
    for (int w : R.types.get(R.t(id)).weapons) r = std::max(r, R.weapons.get(w).range);
    return r;
  };
  CHECK(range("ger_heavy_tank") > range("ger_medium_tank"));
  CHECK(range("rus_tank_destroyer") > range("rus_medium_tank"));
  CHECK(range("ger_sniper") > range("ger_rifleman"));
  CHECK(range("rus_artillery") > range("rus_heavy_tank"));
  CHECK(R.types.get(R.t("ger_engineer")).repair > 0);
  CHECK(R.types.get(R.t("ger_engineer")).weapons.empty());
}

TEST_CASE("the British: the same base, their own guns") {
  Rules R;
  // Every structure the other armies build, the British build too, with the same role and footprint.
  for (size_t i = 0; i < R.types.size(); ++i) {
    const UnitTypeDef& d = R.types.get(static_cast<int>(i));
    if (d.faction != "ger" || d.id == "ger_flak_tower") continue;  // a mission landmark, the Reich's alone
    const std::string mine = d.id == "ger_volkssturm" ? "gbr_recruit" : "gbr_" + d.id.substr(4);  // each army's levy has its own name
    const int t = R.types.find(mine);
    CHECK_MESSAGE(t >= 0, mine);
    if (t < 0) continue;
    const UnitTypeDef& g = R.types.get(t);
    CHECK(g.faction == "gbr");
    CHECK(g.model == d.model);  // art falls back to these models until British ones exist
    CHECK(g.role == d.role);
    CHECK(g.isStructure() == d.isStructure());
    for (const std::string& req : g.requiresIds) CHECK_MESSAGE(req.rfind("gbr_", 0) == 0, mine);  // no German prerequisites
  }
  CHECK(R.types.get(R.t("gbr_mcv")).deploysTo == R.t("gbr_yard"));
  auto best = [&](const char* id, bool ap) {
    int32_t d = 0;
    for (int w : R.types.get(R.t(id)).weapons)
      if ((R.weapons.warhead(R.weapons.get(w).warhead).id.rfind("ap", 0) == 0) == ap) d = std::max(d, R.weapons.get(w).damage);
    return d;
  };
  // The Firefly and the Achilles carry the 17-pounder: the hardest British punch against armour.
  CHECK(best("gbr_firefly", true) > best("gbr_medium_tank", true));
  CHECK(best("gbr_tank_destroyer", true) > best("gbr_heavy_tank", true));
  // The Churchill is the toughest thing on tracks; the 25-pounder fires faster than the German 105.
  CHECK(R.types.get(R.t("gbr_heavy_tank")).maxHealth > R.types.get(R.t("ger_heavy_tank")).maxHealth);
  auto cooldown = [&](const char* id) { return R.weapons.get(R.types.get(R.t(id)).weapons.front()).cooldown; };
  CHECK(cooldown("gbr_artillery") < cooldown("ger_artillery"));
  CHECK(cooldown("gbr_rifleman") < cooldown("ger_rifleman"));  // the Lee-Enfield's rapid fire
}

TEST_CASE("a rocket truck fires its whole salvo, a rocket at a time") {
  Rules R;
  Map map(64, 64, 300);
  Simulation sim(map, R.types, R.weapons, 3);
  const Entity launcher = spawn(sim, R.t("rus_artillery"), 1, {16 * T, 32 * T});
  const Entity target = spawn(sim, R.t("ger_medium_tank"), 0, {30 * T, 32 * T});
  REQUIRE(launcher.valid());
  REQUIRE(target.valid());
  Command atk;
  atk.type = CommandType::Attack;
  atk.player = 1;
  atk.units = {launcher};
  atk.targetUnit = target;
  sim.queue(atk);
  size_t most = 0;
  std::set<uint16_t> delays;
  for (int i = 0; i < 15 * 8; ++i) {
    sim.tick();
    auto& pool = sim.registry().pool<CProjectile>();
    most = std::max(most, pool.size());
    for (uint32_t e : pool.entities()) delays.insert(pool.get(e).delay);
  }
  CHECK(most >= 8);           // eight rockets in the air together
  CHECK(delays.size() > 3);   // launched in a ripple, not all at once
}

TEST_CASE("engineers mend damaged works and machines, and go looking for them") {
  Rules R;
  Map map(64, 64, 300);
  Simulation sim(map, R.types, R.weapons, 5);
  const Entity power = spawn(sim, R.t("ger_power"), 0, {20 * T, 20 * T});
  const Entity tank = spawn(sim, R.t("ger_medium_tank"), 0, {34 * T, 34 * T});
  const Entity eng = spawn(sim, R.t("ger_engineer"), 0, {24 * T, 24 * T});
  const Entity enemyTank = spawn(sim, R.t("rus_medium_tank"), 1, {60 * T, 60 * T});
  REQUIRE(power.valid());
  REQUIRE(tank.valid());
  REQUIRE(eng.valid());
  auto& units = sim.registry().pool<CUnit>();
  const int32_t fullPower = units.get(power.index).health;
  const int32_t fullTank = units.get(tank.index).health;
  units.get(power.index).health = fullPower / 4;
  units.get(tank.index).health = fullTank / 2;
  // Can't mend the enemy, people, or what's whole.
  CHECK_FALSE(sim.canRepair(eng, enemyTank));
  CHECK_FALSE(sim.canRepair(eng, eng));
  CHECK(sim.canRepair(eng, power));

  // Left alone, it finds the damaged power station nearby and fixes it.
  for (int i = 0; i < 15 * 120 && units.get(power.index).health < fullPower; ++i) sim.tick();
  CHECK(units.get(power.index).health == fullPower);

  // Ordered to the tank (out of its sight), it walks over and mends it too.
  Command c;
  c.type = CommandType::Repair;
  c.player = 0;
  c.units = {eng};
  c.targetUnit = tank;
  sim.queue(c);
  sim.tick();
  CHECK(sim.registry().get<COrder>(eng).type == OrderType::Repair);
  for (int i = 0; i < 15 * 120 && units.get(tank.index).health < fullTank; ++i) sim.tick();
  CHECK(units.get(tank.index).health == fullTank);
  for (int i = 0; i < 5; ++i) sim.tick();
  CHECK(sim.registry().get<COrder>(eng).type == OrderType::Idle);
}

TEST_CASE("each biome has its own ground, trees and settlements") {
  Rules R;
  std::map<Biome, Map> maps;
  for (int b = 0; b < static_cast<int>(Biome::Count); ++b) {
    Map m(128, 128);
    m.generate(static_cast<Biome>(b), 1944);
    CHECK(m.biome == static_cast<Biome>(b));
    maps.emplace(static_cast<Biome>(b), std::move(m));
  }
  auto propCount = [&](Biome b, PropKind k) {
    int n = 0;
    for (const Prop& p : maps.at(b).props()) n += p.kind == k;
    return n;
  };
  auto layerShare = [&](Biome b, Layer l) {
    const Map& m = maps.at(b);
    int n = 0, total = 0;
    for (int sy = 0; sy < m.splatH(); sy += 3)
      for (int sx = 0; sx < m.splatW(); sx += 3) {
        n += m.dominantLayer(sx, sy) == l;
        ++total;
      }
    return static_cast<float>(n) / total;
  };
  CHECK(layerShare(Biome::Winter, Layer::Snow) > 0.5f);
  CHECK(layerShare(Biome::Desert, Layer::Sand) > 0.4f);
  CHECK(layerShare(Biome::Temperate, Layer::Grass) > 0.4f);
  CHECK(propCount(Biome::Winter, PropKind::SnowPine) > 50);
  CHECK(propCount(Biome::Winter, PropKind::Tree) == 0);
  CHECK(propCount(Biome::Desert, PropKind::Palm) + propCount(Biome::Desert, PropKind::Scrub) > 20);
  CHECK(propCount(Biome::Tropical, PropKind::JungleTree) > 100);
  // The jungle has stilt houses along its tracks (and timber bridges), nothing else.
  for (const Map::Lot& l : maps.at(Biome::Tropical).lots) CHECK((l.kind == 29 || l.kind == Map::kWoodBridgeLot));
  // (Since 9.8 a village has as many buildings as a few city blocks, in a fraction of the room.)
  CHECK(maps.at(Biome::Urban).lots.size() > 150);
  CHECK(!maps.at(Biome::Temperate).lots.empty());
  // Mud-brick houses alone and in rows, market stalls, courtyard villas, wells (and bridges).
  for (const Map::Lot& l : maps.at(Biome::Desert).lots)
    CHECK((l.kind == 5 || l.kind == 10 || l.kind == 27 || l.kind == 28 || l.kind == Map::kRowLot + 3 || l.kind == Map::kRowLot + 4 ||
           l.kind == Map::kBridgeLot));
  // Every lot names a building the rules have.
  for (const auto& [b, m] : maps)
    for (const Map::Lot& l : m.lots) CHECK(R.types.find(Map::lotBuildingId(l.kind)) >= 0);

  // Same seed, same land.
  Map again(128, 128);
  again.generate(Biome::Urban, 1944);
  CHECK(again.heights() == maps.at(Biome::Urban).heights());
  CHECK(again.lots.size() == maps.at(Biome::Urban).lots.size());
}

TEST_CASE("roads over rivers get bridges; anything crosses on one, and goes in when it falls") {
  Rules R;
  // A river valley: roads ford the river, and the fords get bridges.
  Map gen(128, 128);
  gen.generate(Biome::Temperate, 1944, static_cast<int>(MapLayout::RiverValley));
  int pieces = 0;
  for (const Map::Lot& l : gen.lots) pieces += l.kind == Map::kBridgeLot;
  CHECK(pieces >= 3);

  // A pool of deep water with a bridge across it.
  Map m(32, 32, 310);
  // Deep water across columns 12..14 (the ground lowered under it).
  m.setWaterLevel(200);
  for (int cy = 0; cy <= 32; ++cy)
    for (int cx = 0; cx <= 32; ++cx) m.setCorner(cx, cy, static_cast<uint16_t>(cx >= 12 && cx <= 15 ? 100 : 230));
  m.recomputeInfo();
  Simulation sim(m, R.types, R.weapons, 11);
  const int bridge = R.t("neutral_bridge");
  CHECK(R.types.get(bridge).bridge);
  CHECK_FALSE(m.passable(13, 16, Locomotor::Tracked));
  Command c;
  c.type = CommandType::SpawnList;
  for (int x = 11; x <= 16; ++x) c.spawns.push_back({static_cast<uint16_t>(bridge), NEUTRAL_OWNER, {x * T + 128, 16 * T + 128}, 0, -1});
  sim.queue(c);
  sim.tick();
  CHECK(sim.structureAt(13, 16).valid());
  CHECK(m.passable(13, 16, Locomotor::Tracked));
  CHECK(m.passable(13, 16, Locomotor::Foot));
  CHECK_FALSE(m.passable(13, 15, Locomotor::Foot));  // beside the bridge it is still river
  const Entity tank = spawn(sim, R.t("ger_medium_tank"), 0, {8 * T + 128, 16 * T + 128});
  Command mv;
  mv.type = CommandType::Move;
  mv.player = 0;
  mv.units = {tank};
  mv.target = {13 * T + 128, 16 * T + 128};
  sim.queue(mv);
  for (int i = 0; i < 15 * 30; ++i) sim.tick();
  const CPosition& tp = sim.registry().get<CPosition>(tank);
  CHECK(tp.x / T == 13);  // out over the water on the deck
  // Blow the span under it.
  Command kill;
  kill.type = CommandType::Kill;
  kill.units = {sim.structureAt(13, 16)};
  sim.queue(kill);
  for (int i = 0; i < 3; ++i) sim.tick();
  CHECK_FALSE(sim.registry().alive(tank));
  CHECK_FALSE(m.passable(13, 16, Locomotor::Tracked));
}

TEST_CASE("civilian buildings stand neutral: ignored until someone is ordered to attack them") {
  Rules R;
  Map map(64, 64, 300);
  Simulation sim(map, R.types, R.weapons, 9);
  const Entity house = spawn(sim, R.t("neutral_cottage"), NEUTRAL_OWNER, {20 * T, 20 * T});
  REQUIRE(house.valid());
  const Entity tank = spawn(sim, R.t("ger_medium_tank"), 0, {24 * T, 20 * T});
  REQUIRE(tank.valid());
  auto& units = sim.registry().pool<CUnit>();
  const int32_t full = units.get(house.index).health;
  for (int i = 0; i < 15 * 10; ++i) sim.tick();
  CHECK(units.get(house.index).health == full);  // left alone
  CHECK(sim.coverAt(18, 20) > 0);                 // infantry beside it take cover
  Command atk;
  atk.type = CommandType::Attack;
  atk.player = 0;
  atk.units = {tank};
  atk.targetUnit = house;
  sim.queue(atk);
  for (int i = 0; i < 15 * 20; ++i) sim.tick();
  const bool hurt = !sim.registry().alive(house) || units.get(house.index).health < full;
  CHECK(hurt);
  CHECK_FALSE(sim.playerState(0).defeated);
}

TEST_CASE("the bundled scenario loads, places its armies and briefs the player") {
  Rules R;
  MapFile f;
  std::string err;
  REQUIRE_MESSAGE(loadMap(std::string(LEGION_RULES_DIR) + "../scenarios/Kalach_Crossing.lmap", f, err), err);
  CHECK(f.map.biome == Biome::Winter);
  CHECK(f.units.size() > 40);
  REQUIRE(!f.scriptJson.empty());
  Simulation sim(f.map, R.types, R.weapons, 1);
  Command c;
  c.type = CommandType::SpawnList;
  for (const UnitPlacement& u : f.units) {
    const int t = R.types.find(u.type);
    REQUIRE_MESSAGE(t >= 0, u.type);
    c.spawns.push_back({static_cast<uint16_t>(t), u.owner, {u.x, u.y}, u.facing, u.health});
  }
  sim.queue(c);
  sim.setScript(scriptFromJson(nlohmann::json::parse(f.scriptJson), &R.types), false);
  for (int i = 0; i < 15; ++i) sim.tick();
  CHECK(sim.unitCount() >= f.units.size());  // every placement fitted (plus free trucks)
  CHECK(sim.objectives().size() == 2);
  CHECK(sim.triggerFired(0));
  CHECK_FALSE(sim.triggerFired(1));
}

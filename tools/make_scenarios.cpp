// Builds the scenarios that ship with Legion Classic (content/scenarios/).
//
// Scenarios are normally made in the game's editor; this tool writes the
// built-in ones from code so they can be rebuilt when the rules change:
//
//   g++ -std=c++20 -O2 -Iengine/include -I<nlohmann json>/include tools/make_scenarios.cpp \
//       build-core/libslave_core.a build-core/_deps/miniz-build/libminiz.a -lpthread -o make_scenarios
//   ./make_scenarios content/
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "slave/map/map_io.hpp"
#include "slave/sim/script.hpp"
#include "slave/sim/simulation.hpp"

using namespace slave;

namespace {
constexpr int32_t T = LEPTONS_PER_TILE;

struct Builder {
  UnitTypeRegistry types;
  WeaponRegistry weapons;
  MapFile file;
  std::unique_ptr<Simulation> sim;

  bool load(const std::string& content) {
    std::string err;
    const std::string dir = content + "rules/";
    if (!weapons.loadFile(dir + "weapons.json", err) || !types.loadFile(dir + "units.json", err) ||
        !types.loadFile(dir + "buildings.json", err) || !types.resolveWeapons(weapons, err) || !types.resolveLinks(err)) {
      std::fprintf(stderr, "rules: %s\n", err.c_str());
      return false;
    }
    return true;
  }
  void start(int size, Biome b, uint64_t seed, const std::string& name, const std::string& description) {
    file = MapFile{};
    file.map.resize(size, size);
    file.map.generate(b, seed);
    file.map.name = name;
    file.map.description = description;
    sim = std::make_unique<Simulation>(file.map, types, weapons, 1);
  }
  int t(const char* id) const {
    const int i = types.find(id);
    if (i < 0) std::fprintf(stderr, "unknown unit %s\n", id);
    return i;
  }
  // Places a unit or structure at the nearest spot that takes it.
  bool place(const char* id, uint8_t owner, int tx, int ty, int facing = 0) {
    const int type = t(id);
    if (type < 0) return false;
    for (int r = 0; r < 14; ++r)
      for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
          if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
          const Vec2i p{(tx + dx) * T + (types.get(type).isStructure() ? 0 : T / 2), (ty + dy) * T + (types.get(type).isStructure() ? 0 : T / 2)};
          const int px = p.x / T, py = p.y / T;
          if (!file.map.inBounds(px, py)) continue;
          if (types.get(type).isStructure()) {
            if (!sim->canPlace(type, sim->footprintOrigin(type, p))) continue;
          } else {
            const TileInfo& ti = file.map.info(px, py);
            if (ti.waterDepth > -6 || ti.slope > 20 || (file.map.block(px, py) & Map::kOccupied)) continue;
          }
          Command c;
          c.type = CommandType::SpawnList;
          c.spawns = {{static_cast<uint16_t>(type), owner, p, angleOfFacing(facing), -1}};
          sim->queue(c);
          sim->tick();
          file.units.push_back({id, owner, p.x, p.y, static_cast<uint16_t>(angleOfFacing(facing)), -1});
          return true;
        }
    std::fprintf(stderr, "no room for %s near %d,%d\n", id, tx, ty);
    return false;
  }
  void squad(const char* id, uint8_t owner, int tx, int ty, int n, int facing) {
    for (int i = 0; i < n; ++i) place(id, owner, tx + (i % 4) * 2 - 3, ty + (i / 4) * 2, facing);
  }
};

Action msg(uint8_t player, const std::string& text) {
  Action a;
  a.type = ActionType::Message;
  a.player = player;
  a.text = text;
  return a;
}
Action objective(uint8_t player, const std::string& text) {
  Action a;
  a.type = ActionType::AddObjective;
  a.player = player;
  a.text = text;
  return a;
}
Action simple(ActionType type, uint8_t player, int32_t value = 0) {
  Action a;
  a.type = type;
  a.player = player;
  a.value = value;
  return a;
}
Action spawn(uint8_t player, int unitType, int area, int count) {
  Action a;
  a.type = ActionType::SpawnUnits;
  a.player = player;
  a.unitType = unitType;
  a.area = area;
  a.value = count;
  return a;
}
Condition cond(CondType type, uint8_t player, int area = -1, int value = 0) {
  Condition c;
  c.type = type;
  c.player = player;
  c.area = area;
  c.value = value;
  return c;
}

// Winter, 1946. A German battle group must seize a crossroads village held by
// Soviet infantry, beat off a counter-attack, then destroy the Soviet base.
bool kalach(Builder& b, const std::string& out) {
  // A seed whose river keeps clear of the battle line across the middle.
  uint64_t seed = 1946;
  for (;; ++seed) {
    Map probe(128, 128);
    probe.generate(Biome::Winter, seed);
    bool dry = true;
    for (int x : {10, 14, 20, 40, 64, 90, 108, 116})
      for (int y = 48; y <= 80; ++y) dry = dry && probe.info(x, y).waterDepth < -16;
    if (dry) break;
  }
  b.start(128, Biome::Winter, seed, "Kalach Crossing",
          "Winter 1946. Kampfgruppe Adler must take the crossroads village, hold it against the counter-attack, "
          "then destroy the Soviet base.");
  b.file.map.lots.clear();  // the village is placed by hand
  PlayerSetup ger, rus;
  ger.name = "Kampfgruppe Adler";
  ger.faction = "ger";
  ger.color = 0xFF2C3AC4;
  ger.team = 0;
  ger.active = true;
  ger.human = true;
  ger.credits = 2500;
  ger.startX = 16 * T;
  ger.startY = 64 * T;
  rus.name = "Red Army Guards Corps";
  rus.faction = "rus";
  rus.color = 0xFFC46034;
  rus.team = 1;
  rus.active = true;
  rus.human = false;
  rus.difficulty = 1;
  rus.credits = 3500;
  rus.startX = 108 * T;
  rus.startY = 64 * T;
  b.file.players.assign(MAX_PLAYERS, PlayerSetup{});
  for (int i = 0; i < MAX_PLAYERS; ++i) {
    b.file.players[static_cast<size_t>(i)].name = "Player " + std::to_string(i + 1);
    b.file.players[static_cast<size_t>(i)].team = static_cast<uint8_t>(i);
  }
  b.file.players[0] = ger;
  b.file.players[1] = rus;

  // The village at the crossroads: houses and a church, Soviet riflemen dug in.
  const int vx = 64, vy = 64;
  b.place("neutral_church", NEUTRAL_OWNER, vx, vy);
  for (auto [dx, dy, id] : {std::tuple{-6, -5, "neutral_cottage"}, {6, -5, "neutral_cottage_bombed"}, {-6, 5, "neutral_ruin"},
                            {6, 6, "neutral_cottage"}, {-1, -9, "neutral_townhouse"}, {0, 9, "neutral_cottage_bombed"}})
    b.place(id, NEUTRAL_OWNER, vx + dx, vy + dy);
  b.place("rus_bunker", 1, vx + 4, vy - 2);
  b.place("rus_bunker", 1, vx + 4, vy + 3);
  for (int i = -3; i <= 3; ++i) b.place("rus_sandbags", 1, vx - 4, vy + i * 2);
  b.squad("rus_rifleman", 1, vx + 1, vy - 3, 8, 8);
  b.squad("rus_mg_team", 1, vx - 2, vy + 3, 2, 8);
  b.squad("rus_at_team", 1, vx + 2, vy + 4, 2, 8);
  b.place("rus_medium_tank", 1, vx + 8, vy, 8);

  // The Soviet base in the east.
  const int bx = 108, by = 64;
  for (auto [dx, dy, id] : {std::tuple{0, 0, "rus_yard"}, {-6, -6, "rus_power"}, {-6, 6, "rus_power"}, {4, -7, "rus_refinery"},
                            {5, 7, "rus_barracks"}, {-1, 12, "rus_factory"}, {-10, -2, "rus_bunker"}, {-10, 3, "rus_atgun"}})
    b.place(id, 1, bx + dx, by + dy);
  b.squad("rus_rifleman", 1, bx - 8, by, 6, 8);

  // Kampfgruppe Adler, in the west.
  const int gx = 14, gy = 64;
  b.place("ger_heavy_tank", 0, gx + 6, gy);
  b.place("ger_medium_tank", 0, gx + 4, gy - 4);
  b.place("ger_medium_tank", 0, gx + 4, gy + 4);
  b.place("ger_halftrack", 0, gx + 1, gy - 7);
  b.place("ger_halftrack", 0, gx + 1, gy + 7);
  b.squad("ger_rifleman", 0, gx, gy - 2, 8, 0);
  b.squad("ger_mg_team", 0, gx - 2, gy + 3, 2, 0);
  b.place("ger_engineer", 0, gx - 3, gy);
  b.place("ger_mcv", 0, gx - 5, gy + 1);

  ScenarioScript s;
  s.areas = {{"Village", vx - 8, vy - 10, vx + 8, vy + 10},
             {"West road", 2, gy - 6, 8, gy + 6},
             {"Soviet base", bx - 14, by - 14, std::min(127, bx + 14), by + 16},
             {"East bank", bx - 18, by - 6, bx - 12, by + 6}};
  Trigger brief;
  brief.name = "Briefing";
  brief.conditions = {cond(CondType::Always, 0)};
  brief.actions = {msg(0, "Kampfgruppe Adler: take the crossroads village before the Soviets reinforce it."),
                   objective(0, "Take the village at the crossroads"),
                   objective(0, "Destroy the Soviet base"),
                   msg(0, "Deploy the construction truck to build a base. Engineers repair damaged vehicles.")};
  Trigger taken;
  taken.name = "Village taken";
  taken.conditions = {cond(CondType::UnitsInArea, 0, 0, 4), cond(CondType::NoUnitsInArea, 1, 0)};
  taken.actions = {simple(ActionType::CompleteObjective, 0, 0),
                   msg(0, "The village is ours. Reinforcements are coming up the west road."),
                   spawn(0, b.t("ger_medium_tank"), 1, 2),
                   spawn(0, b.t("ger_tank_destroyer"), 1, 1),
                   spawn(0, b.t("ger_at_team"), 1, 2),
                   simple(ActionType::GiveSupplies, 0, 2000),
                   objective(0, "Hold the village against the counter-attack")};
  Trigger counter;
  counter.name = "Counter-attack";
  {
    Condition after = cond(CondType::TriggerFired, 0);
    after.trigger = 1;
    Condition wait = cond(CondType::Timer, 0, -1, 600);  // ten minutes into the battle
    counter.conditions = {after, wait};
  }
  {
    Action atk;
    atk.type = ActionType::AttackMove;
    atk.player = 1;
    atk.area = 3;
    atk.area2 = 0;
    counter.actions = {msg(0, "Soviet armour is crossing the river! Hold the village!"),
                       spawn(1, b.t("rus_medium_tank"), 3, 3),
                       spawn(1, b.t("rus_rifleman"), 3, 8),
                       spawn(1, b.t("rus_at_team"), 3, 2),
                       atk};
  }
  Trigger held;
  held.name = "Village held";
  {
    Condition after = cond(CondType::TriggerFired, 0);
    after.trigger = 2;
    Condition clear = cond(CondType::NoUnitsInArea, 1, 0);
    Condition still = cond(CondType::UnitsInArea, 0, 0, 1);
    Condition wait = cond(CondType::Timer, 0, -1, 840);  // four minutes after the counter-attack
    held.conditions = {after, clear, still, wait};
  }
  held.actions = {simple(ActionType::CompleteObjective, 0, 2), msg(0, "The counter-attack is broken. Push on to the Soviet base.")};
  Trigger win;
  win.name = "Victory";
  win.conditions = {cond(CondType::PlayerDefeated, 1)};
  win.actions = {simple(ActionType::CompleteObjective, 0, 1), simple(ActionType::Victory, 0)};
  Trigger lose;
  lose.name = "Defeat";
  lose.conditions = {cond(CondType::PlayerDefeated, 0)};
  lose.actions = {simple(ActionType::Defeat, 0)};
  s.triggers = {brief, taken, counter, held, win, lose};

  std::string err;
  if (!saveMap(out, b.file.map, b.file.units, b.file.players, err, scriptToJson(s, &b.types).dump())) {
    std::fprintf(stderr, "save: %s\n", err.c_str());
    return false;
  }
  std::printf("wrote %s (%zu units)\n", out.c_str(), b.file.units.size());
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  const std::string content = argc > 1 ? argv[1] : "content/";
  Builder b;
  if (!b.load(content)) return 1;
  return kalach(b, content + "scenarios/Kalach_Crossing.lmap") ? 0 : 1;
}

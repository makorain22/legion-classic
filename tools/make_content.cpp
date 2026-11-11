// Builds the maps that ship with Legion Classic: the tutorial, the campaign
// missions, the bundled scenario and the skirmish maps.
//
// Missions are normally made in the game's editor; these are written from
// code so they can be rebuilt whenever the rules change:
//
//   cmake --build build --target make_content
//   ./build/make_content content/            (everything)
//   ./build/make_content content/ tutorial   (or: campaigns, skirmish, scenarios)
//
// Briefings (Markdown) and campaign.json files sit beside the maps and are
// written by hand.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

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
  void start(int size, Biome b, uint64_t seed, int layout, const std::string& name, const std::string& description) {
    file = MapFile{};
    file.map.resize(size, size);
    file.map.generate(b, seed, layout);
    file.map.name = name;
    file.map.description = description;
    sim = std::make_unique<Simulation>(file.map, types, weapons, 1);
    file.players.assign(MAX_PLAYERS, PlayerSetup{});
    for (int i = 0; i < MAX_PLAYERS; ++i) {
      file.players[static_cast<size_t>(i)].name = "Player " + std::to_string(i + 1);
      file.players[static_cast<size_t>(i)].team = static_cast<uint8_t>(i);
    }
  }
  int t(const std::string& id) const {
    const int i = types.find(id);
    if (i < 0) std::fprintf(stderr, "unknown unit %s\n", id.c_str());
    return i;
  }
  void side(uint8_t i, const std::string& name, const std::string& faction, uint32_t color, uint8_t team, bool human,
            int credits, int startX = -1, int startY = -1, uint8_t difficulty = 1) {
    PlayerSetup& p = file.players[i];
    p.name = name;
    p.faction = faction;
    p.color = color;
    p.team = team;
    p.active = true;
    p.human = human;
    p.difficulty = difficulty;
    p.credits = credits;
    p.startX = startX >= 0 ? startX * T : -1;
    p.startY = startY >= 0 ? startY * T : -1;
  }
  bool dry(int x, int y) const {
    if (!file.map.inBounds(x, y)) return false;
    const TileInfo& ti = file.map.info(x, y);
    return ti.waterDepth <= -6 && ti.slope <= 20;
  }
  // Places a unit or structure at the nearest spot that takes it.
  bool place(const std::string& id, uint8_t owner, int tx, int ty, int facing = 0, bool quiet = false) {
    const int type = t(id);
    if (type < 0) return false;
    const UnitTypeDef& d = types.get(type);
    for (int r = 0; r < (d.bridge ? 1 : 16); ++r)  // a bridge piece goes where it is or nowhere
      for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
          if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
          const int odd = d.isStructure() ? (d.footW % 2 ? T / 2 : 0) : T / 2;
          const int oddY = d.isStructure() ? (d.footH % 2 ? T / 2 : 0) : T / 2;
          const Vec2i p{(tx + dx) * T + odd, (ty + dy) * T + oddY};
          const int px = p.x / T, py = p.y / T;
          if (!file.map.inBounds(px, py)) continue;
          if (d.isStructure()) {
            if (!sim->canPlace(type, sim->footprintOrigin(type, p))) continue;
          } else if (!dry(px, py) || (file.map.block(px, py) & Map::kOccupied)) {
            continue;
          }
          Command c;
          c.type = CommandType::SpawnList;
          c.spawns = {{static_cast<uint16_t>(type), owner, p, angleOfFacing(facing), -1}};
          sim->queue(c);
          sim->tick();
          file.units.push_back({id, owner, p.x, p.y, static_cast<uint16_t>(angleOfFacing(facing)), -1});
          return true;
        }
    if (!quiet) std::fprintf(stderr, "no room for %s near %d,%d\n", id.c_str(), tx, ty);
    return false;
  }
  void squad(const std::string& id, uint8_t owner, int tx, int ty, int n, int facing) {
    for (int i = 0; i < n; ++i) place(id, owner, tx + (i % 4) * 2 - 3, ty + (i / 4) * 2, facing);
  }
  // A line of field works (trench, wire, sandbags) from a to b.
  void line(const std::string& id, uint8_t owner, int x0, int y0, int x1, int y1) {
    const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    for (int i = 0; i <= n; ++i) {
      const int x = x0 + (n ? (x1 - x0) * i / n : 0), y = y0 + (n ? (y1 - y0) * i / n : 0);
      place(id, owner, x, y, 0, true);
    }
  }
  // The generator's village plots as neutral houses, except near `keepClear` (tiles).
  void villages(const std::vector<std::tuple<int, int, int>>& keepClear) {
        for (const Map::Lot& l : file.map.lots) {
      bool clear = true;
      for (const auto& [x, y, r] : keepClear) clear = clear && (std::abs(l.tx - x) > r || std::abs(l.ty - y) > r);
      if (!clear && l.kind != Map::kBridgeLot && l.kind != Map::kWoodBridgeLot) continue;  // bridges stay whole
      place(Map::lotBuildingId(l.kind), NEUTRAL_OWNER, l.tx, l.ty, static_cast<int>((l.tx * 7 + l.ty * 3) % 4) * 4, true);
    }
    if (std::getenv("LEGION_DEBUG_LOTS")) std::fprintf(stderr, "lots %zu\n", file.map.lots.size());
    file.map.lots.clear();
  }
  // A base's income: post office, logistics warehouse and fuel depot behind
  // the headquarters at (x, y), away from (awayX, awayY).
  void income(const std::string& faction, uint8_t owner, int x, int y, int awayX, int awayY) {
    const int dx = x > awayX ? 1 : -1, dy = y > awayY ? 1 : -1;
    const bool wide = std::abs(x - awayX) >= std::abs(y - awayY);
    place(faction + "_post", owner, x + (wide ? dx * 7 : -6), y + (wide ? -6 : dy * 7));
    place(faction + "_warehouse", owner, x + (wide ? dx * 8 : 0), y + (wide ? 0 : dy * 8));
    place(faction + "_depot", owner, x + (wide ? dx * 7 : 6), y + (wide ? 7 : dy * 8));
  }
  Vec2i nearestSupply(int x, int y) const {
    Vec2i best = file.map.supplyPoints.empty() ? Vec2i{x, y} : file.map.supplyPoints.front();
    for (const Vec2i& p : file.map.supplyPoints)
      if (std::abs(p.x - x) + std::abs(p.y - y) < std::abs(best.x - x) + std::abs(best.y - y)) best = p;
    return best;
  }
  bool save(const std::string& path, const ScenarioScript* s = nullptr) {
    std::string err;
    const std::string script = s ? scriptToJson(*s, &types).dump() : std::string();
    if (!saveMap(path, file.map, file.units, file.players, err, script)) {
      std::fprintf(stderr, "save %s: %s\n", path.c_str(), err.c_str());
      return false;
    }
    std::printf("wrote %s (%zu units)\n", path.c_str(), file.units.size());
    return true;
  }
};

// A seed whose map is dry ground along every listed tile.
uint64_t findSeed(int size, Biome b, int layout, uint64_t from, const std::vector<std::pair<int, int>>& mustBeDry) {
  for (uint64_t seed = from;; ++seed) {
    Map probe(size, size);
    probe.generate(b, seed, layout);
    bool ok = true;
    for (const auto& [x, y] : mustBeDry) ok = ok && probe.info(x, y).waterDepth < -16 && probe.info(x, y).slope <= 20;
    if (ok) return seed;
  }
}
std::vector<std::pair<int, int>> row(int x0, int x1, int y, int step = 2) {
  std::vector<std::pair<int, int>> out;
  for (int x = x0; x <= x1; x += step) out.push_back({x, y});
  return out;
}
std::vector<std::pair<int, int>> rect(int x0, int y0, int x1, int y1, int step = 3) {
  std::vector<std::pair<int, int>> out;
  for (int y = y0; y <= y1; y += step)
    for (int x = x0; x <= x1; x += step) out.push_back({x, y});
  return out;
}
std::vector<std::pair<int, int>> operator+(std::vector<std::pair<int, int>> a, const std::vector<std::pair<int, int>>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// ---------------------------------------------------------------------------
// Script building blocks
// ---------------------------------------------------------------------------
Condition C(CondType type, uint8_t player = 0, int area = -1, int value = 0, int unitType = -1) {
  Condition c;
  c.type = type;
  c.player = player;
  c.area = area;
  c.value = value;
  c.unitType = unitType;
  return c;
}
Condition always() { return C(CondType::Always); }
Condition fired(int trigger, int seconds = 0) {
  Condition c = C(CondType::TriggerFired, 0, -1, seconds);
  c.trigger = trigger;
  return c;
}
Condition timer(int seconds) { return C(CondType::Timer, 0, -1, seconds); }
Condition inArea(uint8_t p, int area, int n, int unit = -1) { return C(CondType::UnitsInArea, p, area, n, unit); }
Condition noneIn(uint8_t p, int area, int unit = -1) { return C(CondType::NoUnitsInArea, p, area, 0, unit); }
Condition atMost(uint8_t p, int n, int unit = -1) { return C(CondType::UnitCount, p, -1, n, unit); }
Condition atLeast(uint8_t p, int n, int unit = -1) { return C(CondType::UnitsAtLeast, p, -1, n, unit); }
Condition cover(uint8_t p, int n) { return C(CondType::InCover, p, -1, n); }
Condition defeated(uint8_t p) { return C(CondType::PlayerDefeated, p); }
Condition order(uint8_t p, const char* kind, int n = 1) {
  Condition c = C(CondType::OrderGiven, p, -1, n);
  c.order = orderFromId(kind);
  return c;
}
Condition flagAtLeast(const char* name, int v) {
  Condition c = C(CondType::FlagAtLeast, 0, -1, v);
  c.text = name;
  return c;
}

Action A(ActionType type, uint8_t player = ANY_PLAYER, int value = 0) {
  Action a;
  a.type = type;
  a.player = player;
  a.value = value;
  return a;
}
Action say(uint8_t p, const std::string& text) {
  Action a = A(ActionType::Message, p);
  a.text = text;
  return a;
}
Action hint(uint8_t p, const std::string& text) {
  Action a = A(ActionType::Hint, p);
  a.text = text;
  return a;
}
Action highlight(uint8_t p, const std::string& what) {
  Action a = A(ActionType::Highlight, p);
  a.text = what;
  return a;
}
Action objective(uint8_t p, const std::string& text, bool optional = false) {
  Action a = A(ActionType::AddObjective, p, optional ? 1 : 0);
  a.text = text;
  return a;
}
Action done(int i) { return A(ActionType::CompleteObjective, ANY_PLAYER, i); }
Action failed(int i) { return A(ActionType::FailObjective, ANY_PLAYER, i); }
Action win(uint8_t p) { return A(ActionType::Victory, p); }
Action lose(uint8_t p) { return A(ActionType::Defeat, p); }
Action spawn(uint8_t p, int unit, int area, int n) {
  Action a = A(ActionType::SpawnUnits, p, n);
  a.unitType = unit;
  a.area = area;
  return a;
}
Action attack(uint8_t p, int from, int to, int unit = -1) {
  Action a = A(ActionType::AttackMove, p);
  a.area = from;
  a.area2 = to;
  a.unitType = unit;
  return a;
}
Action reveal(uint8_t p, int area) {
  Action a = A(ActionType::RevealArea, p);
  a.area = area;
  return a;
}
Action lock(uint8_t p, int unit) {
  Action a = A(ActionType::LockBuild, p);
  a.unitType = unit;
  return a;
}
Action unlock(uint8_t p, int unit) {
  Action a = A(ActionType::UnlockBuild, p);
  a.unitType = unit;
  return a;
}
Action supplies(uint8_t p, int n) { return A(ActionType::GiveSupplies, p, n); }
Action setFlag(const char* name, int v) {
  Action a = A(ActionType::SetFlag, ANY_PLAYER, v);
  a.text = name;
  return a;
}

struct Script {
  ScenarioScript s;
  int area(const std::string& name, int x0, int y0, int x1, int y1) {
    s.areas.push_back({name, x0, y0, x1, y1});
    return static_cast<int>(s.areas.size()) - 1;
  }
  int on(const std::string& name, std::vector<Condition> c, std::vector<Action> a, bool repeat = false) {
    Trigger t;
    t.name = name;
    t.conditions = std::move(c);
    t.actions = std::move(a);
    t.repeat = repeat;
    s.triggers.push_back(std::move(t));
    return static_cast<int>(s.triggers.size()) - 1;
  }
};

constexpr uint32_t kRed = 0xFF2C3AC4, kBlue = 0xFFC46034, kGreen = 0xFF48A042, kKhaki = 0xFF4A86B0, kGrey = 0xFF888888;

// ---------------------------------------------------------------------------
// The tutorial: Basic Training (British, against an opposing force in German kit)
// ---------------------------------------------------------------------------
bool tutorialFirstOrders(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(64, Biome::Temperate, static_cast<int>(MapLayout::Steppe), 101, row(6, 56, 32) + rect(38, 26, 50, 38));
  b.start(64, Biome::Temperate, seed, static_cast<int>(MapLayout::Steppe), "Basic Training: First Orders",
          "Selecting, moving and fighting.");
  b.file.map.lots.clear();
  b.side(0, "Training Company", "gbr", kKhaki, 0, true, 0, 10, 32);
  b.side(1, "Opposing Force", "ger", kRed, 1, true, 0, 54, 32);
  b.place("neutral_well", NEUTRAL_OWNER, 26, 32);
  b.place("neutral_farmhouse", NEUTRAL_OWNER, 25, 27, 4);
  b.place("neutral_barn", NEUTRAL_OWNER, 27, 37, 8);
  b.squad("gbr_rifleman", 0, 10, 30, 6, 0);
  b.place("gbr_mg_team", 0, 8, 34, 0);
  for (auto [x, y] : {std::pair{42, 30}, {44, 33}, {45, 29}, {43, 35}}) b.place("ger_rifleman", 1, x, y, 8);

  Script s;
  const int farm = s.area("Farm", 21, 27, 31, 37), wood = s.area("Wood", 37, 24, 51, 40), east = s.area("East edge", 57, 27, 62, 37);
  const int rifle = b.t("ger_rifleman");
  const int start = s.on("Start", {always()},
       {hint(0, "Basic Training\nWelcome, commander. Move the view by touching a screen edge with the mouse or with the arrow "
                "keys; drag with the middle button to pan; the mouse wheel zooms.\n\nYour section is on the left. Select it: hold "
                "the left mouse button and drag a box around the men."),
        objective(0, "March your section to the farm by the well"), reveal(0, farm)});
  s.on("First move", {fired(start), order(0, "move")},
       {hint(0, "Moving\nGood. Right-click on the ground to send the selected men there; right-drag to set them out in a line "
                "facing the way you drag.\n\nTake the whole section to the farm by the well, to the east.")});
  const int atFarm = s.on("At the farm", {fired(start), inArea(0, farm, 5)},
       {done(0),
        hint(0, "Attack-move\nAn enemy patrol is hiding in the wood further east. A plain move walks past the enemy; an "
                "attack-move fights whatever it meets on the way.\n\nSelect your men, press A, then click beyond the wood "
                "(or Ctrl+right-click there). Keep the Bren team with them: it pins infantry down."),
        objective(0, "Clear the patrol out of the wood"), reveal(0, wood)});
  const int clear = s.on("Wood clear", {fired(atFarm), atMost(1, 0)},
       {done(1),
        hint(0, "Well done\nWhile you wait, some habits worth learning:\n- Ctrl+1 to 9 makes a control group; press the number "
                "to select it again.\n- Double-click a man to select every man of that kind on screen.\n- S stops, Space pauses, "
                "+ and - change the game speed.\n\nThe enemy will try to take the wood back. Stay in it: trees give cover."),
        objective(0, "Hold the wood"), spawn(0, b.t("gbr_rifleman"), farm, 4),
        say(0, "Four more men have come up to the farm. Bring them into the wood.")});
  const int counter = s.on("Counter-attack", {fired(clear, 35)},
       {spawn(1, rifle, east, 5), attack(1, east, wood), say(0, "Enemy infantry coming from the east!")});
  s.on("Held", {fired(counter, 8), atMost(1, 0)},
       {done(2), hint(0, "First Orders: passed\nYou can select, move and fight. Next: cover, and what to do about a tank."), win(0)});
  s.on("Lost", {atMost(0, 0)}, {lose(0)});
  return b.save(out, &s.s);
}

bool tutorialCover(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(64, Biome::Temperate, static_cast<int>(MapLayout::Steppe), 201, row(6, 58, 32) + rect(22, 26, 32, 38));
  b.start(64, Biome::Temperate, seed, static_cast<int>(MapLayout::Steppe), "Basic Training: Cover and Armour",
          "Houses, sandbags, and the PIAT.");
  b.file.map.lots.clear();
  b.side(0, "Training Company", "gbr", kKhaki, 0, true, 0, 12, 32);
  b.side(1, "Opposing Force", "ger", kRed, 1, true, 0, 54, 32);
  b.place("neutral_cottage", NEUTRAL_OWNER, 25, 28, 4);
  b.place("neutral_cottage", NEUTRAL_OWNER, 25, 36, 12);
  b.place("neutral_shed", NEUTRAL_OWNER, 22, 32);
  b.line("gbr_sandbags", 0, 30, 29, 30, 35);
  b.squad("gbr_rifleman", 0, 12, 30, 8, 0);
  b.place("gbr_mg_team", 0, 10, 35, 0);
  b.place("gbr_atgun", 0, 27, 32, 0);

  Script s;
  const int village = s.area("Village", 20, 24, 33, 40), east = s.area("East edge", 57, 26, 62, 38),
            lane = s.area("Lane", 14, 30, 18, 34);
  const int start = s.on("Start", {always()},
       {hint(0, "Cover\nMen caught in the open are cut down quickly. Inside a house they fire from the windows and are hard to "
                "hit.\n\nSelect some riflemen and right-click one of the cottages in the village to the east: they go inside. "
                "Put at least four men under a roof."),
        objective(0, "Garrison the cottages (four men)"), reveal(0, village)});
  const int inside = s.on("Garrisoned", {fired(start), cover(0, 4)},
       {done(0),
        hint(0, "Sandbags\nMen standing next to sandbags are protected too. Move the rest of your men and the Bren team up "
                "behind the sandbags on the east side of the village.\n\nThe enemy is coming. Your men open fire on their own "
                "once the enemy is in range."),
        objective(0, "Hold the village")});
  const int wave = s.on("Infantry", {fired(inside, 25)},
       {spawn(1, b.t("ger_rifleman"), east, 6), attack(1, east, village),
        say(0, "Enemy infantry advancing on the village!")});
  const int tank = s.on("Tank", {fired(wave, 10), atMost(1, 0)},
       {hint(0, "Armour\nA tank is coming. Rifle bullets hardly scratch it, and it will shoot your men out of the houses.\n\n"
                "Two PIAT teams have just come up the lane behind the village. Select them and right-click the tank. The PIAT is short-ranged, but a hit in the side or the rear "
                "does far more harm than one on the thick front armour: work round its flank. The 6-pounder dug in behind "
                "the village will help."),
        objective(0, "Knock out the tank"), spawn(0, b.t("gbr_at_team"), lane, 2),
        say(0, "Two PIAT teams have come up the lane west of the village."), spawn(1, b.t("ger_light_tank"), east, 1),
        attack(1, east, village)});
  const int killed = s.on("Tank knocked out", {fired(tank, 5), atMost(1, 0)},
       {done(1), done(2), highlight(0, "order.evacuate"),
        hint(0, "Evacuate\nTo bring men out of a building, select it (or the men) and press Evacuate, or E.\n\nTry it.")});
  s.on("Evacuated", {fired(killed), order(0, "unload")},
       {highlight(0, ""), hint(0, "Cover and Armour: passed\nNext: digging in."), win(0)});
  s.on("Time", {fired(killed, 40)}, {highlight(0, ""), win(0)});
  s.on("Lost", {atMost(0, 0)}, {lose(0)});
  return b.save(out, &s.s);
}

bool tutorialDigIn(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(64, Biome::Temperate, static_cast<int>(MapLayout::Steppe), 301, row(4, 58, 32) + rect(6, 24, 36, 40));
  b.start(64, Biome::Temperate, seed, static_cast<int>(MapLayout::Steppe), "Basic Training: Digging In",
          "Trenches, wire and holding a line.");
  b.file.map.lots.clear();
  b.side(0, "Training Company", "gbr", kKhaki, 0, true, 400, 18, 32);
  b.side(1, "Opposing Force", "ger", kRed, 1, true, 0, 54, 32);
  b.place("gbr_yard", 0, 16, 32);
  b.place("gbr_power", 0, 11, 26);
  b.place("gbr_barracks", 0, 16, 39);
  b.squad("gbr_rifleman", 0, 21, 29, 12, 0);
  b.place("gbr_mg_team", 0, 20, 36, 0);
  b.place("gbr_mg_team", 0, 22, 36, 0);

  Script s;
  const int front = s.area("Front", 22, 20, 36, 44), east = s.area("East edge", 57, 24, 62, 40);
  const int start = s.on("Start", {always()},
       {lock(0, -1), unlock(0, b.t("gbr_trench")), unlock(0, b.t("gbr_wire")), unlock(0, b.t("gbr_sandbags")),
        highlight(0, "tab.defend"),
        hint(0, "Digging in\nYour company has orders to hold this ground. Open the Defend tab in the sidebar: the highlighted "
                "button.\n\nThen click Slit Trench, hold the left button and drag a line across the field east of the base: a "
                "whole line is dug in one go. Six pieces will do.\n\nYou can only build near your own buildings; the yard reaches "
                "furthest."),
        objective(0, "Dig a trench line of six pieces east of the base")});
  s.on("Point at the trench", {fired(start, 7)}, {highlight(0, "build:gbr_trench")});
  const int dug = s.on("Dug", {fired(start), atLeast(0, 6, b.t("gbr_trench"))},
       {done(0), highlight(0, ""),
        hint(0, "Man the trench\nSelect riflemen and right-click a trench piece. Three men fit in each piece; send more and they "
                "spread along the whole line. Men in a trench stay put until you order them out with Evacuate (E)."),
        objective(0, "Put nine men in the trench")});
  const int manned = s.on("Manned", {fired(dug), cover(0, 9)},
       {done(1), highlight(0, "build:gbr_wire"),
        hint(0, "Wire\nNow lay wire a few tiles in front of the trench, the same way: click Dannert Wire and drag a "
                "line. Infantry crawl through wire, under your fire."),
        objective(0, "Lay six pieces of wire in front of the trench")});
  const int wired = s.on("Wired", {fired(manned), atLeast(0, 6, b.t("gbr_wire"))},
       {done(2), highlight(0, ""),
        hint(0, "Stand to\nHere they come. Your men fire from the trench on their own.\n\nSandbags, also on the Defend tab, "
                "are manned the same way: drag a short wall behind the trench, then right-click it (or right next to it) "
                "with the Bren teams. Three men fit behind each piece, and the bags take half of what is "
                "thrown at them."),
        objective(0, "Beat off the attack"), spawn(1, b.t("ger_rifleman"), east, 9), attack(1, east, front)});
  const int second = s.on("Second wave", {fired(wired, 40), atMost(1, 0)},
       {say(0, "More of them, coming on in a rush!"), spawn(1, b.t("ger_rifleman"), east, 10), attack(1, east, front)});
  s.on("Held", {fired(second, 10), atMost(1, 0)},
       {done(3), unlock(0, -1), hint(0, "Digging In: passed\nWell dug in, a few men stop many. Next: a base, and paying for it."),
        win(0)});
  s.on("Lost", {atMost(0, 3, b.t("gbr_rifleman")), atMost(0, 0, b.t("gbr_mg_team")), fired(wired)}, {lose(0)});
  return b.save(out, &s.s);
}

bool tutorialBase(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(96, Biome::Temperate, static_cast<int>(MapLayout::Crossroads), 401,
                                 rect(8, 40, 26, 56) + rect(70, 14, 84, 26) + rect(36, 44, 44, 52));
  b.start(96, Biome::Temperate, seed, static_cast<int>(MapLayout::Crossroads), "Basic Training: Base and Supply",
          "Building a base, supplies, and an attack.");
  b.side(0, "Training Company", "gbr", kKhaki, 0, true, 4000, 16, 48);
  b.side(1, "Opposing Force", "ger", kRed, 1, true, 0, 78, 20);
  b.file.map.supplyPoints = {{40, 48}};  // one logistics point, a short march east of the yard
  b.villages({{16, 48, 14}, {78, 20, 12}, {40, 48, 6}});
  b.place("gbr_mcv", 0, 16, 48, 0);
  b.squad("gbr_rifleman", 0, 20, 52, 4, 0);
  b.place("ger_barracks", 1, 78, 18);
  b.place("ger_power", 1, 84, 22);
  b.place("ger_bunker", 1, 72, 22);
  b.place("ger_mg_nest", 1, 74, 16);
  b.squad("ger_rifleman", 1, 76, 24, 6, 8);
  b.place("ger_mg_team", 1, 72, 26, 8);

  Script s;
  const int outpost = s.area("Outpost", 66, 10, 90, 30), point = s.area("Logistics point", 35, 43, 45, 53);
  const int start = s.on("Start", {always()},
       {lock(0, -1), highlight(0, "order.deploy"),
        hint(0, "Building a base\nEverything starts with the Construction Lorry. Select it, drive it onto open, level ground "
                "and press Deploy (the highlighted button): it unfolds into a Construction Yard, the heart of your base."),
        objective(0, "Deploy the construction lorry")});
  const int yard = s.on("Yard", {fired(start), atLeast(0, 1, b.t("gbr_yard"))},
       {done(0), unlock(0, b.t("gbr_power")), highlight(0, "build:gbr_power"),
        hint(0, "Power\nThe Build tab lists what the yard can build. Click Power Station. When it is ready the button flashes: "
                "click it, then click the ground near the yard to place it.\n\nShort of power, everything builds at half "
                "speed."),
        objective(0, "Build a power station")});
  const int power = s.on("Power", {fired(yard), atLeast(0, 1, b.t("gbr_power"))},
       {done(1), unlock(0, b.t("gbr_post")), highlight(0, "build:gbr_post"),
        hint(0, "Supply\nSupplies pay for everything, and three buildings earn them every minute: the Post Office a little, "
                "the Logistics Warehouse more, the Strategic Fuel Depot most. Each construction yard allows three post "
                "offices, two warehouses and one fuel depot. Start with a Post Office: build one and place it near the yard."),
        objective(0, "Build a post office")});
  const int depot = s.on("Depot", {fired(power), atLeast(0, 1, b.t("gbr_post"))},
       {done(2), unlock(0, b.t("gbr_barracks")), unlock(0, b.t("gbr_rifleman")), unlock(0, b.t("gbr_mg_team")),
        unlock(0, b.t("gbr_at_team")), unlock(0, b.t("gbr_engineer")), unlock(0, b.t("gbr_recruit")),
        highlight(0, "build:gbr_barracks"),
        hint(0, "Barracks\nThe post office also calls up Recruits: cheap levy infantry, weak unless massed. Proper soldiers "
                "come from a Drill Hall: build one. Then open the Train tab and click Rifleman: each click trains one man, "
                "right-click takes one off the order. Train ten."),
        objective(0, "Train ten riflemen")});
  s.on("Train tab", {fired(depot), atLeast(0, 1, b.t("gbr_barracks"))}, {highlight(0, "tab.train")});
  const int men = s.on("Riflemen", {fired(depot), atLeast(0, 14, b.t("gbr_rifleman"))},
       {done(3), unlock(0, b.t("gbr_warehouse")), unlock(0, b.t("gbr_supply_truck")), highlight(0, "build:gbr_warehouse"),
        hint(0, "Logistics\nA Logistics Warehouse earns twice what a post office does, keeps the ammunition coming (friendly "
                "units near it fire a little faster) and builds supply lorries. Tanks need one behind them. Build a "
                "Logistics Warehouse."),
        objective(0, "Build a logistics warehouse")});
  const int warehouse = s.on("Warehouse", {fired(men), atLeast(0, 1, b.t("gbr_warehouse"))},
       {done(4), highlight(0, "minimap"), reveal(0, point),
        hint(0, "Logistics points\nThe real money is out on the map. The gold square on the minimap, east of here, is a "
                "logistics point. Send your riflemen to stand on it: hold it alone for twelve seconds and a Logistics "
                "Office goes up, with three lorries that carry money back to your yard. Offices and lorries can be "
                "destroyed, yours and the enemy's alike."),
        objective(0, "Take the logistics point")});
  const int office = s.on("Office", {fired(warehouse), atLeast(0, 1, b.t("gbr_office"))},
       {done(5), unlock(0, b.t("gbr_factory")), unlock(0, b.t("gbr_scout_car")), unlock(0, b.t("gbr_halftrack")),
        unlock(0, b.t("gbr_light_tank")), unlock(0, b.t("gbr_medium_tank")), highlight(0, "build:gbr_factory"),
        hint(0, "Armour\nThe lorries are on their way: watch for the figures over your yard as they pay in. A lorry you "
                "lose is replaced from the warehouse. Now a Tank Workshop: build one, then open the Vehicles tab and "
                "build a Cromwell."),
        objective(0, "Build a Cromwell")});
  s.on("Vehicles tab", {fired(office), atLeast(0, 1, b.t("gbr_factory"))}, {highlight(0, "tab.vehicles")});
  const int cromwell = s.on("Cromwell", {fired(office), atLeast(0, 1, b.t("gbr_medium_tank"))},
       {done(6), unlock(0, -1), highlight(0, ""),
        hint(0, "Take the outpost\nScouts have found an enemy outpost to the north-east. Gather your force and attack-move "
                "on it (A, then click). Engineers mend damaged tanks: take some along."),
        objective(0, "Destroy the enemy outpost"), reveal(0, outpost)});
  s.on("Outpost taken", {fired(cromwell), noneIn(1, outpost)},
       {done(7), hint(0, "Basic Training complete\nYou are ready to command. The campaigns are waiting."), win(0)});
  s.on("Lost", {atMost(0, 0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// Germany: Kampfgruppe Adler
// ---------------------------------------------------------------------------
// Winter, 1946. A German battle group must seize a crossroads village held by
// Soviet infantry, beat off a counter-attack, then destroy the Soviet base.
bool kalach(Builder& b, const std::string& out) {
  auto cond = [](CondType type, uint8_t player, int area = -1, int value = 0) { return C(type, player, area, value); };
  auto msg = [](uint8_t p, const std::string& t) { return say(p, t); };
  auto simple = [](ActionType type, uint8_t player, int32_t value = 0) { return A(type, player, value); };
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
  b.start(128, Biome::Winter, seed, -1, "Kalach Crossing",
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
  for (auto [dx, dy, id] : {std::tuple{0, 0, "rus_yard"}, {-6, -6, "rus_power"}, {-6, 6, "rus_power"},
                            {5, 7, "rus_barracks"}, {-1, 12, "rus_factory"}, {-10, -2, "rus_bunker"}, {-10, 3, "rus_atgun"}})
    b.place(id, 1, bx + dx, by + dy);
  b.squad("rus_rifleman", 1, bx - 8, by, 6, 8);
  b.income("rus", 1, bx, by, 64, 64);

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
                   msg(0, "Deploy the construction truck to build a base, with a post office and a warehouse for income. "
                          "Infantry can hold the village houses: right-click a house with them selected.")};
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
  // Campaign: the Tiger that survives Kalach comes back in the next missions.
  Trigger tiger;
  tiger.name = "Tiger survived";
  tiger.conditions = {cond(CondType::PlayerDefeated, 1), atLeast(0, 1, b.t("ger_heavy_tank"))};
  tiger.actions = {setFlag("tiger_survived", 1)};
  Trigger win;
  win.name = "Victory";
  win.conditions = {cond(CondType::PlayerDefeated, 1)};
  win.actions = {simple(ActionType::CompleteObjective, 0, 1), simple(ActionType::Victory, 0)};
  Trigger lose;
  lose.name = "Defeat";
  lose.conditions = {cond(CondType::PlayerDefeated, 0)};
  lose.actions = {simple(ActionType::Defeat, 0)};
  s.triggers = {brief, taken, counter, held, tiger, win, lose};

  std::string err;
  if (!saveMap(out, b.file.map, b.file.units, b.file.players, err, scriptToJson(s, &b.types).dump())) {
    std::fprintf(stderr, "save: %s\n", err.c_str());
    return false;
  }
  std::printf("wrote %s (%zu units)\n", out.c_str(), b.file.units.size());
  return true;
}

bool gerIceRoad(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(128, Biome::Winter, static_cast<int>(MapLayout::FrontLine), 1947,
                                 rect(8, 52, 40, 76) + rect(96, 52, 120, 76));
  b.start(128, Biome::Winter, seed, static_cast<int>(MapLayout::FrontLine), "The Ice Road",
          "January 1947. Hold the crossroads at Verkhny until the supply columns are through.");
  b.side(0, "Kampfgruppe Adler", "ger", kRed, 0, true, 3000, 30, 64);
  b.side(1, "Red Army Tank Corps", "rus", kBlue, 1, false, 4000, 108, 64, 1);
  b.villages({{24, 64, 14}, {108, 64, 16}});
  const int bx = 22, by = 64;
  for (auto [dx, dy, id] : {std::tuple{0, 0, "ger_yard"}, {-5, -6, "ger_power"}, {-5, 6, "ger_power"}, {5, 7, "ger_barracks"},
                            {1, 12, "ger_factory"}})
    b.place(id, 0, bx + dx, by + dy);
  b.income("ger", 0, bx, by, 64, 64);
  // The crossroads: a hamlet, dug in.
  const int cx = 42, cy = 64;
  for (auto [dx, dy, id] : {std::tuple{-3, -5, "neutral_izba"}, {3, 5, "neutral_izba"}, {-4, 5, "neutral_barn"}, {4, -4, "neutral_cottage_bombed"}})
    b.place(id, NEUTRAL_OWNER, cx + dx, cy + dy);
  b.line("ger_trench", 0, cx + 6, cy - 6, cx + 6, cy + 6);
  b.place("ger_bunker", 0, cx + 3, cy);
  b.squad("ger_rifleman", 0, cx, cy - 2, 10, 0);
  b.squad("ger_mg_team", 0, cx - 2, cy + 3, 2, 0);
  b.squad("ger_at_team", 0, cx - 4, cy - 4, 2, 0);
  b.place("ger_medium_tank", 0, cx - 6, cy - 3);
  b.place("ger_medium_tank", 0, cx - 6, cy + 3);
  b.place("ger_tank_destroyer", 0, cx - 8, cy);
  b.place("ger_engineer", 0, bx + 6, by);
  // The Soviet base.
  const int rx = 108, ry = 64;
  for (auto [dx, dy, id] : {std::tuple{0, 0, "rus_yard"}, {-6, -6, "rus_power"}, {-6, 6, "rus_power"}, {5, 7, "rus_barracks"},
                            {-1, 12, "rus_factory"}, {-10, 0, "rus_bunker"}})
    b.place(id, 1, rx + dx, ry + dy);
  b.income("rus", 1, rx, ry, 64, 64);
  b.squad("rus_rifleman", 1, rx - 8, ry, 8, 8);

  Script s;
  const int cross = s.area("Crossroads", cx - 8, cy - 9, cx + 8, cy + 9), east = s.area("East road", 84, 56, 92, 72),
            west = s.area("West road", 2, 58, 8, 70), rbase = s.area("Soviet base", rx - 14, ry - 14, std::min(127, rx + 14), ry + 16);
  s.on("Briefing", {always()},
       {say(0, "Adler: the supply columns need fifteen minutes to cross the ice road behind us. Hold the crossroads until then."),
        objective(0, "Hold the crossroads for fifteen minutes"), objective(0, "Destroy the Soviet base", true),
        objective(0, "Keep a unit at the crossroads at all times")});
  s.on("The Tiger came through", {flagAtLeast("tiger_survived", 1)},
       {spawn(0, b.t("ger_heavy_tank"), west, 1), say(0, "Our Tiger from Kalach has been repaired and rejoins the Kampfgruppe.")});
  const int w1 = s.on("First assault", {timer(180)},
       {say(0, "Soviet tanks on the east road!"), spawn(1, b.t("rus_medium_tank"), east, 3), spawn(1, b.t("rus_rifleman"), east, 10),
        attack(1, east, cross)});
  const int w2 = s.on("Second assault", {fired(w1, 240)},
       {say(0, "A heavier attack is forming up in the east."), spawn(1, b.t("rus_medium_tank"), east, 4),
        spawn(1, b.t("rus_rifleman"), east, 14), spawn(1, b.t("rus_at_team"), east, 3), attack(1, east, cross)});
  s.on("Third assault", {fired(w2, 240)},
       {say(0, "Breakthrough tanks! Hold on, the last columns are almost across."), spawn(1, b.t("rus_heavy_tank"), east, 2),
        spawn(1, b.t("rus_medium_tank"), east, 3), spawn(1, b.t("rus_rifleman"), east, 16), attack(1, east, cross)});
  s.on("Reinforcements", {timer(420)},
       {say(0, "Panzergrenadiers have come up the ice road."), spawn(0, b.t("ger_halftrack"), west, 2),
        spawn(0, b.t("ger_rifleman"), west, 10), supplies(0, 1500)});
  s.on("Base destroyed", {noneIn(1, rbase)}, {done(1), setFlag("ice_road_base", 1)});
  s.on("Crossroads lost", {timer(10), noneIn(0, cross)}, {failed(2)});
  s.on("Held", {timer(900), inArea(0, cross, 1)},
       {done(0), done(2), say(0, "The columns are through. Well held, Adler."), win(0)});
  s.on("Too late", {timer(900), noneIn(0, cross)}, {failed(0), lose(0)});
  s.on("Lost", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

bool gerDonets(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(128, Biome::Temperate, static_cast<int>(MapLayout::Ridge), 1948,
                                 rect(10, 54, 30, 74) + rect(98, 54, 118, 74));
  b.start(128, Biome::Temperate, seed, static_cast<int>(MapLayout::Ridge), "Breakthrough on the Donets",
          "June 1947. The Soviet line on the ridge must be broken before their reserves arrive.");
  b.side(0, "Kampfgruppe Adler", "ger", kRed, 0, true, 6000, 18, 64);
  b.side(1, "Red Army Front", "rus", kBlue, 1, false, 8000, 108, 64, 2);
  b.villages({{18, 64, 14}, {108, 64, 16}});
  b.place("ger_mcv", 0, 16, 64);
  b.place("ger_mcv", 0, 18, 70);
  b.place("ger_heavy_tank", 0, 24, 60);
  b.squad("ger_medium_tank", 0, 24, 64, 4, 0);
  b.squad("ger_rifleman", 0, 20, 58, 12, 0);
  b.squad("ger_at_team", 0, 20, 68, 2, 0);
  b.place("ger_artillery", 0, 12, 66);
  const int rx = 108, ry = 64;
  for (auto [dx, dy, id] : {std::tuple{0, 0, "rus_yard"}, {-6, -6, "rus_power"}, {-6, 6, "rus_power"}, {5, 7, "rus_barracks"},
                            {-1, 12, "rus_factory"}, {-12, -4, "rus_bunker"}, {-12, 4, "rus_bunker"}, {-12, 0, "rus_atgun"},
                            {4, -8, "rus_howitzer_pit"}})
    b.place(id, 1, rx + dx, ry + dy);
  b.income("rus", 1, rx, ry, 64, 64);
  // The ridge line: trenches and wire across the middle.
  b.line("rus_trench", 1, 74, 52, 74, 76);
  b.line("rus_wire", 1, 70, 54, 70, 74);
  b.squad("rus_rifleman", 1, 76, 60, 12, 8);
  b.squad("rus_mg_team", 1, 76, 68, 3, 8);
  b.place("rus_medium_tank", 1, 80, 58, 8);
  b.place("rus_medium_tank", 1, 80, 70, 8);

  Script s;
  const int ridge = s.area("Ridge line", 68, 48, 80, 80), west = s.area("West road", 2, 58, 8, 70),
            rbase = s.area("Soviet base", rx - 16, ry - 14, std::min(127, rx + 14), ry + 16);
  s.on("Briefing", {always()},
       {say(0, "Adler: break the Soviet line on the ridge and destroy their base before the reserves come up."),
        objective(0, "Break through the line on the ridge"), objective(0, "Destroy the Soviet base"),
        reveal(0, ridge)});
  s.on("Veterans", {flagAtLeast("tiger_survived", 1)},
       {spawn(0, b.t("ger_heavy_tank"), west, 1), say(0, "The Tiger that saw Kalach and the ice road leads the attack.")});
  s.on("Ice road bonus", {flagAtLeast("ice_road_base", 1)},
       {supplies(0, 3000), say(0, "Captured Soviet stores from the ice road: three thousand supplies more.")});
  s.on("Line broken", {timer(30), noneIn(1, ridge)}, {done(0), say(0, "The ridge is ours. On to their base!")});
  s.on("Reserves", {timer(900)},
       {say(0, "Soviet reserves are arriving!"), spawn(1, b.t("rus_heavy_tank"), rbase, 3), spawn(1, b.t("rus_rifleman"), rbase, 20)});
  s.on("Won", {defeated(1)}, {done(0), done(1), win(0)});
  s.on("Lost", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// The Soviet Union: Guards Rifle Corps
// ---------------------------------------------------------------------------
bool rusVillage(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(128, Biome::Temperate, static_cast<int>(MapLayout::Steppe), 1946,
                                 rect(8, 52, 30, 76) + rect(56, 52, 72, 76));
  b.start(128, Biome::Temperate, seed, static_cast<int>(MapLayout::Steppe), "Counter-stroke at Sokolovo",
          "August 1946. Retake the village the Germans seized last night, before they dig in.");
  b.side(0, "5th Guards Rifle Division", "rus", kBlue, 0, true, 2000, 16, 64);
  b.side(1, "Panzergrenadier Regiment 9", "ger", kRed, 1, true, 0, 110, 64);
  b.villages({{16, 64, 12}, {64, 64, 14}});
  const int vx = 64, vy = 64;
  b.place("neutral_church", NEUTRAL_OWNER, vx, vy);
  for (auto [dx, dy, id] : {std::tuple{-6, -5, "neutral_izba"}, {6, -5, "neutral_izba"}, {-6, 5, "neutral_school"},
                            {6, 6, "neutral_izba"}, {0, -9, "neutral_well"}, {0, 9, "neutral_barn"}})
    b.place(id, NEUTRAL_OWNER, vx + dx, vy + dy);
  b.place("ger_bunker", 1, vx - 4, vy - 2);
  b.line("ger_sandbags", 1, vx - 8, vy - 4, vx - 8, vy + 4);
  b.squad("ger_rifleman", 1, vx + 1, vy - 3, 10, 8);
  b.squad("ger_mg_team", 1, vx - 2, vy + 3, 3, 8);
  b.place("ger_at_team", 1, vx + 3, vy + 4, 8);
  b.place("ger_medium_tank", 1, vx + 6, vy, 8);
  b.squad("rus_rifleman", 0, 18, 60, 16, 0);
  b.squad("rus_mg_team", 0, 16, 68, 3, 0);
  b.squad("rus_medium_tank", 0, 22, 64, 5, 0);
  b.place("rus_artillery", 0, 10, 64);
  b.place("rus_engineer", 0, 14, 72);
  b.place("rus_engineer", 0, 16, 72);

  Script s;
  const int village = s.area("Sokolovo", vx - 9, vy - 10, vx + 9, vy + 10), east = s.area("East road", 116, 56, 124, 72);
  s.on("Briefing", {always()},
       {say(0, "Comrade commander: the Germans hold Sokolovo with a battalion. Take it back before their armour arrives."),
        objective(0, "Retake Sokolovo"), objective(0, "Hold it against the counter-attack"),
        objective(0, "Keep at least three T-34s in the fight", true), reveal(0, village)});
  const int taken = s.on("Retaken", {inArea(0, village, 5), noneIn(1, village)},
       {done(0), say(0, "Sokolovo is ours! German armour is coming from the east: dig in."), supplies(0, 1500)});
  const int counter = s.on("Counter-attack", {fired(taken, 120)},
       {spawn(1, b.t("ger_medium_tank"), east, 3), spawn(1, b.t("ger_halftrack"), east, 2), spawn(1, b.t("ger_rifleman"), east, 12),
        attack(1, east, village), say(0, "Panzers on the east road!")});
  s.on("Tanks kept", {fired(counter, 60), atMost(1, 0), atLeast(0, 3, b.t("rus_medium_tank"))},
       {done(2), setFlag("guards_tanks", 1)});
  s.on("Held", {fired(counter, 60), atMost(1, 0)},
       {done(1), say(0, "The counter-attack is beaten. Glory to the Guards!"), win(0)});
  s.on("Lost", {atMost(0, 0)}, {lose(0)});
  return b.save(out, &s.s);
}

bool rusBridgehead(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(128, Biome::Temperate, static_cast<int>(MapLayout::FrontLine), 1950,
                                 rect(8, 52, 30, 76) + rect(76, 50, 96, 78));
  b.start(128, Biome::Temperate, seed, static_cast<int>(MapLayout::FrontLine), "The Bridgehead",
          "September 1946. Cross the river, take the far bank and hold it until the engineers bring up a base.");
  b.side(0, "5th Guards Rifle Division", "rus", kBlue, 0, true, 1500, 16, 64);
  b.side(1, "Kampfgruppe Brandt", "ger", kRed, 1, false, 5000, 112, 64, 1);
  b.villages({{16, 64, 12}, {112, 64, 16}});
  b.squad("rus_rifleman", 0, 18, 60, 20, 0);
  b.squad("rus_mg_team", 0, 16, 68, 4, 0);
  b.squad("rus_at_team", 0, 14, 60, 3, 0);
  b.squad("rus_medium_tank", 0, 22, 64, 4, 0);
  b.place("rus_artillery", 0, 10, 64);
  b.place("rus_artillery", 0, 10, 68);
  const int gx = 112, gy = 64;
  for (auto [dx, dy, id] : {std::tuple{0, 0, "ger_yard"}, {-6, -6, "ger_power"}, {5, 7, "ger_barracks"}, {-1, 12, "ger_factory"},
                            {-12, 0, "ger_bunker"}})
    b.place(id, 1, gx + dx, gy + dy);
  b.income("ger", 1, gx, gy, 64, 64);
  b.squad("ger_rifleman", 1, 82, 62, 8, 8);
  b.squad("ger_mg_team", 1, 84, 66, 2, 8);

  Script s;
  const int bank = s.area("Far bank", 74, 48, 98, 80), west = s.area("West road", 2, 58, 8, 70),
            gbase = s.area("German base", gx - 8, gy - 8, std::min(127, gx + 8), gy + 8);
  s.on("Briefing", {always()},
       {say(0, "Find a ford, cross the river and take the far bank. Hold it; the engineers will follow with a construction truck."),
        objective(0, "Get ten men or vehicles onto the far bank"), objective(0, "Hold the bridgehead for eight minutes"),
        objective(0, "Destroy the German base")});
  s.on("Guards tanks", {flagAtLeast("guards_tanks", 1)},
       {spawn(0, b.t("rus_medium_tank"), west, 2), say(0, "The tank crews of Sokolovo rejoin the division.")});
  const int across = s.on("Across", {inArea(0, bank, 10)},
       {done(0), say(0, "We are across! Hold the bridgehead.")});
  const int held = s.on("Held", {fired(across, 480), inArea(0, bank, 1)},
       {done(1), say(0, "The engineers are here. Build a base and destroy the Germans."), spawn(0, b.t("rus_mcv"), west, 1),
        supplies(0, 3000)});
  s.on("Pressure", {fired(across, 120)},
       {spawn(1, b.t("ger_medium_tank"), gbase, 2), spawn(1, b.t("ger_rifleman"), gbase, 8), attack(1, gbase, bank),
        say(0, "German counter-attacks are coming. Dig in on the far bank.")});
  (void)held;
  s.on("Won", {defeated(1)}, {done(2), win(0)});
  s.on("Lost", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

bool rusIronStorm(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(128, Biome::Winter, static_cast<int>(MapLayout::Crossroads), 1951,
                                 rect(8, 52, 30, 76) + rect(96, 52, 120, 76));
  b.start(128, Biome::Winter, seed, static_cast<int>(MapLayout::Crossroads), "Iron Storm",
          "February 1947. The Germans have turned Kirovsk into a fortress. Flatten it.");
  b.side(0, "5th Guards Rifle Division", "rus", kBlue, 0, true, 5000, 16, 64);
  b.side(1, "Festung Kirovsk", "ger", kRed, 1, false, 9000, 108, 64, 2);
  b.villages({{16, 64, 14}, {108, 64, 18}});
  b.place("rus_mcv", 0, 16, 64);
  b.squad("rus_artillery", 0, 12, 58, 4, 0);
  b.squad("rus_heavy_tank", 0, 22, 62, 2, 0);
  b.squad("rus_medium_tank", 0, 22, 66, 4, 0);
  b.squad("rus_rifleman", 0, 20, 56, 16, 0);
  b.squad("rus_engineer", 0, 14, 70, 3, 0);
  const int gx = 108, gy = 64;
  for (auto [dx, dy, id] : {std::tuple{0, 0, "ger_yard"}, {-6, -6, "ger_power"}, {-6, 6, "ger_power"}, {5, 7, "ger_barracks"},
                            {-1, 12, "ger_factory"}, {-12, -5, "ger_bunker"}, {-12, 5, "ger_bunker"}, {-12, 0, "ger_atgun"},
                            {-10, -10, "ger_howitzer_pit"}, {-10, 10, "ger_mg_nest"}, {4, -9, "ger_tower"}})
    b.place(id, 1, gx + dx, gy + dy);
  b.income("ger", 1, gx, gy, 64, 64);
  b.line("ger_trench", 1, gx - 16, gy - 10, gx - 16, gy + 10);
  b.line("ger_wire", 1, gx - 19, gy - 10, gx - 19, gy + 10);
  b.line("ger_traps", 1, gx - 21, gy - 6, gx - 21, gy + 6);
  b.squad("ger_rifleman", 1, gx - 14, gy, 12, 8);
  b.place("ger_heavy_tank", 1, gx - 4, gy, 8);

  Script s;
  const int west = s.area("West road", 2, 58, 8, 70);
  s.on("Briefing", {always()},
       {say(0, "Kirovsk is a fortress: trenches, wire, guns. Use the rocket trucks to break it, then send in the tanks."),
        objective(0, "Destroy Festung Kirovsk")});
  s.on("Veterans", {flagAtLeast("guards_tanks", 1)},
       {spawn(0, b.t("rus_heavy_tank"), west, 1), say(0, "A new IS tank for the veterans of Sokolovo.")});
  s.on("Won", {defeated(1)}, {done(0), win(0)});
  s.on("Lost", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// Britain: the Desert Rats
// ---------------------------------------------------------------------------
bool gbrRidge(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(128, Biome::Desert, static_cast<int>(MapLayout::Ridge), 1946,
                                 rect(20, 50, 50, 78) + rect(100, 52, 120, 76));
  b.start(128, Biome::Desert, seed, static_cast<int>(MapLayout::Ridge), "Point 64",
          "Libya, March 1946. Hold the ridge at Point 64 while the brigade withdraws behind you.");
  b.side(0, "7th Armoured Brigade", "gbr", kKhaki, 0, true, 1500, 36, 64);
  b.side(1, "Panzer Regiment 21", "ger", kRed, 1, true, 0, 112, 64);
  b.villages({{36, 64, 14}});
  const int px = 40, py = 64;
  b.line("gbr_trench", 0, px + 4, py - 8, px + 4, py + 8);
  b.place("gbr_atgun", 0, px + 2, py - 5);
  b.place("gbr_atgun", 0, px + 2, py + 5);
  b.place("gbr_atgun", 0, px + 2, py);
  b.place("gbr_bunker", 0, px, py - 10);
  b.squad("gbr_rifleman", 0, px, py - 3, 12, 0);
  b.squad("gbr_mg_team", 0, px - 2, py + 3, 3, 0);
  b.place("gbr_heavy_tank", 0, px - 4, py - 6);
  b.place("gbr_heavy_tank", 0, px - 4, py + 6);
  b.place("gbr_artillery", 0, px - 12, py);
  b.place("gbr_engineer", 0, px - 8, py + 2);
  b.place("gbr_engineer", 0, px - 8, py - 2);

  Script s;
  const int ridge = s.area("Point 64", px - 6, py - 12, px + 8, py + 12), east = s.area("East track", 118, 54, 124, 74),
            north = s.area("North wadi", 92, 4, 104, 12);
  const int guns = b.t("gbr_atgun");
  s.on("Briefing", {always()},
       {say(0, "The brigade needs twelve minutes to get clear. Hold Point 64 with what you have: three six-pounders, the infantry "
               "and two Churchills."),
        objective(0, "Hold Point 64 for twelve minutes"), objective(0, "Keep two of the six-pounders", true), reveal(0, ridge)});
  const int w1 = s.on("Wave 1", {timer(90)},
       {spawn(1, b.t("ger_medium_tank"), east, 3), spawn(1, b.t("ger_halftrack"), east, 2), spawn(1, b.t("ger_rifleman"), east, 8),
        attack(1, east, ridge), say(0, "Panzers on the east track!")});
  const int w2 = s.on("Wave 2", {fired(w1, 210)},
       {spawn(1, b.t("ger_medium_tank"), east, 4), spawn(1, b.t("ger_rifleman"), east, 12), spawn(1, b.t("ger_artillery"), east, 1),
        attack(1, east, ridge), spawn(1, b.t("ger_light_tank"), north, 3), attack(1, north, ridge),
        say(0, "More armour, and a flanking column in the north wadi!")});
  s.on("Wave 3", {fired(w2, 210)},
       {spawn(1, b.t("ger_heavy_tank"), east, 2), spawn(1, b.t("ger_medium_tank"), east, 3), spawn(1, b.t("ger_rifleman"), east, 12),
        attack(1, east, ridge), say(0, "Tigers! Hold on, Point 64, the brigade is nearly clear.")});
  s.on("Guns kept", {timer(720), atLeast(0, 2, guns)}, {done(1), setFlag("ridge_guns", 1)});
  s.on("Held", {timer(720), inArea(0, ridge, 1)},
       {done(0), say(0, "The brigade is clear. Fall back, and well done."), win(0)});
  s.on("Ridge lost", {timer(30), noneIn(0, ridge)}, {failed(0), lose(0)});
  return b.save(out, &s.s);
}

bool gbrRaid(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(128, Biome::Desert, static_cast<int>(MapLayout::Steppe), 1952,
                                 rect(6, 54, 20, 74) + rect(60, 20, 70, 30) + rect(100, 60, 110, 70) + rect(70, 96, 80, 106));
  b.start(128, Biome::Desert, seed, static_cast<int>(MapLayout::Steppe), "Operation Lantern",
          "Libya, April 1946. A mobile column strikes at the enemy's supply dumps deep behind the line.");
  b.side(0, "Lantern Force", "gbr", kKhaki, 0, true, 0, 12, 64);
  b.side(1, "Afrika Korps rear area", "ger", kRed, 1, true, 0, 90, 64);
  b.villages({{12, 64, 10}});
  b.squad("gbr_halftrack", 0, 14, 60, 4, 0);
  b.squad("gbr_light_tank", 0, 14, 68, 4, 0);
  b.squad("gbr_scout_car", 0, 10, 64, 2, 0);
  b.squad("gbr_rifleman", 0, 8, 56, 8, 0);
  b.place("gbr_engineer", 0, 8, 72);
  const std::vector<std::pair<int, int>> dumps = {{64, 24}, {104, 64}, {74, 100}};
  for (auto [x, y] : dumps) {
    b.place("ger_office", 1, x, y);  // a logistics office and its trucks: the dump
    b.place("ger_mg_nest", 1, x - 4, y + 3);
    b.squad("ger_rifleman", 1, x + 2, y - 3, 4, 8);
  }
  b.place("ger_bunker", 1, 104 - 6, 64);
  b.place("ger_atgun", 1, 74 + 5, 100 - 4);

  Script s;
  const int a1 = s.area("Dump north", 56, 16, 72, 32), a2 = s.area("Dump east", 96, 56, 112, 72), a3 = s.area("Dump south", 66, 92, 82, 108),
            hq = s.area("Their camp", 116, 100, 124, 110);
  const int depot = b.t("ger_office");
  s.on("Briefing", {always()},
       {say(0, "Lantern Force: three supply dumps, marked on your map. Destroy them all. Speed is everything: once they know "
               "we're here, they'll send their armour."),
        objective(0, "Destroy the northern dump"), objective(0, "Destroy the eastern dump"), objective(0, "Destroy the southern dump"),
        reveal(0, a1), reveal(0, a2), reveal(0, a3)});
  s.on("North", {noneIn(1, a1, depot)}, {done(0)});
  s.on("East", {noneIn(1, a2, depot)}, {done(1)});
  s.on("South", {noneIn(1, a3, depot)}, {done(2)});
  const int alarm = s.on("Alarm", {atMost(1, 2, depot)},
       {say(0, "They've raised the alarm: armour moving out of their camp in the south-east!"),
        spawn(1, b.t("ger_medium_tank"), hq, 3), spawn(1, b.t("ger_halftrack"), hq, 2), attack(1, hq, a2)});
  (void)alarm;
  s.on("Won", {atMost(1, 0, depot)}, {say(0, "All three dumps are burning. Back to our lines!"), setFlag("lantern", 1), win(0)});
  s.on("Lost", {atMost(0, 0)}, {lose(0)});
  return b.save(out, &s.s);
}

bool gbrGap(Builder& b, const std::string& out) {
  const uint64_t seed = findSeed(128, Biome::Urban, static_cast<int>(MapLayout::TwinRivers), 1953,
                                 rect(8, 52, 30, 76) + rect(96, 52, 120, 76));
  b.start(128, Biome::Urban, seed, static_cast<int>(MapLayout::TwinRivers), "The Gap at Hellenthal",
          "Rhineland, October 1946. Force the gap through the town and destroy the German garrison.");
  b.side(0, "7th Armoured Brigade", "gbr", kKhaki, 0, true, 6000, 16, 64);
  b.side(1, "Kampfgruppe Hellenthal", "ger", kRed, 1, false, 9000, 108, 64, 2);
  b.villages({{16, 64, 14}, {108, 64, 18}});
  b.place("gbr_mcv", 0, 16, 64);
  b.squad("gbr_medium_tank", 0, 22, 62, 4, 0);
  b.place("gbr_firefly", 0, 24, 66);
  b.squad("gbr_rifleman", 0, 20, 56, 12, 0);
  b.squad("gbr_mg_team", 0, 18, 70, 2, 0);
  b.squad("gbr_engineer", 0, 14, 72, 2, 0);
  const int gx = 108, gy = 64;
  for (auto [dx, dy, id] : {std::tuple{0, 0, "ger_yard"}, {-6, -6, "ger_power"}, {-6, 6, "ger_power"}, {5, 7, "ger_barracks"},
                            {-1, 12, "ger_factory"}, {-12, -5, "ger_bunker"}, {-12, 5, "ger_atgun"}, {-10, 10, "ger_mg_nest"}})
    b.place(id, 1, gx + dx, gy + dy);
  b.income("ger", 1, gx, gy, 64, 64);
  b.squad("ger_rifleman", 1, gx - 14, gy, 10, 8);

  Script s;
  const int west = s.area("West road", 2, 58, 8, 70);
  s.on("Briefing", {always()},
       {say(0, "Build up, then push through the town. The Germans will counter-attack: dig in as you go."),
        objective(0, "Destroy Kampfgruppe Hellenthal")});
  s.on("Point 64 guns", {flagAtLeast("ridge_guns", 1)},
       {spawn(0, b.t("gbr_artillery"), west, 2), say(0, "The gunners of Point 64 are here with two Sextons.")});
  s.on("Lantern", {flagAtLeast("lantern", 1)},
       {supplies(0, 2500), say(0, "The enemy is still short of fuel after Operation Lantern: their attacks will be slow to "
                                  "come. Supplies: two and a half thousand more.")});
  s.on("Won", {defeated(1)}, {done(0), win(0)});
  s.on("Lost", {defeated(0)}, {lose(0)});
  return b.save(out, &s.s);
}

// ---------------------------------------------------------------------------
// Skirmish maps: one for every landscape, villages placed, named.
// ---------------------------------------------------------------------------
struct SkirmishMap {
  const char* file;
  const char* name;
  const char* description;
  Biome biome;
  MapLayout layout;
  int size;
  uint64_t seed;
};
const SkirmishMap kSkirmish[] = {
    {"river_valley", "Orel Valley", "A slow river down the middle, two fords and a mill town on the bank.", Biome::Temperate,
     MapLayout::RiverValley, 128, 2101},
    {"front_line", "The Narew Line", "A river between the sides with three fords, all of them in range of the other bank.",
     Biome::Temperate, MapLayout::FrontLine, 128, 2102},
    {"confluence", "Two Waters", "Where two rivers meet: a fight for the tongue of land between them.", Biome::Temperate,
     MapLayout::Confluence, 128, 2103},
    {"lakeland", "Masurian Lakes", "Lakes and narrow land between them; every causeway matters.", Biome::Winter,
     MapLayout::Lakeland, 128, 2104},
    {"crossroads", "Kursk Crossroads", "A market town where two roads cross, open fields all round.", Biome::Temperate,
     MapLayout::Crossroads, 128, 2105},
    {"ridge", "Point 112", "High ground across the middle, crossed at passes.", Biome::Temperate, MapLayout::Ridge, 128, 2106},
    {"marshes", "Pripet Marshes", "Shallow pools men can wade and causeway roads for the tanks.", Biome::Temperate,
     MapLayout::Marshes, 128, 2107},
    {"great_lake", "Lake Ilmen", "A great frozen lake in the middle: fight round it.", Biome::Winter, MapLayout::GreatLake, 160,
     2108},
    {"steppe", "Kalmyk Steppe", "Open steppe cut by dry gullies: tank country.", Biome::Desert, MapLayout::Steppe, 160, 2109},
    {"forest", "Hürtgen", "Deep forest, few roads, close fighting.", Biome::Temperate, MapLayout::Forest, 128, 2110},
    {"hill_country", "Ardennes Heights", "Rolling hills and wooded valleys in the snow.", Biome::Winter, MapLayout::HillCountry,
     128, 2111},
    {"twin_rivers", "Rhine and Ruhr", "Two rivers and a ruined industrial town between them.", Biome::Urban,
     MapLayout::TwinRivers, 128, 2112},
};

// Hand-placed hamlets in the contested middle of a skirmish map: something to
// fight over and hide in between the bases.
void hamlets(Builder& b, Biome biome, uint64_t seed) {
  const int W = b.file.map.width();
  std::vector<const char*> houses, centres;
  switch (biome) {
    case Biome::Winter: houses = {"neutral_izba", "neutral_izba", "neutral_barn", "neutral_cottage", "neutral_shed"};
      centres = {"neutral_onion_church", "neutral_grain_silo", "neutral_well"}; break;
    case Biome::Desert: houses = {"neutral_adobe", "neutral_adobe", "neutral_market", "neutral_shed", "neutral_ruin"};
      centres = {"neutral_well", "neutral_villa", "neutral_warehouse"}; break;
    case Biome::Urban: houses = {"neutral_townhouse", "neutral_shopfront", "neutral_apartment", "neutral_ruin", "neutral_cottage_bombed"};
      centres = {"neutral_station", "neutral_town_hall", "neutral_factory", "neutral_terrace"}; break;
    case Biome::Tropical: houses = {"neutral_stilt_hut", "neutral_stilt_hut", "neutral_shed"};
      centres = {"neutral_stilt_hut", "neutral_warehouse"}; break;
    default: houses = {"neutral_cottage", "neutral_half_timber", "neutral_stone_cottage", "neutral_farmhouse", "neutral_barn",
                       "neutral_shed"};
      centres = {"neutral_church", "neutral_tavern", "neutral_manor", "neutral_windmill", "neutral_school"}; break;
  }
  const std::vector<std::pair<int, int>> spots = {{W / 2, W / 2},           {W / 2, W * 30 / 100}, {W / 2, W * 70 / 100},
                                                  {W * 36 / 100, W / 2},    {W * 64 / 100, W / 2}, {W * 33 / 100, W * 22 / 100},
                                                  {W * 67 / 100, W * 78 / 100}, {W * 33 / 100, W * 78 / 100},
                                                  {W * 67 / 100, W * 22 / 100}};
  uint64_t h = seed * 2654435761u + 12345u;
  auto rnd = [&](int n) {
    h = h * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<int>((h >> 33) % static_cast<uint64_t>(n));
  };
  int made = 0;
  for (const auto& [x, y] : spots) {
    if (made >= 5) break;
    // Not on water, not on a steep slope, and not already a village there.
    bool ok = true;
    for (int dy = -3; dy <= 3 && ok; dy += 3)
      for (int dx = -3; dx <= 3 && ok; dx += 3) ok = b.dry(x + dx, y + dy);
    for (const UnitPlacement& u : b.file.units)
      if (std::abs(u.x / T - x) < 10 && std::abs(u.y / T - y) < 10) ok = false;
    if (!ok) continue;
    b.place(centres[static_cast<size_t>(rnd(static_cast<int>(centres.size())))], NEUTRAL_OWNER, x, y, rnd(4) * 4, true);
    const int n = 3 + rnd(4);
    for (int k = 0; k < n; ++k) {
      const float a = 6.2831853f * static_cast<float>(k) / static_cast<float>(n) + static_cast<float>(rnd(100)) * 0.01f;
      const int r = 5 + rnd(3);
      b.place(houses[static_cast<size_t>(rnd(static_cast<int>(houses.size())))], NEUTRAL_OWNER,
              x + static_cast<int>(std::cos(a) * static_cast<float>(r)), y + static_cast<int>(std::sin(a) * static_cast<float>(r)),
              rnd(4) * 4, true);
    }
    ++made;
  }
}

bool skirmishMaps(Builder& b, const std::string& dir) {
  bool ok = true;
  nlohmann::json list = nlohmann::json::array();
  for (const SkirmishMap& m : kSkirmish) {
    b.start(m.size, m.biome, m.seed, static_cast<int>(m.layout), m.name, m.description);
    const int W = m.size;
    // Keep the villages out of the four starting areas (as a random skirmish does).
    b.villages({{W * 17 / 100, W / 2, 11}, {W * 83 / 100, W / 2, 11}, {W / 2, W * 17 / 100, 11}, {W / 2, W * 83 / 100, 11},
                {W * 15 / 100, W * 35 / 100, 10}, {W * 15 / 100, W * 65 / 100, 10}, {W * 85 / 100, W * 35 / 100, 10},
                {W * 85 / 100, W * 65 / 100, 10}});
    hamlets(b, m.biome, m.seed);
    b.file.players.clear();  // the skirmish setup chooses the sides
    ok = b.save(dir + m.file + ".lmap") && ok;
    list.push_back({{"file", std::string(m.file) + ".lmap"}, {"name", m.name}, {"description", m.description},
                    {"size", m.size}, {"biome", biomeId(m.biome)}});
  }
  std::FILE* f = std::fopen((dir + "maps.json").c_str(), "wb");
  if (!f) return false;
  const std::string text = nlohmann::json{{"_comment", "Skirmish maps, in the order the Skirmish page lists them. "
                                                        "Made by tools/make_content.cpp."},
                                          {"maps", list}}
                               .dump(1);
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);
  return ok;
}
}  // namespace

int main(int argc, char** argv) {
  const std::string content = argc > 1 ? argv[1] : "content/";
  const std::string what = argc > 2 ? argv[2] : "all";
  Builder b;
  if (!b.load(content)) return 1;
  bool ok = true;
  auto want = [&](const char* w) { return what == "all" || what == w; };
  if (want("scenarios")) ok = kalach(b, content + "scenarios/Kalach_Crossing.lmap") && ok;
  if (want("tutorial")) {
    const std::string d = content + "campaigns/tutorial/";
    ok = tutorialFirstOrders(b, d + "t1_first_orders.lmap") && ok;
    ok = tutorialCover(b, d + "t2_cover.lmap") && ok;
    ok = tutorialDigIn(b, d + "t3_dig_in.lmap") && ok;
    ok = tutorialBase(b, d + "t4_base.lmap") && ok;
  }
  if (want("campaigns")) {
    ok = kalach(b, content + "campaigns/ger/g1_kalach.lmap") && ok;
    ok = gerIceRoad(b, content + "campaigns/ger/g2_ice_road.lmap") && ok;
    ok = gerDonets(b, content + "campaigns/ger/g3_donets.lmap") && ok;
    ok = rusVillage(b, content + "campaigns/rus/r1_sokolovo.lmap") && ok;
    ok = rusBridgehead(b, content + "campaigns/rus/r2_bridgehead.lmap") && ok;
    ok = rusIronStorm(b, content + "campaigns/rus/r3_iron_storm.lmap") && ok;
    ok = gbrRidge(b, content + "campaigns/gbr/b1_point_64.lmap") && ok;
    ok = gbrRaid(b, content + "campaigns/gbr/b2_lantern.lmap") && ok;
    ok = gbrGap(b, content + "campaigns/gbr/b3_hellenthal.lmap") && ok;
  }
  if (want("skirmish")) ok = skirmishMaps(b, content + "maps/skirmish/") && ok;
  return ok ? 0 : 1;
}

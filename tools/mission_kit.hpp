// Tools for writing missions from code (make_content and the campaign files):
// the map Builder, the trigger shorthand and the Script assembler.
#pragma once
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "slave/map/map_io.hpp"
#include "slave/sim/script.hpp"
#include "slave/sim/simulation.hpp"

namespace mk {
using namespace slave;
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
  // `settlements` false: open country, for missions whose buildings are all placed by hand.
  void start(int size, Biome b, uint64_t seed, int layout, const std::string& name, const std::string& description,
             bool settlements = true) {
    startRect(size, size, b, seed, layout, name, description, settlements);
  }
  // A map that needn't be square: long strips for missions along a road, a coast or a valley.
  void startRect(int w, int h, Biome b, uint64_t seed, int layout, const std::string& name, const std::string& description,
                 bool settlements = true) {
    file = MapFile{};
    file.map.resize(w, h);
    file.map.generate(b, seed, layout, settlements);
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
  // The last unit placed: a name for mission scripts, set-piece flags, a stance.
  void tag(const std::string& name, uint8_t flags = 0, uint8_t stance = 0) {
    if (file.units.empty()) return;
    file.units.back().name = name;
    file.units.back().flags = flags;
    file.units.back().stance = stance;
  }
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
  // --- Placing set pieces --------------------------------------------------
  // A unit the story keeps alive and away from the computer until released.
  bool setPiece(const std::string& id, uint8_t owner, int tx, int ty, const std::string& name, int facing = 0,
                uint8_t stance = 1) {
    if (!place(id, owner, tx, ty, facing)) return false;
    tag(name, kScriptInvulnerable | kScriptNoAi, stance);
    return true;
  }
  // A unit with a name for the script (escorts, heroes, targets).
  bool named(const std::string& id, uint8_t owner, int tx, int ty, const std::string& name, int facing = 0) {
    if (!place(id, owner, tx, ty, facing)) return false;
    tag(name);
    return true;
  }
  void namedSquad(const std::string& id, uint8_t owner, int tx, int ty, int n, int facing, const std::string& name,
                  uint8_t flags = 0, uint8_t stance = 0) {
    for (int i = 0; i < n; ++i)
      if (place(id, owner, tx + (i % 4) * 2 - 3, ty + (i / 4) * 2, facing)) tag(name, flags, stance);
  }

  // --- Shaping the ground (missions placed by hand) ---------------------------
  uint16_t ground(int cx, int cy) const {
    cx = std::clamp(cx, 0, file.map.cornersX() - 1);
    cy = std::clamp(cy, 0, file.map.cornersY() - 1);
    return file.map.corner(cx, cy);
  }
  // Water needs a water level: if the map has none, one just under its lowest ground.
  uint16_t water() {
    if (!file.map.waterLevel()) {
      uint16_t lo = 65535;
      for (int y = 0; y < file.map.cornersY(); ++y)
        for (int x = 0; x < file.map.cornersX(); ++x) lo = std::min(lo, file.map.corner(x, y));
      file.map.setWaterLevel(static_cast<uint16_t>(std::max(40, lo - 10)));
    }
    return file.map.waterLevel();
  }
  // Ground of corner (cx, cy) set to at most `h` (never raised), eased into what is around it.
  void lowerTo(int cx, int cy, int h) {
    if (cx < 0 || cy < 0 || cx >= file.map.cornersX() || cy >= file.map.cornersY()) return;
    if (file.map.corner(cx, cy) > h) file.map.setCorner(cx, cy, static_cast<uint16_t>(std::max(1, h)));
  }
  // A river across the map: along x (east-west) when `alongX`, at `at` (+ a gentle
  // wobble), `half` tiles wide either side, with fords (dry road crossings at half
  // speed) at the listed positions along it. Banks slope down over `bank` tiles.
  void river(bool alongX, int at, int half, std::vector<int> fords = {}, uint32_t seed = 7, int bank = 3, int wobble = 3) {
    const int wl = water();
    const int len = alongX ? file.map.cornersX() : file.map.cornersY();
    for (int i = 0; i < len; ++i) {
      const float w = wobble * std::sin(i * 0.071f + seed) + wobble * 0.5f * std::sin(i * 0.19f + seed * 3);
      const int centre = at + static_cast<int>(std::lround(w));
      bool ford = false;
      for (int f : fords) ford = ford || std::abs(i - f) <= 1;
      for (int d = -(half + bank); d <= half + bank; ++d) {
        const int a = std::abs(d);
        int h;
        if (a <= half) h = ford ? wl + 1 : wl - 40;
        else h = (ford ? wl + 2 : wl - 40) + (a - half) * (ford ? 2 : 16);
        if (ford && a <= half + bank) h = std::min(h, wl + 4);
        const int cx = alongX ? i : centre + d, cy = alongX ? centre + d : i;
        lowerTo(cx, cy, h);
      }
      if (ford)
        for (int d = -(half + bank + 2); d <= half + bank + 2; ++d) {
          const int tx = alongX ? i : centre + d, ty = alongX ? centre + d : i;
          paintRoad(tx, ty, 2);
        }
    }
    finishTerrain();
  }
  void paintRoad(int tx, int ty, int r = 1) {
    for (int sy = (ty - r / 2) * SPLAT_PER_TILE; sy < (ty + 1 + r / 2) * SPLAT_PER_TILE; ++sy)
      for (int sx = (tx - r / 2) * SPLAT_PER_TILE; sx < (tx + 1 + r / 2) * SPLAT_PER_TILE; ++sx)
        if (sx >= 0 && sy >= 0 && sx < file.map.splatW() && sy < file.map.splatH()) file.map.paint(sx, sy, Layer::Road, 255);
  }
  // A road along a polyline of tiles, `w` tiles wide, the ground smoothed under it.
  void road(const std::vector<std::pair<int, int>>& pts, int w = 2) {
    for (size_t k = 1; k < pts.size(); ++k) {
      const auto [x0, y0] = pts[k - 1];
      const auto [x1, y1] = pts[k];
      const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
      for (int i = 0; i <= n; ++i) {
        const int x = x0 + (n ? (x1 - x0) * i / n : 0), y = y0 + (n ? (y1 - y0) * i / n : 0);
        paintRoad(x, y, w);
      }
    }
    finishTerrain();
  }
  // Level ground: every corner in the rectangle at the rectangle's average height.
  void flatten(int x0, int y0, int x1, int y1) {
    long sum = 0, n = 0;
    for (int y = y0; y <= y1 + 1; ++y)
      for (int x = x0; x <= x1 + 1; ++x) {
        sum += ground(x, y);
        ++n;
      }
    const uint16_t h = static_cast<uint16_t>(n ? sum / n : 256);
    for (int y = y0; y <= y1 + 1; ++y)
      for (int x = x0; x <= x1 + 1; ++x)
        if (x >= 0 && y >= 0 && x < file.map.cornersX() && y < file.map.cornersY()) file.map.setCorner(x, y, h);
    finishTerrain();
  }
  // A bridge laid in a straight line of tiles (over the water between them).
  int bridge(int x0, int y0, int x1, int y1, bool wood = false) {
    const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    int laid = 0;
    for (int i = 0; i <= n; ++i) {
      const int x = x0 + (n ? (x1 - x0) * i / n : 0), y = y0 + (n ? (y1 - y0) * i / n : 0);
      if (file.map.inBounds(x, y) && file.map.info(x, y).waterDepth > -8) laid += place(wood ? "neutral_bridge_wood" : "neutral_bridge", NEUTRAL_OWNER, x, y, 0, true);
    }
    return laid;
  }
  void finishTerrain() {
    file.map.recomputeInfo();
    file.map.recomputeSpeeds(0, 0, file.map.width() - 1, file.map.height() - 1);
  }
  // Every generated village plot as a neutral building (no keep-clear).
  void allVillages() { villages({}); }

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

// A seed whose map is dry ground along every listed tile (of a w x h map).
inline uint64_t findSeedRect(int w, int h, Biome b, int layout, uint64_t from, const std::vector<std::pair<int, int>>& mustBeDry,
                      bool settlements = true) {
  for (uint64_t seed = from; seed < from + 4000; ++seed) {
    Map probe(w, h);
    probe.generate(b, seed, layout, settlements);
    bool ok = true;
    for (const auto& [x, y] : mustBeDry)
      ok = ok && probe.inBounds(x, y) && probe.info(x, y).waterDepth < -16 && probe.info(x, y).slope <= 20;
    if (ok) return seed;
  }
  std::fprintf(stderr, "no seed found from %llu\n", static_cast<unsigned long long>(from));
  return from;
}
inline uint64_t findSeed(int size, Biome b, int layout, uint64_t from, const std::vector<std::pair<int, int>>& mustBeDry,
                  bool settlements = true) {
  for (uint64_t seed = from;; ++seed) {
    Map probe(size, size);
    probe.generate(b, seed, layout, settlements);
    bool ok = true;
    for (const auto& [x, y] : mustBeDry) ok = ok && probe.info(x, y).waterDepth < -16 && probe.info(x, y).slope <= 20;
    if (ok) return seed;
  }
}
inline std::vector<std::pair<int, int>> row(int x0, int x1, int y, int step = 2) {
  std::vector<std::pair<int, int>> out;
  for (int x = x0; x <= x1; x += step) out.push_back({x, y});
  return out;
}
inline std::vector<std::pair<int, int>> rect(int x0, int y0, int x1, int y1, int step = 3) {
  std::vector<std::pair<int, int>> out;
  for (int y = y0; y <= y1; y += step)
    for (int x = x0; x <= x1; x += step) out.push_back({x, y});
  return out;
}
inline std::vector<std::pair<int, int>> operator+(std::vector<std::pair<int, int>> a, const std::vector<std::pair<int, int>>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// ---------------------------------------------------------------------------
// Script building blocks
// ---------------------------------------------------------------------------
inline Condition C(CondType type, uint8_t player = 0, int area = -1, int value = 0, int unitType = -1) {
  Condition c;
  c.type = type;
  c.player = player;
  c.area = area;
  c.value = value;
  c.unitType = unitType;
  return c;
}
inline Condition always() { return C(CondType::Always); }
inline Condition fired(int trigger, int seconds = 0) {
  Condition c = C(CondType::TriggerFired, 0, -1, seconds);
  c.trigger = trigger;
  return c;
}
inline Condition timer(int seconds) { return C(CondType::Timer, 0, -1, seconds); }
inline Condition inArea(uint8_t p, int area, int n, int unit = -1) { return C(CondType::UnitsInArea, p, area, n, unit); }
inline Condition noneIn(uint8_t p, int area, int unit = -1) { return C(CondType::NoUnitsInArea, p, area, 0, unit); }
inline Condition atMost(uint8_t p, int n, int unit = -1) { return C(CondType::UnitCount, p, -1, n, unit); }
inline Condition atLeast(uint8_t p, int n, int unit = -1) { return C(CondType::UnitsAtLeast, p, -1, n, unit); }
inline Condition cover(uint8_t p, int n) { return C(CondType::InCover, p, -1, n); }
inline Condition defeated(uint8_t p) { return C(CondType::PlayerDefeated, p); }
inline Condition order(uint8_t p, const char* kind, int n = 1) {
  Condition c = C(CondType::OrderGiven, p, -1, n);
  c.order = orderFromId(kind);
  return c;
}
inline Condition flagAtLeast(const char* name, int v) {
  Condition c = C(CondType::FlagAtLeast, 0, -1, v);
  c.text = name;
  return c;
}

inline Action A(ActionType type, uint8_t player = ANY_PLAYER, int value = 0) {
  Action a;
  a.type = type;
  a.player = player;
  a.value = value;
  return a;
}
inline Action say(uint8_t p, const std::string& text) {
  Action a = A(ActionType::Message, p);
  a.text = text;
  return a;
}
inline Action hint(uint8_t p, const std::string& text) {
  Action a = A(ActionType::Hint, p);
  a.text = text;
  return a;
}
inline Action highlight(uint8_t p, const std::string& what) {
  Action a = A(ActionType::Highlight, p);
  a.text = what;
  return a;
}
inline Action objective(uint8_t p, const std::string& text, bool optional = false) {
  Action a = A(ActionType::AddObjective, p, optional ? 1 : 0);
  a.text = text;
  return a;
}
inline Action markedObjective(uint8_t p, const std::string& text, int area, bool optional = false) {
  Action a = objective(p, text, optional);
  a.area = area;  // shown on the map while open
  return a;
}
inline Action done(int i) { return A(ActionType::CompleteObjective, ANY_PLAYER, i); }
inline Action failed(int i) { return A(ActionType::FailObjective, ANY_PLAYER, i); }
inline Action win(uint8_t p) { return A(ActionType::Victory, p); }
inline Action lose(uint8_t p) { return A(ActionType::Defeat, p); }
inline Action spawn(uint8_t p, int unit, int area, int n) {
  Action a = A(ActionType::SpawnUnits, p, n);
  a.unitType = unit;
  a.area = area;
  return a;
}
inline Action attack(uint8_t p, int from, int to, int unit = -1) {
  Action a = A(ActionType::AttackMove, p);
  a.area = from;
  a.area2 = to;
  a.unitType = unit;
  return a;
}
inline Action reveal(uint8_t p, int area) {
  Action a = A(ActionType::RevealArea, p);
  a.area = area;
  return a;
}
inline Action lock(uint8_t p, int unit) {
  Action a = A(ActionType::LockBuild, p);
  a.unitType = unit;
  return a;
}
inline Action unlock(uint8_t p, int unit) {
  Action a = A(ActionType::UnlockBuild, p);
  a.unitType = unit;
  return a;
}
inline Action supplies(uint8_t p, int n) { return A(ActionType::GiveSupplies, p, n); }
inline Action setFlag(const char* name, int v) {
  Action a = A(ActionType::SetFlag, ANY_PLAYER, v);
  a.text = name;
  return a;
}

// The mission engine's conditions and actions (9.9), all through two helpers.
inline Condition when(CondType type, uint8_t player = 0, int area = -1, int value = 0, const std::string& text = {}, int unit = -1) {
  Condition c = C(type, player, area, value, unit);
  c.text = text;
  return c;
}
inline Condition unless(Condition c) {
  c.negate = true;
  return c;
}
inline Action act(ActionType type, uint8_t player = ANY_PLAYER, int value = 0, const std::string& text = {}, int area = -1,
           int area2 = -1, int unit = -1) {
  Action a = A(type, player, value);
  a.text = text;
  a.area = area;
  a.area2 = area2;
  a.unitType = unit;
  return a;
}
inline Action radio(uint8_t p, const std::string& words, int seconds = 0) { return act(ActionType::Transmission, p, seconds, words); }
inline Action scaledSpawn(uint8_t p, int unit, int area, int n) {
  Action a = spawn(p, unit, area, n);
  a.scaled = true;
  return a;
}
inline Action fire(int trigger) {
  Action a = A(ActionType::FireTrigger);
  a.trigger = trigger;
  return a;
}
inline Action alliance(uint8_t p, uint8_t q, bool allies) {
  Action a = A(ActionType::SetAlliance, p, allies ? 1 : 0);
  a.player2 = q;
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
  // Fires when any one of the conditions holds.
  int any(const std::string& name, std::vector<Condition> c, std::vector<Action> a) {
    const int i = on(name, std::move(c), std::move(a));
    s.triggers[static_cast<size_t>(i)].any = true;
    return i;
  }
  // Fires `seconds` after trigger `after` (and any further conditions hold).
  int then(const std::string& name, int after, int seconds, std::vector<Action> a, std::vector<Condition> more = {}) {
    std::vector<Condition> c{fired(after, seconds)};
    for (Condition& x : more) c.push_back(x);
    return on(name, std::move(c), std::move(a));
  }
  // Objective numbering for this mission (the order they are added).
  int objectives = 0;
  Action goal(uint8_t p, const std::string& text, int area = -1, bool optional = false) {
    ++objectives;
    return markedObjective(p, text, area, optional);
  }
};

constexpr uint32_t kRed = 0xFF2C3AC4, kBlue = 0xFFC46034, kGreen = 0xFF48A042, kKhaki = 0xFF4A86B0, kGrey = 0xFF888888;
// The Germany campaign's sides: the coalition (Free German Army) in amber, its
// allied formations in pale olive; the Reich in deep red, the Party's troops in
// slate; the Soviets in blue; the British in green.
constexpr uint32_t kCoalition = 0xFF40B0E0, kCoalitionAlly = 0xFF70C8B8, kReich = 0xFF2828A8, kParty = 0xFF786868,
                   kSoviet = 0xFFC46034, kBritish = 0xFF48A042;

// Lists of actions joined (braced lists welcome).
inline std::vector<Action> concat(std::vector<Action> a, const std::vector<Action>& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}
inline std::vector<Action> concat(std::vector<Action> a, const std::vector<Action>& b, const std::vector<Action>& c) {
  return concat(concat(std::move(a), b), c);
}
inline std::vector<Action> concat(std::vector<Action> a, const std::vector<Action>& b, const std::vector<Action>& c,
                                  const std::vector<Action>& d) {
  return concat(concat(std::move(a), b, c), d);
}
inline std::vector<Action> concat(std::vector<Action> a, const std::vector<Action>& b, const std::vector<Action>& c,
                                  const std::vector<Action>& d, const std::vector<Action>& e) {
  return concat(concat(std::move(a), b, c, d), e);
}

// More shorthand for story missions.
inline std::vector<Action> release(const std::string& name) {  // a set piece let loose: it can die, the computer leads it
  return {act(ActionType::SetInvulnerable, ANY_PLAYER, 0, name), act(ActionType::SetAiControl, ANY_PLAYER, 1, name)};
}
inline Action playArea(int area) { return act(ActionType::SetPlayArea, ANY_PLAYER, 0, "", area); }
inline Action cinematic(bool on) { return act(ActionType::Cinematic, 0, on ? 1 : 0); }
inline Action pan(int area, int seconds) { return act(ActionType::CameraPan, 0, seconds, "", area); }
inline Action cut(int area) { return act(ActionType::CameraTo, 0, 0, "", area); }
inline Action music(const std::string& what) { return act(ActionType::PlayMusic, 0, 0, what); }
inline Action clock(const std::string& label, int seconds) { return act(ActionType::Countdown, 0, seconds, label); }
inline Action stopClock(const std::string& label) { return act(ActionType::StopCountdown, 0, 0, label); }
inline Action mark(const std::string& label, int area, int kind = 0) { return act(ActionType::MarkArea, 0, kind, label, area); }
inline Action unmark(const std::string& label) { return act(ActionType::ClearMarker, 0, 0, label); }
inline Action aiAttack(uint8_t p, int area) { return act(ActionType::AiAttack, p, 0, "", area); }
inline Action aiDefend(uint8_t p, int area) { return act(ActionType::AiDefend, p, 0, "", area); }
inline Action aiFree(uint8_t p) { return act(ActionType::AiFree, p); }
inline Action aiMay(uint8_t p, const char* what, bool yes) { return act(ActionType::AiBehaviour, p, yes ? 1 : 0, what); }
// A computer side that only does what the story tells it: no building, training,
// raiding, capturing or scouting, and no attacks of its own.
inline std::vector<Action> aiScripted(uint8_t p) {
  return {aiMay(p, "build", false), aiMay(p, "train", false), aiMay(p, "raid", false), aiMay(p, "capture", false),
          aiMay(p, "scout", false), aiMay(p, "attack", false), aiMay(p, "fortify", false)};
}
inline Action barrage(uint8_t p, int area, int shells, const char* weapon = "howitzer_105") {
  return act(ActionType::Barrage, p, shells, weapon, area);
}
inline Action named(ActionType t, const std::string& name, int value = 0, int area2 = -1) {
  return act(t, ANY_PLAYER, value, name, -1, area2);
}
inline Action moveNamed(const std::string& name, int to) { return named(ActionType::MoveUnits, name, 0, to); }
inline Action attackNamed(const std::string& name, int to) { return named(ActionType::AttackMove, name, 0, to); }
inline Action killNamed(const std::string& name) { return named(ActionType::KillUnits, name); }
inline Action toOwner(const std::string& name, uint8_t newOwner) {
  Action a = named(ActionType::ChangeOwner, name);
  a.player2 = newOwner;
  return a;
}
inline Action spawnNamed(uint8_t p, int unit, int area, int n, const std::string& name, bool scaled = false) {
  Action a = act(ActionType::SpawnNamed, p, n, name, area, -1, unit);
  a.scaled = scaled;
  return a;
}
inline Action fromFlag(uint8_t p, int unit, int area, const char* flag, int most = 0) {
  return act(ActionType::SpawnFromFlag, p, most, flag, area, -1, unit);
}
inline Action counter(const std::string& name, int v) { return act(ActionType::SetCounter, ANY_PLAYER, v, name); }
inline Action addFlag(const char* name, int v) { return act(ActionType::AddFlag, ANY_PLAYER, v, name); }
inline Condition holds(uint8_t p, int area, int n = 3) { return when(CondType::AreaControlled, p, area, n); }
inline Condition alive(const std::string& name) { return when(CondType::NamedAlive, 0, -1, 0, name); }
inline Condition dead(const std::string& name) { return when(CondType::NamedDead, 0, -1, 0, name); }
inline Condition namedIn(const std::string& name, int area, int n = 1) { return when(CondType::NamedInArea, 0, area, n, name); }
inline Condition clockDone(const std::string& label) { return when(CondType::CountdownDone, 0, -1, 0, label); }
inline Condition clockBelow(const std::string& label, int s) { return when(CondType::CountdownBelow, 0, -1, s, label); }
inline Condition objDone(int i) { return when(CondType::ObjectiveDone, 0, -1, i); }
inline Condition structuresLeft(uint8_t p, int area, int most, int unit = -1) {
  return when(CondType::StructuresAtMost, p, area, most, {}, unit);
}
inline Condition fewerThan(uint8_t p, int area, int n, int unit = -1) {  // at most n-1 units in area
  return when(CondType::UnitsInAreaAtMost, p, area, n - 1, {}, unit);
}


// The Germany campaign (tools/campaign_ger_*.cpp): each act writes its missions into `dir`.
bool gerAct1(Builder& b, const std::string& dir);
bool gerAct2(Builder& b, const std::string& dir);
bool gerAct3(Builder& b, const std::string& dir);
bool gerAct4(Builder& b, const std::string& dir);
bool gerAct5(Builder& b, const std::string& dir);
// The Soviet campaign (tools/campaign_rus_*.cpp, 9.11).
bool rusAct1(Builder& b, const std::string& dir);
bool rusAct2(Builder& b, const std::string& dir);
bool rusAct3(Builder& b, const std::string& dir);
bool rusAct4(Builder& b, const std::string& dir);
bool rusAct5(Builder& b, const std::string& dir);

}  // namespace mk

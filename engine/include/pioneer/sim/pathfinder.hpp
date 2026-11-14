#pragma once
// Pioneer Engine - pathfinding.
//
// * A* on the tile grid (8 directions, no corner cutting), deterministic.
// * Sectors: the map is cut into 16 x 16 tile sectors. Each sector knows its
//   connected parts per movement type and which parts of its neighbours they
//   touch. From that come the regions (which tiles can reach which: a target
//   across deep water or on a cliff top is replaced by the nearest reachable
//   tile) and, for long routes, a guide: the distance to the goal's part from
//   every other part, through the graph of parts. The tile search steers by
//   it, so it heads for the ford or the gap straight away instead of filling
//   the ground in front of the river first. When buildings go up or down
//   only the sectors touched are worked out again.
// * A route cache: the same trip asked for again (units leaving one door for
//   one rally point, supply trucks on their round) is answered from memory.
//   The cache never changes an answer, and reports the work the search took
//   the first time, so a game plays out the same with a warm cache or a cold
//   one (after loading).
// * Flow fields: for many units bound for one place, one pass over the ground
//   gives every tile its step towards the goal; each unit then reads its way
//   off the field instead of searching.
// * Straight-line checks let units skip A* entirely on open ground, and
//   smooth the zig-zags out of grid paths.

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "pioneer/map/map.hpp"
#include "pioneer/math/vec.hpp"

namespace pioneer {

// Every tile's step towards one goal, for one movement type, over a rectangle
// of the map.
class FlowField {
 public:
  static constexpr uint8_t kGoal = 8, kNone = 255;
  Vec2i goal;              // leptons
  Locomotor loco = Locomotor::Foot;
  int x0 = 0, y0 = 0, w = 0, h = 0;  // the tiles it covers
  uint32_t revision = 0;   // the map revision it was built for
  std::vector<uint8_t> dir;    // 0..7: the neighbour to step to; kGoal; kNone: no way from here
  std::vector<uint32_t> cost;  // cost of the way to the goal from each tile (UINT32_MAX: none)

  bool covers(int tx, int ty) const { return tx >= x0 && ty >= y0 && tx < x0 + w && ty < y0 + h; }
  uint8_t dirAt(int tx, int ty) const { return covers(tx, ty) ? dir[static_cast<size_t>(ty - y0) * w + (tx - x0)] : kNone; }
  uint32_t costAt(int tx, int ty) const {
    return covers(tx, ty) ? cost[static_cast<size_t>(ty - y0) * w + (tx - x0)] : UINT32_MAX;
  }
  // The tile to step to from (tx, ty); false at the goal or where there is no way.
  bool next(int tx, int ty, int& nx, int& ny) const;
};

class Pathfinder {
 public:
  static constexpr int kSectorSize = 16;
  // Routes longer than this many tiles (as the crow flies) are guided by
  // the sector graph; shorter ones search the tiles directly.
  static constexpr int kCorridorFrom = 12;

  explicit Pathfinder(const Map& map) : map_(map) {}

  // Brings sectors and regions up to date if the terrain changed since the last call.
  void refresh();

  bool passable(int tx, int ty, Locomotor loco) const { return map_.passable(tx, ty, loco); }
  uint32_t region(int tx, int ty, Locomotor loco) const;

  // True if a unit can drive in a straight line from a to b (leptons).
  // With minSpeed, the line must also stay on ground at least that fast (percent).
  bool lineClear(Vec2i a, Vec2i b, Locomotor loco, int minSpeed = 0) const;

  // The reachable point nearest to `to` for a unit at `from` (leptons). Returns
  // `to` itself when it is reachable.
  Vec2i nearestReachable(Vec2i from, Vec2i to, Locomotor loco) const;

  // Finds a smoothed path; `out` receives waypoints (tile centres in leptons,
  // the last one being `to` or the nearest reachable point). Returns false if
  // the unit cannot move at all. `maxExpansions` bounds the work; when it runs
  // out the path leads to the closest point found so far.
  bool findPath(Vec2i from, Vec2i to, Locomotor loco, std::vector<Vec2i>& out, int maxExpansions = 200000);

  // The flow field to `goal` over the tile rectangle (clipped to the map; an
  // empty rectangle means the whole map). Kept until the terrain changes or
  // newer fields push it out; the pointer is good until the next call.
  const FlowField* flowField(Vec2i goal, Locomotor loco, int x0 = 0, int y0 = 0, int x1 = -1, int y1 = -1);
  // A unit's way along a flow field, smoothed like a searched path. False
  // when the field has no way from there.
  bool flowPath(const FlowField& field, Vec2i from, std::vector<Vec2i>& out) const;

  // Work the last findPath() took (tile and sector nodes expanded). A cached
  // answer reports what the search cost when it was made.
  uint64_t expansionsLastCall() const { return lastExpansions_; }
  uint64_t searches() const { return searches_; }

  struct Stats {
    uint64_t searches = 0;       // findPath calls that needed a search or the cache
    uint64_t straight = 0;       // answered by a straight line
    uint64_t cacheHits = 0;
    uint64_t corridors = 0;      // long routes guided by the sector graph
    uint64_t sectorRebuilds = 0; // sectors worked out again after terrain changes
    uint64_t refreshes = 0;
    uint64_t flowFields = 0;     // fields built
    uint32_t sectors = 0, parts = 0, links = 0;  // the sector graph now
  };
  const Stats& stats() const { return stats_; }
  // Tests and benchmarks: switch the corridor or the cache off to compare.
  void setCorridors(bool on) { useCorridors_ = on; }
  void setCache(bool on) {
    useCache_ = on;
    cache_.clear();
  }

 private:
  struct Part {  // a connected piece of a sector
    int32_t sumX = 0, sumY = 0;  // tile coordinates summed (centre = sum / tiles)
    uint32_t tiles = 0;
    uint32_t costSum = 0;        // sum over its tiles of 100 * 100 / speed
  };
  struct Link {
    uint8_t from;  // part of this sector (1-based)
    uint32_t to;   // part id of a neighbouring sector
  };
  struct Sector {
    uint64_t hash = 0;
    bool built = false;
    std::array<std::vector<Part>, 3> parts;
    std::array<std::vector<Link>, 3> links;
  };
  struct CacheKey {
    uint32_t start;
    Vec2i goal;
    uint8_t loco;
    bool operator==(const CacheKey&) const = default;
  };
  struct CacheHash {
    size_t operator()(const CacheKey& k) const {
      uint64_t h = k.start * 0x9E3779B97F4A7C15ull;
      h ^= (static_cast<uint64_t>(static_cast<uint32_t>(k.goal.x)) << 32 | static_cast<uint32_t>(k.goal.y)) + (h << 6) + (h >> 2);
      return static_cast<size_t>(h ^ k.loco);
    }
  };
  struct CacheEntry {
    std::vector<Vec2i> tiles;  // before smoothing (that depends on where exactly the unit stands)
    uint64_t expansions = 0;
    bool found = false;
  };

  int sectorOf(int tx, int ty) const { return (ty / kSectorSize) * sw_ + tx / kSectorSize; }
  uint32_t partAt(int tx, int ty, Locomotor loco) const;  // 0: impassable
  uint64_t sectorHash(int sx, int sy) const;
  void buildSector(int sx, int sy);
  void linkSector(int sx, int sy);
  void markEdges(int sx, int sy);
  void buildRegions();
  struct Guide {  // the way to one part from every other part of the map
    uint32_t goalPart = 0;
    Locomotor loco = Locomotor::Foot;
    uint32_t settled = 0;        // parts it reaches (the work it took)
    std::vector<uint32_t> dist;  // per part id: distance to the goal part through the parts' middles (10 per tile)
    std::vector<uint32_t> next;  // ...and the part to go to next
  };
  const Guide& guideTo(uint32_t goalPart, Locomotor loco);
  Vec2i partCentre(uint32_t part, Locomotor loco) const;
  bool search(int sx, int sy, int gx, int gy, Locomotor loco, const Guide* guide, int maxExpansions, std::vector<Vec2i>& tiles,
              bool& reached);
  void smooth(Vec2i from, std::vector<Vec2i>& path, Locomotor loco) const;

  const Map& map_;
  uint32_t builtRevision_ = UINT32_MAX;
  int builtW_ = 0, builtH_ = 0;
  int sw_ = 0, sh_ = 0;
  std::vector<Sector> sectors_;
  std::array<std::vector<uint8_t>, 3> local_;      // per tile: its part within its sector (0: impassable)
  std::array<std::vector<uint32_t>, 3> regionOf_;  // per part id: region label
  // Tiles next to something impassable. Paths pay a little extra to cross
  // them, so routes keep off riverbanks, cliff edges and building corners
  // where units snag.
  std::array<std::vector<uint8_t>, 3> edge_;

  // A* scratch arrays, reused between searches (generation-stamped).
  std::vector<uint32_t> gScore_, stamp_, parent_;
  std::vector<uint8_t> closed_;
  uint32_t generation_ = 0;
  std::vector<std::unique_ptr<Guide>> guides_;  // most recently used first
  uint64_t lastExpansions_ = 0, searches_ = 0;
  bool useCorridors_ = true, useCache_ = true;
  std::unordered_map<CacheKey, CacheEntry, CacheHash> cache_;
  std::vector<std::unique_ptr<FlowField>> fields_;  // most recently used first
  Stats stats_;
};

}  // namespace pioneer

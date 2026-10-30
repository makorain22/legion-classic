#pragma once
// Slave Engine - pathfinding.
//
// * A* on the tile grid (8 directions, no corner cutting), deterministic.
// * Regions: every tile is labelled with the connected area it belongs to,
//   per movement type. A target in another region (across deep water, on a
//   cliff top) is replaced by the nearest reachable tile, so a search never
//   floods a 1024 x 1024 map looking for something it can't reach.
// * Straight-line checks let units skip A* entirely on open ground, and
//   smooth the zig-zags out of grid paths.
// Regions rebuild automatically when the map's terrain revision changes.

#include <array>
#include <cstdint>
#include <vector>

#include "slave/map/map.hpp"
#include "slave/math/vec.hpp"

namespace slave {

class Pathfinder {
 public:
  explicit Pathfinder(const Map& map) : map_(map) {}

  // Rebuilds region labels if the terrain changed since the last call.
  void refresh();

  bool passable(int tx, int ty, Locomotor loco) const { return map_.passable(tx, ty, loco); }
  uint32_t region(int tx, int ty, Locomotor loco) const;

  // True if a unit can drive in a straight line from a to b (leptons).
  bool lineClear(Vec2i a, Vec2i b, Locomotor loco) const;

  // The reachable point nearest to `to` for a unit at `from` (leptons). Returns
  // `to` itself when it is reachable.
  Vec2i nearestReachable(Vec2i from, Vec2i to, Locomotor loco) const;

  // Finds a smoothed path; `out` receives waypoints (tile centres in leptons,
  // the last one being `to` or the nearest reachable point). Returns false if
  // the unit cannot move at all. `maxExpansions` bounds the work; when it runs
  // out the path leads to the closest point found so far.
  bool findPath(Vec2i from, Vec2i to, Locomotor loco, std::vector<Vec2i>& out, int maxExpansions = 200000);

  uint64_t expansionsLastCall() const { return lastExpansions_; }
  uint64_t searches() const { return searches_; }

 private:
  void buildRegions();
  void smooth(Vec2i from, std::vector<Vec2i>& path, Locomotor loco) const;

  const Map& map_;
  uint32_t builtRevision_ = UINT32_MAX;
  int builtW_ = 0, builtH_ = 0;
  std::array<std::vector<uint32_t>, 3> regions_;

  // A* scratch arrays, reused between searches (generation-stamped).
  std::vector<uint32_t> gScore_, stamp_, parent_;
  std::vector<uint8_t> closed_;
  uint32_t generation_ = 0;
  uint64_t lastExpansions_ = 0, searches_ = 0;
};

}  // namespace slave

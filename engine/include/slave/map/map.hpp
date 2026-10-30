#pragma once
// Slave Engine - the tile map.
// Milestone 1: flat terrain with types and variants, plus decorative props.
// Milestone 2 adds heights, cliffs, water, overlays and file save/load.

#include <cstdint>
#include <vector>

#include "slave/core/fixed.hpp"

namespace slave {

enum class Terrain : uint8_t { Grass = 0, Meadow, Dirt, Count };

struct Tile {
  Terrain terrain = Terrain::Grass;
  uint8_t variant = 0;  // which of the terrain's tile images to draw
  uint8_t height = 0;   // Milestone 2
  uint8_t flags = 0;
};

enum class PropKind : uint8_t { Tree = 0, Pine, Bush, Rock, Count };

// A decorative map object (tree, rock). Positions are in leptons.
struct Prop {
  PropKind kind = PropKind::Tree;
  uint8_t variant = 0;
  int32_t x = 0, y = 0;
};

class Map {
 public:
  Map() = default;
  Map(int width, int height) { resize(width, height); }

  void resize(int width, int height);

  int width() const { return width_; }
  int height() const { return height_; }
  int32_t widthLeptons() const { return width_ * LEPTONS_PER_TILE; }
  int32_t heightLeptons() const { return height_ * LEPTONS_PER_TILE; }

  bool inBounds(int tx, int ty) const { return tx >= 0 && ty >= 0 && tx < width_ && ty < height_; }
  Tile& at(int tx, int ty) { return tiles_[static_cast<size_t>(ty) * width_ + tx]; }
  const Tile& at(int tx, int ty) const { return tiles_[static_cast<size_t>(ty) * width_ + tx]; }

  std::vector<Prop>& props() { return props_; }
  const std::vector<Prop>& props() const { return props_; }

  // Random green landscape with meadows, dirt patches and woods.
  void generateCountryside(uint64_t seed);

 private:
  int width_ = 0, height_ = 0;
  std::vector<Tile> tiles_;
  std::vector<Prop> props_;
};

}  // namespace slave

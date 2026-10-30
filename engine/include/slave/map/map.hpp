#pragma once
// Slave Engine - the map.
//
// Terrain is stored the way the renderer and the simulation both need it:
//   * Heights at tile corners, (w+1) x (h+1), in height units (64 per tile).
//   * Ground layers as a "splat map": 2 x 2 texels per tile, each texel
//     holding a weight (0..255) for each of the 8 layers. The renderer blends
//     the layer textures with these weights, so there is no visible tile grid.
//   * A water level: water fills everything below it.
//   * Derived per-tile info (slope, water depth) for movement rules.
//   * Props: trees, rocks and other decorative objects.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "slave/core/fixed.hpp"
#include "slave/sim/unit_types.hpp"

namespace slave {

constexpr int32_t HEIGHT_UNITS_PER_TILE = 64;
constexpr int SPLAT_PER_TILE = 2;  // splat texels per tile, per axis
constexpr int MAP_MIN_SIZE = 32;
constexpr int MAP_MAX_SIZE = 1024;
constexpr int MAP_SIZE_STEP = 32;  // map sizes are multiples of the render chunk

enum class Layer : uint8_t { Grass = 0, DryGrass, Dirt, Mud, Sand, Rock, Snow, Road, Count };
constexpr int LAYER_COUNT = static_cast<int>(Layer::Count);
const char* layerName(Layer l);

enum class PropKind : uint8_t { Tree = 0, Pine, Bush, Rock, Count };
const char* propKindName(PropKind k);

// A decorative map object (tree, rock). Positions are in leptons.
struct Prop {
  PropKind kind = PropKind::Tree;
  uint8_t variant = 0;
  int32_t x = 0, y = 0;
};

// Derived per-tile data, refreshed whenever the terrain changes.
struct TileInfo {
  uint16_t slope = 0;     // max - min corner height, in height units
  int16_t waterDepth = 0; // water level - average height (positive = under water)
};

// Movement rules (Milestone 3 moves these to data files per unit type).
struct TerrainRules {
  uint16_t maxSlope[3] = {40, 22, 30};  // per Locomotor: foot, wheeled, tracked
  int16_t maxWaterDepth[3] = {12, 0, 4};
};

class Map {
 public:
  Map() = default;
  Map(int width, int height, uint16_t baseHeight = 256) { resize(width, height, baseHeight); }

  // Resets to a flat grass map. Sizes are rounded to multiples of 32.
  void resize(int width, int height, uint16_t baseHeight = 256);
  static int roundSize(int s);

  int width() const { return width_; }
  int height() const { return height_; }
  int32_t widthLeptons() const { return width_ * LEPTONS_PER_TILE; }
  int32_t heightLeptons() const { return height_ * LEPTONS_PER_TILE; }
  bool inBounds(int tx, int ty) const { return tx >= 0 && ty >= 0 && tx < width_ && ty < height_; }

  // --- Heights (corners) --------------------------------------------------
  int cornersX() const { return width_ + 1; }
  int cornersY() const { return height_ + 1; }
  uint16_t corner(int cx, int cy) const { return heights_[static_cast<size_t>(cy) * cornersX() + cx]; }
  void setCorner(int cx, int cy, uint16_t h) { heights_[static_cast<size_t>(cy) * cornersX() + cx] = h; }
  std::vector<uint16_t>& heights() { return heights_; }
  const std::vector<uint16_t>& heights() const { return heights_; }
  uint16_t maxHeight() const;

  // Height under a point, in height units x 256 (sub-unit precision). Deterministic.
  int32_t heightAtFine(int32_t lx, int32_t ly) const;
  // Same, as a float in height units (for rendering only).
  float heightAt(float lx, float ly) const;

  // --- Ground layers (splat) ----------------------------------------------
  int splatW() const { return width_ * SPLAT_PER_TILE; }
  int splatH() const { return height_ * SPLAT_PER_TILE; }
  uint8_t* splat(int sx, int sy) { return &splat_[(static_cast<size_t>(sy) * splatW() + sx) * LAYER_COUNT]; }
  const uint8_t* splat(int sx, int sy) const {
    return &splat_[(static_cast<size_t>(sy) * splatW() + sx) * LAYER_COUNT];
  }
  std::vector<uint8_t>& splatData() { return splat_; }
  const std::vector<uint8_t>& splatData() const { return splat_; }
  // Adds `amount` (0..255) of a layer to a texel; other layers shrink so the total stays 255.
  void paint(int sx, int sy, Layer layer, int amount);
  void fillLayer(Layer layer);
  Layer dominantLayer(int sx, int sy) const;

  // --- Water --------------------------------------------------------------
  uint16_t waterLevel() const { return waterLevel_; }  // 0 = no water
  void setWaterLevel(uint16_t w) { waterLevel_ = w; }

  // --- Derived info and movement -----------------------------------------
  const TileInfo& info(int tx, int ty) const { return info_[static_cast<size_t>(ty) * width_ + tx]; }
  void recomputeInfo() { recomputeInfo(0, 0, width_ - 1, height_ - 1); }
  void recomputeInfo(int tx0, int ty0, int tx1, int ty1);  // inclusive tile range
  bool passable(int tx, int ty, Locomotor loco) const;
  TerrainRules& rules() { return rules_; }
  // Increases whenever terrain passability may have changed (editor, load).
  uint32_t revision() const { return revision_; }

  // --- Buildings and fortifications ----------------------------------------
  // Per tile: bit n set = impassable for Locomotor n; kOccupied = a structure
  // stands here (nothing else may be built on it). Owned by the simulation:
  // never saved with the map, rebuilt from the buildings on load.
  static constexpr uint8_t kOccupied = 0x80;
  uint8_t block(int tx, int ty) const { return blocks_[static_cast<size_t>(ty) * width_ + tx]; }
  void setBlock(int tx, int ty, uint8_t bits) {
    blocks_[static_cast<size_t>(ty) * width_ + tx] = bits;
    ++revision_;
  }
  void clearBlocks() {
    std::fill(blocks_.begin(), blocks_.end(), uint8_t{0});
    ++revision_;
  }
  // Ground a building could stand on: dry, gently sloped, unoccupied.
  bool buildable(int tx, int ty) const;

  // --- Oil (the resource) ----------------------------------------------------
  // Units of crude oil in the ground per tile, 0..255. Supply trucks pump it
  // out; a tile is used up when it reaches 0.
  uint8_t oil(int tx, int ty) const { return oil_[static_cast<size_t>(ty) * width_ + tx]; }
  void setOil(int tx, int ty, uint8_t v) { oil_[static_cast<size_t>(ty) * width_ + tx] = v; }
  std::vector<uint8_t>& oilData() { return oil_; }
  const std::vector<uint8_t>& oilData() const { return oil_; }
  // Adds a natural-looking oil field: richest in the middle, ragged edges.
  void addOilField(int cx, int cy, int radius, uint32_t seed);

  // --- Props ---------------------------------------------------------------
  std::vector<Prop>& props() { return props_; }
  const std::vector<Prop>& props() const { return props_; }

  // --- Metadata --------------------------------------------------------------
  std::string name = "Untitled";
  std::string author;
  std::string description;

  // Random landscape: gentle hills, a river, dirt roads, fields and woods.
  void generateCountryside(uint64_t seed);

 private:
  int width_ = 0, height_ = 0;
  std::vector<uint16_t> heights_;
  std::vector<uint8_t> splat_;
  std::vector<TileInfo> info_;
  std::vector<uint8_t> blocks_;
  std::vector<uint8_t> oil_;
  std::vector<Prop> props_;
  uint16_t waterLevel_ = 0;
  TerrainRules rules_;
  uint32_t revision_ = 0;
};

}  // namespace slave

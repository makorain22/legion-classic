#pragma once
// Slave Engine - voxel models and the voxel baker.
//
// Units and buildings are built as small 3D grids of coloured cubes. The baker
// renders them from the isometric camera into pixel-art sprites: fixed light
// from the upper left, three-tone shading, a dark outline, a soft drop shadow
// and team-colour marking. This is how Westwood made vehicle art for Tiberian
// Sun and Red Alert 2.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "slave/assets/image.hpp"

namespace slave {

struct VoxelPalette {
  // Index 0 is always empty space.
  std::array<Rgba, 256> color{};
  // For team-colour entries: brightness relative to the team colour (1 = exact).
  std::array<float, 256> teamLevel{};
  std::array<bool, 256> isTeam{};

  void set(uint8_t index, Rgba c) { color[index] = c; isTeam[index] = false; }
  void setTeam(uint8_t index, float level) { isTeam[index] = true; teamLevel[index] = level; }
};

class VoxelModel {
 public:
  VoxelModel() = default;
  VoxelModel(int sx, int sy, int sz)
      : sx_(sx), sy_(sy), sz_(sz), cells_(static_cast<size_t>(sx) * sy * sz, 0) {}

  int sizeX() const { return sx_; }
  int sizeY() const { return sy_; }
  int sizeZ() const { return sz_; }

  uint8_t get(int x, int y, int z) const {
    if (x < 0 || y < 0 || z < 0 || x >= sx_ || y >= sy_ || z >= sz_) return 0;
    return cells_[index(x, y, z)];
  }
  void set(int x, int y, int z, uint8_t c) {
    if (x < 0 || y < 0 || z < 0 || x >= sx_ || y >= sy_ || z >= sz_) return;
    cells_[index(x, y, z)] = c;
  }
  // Fills the box [x0,x1) x [y0,y1) x [z0,z1).
  void box(int x0, int y0, int z0, int x1, int y1, int z1, uint8_t c) {
    for (int z = z0; z < z1; ++z)
      for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) set(x, y, z, c);
  }
  // Fills an ellipsoid centred at (cx,cy,cz) with radii (rx,ry,rz).
  void ellipsoid(float cx, float cy, float cz, float rx, float ry, float rz, uint8_t c);

 private:
  size_t index(int x, int y, int z) const {
    return (static_cast<size_t>(z) * sy_ + y) * sx_ + x;
  }
  int sx_ = 0, sy_ = 0, sz_ = 0;
  std::vector<uint8_t> cells_;
};

struct BakeOptions {
  float voxelTiles = 1.0f / 16.0f;  // size of one voxel, in tiles
  float yawRadians = 0.0f;          // 0 = model's +X points along world +X
  float pivotX = 0, pivotY = 0;     // rotation centre in voxel units (ground level)
  bool shadow = true;
  bool outline = true;      // dark 1-pixel outline (only without supersampling)
  bool quantize = true;     // snap colours to 32 levels per channel (retro look)
  int supersample = 1;      // 2-4: render larger and average down for soft edges
  const VoxelModel* shadowModel = nullptr;  // cast the shadow from this model instead (same frame)
  uint8_t shadowAlpha = 96;
  float ao = 0.0f;     // 0..1: darken faces crowded by neighbouring voxels (crevices, under fenders)
  float grain = 0.0f;  // 0..1: per-voxel brightness noise (worn paint, cloth, earth)
};

struct BakedSprite {
  Image image;
  int anchorX = 0, anchorY = 0;  // pixel in `image` where the pivot touches the ground
};

BakedSprite bakeVoxels(const VoxelModel& model, const VoxelPalette& palette, const BakeOptions& opt);

// A voxel model as the 3D view draws it: the model, its colours, and how the
// baker placed it (size of a voxel, pivot, shading). The turn is not part of
// it: the 16 facings of a unit share one source.
struct VoxelSource {
  VoxelModel model;
  VoxelPalette palette;
  float voxelTiles = 1.0f / 16.0f;
  float pivotX = 0, pivotY = 0;
  float ao = 0, grain = 0;
  bool castsShadow = true;
};
using VoxelSourcePtr = std::shared_ptr<const VoxelSource>;

// Makes sources, sharing one when the same model is baked again in a row
// (the facings of a unit, a structure's turret).
class VoxelSourceCache {
 public:
  VoxelSourcePtr get(const VoxelModel& model, const VoxelPalette& palette, const BakeOptions& opt);

 private:
  VoxelSourcePtr last_;
};

}  // namespace slave

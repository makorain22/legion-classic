#include "pioneer/render/rock_faces.hpp"

#include <algorithm>
#include <cmath>

namespace pioneer {

namespace {

uint32_t hash3(int x, int y, int z) {
  uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(y) * 19349663u ^ static_cast<uint32_t>(z) * 83492791u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return h;
}

// Stone in bands (1..4) and the cap on top (5), per climate.
VoxelPalette rockPalette(Biome b) {
  VoxelPalette p;
  switch (b) {
    case Biome::Winter:
      p.set(1, {118, 120, 126, 255});
      p.set(2, {104, 106, 112, 255});
      p.set(3, {132, 133, 138, 255});
      p.set(4, {92, 94, 100, 255});
      p.set(5, {232, 236, 242, 255});  // snow
      break;
    case Biome::Desert:
      p.set(1, {178, 140, 98, 255});
      p.set(2, {160, 122, 84, 255});
      p.set(3, {192, 156, 110, 255});
      p.set(4, {142, 108, 76, 255});
      p.set(5, {206, 180, 132, 255});  // sand
      break;
    case Biome::Tropical:
      p.set(1, {96, 92, 82, 255});
      p.set(2, {84, 80, 72, 255});
      p.set(3, {108, 104, 92, 255});
      p.set(4, {74, 72, 64, 255});
      p.set(5, {72, 98, 48, 255});  // moss
      break;
    default:
      p.set(1, {112, 106, 96, 255});
      p.set(2, {98, 93, 85, 255});
      p.set(3, {126, 119, 107, 255});
      p.set(4, {88, 84, 77, 255});
      p.set(5, {92, 106, 60, 255});  // turf
      break;
  }
  return p;
}

}  // namespace

RockMesh buildRockFaces(const Map& m, int tx0, int ty0, int tx1, int ty1) {
  RockMesh out;
  tx0 = std::max(0, tx0);
  ty0 = std::max(0, ty0);
  tx1 = std::min(m.width() - 1, tx1);
  ty1 = std::min(m.height() - 1, ty1);
  int bx0 = tx1 + 1, by0 = ty1 + 1, bx1 = -1, by1 = -1;
  for (int ty = ty0; ty <= ty1; ++ty)
    for (int tx = tx0; tx <= tx1; ++tx)
      if (isCliff(m, tx, ty)) {
        bx0 = std::min(bx0, tx);
        by0 = std::min(by0, ty);
        bx1 = std::max(bx1, tx);
        by1 = std::max(by1, ty);
      }
  if (bx1 < 0) return out;

  constexpr int V = kRockVoxelsPerTile;
  constexpr float kUnitsPerVoxel = static_cast<float>(HEIGHT_UNITS_PER_TILE) / V;
  // Columns cover the steep tiles and one tile round them (the rock's edge
  // follows the steepness, not the tile grid); stay inside this range so
  // neighbouring chunks don't overlap.
  const int cx0 = std::max(tx0, bx0 - 1) * V, cy0 = std::max(ty0, by0 - 1) * V;
  const int cx1 = (std::min(tx1, bx1 + 1) + 1) * V, cy1 = (std::min(ty1, by1 + 1) + 1) * V;  // exclusive
  const int nx = cx1 - cx0, ny = cy1 - cy0;
  auto surface = [&](float wx, float wy) {  // voxels, at a point in tiles
    wx = std::clamp(wx, 0.0f, static_cast<float>(m.width()) - 0.001f);
    wy = std::clamp(wy, 0.0f, static_cast<float>(m.height()) - 0.001f);
    return m.heightAt(wx * LEPTONS_PER_TILE, wy * LEPTONS_PER_TILE) / kUnitsPerVoxel;
  };
  std::vector<float> surf(static_cast<size_t>(nx) * ny);
  std::vector<int> top(static_cast<size_t>(nx) * ny, -1);  // -1: no rock in this column
  float zLo = 1e9f, zHi = -1e9f;
  for (int y = 0; y < ny; ++y)
    for (int x = 0; x < nx; ++x) {
      const float wx = (cx0 + x + 0.5f) / V, wy = (cy0 + y + 0.5f) / V;
      const float s = surface(wx, wy);
      surf[static_cast<size_t>(y) * nx + x] = s;
      // Rock where the ground is steep here and a cliff tile is close.
      const int tx = static_cast<int>(wx), ty = static_cast<int>(wy);
      bool near = false;
      for (int oy = -1; oy <= 1 && !near; ++oy)
        for (int ox = -1; ox <= 1 && !near; ++ox) near = isCliff(m, tx + ox, ty + oy);
      if (!near) continue;
      const float gx = (surface(wx + 0.25f, wy) - surface(wx - 0.25f, wy)) * 2.0f * kUnitsPerVoxel;
      const float gy = (surface(wx, wy + 0.25f) - surface(wx, wy - 0.25f)) * 2.0f * kUnitsPerVoxel;
      const float grade = std::sqrt(gx * gx + gy * gy);  // height units per tile
      if (grade < 34.0f && !isCliff(m, tx, ty)) continue;
      if (grade < 22.0f) continue;
      const int X = cx0 + x, Y = cy0 + y;
      const int t = static_cast<int>(std::floor(s + (hash3(X, Y, 7) & 3) * 0.5f));  // a ragged top
      top[static_cast<size_t>(y) * nx + x] = t;
      zLo = std::min(zLo, s);
      zHi = std::max(zHi, static_cast<float>(t));
    }
  if (zHi < zLo) return out;
  const int base = static_cast<int>(std::floor(zLo)) - 6;  // model z 0; voxels start 3 above (no ground shading)
  VoxelModel vm(nx, ny, static_cast<int>(std::ceil(zHi)) - base + 3);
  for (int y = 0; y < ny; ++y)
    for (int x = 0; x < nx; ++x) {
      const int t = top[static_cast<size_t>(y) * nx + x];
      if (t < 0) continue;
      const int X = cx0 + x, Y = cy0 + y;
      const int bottom = static_cast<int>(std::floor(surf[static_cast<size_t>(y) * nx + x])) - 3;
      // Is this column on the crest? (Nothing near it stands higher.)
      const float own = surf[static_cast<size_t>(y) * nx + x];
      float highest = own;
      for (int oy = -3; oy <= 3; ++oy)
        for (int ox = -3; ox <= 3; ++ox) {
          const int xx = x + ox, yy = y + oy;
          if (xx < 0 || yy < 0 || xx >= nx || yy >= ny) continue;
          highest = std::max(highest, surf[static_cast<size_t>(yy) * nx + xx]);
        }
      for (int z = bottom; z <= t; ++z) {
        const uint32_t h = hash3(X / 3, Y / 3, z / 2);
        uint8_t c = static_cast<uint8_t>(1 + ((z / 2 + (h & 1)) % 4));  // bands of stone
        if (z == t && highest - own <= 0.6f) c = 5;                       // the cap on the crest
        vm.set(x, y, z - base, c);
      }
      // A ledge: where the face drops away below a higher neighbour, the
      // stone above juts out over it, with air beneath.
      int ledge = t;
      for (int oy = -2; oy <= 2; ++oy)
        for (int ox = -2; ox <= 2; ++ox) {
          const int xx = x + ox, yy = y + oy;
          if (xx < 0 || yy < 0 || xx >= nx || yy >= ny) continue;
          ledge = std::max(ledge, top[static_cast<size_t>(yy) * nx + xx]);
        }
      if (ledge - t >= 4 && hash3(X / 2, Y / 2, 3) % 3 != 0) {
        for (int z = ledge - 2; z <= ledge - 1; ++z) vm.set(x, y, z - base, static_cast<uint8_t>(1 + (z / 2) % 4));
      }
    }
  out.verts = meshVoxels(vm, rockPalette(m.biome));
  out.origin = {static_cast<float>(cx0) / V, static_cast<float>(cy0) / V, base / static_cast<float>(V)};
  return out;
}

}  // namespace pioneer

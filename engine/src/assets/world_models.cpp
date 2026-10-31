// Slave Engine - the world's furniture: trees and rocks for every biome, and
// the civilian houses, churches and ruins of villages and towns.
//
// Props: one voxel = 1/32 tile. Buildings: one voxel = 1/12 tile, so a
// footprint of W x H tiles is a 12W x 12H grid.

#include <algorithm>
#include <cmath>

#include "art_common.hpp"
#include "slave/core/rng.hpp"

namespace slave::art {

namespace {

float hashf(uint32_t a, uint32_t b) { return (hash32(a * 2654435761u ^ (b + 0x9e3779b9u)) & 0xFFFF) / 65535.0f; }

void blob(VoxelModel& m, float cx, float cy, float cz, float rx, float ry, float rz, uint8_t c) {
  m.ellipsoid(cx, cy, cz, rx, ry, rz, c);
}

// Snow settles on every upward face that isn't sheltered.
void snowCap(VoxelModel& m, uint32_t seed, int pct) {
  const VoxelModel src = m;
  for (int z = 0; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x) {
        if (!src.get(x, y, z) || src.get(x, y, z + 1)) continue;
        const uint32_t h = hash32(static_cast<uint32_t>((z * 131 + y) * 97 + x) ^ seed) % 100;
        if (h < static_cast<uint32_t>(pct)) m.set(x, y, z, h < static_cast<uint32_t>(pct / 4) ? SNOW_SHADE : SNOW);
      }
}

}  // namespace

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------
void bar3(VoxelModel& m, float x0, float y0, float z0, float x1, float y1, float z1, float r, uint8_t c) {
  const float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
  const int steps = std::max(1, static_cast<int>(len * 2.5f));
  for (int i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / steps;
    const float cx = x0 + (x1 - x0) * t, cy = y0 + (y1 - y0) * t, cz = z0 + (z1 - z0) * t;
    const float rr = std::max(r, 0.5f);
    for (int z = static_cast<int>(cz - rr - 1); z <= static_cast<int>(cz + rr + 1); ++z)
      for (int y = static_cast<int>(cy - rr - 1); y <= static_cast<int>(cy + rr + 1); ++y)
        for (int x = static_cast<int>(cx - rr - 1); x <= static_cast<int>(cx + rr + 1); ++x) {
          const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, dz = z + 0.5f - cz;
          if (dx * dx + dy * dy + dz * dz <= rr * rr) m.set(x, y, z, c);
        }
  }
}

VoxelModel makeSnowPine(int v) {
  VoxelModel m(30, 30, 62);
  m.box(14, 14, 0, 16, 16, 12, TRUNK);
  const int top = 54 + (v % 3) * 3;
  for (int z = 8; z < top; ++z) {
    float r = 1.2f + (top - z) * 0.25f;
    if (z % 6 < 2) r *= 0.75f;  // tiered boughs
    for (int y = 0; y < 30; ++y)
      for (int x = 0; x < 30; ++x) {
        const float dx = x + 0.5f - 15.0f, dy = y + 0.5f - 15.0f;
        const float jag = (hash2(x, y, static_cast<uint32_t>(z + v * 101)) % 100) / 100.0f * 1.3f;
        if (dx * dx + dy * dy <= (r + jag - 0.6f) * (r + jag - 0.6f)) m.set(x, y, z, PINE_DARK);
      }
  }
  speckle(m, PINE_DARK, PINE, LEAF_DARK, 91u + v, 30, 6);
  snowCap(m, 300u + v, 82);
  return m;
}

VoxelModel makeDeadTree(int v) {
  VoxelModel m(34, 34, 50);
  const float lean = (v % 2 ? 1.0f : -1.0f) * 1.5f;
  const float top = 34.0f + (v % 3) * 4.0f;
  bar3(m, 17, 17, 0, 17 + lean, 17, top, 1.4f, DEAD_WOOD);
  for (int i = 0; i < 5 + v % 3; ++i) {
    const float z = 14.0f + i * 4.0f;
    const float a = hashf(v * 17 + i, 3) * kTwoPi;
    const float len = 5.0f + hashf(v, i) * 7.0f;
    bar3(m, 17 + lean * z / top, 17, z, 17 + std::cos(a) * len, 17 + std::sin(a) * len, z + 3 + len * 0.5f, 0.6f,
         DEAD_WOOD);
  }
  speckle(m, DEAD_WOOD, TRUNK, SOOT, 55u + v, 20, 8);  // scorched by shellfire
  return m;
}

VoxelModel makePalm(int v) {
  VoxelModel m(44, 44, 62);
  // A curved trunk, ringed.
  const float bend = 6.0f + (v % 3) * 2.0f, dir = hashf(v, 1) * kTwoPi;
  float px = 22, py = 22;
  const int H = 46 + (v % 2) * 6;
  for (int z = 0; z < H; ++z) {
    const float t = static_cast<float>(z) / H;
    const float cx = 22 + std::cos(dir) * bend * t * t, cy = 22 + std::sin(dir) * bend * t * t;
    cylinder(m, cx, cy, 1.3f - t * 0.3f, z, z + 1, (z % 3 == 0) ? TRUNK : PALM_TRUNK);
    px = cx;
    py = cy;
  }
  // Fronds radiating out and drooping.
  const int fronds = 8;
  for (int i = 0; i < fronds; ++i) {
    const float a = i * kTwoPi / fronds + hashf(v, i + 9) * 0.5f;
    for (int k = 0; k < 15; ++k) {
      const float r = k * 1.0f;
      const float droop = r * r * 0.05f;
      const int x = static_cast<int>(px + std::cos(a) * r), y = static_cast<int>(py + std::sin(a) * r);
      const int z = static_cast<int>(H + 1 - droop);
      m.set(x, y, z, k % 4 == 0 ? PALM_DARK : PALM);
      // Leaflets either side.
      const int sx = static_cast<int>(std::round(-std::sin(a))), sy = static_cast<int>(std::round(std::cos(a)));
      if (k > 2 && k < 13) {
        m.set(x + sx, y + sy, z - 1, PALM_DARK);
        m.set(x - sx, y - sy, z - 1, PALM);
      }
    }
  }
  m.ellipsoid(px, py, H, 1.8f, 1.8f, 1.4f, PALM_DARK);
  return m;
}

VoxelModel makeScrub(int v) {
  VoxelModel m(24, 24, 10);
  for (int i = 0; i < 5; ++i)
    blob(m, 12 + (hashf(v, i) - 0.5f) * 10, 12 + (hashf(v, i + 7) - 0.5f) * 10, 2.5f, 3.2f, 3.2f, 2.6f, DRY_GRASS);
  speckle(m, DRY_GRASS, DRY_DARK, DEAD_WOOD, 31u + v, 34, 10);
  return m;
}

VoxelModel makeJungleTree(int v) {
  VoxelModel m(52, 52, 70);
  const int top = 50 + (v % 3) * 5;
  m.box(24, 24, 0, 28, 28, top, TRUNK);
  for (int i = 0; i < 4; ++i) {  // buttress roots
    const float a = i * kTwoPi / 4 + 0.4f;
    bar3(m, 26, 26, 8, 26 + std::cos(a) * 7, 26 + std::sin(a) * 7, 0, 1.0f, TRUNK);
  }
  // A broad, layered canopy.
  for (int i = 0; i < 9; ++i) {
    const float ox = (hashf(v, i) - 0.5f) * 26, oy = (hashf(v, i + 30) - 0.5f) * 26;
    const float r = 7.0f + hashf(v, i + 60) * 4.0f;
    blob(m, 26 + ox, 26 + oy, top - 2 + hashf(v, i + 90) * 6, r, r, r * 0.55f, JUNGLE);
  }
  for (int z = 0; z < m.sizeZ(); ++z)
    for (int y = 0; y < 52; ++y)
      for (int x = 0; x < 52; ++x) {
        if (m.get(x, y, z) != JUNGLE) continue;
        const uint32_t h = hash32(static_cast<uint32_t>((z * 211 + y) * 97 + x) ^ (v * 999u)) % 100;
        if (z < top - 2 && h < 50) m.set(x, y, z, JUNGLE_DARK);
        else if (h > 84) m.set(x, y, z, JUNGLE_LIGHT);
      }
  for (int i = 0; i < 6; ++i) {  // hanging vines
    const int x = 14 + static_cast<int>(hashf(v, i + 40) * 24), y = 14 + static_cast<int>(hashf(v, i + 50) * 24);
    for (int z = top - 14; z < top - 4; ++z)
      if (!m.get(x, y, z)) m.set(x, y, z, JUNGLE_DARK);
  }
  return m;
}

VoxelModel makeDebris(int v) {
  VoxelModel m(28, 28, 10);
  for (int i = 0; i < 26; ++i) {
    const int x = 4 + static_cast<int>(hashf(v, i) * 18), y = 4 + static_cast<int>(hashf(v, i + 100) * 18);
    const int w = 1 + static_cast<int>(hashf(v, i + 200) * 3), d = 1 + static_cast<int>(hashf(v, i + 300) * 3);
    const int z = static_cast<int>(hashf(v, i + 400) * 3.5f);
    const uint32_t k = hash32(static_cast<uint32_t>(v * 77 + i)) % 5;
    const uint8_t c = k == 0 ? BRICK : k == 1 ? BRICK_DARK : k == 2 ? CONCRETE : k == 3 ? PLASTER_DARK : SOOT;
    m.box(x, y, 0, x + w, y + d, z + 1, c);
  }
  bar3(m, 5, 8 + v % 5, 1, 22, 14, 4 + v % 3, 0.6f, WOOD_DARK);  // a charred beam
  return m;
}

VoxelModel makeSandRock(int v) {
  VoxelModel m(28, 24, 16);
  const float r = 9.0f - (v % 2) * 1.5f;
  m.ellipsoid(14, 12, 3.5f, r, r * 0.72f, 7.0f, SAND_ROCK);
  m.ellipsoid(14 + (v % 2 ? 5.0f : -5.0f), 12, 2.0f, r * 0.55f, r * 0.5f, 4.0f, SAND_ROCK);
  for (int z = 0; z < m.sizeZ(); ++z)  // strata
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x)
        if (m.get(x, y, z) == SAND_ROCK && (z + v) % 4 == 0) m.set(x, y, z, SAND_ROCK_DARK);
  speckle(m, SAND_ROCK, SAND_ROCK_DARK, ROCK_LIGHT, 600u + v, 14, 6);
  return m;
}

// ---------------------------------------------------------------------------
// Civilian buildings
// ---------------------------------------------------------------------------
namespace {

// Walls of a house: a box with doors and windows punched on every side.
void houseWalls(VoxelModel& m, int x0, int y0, int x1, int y1, int h, uint8_t wall, uint8_t trim, int storeys = 1) {
  m.box(x0, y0, 0, x1, y1, h, wall);
  m.box(x0, y0, 0, x1, y1, 1, trim);  // plinth
  const int sh = h / storeys;
  for (int s = 0; s < storeys; ++s) {
    const int z = s * sh + std::max(2, sh / 3);
    for (int x = x0 + 2; x + 2 <= x1 - 1; x += 4) {
      m.box(x, y0, z, x + 2, y0 + 1, z + 3, WINDOW);
      m.box(x, y1 - 1, z, x + 2, y1, z + 3, WINDOW);
      m.box(x, y0, z - 1, x + 2, y0 + 1, z, trim);  // sills
      m.box(x, y1 - 1, z - 1, x + 2, y1, z, trim);
    }
    for (int y = y0 + 2; y + 2 <= y1 - 1; y += 4) {
      m.box(x0, y, z, x0 + 1, y + 2, z + 3, WINDOW);
      m.box(x1 - 1, y, z, x1, y + 2, z + 3, WINDOW);
    }
  }
  const int dx = (x0 + x1) / 2;
  m.box(dx - 1, y1 - 1, 1, dx + 1, y1, 5, WOOD_DARK);  // door
}

// A pitched roof along X, ridge in the middle, with overhanging eaves.
void tiledRoof(VoxelModel& m, int x0, int y0, int x1, int y1, int z) {
  const int half = (y1 - y0) / 2 + 1;
  for (int i = 0; i <= half; ++i) {
    const uint8_t c = (i % 2) ? TILE_ROOF : TILE_ROOF_DARK;
    m.box(x0 - 1, y0 - 1 + i, z + i, x1 + 1, y1 + 1 - i, z + i + 1, c);
  }
  // Gables: fill the triangle under the roof at both ends.
  for (int i = 0; i + 1 < half; ++i) {
    m.box(x0, y0 + i + 1, z + i, x0 + 1, y1 - i - 1, z + i + 1, PLASTER);
    m.box(x1 - 1, y0 + i + 1, z + i, x1, y1 - i - 1, z + i + 1, PLASTER);
  }
}

void chimney(VoxelModel& m, int x, int y, int z0, int z1) {
  m.box(x, y, z0, x + 2, y + 2, z1, BRICK);
  m.box(x, y, z1 - 1, x + 2, y + 2, z1, BRICK_DARK);
}

// Shell damage: a crater bitten out of the building, its edges burnt.
void blast(VoxelModel& m, float cx, float cy, float cz, float r, uint32_t seed) {
  for (int z = 0; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x) {
        const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, dz = z + 0.5f - cz;
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz) + (hashf(seed, (z * 64 + y) * 64 + x) - 0.5f) * 2.0f;
        if (d < r) m.set(x, y, z, 0);
        else if (d < r + 1.4f && m.get(x, y, z)) m.set(x, y, z, SOOT);
      }
}

// Broken masonry heaped on the ground.
void rubbleHeap(VoxelModel& m, float cx, float cy, float r, float h, uint32_t seed) {
  for (int y = static_cast<int>(cy - r); y <= static_cast<int>(cy + r); ++y)
    for (int x = static_cast<int>(cx - r); x <= static_cast<int>(cx + r); ++x) {
      const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy) / r;
      if (d > 1) continue;
      const int top = static_cast<int>((1 - d * d) * h + hashf(seed, y * 97 + x) * 1.5f);
      for (int z = 0; z < top; ++z) {
        const uint32_t k = hash32(static_cast<uint32_t>((z * 31 + y) * 57 + x) ^ seed) % 6;
        m.set(x, y, z, k == 0 ? BRICK : k == 1 ? BRICK_DARK : k == 2 ? PLASTER_DARK : k == 3 ? SOOT : PLASTER);
      }
    }
}

void grimeWalls(VoxelModel& m, uint32_t seed) {
  weather(m, PLASTER, PLASTER_DARK, CONCRETE_LIGHT, seed, 16, 5);
  weather(m, BRICK, BRICK_DARK, RUST, seed + 1, 14, 4);
  weather(m, TILE_ROOF, TILE_ROOF_DARK, SOOT, seed + 2, 10, 3);
  // Rain streaks and soot under the windows.
  for (int z = 1; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x)
        if (m.get(x, y, z) == WINDOW && (m.get(x, y, z - 1) == PLASTER || m.get(x, y, z - 1) == BRICK) &&
            hash32(static_cast<uint32_t>((z * 13 + y) * 7 + x) ^ seed) % 3 == 0)
          m.set(x, y, z - 1, PLASTER_DARK);
}

}  // namespace

VoxelModel makeCottage(const FactionLook&) {
  VoxelModel m(24, 24, 22);
  houseWalls(m, 3, 5, 21, 19, 9, PLASTER, PLASTER_DARK);
  tiledRoof(m, 3, 5, 21, 19, 9);
  chimney(m, 16, 7, 12, 18);
  m.box(1, 19, 0, 23, 20, 2, WOOD_DARK);  // garden fence
  grimeWalls(m, 11);
  return m;
}

VoxelModel makeCottageBombed(const FactionLook& look) {
  VoxelModel m = makeCottage(look);
  blast(m, 15, 9, 14, 6.5f, 21);  // the roof caved in at one end
  blast(m, 4, 14, 4, 2.6f, 22);   // a hole in the wall
  rubbleHeap(m, 2, 12, 4.5f, 3.0f, 23);
  rubbleHeap(m, 14, 12, 4.0f, 2.5f, 24);
  return m;
}

VoxelModel makeTownhouse(const FactionLook&) {
  VoxelModel m(24, 24, 34);
  houseWalls(m, 2, 2, 22, 22, 24, PLASTER, CONCRETE_DARK, 3);
  m.box(2, 2, 0, 22, 22, 4, BRICK);  // brick ground floor (shops)
  for (int x = 4; x < 20; x += 6) m.box(x, 21, 1, x + 4, 22, 3, WINDOW);
  m.box(1, 1, 24, 23, 23, 25, CONCRETE_DARK);  // cornice
  for (int i = 0; i < 4; ++i) m.box(2 + i, 2 + i, 25 + i, 22 - i, 22 - i, 26 + i, i % 2 ? ROOF : ROOF_DARK);  // mansard
  chimney(m, 5, 15, 25, 32);
  chimney(m, 17, 5, 25, 31);
  grimeWalls(m, 31);
  blast(m, 20, 3, 22, 3.5f, 32);  // a shell hit the top corner
  return m;
}

VoxelModel makeChurch(const FactionLook&) {
  VoxelModel m(36, 36, 64);
  // Nave along X.
  m.box(4, 9, 0, 28, 27, 14, PLASTER);
  m.box(4, 9, 0, 28, 27, 1, CONCRETE_DARK);
  for (int x = 7; x < 26; x += 5) {  // tall arched windows
    m.box(x, 9, 4, x + 2, 10, 11, WINDOW);
    m.box(x, 26, 4, x + 2, 27, 11, WINDOW);
  }
  for (int i = 0; i <= 10; ++i) m.box(4, 8 + i, 14 + i, 28, 28 - i, 15 + i, i % 2 ? ROOF : ROOF_DARK);
  // The tower at the west end, with a spire.
  m.box(26, 12, 0, 34, 24, 34, PLASTER);
  m.box(26, 12, 0, 34, 24, 1, CONCRETE_DARK);
  m.box(29, 12, 26, 31, 13, 31, WINDOW);  // belfry openings
  m.box(29, 23, 26, 31, 24, 31, WINDOW);
  m.box(26, 17, 26, 27, 19, 31, WINDOW);
  m.box(29, 17, 1, 31, 18, 1, WOOD_DARK);
  m.box(33, 16, 1, 34, 20, 7, WOOD_DARK);  // west door
  for (int i = 0; i < 12; ++i) {
    const float r = 4.6f - i * 0.38f;
    for (int y = 12; y < 24; ++y)
      for (int x = 26; x < 34; ++x)
        if (std::abs(x + 0.5f - 30.0f) <= r && std::abs(y + 0.5f - 18.0f) <= r)
          m.set(x, y, 34 + i * 2, ROOF_DARK), m.set(x, y, 35 + i * 2, ROOF_DARK);
  }
  m.box(30, 18, 58, 31, 19, 62, STEEL_DARK);  // the cross on the spire
  m.box(29, 18, 60, 32, 19, 61, STEEL_DARK);
  grimeWalls(m, 41);
  blast(m, 10, 26, 18, 5.0f, 42);  // shelled roof
  rubbleHeap(m, 9, 30, 4.0f, 2.5f, 43);
  return m;
}

VoxelModel makeRuin(const FactionLook&) {
  VoxelModel m(24, 24, 20);
  houseWalls(m, 2, 3, 22, 21, 16, BRICK, BRICK_DARK, 2);
  m.box(3, 4, 1, 21, 20, 16, 0);  // gutted
  // Walls broken down to jagged stumps.
  for (int y = 0; y < 24; ++y)
    for (int x = 0; x < 24; ++x) {
      const int keep = 3 + static_cast<int>(hashf(7, (y / 2) * 31 + x / 2) * 14);
      for (int z = keep; z < 20; ++z) m.set(x, y, z, 0);
    }
  rubbleHeap(m, 12, 12, 8.0f, 4.0f, 51);
  rubbleHeap(m, 3, 20, 4.0f, 2.0f, 52);
  speckle(m, BRICK, SOOT, BRICK_DARK, 53, 22, 14);
  m.box(6, 8, 3, 18, 9, 4, WOOD_DARK);  // a fallen joist
  return m;
}

VoxelModel makeAdobe(const FactionLook&) {
  VoxelModel m(24, 24, 18);
  m.box(3, 3, 0, 21, 21, 9, SAND_ROCK);
  m.box(2, 2, 9, 22, 22, 10, SAND_ROCK_DARK);  // flat roof
  for (int x = 2; x < 22; ++x)                 // parapet
    for (int y : {2, 21}) m.set(x, y, 10, SAND_ROCK);
  for (int y = 2; y < 22; ++y)
    for (int x : {2, 21}) m.set(x, y, 10, SAND_ROCK);
  for (int x = 6; x < 18; x += 5) {  // small deep windows
    m.box(x, 3, 5, x + 2, 4, 7, WINDOW);
    m.box(x, 20, 5, x + 2, 21, 7, WINDOW);
  }
  m.box(11, 20, 0, 13, 21, 5, WOOD_DARK);
  for (int i : {5, 10, 15}) m.box(i, 1, 8, i + 1, 3, 9, WOOD);  // roof beams poking out
  m.box(14, 14, 10, 19, 19, 14, SAND_ROCK);                  // a little stair house on the roof
  speckle(m, SAND_ROCK, SAND_ROCK_DARK, PLASTER, 61, 18, 8);
  return m;
}

}  // namespace slave::art

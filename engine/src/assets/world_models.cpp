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
}  // namespace

void houseWalls(VoxelModel& m, int x0, int y0, int x1, int y1, int h, uint8_t wall, uint8_t trim, int storeys) {
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

namespace {

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

// A timber barn: plank walls, big doors, a steep shingled roof.
VoxelModel makeBarn(const FactionLook&) {
  VoxelModel m(24, 24, 24);
  m.box(3, 4, 0, 21, 20, 10, WOOD);
  for (int x = 3; x < 21; x += 2) m.box(x, 4, 0, x + 1, 20, 10, WOOD_DARK);  // plank seams
  m.box(4, 5, 0, 20, 19, 10, WOOD);
  m.box(9, 19, 0, 15, 20, 8, WOOD_DARK);  // doors
  m.box(11, 19, 0, 13, 20, 8, DEAD_WOOD);
  const int half = 9;
  for (int i = 0; i <= half; ++i)
    m.box(2, 3 + i, 10 + i, 22, 21 - i, 11 + i, (i % 2) ? ROOF_DARK : ROOF);
  for (int i = 0; i + 1 < half; ++i) {
    m.box(3, 4 + i + 1, 10 + i, 4, 20 - i - 1, 11 + i, WOOD);
    m.box(20, 4 + i + 1, 10 + i, 21, 20 - i - 1, 11 + i, WOOD);
  }
  m.box(1, 20, 0, 4, 23, 2, DRY_GRASS);  // a hay pile
  weather(m, WOOD, WOOD_DARK, DEAD_WOOD, 71, 14, 6);
  return m;
}

// A small lean-to shed.
VoxelModel makeShed(const FactionLook&) {
  VoxelModel m(12, 12, 12);
  m.box(2, 3, 0, 10, 9, 6, WOOD);
  for (int x = 2; x < 10; x += 2) m.box(x, 3, 0, x + 1, 4, 6, WOOD_DARK);
  m.box(5, 8, 0, 7, 9, 5, WOOD_DARK);
  for (int y = 2; y < 10; ++y) m.box(1, y, 6 + (9 - y) / 3, 11, y + 1, 7 + (9 - y) / 3, ROOF_DARK);
  m.box(8, 9, 0, 11, 11, 1, DEAD_WOOD);  // logs stacked outside
  weather(m, WOOD, WOOD_DARK, DEAD_WOOD, 72, 12, 6);
  return m;
}

// A Russian log house: round logs, white carved window frames, plank roof.
VoxelModel makeIzba(const FactionLook&) {
  VoxelModel m(24, 24, 20);
  for (int z = 0; z < 9; ++z) m.box(4, 5, z, 20, 19, z + 1, z % 2 ? WOOD_DARK : WOOD);
  m.box(5, 6, 0, 19, 18, 9, WOOD);
  for (int x = 6; x < 18; x += 4) {
    m.box(x, 18, 3, x + 2, 19, 6, WINDOW);
    m.box(x - 1, 18, 2, x + 3, 19, 3, PLASTER);   // carved sill
    m.box(x - 1, 18, 6, x + 3, 19, 7, PLASTER);   // and lintel
  }
  m.box(19, 10, 0, 20, 12, 6, WOOD_DARK);  // door at the side
  const int half = 8;
  for (int i = 0; i <= half; ++i) m.box(3, 4 + i, 9 + i, 21, 20 - i, 10 + i, (i % 2) ? DEAD_WOOD : WOOD_DARK);
  for (int i = 0; i + 1 < half; ++i) {
    m.box(4, 5 + i + 1, 9 + i, 5, 19 - i - 1, 10 + i, WOOD);
    m.box(19, 5 + i + 1, 9 + i, 20, 19 - i - 1, 10 + i, WOOD);
  }
  m.box(7, 8, 14, 9, 10, 19, BRICK);  // stove chimney
  for (int x = 1; x < 23; x += 3) m.box(x, 22, 0, x + 1, 23, 3, DEAD_WOOD);  // wattle fence posts
  m.box(1, 22, 1, 23, 23, 2, DEAD_WOOD);
  weather(m, WOOD, WOOD_DARK, DEAD_WOOD, 73, 14, 6);
  return m;
}

// A brick warehouse: long, low, loading doors along the front.
VoxelModel makeWarehouse(const FactionLook&) {
  VoxelModel m(36, 24, 22);
  m.box(2, 3, 0, 34, 21, 12, BRICK);
  m.box(2, 3, 0, 34, 21, 1, CONCRETE_DARK);
  for (int x = 5; x < 32; x += 7) {
    m.box(x, 20, 0, x + 4, 21, 8, WOOD_DARK);  // loading doors
    m.box(x - 1, 20, 8, x + 5, 21, 9, CONCRETE);
    m.box(x, 3, 7, x + 3, 4, 10, WINDOW);      // high windows at the back
  }
  for (int y = 6; y < 19; y += 5) {
    m.box(2, y, 7, 3, y + 2, 10, WINDOW);
    m.box(33, y, 7, 34, y + 2, 10, WINDOW);
  }
  for (int i = 0; i <= 5; ++i) m.box(1, 2 + i * 2, 12 + i, 35, 22 - i * 2, 13 + i, (i % 2) ? ROOF_DARK : ROOF);
  m.box(27, 7, 16, 30, 10, 20, BRICK_DARK);  // chimney
  m.box(4, 21, 0, 10, 24, 2, WOOD);           // crates on the ramp
  m.box(5, 21, 2, 8, 23, 3, WOOD_DARK);
  grimeWalls(m, 74);
  return m;
}

// A village well: a stone ring under a little roof on two posts.
VoxelModel makeWell(const FactionLook&) {
  VoxelModel m(12, 12, 12);
  for (int y = 0; y < 12; ++y)
    for (int x = 0; x < 12; ++x) {
      const float d = std::hypot(x + 0.5f - 6, y + 0.5f - 6);
      if (d < 3.6f && d > 2.2f)
        for (int z = 0; z < 3; ++z) m.set(x, y, z, (x + y + z) % 3 ? ROCK : ROCK_DARK);
      else if (d <= 2.2f) m.set(x, y, 0, WINDOW);  // dark water
    }
  m.box(2, 5, 0, 3, 7, 8, WOOD_DARK);
  m.box(9, 5, 0, 10, 7, 8, WOOD_DARK);
  m.box(2, 6, 6, 10, 7, 7, DEAD_WOOD);  // windlass
  for (int i = 0; i < 3; ++i) m.box(1, 3 + i, 8 + i, 11, 10 - i, 9 + i, ROOF_DARK);
  return m;
}

// A railway water tower: a timber tank on a brick tower.
VoxelModel makeWaterTower(const FactionLook&) {
  VoxelModel m(12, 12, 40);
  m.box(3, 3, 0, 9, 9, 22, BRICK);
  for (int z = 4; z < 20; z += 6) m.box(5, 8, z, 7, 9, z + 3, WINDOW);
  m.box(5, 8, 0, 7, 9, 4, WOOD_DARK);
  for (int y = 0; y < 12; ++y)
    for (int x = 0; x < 12; ++x)
      if (std::hypot(x + 0.5f - 6, y + 0.5f - 6) < 5.8f)
        for (int z = 22; z < 32; ++z) m.set(x, y, z, (z % 3 == 0) ? STEEL_DARK : WOOD_DARK);
  for (int i = 0; i < 4; ++i)
    for (int y = 0; y < 12; ++y)
      for (int x = 0; x < 12; ++x)
        if (std::hypot(x + 0.5f - 6, y + 0.5f - 6) < 5.8f - i * 1.4f) m.set(x, y, 32 + i, ROOF_DARK);
  weather(m, BRICK, BRICK_DARK, SOOT, 75, 14, 5);
  return m;
}

// --- Small decorations -------------------------------------------------------
// (prop voxels are 1/32 tile)
VoxelModel makeStump(int v) {
  VoxelModel m(16, 16, 10);
  const float r = 2.6f + (v % 3) * 0.5f;
  for (int y = 0; y < 16; ++y)
    for (int x = 0; x < 16; ++x) {
      const float d = std::hypot(x + 0.5f - 8, y + 0.5f - 8);
      if (d < r) {
        const int h = 3 + v % 2 + (d < r - 1 ? 1 : 0);
        for (int z = 0; z < h; ++z) m.set(x, y, z, z == h - 1 && d < r - 0.8f ? DEAD_WOOD : TRUNK);
      }
    }
  for (int i = 0; i < 4; ++i) {  // roots
    const float a = i * 1.57f + v;
    for (int k = 0; k < 4; ++k) m.set(8 + static_cast<int>(std::cos(a) * (r + k)), 8 + static_cast<int>(std::sin(a) * (r + k)), 0, TRUNK);
  }
  return m;
}

VoxelModel makeLogs(int v) {
  VoxelModel m(28, 16, 10);
  const int n = 3 + v % 2;
  for (int i = 0; i < n; ++i) {
    const float cy = 5.0f + i * 3.0f - (i >= 3 ? 7.5f : 0), cz = i >= 3 ? 4.6f : 1.6f;
    for (int x = 3 + (i % 2); x < 24 - (i % 2); ++x)
      for (int y = 0; y < 16; ++y)
        for (int z = 0; z < 10; ++z)
          if (std::hypot(y + 0.5f - cy, z + 0.5f - cz) < 1.6f) m.set(x, y, z, (x == 3 + (i % 2) || x == 23 - (i % 2)) ? DEAD_WOOD : TRUNK);
  }
  return m;
}

VoxelModel makeHaystack(int v) {
  VoxelModel m(20, 20, 20);
  const float r = 6.5f + v, h = 12.0f + v * 2;
  for (int z = 0; z < 20; ++z)
    for (int y = 0; y < 20; ++y)
      for (int x = 0; x < 20; ++x) {
        const float k = z / h;
        if (k > 1) continue;
        const float rr = r * std::sqrt(std::max(0.0f, 1.0f - k * k * k));
        if (std::hypot(x + 0.5f - 10, y + 0.5f - 10) < rr)
          m.set(x, y, z, (hash32(static_cast<uint32_t>((z * 20 + y) * 20 + x + v)) % 5) ? DRY_GRASS : DRY_DARK);
      }
  m.box(9, 9, static_cast<int>(h) - 1, 11, 11, static_cast<int>(h) + 2, DEAD_WOOD);  // the pole
  return m;
}

VoxelModel makeCrates(int v) {
  VoxelModel m(20, 20, 12);
  auto crate = [&](int x, int y, int z, int s) {
    m.box(x, y, z, x + s, y + s, z + s, WOOD);
    m.box(x, y, z + s - 1, x + s, y + s, z + s, WOOD_DARK);
    m.box(x, y, z, x + 1, y + s, z + s, WOOD_DARK);
  };
  auto barrel = [&](int cx, int cy, uint8_t c) {
    for (int y = cy - 2; y <= cy + 2; ++y)
      for (int x = cx - 2; x <= cx + 2; ++x)
        if (std::hypot(x + 0.5f - cx - 0.5f, y + 0.5f - cy - 0.5f) < 2.4f)
          for (int z = 0; z < 7; ++z) m.set(x, y, z, (z == 2 || z == 5) ? static_cast<uint8_t>(STEEL_DARK) : c);
  };
  crate(3, 3, 0, 6);
  if (v % 2) crate(4, 4, 6, 4);
  crate(10, 2, 0, 5);
  barrel(14, 13, v % 3 ? RUST : TANK_PAINT_DARK);
  if (v >= 2) barrel(8, 14, RUST);
  return m;
}

VoxelModel makeFence(int v) {
  VoxelModel m(32, 8, 10);
  for (int x = 1; x < 31; x += 5) m.box(x, 3, 0, x + 1, 4, 7 + (x % 2), DEAD_WOOD);  // posts
  for (int z : {2, 5}) m.box(0, 3, z, 32, 4, z + 1, WOOD);                          // rails
  if (v == 1) m.box(14, 3, 0, 19, 4, 7, 0);                                       // a gap
  return m;
}

// --- Field works, at twice the structure resolution (24 voxels a tile) -----
// Joined pieces: `mask` says which neighbours a piece runs to (+X 1, +Y 2,
// -X 4, -Y 8); a lone piece is a short row across the tile.
namespace {
// The segments of a joined piece: from the centre to each linked edge.
template <class Seg>
void forArms(int mask, Seg&& seg) {
  constexpr float c = 12.0f;
  if (mask == 0) {
    seg(c - 7, c, c + 7, c);
    return;
  }
  if (mask & 1) seg(c, c, 24.5f, c);
  if (mask & 4) seg(-0.5f, c, c, c);
  if (mask & 2) seg(c, c, c, 24.5f);
  if (mask & 8) seg(c, -0.5f, c, c);
}
}  // namespace

// Sandbags: two courses of plump bags laid like brickwork along each arm.
VoxelModel makeSandbagLine(int mask) {
  VoxelModel m(24, 24, 8);
  auto bag = [&](float cx, float cy, float cz, bool alongX, uint32_t seed) {
    const float hx = alongX ? 2.4f : 1.9f, hy = alongX ? 1.9f : 2.4f, hz = 1.35f;
    const float tone = hashf(seed, 7);
    const uint8_t col = tone < 0.55f ? SANDBAG : tone < 0.85f ? SANDBAG_DARK : DRY_GRASS;
    for (int z = static_cast<int>(cz - hz - 1); z <= static_cast<int>(cz + hz + 1); ++z)
      for (int y = static_cast<int>(cy - hy - 1); y <= static_cast<int>(cy + hy + 1); ++y)
        for (int x = static_cast<int>(cx - hx - 1); x <= static_cast<int>(cx + hx + 1); ++x) {
          const float dx = (x + 0.5f - cx) / hx, dy = (y + 0.5f - cy) / hy, dz = (z + 0.5f - cz) / hz;
          // A squashed, rounded box: flat underneath, pillowy on top.
          if (std::pow(std::abs(dx), 2.6f) + std::pow(std::abs(dy), 2.6f) + dz * dz * (dz > 0 ? 1.0f : 0.4f) <= 1.0f)
            m.set(x, y, z, (std::abs(dx) > 0.82f || std::abs(dy) > 0.82f) && dz < 0.3f ? static_cast<uint8_t>(SANDBAG_DARK) : col);
        }
  };
  uint32_t seed = 101u + static_cast<uint32_t>(mask) * 31u;
  forArms(mask, [&](float x0, float y0, float x1, float y1) {
    const float dx = x1 - x0, dy = y1 - y0, len = std::max(std::abs(dx), std::abs(dy));
    const float ux = dx / len, uy = dy / len;
    const bool alongX = std::abs(dx) > std::abs(dy);
    for (int course = 0; course < 2; ++course)
      for (float t = course ? 2.4f : 0.0f; t <= len + 0.01f; t += 4.8f)
        bag(x0 + ux * t, y0 + uy * t, 1.3f + course * 2.4f, alongX, ++seed);
  });
  bag(12, 12, 1.3f, true, 999);  // the corner bag
  return m;
}

// Barbed wire: stakes and a loose concertina coil along each arm.
VoxelModel makeWireLine(int mask) {
  VoxelModel m(24, 24, 12);
  forArms(mask, [&](float x0, float y0, float x1, float y1) {
    const float len = std::hypot(x1 - x0, y1 - y0);
    const float ux = (x1 - x0) / len, uy = (y1 - y0) / len, px = -uy, py = ux;
    // Screw pickets at each end.
    for (float t : {0.0f, len}) {
      const float sx = x0 + ux * std::min(t, len - 1.0f), sy = y0 + uy * std::min(t, len - 1.0f);
      bar3(m, sx, sy, 0, sx, sy, 8.5f, 0.45f, WOOD_DARK);
    }
    // Coil: a helix of thin wire, radius 3 voxels.
    const int steps = static_cast<int>(len * 9);
    for (int i = 0; i <= steps; ++i) {
      const float t = static_cast<float>(i) / steps * len;
      const float a = t * 1.6f;
      const float r = 3.0f;
      const float x = x0 + ux * t + px * std::cos(a) * r * 0.9f, y = y0 + uy * t + py * std::cos(a) * r * 0.9f;
      const float z = 3.6f + std::sin(a) * r;
      if (z < 0) continue;
      m.set(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z), (i / 3) % 2 ? STEEL : RUST);
    }
    // A straight top strand between the pickets.
    for (float t = 0; t <= len; t += 0.4f) m.set(static_cast<int>(x0 + ux * t), static_cast<int>(y0 + uy * t), 7, STEEL_DARK);
  });
  return m;
}

// Czech hedgehogs: three girders welded crosswise, three of them on a tile.
VoxelModel makeHedgehogs(int v) {
  VoxelModel m(24, 24, 12);
  auto hog = [&](float cx, float cy, float a) {
    const float L = 5.2f;
    for (int k = 0; k < 3; ++k) {
      // Three girders along the cube's body diagonals, giving the six-armed star.
      const float dirs[3][3] = {{1, 0.15f, 0.9f}, {-0.4f, 1, 0.85f}, {0.55f, -0.75f, 1}};
      const float dx = dirs[k][0], dy = dirs[k][1], dz = dirs[k][2];
      const float n = std::sqrt(dx * dx + dy * dy + dz * dz);
      const float ca = std::cos(a), sa = std::sin(a);
      const float rx = (ca * dx - sa * dy) / n, ry = (sa * dx + ca * dy) / n, rz = dz / n;
      bar3(m, cx - rx * L, cy - ry * L, 5.0f - rz * L, cx + rx * L, cy + ry * L, 5.0f + rz * L, 0.7f, k == 1 ? RUST : STEEL_DARK);
    }
  };
  hog(7, 7, 0.3f + v);
  hog(17, 9, 1.4f + v);
  hog(11, 17, 2.2f + v);
  return m;
}

// A concrete pillbox: a smooth low dome on a banked earth mound, firing
// slits all round and a sunken entrance at the back.
VoxelModel makePillbox(const FactionLook& look) {
  VoxelModel m(48, 48, 22);
  const float cx = 24, cy = 24;
  for (int y = 0; y < 48; ++y)
    for (int x = 0; x < 48; ++x) {
      const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
      // Earth berm, sloping away.
      const float berm = std::clamp((23.5f - d) * 0.75f, 0.0f, 8.0f);
      for (int z = 0; z < static_cast<int>(berm); ++z) m.set(x, y, z, z == static_cast<int>(berm) - 1 ? EARTH : EARTH_DARK);
      // Dome: a flattened hemisphere of concrete.
      if (d < 15.5f) {
        const float k = d / 15.5f;
        const float top = 6.0f + 12.5f * std::sqrt(std::max(0.0f, 1.0f - k * k * k));
        for (int z = 5; z < static_cast<int>(top); ++z) m.set(x, y, z, look.sloped ? CONCRETE_DARK : CONCRETE);
      }
    }
  weather(m, EARTH, EARTH_DARK, SANDBAG_DARK, 61, 26, 10);
  weather(m, CONCRETE, CONCRETE_DARK, CONCRETE_LIGHT, 62, 22, 12);
  // Firing slits: dark wedges cut into the dome, with a concrete lip above.
  for (int s = 0; s < 6; ++s) {
    const float a = s * 1.0472f + 0.4f;
    for (int w = -2; w <= 2; ++w) {
      const float aa = a + w * 0.06f;
      for (float r = 11.0f; r < 16.5f; r += 0.5f) {
        const int x = static_cast<int>(cx + std::cos(aa) * r), y = static_cast<int>(cy + std::sin(aa) * r);
        for (int z = 10; z < 12; ++z) m.set(x, y, z, WINDOW);
        m.set(x, y, 12, CONCRETE_DARK);
      }
    }
  }
  // Entrance: a sunken passage behind, lined with timber.
  for (int x = 20; x < 28; ++x)
    for (int y = 37; y < 48; ++y)
      for (int z = 0; z < 9; ++z) m.set(x, y, z, 0);
  for (int y = 37; y < 48; ++y) {
    m.box(19, y, 0, 20, y + 1, 6, WOOD_DARK);
    m.box(28, y, 0, 29, y + 1, 6, WOOD_DARK);
  }
  m.box(21, 36, 5, 27, 38, 9, WINDOW);  // the doorway
  m.box(22, 22, 18, 26, 26, 19, TEAM);  // a painted roof marking
  return m;
}

// The pillbox's machine gun, in a small rotating cupola on the dome.
VoxelModel makePillboxGun(const FactionLook&) {
  VoxelModel m(48, 48, 23);
  for (int y = 0; y < 48; ++y)
    for (int x = 0; x < 48; ++x) {
      const float d = std::hypot(x + 0.5f - 24, y + 0.5f - 24);
      if (d < 4.2f)
        for (int z = 18; z < 21 + (d < 2.5f ? 1 : 0); ++z) m.set(x, y, z, z > 20 ? STEEL : STEEL_DARK);
    }
  m.box(27, 23, 19, 33, 25, 20, STEEL_DARK);  // barrel
  return m;
}

// A post-and-rail fence (supply compounds), joined like the field works.
VoxelModel makeFenceLine(int mask) {
  VoxelModel m(24, 24, 14);
  forArms(mask, [&](float x0, float y0, float x1, float y1) {
    const float len = std::hypot(x1 - x0, y1 - y0);
    const float ux = (x1 - x0) / len, uy = (y1 - y0) / len;
    for (float t = 0; t <= len + 0.01f; t += 6.0f) {
      const float px = x0 + ux * std::min(t, len - 0.6f), py = y0 + uy * std::min(t, len - 0.6f);
      bar3(m, px, py, 0, px, py, 8.5f, 0.85f, WOOD_DARK);
    }
    for (float z : {3.5f, 7.0f}) bar3(m, x0, y0, z, x1, y1, z, 0.8f, z > 5 ? WOOD : DEAD_WOOD);
  });
  bar3(m, 12, 12, 0, 12, 12, 9.0f, 1.0f, WOOD_DARK);  // the corner post
  return m;
}

// A railway station: a brick hall, a canopy over the platform, a clock.
VoxelModel makeStation(const FactionLook&) {
  VoxelModel m(48, 24, 26);
  houseWalls(m, 6, 4, 42, 16, 12, BRICK, CONCRETE_DARK, 2);
  for (int i = 0; i <= 7; ++i) m.box(5, 3 + i, 12 + i, 43, 17 - i, 13 + i, (i % 2) ? ROOF_DARK : ROOF);
  m.box(20, 3, 12, 28, 4, 20, BRICK);       // the gable over the entrance
  m.box(23, 2, 15, 25, 3, 17, PLASTER);     // its clock
  m.box(2, 16, 0, 46, 23, 2, CONCRETE);     // platform
  for (int x = 4; x < 46; x += 6) m.box(x, 20, 2, x + 1, 21, 9, STEEL_DARK);  // canopy posts
  m.box(2, 16, 9, 46, 23, 10, ROOF_DARK);   // canopy
  m.box(30, 11, 20, 33, 14, 24, BRICK_DARK);
  grimeWalls(m, 81);
  return m;
}

// A long farmhouse: dwelling at one end, stable and hayloft at the other.
VoxelModel makeFarmhouse(const FactionLook&) {
  VoxelModel m(36, 24, 22);
  houseWalls(m, 3, 5, 18, 19, 9, PLASTER, PLASTER_DARK);
  m.box(18, 5, 0, 33, 19, 9, WOOD);
  for (int x = 18; x < 33; x += 2) m.box(x, 18, 0, x + 1, 19, 9, WOOD_DARK);
  m.box(23, 18, 0, 28, 19, 7, WOOD_DARK);  // stable doors
  tiledRoof(m, 3, 5, 33, 19, 9);
  chimney(m, 8, 7, 12, 18);
  m.box(26, 19, 0, 30, 23, 2, DRY_GRASS);  // straw in the yard
  grimeWalls(m, 82);
  return m;
}

// A two-storey schoolhouse with a little bell turret.
VoxelModel makeSchool(const FactionLook&) {
  VoxelModel m(36, 24, 30);
  houseWalls(m, 3, 4, 33, 20, 16, PLASTER, BRICK, 2);
  tiledRoof(m, 3, 4, 33, 20, 16);
  m.box(16, 10, 23, 20, 14, 27, WOOD);       // bell turret
  m.box(15, 9, 27, 21, 15, 28, ROOF_DARK);
  m.box(16, 20, 0, 20, 23, 1, CONCRETE);     // steps
  grimeWalls(m, 83);
  return m;
}


// ---------------------------------------------------------------------------
// More civilian buildings (12 voxels a tile, like the others).
// ---------------------------------------------------------------------------
namespace {

// A steep pitched roof along X (twice as steep as tiledRoof), gables filled.
void steepRoof(VoxelModel& m, int x0, int y0, int x1, int y1, int z, uint8_t a, uint8_t b, uint8_t gable) {
  const int half = (y1 - y0) / 2 + 1;
  for (int i = 0; i <= half; ++i)
    m.box(x0 - 1, y0 - 1 + i, z + 2 * i, x1 + 1, y1 + 1 - i, z + 2 * i + 2, (i % 2) ? a : b);
  for (int i = 0; i + 1 < half; ++i) {
    m.box(x0, y0 + i + 1, z + 2 * i, x0 + 1, y1 - i - 1, z + 2 * i + 2, gable);
    m.box(x1 - 1, y0 + i + 1, z + 2 * i, x1, y1 - i - 1, z + 2 * i + 2, gable);
  }
}

// A hipped roof: slopes on all four sides up to a short ridge.
void hippedRoof(VoxelModel& m, int x0, int y0, int x1, int y1, int z, uint8_t a, uint8_t b) {
  for (int i = 0; x0 - 1 + i < x1 + 1 - i && y0 - 1 + i < y1 + 1 - i; ++i)
    m.box(x0 - 1 + i, y0 - 1 + i, z + i, x1 + 1 - i, y1 + 1 - i, z + i + 1, (i % 2) ? a : b);
}

// Dark oak framing over plastered walls: posts, rails and braces, round the
// windows (only where the wall shows).
void timberFrame(VoxelModel& m, int x0, int y0, int x1, int y1, int z0, int z1) {
  auto beam = [&](int x, int y, int z) {
    const uint8_t c = m.get(x, y, z);
    if (c == PLASTER || c == PLASTER_DARK) m.set(x, y, z, WOOD_DARK);
  };
  for (int z = z0; z < z1; ++z) {
    const bool rail = z == z0 || z == z1 - 1 || z == (z0 + z1) / 2;
    for (int x = x0; x < x1; ++x) {
      const bool post = (x - x0) % 4 == 0 || x == x1 - 1;
      const bool brace = ((x - x0) % 4) == ((z - z0) % 4);
      if (rail || post || brace) beam(x, y0, z), beam(x, y1 - 1, z);
    }
    for (int y = y0; y < y1; ++y) {
      const bool post = (y - y0) % 4 == 0 || y == y1 - 1;
      const bool brace = ((y - y0) % 4) == (3 - (z - z0) % 4);
      if (rail || post || brace) beam(x0, y, z), beam(x1 - 1, y, z);
    }
  }
}

}  // namespace

VoxelModel makeHalfTimber(const FactionLook&) {
  VoxelModel m(24, 24, 34);
  houseWalls(m, 4, 6, 20, 18, 8, PLASTER, BRICK);
  houseWalls(m, 3, 5, 21, 19, 8, PLASTER, WOOD_DARK);  // the jettied upper floor
  for (int z = 0; z < 8; ++z)                           // ...standing on the ground floor
    for (int y = 5; y < 19; ++y)
      for (int x = 3; x < 21; ++x)
        if (x < 4 || x >= 20 || y < 6 || y >= 18) m.set(x, y, z, 0);
  houseWalls(m, 4, 6, 20, 18, 8, PLASTER, BRICK);
  VoxelModel up(24, 24, 34);
  houseWalls(up, 3, 5, 21, 19, 8, PLASTER, WOOD_DARK);
  for (int z = 0; z < 8; ++z)
    for (int y = 0; y < 24; ++y)
      for (int x = 0; x < 24; ++x)
        if (const uint8_t c = up.get(x, y, z)) m.set(x, y, z + 8, c);
  timberFrame(m, 4, 6, 20, 18, 0, 8);
  timberFrame(m, 3, 5, 21, 19, 8, 16);
  steepRoof(m, 3, 5, 21, 19, 16, ROOF, ROOF_DARK, PLASTER);
  chimney(m, 7, 8, 20, 30);
  m.box(10, 19, 2, 14, 20, 3, WOOD);  // a bench by the door
  grimeWalls(m, 111);
  return m;
}

VoxelModel makeStoneCottage(const FactionLook&) {
  VoxelModel m(24, 24, 22);
  houseWalls(m, 3, 5, 21, 19, 8, ROCK, ROCK_DARK);
  speckle(m, ROCK, ROCK_DARK, ROCK_LIGHT, 121, 26, 14);
  // A thick thatch, rounded over the ridge and hanging low at the eaves.
  for (int i = 0; i <= 8; ++i) {
    const uint8_t c = (i % 3 == 0) ? DRY_DARK : DRY_GRASS;
    m.box(2, 4 + i, 7 + i, 22, 20 - i, 8 + i, c);
  }
  for (int i = 0; i < 7; ++i) {
    m.box(3, 6 + i, 8 + i, 4, 18 - i, 9 + i, ROCK);
    m.box(20, 6 + i, 8 + i, 21, 18 - i, 9 + i, ROCK);
  }
  speckle(m, DRY_GRASS, DRY_DARK, DEAD_WOOD, 122, 18, 6);
  chimney(m, 17, 11, 12, 19);
  m.box(1, 20, 0, 23, 21, 2, ROCK_DARK);  // a dry-stone wall round the garden
  m.box(1, 3, 0, 2, 21, 2, ROCK_DARK);
  return m;
}

VoxelModel makeShopfront(const FactionLook&) {
  VoxelModel m(24, 24, 32);
  houseWalls(m, 3, 3, 21, 19, 20, PLASTER, CONCRETE_DARK, 2);
  m.box(3, 18, 1, 21, 19, 7, BRICK);                 // the shop's front
  for (int x = 4; x < 20; x += 5) m.box(x, 18, 2, x + 4, 19, 6, WINDOW);  // big display windows
  m.box(10, 18, 1, 12, 19, 6, WOOD_DARK);            // shop door
  m.box(3, 18, 7, 21, 19, 8, WOOD_DARK);             // the sign board
  for (int x = 6; x < 18; x += 2) m.set(x, 18, 7, PLASTER);  // its painted letters
  for (int i = 0; i < 4; ++i)                        // a striped awning sloping out over the pavement
    for (int x = 3; x < 21; ++x) m.set(x, 19 + i, 7 - i / 2, ((x / 2) % 2) ? CANVAS : MARK_RED);
  tiledRoof(m, 3, 3, 21, 19, 20);
  chimney(m, 16, 6, 23, 30);
  grimeWalls(m, 131);
  return m;
}

VoxelModel makeApartment(const FactionLook&) {
  VoxelModel m(24, 24, 50);
  houseWalls(m, 2, 2, 22, 22, 40, PLASTER, CONCRETE_DARK, 4);
  m.box(2, 2, 0, 22, 22, 4, CONCRETE);               // a stone ground floor
  for (int x = 4; x < 20; x += 6) m.box(x, 21, 1, x + 3, 22, 4, WINDOW);
  for (int z = 10; z < 40; z += 10)                  // balconies on the front, with railings
    for (int x : {4, 14}) {
      m.box(x, 22, z, x + 6, 24, z + 1, CONCRETE_DARK);
      m.box(x, 23, z + 1, x + 6, 24, z + 3, STEEL_DARK);
      m.box(x + 1, 23, z + 1, x + 5, 24, z + 2, 0);
    }
  m.box(1, 1, 40, 23, 23, 41, CONCRETE_DARK);        // cornice and a flat roof with a parapet
  m.box(1, 1, 41, 23, 2, 43, CONCRETE);
  m.box(1, 22, 41, 23, 23, 43, CONCRETE);
  m.box(1, 1, 41, 2, 23, 43, CONCRETE);
  m.box(22, 1, 41, 23, 23, 43, CONCRETE);
  m.box(5, 5, 41, 10, 10, 46, BRICK_DARK);           // the stair head
  cylinder(m, 16, 15, 2.6f, 41, 47, STEEL_DARK);     // a water tank
  chimney(m, 17, 4, 41, 46);
  grimeWalls(m, 141);
  blast(m, 3, 12, 30, 3.0f, 142);                     // shell-scarred
  return m;
}

VoxelModel makeBrickworks(const FactionLook&) {
  VoxelModel m(36, 24, 58);
  m.box(2, 4, 0, 34, 21, 11, BRICK);
  m.box(2, 4, 0, 34, 21, 1, CONCRETE_DARK);
  for (int x = 4; x < 32; x += 5) m.box(x, 20, 3, x + 3, 21, 8, WINDOW);   // tall iron-framed windows
  for (int x = 4; x < 32; x += 5) m.box(x, 4, 3, x + 3, 5, 8, WINDOW);
  m.box(14, 20, 0, 22, 21, 9, WOOD_DARK);           // the big doors
  // A saw-tooth roof: steep glazed faces to the north, slopes to the south.
  for (int t = 0; t < 4; ++t) {
    const int y0 = 4 + t * 4;
    for (int i = 0; i < 4; ++i) m.box(1, y0 + i, 11 + (3 - i) * 2, 35, y0 + i + 1, 12 + (3 - i) * 2 + 1, ROOF_DARK);
    for (int i = 0; i < 4; ++i) m.box(1, y0, 11, 35, y0 + 1, 11 + (3 - i) * 2 + 2, i == 0 ? WINDOW : ROOF_DARK);
    m.box(1, y0, 12, 35, y0 + 1, 18, WINDOW);
  }
  // The chimney, with darker bands and soot at the top.
  for (int z = 0; z < 56; ++z) {
    const float r = 2.8f - z * 0.012f;
    cylinder(m, 30, 8, r, z, z + 1, (z % 9 == 8) ? BRICK_DARK : BRICK);
  }
  cylinder(m, 30, 8, 1.6f, 50, 56, 0);
  cylinder(m, 30, 8, 2.9f, 53, 56, SOOT);
  cylinder(m, 30, 8, 1.6f, 53, 56, 0);
  m.box(3, 21, 0, 9, 24, 3, WOOD);                    // crates in the yard
  m.box(10, 21, 0, 12, 23, 4, RUST);                  // a drum
  grimeWalls(m, 151);
  return m;
}

VoxelModel makeTavern(const FactionLook&) {
  VoxelModel m(36, 24, 28);
  houseWalls(m, 3, 5, 33, 19, 15, PLASTER, PLASTER_DARK, 2);
  timberFrame(m, 3, 5, 33, 19, 8, 15);
  tiledRoof(m, 3, 5, 33, 19, 15);
  chimney(m, 7, 8, 20, 26);
  chimney(m, 27, 13, 20, 25);
  m.box(17, 19, 9, 18, 22, 10, STEEL_DARK);            // the sign's iron bracket
  m.box(18, 21, 5, 21, 22, 9, WOOD);                    // the swinging sign
  m.box(19, 21, 6, 20, 22, 8, MARK_RED);
  for (int x : {6, 24}) {                               // tables and benches outside
    m.box(x, 20, 2, x + 4, 22, 3, WOOD);
    m.box(x, 22, 1, x + 4, 23, 2, WOOD_DARK);
  }
  cylinder(m, 30, 21, 1.2f, 0, 3, WOOD_DARK);           // barrels
  cylinder(m, 32, 21, 1.2f, 0, 3, WOOD_DARK);
  grimeWalls(m, 161);
  return m;
}

VoxelModel makeTerrace(const FactionLook&) {
  VoxelModel m(36, 24, 28);
  const uint8_t walls[3] = {BRICK, PLASTER, PLASTER_DARK};
  for (int k = 0; k < 3; ++k) {
    const int x0 = 2 + k * 11;
    houseWalls(m, x0, 5, x0 + 11, 19, 15, walls[k], k == 0 ? BRICK_DARK : CONCRETE_DARK, 2);
    m.box(x0 + 1, 19, 0, x0 + 4, 21, 1, CONCRETE);       // front steps
  }
  tiledRoof(m, 2, 5, 35, 19, 15);
  for (int k = 0; k < 4; ++k) chimney(m, 1 + k * 11, 11, 20, 26);
  grimeWalls(m, 171);
  return m;
}

VoxelModel makeManor(const FactionLook&) {
  VoxelModel m(36, 24, 30);
  houseWalls(m, 3, 5, 33, 19, 16, PLASTER, CONCRETE_DARK, 2);
  hippedRoof(m, 3, 5, 33, 19, 16, ROOF, ROOF_DARK);
  chimney(m, 5, 11, 18, 27);
  chimney(m, 29, 11, 18, 27);
  // A pillared porch with a pediment over the door.
  for (int x : {14, 17, 20, 23}) m.box(x - 1, 19, 0, x, 22, 9, CONCRETE_LIGHT);
  m.box(12, 19, 9, 24, 23, 10, CONCRETE);
  for (int i = 0; i < 3; ++i) m.box(13 + i * 2, 21, 10 + i, 23 - i * 2, 23, 11 + i, CONCRETE);
  m.box(15, 22, 0, 21, 24, 1, CONCRETE);                // steps
  for (int x : {2, 33}) m.box(x, 21, 0, x + 1, 24, 3, PINE);  // clipped yews
  grimeWalls(m, 181);
  return m;
}

VoxelModel makeTownHall(const FactionLook&) {
  VoxelModel m(36, 36, 62);
  houseWalls(m, 3, 7, 33, 29, 16, BRICK, CONCRETE, 2);
  m.box(3, 7, 15, 33, 29, 16, CONCRETE);                // stone band at the eaves
  hippedRoof(m, 3, 7, 33, 29, 16, ROOF, ROOF_DARK);
  // The clock tower rising out of the front.
  m.box(14, 23, 0, 22, 31, 38, BRICK);
  m.box(14, 23, 0, 22, 31, 1, CONCRETE);
  m.box(16, 30, 1, 20, 31, 7, WOOD_DARK);               // main door
  m.box(15, 30, 10, 21, 31, 13, WINDOW);
  for (int z : {16, 26}) m.box(13, 22, z, 23, 32, z + 1, CONCRETE);
  m.box(16, 30, 30, 20, 31, 34, PLASTER);               // clock faces
  m.box(21, 25, 30, 22, 29, 34, PLASTER);
  m.box(17, 30, 32, 19, 31, 33, MARK_BLACK);
  m.box(18, 30, 31, 19, 31, 33, MARK_BLACK);
  m.box(15, 24, 34, 21, 30, 38, 0);                     // open belfry
  m.box(15, 24, 34, 16, 25, 38, BRICK);
  m.box(20, 24, 34, 21, 25, 38, BRICK);
  m.box(15, 29, 34, 16, 30, 38, BRICK);
  m.box(20, 29, 34, 21, 30, 38, BRICK);
  for (int i = 0; i < 8; ++i) m.box(13 + i, 22 + i, 38 + i * 2, 23 - i, 32 - i, 40 + i * 2, ROOF_DARK);  // spire
  m.box(17, 26, 54, 19, 28, 59, STEEL_DARK);            // weathervane
  m.box(16, 27, 57, 20, 28, 58, STEEL_DARK);
  grimeWalls(m, 191);
  return m;
}

VoxelModel makeOnionChurch(const FactionLook&) {
  VoxelModel m(36, 36, 60);
  for (int z = 0; z < 12; ++z) m.box(6, 11, z, 26, 25, z + 1, z % 2 ? WOOD_DARK : WOOD);  // log walls
  for (int x = 9; x < 24; x += 5) {
    m.box(x, 11, 4, x + 2, 12, 9, WINDOW);
    m.box(x, 24, 4, x + 2, 25, 9, WINDOW);
  }
  steepRoof(m, 6, 11, 26, 25, 12, DEAD_WOOD, WOOD_DARK, WOOD);
  // The drum and its onion dome over the middle.
  cylinder(m, 16, 18, 3.6f, 20, 30, WOOD);
  for (int z = 0; z < 14; ++z) {
    const float t = z / 13.0f;
    const float r = t < 0.45f ? 3.6f + 2.2f * std::sin(t / 0.45f * 1.5708f) : 5.8f * std::cos((t - 0.45f) / 0.55f * 1.5708f);
    cylinder(m, 16, 18, std::max(0.6f, r), 30 + z, 31 + z, z % 3 ? PINE : PINE_DARK);
  }
  m.box(15, 18, 44, 17, 19, 50, STEEL_DARK);            // cross
  m.box(14, 18, 47, 18, 19, 48, STEEL_DARK);
  m.box(15, 18, 45, 17, 19, 46, STEEL_DARK);
  // The bell tower at the west end, with a tent roof and a little onion.
  for (int z = 0; z < 26; ++z) m.box(26, 13, z, 33, 23, z + 1, z % 2 ? WOOD_DARK : WOOD);
  m.box(27, 13, 20, 32, 14, 24, 0);
  m.box(27, 22, 20, 32, 23, 24, 0);
  for (int i = 0; i < 5; ++i) m.box(26 + i, 13 + i, 26 + i * 2, 33 - i, 23 - i, 28 + i * 2, DEAD_WOOD);
  dome(m, 29.5f, 18, 36, 1.8f, 1.8f, 3.0f, PINE);
  m.box(29, 18, 39, 30, 19, 43, STEEL_DARK);
  m.box(33, 16, 0, 34, 20, 7, WOOD_DARK);               // west door
  for (int x = 2; x < 35; x += 3) m.box(x, 33, 0, x + 1, 34, 3, DEAD_WOOD);  // churchyard fence
  m.box(2, 33, 1, 35, 34, 2, DEAD_WOOD);
  weather(m, WOOD, WOOD_DARK, DEAD_WOOD, 201, 14, 6);
  return m;
}

VoxelModel makeGrainSilo(const FactionLook&) {
  VoxelModel m(14, 14, 54);
  for (int z = 0; z < 44; ++z) cylinder(m, 7, 7, 5.0f, z, z + 1, (z % 8 == 7) ? CONCRETE_DARK : CONCRETE);
  dome(m, 7, 7, 44, 5.0f, 5.0f, 3.0f, CONCRETE_DARK);
  m.box(5, 5, 46, 10, 10, 51, CONCRETE_LIGHT);          // the head house
  m.box(6, 9, 48, 9, 10, 50, WINDOW);
  for (int z = 1; z < 46; z += 2) m.set(7, 12, z, STEEL_DARK);  // the ladder
  m.box(6, 12, 0, 9, 13, 46, 0);
  for (int z = 1; z < 46; ++z) m.set(6, 12, z, STEEL_DARK), m.set(8, 12, z, STEEL_DARK);
  weather(m, CONCRETE, CONCRETE_DARK, RUST, 211, 10, 4);
  return m;
}

VoxelModel makeWindmill(const FactionLook&) {
  VoxelModel m(16, 36, 48);
  const float cy = 18;
  // A post mill: the timber body turns on a post over a trestle.
  m.box(7, 17, 0, 9, 19, 14, WOOD_DARK);                // the post
  for (int i = 0; i < 6; ++i) {                          // quarter bars
    m.set(7 - i, 18, 6 - i, WOOD_DARK);
    m.set(8 + i, 18, 6 - i, WOOD_DARK);
    m.set(8, 17 - i, 6 - i, WOOD_DARK);
    m.set(8, 18 + i, 6 - i, WOOD_DARK);
  }
  m.box(3, 13, 12, 13, 23, 30, WOOD);
  for (int z = 12; z < 30; z += 2) m.box(3, 13, z, 13, 23, z + 1, WOOD_DARK);  // weatherboards
  m.box(3, 13, 12, 13, 23, 30, 0);
  for (int z = 12; z < 30; ++z) {
    m.box(3, 13, z, 13, 14, z + 1, z % 2 ? WOOD_DARK : WOOD);
    m.box(3, 22, z, 13, 23, z + 1, z % 2 ? WOOD_DARK : WOOD);
    m.box(3, 13, z, 4, 23, z + 1, z % 2 ? WOOD_DARK : WOOD);
    m.box(12, 13, z, 13, 23, z + 1, z % 2 ? WOOD_DARK : WOOD);
  }
  for (int i = 0; i < 6; ++i) m.box(2, 12 + i, 30 + i, 14, 24 - i, 31 + i, ROOF_DARK);  // curved roof
  m.box(1, 17, 13, 3, 19, 20, WOOD_DARK);               // tail ladder
  m.box(7, 22, 18, 9, 23, 21, WINDOW);
  // Four sails on a stock through the front, cross-wise.
  const float hx = 13.5f, hz = 26;
  cylX(m, 12, 15, cy, hz, 1.0f, WOOD_DARK);
  for (int a = 0; a < 4; ++a) {
    const float dy = (a & 1) ? 1.0f : -1.0f, dz = (a & 2) ? 1.0f : -1.0f;
    for (int r = 1; r < 15; ++r) {
      const int y = static_cast<int>(cy + dy * r * 0.707f), z = static_cast<int>(hz + dz * r * 0.707f);
      if (z < 1) continue;
      m.set(static_cast<int>(hx), y, z, WOOD_DARK);
      if (r > 3)                                          // the lattice and its sailcloth on one side
        for (int w = 1; w <= 3; ++w) {
          const int sy = static_cast<int>(cy + (dy * r - dz * w) * 0.707f), sz = static_cast<int>(hz + (dz * r + dy * w) * 0.707f);
          if (sz >= 1 && sy >= 0 && sy < 36) m.set(static_cast<int>(hx), sy, sz, (r + w) % 3 ? CANVAS : WOOD);
        }
    }
  }
  weather(m, WOOD, WOOD_DARK, DEAD_WOOD, 221, 12, 6);
  return m;
}

VoxelModel makeMarket(const FactionLook&) {
  VoxelModel m(24, 24, 14);
  m.box(1, 1, 0, 23, 23, 1, CONCRETE);                   // cobbled square
  speckle(m, CONCRETE, ROCK_DARK, CONCRETE_LIGHT, 231, 20, 12);
  cylinder(m, 12, 12, 2.4f, 1, 3, ROCK);                 // a pump trough in the middle
  m.box(12, 11, 3, 13, 12, 7, STEEL_DARK);
  const struct { int x, y; bool alongX; uint8_t a, b; } stalls[] = {
      {3, 3, true, CANVAS, MARK_RED}, {14, 3, true, CANVAS, CANVAS_DARK}, {3, 17, true, CANVAS_DARK, CANVAS}, {14, 17, true, CANVAS, MARK_RED}};
  for (const auto& st : stalls) {
    for (int px : {st.x, st.x + 6}) for (int py : {st.y, st.y + 4}) m.box(px, py, 1, px + 1, py + 1, 8, WOOD_DARK);
    m.box(st.x, st.y + 1, 1, st.x + 7, st.y + 3, 4, WOOD);  // the counter
    for (int i = 0; i < 5; ++i)
      for (int x = st.x - 1; x < st.x + 8; ++x) m.set(x, st.y - 1 + i, 9 - i / 2, ((x / 2) % 2) ? st.a : st.b);
    m.box(st.x + 1, st.y + 1, 4, st.x + 3, st.y + 3, 5, LEAF);     // greens
    m.box(st.x + 4, st.y + 1, 4, st.x + 6, st.y + 3, 5, MARK_RED);  // apples
  }
  m.box(19, 11, 1, 22, 14, 3, WOOD);                     // crates and a basket
  cylinder(m, 4, 12, 1.4f, 1, 3, DRY_GRASS);
  return m;
}

VoxelModel makeVilla(const FactionLook&) {
  VoxelModel m(36, 36, 24);
  // A wall round the plot, a house along two sides of a courtyard.
  m.box(2, 2, 0, 34, 34, 7, SAND_ROCK);
  m.box(3, 3, 0, 33, 33, 7, 0);
  m.box(15, 33, 0, 21, 34, 6, WOOD_DARK);                // the gate
  m.box(14, 33, 6, 22, 34, 8, SAND_ROCK_DARK);
  m.box(3, 3, 0, 33, 14, 12, PLASTER);                   // the main wing
  m.box(3, 3, 0, 13, 33, 12, PLASTER);                   // the side wing
  m.box(2, 2, 12, 34, 15, 13, SAND_ROCK_DARK);           // flat roofs with parapets
  m.box(2, 2, 12, 14, 34, 13, SAND_ROCK_DARK);
  for (int x = 15; x < 32; x += 4) m.box(x, 13, 0, x + 2, 14, 5, 0);   // an arcade on the courtyard
  for (int x = 15; x < 32; x += 4) m.box(x, 13, 5, x + 2, 14, 6, PLASTER_DARK);
  for (int y = 16; y < 31; y += 5) m.box(12, y, 5, 13, y + 2, 8, WINDOW);
  for (int x = 6; x < 32; x += 6) m.box(x, 3, 6, x + 2, 4, 9, WINDOW);
  m.box(26, 6, 13, 32, 12, 18, PLASTER);                 // a little roof room
  cylinder(m, 24, 24, 2.2f, 0, 2, ROCK);                 // a fountain
  m.box(29, 28, 0, 30, 29, 9, PALM_TRUNK);               // and a palm
  dome(m, 29.5f, 28.5f, 9, 3.5f, 3.5f, 2.0f, PALM);
  speckle(m, PLASTER, PLASTER_DARK, SAND_ROCK, 241, 14, 8);
  speckle(m, SAND_ROCK, SAND_ROCK_DARK, PLASTER, 242, 18, 8);
  return m;
}

VoxelModel makeStiltHut(const FactionLook&) {
  VoxelModel m(24, 24, 26);
  for (int x : {5, 12, 18}) for (int y : {6, 17}) m.box(x, y, 0, x + 1, y + 1, 7, WOOD_DARK);  // the stilts
  m.box(4, 5, 7, 20, 19, 8, DEAD_WOOD);                  // the floor
  m.box(4, 18, 7, 20, 21, 8, DEAD_WOOD);                 // and a veranda
  for (int z = 8; z < 14; ++z) m.box(5, 6, z, 19, 18, z + 1, z % 2 ? DRY_GRASS : DRY_DARK);  // woven walls
  m.box(6, 7, 8, 18, 17, 14, 0);
  m.box(11, 17, 8, 13, 18, 13, 0);                       // doorway
  m.box(6, 17, 10, 9, 18, 12, 0);
  m.box(15, 17, 10, 18, 18, 12, 0);
  for (int i = 0; i <= 8; ++i) m.box(3, 3 + i, 13 + i, 21, 21 - i, 14 + i, (i % 2) ? PALM_DARK : DRY_DARK);  // a deep thatch
  for (int i = 0; i < 6; ++i) m.box(11, 21 + i / 2, 7 - i, 13, 22 + i / 2, 8 - i, WOOD);  // ladder down
  return m;
}

// Row houses (Milestone 9.8): narrow town houses one tile wide, built wall to
// wall. A piece fills its tile to the edge on every side where another row
// house stands (`mask`: +X 1, +Y 2, -X 4, -Y 8) and shows a front or a gable
// on the others, so rows, corners and whole blocks round a courtyard grow out
// of single pieces. The ridge runs along the row; a corner house carries a
// cross-gabled roof; a taller house shows its bare party wall above a lower
// neighbour. Styles: 0 plastered, two storeys; 1 brick with a shop below,
// three storeys; 2 half-timbered under a steep roof; 3 and 4 flat-roofed
// mud brick, low and tall. 12 voxels a tile like the other buildings, in a
// 16 x 16 grid (the tile is 2..14) so eaves and steps have room.
VoxelModel makeRowHouse(int style, int mask) {
  constexpr int E0 = 2, E1 = 14;
  const bool jx1 = mask & 1, jy1 = mask & 2, jx0 = mask & 4, jy0 = mask & 8;
  const bool adobe = style >= 3;
  const int storeys = style == 1 ? 3 : style == 4 ? 2 : style == 3 ? 1 : 2;
  const int sh = adobe ? 7 : style == 2 ? 6 : 7;        // storey height
  const int h = storeys * sh + (adobe ? 2 : 0);
  const uint8_t wall = style == 1 ? BRICK : adobe ? SAND_ROCK : PLASTER;
  const uint8_t trim = style == 1 ? BRICK_DARK : adobe ? SAND_ROCK_DARK : style == 2 ? WOOD_DARK : PLASTER_DARK;
  VoxelModel m(16, 16, h + 16);
  // Walls: out to the tile's edge where a neighbour stands, a voxel in where the street is.
  const int x0 = jx0 ? E0 : E0 + 1, x1 = jx1 ? E1 : E1 - 1, y0 = jy0 ? E0 : E0 + 1, y1 = jy1 ? E1 : E1 - 1;
  m.box(x0, y0, 0, x1, y1, h, wall);
  m.box(x0, y0, 0, x1, y1, 1, trim);  // plinth

  // A front: door and window below, two windows to every floor above. `put`
  // maps (along the wall, up) to the voxel on that face.
  auto face = [&](int side, bool front) {
    const bool alongX = side == 1 || side == 3;  // the +Y and -Y faces run along X
    const int a0 = alongX ? x0 : y0, a1 = alongX ? x1 : y1;
    const int fixed = side == 0 ? x1 - 1 : side == 1 ? y1 - 1 : side == 2 ? x0 : y0;
    auto put = [&](int a, int z, uint8_t c) {
      if (a < a0 || a >= a1 || z < 0) return;
      if (alongX) m.set(a, fixed, z, c);
      else m.set(fixed, a, z, c);
    };
    const int w = a1 - a0, left = a0 + 1 + (w > 10 ? 1 : 0), right = a1 - 3 - (w > 10 ? 1 : 0);
    for (int s = 0; s < storeys; ++s) {
      const int z = s * sh + (adobe ? 3 : 2);
      const bool shop = style == 1 && s == 0 && front;
      for (int a : {left, right}) {
        if (s == 0 && front && a == left) {
          for (int dz = 1; dz <= (adobe ? 4 : 5); ++dz) put(a, dz, WOOD_DARK), put(a + 1, dz, WOOD_DARK);  // the door
          continue;
        }
        const int top = adobe ? 2 : 3;
        for (int dz = 0; dz < top; ++dz) put(a, z + dz, WINDOW), put(a + 1, z + dz, WINDOW);
        if (shop) {
          for (int dz = -1; dz < top + 1; ++dz)
            for (int da = -2; da <= 1; ++da) put(a + da, z + dz, WINDOW);  // a shop window
        } else if (!adobe) {
          put(a, z - 1, trim), put(a + 1, z - 1, trim);  // sill
        }
      }
      if (shop)
        for (int a = a0; a < a1; ++a) put(a, sh - 1, CONCRETE_DARK);  // the board over the shop
    }
    if (adobe && front)
      for (int a = a0 + 2; a < a1 - 1; a += 3) {  // roof beams poking out of the wall
        const int ox = side == 0 ? x1 : side == 2 ? x0 - 1 : a, oy = side == 1 ? y1 : side == 3 ? y0 - 1 : a;
        m.set(ox, oy, h - 3, WOOD);
      }
  };
  if (!jx1) face(0, true);
  if (!jy1) face(1, true);
  if (!jx0) face(2, false);
  if (!jy0) face(3, false);
  if (style == 2) timberFrame(m, x0, y0, x1, y1, 1, h);

  if (adobe) {
    // A flat roof behind a low parapet (none where the next roof carries on).
    m.box(x0, y0, h - 1, x1, y1, h, SAND_ROCK_DARK);
    if (!jx0) m.box(x0, y0, h, x0 + 1, y1, h + 1, SAND_ROCK);
    if (!jx1) m.box(x1 - 1, y0, h, x1, y1, h + 1, SAND_ROCK);
    if (!jy0) m.box(x0, y0, h, x1, y0 + 1, h + 1, SAND_ROCK);
    if (!jy1) m.box(x0, y1 - 1, h, x1, y1, h + 1, SAND_ROCK);
    if (style == 4) m.box(x0 + 2, y0 + 2, h, x0 + 6, y0 + 6, h + 3, SAND_ROCK);  // a stair house up on the roof
    speckle(m, SAND_ROCK, SAND_ROCK_DARK, PLASTER, 61u + static_cast<uint32_t>(style * 16 + mask), 18, 8);
    return m;
  }

  // Pitched roofs. Along X when the row runs that way (or the house stands
  // alone), along Y when it runs that way, both for a corner.
  const bool rowX = jx0 || jx1 || !(jy0 || jy1), rowY = jy0 || jy1;
  const int pitch = style == 2 ? 2 : 1;
  const uint8_t ra = style == 1 ? ROOF : TILE_ROOF, rb = style == 1 ? ROOF_DARK : TILE_ROOF_DARK;
  auto roof = [&](bool alongX) {
    // Across the ridge: from eave to eave (a voxel over the wall on open sides).
    const int c0 = alongX ? y0 - (jy0 ? 0 : 1) : x0 - (jx0 ? 0 : 1), c1 = alongX ? y1 + (jy1 ? 0 : 1) : x1 + (jx1 ? 0 : 1);
    // Along it: to the tile's edge at a neighbour, a voxel over the gable otherwise.
    const int l0 = alongX ? (jx0 ? E0 : x0 - 1) : (jy0 ? E0 : y0 - 1), l1 = alongX ? (jx1 ? E1 : x1 + 1) : (jy1 ? E1 : y1 + 1);
    const int w0 = alongX ? x0 : y0, w1 = alongX ? x1 : y1;  // the end walls
    const int half = (c1 - c0 + 1) / 2;
    for (int i = 0; i < half; ++i) {
      const int a = c0 + i, b = c1 - i, z = h + i * pitch;
      const uint8_t c = (i % 2) ? ra : rb;
      if (alongX) {
        m.box(l0, a, z, l1, b, z + pitch, c);
        // Gables and party walls: the wall carries on up under the roof at both ends.
        if (b - a > 2) m.box(w0, a + 1, z, w0 + 1, b - 1, z + pitch, wall), m.box(w1 - 1, a + 1, z, w1, b - 1, z + pitch, wall);
      } else {
        m.box(a, l0, z, b, l1, z + pitch, c);
        if (b - a > 2) m.box(a + 1, w0, z, b - 1, w0 + 1, z + pitch, wall), m.box(a + 1, w1 - 1, z, b - 1, w1, z + pitch, wall);
      }
    }
    return h + half * pitch;
  };
  int top = h;
  if (rowX) top = std::max(top, roof(true));
  if (rowY) top = std::max(top, roof(false));
  // A window in an open gable.
  if (rowX && !jx0) m.box(x0, 7, h + 1, x0 + 1, 9, h + 1 + pitch, WINDOW);
  if (rowX && !jx1) m.box(x1 - 1, 7, h + 1, x1, 9, h + 1 + pitch, WINDOW);
  if (rowY && !jy0) m.box(7, y0, h + 1, 9, y0 + 1, h + 1 + pitch, WINDOW);
  if (rowY && !jy1) m.box(7, y1 - 1, h + 1, 9, y1, h + 1 + pitch, WINDOW);
  // The chimney stack stands on the party wall (shared with the neighbour), or at one end.
  {
    const int cx = rowX ? (jx1 ? E1 - 2 : x1 - 3) : 7, cy = rowX ? 7 : (jy1 ? E1 - 2 : y1 - 3);
    m.box(cx, cy, top - 2 * pitch, cx + 2, cy + 2, top + 2, BRICK);
    m.box(cx, cy, top + 1, cx + 2, cy + 2, top + 2, BRICK_DARK);
  }
  if (style == 1 && !jy1) m.box(x0 + 1, y1, 0, x0 + 4, y1 + 1, 1, CONCRETE);  // a step up to the shop
  grimeWalls(m, 200u + static_cast<uint32_t>(style * 16 + mask));
  return m;
}

// For the editor's list and the encyclopedia: a house standing alone.
VoxelModel makeRowPlaster(const FactionLook&) { return makeRowHouse(0, 0); }
VoxelModel makeRowBrick(const FactionLook&) { return makeRowHouse(1, 0); }
VoxelModel makeRowTimber(const FactionLook&) { return makeRowHouse(2, 0); }
VoxelModel makeRowAdobe(const FactionLook&) { return makeRowHouse(3, 0); }
VoxelModel makeRowAdobeTall(const FactionLook&) { return makeRowHouse(4, 0); }

// Bridges, at 24 voxels a tile like the other field works. The deck is level
// across the whole bridge; its top is kBridgeDeckZ voxels above the sprite's
// foot, piers and arches (or timber trestles) reaching kBridgePierDepth below
// the deck line, down into the water or the bank. Pieces join with their
// neighbours; an end piece runs a ramp down to the bank.
VoxelModel makeBridgeSpan(int mask, bool wood) {
  constexpr int D = kBridgePierDepth, TOP = kBridgeDeckZ;
  VoxelModel m(24, 24, TOP + 4);
  static constexpr int kDir[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  int arms = 0;
  for (int a = 0; a < 4; ++a) arms += (mask >> a) & 1;
  if (mask == 0) mask = 5;
  int ramp = -1;  // a lone end: the far side ramps down to the bank
  if (arms == 1)
    for (int a = 0; a < 4; ++a)
      if (mask & (1 << a)) ramp = (a + 2) % 4;
  const uint8_t deck = wood ? WOOD : CONCRETE, deckDark = wood ? WOOD_DARK : CONCRETE_DARK;
  auto put = [&](int x, int y, int z, uint8_t c) {
    if (x >= 0 && y >= 0 && x < 24 && y < 24 && z >= 0 && z < TOP + 4) m.set(x, y, z, c);
  };
  for (int a = 0; a < 4; ++a) {
    const bool isRamp = a == ramp;
    if (!(mask & (1 << a)) && !isRamp) continue;
    const int dx = kDir[a][0], dy = kDir[a][1];
    for (int u = 0; u <= 12; ++u)
      for (int v = -7; v < 7; ++v) {
        const int x = 12 + dx * u - dy * v - (dx < 0 ? 1 : 0);
        const int y = 12 + dy * u + dx * v - (dy < 0 ? 1 : 0);
        const int top = isRamp ? TOP - u / 2 : TOP;
        const bool edge = v == -7 || v == 6;
        const uint8_t dc = wood ? (((dx ? x : y) / 2) % 2 ? deck : deckDark) : (((x * 3 + y) % 7) ? deck : deckDark);
        put(x, y, top - 1, dc);
        put(x, y, top - 2, wood ? WOOD_DARK : ROCK);
        if (edge) {  // parapets or rails
          if (wood) {
            if (u % 4 == 0) for (int z = top; z < top + 3; ++z) put(x, y, z, WOOD_DARK);
            put(x, y, top + 2, WOOD);
          } else {
            put(x, y, top, ROCK);
            put(x, y, top + 1, (u % 3) ? ROCK : ROCK_DARK);
          }
        }
        if (isRamp) {
          // The approach: solid masonry, or timber cribbing, down into the bank.
          if (!wood) for (int z = 0; z < top - 2; ++z) put(x, y, z, z % 4 == 3 ? ROCK : ROCK_DARK);
          else if (u % 4 == 1 && (v == -6 || v == 5)) for (int z = 0; z < top - 2; ++z) put(x, y, z, WOOD_DARK);
          continue;
        }
        if (wood) {
          // Trestles: posts at each end of the piece, braced.
          if ((u == 2 || u == 11) && (v == -6 || v == -1 || v == 4))
            for (int z = 0; z < TOP - 2; ++z) put(x, y, z, WOOD_DARK);
          if ((u == 2 || u == 11) && v > -7 && v < 6)
            for (int z : {TOP - 3, TOP - 9, TOP - 15}) put(x, y, z, WOOD_DARK);
          if ((u == 2 || u == 11) && v > -7 && v < 6) put(x, y, std::abs((v + 7) % 8 - 4) * 3 + 2, WOOD_DARK);  // X braces
        } else {
          // A pier under the middle, arches springing from it to the next.
          const float t = std::clamp((u - 3) / 9.0f, 0.0f, 1.0f);
          const float open = u < 3 ? 0.0f : (D + 2.5f) * std::sqrt(std::max(0.0f, 1.0f - (1.0f - t) * (1.0f - t)));
          for (int z = static_cast<int>(open); z < TOP - 2; ++z)
            put(x, y, z, edge || u < 3 ? (z % 4 == 3 ? ROCK : ROCK_DARK) : ROCK_DARK);
        }
      }
  }
  if (!wood) {
    // The pier's cutwaters, pointed up and down stream.
    const bool alongX = (mask & 5) != 0;
    for (int k = 0; k < 3; ++k)
      for (int z = 0; z < TOP - 4; ++z) {
        if (alongX) {
          for (int x = 11 - k; x < 13 + k; ++x) put(x, 4 + k, z, ROCK_DARK), put(x, 19 - k, z, ROCK_DARK);
        } else {
          for (int y = 11 - k; y < 13 + k; ++y) put(4 + k, y, z, ROCK_DARK), put(19 - k, y, z, ROCK_DARK);
        }
      }
    speckle(m, ROCK, ROCK_DARK, ROCK_LIGHT, 251u + static_cast<uint32_t>(mask), 18, 8);
  } else {
    weather(m, WOOD, WOOD_DARK, DEAD_WOOD, 252u + static_cast<uint32_t>(mask), 14, 6);
  }
  return m;
}
}  // namespace slave::art

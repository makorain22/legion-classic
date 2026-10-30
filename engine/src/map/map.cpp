#include "slave/map/map.hpp"

#include "slave/core/rng.hpp"

namespace slave {

namespace {
// Integer value noise in 0..1023 with bilinear smoothing. Deterministic.
int32_t valueNoise(int32_t x, int32_t y, int32_t cell, uint32_t seed) {
  const int32_t cx = x / cell, cy = y / cell;
  const int32_t fx = (x % cell) * 1024 / cell, fy = (y % cell) * 1024 / cell;
  auto v = [&](int32_t ix, int32_t iy) { return static_cast<int32_t>(hash2(ix, iy, seed) & 1023); };
  const int32_t a = v(cx, cy), b = v(cx + 1, cy), c = v(cx, cy + 1), d = v(cx + 1, cy + 1);
  const int32_t top = a + (b - a) * fx / 1024;
  const int32_t bot = c + (d - c) * fx / 1024;
  return top + (bot - top) * fy / 1024;
}

int32_t fractalNoise(int32_t x, int32_t y, uint32_t seed) {
  return (valueNoise(x, y, 16, seed) * 4 + valueNoise(x, y, 8, seed + 1) * 2 +
          valueNoise(x, y, 4, seed + 2)) / 7;
}
}  // namespace

void Map::resize(int width, int height) {
  width_ = width;
  height_ = height;
  tiles_.assign(static_cast<size_t>(width) * height, Tile{});
  props_.clear();
}

void Map::generateCountryside(uint64_t seed) {
  Rng rng(seed);
  const uint32_t s = static_cast<uint32_t>(seed);
  props_.clear();

  for (int y = 0; y < height_; ++y) {
    for (int x = 0; x < width_; ++x) {
      Tile& t = at(x, y);
      const int32_t ground = fractalNoise(x, y, s);
      const int32_t lush = fractalNoise(x + 500, y + 500, s + 7);
      if (ground < 300)
        t.terrain = Terrain::Dirt;
      else if (lush > 600)
        t.terrain = Terrain::Meadow;
      else
        t.terrain = Terrain::Grass;
      t.variant = static_cast<uint8_t>(hash2(x, y, s + 99) & 3);
    }
  }

  // Woods where the "forest" noise is high, scattered trees elsewhere.
  for (int y = 1; y < height_ - 1; ++y) {
    for (int x = 1; x < width_ - 1; ++x) {
      if (at(x, y).terrain == Terrain::Dirt) continue;
      const int32_t forest = fractalNoise(x + 1000, y + 1000, s + 13);
      uint32_t chance = forest > 720 ? 55 : (forest > 620 ? 12 : 1);
      if (rng.below(100) >= chance) {
        if (rng.below(400) == 0) {
          props_.push_back({PropKind::Rock, static_cast<uint8_t>(rng.below(3)),
                            x * LEPTONS_PER_TILE + rng.range(64, 192),
                            y * LEPTONS_PER_TILE + rng.range(64, 192)});
        }
        continue;
      }
      const PropKind kind = rng.below(100) < 35 ? PropKind::Pine
                            : rng.below(100) < 15 ? PropKind::Bush
                                                  : PropKind::Tree;
      props_.push_back({kind, static_cast<uint8_t>(rng.below(4)),
                        x * LEPTONS_PER_TILE + rng.range(48, 208),
                        y * LEPTONS_PER_TILE + rng.range(48, 208)});
    }
  }
}

}  // namespace slave

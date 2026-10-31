#include "slave/map/map.hpp"

#include <algorithm>
#include <cmath>

#include "slave/core/rng.hpp"

namespace slave {

const char* layerName(Layer l) {
  static const char* names[] = {"Grass", "Dry grass", "Dirt", "Mud", "Sand", "Rock", "Snow", "Dirt road"};
  return names[static_cast<int>(l)];
}

const char* propKindName(PropKind k) {
  static const char* names[] = {"Oak tree", "Pine tree", "Bush", "Rock", "Snowy pine", "Dead tree",
                                "Palm", "Scrub", "Jungle tree", "Debris", "Sandstone",
                                "Tree stump", "Log pile", "Haystack", "Crates and barrels", "Fence"};
  return names[static_cast<int>(k)];
}

const char* propKindId(PropKind k) {
  static const char* ids[] = {"tree", "pine", "bush", "rock", "snow_pine", "dead_tree",
                              "palm", "scrub", "jungle_tree", "debris", "sandstone",
                              "stump", "logs", "haystack", "crates", "fence"};
  return ids[static_cast<int>(k) % static_cast<int>(PropKind::Count)];
}

namespace {
// Integer value noise in 0..1023 with bilinear smoothing. Deterministic.
int32_t valueNoise(int32_t x, int32_t y, int32_t cell, uint32_t seed) {
  const int32_t cx = x / cell, cy = y / cell;
  const int32_t fx = (x % cell) * 1024 / cell, fy = (y % cell) * 1024 / cell;
  // Smoothstep the fractions (integer) for rounder hills.
  auto smooth = [](int32_t f) { return f * f * (3072 - 2 * f) / (1024 * 1024); };
  const int32_t sx = smooth(fx), sy = smooth(fy);
  auto v = [&](int32_t ix, int32_t iy) { return static_cast<int32_t>(hash2(ix, iy, seed) & 1023); };
  const int32_t a = v(cx, cy), b = v(cx + 1, cy), c = v(cx, cy + 1), d = v(cx + 1, cy + 1);
  const int32_t top = a + (b - a) * sx / 1024;
  const int32_t bot = c + (d - c) * sx / 1024;
  return top + (bot - top) * sy / 1024;
}

int32_t fractal(int32_t x, int32_t y, int32_t baseCell, uint32_t seed) {
  return (valueNoise(x, y, baseCell, seed) * 4 + valueNoise(x, y, std::max(2, baseCell / 2), seed + 1) * 2 +
          valueNoise(x, y, std::max(2, baseCell / 4), seed + 2)) /
         7;
}

float smoothstep(float a, float b, float x) {
  const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}
}  // namespace

int Map::roundSize(int s) {
  s = std::clamp(s, MAP_MIN_SIZE, MAP_MAX_SIZE);
  return (s + MAP_SIZE_STEP / 2) / MAP_SIZE_STEP * MAP_SIZE_STEP;
}

void Map::resize(int width, int height, uint16_t baseHeight) {
  width_ = roundSize(width);
  height_ = roundSize(height);
  heights_.assign(static_cast<size_t>(cornersX()) * cornersY(), baseHeight);
  splat_.assign(static_cast<size_t>(splatW()) * splatH() * LAYER_COUNT, 0);
  fillLayer(Layer::Grass);
  info_.assign(static_cast<size_t>(width_) * height_, TileInfo{});
  blocks_.assign(static_cast<size_t>(width_) * height_, 0);
  oil_.assign(static_cast<size_t>(width_) * height_, 0);
  props_.clear();
  waterLevel_ = 0;
  recomputeInfo();
}

uint16_t Map::maxHeight() const {
  return heights_.empty() ? 0 : *std::max_element(heights_.begin(), heights_.end());
}

int32_t Map::heightAtFine(int32_t lx, int32_t ly) const {
  lx = std::clamp(lx, 0, widthLeptons() - 1);
  ly = std::clamp(ly, 0, heightLeptons() - 1);
  const int tx = lx >> LEPTON_SHIFT, ty = ly >> LEPTON_SHIFT;
  const int64_t fx = lx & 255, fy = ly & 255;
  const int64_t h00 = corner(tx, ty), h10 = corner(tx + 1, ty);
  const int64_t h01 = corner(tx, ty + 1), h11 = corner(tx + 1, ty + 1);
  const int64_t top = h00 * 256 + (h10 - h00) * fx;
  const int64_t bot = h01 * 256 + (h11 - h01) * fx;
  return static_cast<int32_t>(top + (bot - top) * fy / 256);
}

float Map::heightAt(float lx, float ly) const {
  const float fx = std::clamp(lx / LEPTONS_PER_TILE, 0.0f, static_cast<float>(width_) - 0.001f);
  const float fy = std::clamp(ly / LEPTONS_PER_TILE, 0.0f, static_cast<float>(height_) - 0.001f);
  const int tx = static_cast<int>(fx), ty = static_cast<int>(fy);
  const float ax = fx - tx, ay = fy - ty;
  const float top = corner(tx, ty) + (corner(tx + 1, ty) - static_cast<float>(corner(tx, ty))) * ax;
  const float bot = corner(tx, ty + 1) + (corner(tx + 1, ty + 1) - static_cast<float>(corner(tx, ty + 1))) * ax;
  return top + (bot - top) * ay;
}

void Map::paint(int sx, int sy, Layer layer, int amount) {
  if (sx < 0 || sy < 0 || sx >= splatW() || sy >= splatH() || amount <= 0) return;
  uint8_t* w = splat(sx, sy);
  const int li = static_cast<int>(layer);
  const int cur = w[li];
  const int target = std::min(255, cur + amount);
  const int othersBefore = 255 - cur;
  const int othersAfter = 255 - target;
  int sum = target;
  for (int i = 0; i < LAYER_COUNT; ++i) {
    if (i == li) continue;
    w[i] = othersBefore > 0 ? static_cast<uint8_t>(w[i] * othersAfter / othersBefore) : 0;
    sum += w[i];
  }
  w[li] = static_cast<uint8_t>(target + (255 - sum));  // absorb rounding so the total is exactly 255
}

void Map::fillLayer(Layer layer) {
  for (size_t i = 0; i < splat_.size(); i += LAYER_COUNT) {
    std::fill_n(&splat_[i], LAYER_COUNT, uint8_t{0});
    splat_[i + static_cast<size_t>(layer)] = 255;
  }
}

Layer Map::dominantLayer(int sx, int sy) const {
  const uint8_t* w = splat(sx, sy);
  return static_cast<Layer>(std::max_element(w, w + LAYER_COUNT) - w);
}

void Map::recomputeInfo(int tx0, int ty0, int tx1, int ty1) {
  ++revision_;
  tx0 = std::max(0, tx0);
  ty0 = std::max(0, ty0);
  tx1 = std::min(width_ - 1, tx1);
  ty1 = std::min(height_ - 1, ty1);
  for (int ty = ty0; ty <= ty1; ++ty)
    for (int tx = tx0; tx <= tx1; ++tx) {
      const int a = corner(tx, ty), b = corner(tx + 1, ty), c = corner(tx, ty + 1), d = corner(tx + 1, ty + 1);
      TileInfo& t = info_[static_cast<size_t>(ty) * width_ + tx];
      t.slope = static_cast<uint16_t>(std::max({a, b, c, d}) - std::min({a, b, c, d}));
      const int avg = (a + b + c + d) / 4;
      t.waterDepth = waterLevel_ ? static_cast<int16_t>(std::clamp(int(waterLevel_) - avg, -30000, 30000)) : int16_t(-30000);
    }
}

bool Map::passable(int tx, int ty, Locomotor loco) const {
  if (!inBounds(tx, ty)) return false;
  const TileInfo& t = info(tx, ty);
  const int li = static_cast<int>(loco);
  return t.slope <= rules_.maxSlope[li] && t.waterDepth <= rules_.maxWaterDepth[li] &&
         !(blocks_[static_cast<size_t>(ty) * width_ + tx] & (1u << li));
}

const char* biomeName(Biome b) {
  static const char* names[] = {"Temperate", "Winter", "Desert", "Tropical", "Urban"};
  return names[static_cast<int>(b) % static_cast<int>(Biome::Count)];
}
const char* biomeId(Biome b) {
  static const char* ids[] = {"temperate", "winter", "desert", "tropical", "urban"};
  return ids[static_cast<int>(b) % static_cast<int>(Biome::Count)];
}
bool parseBiome(const std::string& s, Biome& out) {
  for (int i = 0; i < static_cast<int>(Biome::Count); ++i)
    if (s == biomeId(static_cast<Biome>(i))) {
      out = static_cast<Biome>(i);
      return true;
    }
  return false;
}

void Map::generate(Biome b, uint64_t seed) { generateTerrain(seed, b); }

bool Map::buildable(int tx, int ty) const {
  if (!inBounds(tx, ty)) return false;
  const TileInfo& t = info(tx, ty);
  return t.slope <= 24 && t.waterDepth < -4 && !(block(tx, ty) & kOccupied);
}

void Map::addOilField(int cx, int cy, int radius, uint32_t seed) {
  for (int y = cy - radius; y <= cy + radius; ++y)
    for (int x = cx - radius; x <= cx + radius; ++x) {
      if (!inBounds(x, y) || info(x, y).waterDepth > -4) continue;
      const int dx = x - cx, dy = y - cy;
      const int d2 = dx * dx + dy * dy;
      const int r2 = radius * radius;
      // Ragged edge: each tile's own random threshold.
      const int edge = r2 - static_cast<int>(hash2(x, y, seed) % static_cast<uint32_t>(std::max(1, r2 / 2)));
      if (d2 > edge) continue;
      const int amount = 90 + (165 * (r2 - d2)) / std::max(1, r2) + static_cast<int>(hash2(y, x, seed) % 30);
      setOil(x, y, static_cast<uint8_t>(std::clamp(amount, 0, 255)));
    }
}

// ---------------------------------------------------------------------------
// Random countryside in the Blitzkrieg spirit: rolling fields, a winding
// river with muddy banks, dirt roads that ford it, woods and patchy ground.
// Heights use integer maths only, so every PC generates the same terrain.
// ---------------------------------------------------------------------------
void Map::generateCountryside(uint64_t seed) { generateTerrain(seed, Biome::Temperate); }

void Map::generateTerrain(uint64_t seed, Biome b) {
  biome = b;
  lots.clear();
  const bool winter = b == Biome::Winter, desert = b == Biome::Desert, tropical = b == Biome::Tropical,
             urban = b == Biome::Urban;
  Rng rng(seed);
  const uint32_t s = static_cast<uint32_t>(seed);
  const int W = width_, H = height_;
  props_.clear();

  // --- Rolling hills -----------------------------------------------------
  for (int cy = 0; cy < cornersY(); ++cy)
    for (int cx = 0; cx < cornersX(); ++cx) {
      const int32_t n = fractal(cx, cy, 48, s);  // 0..1023
      // Deserts roll in long dunes; towns sit on flatter ground.
      const int32_t amp = urban ? 90 : desert ? 170 : tropical ? 130 : 150;
      int32_t h = 430 + (n - 512) * amp / 512;
      if (desert) h += (valueNoise(cx * 3, cy, 22, s + 77) - 512) * 30 / 512;  // dune ridges
      setCorner(cx, cy, static_cast<uint16_t>(h));
    }
  waterLevel_ = 330;

  // --- River (west to east) ------------------------------------------------
  const int32_t riverY0 = H * 3 / 10 + static_cast<int32_t>(rng.below(static_cast<uint32_t>(H * 4 / 10)));
  auto riverY = [&](int32_t x) { return riverY0 * 256 + (valueNoise(x, 0, 40, s + 31) - 512) * H * 40 / 1024; };  // x256
  // Desert rivers are narrow; jungle rivers broad and slow.
  const int32_t riverScale = desert ? 160 : tropical ? 360 : 256;
  auto riverHalfWidth = [&](int32_t x) {
    return (2 * 256 + valueNoise(x, 7, 30, s + 37) * 256 / 512) * riverScale / 256;
  };  // x256
  // --- Two dirt roads (north to south) -----------------------------------
  auto roadX = [&](int road, int32_t y) {
    const int32_t base = (road == 0 ? W * 26 / 100 : W * 72 / 100) * 256;
    return base + (valueNoise(y, road * 50, 36, s + 41 + road) - 512) * W * 20 / 1024;
  };

  const int32_t wl = waterLevel_;
  constexpr int32_t kValley = 12 * 256;  // banks rise over 12 tiles: no cliffs
  for (int cy = 0; cy < cornersY(); ++cy)
    for (int cx = 0; cx < cornersX(); ++cx) {
      int32_t h = corner(cx, cy);
      const int32_t d = std::abs(cy * 256 - riverY(cx));  // distance from the river centre, x256
      const int32_t hw = riverHalfWidth(cx);
      if (d < hw) {
        // River bed: a continuous bowl, shallow at the edge, deep in the middle.
        const int64_t t = static_cast<int64_t>(d) * 1024 / hw;  // 0..1024
        const int32_t bed = wl - 4 - static_cast<int32_t>(38 * (1024 * 1024 - t * t) / (1024 * 1024));
        h = std::min(h, bed);
      } else if (d < hw + kValley) {
        // Valley sides ease from the water's edge up to the original ground.
        int64_t t = static_cast<int64_t>(d - hw) * 1024 / kValley;  // 0..1024
        t = 1024 - (1024 - t) * (1024 - t) / 1024;                  // ease-out: steepest by the water
        const int32_t valley = static_cast<int32_t>((wl - 4) + (static_cast<int64_t>(h) - (wl - 4)) * t / 1024);
        h = std::min(h, valley);
      }
      // Roads cross the river on a shallow ford.
      for (int r = 0; r < 2; ++r) {
        const int32_t rd = std::abs(cx * 256 - roadX(r, cy));
        if (rd < 3 * 256 && h < wl + 3) {
          const int32_t raise = wl + 3 - rd * rd * 4 / (256 * 256);
          h = std::max(h, raise);
        }
      }
      setCorner(cx, cy, static_cast<uint16_t>(std::clamp(h, 0, 65535)));
    }

  // Two gentle smoothing passes remove any leftover ridges from the carving.
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<uint16_t> src = heights_;
    for (int cy = 1; cy < cornersY() - 1; ++cy)
      for (int cx = 1; cx < cornersX() - 1; ++cx) {
        int32_t sum = 0;
        for (int oy = -1; oy <= 1; ++oy)
          for (int ox = -1; ox <= 1; ++ox)
            sum += src[static_cast<size_t>(cy + oy) * cornersX() + cx + ox] * ((ox == 0 && oy == 0) ? 4 : 1);
        setCorner(cx, cy, static_cast<uint16_t>(sum / 12));
      }
  }
  recomputeInfo();

  // --- Ground layers --------------------------------------------------------
  for (int sy = 0; sy < splatH(); ++sy)
    for (int sx = 0; sx < splatW(); ++sx) {
      const float tx = (sx + 0.5f) / SPLAT_PER_TILE, ty = (sy + 0.5f) / SPLAT_PER_TILE;
      const int itx = std::min(W - 1, static_cast<int>(tx)), ity = std::min(H - 1, static_cast<int>(ty));
      float w[LAYER_COUNT] = {};
      const float h = heightAt(tx * LEPTONS_PER_TILE, ty * LEPTONS_PER_TILE);
      const float above = h - waterLevel_;
      const float dry = fractal(sx, sy, 40, s + 101) / 1023.0f;
      const float dirt = fractal(sx, sy, 18, s + 103) / 1023.0f;
      const float mudNoise = valueNoise(sx, sy, 5, s + 109) / 1023.0f;
      const float sandNoise = valueNoise(sx, sy, 6, s + 107) / 1023.0f;
      const float slopeK = smoothstep(24.0f, 40.0f, static_cast<float>(info(itx, ity).slope));
      const float shoreMud = 1.0f - smoothstep(-4.0f, 10.0f + mudNoise * 14.0f, above);
      if (winter) {
        // Snow over everything; thawed, trampled patches and frozen mud by the water.
        w[int(Layer::Snow)] = 1.4f;
        w[int(Layer::DryGrass)] = smoothstep(0.66f, 0.8f, dry) * 1.3f;
        w[int(Layer::Dirt)] = smoothstep(0.72f, 0.82f, dirt) * 1.2f;
        w[int(Layer::Mud)] = shoreMud * 1.6f;
        w[int(Layer::Rock)] = slopeK * 2.2f;
      } else if (desert) {
        // Sand and gravel, scrubby patches, bare rock outcrops.
        w[int(Layer::Sand)] = 1.5f;
        w[int(Layer::DryGrass)] = smoothstep(0.62f, 0.78f, dry) * 1.1f;
        w[int(Layer::Dirt)] = smoothstep(0.6f, 0.74f, dirt) * 1.3f;
        w[int(Layer::Rock)] = slopeK * 3.0f + smoothstep(0.74f, 0.84f, fractal(sx, sy, 24, s + 111) / 1023.0f) * 2.0f;
        w[int(Layer::Grass)] = shoreMud * 1.6f;  // an oasis green along the water
        w[int(Layer::Mud)] = shoreMud * 0.6f;
      } else if (tropical) {
        // Lush green, wet red earth, mud everywhere near water.
        w[int(Layer::Grass)] = 1.2f;
        w[int(Layer::Dirt)] = smoothstep(0.6f, 0.72f, dirt) * 1.4f;
        w[int(Layer::Mud)] = shoreMud * 3.0f + smoothstep(0.7f, 0.85f, mudNoise) * 0.8f;
        w[int(Layer::Rock)] = slopeK * 2.4f;
      } else if (urban) {
        // Rubble-strewn earth, weedy lots, little parks.
        w[int(Layer::Dirt)] = 1.1f;
        w[int(Layer::Grass)] = smoothstep(0.55f, 0.7f, dry) * 1.4f;
        w[int(Layer::DryGrass)] = smoothstep(0.45f, 0.6f, dirt) * 0.9f;
        w[int(Layer::Mud)] = shoreMud * 2.0f;
        w[int(Layer::Rock)] = slopeK * 3.0f + smoothstep(0.7f, 0.8f, mudNoise) * 0.9f;
      } else {
        w[int(Layer::Grass)] = 1.0f;
        w[int(Layer::DryGrass)] = smoothstep(0.58f, 0.72f, dry) * 1.0f;
        w[int(Layer::Dirt)] = smoothstep(0.66f, 0.76f, dirt) * 1.6f;
        w[int(Layer::Mud)] = 2.2f * shoreMud;
        w[int(Layer::Sand)] = (1.0f - smoothstep(2.0f, 10.0f, std::abs(above - 6.0f))) * smoothstep(0.55f, 0.8f, sandNoise) * 1.4f;
        w[int(Layer::Rock)] = slopeK * 3.0f;
      }

      for (int r = 0; r < 2; ++r) {
        const float rd = std::abs(tx * 256.0f - roadX(r, static_cast<int32_t>(ty))) / 256.0f;
        w[int(Layer::Road)] = std::max(w[int(Layer::Road)], (1.0f - smoothstep(0.45f, 0.95f, rd)) * 4.0f);
      }
      if (urban) {
        // A grid of streets every 12 tiles.
        const float gx = std::fmod(tx + 6.0f, 12.0f), gy = std::fmod(ty + 6.0f, 12.0f);
        const float sd = std::min(std::abs(gx - 6.0f), std::abs(gy - 6.0f));
        w[int(Layer::Road)] = std::max(w[int(Layer::Road)], (1.0f - smoothstep(0.5f, 1.0f, sd)) * 4.0f);
      }
      float sum = 0;
      for (float v : w) sum += v;
      uint8_t* out = splat(sx, sy);
      int total = 0;
      for (int i = 0; i < LAYER_COUNT; ++i) {
        out[i] = static_cast<uint8_t>(w[i] / sum * 255.0f);
        total += out[i];
      }
      out[int(dominantLayer(sx, sy))] += static_cast<uint8_t>(255 - total);
    }

  // --- Woods, hedges and rocks ---------------------------------------------
  auto nearRoad = [&](int x, int y) {
    for (int r = 0; r < 2; ++r)
      if (std::abs(x * 256 + 128 - roadX(r, y)) < 3 * 256) return true;
    return false;
  };
  for (int y = 1; y < H - 1; ++y)
    for (int x = 1; x < W - 1; ++x) {
      const TileInfo& t = info(x, y);
      if (t.waterDepth > -14 || nearRoad(x, y)) continue;
      if (t.slope > 26) {
        if (rng.below(6) == 0)
          props_.push_back({PropKind::Rock, static_cast<uint8_t>(rng.below(3)), x * 256 + rng.range(40, 216),
                            y * 256 + rng.range(40, 216)});
        continue;
      }
      const int32_t forest = fractal(x + 1000, y + 1000, 28, s + 13);
      const bool nearWater = t.waterDepth > -40;
      auto put = [&](PropKind kind, uint32_t variants) {
        props_.push_back({kind, static_cast<uint8_t>(rng.below(variants)), x * 256 + rng.range(40, 216),
                          y * 256 + rng.range(40, 216)});
      };
      const uint32_t pick = rng.below(100);
      if (winter) {
        const uint32_t chance = forest > 690 ? 65 : (forest > 610 ? 16 : 1);
        if (rng.below(100) >= chance) continue;
        if (chance <= 16 && rng.below(100) < 12) {  // felled timber at the forest's edge
          put(rng.below(2) ? PropKind::Logs : PropKind::Stump, 3);
          continue;
        }
        put(pick < 72 ? PropKind::SnowPine : pick < 88 ? PropKind::DeadTree : PropKind::Rock, pick < 88 ? 4 : 3);
      } else if (desert) {
        // Palms crowd the water; scrub and sandstone elsewhere.
        const uint32_t chance = nearWater ? 26 : (forest > 700 ? 6 : 1);
        if (rng.below(100) >= chance) continue;
        if (nearWater) put(pick < 70 ? PropKind::Palm : PropKind::Scrub, 4);
        else put(pick < 55 ? PropKind::Scrub : pick < 85 ? PropKind::SandRock : PropKind::DeadTree, pick < 55 ? 4 : 3);
      } else if (tropical) {
        const uint32_t chance = forest > 640 ? 80 : (forest > 540 ? 30 : 4);
        if (rng.below(100) >= chance) continue;
        put(pick < 55 ? PropKind::JungleTree : pick < 75 ? PropKind::Palm : PropKind::Bush, 4);
      } else if (urban) {
        // Shattered trees in the parks, debris along the streets.
        const float gx = static_cast<float>((x + 6) % 12), gy = static_cast<float>((y + 6) % 12);
        const bool street = std::min(std::abs(gx - 6.0f), std::abs(gy - 6.0f)) < 1.5f;
        if (street) {
          const uint32_t r = rng.below(100);
          if (r < 5) put(PropKind::Debris, 4);
          else if (r < 7) put(PropKind::Crates, 4);
          continue;
        }
        const uint32_t chance = forest > 700 ? 14 : 2;
        if (rng.below(100) >= chance) continue;
        put(pick < 40 ? PropKind::Tree : pick < 70 ? PropKind::DeadTree : PropKind::Debris, 4);
      } else {
        const uint32_t chance = forest > 700 ? 70 : (forest > 620 ? 18 : 1);
        if (rng.below(1000) < 4 && forest < 560) {  // haystacks out in the fields
          put(PropKind::Haystack, 3);
          continue;
        }
        if (rng.below(100) >= chance) continue;
        if (chance == 18 && pick < 8) {  // woodcutting at the forest's edge
          put(pick < 4 ? PropKind::Stump : PropKind::Logs, 3);
          continue;
        }
        const PropKind kind = pick < 30 ? PropKind::Pine : pick < 48 ? PropKind::Bush : PropKind::Tree;
        put(kind, 4);
      }
    }

  // --- Supply points ---------------------------------------------------------
  // Two beside each starting area, more out in the middle to fight over.
  // Symmetric, so no side starts with an advantage.
  std::fill(oil_.begin(), oil_.end(), uint8_t{0});
  supplyPoints.clear();
  const struct { int fx, fy; } sites[] = {
      {17, 38}, {17, 62}, {83, 38}, {83, 62},  // west and east homes
      {38, 17}, {62, 17}, {38, 83}, {62, 83},  // north and south homes
      {50, 50}, {32, 30}, {68, 70}, {32, 70}, {68, 30},  // contested
  };
  for (const auto& site : sites) {
    int cx = W * site.fx / 100, cy = H * site.fy / 100;
    for (int tries = 0; tries < 60; ++tries) {
      bool ok = true;
      for (int y = cy - 2; y <= cy + 2 && ok; ++y)
        for (int x = cx - 2; x <= cx + 2 && ok; ++x)
          ok = inBounds(x, y) && info(x, y).waterDepth < -16 && info(x, y).slope < 20;
      if (ok) break;
      cx = std::clamp(cx + static_cast<int>(rng.range(-3, 3)), 3, W - 4);
      cy = std::clamp(cy + static_cast<int>(rng.range(-3, 3)), 3, H - 4);
    }
    supplyPoints.push_back({cx, cy});
  }
  // Clear trees off the sites so they can be seen and built on.
  std::erase_if(props_, [&](const Prop& p) {
    const int tx = p.x >> LEPTON_SHIFT, ty = p.y >> LEPTON_SHIFT;
    for (const Vec2i& sp : supplyPoints)
      if (std::abs(tx - sp.x) <= 3 && std::abs(ty - sp.y) <= 3) return true;
    return false;
  });

  // --- Villages and towns ------------------------------------------------------
  // Plots for neutral houses, kept clear of the four starting areas (west,
  // east, north, south), the oil and the water. Towns fill whole street
  // blocks; elsewhere a few hamlets sit along the roads.
  if (b == Biome::Tropical) return;
  const int spots[4][2] = {{W * 17 / 100, H / 2}, {W * 83 / 100, H / 2}, {W / 2, H * 17 / 100}, {W / 2, H * 83 / 100}};
  auto plotFree = [&](int cx, int cy, int half) {
    for (const auto& sp : spots)
      if (std::abs(cx - sp[0]) < 15 && std::abs(cy - sp[1]) < 15) return false;
    for (int y = cy - half; y <= cy + half; ++y)
      for (int x = cx - half; x <= cx + half; ++x) {
        if (!inBounds(x, y) || x < 2 || y < 2 || x >= W - 2 || y >= H - 2) return false;
        const TileInfo& t = info(x, y);
        if (t.slope > 18 || t.waterDepth > -12 || oil(x, y) > 0) return false;
      }
    for (const Lot& l : lots)
      if (std::abs(l.tx - cx) < half + 3 && std::abs(l.ty - cy) < half + 3) return false;
    for (const Vec2i& sp : supplyPoints)
      if (std::abs(sp.x - cx) < half + 4 && std::abs(sp.y - cy) < half + 4) return false;
    return true;
  };
  auto clearProps = [&](int cx, int cy, int half) {
    std::erase_if(props_, [&](const Prop& p) {
      const int tx = p.x >> LEPTON_SHIFT, ty = p.y >> LEPTON_SHIFT;
      return std::abs(tx - cx) <= half + 1 && std::abs(ty - cy) <= half + 1;
    });
  };
  if (urban) {
    for (int by = 0; by + 12 <= H; by += 12)
      for (int bx = 0; bx + 12 <= W; bx += 12) {
        // Each street block holds up to four buildings.
        for (int k = 0; k < 4; ++k) {
          const int cx = bx + 3 + (k % 2) * 6, cy = by + 3 + (k / 2) * 6;
          if (rng.below(100) < 22 || !plotFree(cx, cy, 1)) continue;
          const uint32_t pick = rng.below(100);
          uint8_t kind = pick < 32 ? 2 : pick < 54 ? 4 : pick < 74 ? 1 : pick < 86 ? 0 : pick < 92 ? 9 : pick < 95 ? 11 : 3;
          if (kind == 9 && !plotFree(cx, cy, 2)) kind = 2;  // a warehouse needs more room
          lots.push_back({cx, cy, kind});
          clearProps(cx, cy, kind == 9 ? 2 : 1);
        }
      }
    return;
  }
  const int hamlets = std::max(1, W * H / (128 * 128)) * (desert ? 2 : 3);
  for (int hIdx = 0; hIdx < hamlets; ++hIdx) {
    // A hamlet: three to six houses around a point beside a road.
    const int road = hIdx % 2;
    const int hy = 6 + static_cast<int>(rng.below(static_cast<uint32_t>(std::max(1, H - 12))));
    const int hx = (roadX(road, hy) >> 8) + (rng.below(2) ? 4 : -4);
    const int count = 3 + static_cast<int>(rng.below(4));
    for (int k = 0; k < count * 3 && k < 24; ++k) {
      const int cx = hx + static_cast<int>(rng.range(-6, 6)), cy = hy + static_cast<int>(rng.range(-6, 6));
      if (nearRoad(cx, cy) || !plotFree(cx, cy, 1)) continue;
      const uint32_t pick = rng.below(100);
      uint8_t kind = 0;
      if (desert) kind = pick < 85 ? 5 : 10;
      else if (winter) kind = pick < 45 ? 8 : pick < 60 ? 6 : pick < 72 ? 7 : pick < 82 ? 1 : pick < 90 ? 10 : pick < 96 ? 4 : 3;
      else kind = pick < 42 ? 0 : pick < 58 ? 1 : pick < 70 ? 6 : pick < 79 ? 7 : pick < 85 ? 10 : pick < 89 ? 11 : pick < 95 ? 4 : 3;
      lots.push_back({cx, cy, kind});
      clearProps(cx, cy, 1);
    }
  }
}

}  // namespace slave

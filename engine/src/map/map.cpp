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
  static const char* names[] = {"Oak tree", "Pine tree", "Bush", "Rock"};
  return names[static_cast<int>(k)];
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
void Map::generateCountryside(uint64_t seed) {
  Rng rng(seed);
  const uint32_t s = static_cast<uint32_t>(seed);
  const int W = width_, H = height_;
  props_.clear();

  // --- Rolling hills -----------------------------------------------------
  for (int cy = 0; cy < cornersY(); ++cy)
    for (int cx = 0; cx < cornersX(); ++cx) {
      const int32_t n = fractal(cx, cy, 48, s);  // 0..1023
      setCorner(cx, cy, static_cast<uint16_t>(430 + (n - 512) * 150 / 512));
    }
  waterLevel_ = 330;

  // --- River (west to east) ------------------------------------------------
  const int32_t riverY0 = H * 3 / 10 + static_cast<int32_t>(rng.below(static_cast<uint32_t>(H * 4 / 10)));
  auto riverY = [&](int32_t x) { return riverY0 * 256 + (valueNoise(x, 0, 40, s + 31) - 512) * H * 40 / 1024; };  // x256
  auto riverHalfWidth = [&](int32_t x) { return 2 * 256 + valueNoise(x, 7, 30, s + 37) * 256 / 512; };    // x256
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
      w[int(Layer::Grass)] = 1.0f;
      const float dry = fractal(sx, sy, 40, s + 101) / 1023.0f;
      w[int(Layer::DryGrass)] = smoothstep(0.58f, 0.72f, dry) * 1.0f;
      const float dirt = fractal(sx, sy, 18, s + 103) / 1023.0f;
      w[int(Layer::Dirt)] = smoothstep(0.66f, 0.76f, dirt) * 1.6f;

      const float h = heightAt(tx * LEPTONS_PER_TILE, ty * LEPTONS_PER_TILE);
      const float above = h - waterLevel_;
      const float mudNoise = valueNoise(sx, sy, 5, s + 109) / 1023.0f;
      w[int(Layer::Mud)] = 2.2f * (1.0f - smoothstep(-4.0f, 10.0f + mudNoise * 14.0f, above));
      const float sandNoise = valueNoise(sx, sy, 6, s + 107) / 1023.0f;
      w[int(Layer::Sand)] = (1.0f - smoothstep(2.0f, 10.0f, std::abs(above - 6.0f))) * smoothstep(0.55f, 0.8f, sandNoise) * 1.4f;
      w[int(Layer::Rock)] = smoothstep(24.0f, 40.0f, static_cast<float>(info(itx, ity).slope)) * 3.0f;

      for (int r = 0; r < 2; ++r) {
        const float rd = std::abs(tx * 256.0f - roadX(r, static_cast<int32_t>(ty))) / 256.0f;
        w[int(Layer::Road)] = std::max(w[int(Layer::Road)], (1.0f - smoothstep(0.45f, 0.95f, rd)) * 4.0f);
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
      const uint32_t chance = forest > 700 ? 70 : (forest > 620 ? 18 : 1);
      if (rng.below(100) >= chance) continue;
      const uint32_t pick = rng.below(100);
      const PropKind kind = pick < 30 ? PropKind::Pine : pick < 48 ? PropKind::Bush : PropKind::Tree;
      props_.push_back({kind, static_cast<uint8_t>(rng.below(4)), x * 256 + rng.range(40, 216),
                        y * 256 + rng.range(40, 216)});
    }

  // --- Oil fields ------------------------------------------------------------
  // One rich field near each side's half of the map, smaller ones out front to
  // fight over. Mirrored, so neither side starts with an advantage.
  std::fill(oil_.begin(), oil_.end(), uint8_t{0});
  const int r = std::max(3, W / 36);
  const struct { int fx, fy, rr; } fields[] = {
      {20, 34, r + 1}, {80, 66, r + 1},  // home fields (percent of the map)
      {12, 66, r}, {88, 34, r},          // second home fields
      {50, 18, r}, {50, 82, r},          // contested, north and south
  };
  for (const auto& f : fields) {
    // Nudge each field onto dry, fairly flat ground.
    int cx = W * f.fx / 100, cy = H * f.fy / 100;
    for (int tries = 0; tries < 40; ++tries) {
      if (inBounds(cx, cy) && info(cx, cy).waterDepth < -20 && info(cx, cy).slope < 20) break;
      cx += static_cast<int>(rng.range(-3, 3));
      cy += static_cast<int>(rng.range(-3, 3));
      cx = std::clamp(cx, f.rr + 1, W - f.rr - 2);
      cy = std::clamp(cy, f.rr + 1, H - f.rr - 2);
    }
    addOilField(cx, cy, f.rr, s + static_cast<uint32_t>(cx * 31 + cy));
  }
  // Clear trees off the fields so trucks and players can see them.
  std::erase_if(props_, [&](const Prop& p) {
    const int tx = p.x >> LEPTON_SHIFT, ty = p.y >> LEPTON_SHIFT;
    return inBounds(tx, ty) && oil(tx, ty) > 0;
  });
}

}  // namespace slave

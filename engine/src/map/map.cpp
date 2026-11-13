#include "pioneer/map/map.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <functional>

#include "pioneer/core/rng.hpp"

namespace pioneer {

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
  ice_.assign(static_cast<size_t>(width_) * height_, 0);
  floodMemory_.clear();
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
  // The drawn ground (9.2): a smooth surface through the corner heights
  // (Catmull-Rom across the 4 x 4 corners round the point), never rising
  // above or dipping below the cell's own corners, so steps stay steps.
  const float fx = std::clamp(lx / LEPTONS_PER_TILE, 0.0f, static_cast<float>(width_) - 0.001f);
  const float fy = std::clamp(ly / LEPTONS_PER_TILE, 0.0f, static_cast<float>(height_) - 0.001f);
  const int tx = static_cast<int>(fx), ty = static_cast<int>(fy);
  const float ax = fx - tx, ay = fy - ty;
  auto c = [&](int x, int y) { return static_cast<float>(corner(std::clamp(x, 0, width_), std::clamp(y, 0, height_))); };
  // Each curve stays between its two middle points (no overshoot at a step,
  // and neighbouring cells meet exactly along their shared edge).
  auto cr = [](float p0, float p1, float p2, float p3, float t) {
    const float v = p1 + 0.5f * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
    return std::clamp(v, std::min(p1, p2), std::max(p1, p2));
  };
  float rows[4];
  for (int j = 0; j < 4; ++j) {
    const int y = ty - 1 + j;
    rows[j] = cr(c(tx - 1, y), c(tx, y), c(tx + 1, y), c(tx + 2, y), ax);
  }
  return cr(rows[0], rows[1], rows[2], rows[3], ay);
}

float Map::heightAtLinear(float lx, float ly) const {
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
  // Speeds look at the water next door, so do one tile more each way.
  recomputeSpeeds(tx0 - 1, ty0 - 1, tx1 + 1, ty1 + 1);
}

void Map::recomputeSpeeds(int tx0, int ty0, int tx1, int ty1) {
  tx0 = std::max(0, tx0);
  ty0 = std::max(0, ty0);
  tx1 = std::min(width_ - 1, tx1);
  ty1 = std::min(height_ - 1, ty1);
  // Rough ground, per layer, for foot / wheeled / tracked (percent).
  static const uint8_t kRough[LAYER_COUNT][3] = {
      {100, 100, 100},  // grass
      {100, 100, 100},  // dry grass
      {100, 95, 100},   // dirt
      {80, 60, 75},     // mud
      {85, 75, 90},     // sand
      {90, 85, 90},     // rock
      {80, 75, 85},     // snow
      {100, 100, 100},  // road
  };
  for (int ty = ty0; ty <= ty1; ++ty)
    for (int tx = tx0; tx <= tx1; ++tx) {
      TileInfo& t = info_[static_cast<size_t>(ty) * width_ + tx];
      // The ground: the weighted mix of the tile's four splat texels.
      int weight[LAYER_COUNT] = {};
      for (int k = 0; k < 4; ++k) {
        const uint8_t* w = splat(tx * SPLAT_PER_TILE + (k & 1), ty * SPLAT_PER_TILE + (k >> 1));
        for (int l = 0; l < LAYER_COUNT; ++l) weight[l] += w[l];
      }
      int total = 0;
      for (int l = 0; l < LAYER_COUNT; ++l) total += weight[l];
      const bool road = total > 0 && weight[int(Layer::Road)] * 2 >= total;
      // A ford: a road at the water's edge with water beside it.
      bool wetNear = false;
      if (waterLevel_ && t.waterDepth > -10)
        for (int oy = -1; oy <= 1 && !wetNear; ++oy)
          for (int ox = -1; ox <= 1 && !wetNear; ++ox) {
            const int nx = tx + ox, ny = ty + oy;
            if ((ox || oy) && inBounds(nx, ny) && info_[static_cast<size_t>(ny) * width_ + nx].waterDepth > 0) wetNear = true;
          }
      t.ford = road && wetNear;
      for (int li = 0; li < 3; ++li) {
        int pct = 100;
        if (total > 0) {
          int rough = 0;
          for (int l = 0; l < LAYER_COUNT; ++l) rough += weight[l] * kRough[l][li];
          pct = rough / total;
        }
        // Slopes: down to 65% at the steepest a locomotor can climb.
        const int maxSlope = std::max<int>(1, rules_.maxSlope[li]);
        pct = pct * (100 - 35 * std::min<int>(t.slope, maxSlope) / maxSlope) / 100;
        // Wading and fords: half speed. Ice: men pick their way at 70%, wheels slither at 60%.
        if (t.waterDepth > 0 && ice_[static_cast<size_t>(ty) * width_ + tx]) pct = std::min(pct, li == 0 ? 70 : 60);
        else if (t.waterDepth > 0 || t.ford) pct = std::min(pct, 50);
        t.speed[li] = static_cast<uint8_t>(std::clamp(pct, 10, 100));
      }
    }
}

bool Map::passable(int tx, int ty, Locomotor loco) const {
  if (!inBounds(tx, ty)) return false;
  const TileInfo& t = info(tx, ty);
  const int li = static_cast<int>(loco);
  const uint8_t b = blocks_[static_cast<size_t>(ty) * width_ + tx];
  if (b & kDeck) return !(b & (1u << li));  // on a bridge
  // On the ice: anything but tracks, whatever the water underneath.
  if (t.waterDepth > 0 && ice_[static_cast<size_t>(ty) * width_ + tx] && loco != Locomotor::Tracked)
    return t.slope <= rules_.maxSlope[li] && !(b & (1u << li));
  return t.slope <= rules_.maxSlope[li] && t.waterDepth <= rules_.maxWaterDepth[li] && !(b & (1u << li));
}

int Map::setIce(int x0, int y0, int x1, int y1, bool frozen) {
  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(width_ - 1, x1);
  y1 = std::min(height_ - 1, y1);
  int n = 0;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      uint8_t& c = ice_[static_cast<size_t>(y) * width_ + x];
      const uint8_t want = frozen && info(x, y).waterDepth > 0 ? 1 : 0;
      if (c == want) continue;
      c = want;
      ++n;
    }
  if (n) {
    recomputeSpeeds(x0, y0, x1, y1);
    ++revision_;  // paths are rebuilt
  }
  return n;
}

int Map::flood(int x0, int y0, int x1, int y1) {
  if (!waterLevel_) return 0;
  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(width_ - 1, x1);
  y1 = std::min(height_ - 1, y1);
  const uint16_t target = static_cast<uint16_t>(std::max(1, int(waterLevel_) - kFloodDepth));
  int n = 0;
  for (int cy = y0; cy <= y1 + 1; ++cy)
    for (int cx = x0; cx <= x1 + 1; ++cx) {
      const uint32_t i = static_cast<uint32_t>(cy) * cornersX() + cx;
      if (heights_[i] <= target) continue;
      const bool known = std::any_of(floodMemory_.begin(), floodMemory_.end(), [&](const auto& m) { return m.first == i; });
      if (!known) floodMemory_.push_back({i, heights_[i]});
      heights_[i] = target;
      ++n;
    }
  if (n) {
    recomputeInfo(x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    noteHeightEdit({x0 - 1, y0 - 1, x1 + 1, y1 + 1});
    ++revision_;
  }
  return n;
}

int Map::drain(int x0, int y0, int x1, int y1) {
  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(width_ - 1, x1);
  y1 = std::min(height_ - 1, y1);
  int n = 0;
  for (auto it = floodMemory_.begin(); it != floodMemory_.end();) {
    const int cx = static_cast<int>(it->first) % cornersX(), cy = static_cast<int>(it->first) / cornersX();
    if (cx < x0 || cy < y0 || cx > x1 + 1 || cy > y1 + 1) {
      ++it;
      continue;
    }
    heights_[it->first] = it->second;
    it = floodMemory_.erase(it);
    ++n;
  }
  if (n) {
    recomputeInfo(x0 - 1, y0 - 1, x1 + 1, y1 + 1);
    noteHeightEdit({x0 - 1, y0 - 1, x1 + 1, y1 + 1});
    ++revision_;
  }
  return n;
}

bool Map::anyIce() const { return std::find(ice_.begin(), ice_.end(), uint8_t{1}) != ice_.end(); }

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


Vec2i Map::lotFootprint(uint8_t kind) {
  // (tests/test_content.cpp checks this against the rules.)
  switch (kind) {
    case 3: case 23: case 24: case 28: return {3, 3};                               // churches, town hall, villa
    case 9: case 13: case 14: case 19: case 20: case 21: case 22: return {3, 2};    // warehouse, farm, school and their like
    case 12: return {4, 2};                                                         // station
    case 7: case 10: case 11: case 25: case 26: return {1, 1};                      // shed, well, towers, silo, windmill
    default: return kind >= kRowLot ? Vec2i{1, 1} : Vec2i{2, 2};                    // houses; row houses and bridge pieces
  }
}

const char* Map::lotBuildingId(uint8_t kind) {
  static const char* kinds[] = {
      "neutral_cottage", "neutral_cottage_bombed", "neutral_townhouse", "neutral_church",   "neutral_ruin",
      "neutral_adobe",   "neutral_barn",           "neutral_shed",      "neutral_izba",     "neutral_warehouse",
      "neutral_well",    "neutral_water_tower",    "neutral_station",   "neutral_farmhouse", "neutral_school",
      // 15.. the variants that stand in for the plain kinds above
      "neutral_half_timber", "neutral_stone_cottage", "neutral_shopfront", "neutral_apartment", "neutral_factory",
      "neutral_tavern",      "neutral_terrace",       "neutral_manor",     "neutral_town_hall", "neutral_onion_church",
      "neutral_grain_silo",  "neutral_windmill",      "neutral_market",    "neutral_villa",     "neutral_stilt_hut",
      // 30.. row houses, one tile each, built wall to wall (9.8)
      "neutral_row", "neutral_row_brick", "neutral_row_timber", "neutral_row_adobe", "neutral_row_adobe_tall"};
  if (kind == kBridgeLot) return "neutral_bridge";
  if (kind == kWoodBridgeLot) return "neutral_bridge_wood";
  return kinds[kind % (sizeof kinds / sizeof kinds[0])];
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
void Map::generateCountryside(uint64_t seed) { generateTerrain(seed, Biome::Temperate, 0); }

const char* mapLayoutName(MapLayout l) {
  static const char* names[] = {"River valley", "Front line river", "Confluence", "Lakeland",   "Crossroads",
                                "Ridge",        "Marshes",          "Great lake", "Open steppe", "Deep forest",
                                "Hill country", "Twin rivers"};
  return names[static_cast<int>(l) % static_cast<int>(MapLayout::Count)];
}

MapLayout layoutForSeed(uint64_t seed) {
  return static_cast<MapLayout>(hash2(static_cast<int32_t>(seed), static_cast<int32_t>(seed >> 32), 0x5EED) %
                                static_cast<uint32_t>(MapLayout::Count));
}

void Map::generate(Biome b, uint64_t seed, int layout, bool settlements) {
  generateTerrain(seed, b, layout < 0 ? static_cast<int>(layoutForSeed(seed)) : layout, settlements);
  recomputeInfo();  // speeds read the finished ground layers and roads
}

namespace {
// A winding line across the map (river, road, ridge, gully), in tiles.
struct Curve {
  std::vector<Vec2f> pts;
  float halfWidth = 1;  // tiles
};

// From a to b with the middle points pushed sideways by up to `wander` tiles,
// then rounded off (Chaikin) so it bends naturally.
Curve makeCurve(Vec2f a, Vec2f b, float wander, float halfWidth, Rng& rng, int segments = 10) {
  Curve c;
  c.halfWidth = halfWidth;
  const Vec2f d = b - a;
  const float len = std::max(1.0f, std::sqrt(d.x * d.x + d.y * d.y));
  const Vec2f side{-d.y / len, d.x / len};
  std::vector<Vec2f> p;
  float drift = 0;
  for (int i = 0; i <= segments; ++i) {
    const float t = static_cast<float>(i) / segments;
    if (i > 0 && i < segments) drift = std::clamp(drift + (static_cast<float>(rng.range(-1000, 1000)) / 1000.0f) * wander * 0.6f, -wander, wander);
    const float w = (i == 0 || i == segments) ? 0.0f : drift;
    p.push_back({a.x + d.x * t + side.x * w, a.y + d.y * t + side.y * w});
  }
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<Vec2f> q{p.front()};
    for (size_t i = 0; i + 1 < p.size(); ++i) {
      q.push_back({p[i].x * 0.75f + p[i + 1].x * 0.25f, p[i].y * 0.75f + p[i + 1].y * 0.25f});
      q.push_back({p[i].x * 0.25f + p[i + 1].x * 0.75f, p[i].y * 0.25f + p[i + 1].y * 0.75f});
    }
    q.push_back(p.back());
    p = std::move(q);
  }
  c.pts = std::move(p);
  return c;
}

// Distance (tiles) from a point to the curve, and how far along it the nearest point lies.
float curveDistance(const Curve& c, Vec2f p, float* along = nullptr) {
  float best = 1e9f, bestAlong = 0, run = 0;
  for (size_t i = 0; i + 1 < c.pts.size(); ++i) {
    const Vec2f a = c.pts[i], b = c.pts[i + 1];
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float l2 = dx * dx + dy * dy;
    const float t = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
    const float ex = a.x + dx * t - p.x, ey = a.y + dy * t - p.y;
    const float d2 = ex * ex + ey * ey;
    const float seg = std::sqrt(l2);
    if (d2 < best) {
      best = d2;
      bestAlong = run + seg * t;
    }
    run += seg;
  }
  if (along) *along = bestAlong;
  return std::sqrt(best);
}

Vec2f curvePoint(const Curve& c, float t) {
  const float f = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(c.pts.size() - 1);
  const size_t i = std::min(c.pts.size() - 2, static_cast<size_t>(f));
  const float k = f - static_cast<float>(i);
  return {c.pts[i].x + (c.pts[i + 1].x - c.pts[i].x) * k, c.pts[i].y + (c.pts[i + 1].y - c.pts[i].y) * k};
}

struct Lake {
  Vec2f c;
  float rx = 5, ry = 5;  // tiles
  float depth = 38;      // height units below the water line at the middle
};

// Everything a layout decides before the land is shaped.
struct Plan {
  std::vector<Curve> rivers, roads, ridges, gullies;
  std::vector<float> passes;  // along the first ridge: where it dips low enough to cross (0..1)
  std::vector<Lake> lakes;
  int32_t hillAmp = 150, hillCell = 48, baseHeight = 430;
  int32_t escarpments = 0;  // terrace step (height units) on part of the hills, rock faces between; 0 = none
  float riverDepth = 38;
  int forestShift = 0;   // + more woods, - fewer
  int hamletScale = 3;   // hamlets per 128x128
  bool centralTown = false;
  bool marsh = false;
};
}  // namespace

void Map::generateTerrain(uint64_t seed, Biome b, int layoutIndex, bool settlements) {
  biome = b;
  layout = static_cast<MapLayout>(std::clamp(layoutIndex, 0, static_cast<int>(MapLayout::Count) - 1));
  lots.clear();
  const bool winter = b == Biome::Winter, desert = b == Biome::Desert, tropical = b == Biome::Tropical,
             urban = b == Biome::Urban;
  Rng rng(seed);
  const uint32_t s = static_cast<uint32_t>(seed);
  const int W = width_, H = height_;
  const float Wf = static_cast<float>(W), Hf = static_cast<float>(H);
  const float scale = std::sqrt(Wf * Hf) / 128.0f;  // features grow with the map
  props_.clear();
  waterLevel_ = 330;
  const int32_t wl = waterLevel_;
  auto rf = [&](float lo, float hi) { return lo + (hi - lo) * static_cast<float>(rng.below(10001)) / 10000.0f; };

  // Where sides may start (1v1, 2v2 and four-way): kept dry and fairly flat.
  const Vec2f spots[] = {{Wf * 0.17f, Hf * 0.5f},  {Wf * 0.83f, Hf * 0.5f},  {Wf * 0.5f, Hf * 0.17f},
                         {Wf * 0.5f, Hf * 0.83f},  {Wf * 0.15f, Hf * 0.3f},  {Wf * 0.15f, Hf * 0.7f},
                         {Wf * 0.85f, Hf * 0.3f},  {Wf * 0.85f, Hf * 0.7f}};
  auto nearSpot = [&](Vec2f p, float r) {
    for (const Vec2f& sp : spots)
      if (std::abs(p.x - sp.x) < r && std::abs(p.y - sp.y) < r) return true;
    return false;
  };

  // --- The layout ------------------------------------------------------------
  Plan plan;
  const float rw = (desert ? 1.6f : tropical ? 3.4f : 2.4f) * std::min(1.4f, std::max(0.8f, scale));  // river half width
  auto riverWE = [&](float y) { plan.rivers.push_back(makeCurve({-4, y}, {Wf + 4, y + rf(-0.08f, 0.08f) * Hf}, Hf * 0.09f, rw, rng)); };
  auto riverNS = [&](float x, float wander) {
    plan.rivers.push_back(makeCurve({x, -4}, {x + rf(-0.06f, 0.06f) * Wf, Hf + 4}, Wf * wander, rw, rng));
  };
  auto roadWE = [&](float y, float wander) { plan.roads.push_back(makeCurve({-2, y}, {Wf + 2, y + rf(-0.06f, 0.06f) * Hf}, Hf * wander, 1, rng)); };
  auto roadNS = [&](float x, float wander) { plan.roads.push_back(makeCurve({x, -2}, {x + rf(-0.06f, 0.06f) * Wf, Hf + 2}, Wf * wander, 1, rng)); };
  auto addLakes = [&](int count, float rMin, float rMax, float depth, float keepOut) {
    for (int i = 0, tries = 0; i < count && tries < count * 30; ++tries) {
      Lake l;
      l.rx = rf(rMin, rMax) * scale;
      l.ry = l.rx * rf(0.6f, 1.4f);
      l.depth = depth;
      l.c = {rf(0.08f, 0.92f) * Wf, rf(0.08f, 0.92f) * Hf};
      if (nearSpot(l.c, std::max(l.rx, l.ry) + keepOut)) continue;
      bool clash = false;
      for (const Lake& o : plan.lakes)
        clash = clash || std::hypot(o.c.x - l.c.x, o.c.y - l.c.y) < std::max(o.rx, o.ry) + std::max(l.rx, l.ry) + 4;
      if (clash) continue;
      plan.lakes.push_back(l);
      ++i;
    }
  };
  // Rivers and lakes bend round the starting areas instead of stopping short.
  auto skirtSpots = [&](Curve& c) {
    for (int iter = 0; iter < 4; ++iter) {
      // Ease the bends, then push clear again.
      std::vector<Vec2f> q = c.pts;
      for (size_t i = 1; i + 1 < q.size(); ++i)
        q[i] = {(c.pts[i - 1].x + c.pts[i].x * 2 + c.pts[i + 1].x) / 4, (c.pts[i - 1].y + c.pts[i].y * 2 + c.pts[i + 1].y) / 4};
      c.pts = std::move(q);
      for (size_t i = 1; i + 1 < c.pts.size(); ++i)
        for (const Vec2f& sp : spots) {
          const float keep = 12.5f + c.halfWidth;
          float dx = c.pts[i].x - sp.x, dy = c.pts[i].y - sp.y;
          const float d = std::sqrt(dx * dx + dy * dy);
          if (d >= keep) continue;
          if (d < 0.01f) dx = 1, dy = 0;
          const float k = keep / std::max(d, 0.01f);
          c.pts[i] = {sp.x + dx * k, sp.y + dy * k};
        }
    }
  };
  switch (layout) {
    case MapLayout::RiverValley:  // a river west to east, two roads crossing it
      riverWE(Hf * rf(0.36f, 0.64f));
      roadNS(Wf * rf(0.22f, 0.3f), 0.1f);
      roadNS(Wf * rf(0.68f, 0.76f), 0.1f);
      break;
    case MapLayout::FrontLine:  // a river between the sides, three fords
      riverNS(Wf * rf(0.46f, 0.54f), 0.06f);
      for (float y : {0.22f, 0.5f, 0.78f}) roadWE(Hf * (y + rf(-0.04f, 0.04f)), 0.05f);
      plan.forestShift = 20;
      break;
    case MapLayout::Confluence: {  // two rivers meet in the middle and flow on south
      const Vec2f mid{Wf * rf(0.45f, 0.55f), Hf * rf(0.42f, 0.5f)};
      plan.rivers.push_back(makeCurve({Wf * rf(0.18f, 0.3f), -4}, mid, Wf * 0.06f, rw * 0.8f, rng, 6));
      plan.rivers.push_back(makeCurve({Wf * rf(0.7f, 0.82f), -4}, mid, Wf * 0.06f, rw * 0.8f, rng, 6));
      plan.rivers.push_back(makeCurve(mid, {Wf * rf(0.62f, 0.75f), Hf + 4}, Wf * 0.06f, rw * 1.2f, rng, 6));
      roadWE(Hf * rf(0.55f, 0.65f), 0.06f);
      roadWE(Hf * rf(0.2f, 0.28f), 0.05f);
      roadNS(Wf * rf(0.3f, 0.36f), 0.05f);
      break;
    }
    case MapLayout::Lakeland:  // lakes scattered among the woods
      addLakes(4 + static_cast<int>(rng.below(5)), 4.0f, 10.0f, 36, 15);
      roadWE(Hf * rf(0.45f, 0.55f), 0.14f);
      roadNS(Wf * rf(0.45f, 0.55f), 0.14f);
      plan.forestShift = 10;
      break;
    case MapLayout::Crossroads:  // two roads cross at a market town; a brook and a pond
      roadWE(Hf * 0.5f, 0.03f);
      roadNS(Wf * 0.5f, 0.03f);
      plan.roads.push_back(makeCurve({-2, Hf * rf(0.05f, 0.2f)}, {Wf + 2, Hf * rf(0.8f, 0.95f)}, Hf * 0.08f, 1, rng));
      plan.rivers.push_back(makeCurve({Wf * rf(0.3f, 0.4f), -4}, {Wf * rf(0.6f, 0.7f), Hf + 4}, Wf * 0.12f, 1.1f, rng));
      plan.rivers.back().halfWidth = 1.2f;
      addLakes(1 + static_cast<int>(rng.below(2)), 3.0f, 5.0f, 30, 16);
      plan.centralTown = true;
      plan.hamletScale = 2;
      break;
    case MapLayout::Ridge: {  // a high ridge between the sides, crossed at a few passes
      plan.ridges.push_back(makeCurve({Wf * rf(0.45f, 0.55f), -6}, {Wf * rf(0.45f, 0.55f), Hf + 6}, Wf * 0.07f, 9.0f * scale, rng));
      plan.passes = {rf(0.16f, 0.3f), rf(0.45f, 0.55f), rf(0.7f, 0.84f)};
      for (float t : plan.passes) {
        const Vec2f pp = curvePoint(plan.ridges[0], t);
        plan.roads.push_back(makeCurve({-2, pp.y + rf(-0.05f, 0.05f) * Hf}, pp, Hf * 0.04f, 1, rng, 5));
        plan.roads.push_back(makeCurve(pp, {Wf + 2, pp.y + rf(-0.05f, 0.05f) * Hf}, Hf * 0.04f, 1, rng, 5));
      }
      plan.hillAmp = 120;
      plan.escarpments = 72;
      break;
    }
    case MapLayout::Marshes:  // low, wet ground: shallow pools men can wade, causeway roads
      plan.hillAmp = 50;
      plan.baseHeight = 352;
      addLakes(18 + static_cast<int>(rng.below(14)), 2.0f, 5.5f, 9, 12);
      roadWE(Hf * rf(0.4f, 0.6f), 0.08f);
      roadNS(Wf * rf(0.3f, 0.4f), 0.08f);
      roadNS(Wf * rf(0.6f, 0.7f), 0.08f);
      plan.marsh = true;
      plan.forestShift = -10;
      break;
    case MapLayout::GreatLake: {  // a great lake in the middle: fight round its shores
      Lake l;
      l.c = {Wf * rf(0.47f, 0.53f), Hf * rf(0.47f, 0.53f)};
      l.rx = Wf * rf(0.17f, 0.22f);
      l.ry = Hf * rf(0.15f, 0.2f);
      l.depth = 44;
      plan.lakes.push_back(l);
      roadWE(Hf * rf(0.12f, 0.18f), 0.04f);
      roadWE(Hf * rf(0.82f, 0.88f), 0.04f);
      roadNS(Wf * rf(0.2f, 0.25f), 0.05f);
      roadNS(Wf * rf(0.75f, 0.8f), 0.05f);
      break;
    }
    case MapLayout::Steppe:  // open grassland, long roads, dry gullies
      plan.hillAmp = 70;
      plan.hillCell = 64;
      plan.forestShift = -60;
      roadWE(Hf * rf(0.3f, 0.4f), 0.04f);
      roadWE(Hf * rf(0.6f, 0.7f), 0.04f);
      roadNS(Wf * rf(0.45f, 0.55f), 0.05f);
      for (int i = 0, n = 3 + static_cast<int>(rng.below(4)); i < n; ++i) {
        const Vec2f a{rf(0.2f, 0.8f) * Wf, rf(0.1f, 0.9f) * Hf};
        const float ang = rf(0, 6.283f), len = rf(12, 30) * scale;
        plan.gullies.push_back(makeCurve(a, {a.x + std::cos(ang) * len, a.y + std::sin(ang) * len}, len * 0.2f, 2.5f, rng, 6));
      }
      plan.hamletScale = 2;
      break;
    case MapLayout::Forest:  // dense woods, logging tracks, a stream
      plan.forestShift = 90;
      for (int i = 0; i < 3; ++i)
        plan.roads.push_back(makeCurve({rf(0, 0.2f) * Wf, rf(0.1f, 0.9f) * Hf}, {rf(0.8f, 1) * Wf, rf(0.1f, 0.9f) * Hf}, Hf * 0.12f, 1, rng));
      roadNS(Wf * rf(0.4f, 0.6f), 0.12f);
      plan.rivers.push_back(makeCurve({Wf * rf(0.2f, 0.4f), -4}, {Wf * rf(0.6f, 0.8f), Hf + 4}, Wf * 0.1f, 1.4f, rng));
      plan.hamletScale = 2;
      break;
    case MapLayout::HillCountry:  // rough hills and a winding river
      plan.hillAmp = 250;
      plan.hillCell = 32;
      plan.escarpments = 96;
      riverNS(Wf * rf(0.45f, 0.55f), 0.14f);
      roadWE(Hf * rf(0.3f, 0.4f), 0.1f);
      roadWE(Hf * rf(0.6f, 0.7f), 0.1f);
      break;
    case MapLayout::TwinRivers:  // two rivers make a long island in the middle
      riverNS(Wf * rf(0.33f, 0.37f), 0.04f);
      riverNS(Wf * rf(0.63f, 0.67f), 0.04f);
      for (float y : {0.3f, 0.7f}) roadWE(Hf * (y + rf(-0.05f, 0.05f)), 0.05f);
      roadNS(Wf * 0.5f, 0.06f);
      break;
    default: break;
  }
  for (Curve& r : plan.rivers) skirtSpots(r);
  if (urban) plan.hillAmp = std::min(plan.hillAmp, 90);
  if (desert) plan.hillAmp = plan.hillAmp * 170 / 150;

  // --- Distance fields at every height corner ------------------------------------
  const int CX = cornersX(), CY = cornersY();
  std::vector<float> roadDist(static_cast<size_t>(CX) * CY, 1e9f);
  for (int cy = 0; cy < CY; ++cy)
    for (int cx = 0; cx < CX; ++cx) {
      float d = 1e9f;
      for (const Curve& r : plan.roads) d = std::min(d, curveDistance(r, {static_cast<float>(cx), static_cast<float>(cy)}));
      roadDist[static_cast<size_t>(cy) * CX + cx] = d;
    }
  // Road distance anywhere (tiles), from the corners.
  auto roadAt = [&](float tx, float ty) {
    const int x0 = std::clamp(static_cast<int>(tx), 0, CX - 2), y0 = std::clamp(static_cast<int>(ty), 0, CY - 2);
    const float fx = std::clamp(tx - x0, 0.0f, 1.0f), fy = std::clamp(ty - y0, 0.0f, 1.0f);
    auto at = [&](int x, int y) { return roadDist[static_cast<size_t>(y) * CX + x]; };
    const float top = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
    const float bot = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
    return top + (bot - top) * fy;
  };

  // --- Shape the land -------------------------------------------------------------
  constexpr float kValley = 12.0f;  // banks rise over 12 tiles: no cliffs
  std::vector<int32_t> hill(static_cast<size_t>(CX) * CY);
  for (int cy = 0; cy < CY; ++cy)
    for (int cx = 0; cx < CX; ++cx) {
      const int32_t n = fractal(cx, cy, plan.hillCell, s);  // 0..1023
      int32_t h = plan.baseHeight + (n - 512) * plan.hillAmp / 512;
      if (desert) h += (valueNoise(cx * 3, cy, 22, s + 77) - 512) * 30 / 512;  // dune ridges
      if (plan.escarpments > 0) {
        // Escarpments (9.2): on some hillsides the land steps up in terraces
        // with a rock face between; never across a road.
        const float rd = roadDist[static_cast<size_t>(cy) * CX + cx];
        const float mask = smoothstep(0.52f, 0.66f, valueNoise(cx, cy, 26, s + 211) / 1023.0f) * smoothstep(3.0f, 6.0f, rd);
        if (mask > 0) {
          const float step = static_cast<float>(plan.escarpments);
          const float hf = static_cast<float>(h);
          const float lo = std::floor(hf / step) * step;
          const float terrace = lo + smoothstep(0.8f, 0.97f, (hf - lo) / step) * step;
          h = static_cast<int32_t>(hf + (terrace - hf) * mask);
        }
      }
      hill[static_cast<size_t>(cy) * CX + cx] = h;
    }
  for (int cy = 0; cy < CY; ++cy)
    for (int cx = 0; cx < CX; ++cx) {
      const Vec2f p{static_cast<float>(cx), static_cast<float>(cy)};
      float h = static_cast<float>(hill[static_cast<size_t>(cy) * CX + cx]);
      // Starting areas stay dry and gentle: how much this corner belongs to one.
      float protect = 0, target = h;
      for (const Vec2f& sp : spots) {
        const float wgt = 1.0f - smoothstep(7.5f, 12.5f, std::hypot(p.x - sp.x, p.y - sp.y));
        if (wgt > protect) {
          protect = wgt;
          const int sx = std::clamp(static_cast<int>(sp.x), 0, CX - 1), sy = std::clamp(static_cast<int>(sp.y), 0, CY - 1);
          target = std::max(static_cast<float>(hill[static_cast<size_t>(sy) * CX + sx]), static_cast<float>(wl + 36));
        }
      }
      // Ridges: a broad hump, low where a pass crosses.
      for (size_t r = 0; r < plan.ridges.size(); ++r) {
        float along = 0;
        const float d = curveDistance(plan.ridges[r], p, &along);
        const Curve& c = plan.ridges[r];
        if (d < c.halfWidth * 1.6f) {
          float total = 0;
          for (size_t i = 0; i + 1 < c.pts.size(); ++i) total += std::hypot(c.pts[i + 1].x - c.pts[i].x, c.pts[i + 1].y - c.pts[i].y);
          float lift = 230.0f * (1.0f - smoothstep(0.0f, c.halfWidth * 1.6f, d));
          lift *= 0.8f + 0.4f * valueNoise(static_cast<int32_t>(along), 3, 12, s + 59) / 1023.0f;
          if (r == 0)
            for (float t : plan.passes) lift *= smoothstep(4.0f, 11.0f * scale, std::abs(along - t * total));
          h += lift * (1.0f - protect);
        }
      }
      // Gullies: dry ravines cut into the plain.
      for (const Curve& g : plan.gullies) {
        const float d = curveDistance(g, p);
        if (d < g.halfWidth + 4) h -= 45.0f * (1.0f - smoothstep(0.0f, g.halfWidth + 4, d));
      }
      if (!plan.gullies.empty()) h = std::max(h, static_cast<float>(wl + 8));
      // Rivers and lakes: a bowl below the water line, banks easing up.
      float waterT = 1e9f, bank = 1e9f, depth = plan.riverDepth;
      for (const Curve& r : plan.rivers) {
        float along = 0;
        const float d = curveDistance(r, p, &along);
        const float hw = r.halfWidth * (0.8f + 0.5f * valueNoise(static_cast<int32_t>(along * 4), 7, 30, s + 37) / 1023.0f);
        if (d / hw < waterT) {
          waterT = d / hw;
          bank = d - hw;
          depth = plan.riverDepth * std::min(1.0f, hw / 2.0f);
        }
      }
      for (const Lake& l : plan.lakes) {
        const float dx = (p.x - l.c.x) / l.rx, dy = (p.y - l.c.y) / l.ry;
        const float ang = std::atan2(dy, dx);
        const float wobble = 0.85f + 0.3f * valueNoise(static_cast<int32_t>((ang + 3.2f) * 20), 11, 9, s + 71) / 1023.0f;
        const float t = std::sqrt(dx * dx + dy * dy) / wobble;
        if (t < waterT) {
          waterT = t;
          bank = (t - 1.0f) * std::min(l.rx, l.ry);
          depth = l.depth;
        }
      }
      const float valley = plan.marsh ? 5.0f : kValley;
      if (waterT < 1.0f) {
        const float bed = static_cast<float>(wl) - 4 - depth * (1.0f - waterT * waterT);
        h = std::min(h, bed);
      } else if (bank < valley) {
        // A river cut into its valley: a short steep bank out of the water,
        // then the valley easing up to the land. Where a road comes down to
        // a ford the bank is cut away for it.
        const float rd = roadDist[static_cast<size_t>(cy) * CX + cx];
        const float rise = (plan.marsh ? 3.0f : 16.0f) * smoothstep(1.5f, 4.5f, rd);
        const float base = static_cast<float>(wl - 4), bankTop = static_cast<float>(wl) + rise;
        constexpr float kLip = 2.0f;  // tiles of steep bank
        float hv;
        if (bank < kLip) {
          hv = base + (bankTop - base) * smoothstep(0.0f, kLip, bank);
        } else {
          float t = (bank - kLip) / (valley - kLip);
          t = 1 - (1 - t) * (1 - t);
          hv = bankTop + (h - bankTop) * t;
        }
        h = std::min(h, std::max(hv, base));
      }
      // Roads cross water on fords (and marsh on causeways).
      const float rd = roadDist[static_cast<size_t>(cy) * CX + cx];
      const float top = static_cast<float>(wl) + (plan.marsh ? 6.0f : 3.0f);
      if (rd < 3.0f && h < top) h = std::max(h, top - rd * rd * 4.0f);
      if (protect > 0) h += (target - h) * protect;
      setCorner(cx, cy, static_cast<uint16_t>(std::clamp(static_cast<int>(h), 0, 65535)));
    }

  // Two gentle smoothing passes remove any leftover ridges from the carving.
  for (int pass = 0; pass < 2; ++pass) {
    std::vector<uint16_t> src = heights_;
    for (int cy = 1; cy < CY - 1; ++cy)
      for (int cx = 1; cx < CX - 1; ++cx) {
        int32_t sum = 0;
        for (int oy = -1; oy <= 1; ++oy)
          for (int ox = -1; ox <= 1; ++ox)
            sum += src[static_cast<size_t>(cy + oy) * CX + cx + ox] * ((ox == 0 && oy == 0) ? 4 : 1);
        setCorner(cx, cy, static_cast<uint16_t>(sum / 12));
      }
  }
  // Graded roads: the road bed is smoothed along its length (cut and fill),
  // the shoulders eased into it; fords stay at the water's edge.
  {
    const float fordTop = static_cast<float>(wl) + (plan.marsh ? 6.0f : 3.0f);
    for (int pass = 0; pass < 3; ++pass) {
      const std::vector<uint16_t> src = heights_;
      for (int cy = 2; cy < CY - 2; ++cy)
        for (int cx = 2; cx < CX - 2; ++cx) {
          const float rd = roadDist[static_cast<size_t>(cy) * CX + cx];
          if (rd >= 3.0f) continue;
          // The road's own height here: the mean of the road corners round it.
          int32_t sum = 0, n = 0;
          for (int oy = -2; oy <= 2; ++oy)
            for (int ox = -2; ox <= 2; ++ox)
              if (roadDist[static_cast<size_t>(cy + oy) * CX + cx + ox] < 1.5f) {
                sum += src[static_cast<size_t>(cy + oy) * CX + cx + ox];
                ++n;
              }
          if (!n) continue;
          const float bed = static_cast<float>(sum) / n;
          const float k = 1.0f - smoothstep(1.2f, 3.0f, rd);  // the bed itself, then the shoulders
          float h = src[static_cast<size_t>(cy) * CX + cx] + (bed - src[static_cast<size_t>(cy) * CX + cx]) * k;
          if (wl && rd < 3.0f && h < fordTop) h = std::max(h, fordTop - rd * rd * 4.0f);
          setCorner(cx, cy, static_cast<uint16_t>(std::clamp(static_cast<int>(h + 0.5f), 0, 65535)));
        }
    }
  }
  recomputeInfo();

  // --- Ground layers --------------------------------------------------------
  for (int sy = 0; sy < splatH(); ++sy)
    for (int sx = 0; sx < splatW(); ++sx) {
      const float tx = (sx + 0.5f) / SPLAT_PER_TILE, ty = (sy + 0.5f) / SPLAT_PER_TILE;
      const int itx = std::min(W - 1, static_cast<int>(tx)), ity = std::min(H - 1, static_cast<int>(ty));
      float w[LAYER_COUNT] = {};
      const float h = heightAtLinear(tx * LEPTONS_PER_TILE, ty * LEPTONS_PER_TILE);
      const float above = h - waterLevel_;
      const float dry = fractal(sx, sy, 40, s + 101) / 1023.0f;
      const float dirt = fractal(sx, sy, 18, s + 103) / 1023.0f;
      const float mudNoise = valueNoise(sx, sy, 5, s + 109) / 1023.0f;
      const float sandNoise = valueNoise(sx, sy, 6, s + 107) / 1023.0f;
      const float slopeK = smoothstep(24.0f, 40.0f, static_cast<float>(info(itx, ity).slope));
      const float shoreMud = 1.0f - smoothstep(-4.0f, (plan.marsh ? 22.0f : 10.0f) + mudNoise * 14.0f, above);
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
        const bool steppe = layout == MapLayout::Steppe;
        w[int(Layer::Grass)] = steppe ? 0.7f : 1.0f;
        w[int(Layer::DryGrass)] = smoothstep(steppe ? 0.4f : 0.58f, steppe ? 0.6f : 0.72f, dry) * (steppe ? 1.5f : 1.0f);
        w[int(Layer::Dirt)] = smoothstep(0.66f, 0.76f, dirt) * 1.6f;
        w[int(Layer::Mud)] = 2.2f * shoreMud;
        w[int(Layer::Sand)] = (1.0f - smoothstep(2.0f, 10.0f, std::abs(above - 6.0f))) * smoothstep(0.55f, 0.8f, sandNoise) * 1.4f;
        w[int(Layer::Rock)] = slopeK * 3.0f;
      }
      const float rd = roadAt(tx, ty);
      w[int(Layer::Road)] = (1.0f - smoothstep(0.45f, 0.95f, rd)) * 4.0f;
      if (urban) {
        // A grid of streets every 12 tiles.
        const float gx = std::fmod(tx + 6.0f, 12.0f), gy = std::fmod(ty + 6.0f, 12.0f);
        const float sd = std::min(std::abs(gx - 6.0f), std::abs(gy - 6.0f));
        w[int(Layer::Road)] = std::max(w[int(Layer::Road)], (1.0f - smoothstep(0.5f, 1.0f, sd)) * 4.0f);
      } else if (plan.centralTown) {
        // The market square.
        const float d = std::max(std::abs(tx - Wf * 0.5f), std::abs(ty - Hf * 0.5f));
        w[int(Layer::Road)] = std::max(w[int(Layer::Road)], (1.0f - smoothstep(2.5f, 3.2f, d)) * 4.0f);
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
  auto nearRoad = [&](int x, int y) { return roadAt(x + 0.5f, y + 0.5f) < 3.0f; };
  const int32_t fs = plan.forestShift;
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
      const int32_t forest = fractal(x + 1000, y + 1000, 28, s + 13) + fs;
      const bool nearWater = t.waterDepth > -40;
      auto put = [&](PropKind kind, uint32_t variants) {
        props_.push_back({kind, static_cast<uint8_t>(rng.below(variants)), x * 256 + rng.range(40, 216),
                          y * 256 + rng.range(40, 216)});
      };
      const uint32_t pick = rng.below(100);
      if (plan.marsh && nearWater && !winter && !desert) {
        // Reed beds and alders round the pools.
        if (rng.below(100) < 18) put(pick < 60 ? PropKind::Bush : pick < 80 ? PropKind::DeadTree : PropKind::Tree, pick < 80 ? 4 : 4);
        continue;
      }
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
        if (rng.below(1000) < (layout == MapLayout::Steppe ? 9u : 4u) && forest < 560) {  // haystacks out in the fields
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

  // --- Logistics points -------------------------------------------------------
  // A few, scattered at random, far from every starting area and from each
  // other: the side that holds one gets an office there and convoys of money
  // from it (9.6). Each has a twin mirrored through the middle of the map, so
  // in a duel neither side has them nearer; an odd one lies near the middle.
  // Placed with their own random stream, so the rest of the map is the same
  // whatever happens here.
  std::fill(oil_.begin(), oil_.end(), uint8_t{0});
  supplyPoints.clear();
  {
    Rng prng(seed ^ 0x9E3779B97F4A7C15ULL);
    const int S = std::min(W, H);
    const int count = std::clamp((W + H) / 80, 2, 12);
    const int homeAt[4][2] = {{W * 17 / 100, H / 2}, {W * 83 / 100, H / 2}, {W / 2, H * 17 / 100}, {W / 2, H * 83 / 100}};
    auto flat = [&](int cx, int cy) {
      for (int y = cy - 2; y <= cy + 2; ++y)
        for (int x = cx - 2; x <= cx + 2; ++x)
          if (!inBounds(x, y) || info(x, y).waterDepth >= -16 || info(x, y).slope >= 20) return false;
      return cx >= 6 && cy >= 6 && cx < W - 6 && cy < H - 6;
    };
    for (int relax = 0; relax < 3 && static_cast<int>(supplyPoints.size()) < count; ++relax) {
      const int fromHome = S * (30 - relax * 6) / 100, apart = S * (22 - relax * 5) / 100;
      auto good = [&](int cx, int cy) {
        if (!flat(cx, cy)) return false;
        for (const auto& h : homeAt)
          if ((cx - h[0]) * (cx - h[0]) + (cy - h[1]) * (cy - h[1]) < fromHome * fromHome) return false;
        for (const Vec2i& sp : supplyPoints)
          if ((cx - sp.x) * (cx - sp.x) + (cy - sp.y) * (cy - sp.y) < apart * apart) return false;
        return true;
      };
      // Near `want`, the first good spot in widening rings.
      auto near = [&](Vec2i want, int reach, Vec2i& out) {
        for (int r = 0; r <= reach; ++r)
          for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
              if (std::max(std::abs(dx), std::abs(dy)) != r || !good(want.x + dx, want.y + dy)) continue;
              out = {want.x + dx, want.y + dy};
              return true;
            }
        return false;
      };
      if (count % 2 == 1 && supplyPoints.empty()) {
        Vec2i c;
        if (near({W / 2 + static_cast<int>(prng.range(-S / 12, S / 12)), H / 2 + static_cast<int>(prng.range(-S / 12, S / 12))}, S / 8, c))
          supplyPoints.push_back(c);
      }
      for (int tries = 0; tries < 600 && static_cast<int>(supplyPoints.size()) + 1 < count + (count % 2 == 1 && supplyPoints.empty() ? 1 : 0); ++tries) {
        const Vec2i a{static_cast<int>(prng.range(W * 8 / 100, W * 92 / 100)), static_cast<int>(prng.range(H * 8 / 100, H * 92 / 100))};
        if (!good(a.x, a.y)) continue;
        supplyPoints.push_back(a);
        Vec2i twin;
        if (near({W - 1 - a.x, H - 1 - a.y}, 5, twin)) supplyPoints.push_back(twin);
        else supplyPoints.pop_back();
      }
    }
    // Always in the same order, whatever order they were found in.
    std::sort(supplyPoints.begin(), supplyPoints.end(), [](Vec2i a, Vec2i b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
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
  // On the way out, whichever way the plots were laid: dress the plain house
  // kinds as their local variants (by place, not by the random stream, so
  // the rest of the map is unchanged), and bridge the rivers where roads ford.
  struct AtEnd {
    std::function<void()> f;
    ~AtEnd() { f(); }
  } atEnd{[&] {
    for (Lot& l : lots) {
      const uint32_t hv = hash32(static_cast<uint32_t>(l.tx) * 92821u ^ static_cast<uint32_t>(l.ty) * 68917u ^ static_cast<uint32_t>(s));
      const uint32_t k4 = hv % 4, k3 = (hv >> 8) % 3;
      switch (l.kind) {
        case 0: l.kind = k4 == 1 ? 15 : k4 == 2 ? 16 : k4 == 3 && !winter ? 15 : 0; break;            // cottages
        case 2: l.kind = k4 == 1 || k4 == 3 ? 17 : k4 == 2 && urban ? 18 : 2; break;                   // town houses
        case 9: l.kind = (urban ? k4 < 2 : k4 == 0) ? 19 : 9; break;                                    // warehouses
        case 14: l.kind = k3 == 1 ? 20 : k3 == 2 ? 21 : 14; break;                                      // schools
        case 13: l.kind = k3 == 2 ? 22 : 13; break;                                                      // farms
        case 3: l.kind = winter ? 24 : (urban || plan.centralTown) && k4 < 2 ? 23 : 3; break;           // churches
        case 11: l.kind = winter ? 25 : !urban && k4 < 2 ? 26 : 11; break;                              // towers
        case 5: l.kind = k4 == 1 ? 27 : 5; break;                                                        // adobe houses
        default: break;
      }
    }
    // Bridges (9.2): beside each ford, a straight span from one high bank to
    // the other, square to the map's grid, its deck at the height of the
    // banks and its ends set into them; where the banks are low there is
    // only the ford.
    const bool wood = winter || tropical;
    auto avgHeight = [&](int tx, int ty) {
      return (corner(tx, ty) + corner(tx + 1, ty) + corner(tx, ty + 1) + corner(tx + 1, ty + 1)) / 4;
    };
    auto wetTile = [&](int tx, int ty) {
      return avgHeight(tx, ty) < static_cast<int>(wl) + 4 ||
             std::min({corner(tx, ty), corner(tx + 1, ty), corner(tx, ty + 1), corner(tx + 1, ty + 1)}) < wl;
    };
    auto taken = [&](int tx, int ty) {
      for (const Lot& l : lots)
        if (std::abs(l.tx - tx) <= 1 && std::abs(l.ty - ty) <= 1) return true;
      return false;
    };
    for (const Curve& road : plan.roads) {
      // Where the road meets each river: the middle of each wet stretch.
      std::vector<std::pair<Vec2f, Vec2f>> crossings;  // point, road direction
      bool inWet = false;
      float wetStart = 0;
      for (int i = 0; i <= 2000; ++i) {
        const float u = i / 2000.0f;
        const Vec2f p = curvePoint(road, u);
        bool w = false;
        for (const Curve& r : plan.rivers) w = w || curveDistance(r, p) < r.halfWidth;
        if (w && !inWet) wetStart = u;
        if (!w && inWet) {
          const float mid = (wetStart + u) * 0.5f;
          const Vec2f a = curvePoint(road, std::max(0.0f, mid - 0.01f)), b = curvePoint(road, std::min(1.0f, mid + 0.01f));
          crossings.push_back({curvePoint(road, mid), b - a});
        }
        inWet = w;
      }
      for (const auto& [at, dir] : crossings) {
        const int cx = static_cast<int>(std::floor(at.x)), cy = static_cast<int>(std::floor(at.y));
        // Try both axes and a few places beside the ford; keep the shortest good span.
        std::vector<Vec2i> best;
        for (int axis = 0; axis < 2; ++axis)
          for (int off : {4, -4, 5, -5, 6, -6}) {
            const int ax = axis == 0 ? 1 : 0, ay = axis == 0 ? 0 : 1;   // along the bridge
            const int sx = cx + (axis == 0 ? 0 : off), sy = cy + (axis == 0 ? off : 0);  // beside the ford
            if (!inBounds(sx, sy) || !wetTile(sx, sy)) continue;
            int a = 0, b = 0;
            while (a < 16 && inBounds(sx - ax * (a + 1), sy - ay * (a + 1)) && wetTile(sx - ax * (a + 1), sy - ay * (a + 1))) ++a;
            while (b < 16 && inBounds(sx + ax * (b + 1), sy + ay * (b + 1)) && wetTile(sx + ax * (b + 1), sy + ay * (b + 1))) ++b;
            const Vec2i endA{sx - ax * (a + 1), sy - ay * (a + 1)}, endB{sx + ax * (b + 1), sy + ay * (b + 1)};
            if (a >= 16 || b >= 16 || !inBounds(endA.x, endA.y) || !inBounds(endB.x, endB.y)) continue;
            if (endA.x < 2 || endA.y < 2 || endB.x >= W - 2 || endB.y >= H - 2) continue;
            // Both banks must stand well above the water.
            if (avgHeight(endA.x, endA.y) < static_cast<int>(wl) + 12 || avgHeight(endB.x, endB.y) < static_cast<int>(wl) + 12) continue;
            std::vector<Vec2i> span;
            bool clear = true;
            for (int k = -a - 1; k <= b + 1 && clear; ++k) {
              const Vec2i t{sx + ax * k, sy + ay * k};
              clear = !taken(t.x, t.y);
              span.push_back(t);
            }
            if (clear && (best.empty() || span.size() < best.size())) best = std::move(span);
          }
        if (best.size() < 3) continue;
        const Vec2i endA = best.front(), endB = best.back();
        // The deck sits at the higher bank; both ends are set level with it
        // and the ground round them eased in, with a stretch of road on.
        const int deck = std::max(avgHeight(endA.x, endA.y), avgHeight(endB.x, endB.y));
        const int ax = endB.x != endA.x ? 1 : 0, ay = 1 - ax;
        for (const Vec2i& e : {endA, endB}) {
          const int out = (e.x == endA.x && e.y == endA.y) ? -1 : 1;
          for (int cy2 = e.y - 2; cy2 <= e.y + 3; ++cy2)
            for (int cx2 = e.x - 2; cx2 <= e.x + 3; ++cx2) {
              if (cx2 < 0 || cy2 < 0 || cx2 > W || cy2 > H) continue;
              const int d = std::max({e.x - cx2, cx2 - (e.x + 1), e.y - cy2, cy2 - (e.y + 1), 0});
              const int h = corner(cx2, cy2);
              // On the river side only the abutment's own corners move.
              const int along = (cx2 - e.x) * ax + (cy2 - e.y) * ay;
              if (d > 0 && along * out < 0) continue;
              setCorner(cx2, cy2, static_cast<uint16_t>(std::clamp(h, deck - 16 * d, deck + 16 * d)));
            }
          for (int k = 0; k <= 2; ++k) {  // road onto the bridge
            const int tx = e.x + ax * out * k, ty = e.y + ay * out * k;
            if (!inBounds(tx, ty)) continue;
            for (int q = 0; q < 4; ++q) {
              uint8_t* w = splat(tx * SPLAT_PER_TILE + (q & 1), ty * SPLAT_PER_TILE + (q >> 1));
              for (int l = 0; l < LAYER_COUNT; ++l) w[l] = l == int(Layer::Road) ? 255 : 0;
            }
          }
        }
        for (const Vec2i& t : best) lots.push_back({t.x, t.y, wood ? kWoodBridgeLot : kBridgeLot});
      }
    }
  }};
  const int homes[4][2] = {{W * 17 / 100, H / 2}, {W * 83 / 100, H / 2}, {W / 2, H * 17 / 100}, {W / 2, H * 83 / 100}};
  auto plotFree = [&](int cx, int cy, int half) {
    for (const auto& sp : homes)
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
  if (!settlements) return;
  if (urban) {
    for (int by = 0; by + 12 <= H; by += 12)
      for (int bx = 0; bx + 12 <= W; bx += 12) {
        // Each street block holds up to four buildings.
        for (int k = 0; k < 4; ++k) {
          const int cx = bx + 3 + (k % 2) * 6, cy = by + 3 + (k / 2) * 6;
          if (rng.below(100) < 22 || !plotFree(cx, cy, 1)) continue;
          const uint32_t pick = rng.below(100);
          uint8_t kind = pick < 30 ? 2 : pick < 50 ? 4 : pick < 68 ? 1 : pick < 78 ? 0 : pick < 84 ? 9 : pick < 87 ? 11
                         : pick < 93 ? 14 : pick < 96 ? 12 : 3;
          if ((kind == 9 || kind == 12 || kind == 14) && !plotFree(cx, cy, 2)) kind = 2;  // the big ones need more room
          lots.push_back({cx, cy, kind});
          clearProps(cx, cy, (kind == 9 || kind == 12 || kind == 14) ? 2 : 1);
        }
      }
    return;
  }
  // --- Compact settlements (9.8) -------------------------------------------------
  // Villages and towns the way they stand in Europe: narrow houses built wall
  // to wall in rows along the street, lanes leading off it between them,
  // blocks closed round a courtyard, the church and the inn on a small
  // square, a farm at the end of the street. A settlement takes a fraction
  // of the ground the scattered houses did and has several times as many
  // buildings. Row houses are one tile each and join up with their
  // neighbours (kRowLot + style); the landmarks are the free-standing
  // buildings. Laid with their own random stream. Where the winter and the
  // jungle build in timber the houses stand apart, but as close together.
  Rng trng(seed ^ 0x51ED270B9F3A6C15ULL);
  auto tr = [&](int lo, int hi) { return static_cast<int>(trng.range(lo, hi)); };
  const size_t tiles = static_cast<size_t>(W) * H;
  auto at = [&](int x, int y) { return static_cast<size_t>(y) * W + x; };
  // What stands on each tile: 0 nothing, 1 a building, 2 a lane or square (paved), 3 a yard (bare earth).
  std::vector<uint8_t> occ(tiles, 0);
  std::vector<int32_t> piece(tiles, -1);  // which row, block or building a built tile belongs to
  std::vector<uint8_t> rowPiece;          // per piece: row houses (which join) or a building standing alone
  std::vector<Prop> decor;                // trees on the green, ricks, crates: added once the streets are cleared
  int pieces = 0;
  // Never built on: the map's rim, steep or wet ground, the starting areas,
  // logistics points, and the approaches to fords and bridges.
  std::vector<uint8_t> bad(tiles, 0);
  {
    std::vector<Vec2f> fords;
    for (const Curve& road : plan.roads)
      for (int i = 0; i <= 600; ++i) {
        const Vec2f p = curvePoint(road, i / 600.0f);
        bool wet = false;
        for (const Curve& r : plan.rivers) wet = wet || curveDistance(r, p) < r.halfWidth + 1.0f;
        if (wet && (fords.empty() || std::hypot(fords.back().x - p.x, fords.back().y - p.y) > 2.0f)) fords.push_back(p);
      }
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const TileInfo& t = info(x, y);
        bool b = x < 3 || y < 3 || x >= W - 3 || y >= H - 3 || t.slope > 18 || t.waterDepth > -12 || oil(x, y) > 0;
        for (int oy = -2; oy <= 2 && !b; ++oy)  // not on the water's edge either
          for (int ox = -2; ox <= 2 && !b; ++ox) b = inBounds(x + ox, y + oy) && info(x + ox, y + oy).waterDepth > -4;
        for (const auto& sp : homes) b = b || (std::abs(x - sp[0]) < 15 && std::abs(y - sp[1]) < 15);
        for (const Vec2i& sp : supplyPoints) b = b || (std::abs(x - sp.x) < 5 && std::abs(y - sp.y) < 5);
        for (const Vec2f& f : fords) b = b || (std::abs(x + 0.5f - f.x) < 8.0f && std::abs(y + 0.5f - f.y) < 8.0f);
        bad[at(x, y)] = b;
      }
  }
  // Houses come up to the road's verge and no nearer.
  auto roadClear = [&](int x, int y) { return roadAt(x + 0.5f, y + 0.5f) >= 1.9f; };
  auto freeTile = [&](int x, int y) { return inBounds(x, y) && !bad[at(x, y)] && !occ[at(x, y)] && roadClear(x, y); };
  auto built = [&](int x, int y) { return inBounds(x, y) && occ[at(x, y)] == 1; };
  auto isRow = [&](int x, int y) { return built(x, y) && rowPiece[static_cast<size_t>(piece[at(x, y)])]; };
  // A row house may go on a free tile beside other row houses (they join),
  // but not hard against a free-standing building.
  auto canRow = [&](int x, int y) {
    if (!freeTile(x, y)) return false;
    for (int oy = -1; oy <= 1; ++oy)
      for (int ox = -1; ox <= 1; ++ox)
        if ((ox || oy) && built(x + ox, y + oy) && !isRow(x + ox, y + oy)) return false;
    return true;
  };
  auto newPiece = [&](bool row) {
    rowPiece.push_back(row);
    return pieces++;
  };
  auto putRow = [&](int x, int y, int p, uint8_t style) {
    lots.push_back({x, y, static_cast<uint8_t>(kRowLot + style)});
    occ[at(x, y)] = 1;
    piece[at(x, y)] = p;
  };
  // Keep a tile open: 2 paved, 3 a yard.
  auto keepOpen = [&](int x, int y, uint8_t what) {
    if (inBounds(x, y) && !bad[at(x, y)] && occ[at(x, y)] != 1 && (occ[at(x, y)] == 0 || what == 2)) occ[at(x, y)] = what;
  };
  // A free-standing building on the tiles from (x0, y0), a clear tile all round.
  auto canStand = [&](int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h; ++y)
      for (int x = x0; x < x0 + w; ++x)
        if (!freeTile(x, y)) return false;
    for (int y = y0 - 1; y <= y0 + h; ++y)
      for (int x = x0 - 1; x <= x0 + w; ++x)
        if (built(x, y)) return false;
    return true;
  };
  auto stand = [&](int x0, int y0, uint8_t kind) {
    const Vec2i sz = lotFootprint(kind);
    if (!canStand(x0, y0, sz.x, sz.y)) return false;
    lots.push_back({x0 + sz.x / 2, y0 + sz.y / 2, kind});
    const int p = newPiece(false);
    for (int y = y0; y < y0 + sz.y; ++y)
      for (int x = x0; x < x0 + sz.x; ++x) occ[at(x, y)] = 1, piece[at(x, y)] = p;
    return true;
  };
  // A well or a shed may stand on a square or in a yard, a step clear of the houses.
  auto standSmall = [&](int x, int y, uint8_t kind) {
    if (!inBounds(x, y) || bad[at(x, y)] || occ[at(x, y)] == 1 || !roadClear(x, y)) return false;
    for (int oy = -1; oy <= 1; ++oy)
      for (int ox = -1; ox <= 1; ++ox)
        if (built(x + ox, y + oy)) return false;
    lots.push_back({x, y, kind});
    occ[at(x, y)] = 1;
    piece[at(x, y)] = newPiece(false);
    return true;
  };
  // The first place round (x, y) where the building can stand, nearest first.
  auto standNear = [&](int x, int y, int reach, uint8_t kind) {
    const Vec2i sz = lotFootprint(kind);
    for (int r = 0; r <= reach; ++r)
      for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
          if (std::max(std::abs(dx), std::abs(dy)) == r && stand(x + dx - sz.x / 2, y + dy - sz.y / 2, kind)) return true;
    return false;
  };
  auto rowStyle = [&](bool town) -> uint8_t {
    const int p = tr(0, 99);
    if (desert) return p < 68 ? 3 : 4;                        // mud brick, low and tall
    if (winter) return p < 50 ? 2 : p < 82 ? 0 : 1;           // timber, plaster, the odd brick front
    if (town) return p < 42 ? 0 : p < 74 ? 1 : 2;             // plaster, brick shops, timber
    return p < 52 ? 0 : p < 86 ? 2 : 1;
  };
  // Timber country: log houses and stilt huts stand apart.
  const bool apart = tropical || (winter && !plan.centralTown);
  auto apartKind = [&]() -> uint8_t {
    if (tropical) return 29;
    const int p = tr(0, 99);
    return p < 72 ? 8 : p < 82 ? 6 : p < 90 ? 1 : 4;  // log houses, a barn, a burnt-out one, a ruin
  };

  // The road a settlement is being laid along.
  const Curve* street = nullptr;
  auto streetDist = [&](int x, int y) { return curveDistance(*street, {x + 0.5f, y + 0.5f}); };
  // Where that road runs at `a` tiles along from (cx, cy): the tile across the
  // axis nearest its middle (false when the road has gone elsewhere).
  auto roadAcross = [&](int cx, int cy, bool alongX, int a, int& kc) {
    float best = 1e9f;
    for (int k = -8; k <= 8; ++k) {
      const int x = alongX ? cx + a : cx + k, y = alongX ? cy + k : cy + a;
      if (!inBounds(x, y)) continue;
      const float d = streetDist(x, y);
      if (d < best) best = d, kc = k;
    }
    return best < 1.5f;
  };
  // Houses along one or both sides of the road (`sides`: 1 the low side, 2
  // the high side, 3 both) for `reach` tiles each way from (cx, cy), leaving
  // the first `skip` clear. Rows of two to five, an alley between rows. A row
  // is straight: where the road bends away from it or into it the row ends
  // and the next one starts on a new line.
  auto frontage = [&](int cx, int cy, bool alongX, int reach, int skip, int sides, bool town) {
    int count = 0;
    auto tile = [&](int a, int k) { return alongX ? Vec2i{cx + a, cy + k} : Vec2i{cx + k, cy + a}; };
    for (int side = -1; side <= 1; side += 2) {
      if (!(sides & (side < 0 ? 1 : 2))) continue;
      int run = 0, runMax = tr(2, 5), p = newPiece(true), line = 0;
      for (int a = -reach; a <= reach; ++a) {
        int kc = 0;
        const bool here = std::abs(a) >= skip && roadAcross(cx, cy, alongX, a, kc);
        if (!here || run >= runMax) {  // the square, no road, or time for an alley
          if (run) p = newPiece(true), runMax = tr(2, 5);
          run = 0;
          continue;
        }
        if (run == 0) {
          // The row's line: far enough out to clear the verge for its first three houses.
          line = kc + side * 2;
          for (int j = 0; j < 3; ++j) {
            int kj = 0;
            if (std::abs(a + j) < skip || !roadAcross(cx, cy, alongX, a + j, kj)) break;
            int k = kj + side * 2;
            while (std::abs(k - kj) < 4 && inBounds(tile(a + j, k).x, tile(a + j, k).y) && !roadClear(tile(a + j, k).x, tile(a + j, k).y)) k += side;
            if ((k - line) * side > 0) line = k;
          }
        }
        const Vec2i t = tile(a, line);
        bool put = false;
        if (inBounds(t.x, t.y) && roadClear(t.x, t.y) && streetDist(t.x, t.y) < 4.4f) {
          if (apart) {
            // A log house or hut on this tile and the one behind, the next one a tile on.
            const int x0 = alongX ? t.x : std::min(t.x, t.x + side), y0 = alongX ? std::min(t.y, t.y + side) : t.y;
            if (stand(x0, y0, apartKind())) put = true, a += 2;
          } else if (canRow(t.x, t.y)) {
            putRow(t.x, t.y, p, rowStyle(town));
            put = true;
          }
        }
        if (put) {
          ++count;
          ++run;
        } else if (run) {
          run = 0;
          p = newPiece(true);
        }
      }
    }
    return count;
  };
  // A lane leading off the road at `a0` along, `len` tiles out to `side`,
  // houses down both sides of it. Returns the tile at its far end.
  auto lane = [&](int cx, int cy, bool alongX, int a0, int side, int len, Vec2i& end, int& k0) {
    int kc = 0;
    if (!roadAcross(cx, cy, alongX, a0, kc)) return false;
    auto tile = [&](int a, int k) { return alongX ? Vec2i{cx + a, cy + k} : Vec2i{cx + k, cy + a}; };
    // From the road out: every lane tile must be buildable ground.
    for (int i = 1; i <= len + 2; ++i) {
      const Vec2i t = tile(a0, kc + side * i);
      if (!inBounds(t.x, t.y) || bad[at(t.x, t.y)] || occ[at(t.x, t.y)] == 1) return false;
    }
    k0 = kc + side * 2;
    while (std::abs(k0 - kc) < 4 && !roadClear(tile(a0, k0).x, tile(a0, k0).y)) k0 += side;
    for (int i = -2; i < len; ++i) keepOpen(tile(a0, k0 + side * i).x, tile(a0, k0 + side * i).y, 2);
    end = tile(a0, k0 + side * len);
    return true;
  };
  auto laneHouses = [&](int cx, int cy, bool alongX, int a0, int side, int len, int k0, bool town) {
    auto tile = [&](int a, int k) { return alongX ? Vec2i{cx + a, cy + k} : Vec2i{cx + k, cy + a}; };
    int count = 0;
    for (int da = -1; da <= 1; da += 2) {
      int run = 0, runMax = tr(3, 5), p = newPiece(true);
      for (int i = 0; i < len; ++i) {
        const Vec2i t = tile(a0 + da, k0 + side * i);
        if (run >= runMax) {
          run = 0, runMax = tr(2, 4), p = newPiece(true);
          continue;
        }
        if (apart) {
          const int x0 = alongX ? std::min(t.x, t.x + da) : std::min(t.x, t.x + side), y0 = alongX ? std::min(t.y, t.y + side) : std::min(t.y, t.y + da);
          if (stand(x0, y0, apartKind())) ++count, i += 2;
        } else if (canRow(t.x, t.y)) {
          putRow(t.x, t.y, p, rowStyle(town));
          ++count;
          ++run;
        } else if (run) {
          run = 0, p = newPiece(true);
        }
      }
    }
    return count;
  };
  // A block: houses all round a yard, one left out for the way in.
  auto block = [&](int x0, int y0, int w, int h, bool town) {
    for (int y = y0 - 1; y <= y0 + h; ++y)
      for (int x = x0 - 1; x <= x0 + w; ++x) {
        const bool inside = x >= x0 && x < x0 + w && y >= y0 && y < y0 + h;
        if (inside ? !freeTile(x, y) : built(x, y)) return false;
      }
    const int p = newPiece(true);
    // The way in: not a corner house.
    int gap = -1;
    if (w >= 3 && h >= 3) {
      const int n = 2 * (w - 2) + 2 * (h - 2);
      gap = tr(0, n - 1);
    }
    int edge = 0;
    for (int y = y0; y < y0 + h; ++y)
      for (int x = x0; x < x0 + w; ++x) {
        const bool onX = x == x0 || x == x0 + w - 1, onY = y == y0 || y == y0 + h - 1;
        if (!onX && !onY) {
          occ[at(x, y)] = 3;
          continue;
        }
        if (onX != onY && edge++ == gap) {
          occ[at(x, y)] = 3;
          continue;
        }
        putRow(x, y, p, rowStyle(town));
      }
    // The lane round it.
    for (int y = y0 - 1; y <= y0 + h; ++y)
      for (int x = x0 - 1; x <= x0 + w; ++x) keepOpen(x, y, 2);
    return true;
  };
  auto prop = [&](PropKind kind, int tx, int ty, uint32_t variants) {
    decor.push_back({kind, static_cast<uint8_t>(trng.below(variants)), tx * 256 + tr(70, 186), ty * 256 + tr(70, 186)});
    keepOpen(tx, ty, 3);
  };
  // A landmark in the street front: on `side` of the road, from `from` tiles
  // along onwards in steps of `dir`, the first place it fits.
  auto standOnStreet = [&](int cx, int cy, bool alongX, int from, int dir, int side, uint8_t kind) {
    const Vec2i sz = lotFootprint(kind);
    const int w = alongX ? sz.x : sz.y, d = alongX ? sz.y : sz.x;  // along the street, and back from it
    for (int step = 0; step < 6; ++step) {
      const int a = from + dir * step, a0 = dir > 0 ? a : a - w + 1;
      int k0 = 0, k1 = 0;
      if (!roadAcross(cx, cy, alongX, a0, k0) || !roadAcross(cx, cy, alongX, a0 + w - 1, k1)) continue;
      const int kc = side > 0 ? std::max(k0, k1) : std::min(k0, k1);
      for (int out = 2; out <= 4; ++out) {
        const int k = side > 0 ? kc + out : kc - out - d + 1;
        if (stand(alongX ? cx + a0 : cx + k, alongX ? cy + k : cy + a0, kind)) return true;
      }
    }
    return false;
  };

  // --- The market town at the crossroads ---
  std::vector<Vec2i> centres;  // of every settlement, to keep them apart
  if (plan.centralTown && !tropical && plan.roads.size() >= 2) {
    // Where the two roads really cross.
    Vec2f cross{Wf * 0.5f, Hf * 0.5f};
    float bestD = 1e9f;
    for (int i = 0; i <= 400; ++i) {
      const Vec2f p = curvePoint(plan.roads[0], 0.3f + 0.4f * i / 400.0f);
      const float d = curveDistance(plan.roads[1], p);
      if (d < bestD) bestD = d, cross = p;
    }
    const int tx0 = static_cast<int>(cross.x), ty0 = static_cast<int>(cross.y);
    const int R = std::clamp(std::min(W, H) / 9, 9, 16);
    centres.push_back({tx0, ty0});
    // The market square, and what stands on it.
    for (int y = ty0 - 3; y <= ty0 + 3; ++y)
      for (int x = tx0 - 3; x <= tx0 + 3; ++x) keepOpen(x, y, 2);
    // Which way each road runs through the town.
    bool runsX[2] = {true, false};
    for (int r = 0; r < 2; ++r) {
      const Curve& road = plan.roads[static_cast<size_t>(r)];
      float bestU = 0.5f, bd = 1e9f;
      for (int i = 0; i <= 400; ++i) {
        const Vec2f p = curvePoint(road, i / 400.0f);
        const float d = std::hypot(p.x - cross.x, p.y - cross.y);
        if (d < bd) bd = d, bestU = i / 400.0f;
      }
      const Vec2f a = curvePoint(road, bestU - 0.08f), b = curvePoint(road, bestU + 0.08f);
      runsX[r] = std::abs(b.x - a.x) >= std::abs(b.y - a.y);
    }
    // In the street fronts on the square: the church and the town hall on
    // one road, facing each other across the square; the inn and the market
    // stalls on the other.
    const int d0 = tr(0, 1) ? 1 : -1, s0 = tr(0, 1) ? 1 : -1, d1 = tr(0, 1) ? 1 : -1, s1 = tr(0, 1) ? 1 : -1;
    street = &plan.roads[0];
    standOnStreet(tx0, ty0, runsX[0], 4 * d0, d0, s0, desert ? 28 : 3);
    standOnStreet(tx0, ty0, runsX[0], -4 * d0, -d0, -s0, desert ? 28 : winter ? 14 : 23);
    street = &plan.roads[1];
    standOnStreet(tx0, ty0, runsX[1], 4 * d1, d1, s1, desert ? 5 : 20);
    standOnStreet(tx0, ty0, runsX[1], -4 * d1, -d1, -s1, 27);
    // Houses along both roads through the town, then blocks packed in behind
    // them from the middle outwards.
    for (int r = 0; r < 2; ++r) {
      street = &plan.roads[static_cast<size_t>(r)];
      frontage(tx0, ty0, runsX[r], R + 3, 4, 3, true);
    }
    // Public buildings among the blocks: a school, a station by the road, a warehouse.
    if (!desert) {
      const uint8_t pub[3] = {14, 12, 9};
      for (int i = 0; i < 3; ++i)
        for (int tries = 0; tries < 12; ++tries) {
          const int x = tx0 + tr(-R, R), y = ty0 + tr(-R, R);
          if (std::max(std::abs(x - tx0), std::abs(y - ty0)) < 6) continue;
          const Vec2i sz = lotFootprint(pub[i]);
          if (!stand(x, y, pub[i])) continue;
          for (int yy = y - 1; yy <= y + sz.y; ++yy)
            for (int xx = x - 1; xx <= x + sz.x; ++xx) keepOpen(xx, yy, 3);
          break;
        }
    }
    std::vector<Vec2i> spots2;
    for (int y = ty0 - R; y <= ty0 + R; ++y)
      for (int x = tx0 - R; x <= tx0 + R; ++x) spots2.push_back({x, y});
    std::stable_sort(spots2.begin(), spots2.end(), [&](Vec2i p, Vec2i q) {
      return std::max(std::abs(p.x - tx0), std::abs(p.y - ty0)) < std::max(std::abs(q.x - tx0), std::abs(q.y - ty0));
    });
    static const int sizes[6][2] = {{5, 4}, {4, 5}, {4, 4}, {4, 3}, {3, 4}, {3, 3}};
    for (const Vec2i& sp : spots2) {
      if (!freeTile(sp.x, sp.y)) continue;
      const int first = tr(0, 5);
      for (int i = 0; i < 6; ++i) {
        const int w = sizes[(first + i) % 6][0], h = sizes[(first + i) % 6][1];
        // Anchored on the corner nearest the middle, growing outwards.
        const int x0 = sp.x >= tx0 ? sp.x : sp.x - w + 1, y0 = sp.y >= ty0 ? sp.y : sp.y - h + 1;
        const int far = std::max({std::abs(x0 - tx0), std::abs(x0 + w - 1 - tx0), std::abs(y0 - ty0), std::abs(y0 + h - 1 - ty0)});
        if (far > R) continue;
        if (block(x0, y0, w, h, true)) break;
      }
    }
    // Short rows fill what is left along the lanes.
    for (const Vec2i& sp : spots2) {
      if (!inBounds(sp.x, sp.y) || occ[at(sp.x, sp.y)] != 0 || std::max(std::abs(sp.x - tx0), std::abs(sp.y - ty0)) > R - 2) continue;
      for (int dir = 0; dir < 2; ++dir) {
        const int n = tr(3, 5);
        bool ok = true;
        for (int i = -1; i <= n && ok; ++i)
          for (int j = -1; j <= 1 && ok; ++j) {
            const int x = sp.x + (dir ? j : i), y = sp.y + (dir ? i : j);
            const bool inside = i >= 0 && i < n && j == 0;
            ok = inside ? freeTile(x, y) : !built(x, y);
          }
        if (!ok) continue;
        const int p = newPiece(true);
        for (int i = 0; i < n; ++i) putRow(sp.x + (dir ? 0 : i), sp.y + (dir ? i : 0), p, rowStyle(true));
        for (int i = -1; i <= n; ++i)
          for (int j = -1; j <= 1; ++j) keepOpen(sp.x + (dir ? j : i), sp.y + (dir ? i : j), 2);
        break;
      }
    }
  }

  // --- Villages along the roads ---
  if (!plan.roads.empty()) {
    const int villages = std::max(1, W * H / (128 * 128)) * (desert ? 2 : plan.hamletScale);
    for (int v = 0; v < villages; ++v) {
      const Curve& road = plan.roads[static_cast<size_t>(v) % plan.roads.size()];
      // A stretch of road with room beside it, away from the other settlements.
      Vec2i c{-1, -1};
      bool alongX = true;
      for (int tries = 0; tries < 24 && c.x < 0; ++tries) {
        const float u = static_cast<float>(tr(80, 920)) / 1000.0f;
        const Vec2f p = curvePoint(road, u), p0 = curvePoint(road, u - 0.02f), p1 = curvePoint(road, u + 0.02f);
        const int x = static_cast<int>(p.x), y = static_cast<int>(p.y);
        if (!inBounds(x, y) || x < 12 || y < 12 || x >= W - 12 || y >= H - 12) continue;
        bool room = true;
        for (const Vec2i& o : centres) room = room && std::max(std::abs(o.x - x), std::abs(o.y - y)) >= (tries < 16 ? 26 : 18);
        int good = 0;
        for (int dy = -4; dy <= 4; ++dy)
          for (int dx = -4; dx <= 4; ++dx) good += !bad[at(x + dx, y + dy)];
        if (!room || good < 70) continue;
        c = {x, y};
        alongX = std::abs(p1.x - p0.x) >= std::abs(p1.y - p0.y);
      }
      if (c.x < 0) continue;
      centres.push_back(c);
      street = &road;
      const int size = tr(0, 99) < 30 ? 0 : tr(0, 99) < 70 ? 1 : 2;  // hamlet, village, big village
      const int reach = size == 0 ? tr(3, 4) : size == 1 ? tr(5, 7) : tr(7, 9);
      const int sideA = tr(0, 1) ? 1 : -1;
      const uint8_t church = tropical ? 29 : desert ? 28 : 3;
      Vec2i wellAt{-1, -1};
      if (size >= 1) {
        // The church lane: off the street to one side, the church on a green at its end.
        const int a0 = tr(-1, 1), len = size == 1 ? tr(3, 4) : tr(4, 6);
        Vec2i end;
        int k0 = 0;
        if (lane(c.x, c.y, alongX, a0, sideA, len, end, k0)) {
          const Vec2i sz = lotFootprint(church);
          // The church across the end of the lane, the green in front of it.
          const int ex = alongX ? end.x - sz.x / 2 : (sideA > 0 ? end.x + 1 : end.x - sz.x);
          const int ey = alongX ? (sideA > 0 ? end.y + 1 : end.y - sz.y) : end.y - sz.y / 2;
          for (int j = -1; j <= 1; ++j) keepOpen(alongX ? end.x + j : end.x, alongX ? end.y : end.y + j, 2);
          if (stand(ex, ey, church) && !tropical && !desert) {
            prop(winter ? PropKind::SnowPine : PropKind::Tree, alongX ? end.x - 2 : end.x, alongX ? end.y : end.y - 2, 3);
            prop(winter ? PropKind::SnowPine : PropKind::Tree, alongX ? end.x + 2 : end.x, alongX ? end.y : end.y + 2, 3);
          }
          laneHouses(c.x, c.y, alongX, a0, sideA, len, k0, false);
          wellAt = alongX ? Vec2i{end.x + (tr(0, 1) ? 1 : -1), end.y} : Vec2i{end.x, end.y + (tr(0, 1) ? 1 : -1)};
        }
      }
      if (size >= 2) {
        // A second lane on the other side, to the inn (or the market, or a villa).
        const int a0 = tr(0, 1) ? tr(3, 4) : tr(-4, -3), len = tr(3, 4);
        Vec2i end;
        int k0 = 0;
        if (lane(c.x, c.y, alongX, a0, -sideA, len, end, k0)) {
          const uint8_t inn = tropical ? 29 : desert ? 27 : winter ? 14 : 20;
          const Vec2i sz = lotFootprint(inn);
          const int ex = alongX ? end.x - sz.x / 2 : (-sideA > 0 ? end.x + 1 : end.x - sz.x);
          const int ey = alongX ? (-sideA > 0 ? end.y + 1 : end.y - sz.y) : end.y - sz.y / 2;
          stand(ex, ey, inn);
          laneHouses(c.x, c.y, alongX, a0, -sideA, len, k0, false);
        }
      }
      frontage(c.x, c.y, alongX, reach, 0, size == 0 && tr(0, 99) < 40 ? (sideA > 0 ? 2 : 1) : 3, false);
      if (size == 2 && !apart) {
        // A big village has a block or two behind the street, round a yard.
        std::vector<Vec2i> behind;
        for (int k = 4; k <= 8; ++k)
          for (int a = -reach; a <= reach; ++a)
            for (int sgn = -1; sgn <= 1; sgn += 2) behind.push_back(alongX ? Vec2i{c.x + a, c.y + k * sgn} : Vec2i{c.x + k * sgn, c.y + a});
        int blocks = 0;
        for (size_t i = 0; i < behind.size() && blocks < 2; ++i) {
          const Vec2i sp = behind[(i * 7 + static_cast<size_t>(v) * 3) % behind.size()];
          const int w = tr(3, 4), h = tr(3, 4);
          blocks += block(sp.x, sp.y, w, h, false);
        }
      }
      if (tropical) continue;
      // The well by the street, and a farm at one end: barn, shed, ricks.
      if (wellAt.x < 0 || !standSmall(wellAt.x, wellAt.y, 10)) {
        int kc = 0;
        const int a = (tr(0, 1) ? 1 : -1) * (reach + 1);
        if (roadAcross(c.x, c.y, alongX, a, kc)) standSmall(alongX ? c.x + a : c.x + kc + sideA * 3, alongX ? c.y + kc + sideA * 3 : c.y + a, 10);
      }
      if (!desert) {
        const int end = (tr(0, 1) ? 1 : -1) * (reach + 3), out = (tr(0, 1) ? 1 : -1) * 4;
        const int fx = c.x + (alongX ? end : out), fy = c.y + (alongX ? out : end);
        if (standNear(fx, fy, 3, size == 0 ? 6 : 13)) {
          standNear(fx + (alongX ? 3 : 0), fy + (alongX ? 0 : 3), 2, winter ? 7 : 6);
          standNear(fx - (alongX ? 2 : 0), fy - (alongX ? 0 : 2), 2, 7);
          for (int i = 0; i < 3; ++i) {
            const int hx = fx + tr(-3, 3), hy = fy + tr(-3, 3);
            if (freeTile(hx, hy)) prop(winter ? PropKind::Logs : PropKind::Haystack, hx, hy, 3);
          }
        }
      }
    }
  }

  // A house standing by itself is a small one: the tall fronts belong in a row.
  for (Lot& l : lots) {
    if (l.kind < kRowLot || l.kind >= kRowLot + kRowLots) continue;
    if (isRow(l.tx + 1, l.ty) || isRow(l.tx - 1, l.ty) || isRow(l.tx, l.ty + 1) || isRow(l.tx, l.ty - 1)) continue;
    if (l.kind == kRowLot + 1) l.kind = kRowLot;
    if (l.kind == kRowLot + 4) l.kind = kRowLot + 3;
  }
  if (std::getenv("LEGION_DEBUG_TOWN")) {
    for (const Vec2i& c : centres) {
      std::fprintf(stderr, "settlement at %d,%d\n", c.x, c.y);
      for (int y = c.y - 18; y <= c.y + 18; ++y) {
        for (int x = c.x - 22; x <= c.x + 22; ++x) {
          char ch = ' ';
          if (!inBounds(x, y)) ch = '?';
          else if (occ[at(x, y)] == 1) ch = isRow(x, y) ? '#' : 'B';
          else if (roadAt(x + 0.5f, y + 0.5f) < 1.0f) ch = '=';
          else if (occ[at(x, y)] == 2) ch = ':';
          else if (occ[at(x, y)] == 3) ch = '.';
          else if (bad[at(x, y)]) ch = 'x';
          else if (!roadClear(x, y)) ch = '-';
          std::fputc(ch, stderr);
        }
        std::fputc('\n', stderr);
      }
    }
  }
  // --- The ground under them ---
  // Every row, block and building stands level (a row on a slope is a row
  // of broken roofs): each group of touching buildings is set at the mean
  // height of its ground, neighbours across a lane are brought within a
  // step of each other, and the land round about is eased in.
  {
    std::vector<int32_t> comp(tiles, -1);
    std::vector<int64_t> sum;
    std::vector<int32_t> count;
    std::vector<Vec2i> stack;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        if (occ[at(x, y)] != 1 || comp[at(x, y)] >= 0) continue;
        const int32_t id = static_cast<int32_t>(sum.size());
        sum.push_back(0);
        count.push_back(0);
        comp[at(x, y)] = id;
        stack.push_back({x, y});
        while (!stack.empty()) {
          const Vec2i t = stack.back();
          stack.pop_back();
          sum[static_cast<size_t>(id)] += corner(t.x, t.y) + corner(t.x + 1, t.y) + corner(t.x, t.y + 1) + corner(t.x + 1, t.y + 1);
          count[static_cast<size_t>(id)] += 4;
          for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
              const int nx = t.x + ox, ny = t.y + oy;
              if (!inBounds(nx, ny) || occ[at(nx, ny)] != 1 || comp[at(nx, ny)] >= 0) continue;
              comp[at(nx, ny)] = id;
              stack.push_back({nx, ny});
            }
        }
      }
    const size_t n = sum.size();
    if (n) {
      std::vector<float> level(n);
      for (size_t i = 0; i < n; ++i)
        level[i] = std::max(static_cast<float>(sum[i]) / static_cast<float>(count[i]), static_cast<float>(wl) + 14.0f);
      // Neighbours: groups with tiles within three of each other.
      std::vector<std::pair<int32_t, int32_t>> pairs;
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
          const int32_t a = comp[at(x, y)];
          if (a < 0) continue;
          for (int oy = 0; oy <= 3; ++oy)
            for (int ox = -3; ox <= 3; ++ox) {
              if (!inBounds(x + ox, y + oy)) continue;
              const int32_t b = comp[at(x + ox, y + oy)];
              if (b >= 0 && b != a) pairs.push_back({std::min(a, b), std::max(a, b)});
            }
        }
      std::sort(pairs.begin(), pairs.end());
      pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
      constexpr float kStep = 8.0f;
      for (int pass = 0; pass < 12; ++pass)
        for (const auto& [a, b] : pairs) {
          const float d = level[static_cast<size_t>(b)] - level[static_cast<size_t>(a)];
          if (std::abs(d) <= kStep) continue;
          const float move = (std::abs(d) - kStep) * (d > 0 ? 1.0f : -1.0f);
          const float wa = static_cast<float>(count[static_cast<size_t>(b)]) / static_cast<float>(count[static_cast<size_t>(a)] + count[static_cast<size_t>(b)]);
          level[static_cast<size_t>(a)] += move * wa;
          level[static_cast<size_t>(b)] -= move * (1.0f - wa);
        }
      // Set the corners under the buildings; bound the corners round them.
      constexpr int kReach = 6, kEase = 9;
      std::vector<int32_t> fixed(static_cast<size_t>(CX) * CY, -1), lo(static_cast<size_t>(CX) * CY, -1000000), hi(static_cast<size_t>(CX) * CY, 1000000);
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
          const int32_t c = comp[at(x, y)];
          if (c < 0) continue;
          const int32_t l = static_cast<int32_t>(level[static_cast<size_t>(c)] + 0.5f);
          for (int q = 0; q < 4; ++q) fixed[static_cast<size_t>(y + (q >> 1)) * CX + x + (q & 1)] = l;
        }
      for (int cy = 0; cy < CY; ++cy)
        for (int cx = 0; cx < CX; ++cx) {
          const int32_t l = fixed[static_cast<size_t>(cy) * CX + cx];
          if (l < 0) continue;
          for (int oy = -kReach; oy <= kReach; ++oy)
            for (int ox = -kReach; ox <= kReach; ++ox) {
              const int nx = cx + ox, ny = cy + oy;
              if (nx < 0 || ny < 0 || nx >= CX || ny >= CY) continue;
              const int d = std::max(std::abs(ox), std::abs(oy));
              const size_t i = static_cast<size_t>(ny) * CX + nx;
              lo[i] = std::max(lo[i], l - kEase * d);
              hi[i] = std::min(hi[i], l + kEase * d);
            }
        }
      for (int cy = 0; cy < CY; ++cy)
        for (int cx = 0; cx < CX; ++cx) {
          const size_t i = static_cast<size_t>(cy) * CX + cx;
          if (hi[i] == 1000000) continue;
          const int32_t h = corner(cx, cy);
          if (fixed[i] < 0 && h < static_cast<int32_t>(wl) + 8) continue;  // banks and beds stay as they are
          int32_t v = fixed[i] >= 0 ? fixed[i] : lo[i] > hi[i] ? (lo[i] + hi[i]) / 2 : std::clamp(h, lo[i], hi[i]);
          if (fixed[i] < 0) v = std::max(v, static_cast<int32_t>(wl) + 8);
          if (v != h) setCorner(cx, cy, static_cast<uint16_t>(std::clamp(v, 0, 65535)));
        }
    }
  }
  // Paving: lanes and squares, and the strip between a house front and the
  // road; bare earth in the yards and under the houses.
  {
    auto paint = [&](int tx, int ty, Layer layer, int strength) {
      for (int q = 0; q < 4; ++q) {
        uint8_t* w = splat(tx * SPLAT_PER_TILE + (q & 1), ty * SPLAT_PER_TILE + (q >> 1));
        int total = 0;
        for (int l = 0; l < LAYER_COUNT; ++l) {
          w[l] = static_cast<uint8_t>(w[l] * (255 - strength) / 255);
          total += w[l];
        }
        w[int(layer)] = static_cast<uint8_t>(w[int(layer)] + 255 - total);
      }
    };
    const Layer yard = desert ? Layer::Sand : Layer::Dirt;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const uint8_t o = occ[at(x, y)];
        if (o == 2) paint(x, y, Layer::Road, 255);
        else if (o == 3) paint(x, y, yard, winter ? 150 : 230);
        else if (o == 1 && isRow(x, y)) paint(x, y, yard, winter ? 120 : 200);
        else if (o == 0 && roadAt(x + 0.5f, y + 0.5f) < 2.6f &&
                 (isRow(x + 1, y) || isRow(x - 1, y) || isRow(x, y + 1) || isRow(x, y - 1)))
          paint(x, y, Layer::Road, 255);
      }
    // No trees in the streets or through the roofs.
    std::erase_if(props_, [&](const Prop& p) {
      const int tx = p.x >> LEPTON_SHIFT, ty = p.y >> LEPTON_SHIFT;
      if (!inBounds(tx, ty)) return false;
      if (occ[at(tx, ty)] == 1 || occ[at(tx, ty)] == 2) return true;
      for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox)
          if (built(tx + ox, ty + oy)) return true;
      return false;
    });
    props_.insert(props_.end(), decor.begin(), decor.end());
    // Something in the bigger yards: a tree, a stack of crates.
    for (int y = 1; y < H - 1; ++y)
      for (int x = 1; x < W - 1; ++x)
        if (occ[at(x, y)] == 3 && occ[at(x + 1, y)] == 3 && hash32(static_cast<uint32_t>(x * 7919 + y * 104729) ^ s) % 3 == 0)
          props_.push_back({desert ? PropKind::Palm : winter ? PropKind::Logs : hash32(static_cast<uint32_t>(x + y * 977) ^ s) % 2 ? PropKind::Tree : PropKind::Crates,
                            static_cast<uint8_t>(trng.below(3)), x * 256 + tr(70, 186), y * 256 + tr(70, 186)});
  }
}

}  // namespace pioneer

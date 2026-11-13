#include "art_common.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

#include "pioneer/core/rng.hpp"
#include "pioneer/map/map.hpp"

namespace pioneer::art {

// Germany: dunkelgelb under olive and red-brown stripes, field-grey uniforms.
// The Soviet Union: plain 4BO green armour and khaki uniforms.
// Britain: plain SCC 2 service brown armour, khaki battledress, green helmets.
const FactionLook kFactions[kFactionLooks] = {
    // German: darker - weathered dark yellow under heavy green and brown, field-grey uniforms.
    {"ger", {118, 108, 76, 255}, {74, 80, 70, 255}, {66, 72, 64, 255}, false, true, {64, 74, 48, 255}, {84, 58, 42, 255}},
    // Soviet: 4BO green armour, tan-khaki uniforms, green helmets (white markings on the vehicles).
    {"rus", {78, 92, 52, 255}, {146, 130, 90, 255}, {74, 88, 54, 255}, true, false, {68, 80, 46, 255}, {92, 102, 64, 255}},
    // British: service-drab brown armour, khaki-brown battledress, dull green Brodie helmets.
    {"gbr", {92, 82, 60, 255}, {124, 106, 72, 255}, {82, 86, 60, 255}, false, false, {72, 74, 52, 255}, {60, 56, 44, 255}, true},
};

Rgba scale(Rgba c, float k) {
  auto ch = [&](uint8_t v) { return static_cast<uint8_t>(std::clamp(v * k, 0.0f, 255.0f)); };
  return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

uint8_t u8(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f)); }

VoxelPalette makePalette(const FactionLook& f) {
  VoxelPalette p;
  p.setTeam(TEAM, 1.0f);
  p.setTeam(TEAM_DARK, 0.72f);
  p.setTeam(TEAM_LIGHT, 1.3f);
  p.set(BASE, f.base);
  p.set(BASE_DARK, scale(f.base, 0.80f));
  p.set(BASE_LIGHT, scale(f.base, 1.12f));
  p.set(CAMO_A, f.camoA);
  p.set(CAMO_A_DARK, scale(f.camoA, 0.8f));
  p.set(CAMO_B, f.camoB);
  p.set(CAMO_B_DARK, scale(f.camoB, 0.8f));
  p.set(UNIFORM, f.uniform);
  p.set(UNIFORM_DARK, scale(f.uniform, 0.76f));
  p.set(UNIFORM_LIGHT, scale(f.uniform, 1.15f));
  p.set(HELMET, f.helmet);
  p.set(HELMET_DARK, scale(f.helmet, 0.75f));
  p.set(STEEL_DARK, {46, 48, 46, 255});
  p.set(STEEL, {104, 108, 102, 255});
  p.set(TRACK_A, {44, 40, 36, 255});
  p.set(TRACK_B, {86, 80, 70, 255});
  p.set(RUBBER, {32, 31, 30, 255});
  p.set(SKIN, {204, 160, 124, 255});
  p.set(GLASS, {110, 136, 146, 255});
  p.set(LAMP, {226, 206, 140, 255});
  p.set(BOOT, {40, 32, 24, 255});
  p.set(LEATHER, {70, 54, 38, 255});
  p.set(WEBBING, f.sloped ? Rgba{104, 94, 66, 255} : Rgba{56, 50, 40, 255});
  p.set(STOCK, {104, 72, 46, 255});
  p.set(BLANKET, f.sloped ? Rgba{112, 100, 74, 255} : Rgba{86, 84, 70, 255});
  p.set(CAP, f.sloped ? Rgba{118, 108, 74, 255} : Rgba{76, 80, 72, 255});
  p.set(MUD, {96, 80, 58, 255});
  p.set(MUD_DARK, {66, 54, 40, 255});
  p.set(DUST, scale({150, 138, 112, 255}, 0.95f));
  p.set(CHIP, {74, 72, 66, 255});
  p.set(MARK_WHITE, {206, 202, 190, 255});
  p.set(MARK_BLACK, {30, 30, 28, 255});
  p.set(MARK_RED, {150, 40, 32, 255});
  p.set(EXHAUST, {62, 46, 36, 255});

  p.set(TRUNK, {78, 62, 46, 255});
  p.set(LEAF_DARK, {44, 60, 30, 255});
  p.set(LEAF, {66, 86, 42, 255});
  p.set(LEAF_LIGHT, {96, 114, 58, 255});
  p.set(PINE, {44, 66, 44, 255});
  p.set(PINE_DARK, {30, 46, 34, 255});
  p.set(ROCK, {122, 116, 106, 255});
  p.set(ROCK_DARK, {88, 84, 76, 255});
  p.set(ROCK_LIGHT, {150, 144, 132, 255});
  p.set(BUSH, {72, 90, 46, 255});
  p.set(SNOW, {226, 230, 236, 255});
  p.set(SNOW_SHADE, {184, 192, 204, 255});
  p.set(PALM, {84, 110, 48, 255});
  p.set(PALM_DARK, {58, 80, 36, 255});
  p.set(PALM_TRUNK, {120, 98, 70, 255});
  p.set(DRY_GRASS, {150, 132, 86, 255});
  p.set(DRY_DARK, {112, 96, 62, 255});
  p.set(DEAD_WOOD, {96, 86, 74, 255});
  p.set(JUNGLE, {48, 84, 38, 255});
  p.set(JUNGLE_DARK, {30, 58, 28, 255});
  p.set(JUNGLE_LIGHT, {82, 116, 50, 255});
  p.set(SAND_ROCK, {176, 150, 112, 255});
  p.set(SAND_ROCK_DARK, {136, 112, 82, 255});
  p.set(PLASTER, {168, 160, 144, 255});
  p.set(PLASTER_DARK, {128, 122, 110, 255});
  p.set(TILE_ROOF, {136, 74, 52, 255});
  p.set(TILE_ROOF_DARK, {100, 56, 42, 255});
  p.set(CACTUS, {88, 112, 64, 255});
  p.set(BIRCH, {196, 192, 180, 255});

  // Structures: German works are grey concrete under dark roofs; Soviet ones
  // brick and weathered timber under green-grey tin.
  if (f.sloped) {
    p.set(CONCRETE, {128, 122, 108, 255});
    p.set(ROOF, {84, 96, 78, 255});
    p.set(WOOD, {112, 90, 64, 255});
    p.set(TANK_PAINT, {96, 104, 70, 255});
  } else {
    p.set(CONCRETE, {132, 132, 126, 255});
    p.set(ROOF, {78, 74, 72, 255});
    p.set(WOOD, {104, 88, 68, 255});
    p.set(TANK_PAINT, {116, 116, 110, 255});
  }
  p.set(CONCRETE_DARK, scale(p.color[CONCRETE], 0.78f));
  p.set(CONCRETE_LIGHT, scale(p.color[CONCRETE], 1.14f));
  p.set(BRICK, {124, 72, 56, 255});
  p.set(BRICK_DARK, {92, 54, 44, 255});
  p.set(ROOF_DARK, scale(p.color[ROOF], 0.76f));
  p.set(ROOF_LIGHT, scale(p.color[ROOF], 1.2f));
  p.set(WOOD_DARK, scale(p.color[WOOD], 0.72f));
  p.set(WINDOW, {40, 46, 50, 255});
  p.set(WINDOW_LIT, {196, 170, 110, 255});
  p.set(SANDBAG, {150, 134, 100, 255});
  p.set(SANDBAG_DARK, {118, 104, 76, 255});
  p.set(EARTH, {104, 86, 62, 255});
  p.set(EARTH_DARK, {74, 60, 44, 255});
  p.set(RUST, {120, 74, 46, 255});
  p.set(TANK_PAINT_DARK, scale(p.color[TANK_PAINT], 0.75f));
  p.set(CANVAS, {120, 116, 88, 255});
  p.set(CANVAS_DARK, {94, 90, 68, 255});
  p.set(SOOT, {48, 46, 44, 255});
  return p;
}

float noise2(float x, float y, uint32_t seed) {
  const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
  const float fx = x - ix, fy = y - iy;
  auto v = [&](int a, int b) { return (hash2(a, b, seed) & 1023) / 1023.0f; };
  const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  const float t = v(ix, iy) + (v(ix + 1, iy) - v(ix, iy)) * sx;
  const float b2 = v(ix, iy + 1) + (v(ix + 1, iy + 1) - v(ix, iy + 1)) * sx;
  return t + (b2 - t) * sy;
}

float fbm2(float x, float y, uint32_t seed) {
  return noise2(x, y, seed) * 0.55f + noise2(x * 2.1f, y * 2.1f, seed + 7) * 0.3f +
         noise2(x * 4.3f, y * 4.3f, seed + 13) * 0.15f;
}

float noise3(float x, float y, float z, uint32_t seed) {
  const int iz = static_cast<int>(std::floor(z));
  const float fz = z - iz, sz = fz * fz * (3 - 2 * fz);
  const float a = noise2(x, y, seed + static_cast<uint32_t>(iz) * 7919u);
  const float b = noise2(x, y, seed + static_cast<uint32_t>(iz + 1) * 7919u);
  return a + (b - a) * sz;
}

BakeOptions unitBake(float voxelTiles) {
  BakeOptions o;
  o.voxelTiles = voxelTiles;
  o.supersample = 2;  // soft edges, no outlines: the gritty look
  o.outline = false;
  o.quantize = false;
  o.ao = 0.5f;
  o.grain = 0.10f;
  return o;
}

// ---------------------------------------------------------------------------
// Modelling toolkit
// ---------------------------------------------------------------------------
void speckle(VoxelModel& m, uint8_t from, uint8_t a, uint8_t b, uint32_t seed, int pa, int pb) {
  for (int z = 0; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x) {
        if (m.get(x, y, z) != from) continue;
        const uint32_t h = hash32(static_cast<uint32_t>((z * 97 + y) * 131 + x) ^ seed) % 100;
        if (h < static_cast<uint32_t>(pa)) m.set(x, y, z, a);
        else if (h < static_cast<uint32_t>(pa + pb)) m.set(x, y, z, b);
      }
}

void weather(VoxelModel& m, uint8_t from, uint8_t dark, uint8_t light, uint32_t seed, int pd, int pl) {
  speckle(m, from, dark, light, seed, pd, pl);
}

void cylinder(VoxelModel& m, float cx, float cy, float r, int z0, int z1, uint8_t c) {
  for (int z = z0; z < z1; ++z)
    for (int y = static_cast<int>(cy - r - 1); y <= static_cast<int>(cy + r + 1); ++y)
      for (int x = static_cast<int>(cx - r - 1); x <= static_cast<int>(cx + r + 1); ++x) {
        const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        if (dx * dx + dy * dy <= r * r) m.set(x, y, z, c);
      }
}

void cylX(VoxelModel& m, float x0, float x1, float cy, float cz, float r, uint8_t c) {
  const float rr = std::max(r, 0.5f);
  for (int x = static_cast<int>(std::floor(x0)); x < static_cast<int>(std::ceil(x1)); ++x)
    for (int z = static_cast<int>(cz - rr - 1); z <= static_cast<int>(cz + rr + 1); ++z)
      for (int y = static_cast<int>(cy - rr - 1); y <= static_cast<int>(cy + rr + 1); ++y) {
        const float dy = y + 0.5f - cy, dz = z + 0.5f - cz;
        if (dy * dy + dz * dz <= rr * rr) m.set(x, y, z, c);
      }
}

void cylY(VoxelModel& m, float cx, float cz, float r, float y0, float y1, uint8_t c) {
  for (int y = static_cast<int>(std::floor(y0)); y < static_cast<int>(std::ceil(y1)); ++y)
    for (int z = static_cast<int>(cz - r - 1); z <= static_cast<int>(cz + r + 1); ++z)
      for (int x = static_cast<int>(cx - r - 1); x <= static_cast<int>(cx + r + 1); ++x) {
        const float dx = x + 0.5f - cx, dz = z + 0.5f - cz;
        if (dx * dx + dz * dz <= r * r) m.set(x, y, z, c);
      }
}

void dome(VoxelModel& m, float cx, float cy, float z0, float rx, float ry, float h, uint8_t c) {
  for (int z = static_cast<int>(z0); z < static_cast<int>(z0 + h + 1); ++z)
    for (int y = static_cast<int>(cy - ry - 1); y <= static_cast<int>(cy + ry + 1); ++y)
      for (int x = static_cast<int>(cx - rx - 1); x <= static_cast<int>(cx + rx + 1); ++x) {
        const float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, dz = (z + 0.5f - z0) / h;
        if (dx * dx + dy * dy + dz * dz <= 1.0f) m.set(x, y, z, c);
      }
}

void armourBox(VoxelModel& m, float x0, float x1, float y0, float y1, int z0, int z1, float front, float rear,
               float side, uint8_t c) {
  slab(m, z0, z1, [&](int z) {
    const float t = static_cast<float>(z - z0);
    return Rect{x0 + t * rear, x1 - t * front, y0 + t * side, y1 - t * side};
  }, c);
}

void roadWheel(VoxelModel& m, float cx, float cz, float r, float y0, float y1, int frame, uint8_t paint) {
  cylY(m, cx, cz, r, y0, y1, RUBBER);
  // The painted disc and hub only show on the outer faces.
  const int ya = static_cast<int>(std::floor(y0)), yb = static_cast<int>(std::ceil(y1)) - 1;
  for (int y : {ya, yb}) {
    cylY(m, cx, cz, std::max(0.8f, r - 0.9f), static_cast<float>(y), static_cast<float>(y + 1), paint);
    cylY(m, cx, cz, std::max(0.5f, r * 0.32f), static_cast<float>(y), static_cast<float>(y + 1), STEEL);
    // A bolt that goes round as the wheel turns.
    const float a = frame * kTwoPi / 4.0f + 0.4f;
    const int bx = static_cast<int>(std::floor(cx + std::cos(a) * r * 0.55f));
    const int bz = static_cast<int>(std::floor(cz + std::sin(a) * r * 0.55f));
    if (r > 1.8f) m.set(bx, y, bz, STEEL_DARK);
  }
}

void tracks(VoxelModel& m, const TrackSpec& t, int frame) {
  const float R = t.height / 2.0f;
  const float cx0 = t.x0 + R, cx1 = t.x1 - R, cz = R;
  for (int z = 0; z < t.height; ++z)
    for (int x = static_cast<int>(t.x0); x < static_cast<int>(std::ceil(t.x1)); ++x) {
      const float px = x + 0.5f, pz = z + 0.5f;
      float d;
      float param;
      if (px < cx0) {
        d = std::hypot(px - cx0, pz - cz);
        param = 1000.0f - pz;  // around the idler
      } else if (px > cx1) {
        d = std::hypot(px - cx1, pz - cz);
        param = 2000.0f + pz;
      } else {
        d = std::abs(pz - cz);
        param = pz < cz ? px : 3000.0f - px;  // the belt runs forward along the bottom, back along the top
      }
      if (d > R || d < R - 1.35f) continue;
      const int link = static_cast<int>(std::floor(param)) + 400;
      const uint8_t c = ((link - frame) % 4 == 0) ? TRACK_B : TRACK_A;
      for (int y = static_cast<int>(t.y0); y < static_cast<int>(std::ceil(t.y1)); ++y) m.set(x, y, z, c);
    }
  // Drive sprocket (front) and idler (rear).
  const float inner = R - 1.35f;
  const float sprZ = t.raisedSprocket ? cz + 0.6f : cz;
  cylY(m, cx1, sprZ, inner, t.y0 + 0.3f, t.y1 - 0.3f, STEEL_DARK);
  for (int k = 0; k < 6; ++k) {  // sprocket teeth, turning
    const float a = (k + frame * 0.5f) * kTwoPi / 6.0f;
    m.set(static_cast<int>(cx1 + std::cos(a) * (inner - 0.3f)), static_cast<int>(t.y0),
          static_cast<int>(sprZ + std::sin(a) * (inner - 0.3f)), STEEL);
    m.set(static_cast<int>(cx1 + std::cos(a) * (inner - 0.3f)), static_cast<int>(std::ceil(t.y1)) - 1,
          static_cast<int>(sprZ + std::sin(a) * (inner - 0.3f)), STEEL);
  }
  roadWheel(m, cx0, cz, inner - 0.2f, t.y0 + 0.3f, t.y1 - 0.3f, frame, STEEL_DARK);
  // Road wheels, sitting on the bottom run.
  const float wz = t.wheelR + 1.1f;
  const float first = cx0 + inner + t.wheelR * 0.6f, last = cx1 - inner - t.wheelR * 0.6f;
  for (int i = 0; i < t.wheels; ++i) {
    const float wx = t.wheels == 1 ? (first + last) / 2 : first + (last - first) * i / (t.wheels - 1);
    roadWheel(m, wx, wz, t.wheelR, t.y0 + 0.3f, t.y1 - 0.3f, frame);
  }
  if (t.returnRollers) {
    const int rollers = std::max(2, t.wheels / 2);
    for (int i = 0; i < rollers; ++i) {
      const float rx = cx0 + (cx1 - cx0) * (i + 1) / (rollers + 1);
      cylY(m, rx, t.height - 2.0f, 0.95f, t.y0 + 0.5f, t.y1 - 0.5f, STEEL_DARK);
    }
  }
}

namespace {
bool hullMaterial(uint8_t c) {
  return c == BASE || c == BASE_DARK || c == BASE_LIGHT || c == CAMO_A || c == CAMO_A_DARK || c == CAMO_B ||
         c == CAMO_B_DARK || c == CANVAS || c == CANVAS_DARK;
}
bool solid(const VoxelModel& m, int x, int y, int z) { return m.get(x, y, z) != 0; }
}  // namespace

void camouflage(VoxelModel& m, const FactionLook& look, uint32_t seed, float s) {
  for (int z = 0; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x) {
        const uint8_t c = m.get(x, y, z);
        if (c != BASE && c != BASE_DARK && c != BASE_LIGHT) continue;
        const bool dark = c == BASE_DARK;
        if (look.camo) {
          // Wavy bands: stretched noise, stripes running up the sides.
          const float n = noise3(x * s * 0.7f, y * s, z * s * 1.6f, seed) * 0.75f +
                          noise3(x * s * 2.3f, y * s * 2.3f, z * s * 2.3f, seed + 31) * 0.25f;
          if (n > 0.60f) m.set(x, y, z, dark ? CAMO_A_DARK : CAMO_A);
          else if (n < 0.35f) m.set(x, y, z, dark ? CAMO_B_DARK : CAMO_B);
        } else {
          // Plain paint, unevenly faded and touched up.
          const float n = noise3(x * s * 0.6f, y * s * 0.6f, z * s * 0.6f, seed);
          if (n > 0.76f) m.set(x, y, z, dark ? CAMO_A_DARK : CAMO_A);
          else if (n < 0.18f) m.set(x, y, z, CAMO_B);
        }
      }
}

void grime(VoxelModel& m, uint32_t seed, int mudHeight, float amount) {
  const VoxelModel src = m;
  for (int z = 0; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x) {
        const uint8_t c = src.get(x, y, z);
        if (!c || c == TEAM || c == TEAM_DARK || c == TEAM_LIGHT || c == GLASS || c == LAMP || c == SKIN ||
            c == MARK_WHITE || c == MARK_BLACK || c == MARK_RED)
          continue;
        const uint32_t h = hash32(static_cast<uint32_t>((z * 211 + y) * 157 + x) ^ seed) % 1000;
        const bool openTop = !solid(src, x, y, z + 1);
        const bool openSide = !solid(src, x + 1, y, z) || !solid(src, x - 1, y, z) || !solid(src, x, y + 1, z) ||
                              !solid(src, x, y - 1, z);
        // Mud: thick on the running gear, splashed up the lower hull.
        if (z < mudHeight) {
          const float t = 1.0f - static_cast<float>(z) / mudHeight;
          const float n = noise3(x * 0.33f, y * 0.33f, z * 0.45f, seed + 5);
          const float v = n * 0.7f + t * 0.75f * amount;
          if (v > 0.78f) {
            m.set(x, y, z, (v > 0.95f || h < 300) ? MUD_DARK : MUD);
            continue;
          }
        }
        if (!hullMaterial(c)) continue;
        // Dust settles on the top faces.
        if (openTop && h < 450 * amount && noise3(x * 0.25f, y * 0.25f, z * 0.25f, seed + 9) > 0.62f) {
          m.set(x, y, z, DUST);
          continue;
        }
        // Chipped paint on exposed edges.
        if (openTop && openSide && h > 1000 - 90 * amount) {
          m.set(x, y, z, CHIP);
          continue;
        }
        // Faded and patched paint.
        if (c == BASE && h > 900) m.set(x, y, z, BASE_LIGHT);
        else if (c == BASE && h > 820) m.set(x, y, z, BASE_DARK);
      }
}

void stampSide(VoxelModel& m, float cx, int ztop, bool right, const std::vector<const char*>& rows, int yLimit) {
  if (rows.empty()) return;
  const int w = static_cast<int>(std::strlen(rows[0]));
  const int x0 = static_cast<int>(std::floor(cx - w / 2.0f + 0.5f));
  for (size_t r = 0; r < rows.size(); ++r) {
    const int z = ztop - static_cast<int>(r);
    for (int col = 0; col < w; ++col) {
      const char ch = rows[r][col];
      if (ch == '.' || ch == 0) continue;
      const uint8_t c = ch == 'W' ? MARK_WHITE : ch == 'B' ? MARK_BLACK : ch == 'R' ? MARK_RED : TEAM;
      // The marking faces the viewer, so it goes on the far end of the
      // row along X mirrored for the right side.
      const int x = right ? x0 + (w - 1 - col) : x0 + col;
      if (right) {
        for (int y = m.sizeY() - 1; y >= 0; --y)
          if (m.get(x, y, z)) {
            if (yLimit < 0 || y >= yLimit) m.set(x, y, z, c);
            break;
          }
      } else {
        for (int y = 0; y < m.sizeY(); ++y)
          if (m.get(x, y, z)) {
            if (yLimit < 0 || y <= yLimit) m.set(x, y, z, c);
            break;
          }
      }
    }
  }
}

void stampTop(VoxelModel& m, float cx, float cy, const std::vector<const char*>& rows) {
  if (rows.empty()) return;
  const int w = static_cast<int>(std::strlen(rows[0])), hgt = static_cast<int>(rows.size());
  for (int r = 0; r < hgt; ++r)
    for (int col = 0; col < w; ++col) {
      const char ch = rows[static_cast<size_t>(r)][col];
      if (ch == '.') continue;
      const uint8_t c = ch == 'W' ? MARK_WHITE : ch == 'B' ? MARK_BLACK : ch == 'R' ? MARK_RED : TEAM;
      // Row r runs along -X (top of the picture = front of the vehicle).
      const int x = static_cast<int>(cx) + hgt / 2 - r, y = static_cast<int>(cy) - w / 2 + col;
      for (int z = m.sizeZ() - 1; z >= 0; --z)
        if (m.get(x, y, z)) {
          m.set(x, y, z, c);
          break;
        }
    }
}

void teamBand(VoxelModel& m, int z0, int z1, int xMin, int xMax) {
  const VoxelModel src = m;
  for (int z = z0; z < z1; ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = std::max(0, xMin); x < std::min(m.sizeX(), xMax); ++x) {
        const uint8_t c = src.get(x, y, z);
        if (!c || c == STEEL_DARK || c == SKIN || c == GLASS) continue;
        const bool outer = !solid(src, x + 1, y, z) || !solid(src, x - 1, y, z) || !solid(src, x, y + 1, z) ||
                           !solid(src, x, y - 1, z);
        if (outer) m.set(x, y, z, TEAM_DARK);
      }
}

const std::vector<const char*>& nationalMark(const FactionLook& look, bool small) {
  // A plain cross with a white border; a red star with a white edge.
  static const std::vector<const char*> cross = {"..WWW..", "..WBW..", "WWWBWWW", "WBBBBBW",
                                                 "WWWBWWW", "..WBW..", "..WWW.."};
  static const std::vector<const char*> crossSmall = {".WBW.", "WWBWW", "BBBBB", "WWBWW", ".WBW."};
  static const std::vector<const char*> star = {"...R...", "..RRR..", "RRRRRRR", ".RRRRR.",
                                                "..RRR..", ".RR.RR.", "RR...RR"};
  static const std::vector<const char*> starSmall = {"..R..", "RRRRR", ".RRR.", ".R.R.", "R...R"};
  // British: the white-red-white recognition flash, upright stripes.
  static const std::vector<const char*> flash = {"WWRRWW", "WWRRWW", "WWRRWW", "WWRRWW"};
  static const std::vector<const char*> flashSmall = {"WRW", "WRW", "WRW"};
  if (look.british) return small ? flashSmall : flash;
  if (look.sloped) return small ? starSmall : star;
  return small ? crossSmall : cross;
}

void splitAt(const VoxelModel& m, int zMin, VoxelModel& bottom, VoxelModel& top) {
  bottom = VoxelModel(m.sizeX(), m.sizeY(), m.sizeZ());
  top = VoxelModel(m.sizeX(), m.sizeY(), m.sizeZ());
  for (int z = 0; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x)
        if (const uint8_t c = m.get(x, y, z)) (z >= zMin ? top : bottom).set(x, y, z, c);
}

// ---------------------------------------------------------------------------
// Bake queue
// ---------------------------------------------------------------------------
int BakeQueue::push(VoxelModel model, const VoxelPalette& pal, BakeOptions o, const VoxelModel* shadow) {
  palettes_.push_back(pal);
  Job j;
  j.model = std::move(model);
  if (shadow) {
    j.shadow = *shadow;
    j.ownShadow = true;
  }
  j.pal = &palettes_.back();
  j.o = o;
  j.source = sources_.get(j.model, pal, o);
  jobs_.push_back(std::move(j));
  return base_ + static_cast<int>(jobs_.size()) - 1;
}

int BakeQueue::pushShadowed(VoxelModel model, VoxelModel shadow, const VoxelPalette& pal, BakeOptions o) {
  return push(std::move(model), pal, o, &shadow);
}

void BakeQueue::run() {
  std::vector<BakedSprite> out(jobs_.size());
  std::atomic<size_t> next{0};
  auto worker = [&] {
    for (;;) {
      const size_t i = next.fetch_add(1);
      if (i >= jobs_.size()) return;
      Job& j = jobs_[i];
      BakeOptions o = j.o;
      if (j.ownShadow) o.shadowModel = &j.shadow;
      out[i] = bakeVoxels(j.model, *j.pal, o);
    }
  };
  const unsigned n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
  std::vector<std::thread> threads;
  for (unsigned t = 1; t < n; ++t) threads.emplace_back(worker);
  worker();
  for (std::thread& t : threads) t.join();
  for (size_t k = 0; k < out.size(); ++k) {
    BakedSprite& s = out[k];
    // Trim empty margins: sprites are baked in the bounds of the whole voxel
    // grid, which is mostly air around a long gun or a pair of soldiers.
    const Image& img = s.image;
    int x0 = img.w, y0 = img.h, x1 = -1, y1 = -1;
    for (int y = 0; y < img.h; ++y)
      for (int x = 0; x < img.w; ++x)
        if (img.px[static_cast<size_t>(y) * img.w + x].a) {
          x0 = std::min(x0, x);
          x1 = std::max(x1, x);
          y0 = std::min(y0, y);
          y1 = std::max(y1, y);
        }
    if (x1 < 0) {
      x0 = y0 = 0;
      x1 = y1 = 0;
    }
    Image cut(x1 - x0 + 1, y1 - y0 + 1);
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) cut.at(x - x0, y - y0) = img.px[static_cast<size_t>(y) * img.w + x];
    ab_.add(std::move(cut), s.anchorX - x0, s.anchorY - y0, jobs_[k].source, jobs_[k].o.yawRadians);
  }
  jobs_.clear();
  palettes_.clear();
  base_ = static_cast<int>(ab_.count());
}

void addUnit(BakeQueue& q, SpriteCatalog& cat, const std::string& name, UnitBuilder make, int animFrames,
             CorpseBuilder corpse, float voxelTiles) {
  for (const FactionLook& look : kFactions) {
    if (cat.units.count(name + "@" + look.id)) continue;  // an art pack provides it
    const VoxelPalette pal = makePalette(look);
    UnitArt art;
    art.body.facings = 16;
    art.body.animFrames = animFrames;
    UnitModel first;
    for (int a = 0; a < animFrames; ++a) {
      UnitModel um = make(look, a);
      // The shadow is cast by hull and turret together.
      VoxelModel whole = um.body;
      if (um.hasTurret())
        for (int z = 0; z < whole.sizeZ(); ++z)
          for (int y = 0; y < whole.sizeY(); ++y)
            for (int x = 0; x < whole.sizeX(); ++x)
              if (const uint8_t c = um.turret.get(x, y, z)) whole.set(x, y, z, c);
      for (int f = 0; f < 16; ++f) {
        BakeOptions o = unitBake(voxelTiles);
        o.yawRadians = static_cast<float>(f) * kTwoPi / 16.0f;
        o.pivotX = um.body.sizeX() / 2.0f;
        o.pivotY = um.body.sizeY() / 2.0f;
        const int idx = q.push(um.body, pal, o, &whole);
        if (a == 0 && f == 0) art.body.first = idx;
      }
      if (a == 0) first = std::move(um);
    }
    if (first.hasTurret()) {
      art.hasTurret = true;
      art.turretOffset = (first.pivotX - first.body.sizeX() / 2.0f) * voxelTiles * LEPTONS_PER_TILE;
      art.turret.facings = 16;
      art.turret.animFrames = 1;
      for (int f = 0; f < 16; ++f) {
        BakeOptions o = unitBake(voxelTiles);
        o.yawRadians = static_cast<float>(f) * kTwoPi / 16.0f;
        o.pivotX = first.pivotX;
        o.pivotY = first.pivotY;
        o.shadow = false;
        const int idx = q.push(first.turret, pal, o);
        if (f == 0) art.turret.first = idx;
      }
    }
    if (corpse) {
      const VoxelModel dead = corpse(look, 0);
      art.hasCorpse = true;
      art.corpse.facings = 16;
      for (int f = 0; f < 16; ++f) {
        BakeOptions o = unitBake(voxelTiles);
        o.yawRadians = static_cast<float>(f) * kTwoPi / 16.0f;
        o.pivotX = dead.sizeX() / 2.0f;
        o.pivotY = dead.sizeY() / 2.0f;
        o.shadowAlpha = 60;
        const int idx = q.push(dead, pal, o);
        if (f == 0) art.corpse.first = idx;
      }
    }
    cat.units[name + "@" + look.id] = art;
  }
}

}  // namespace pioneer::art

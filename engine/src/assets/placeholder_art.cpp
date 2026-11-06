#include "slave/assets/placeholder_art.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <atomic>
#include <fstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "art_common.hpp"
#include "slave/assets/voxel.hpp"
#include "slave/core/log.hpp"
#include "slave/core/rng.hpp"

namespace slave {

using namespace art;

namespace {






// ---------------------------------------------------------------------------
// Trees, bushes and rocks: twice the voxel resolution of units and baked with
// supersampling, for the soft, natural look of the gritty art direction.
// ---------------------------------------------------------------------------
constexpr float kPropVoxel = 1.0f / 32.0f;

// Leafy canopy made of several overlapping blobs, darker underneath.
void canopy(VoxelModel& m, int v, float cx, float cy, float cz, float r, int blobs) {
  for (int i = 0; i < blobs; ++i) {
    const uint32_t h = hash32(static_cast<uint32_t>(v * 131 + i * 7919 + 17));
    const float ox = ((h & 255) / 255.0f - 0.5f) * r * 1.1f;
    const float oy = (((h >> 8) & 255) / 255.0f - 0.5f) * r * 1.1f;
    const float oz = (((h >> 16) & 255) / 255.0f - 0.5f) * r * 0.6f;
    const float rr = r * (0.55f + ((h >> 24) & 255) / 255.0f * 0.35f);
    m.ellipsoid(cx + ox, cy + oy, cz + oz, rr, rr, rr * 0.8f, LEAF);
  }
  m.ellipsoid(cx, cy, cz, r * 0.8f, r * 0.8f, r * 0.7f, LEAF);
  for (int z = 0; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x) {
        if (m.get(x, y, z) != LEAF) continue;
        const uint32_t h = hash32(static_cast<uint32_t>((z * 211 + y) * 97 + x) ^ (v * 7777u)) % 100;
        const bool under = z < cz - r * 0.2f;
        if (h < (under ? 45u : 18u)) m.set(x, y, z, LEAF_DARK);
        else if (!under && h > 82) m.set(x, y, z, LEAF_LIGHT);
      }
}

VoxelModel makeTree(int v) {
  VoxelModel m(40, 40, 54);
  const float cz = 34.0f + (v % 3) * 2.0f;
  m.box(19, 19, 0, 22, 22, static_cast<int>(cz) - 6, TRUNK);  // trunk
  for (int i = 0; i < 3; ++i) {                                // branches
    const int dx = (i == 0) ? 3 : (i == 1) ? -3 : 0, dy = (i == 2) ? 3 : -1;
    m.box(20 + std::min(0, dx), 20 + std::min(0, dy), 18 + i * 4, 21 + std::max(0, dx), 21 + std::max(0, dy),
          20 + i * 4, TRUNK);
  }
  canopy(m, v, 20.5f, 20.5f, cz, 11.0f + (v % 2) * 2.0f, 6);
  return m;
}

VoxelModel makePine(int v) {
  VoxelModel m(28, 28, 60);
  m.box(13, 13, 0, 15, 15, 12, TRUNK);
  const int top = 52 + (v % 3) * 3;
  for (int z = 8; z < top; ++z) {
    float r = 1.2f + (top - z) * 0.24f;
    if (z % 6 < 2) r *= 0.78f;  // tiered branches
    for (int y = 0; y < 28; ++y)
      for (int x = 0; x < 28; ++x) {
        const float dx = x + 0.5f - 14.0f, dy = y + 0.5f - 14.0f;
        const float jag = (hash2(x, y, static_cast<uint32_t>(z + v * 101)) % 100) / 100.0f * 1.2f;
        if (dx * dx + dy * dy <= (r + jag - 0.6f) * (r + jag - 0.6f)) m.set(x, y, z, PINE);
      }
  }
  speckle(m, PINE, PINE_DARK, LEAF_DARK, 77u + v, 34, 8);
  return m;
}

VoxelModel makeBush(int v) {
  VoxelModel m(24, 24, 16);
  canopy(m, v + 50, 12.0f, 12.0f, 6.0f, 7.0f + (v % 2), 4);
  for (int z = 0; z < m.sizeZ(); ++z)  // bushes get their own, slightly yellower green
    for (int y = 0; y < 24; ++y)
      for (int x = 0; x < 24; ++x)
        if (m.get(x, y, z) == LEAF) m.set(x, y, z, BUSH);
  return m;
}

VoxelModel makeRock(int v) {
  VoxelModel m(24, 20, 12);
  const float r = 8.0f - (v % 2) * 1.5f;
  m.ellipsoid(12, 10, 2.5f, r, r * 0.75f, 5.5f, ROCK);
  m.ellipsoid(12 + (v % 2 ? 4.0f : -4.0f), 11, 2.0f, r * 0.6f, r * 0.5f, 3.5f, ROCK);
  speckle(m, ROCK, ROCK_DARK, ROCK_LIGHT, 4242u + v, 28, 18);
  return m;
}

Image makeEllipseRing(int w, int h) {
  Image img(w, h);
  const float cx = w / 2.0f, cy = h / 2.0f;
  const float ox = cx - 0.5f, oy = cy - 0.5f;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float dx = (x + 0.5f - cx), dy = (y + 0.5f - cy);
      const float outer = (dx * dx) / (ox * ox) + (dy * dy) / (oy * oy);
      const float inner = (dx * dx) / ((ox - 1.4f) * (ox - 1.4f)) + (dy * dy) / ((oy - 1.1f) * (oy - 1.1f));
      if (outer <= 1.0f && inner > 1.0f) img.at(x, y) = {255, 255, 255, 255};
    }
  return img;
}

Image makeDiamondOutline(int w, int h) {
  Image img(w, h);
  for (int y = 0; y < h; ++y) {
    const int half = y < h / 2 ? (y + 1) * (w / h) : (h - y) * (w / h);
    const int a = w / 2 - half, b = w / 2 + half - 1;
    for (int t = 0; t < 2; ++t) {
      if (a + t >= 0 && a + t < w) img.at(a + t, y) = {255, 255, 255, 255};
      if (b - t >= 0 && b - t < w) img.at(b - t, y) = {255, 255, 255, 255};
    }
  }
  return img;
}






// One frame of a fireball that blooms, then turns to dark smoke.
Image makeExplosionFrame(int frame, int frames) {
  const int S = 72;
  Image img(S, S);
  const float t = static_cast<float>(frame) / (frames - 1);
  const float radius = S * 0.5f * (0.30f + 0.62f * std::sqrt(t));
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
      const float dx = x + 0.5f - S / 2.0f, dy = (y + 0.5f - S * 0.58f) * 1.15f;
      const float n = fbm2(x * 0.12f + frame * 0.7f, y * 0.12f, 900u + frame);
      const float d = std::sqrt(dx * dx + dy * dy) / radius + (n - 0.5f) * 0.55f;
      if (d >= 1.0f) continue;
      const float heat = (1.0f - t * 1.15f) * (1.0f - d * 0.75f) + (n - 0.5f) * 0.35f;
      float r, g, b;
      if (heat > 0.72f) { r = 255; g = 236; b = 170; }
      else if (heat > 0.5f) { r = 252; g = 172; b = 64; }
      else if (heat > 0.3f) { r = 214; g = 96; b = 36; }
      else if (heat > 0.12f) { r = 104; g = 58; b = 36; }
      else { const float k = 40 + n * 40; r = k + 6; g = k; b = k - 4; }
      const float edge = std::clamp((1.0f - d) * 3.5f, 0.0f, 1.0f);
      const float alpha = edge * (heat > 0.12f ? 1.0f : 0.85f - t * 0.35f);
      img.at(x, y) = {u8(r), u8(g), u8(b), u8(alpha * 255)};
    }
  return img;
}

Image makePuff(int S, Rgba color, uint32_t seed, float maxAlpha) {
  Image img(S, S);
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
      const float dx = (x + 0.5f) / S - 0.5f, dy = (y + 0.5f) / S - 0.5f;
      const float n = fbm2(x * 0.25f, y * 0.25f, seed);
      const float d = std::sqrt(dx * dx + dy * dy) * 2.0f + (n - 0.5f) * 0.6f;
      if (d >= 1.0f) continue;
      const float shade = 0.75f + n * 0.5f - dy * 0.4f;  // lit from above
      img.at(x, y) = {u8(color.r * shade), u8(color.g * shade), u8(color.b * shade),
                      u8((1.0f - d) * (1.0f - d) * 2.2f * maxAlpha * 255)};
    }
  return img;
}

Image makeFlash(int S) {
  Image img(S, S);
  const float c = S / 2.0f;
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
      const float dx = x + 0.5f - c, dy = y + 0.5f - c;
      const float d = std::sqrt(dx * dx + dy * dy) / c;
      const float a = std::atan2(dy, dx);
      const float rays = 0.55f + 0.45f * std::pow(std::abs(std::cos(a * 3.0f)), 6.0f);
      const float v = std::clamp(1.0f - d / rays, 0.0f, 1.0f);
      if (v <= 0) continue;
      img.at(x, y) = {255, u8(200 + 55 * v), u8(110 + 120 * v * v), u8(v * 255)};
    }
  return img;
}

Image makeFlame(int frame) {
  const int W = 14, H = 22;
  Image img(W, H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const float u = (x + 0.5f) / W - 0.5f, v = 1.0f - (y + 0.5f) / H;  // v: 0 bottom, 1 top
      const float n = fbm2(x * 0.4f, y * 0.35f - frame * 1.7f, 400u + frame);
      const float width = (1.0f - v) * 0.5f * (0.7f + n * 0.6f);
      if (std::abs(u) > width) continue;
      const float heat = (1.0f - v) * (1.0f - std::abs(u) / std::max(0.01f, width)) + n * 0.3f;
      const Rgba c = heat > 0.8f ? Rgba{255, 230, 150, 255} : heat > 0.45f ? Rgba{250, 150, 50, 255} : Rgba{200, 70, 30, 255};
      img.at(x, y) = {c.r, c.g, c.b, u8(std::min(1.0f, heat * 1.6f) * 230)};
    }
  return img;
}

// Ground decals, squashed 2:1 to lie flat in the isometric view.
Image makeCrater(int W, uint32_t seed, bool scorchOnly) {
  const int H = W / 2;
  Image img(W, H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const float dx = (x + 0.5f) / W - 0.5f, dy = ((y + 0.5f) / H - 0.5f);
      const float n = fbm2(x * 0.18f, y * 0.36f, seed);
      const float d = std::sqrt(dx * dx + dy * dy) * 2.0f + (n - 0.5f) * 0.45f;
      if (d >= 1.0f) continue;
      if (scorchOnly) {
        img.at(x, y) = {18, 15, 12, u8((1.0f - d) * 1.6f * 150)};
        continue;
      }
      if (d < 0.55f) {  // the pit
        const float k = 0.45f + n * 0.4f + dy * 0.6f;
        img.at(x, y) = {u8(58 * k), u8(48 * k), u8(36 * k), 235};
      } else if (d < 0.78f) {  // thrown-up rim, lit on the far side
        const float k = 0.75f + n * 0.35f - dy * 0.6f;
        img.at(x, y) = {u8(92 * k), u8(78 * k), u8(60 * k), u8(150 + 70 * (0.78f - d) / 0.23f)};
      } else {  // scattered earth
        if (hash2(x, y, seed) % 3 == 0) img.at(x, y) = {u8(90 + n * 30), u8(74 + n * 24), 54, u8((1.0f - d) * 4.0f * 200)};
      }
    }
  return img;
}

void addEffects(AtlasBuilder& ab, EffectArt& fx) {
  for (int i = 0; i < 8; ++i) fx.explosion[static_cast<size_t>(i)] = ab.add(makeExplosionFrame(i, 8), 36, 48);
  for (int i = 0; i < 4; ++i) fx.fire[static_cast<size_t>(i)] = ab.add(makeFlame(i), 7, 21);
  const int flashSizes[3] = {10, 18, 30};
  for (int i = 0; i < 3; ++i) fx.flash[static_cast<size_t>(i)] = ab.add(makeFlash(flashSizes[i]), flashSizes[i] / 2, flashSizes[i] / 2);
  fx.smoke = ab.add(makePuff(36, {92, 88, 82, 255}, 71, 0.55f), 18, 18);
  fx.dust = ab.add(makePuff(28, {128, 108, 80, 255}, 77, 0.6f), 14, 18);
  Image spark(3, 3);
  for (auto& p : spark.px) p = {255, 226, 150, 255};
  fx.spark = ab.add(std::move(spark), 1, 1);
  fx.craterSmall = ab.add(makeCrater(30, 11, false), 15, 7);
  fx.craterLarge = ab.add(makeCrater(54, 13, false), 27, 13);
  fx.scorch = ab.add(makeCrater(84, 17, true), 42, 21);
}




// ---------------------------------------------------------------------------
// Structures. One voxel = 1/12 tile, so a footprint of W x H tiles is
// 12W x 12H voxels. Baked once (they don't turn), supersampled.
// ---------------------------------------------------------------------------


// A pitched roof along X over [x0,x1) x [y0,y1), eaves at z.
void pitchedRoofX(VoxelModel& m, int x0, int y0, int x1, int y1, int z, uint8_t c, uint8_t ridge) {
  const int half = (y1 - y0) / 2;
  for (int i = 0; i <= half; ++i) {
    m.box(x0, y0 + i, z + i, x1, y1 - i, z + i + 1, c);
  }
  m.box(x0, y0 + half, z + half, x1, y0 + half + 1 + (y1 - y0) % 2, z + half + 1, ridge);
}

void windowsAlongX(VoxelModel& m, int x0, int x1, int y, int z, int step, uint8_t c = WINDOW) {
  for (int x = x0; x < x1; x += step) m.box(x, y, z, x + 2, y + 1, z + 2, c);
}
void windowsAlongY(VoxelModel& m, int y0, int y1, int x, int z, int step, uint8_t c = WINDOW) {
  for (int y = y0; y < y1; y += step) m.box(x, y, z, x + 1, y + 2, z + 2, c);
}


void sandbagRow(VoxelModel& m, int x0, int y0, int x1, int y1, int h) {
  m.box(x0, y0, 0, x1, y1, h, SANDBAG);
  for (int z = 0; z < h; ++z)  // staggered bags
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x)
        if (((x + y + (z % 2) * 2) % 4) == 0) m.set(x, y, z, SANDBAG_DARK);
}

VoxelModel makeYard(const FactionLook& look) {
  VoxelModel m(48, 36, 34);
  m.box(0, 0, 0, 48, 36, 1, CONCRETE_DARK);              // concrete apron
  weather(m, CONCRETE_DARK, SOOT, CONCRETE, 11, 10, 12);
  // Main assembly hall (back), big doors facing the front (+Y).
  m.box(2, 2, 1, 30, 20, 13, CONCRETE);
  weather(m, CONCRETE, CONCRETE_DARK, CONCRETE_LIGHT, 12);
  if (look.sloped) pitchedRoofX(m, 1, 1, 31, 21, 13, ROOF, ROOF_DARK);
  else {
    m.box(1, 1, 13, 31, 21, 15, ROOF);                    // flat roof with skylights
    for (int x = 4; x < 28; x += 6) m.box(x, 6, 15, x + 3, 16, 16, WINDOW);
  }
  m.box(8, 19, 1, 22, 20, 11, STEEL_DARK);                // hangar door
  for (int x = 9; x < 22; x += 3) m.box(x, 20, 1, x + 1, 21, 11, STEEL);
  windowsAlongY(m, 4, 18, 30, 7, 4);
  // Office block (front right) with lit windows.
  m.box(32, 18, 1, 46, 32, 10, look.sloped ? BRICK : CONCRETE_LIGHT);
  if (look.sloped) weather(m, BRICK, BRICK_DARK, BRICK, 13);
  m.box(31, 17, 10, 47, 33, 11, ROOF_DARK);
  windowsAlongX(m, 34, 45, 32, 6, 4, WINDOW_LIT);
  windowsAlongY(m, 20, 31, 46, 6, 4);
  m.box(36, 31, 10, 42, 33, 11, TEAM);                    // team panel on the roof edge
  // Gantry crane over the yard.
  for (int x : {33, 45}) m.box(x, 2, 1, x + 1, 3, 30, STEEL_DARK);
  m.box(33, 2, 29, 46, 3, 31, STEEL);
  m.box(38, 2, 26, 39, 3, 29, STEEL_DARK);                // hook
  // Stacks of material and a fuel drum cluster on the apron.
  m.box(4, 25, 1, 12, 32, 4, WOOD);
  m.box(5, 26, 4, 11, 31, 6, WOOD_DARK);
  for (int i = 0; i < 4; ++i) cylinder(m, 18.5f + (i % 2) * 3, 27.5f + (i / 2) * 3, 1.3f, 1, 5, RUST);
  // Flag.
  m.box(26, 30, 1, 27, 31, 22, STEEL_DARK);
  m.box(27, 30, 18, 32, 31, 22, TEAM);
  return m;
}

VoxelModel makePowerStation(const FactionLook& look) {
  VoxelModel m(24, 24, 44);
  m.box(0, 0, 0, 24, 24, 1, CONCRETE_DARK);
  m.box(2, 6, 1, 20, 22, 14, look.sloped ? BRICK : CONCRETE);
  weather(m, look.sloped ? BRICK : CONCRETE, look.sloped ? BRICK_DARK : CONCRETE_DARK, look.sloped ? BRICK : CONCRETE_LIGHT, 21);
  pitchedRoofX(m, 1, 5, 21, 23, 14, ROOF, ROOF_DARK);
  windowsAlongX(m, 4, 19, 22, 6, 4, WINDOW_LIT);
  windowsAlongY(m, 8, 21, 20, 6, 4);
  // Two tall chimneys, sooty at the top.
  for (float cx : {6.0f, 13.0f}) {
    cylinder(m, cx, 3.5f, 2.2f, 1, 42, look.sloped ? BRICK_DARK : CONCRETE_DARK);
    cylinder(m, cx, 3.5f, 2.2f, 36, 42, SOOT);
  }
  // Transformer yard.
  m.box(20, 2, 1, 23, 8, 6, STEEL_DARK);
  m.box(21, 3, 6, 22, 7, 9, STEEL);
  m.box(8, 22, 1, 13, 23, 9, TEAM);  // team-painted doors
  return m;
}

VoxelModel makeRefinery(const FactionLook& look) {
  VoxelModel m(36, 36, 26);
  m.box(0, 0, 0, 36, 36, 1, CONCRETE_DARK);
  weather(m, CONCRETE_DARK, SOOT, CONCRETE, 31, 16, 6);
  // Three storage tanks.
  const struct { float x, y, r; int h; } tanks[] = {{8, 8, 6.0f, 18}, {21, 7, 5.0f, 15}, {8, 22, 5.0f, 15}};
  for (const auto& t : tanks) {
    cylinder(m, t.x, t.y, t.r, 1, t.h, look.sloped ? TANK_PAINT : CONCRETE_LIGHT);
    cylinder(m, t.x, t.y, t.r - 1, t.h, t.h + 1, TANK_PAINT_DARK);  // roof
    cylinder(m, t.x, t.y, t.r, t.h - 3, t.h - 2, TEAM);            // team band
  }
  weather(m, TANK_PAINT, RUST, TANK_PAINT_DARK, 32, 14, 10);
  weather(m, CONCRETE_LIGHT, RUST, CONCRETE, 33, 12, 10);
  // Pipes between the tanks and the pump house.
  m.box(8, 14, 3, 9, 30, 4, STEEL_DARK);
  m.box(8, 29, 3, 30, 30, 4, STEEL_DARK);
  m.box(21, 12, 3, 22, 30, 4, STEEL_DARK);
  // Pump house and loading bay at the front (where trucks unload).
  m.box(20, 18, 1, 34, 30, 10, look.sloped ? WOOD : CONCRETE);
  pitchedRoofX(m, 19, 17, 35, 31, 10, ROOF, ROOF_DARK);
  m.box(24, 30, 1, 30, 31, 7, STEEL_DARK);
  m.box(14, 32, 1, 34, 36, 2, CONCRETE);  // loading apron
  for (int x = 15; x < 34; x += 6) m.box(x, 33, 2, x + 3, 35, 4, RUST);  // drums
  return m;
}

VoxelModel makeBarracks(const FactionLook& look) {
  VoxelModel m(36, 24, 26);
  m.box(0, 0, 0, 36, 24, 1, EARTH);
  // Long hut.
  m.box(2, 3, 1, 34, 17, 10, look.sloped ? WOOD : CONCRETE);
  weather(m, WOOD, WOOD_DARK, WOOD, 41, 30, 0);
  weather(m, CONCRETE, CONCRETE_DARK, CONCRETE_LIGHT, 42);
  pitchedRoofX(m, 1, 2, 35, 18, 10, ROOF, ROOF_DARK);
  windowsAlongX(m, 4, 33, 17, 5, 5);
  m.box(16, 17, 1, 20, 18, 8, WOOD_DARK);          // door
  // Sandbagged entrance and a small parade ground.
  sandbagRow(m, 12, 19, 15, 23, 3);
  sandbagRow(m, 21, 19, 24, 23, 3);
  m.box(30, 19, 1, 31, 20, 20, STEEL_DARK);        // flag pole
  m.box(31, 19, 15, 35, 20, 20, TEAM);
  return m;
}

VoxelModel makeFactory(const FactionLook& look) {
  VoxelModel m(48, 36, 36);
  m.box(0, 0, 0, 48, 36, 1, CONCRETE_DARK);
  weather(m, CONCRETE_DARK, SOOT, CONCRETE, 51, 18, 6);
  const uint8_t wall = look.sloped ? BRICK : CONCRETE;
  m.box(2, 2, 1, 46, 26, 16, wall);
  weather(m, wall, look.sloped ? BRICK_DARK : CONCRETE_DARK, look.sloped ? BRICK : CONCRETE_LIGHT, 52);
  // Saw-tooth roof: a row of north-lit ridges.
  for (int y = 2; y < 26; y += 6)
    for (int i = 0; i < 6; ++i) {
      m.box(1, y + i, 16, 47, y + i + 1, 16 + (5 - i) / 1, ROOF);
      m.box(1, y, 16, 47, y + 1, 22, WINDOW);  // glazing on the steep side
    }
  // Big doors for tanks on the front.
  m.box(14, 25, 1, 34, 27, 13, STEEL_DARK);
  for (int x = 15; x < 34; x += 4) m.box(x, 26, 1, x + 1, 27, 13, STEEL);
  m.box(14, 25, 13, 34, 27, 15, TEAM);
  windowsAlongY(m, 4, 24, 46, 8, 4);
  // Chimney.
  cylinder(m, 42.5f, 6.5f, 2.0f, 16, 34, wall);
  cylinder(m, 42.5f, 6.5f, 2.0f, 30, 34, SOOT);
  // Finished hull on the apron.
  m.box(36, 29, 1, 44, 34, 4, TANK_PAINT_DARK);
  m.box(38, 30, 4, 42, 33, 6, TANK_PAINT);
  return m;
}

VoxelModel makeWatchtower(const FactionLook& look) {
  VoxelModel m(12, 12, 40);
  for (int x : {2, 9})
    for (int y : {2, 9}) m.box(x, y, 0, x + 1, y + 1, 30, WOOD_DARK);  // legs
  for (int z = 6; z < 28; z += 7) {                                   // cross braces
    m.box(2, 2, z, 10, 3, z + 1, WOOD);
    m.box(2, 9, z, 10, 10, z + 1, WOOD);
  }
  m.box(1, 1, 28, 11, 11, 30, WOOD);          // platform
  m.box(1, 1, 30, 11, 2, 32, WOOD_DARK);      // railing
  m.box(1, 10, 30, 11, 11, 32, WOOD_DARK);
  m.box(0, 0, 36, 12, 12, 37, ROOF);          // roof
  m.box(2, 2, 37, 10, 10, 38, ROOF_DARK);
  for (int x : {1, 10})
    for (int y : {1, 10}) m.box(x, y, 30, x + 1, y + 1, 36, WOOD_DARK);
  m.box(5, 5, 37, 7, 7, 39, TEAM);
  (void)look;
  return m;
}

VoxelModel makeAtGunPit(const FactionLook&) {
  VoxelModel m(24, 24, 8);
  // A ring of sandbags round a shallow pit.
  for (int y = 0; y < 24; ++y)
    for (int x = 0; x < 24; ++x) {
      const float dx = x + 0.5f - 12, dy = y + 0.5f - 12;
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d < 11.5f) m.set(x, y, 0, d < 8.0f ? EARTH_DARK : EARTH);
      if (d >= 8.5f && d < 11.5f && !(dy > 6 && std::abs(dx) < 2.5f)) {  // gap at the back for the crew
        const int h = 3 + ((x * 7 + y * 3) % 3 == 0);
        for (int z = 1; z <= h; ++z) m.set(x, y, z, ((x + y + z) % 3) ? SANDBAG : SANDBAG_DARK);
      }
    }
  return m;
}

// The gun itself: shield, barrel and trail, turning in the pit.
VoxelModel makeAtGun(const FactionLook& look) {
  VoxelModel m(24, 24, 8);
  m.box(9, 9, 1, 15, 15, 2, TANK_PAINT_DARK);        // carriage
  m.box(7, 11, 1, 10, 13, 2, TANK_PAINT_DARK);       // trail
  m.box(13, 8, 2, 14, 16, look.sloped ? 6 : 5, TANK_PAINT);  // shield
  m.box(12, 11, 3, 23, 13, 4, STEEL_DARK);           // long barrel
  m.box(22, 11, 3, 24, 13, 4, STEEL);                // muzzle brake
  m.box(10, 10, 2, 13, 14, 3, TEAM);
  return m;
}

// An MG nest: a ring of sandbags round a shallow scrape, a gap at the back.
VoxelModel makeMgNest(const FactionLook&) {
  VoxelModel m(12, 12, 6);
  for (int y = 0; y < 12; ++y)
    for (int x = 0; x < 12; ++x) {
      const float dx = x + 0.5f - 6, dy = y + 0.5f - 6, d = std::sqrt(dx * dx + dy * dy);
      if (d < 5.8f) m.set(x, y, 0, d < 3.6f ? EARTH_DARK : EARTH);
      if (d >= 3.6f && d < 5.8f && !(dx < -2.5f && std::abs(dy) < 1.5f)) {
        const int h = 2 + ((x * 5 + y * 3) % 4 == 0);
        for (int z = 1; z <= h; ++z) m.set(x, y, z, ((x + y + z) % 3) ? SANDBAG : SANDBAG_DARK);
      }
    }
  m.box(1, 8, 1, 3, 10, 2, WOOD);  // ammunition box
  return m;
}

// The machine gun on its tripod.
VoxelModel makeMgNestGun(const FactionLook&) {
  VoxelModel m(12, 12, 6);
  m.box(5, 5, 1, 6, 7, 3, STEEL_DARK);   // tripod and gunner's back
  m.box(5, 5, 3, 8, 7, 4, STEEL_DARK);   // receiver
  m.box(8, 6, 3, 11, 7, 4, STEEL);       // barrel
  m.box(4, 5, 1, 5, 7, 4, UNIFORM);      // the gunner, hunched behind
  m.box(4, 5, 4, 5, 7, 5, HELMET);
  return m;
}

// A howitzer emplacement: a wide sandbag ring, shell cases and crates.
VoxelModel makeHowitzerPit(const FactionLook& look) {
  VoxelModel m = makeAtGunPit(look);
  m.box(3, 15, 1, 6, 18, 3, WOOD);       // ammunition crates
  m.box(4, 16, 3, 6, 18, 4, WOOD_DARK);
  m.box(18, 4, 1, 20, 6, 2, RUST);       // spent cases
  m.box(17, 6, 1, 19, 7, 2, RUST);
  return m;
}

// The howitzer: split trail behind, shield, a short thick barrel raised high.
VoxelModel makeHowitzer(const FactionLook& look) {
  VoxelModel m(24, 24, 12);
  m.box(10, 9, 1, 14, 15, 3, TANK_PAINT_DARK);  // carriage and axle
  m.box(9, 7, 1, 11, 9, 4, RUBBER);             // wheels
  m.box(9, 15, 1, 11, 17, 4, RUBBER);
  for (int i = 0; i < 7; ++i) {                 // split trail, opened wide
    m.set(9 - i, 10 - i / 2, 1, TANK_PAINT_DARK);
    m.set(9 - i, 14 + i / 2, 1, TANK_PAINT_DARK);
  }
  m.box(13, 8, 2, 14, 16, look.sloped ? 7 : 6, TANK_PAINT);  // shield
  for (int i = 0; i < 9; ++i) m.box(12 + i, 11, 4 + i / 2, 13 + i, 13, 6 + i / 2, i > 6 ? STEEL : STEEL_DARK);  // barrel, elevated
  m.box(10, 10, 3, 13, 14, 4, TEAM);
  return m;
}

// Sandbags, wire and hedgehogs as one piece (the sidebar picture and the body).
VoxelModel makeSandbagPiece(const FactionLook&) { return makeSandbagLine(5); }
VoxelModel makeWirePiece(const FactionLook&) { return makeWireLine(5); }
VoxelModel makeHedgehogPiece(const FactionLook&) { return makeHedgehogs(0); }

// Bakes a structure: one view for the body, and 16 facings of its turret if any.
void addStructure(AtlasBuilder& ab, SpriteCatalog& cat, const std::string& name, VoxelModel (*make)(const FactionLook&),
                  VoxelModel (*turret)(const FactionLook&) = nullptr, float voxelTiles = kStructureVoxel) {
  for (const FactionLook& look : kFactions) {
    if (cat.units.count(name + "@" + look.id)) continue;  // an art pack provides it
    const VoxelPalette pal = makePalette(look);
    const VoxelModel body = make(look);
    UnitArt art;
    art.body.facings = 1;
    art.body.animFrames = 1;
    art.body.first = static_cast<int>(ab.count());
    BakeOptions o = unitBake(voxelTiles);
    o.pivotX = body.sizeX() / 2.0f;
    o.pivotY = body.sizeY() / 2.0f;
    o.shadowAlpha = 110;
    BakedSprite s = bakeVoxels(body, pal, o);
    ab.add(std::move(s.image), s.anchorX, s.anchorY);
    if (turret) {
      const VoxelModel t = turret(look);
      art.hasTurret = true;
      art.turretOffset = 0;
      art.turret.facings = 16;
      art.turret.first = static_cast<int>(ab.count());
      for (int f = 0; f < 16; ++f) {
        BakeOptions to = unitBake(voxelTiles);
        to.yawRadians = static_cast<float>(f) * kTwoPi / 16.0f;
        to.pivotX = t.sizeX() / 2.0f;
        to.pivotY = t.sizeY() / 2.0f;
        to.shadow = false;
        BakedSprite ts = bakeVoxels(t, pal, to);
        ab.add(std::move(ts.image), ts.anchorX, ts.anchorY);
      }
    }
    cat.units[name + "@" + look.id] = art;
  }
}

// --- Ground decals for the economy and fortifications -------------------------
// Diamond tile helpers: (u, v) in [0,1]^2 across the tile, mapped to a 64x32 image.
template <class F>
Image tileDecal(F shade) {
  const int W = 64, H = 32;
  Image img(W, H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      // Inverse of the diamond: screen (x, y) -> tile (u, v).
      const float sx = (x + 0.5f - W / 2.0f) / (W / 2.0f), sy = (y + 0.5f) / (H / 2.0f) - 1.0f;
      const float u = (sx + sy) * 0.5f + 0.5f, v = (sy - sx) * 0.5f + 0.5f;
      if (u < 0 || v < 0 || u > 1 || v > 1) continue;
      img.at(x, y) = shade(u, v, x, y);
    }
  return img;
}

// A trench segment. `mask`: bit 0 = joins +X neighbour, 1 = +Y, 2 = -X, 3 = -Y.
Image makeTrenchDecal(int mask) {
  return tileDecal([&](float u, float v, int x, int y) -> Rgba {
    // Distance to the trench's centre line(s).
    const float cu = u - 0.5f, cv = v - 0.5f;
    float d = std::sqrt(cu * cu + cv * cv);  // the hub
    if (mask & 1 && u >= 0.5f) d = std::min(d, std::abs(cv));
    if (mask & 4 && u <= 0.5f) d = std::min(d, std::abs(cv));
    if (mask & 2 && v >= 0.5f) d = std::min(d, std::abs(cu));
    if (mask & 8 && v <= 0.5f) d = std::min(d, std::abs(cu));
    const float n = fbm2(x * 0.3f, y * 0.5f, 900u + static_cast<uint32_t>(mask));
    const float dd = d + (n - 0.5f) * 0.06f;
    if (mask == 0 && d > 0.3f) return {0, 0, 0, 0};
    if (dd < 0.12f) {  // the dug channel: dark, deeper in the middle
      const float k = 0.45f + dd * 2.5f + n * 0.2f;
      return {u8(62 * k), u8(50 * k), u8(38 * k), 245};
    }
    if (dd < 0.21f) {  // parapet of thrown-up earth and sandbags
      const bool bag = hash2(x / 2, y, 77) % 4 == 0;
      const float k = 0.9f + n * 0.3f;
      return bag ? Rgba{u8(140 * k), u8(124 * k), u8(92 * k), 240} : Rgba{u8(112 * k), u8(92 * k), u8(66 * k), 230};
    }
    if (dd < 0.27f) return {u8(96 + n * 20), u8(80 + n * 16), 58, u8((0.27f - dd) / 0.06f * 170)};
    return {0, 0, 0, 0};
  });
}

Image makeOilDecal(int variant) {
  return tileDecal([&](float u, float v, int x, int y) -> Rgba {
    const float n = fbm2(x * 0.16f + variant * 13.0f, y * 0.32f, 600u + static_cast<uint32_t>(variant));
    const float edge = std::min({u, v, 1 - u, 1 - v}) * 4.0f;  // fade at the tile edge
    const float a = std::clamp((n - 0.38f) * 3.2f, 0.0f, 1.0f) * std::clamp(edge + 0.35f, 0.0f, 1.0f);
    if (a <= 0.02f) return {0, 0, 0, 0};
    const float sheen = std::pow(std::clamp(n - 0.7f, 0.0f, 1.0f) * 3.3f, 3.0f);
    return {u8(18 + 70 * sheen), u8(16 + 60 * sheen), u8(14 + 76 * sheen), u8(a * 225)};
  });
}

// Rubble left by a destroyed building, `size` tiles across.
Image makeRubble(int size, uint32_t seed) {
  const int W = 64 * size, H = 32 * size;
  Image img(W, H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const float dx = (x + 0.5f) / W - 0.5f, dy = (y + 0.5f) / H - 0.5f;
      const float n = fbm2(x * 0.12f, y * 0.24f, seed);
      const float d = (std::abs(dx) + std::abs(dy)) * 2.0f + (n - 0.5f) * 0.4f;  // a diamond
      if (d >= 1.0f) continue;
      const uint32_t h = hash2(x / 2, y / 2, seed);
      const bool chunk = h % 7 == 0 && d < 0.85f;
      const float k = chunk ? 0.9f + (h % 50) / 100.0f : 0.35f + n * 0.35f;
      const Rgba c = chunk ? Rgba{u8(120 * k), u8(116 * k), u8(108 * k), 255}   // broken concrete
                           : Rgba{u8(60 * k), u8(54 * k), u8(48 * k), u8(220 * std::min(1.0f, (1.0f - d) * 4))};  // soot and ash
      img.at(x, y) = c;
    }
  return img;
}

void addEconomyDecals(AtlasBuilder& ab, SpriteCatalog& cat) {
  for (int m = 0; m < 16; ++m) cat.trench[static_cast<size_t>(m)] = ab.add(makeTrenchDecal(m), 32, 16);
  for (int v = 0; v < 4; ++v) cat.oil[static_cast<size_t>(v)] = ab.add(makeOilDecal(v), 32, 16);
  for (int s = 0; s < 3; ++s) cat.rubble[static_cast<size_t>(s)] = ab.add(makeRubble(s + 2, 70u + s), 32 * (s + 2), 16 * (s + 2));
}

// --- PNG overrides and export ---------------------------------------------------
// Team colour travels through PNG files as pure magenta: (v, 0, v) is team
// colour at brightness v. Inside the engine it is red with alpha kTeamAlpha.
Image toPngColours(const Image& src) {
  Image out = src;
  for (Rgba& c : out.px)
    if (c.a == kTeamAlpha) c = {c.r, 0, c.r, 255};
  return out;
}

Image fromPngColours(const Image& src) {
  Image out = src;
  for (Rgba& c : out.px) {
    if (c.a > 128 && c.g < 16 && c.r > 24 && std::abs(static_cast<int>(c.r) - static_cast<int>(c.b)) < 12)
      c = {static_cast<uint8_t>((c.r + c.b) / 2), 0, 0, kTeamAlpha};
    else if (c.a == kTeamAlpha)
      c.a = kTeamAlpha - 1;
  }
  return out;
}

void applyArtOverrides(AtlasBuilder& ab, const SpriteCatalog& cat, const std::string& dir, bool skipUnits = false) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return;
  // Optional anchors: {"units/tank@ger/body_f00_a0": [x, y], ...}
  nlohmann::json anchors = nlohmann::json::object();
  if (std::ifstream in(dir + "/sprites.json"); in) {
    anchors = nlohmann::json::parse(in, nullptr, false);
    if (!anchors.is_object()) {
      SLV_WARN("Art overrides: %s/sprites.json is not valid JSON; using default anchors", dir.c_str());
      anchors = nlohmann::json::object();
    }
  }
  int replaced = 0;
  forEachSprite(cat, [&](const std::string& name, int frame) {
    if (skipUnits && (name.rfind("units/", 0) == 0 || name.rfind("icons/", 0) == 0)) return;  // loaded already
    const std::string path = dir + "/" + name + ".png";
    if (!fs::exists(path, ec)) return;
    Image img;
    if (!img.loadPng(path)) {
      SLV_WARN("Art overrides: can't read %s", path.c_str());
      return;
    }
    int ax = ab.anchorX(frame), ay = ab.anchorY(frame);
    if (anchors.contains(name) && anchors[name].is_array() && anchors[name].size() == 2) {
      ax = anchors[name][0].get<int>();
      ay = anchors[name][1].get<int>();
    } else if (img.w != ab.image(frame).w || img.h != ab.image(frame).h) {
      ax = img.w / 2;  // a new size with no anchor given: stand it on its bottom-centre
      ay = img.h * 3 / 4;
    }
    ab.replace(frame, fromPngColours(img), ax, ay);
    ++replaced;
  });
  if (replaced) SLV_INFO("Art overrides: %d sprites replaced from %s", replaced, dir.c_str());
}

void exportArt(const AtlasBuilder& ab, const SpriteCatalog& cat, const std::string& dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  nlohmann::json anchors = nlohmann::json::object();
  int written = 0;
  forEachSprite(cat, [&](const std::string& name, int frame) {
    const std::string path = dir + "/" + name + ".png";
    fs::create_directories(fs::path(path).parent_path(), ec);
    if (toPngColours(ab.image(frame)).savePng(path)) ++written;
    anchors[name] = {ab.anchorX(frame), ab.anchorY(frame)};
  });
  std::ofstream(dir + "/sprites.json") << anchors.dump(1) << "\n";
  SLV_INFO("Exported %d sprites to %s", written, dir.c_str());
}


// --- Art packs -------------------------------------------------------------------------
// Loads a pack's units (see art_pack.hpp) straight into the atlas. Units it
// provides are not baked from voxels.
void loadArtPack(AtlasBuilder& ab, SpriteCatalog& cat, const std::string& dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  std::ifstream in(dir + "/sprites.json");
  if (!in) return;
  const nlohmann::json meta = nlohmann::json::parse(in, nullptr, false);
  if (!meta.is_object() || !meta.contains("_units") || !meta["_units"].is_object()) {
    SLV_WARN("Art pack %s: sprites.json has no _units block", dir.c_str());
    return;
  }
  auto two = [](int v) { return (v < 10 ? std::string("0") : std::string()) + std::to_string(v); };
  struct Want {
    std::string name;
    Image img;
    int ax = 0, ay = 0;
  };
  std::vector<Want> wants;
  std::vector<std::pair<std::string, UnitArt>> arts;
  for (const auto& [key, u] : meta["_units"].items()) {
    if (!u.is_object()) continue;
    UnitArt art;
    const std::string base = "units/" + key + "/";
    auto sheet = [&](SpriteSheet& sh, const std::string& prefix, int facings, int frames, bool single) {
      sh.facings = std::max(1, facings);
      sh.animFrames = std::max(1, frames);
      sh.first = static_cast<int>(ab.count() + wants.size());
      for (int a = 0; a < sh.animFrames; ++a)
        for (int f = 0; f < sh.facings; ++f) {
          std::string n = base + prefix;
          if (!single) n += "_f" + two(f) + (prefix == "turret" || (prefix == "corpse" && sh.animFrames == 1) ? "" : "_a" + std::to_string(a));
          wants.push_back({n, {}, 0, 0});
        }
    };
    const int facings = u.value("facings", 16), frames = u.value("frames", 1);
    sheet(art.body, "body", facings, frames, facings == 1 && frames == 1);
    if (u.contains("walk") && u["walk"].is_array() && u["walk"].size() == 3) {
      art.walkFirst = u["walk"][0].get<int>();
      art.walkFrames = u["walk"][1].get<int>();
      art.walkMs = std::max(20, u["walk"][2].get<int>());
    }
    if (u.contains("shoot") && u["shoot"].is_array() && u["shoot"].size() == 3) {
      art.shootFirst = u["shoot"][0].get<int>();
      art.shootFrames = u["shoot"][1].get<int>();
      art.shootMs = std::max(20, u["shoot"][2].get<int>());
    }
    if (u.contains("turret") && u["turret"].is_object()) {
      art.hasTurret = true;
      art.turretOffset = u["turret"].value("offset", 0.0f);
      sheet(art.turret, "turret", u["turret"].value("facings", 16), 1, false);
    }
    if (u.contains("corpse") && u["corpse"].is_object()) {
      art.hasCorpse = true;
      art.corpseMs = u["corpse"].value("ms", 100);
      sheet(art.corpse, "corpse", u["corpse"].value("facings", 4), u["corpse"].value("frames", 1), false);
    }
    if (fs::exists(dir + "/icons/" + key + ".png", ec)) {
      art.icon = static_cast<int>(ab.count() + wants.size());
      wants.push_back({"icons/" + key, {}, 0, 0});
    }
    arts.push_back({key, art});
  }
  // Read the PNGs on all cores.
  std::atomic<size_t> next{0};
  std::atomic<int> missing{0};
  auto work = [&] {
    for (size_t i; (i = next++) < wants.size();) {
      Want& w = wants[i];
      if (!w.img.loadPng(dir + "/" + w.name + ".png")) {
        w.img = Image(1, 1);
        ++missing;
        continue;
      }
      if (meta.contains(w.name) && meta[w.name].is_array() && meta[w.name].size() == 2) {
        w.ax = meta[w.name][0].get<int>();
        w.ay = meta[w.name][1].get<int>();
      } else {
        w.ax = w.img.w / 2;
        w.ay = w.img.h * 3 / 4;
      }
      w.img = fromPngColours(w.img);
    }
  };
  {
    std::vector<std::thread> pool;
    const unsigned n = std::max(1u, std::min(12u, std::thread::hardware_concurrency()));
    for (unsigned t = 0; t < n; ++t) pool.emplace_back(work);
    for (auto& t : pool) t.join();
  }
  for (Want& w : wants) ab.add(std::move(w.img), w.ax, w.ay);
  for (auto& [key, art] : arts) cat.units[key] = art;
  SLV_INFO("Art pack %s: %zu units, %zu sprites%s", dir.c_str(), arts.size(), wants.size(),
           missing ? (" (" + std::to_string(missing.load()) + " missing)").c_str() : "");
}
}  // namespace

bool buildPlaceholderArt(SpriteCatalog& cat, std::string& error, const ArtOptions& options) {
  const VoxelPalette pal = makePalette(kFactions[0]);
  AtlasBuilder ab;
  for (const std::string& dir : options.packDirs) loadArtPack(ab, cat, dir);

  {
    BakeQueue q(ab);
    addUnitModels(q, cat);
    q.run();
  }
  addEffects(ab, cat.effects);
  addStructure(ab, cat, "yard", &makeYard);
  addStructure(ab, cat, "power", &makePowerStation);
  addStructure(ab, cat, "refinery", &makeRefinery);
  addStructure(ab, cat, "barracks", &makeBarracks);
  addStructure(ab, cat, "factory", &makeFactory);
  addStructure(ab, cat, "tower", &makeWatchtower);
  addStructure(ab, cat, "bunker", &makePillbox, &makePillboxGun, kFineStructureVoxel);
  addStructure(ab, cat, "atgun", &makeAtGunPit, &makeAtGun);
  addStructure(ab, cat, "sandbags", &makeSandbagPiece, nullptr, kFineStructureVoxel);
  addStructure(ab, cat, "wire", &makeWirePiece, nullptr, kFineStructureVoxel);
  addStructure(ab, cat, "traps", &makeHedgehogPiece, nullptr, kFineStructureVoxel);
  {
    // Sandbag walls and wire entanglements join up with their neighbours.
    const VoxelPalette fpal = makePalette(kFactions[0]);
    for (const char* model : {"sandbags", "wire", "fence"}) {
      std::array<int, 16>& frames = cat.joined[model];
      for (int mask = 0; mask < 16; ++mask) {
        const VoxelModel vm = model[0] == 's' ? makeSandbagLine(mask) : model[0] == 'w' ? makeWireLine(mask) : makeFenceLine(mask);
        BakeOptions o = unitBake(kFineStructureVoxel);
        o.pivotX = 12;
        o.pivotY = 12;
        o.shadowAlpha = model[0] == 's' ? 110 : 60;
        BakedSprite b = bakeVoxels(vm, fpal, o);
        frames[static_cast<size_t>(mask)] = ab.add(std::move(b.image), b.anchorX, b.anchorY);
      }
    }
  }
  addStructure(ab, cat, "cottage", &makeCottage);
  addStructure(ab, cat, "cottage_bombed", &makeCottageBombed);
  addStructure(ab, cat, "townhouse", &makeTownhouse);
  addStructure(ab, cat, "church", &makeChurch);
  addStructure(ab, cat, "ruin", &makeRuin);
  addStructure(ab, cat, "adobe", &makeAdobe);
  addStructure(ab, cat, "mg_nest", &makeMgNest, &makeMgNestGun);
  addStructure(ab, cat, "howitzer_pit", &makeHowitzerPit, &makeHowitzer);
  addStructure(ab, cat, "barn", &makeBarn);
  addStructure(ab, cat, "shed", &makeShed);
  addStructure(ab, cat, "izba", &makeIzba);
  addStructure(ab, cat, "warehouse", &makeWarehouse);
  addStructure(ab, cat, "well", &makeWell);
  addStructure(ab, cat, "water_tower", &makeWaterTower);
  addStructure(ab, cat, "station", &makeStation);
  addStructure(ab, cat, "farmhouse", &makeFarmhouse);
  addStructure(ab, cat, "school", &makeSchool);
  addEconomyDecals(ab, cat);
  {
    // Trenches are ground decals; their "unit art" is the lone foxhole piece.
    for (const FactionLook& look : kFactions) {
      if (cat.units.count(std::string("trench@") + look.id)) continue;
      UnitArt t;
      t.body.facings = 1;
      t.body.first = cat.trench[0];
      cat.units[std::string("trench@") + look.id] = t;
    }
  }

  struct PropSpec {
    PropKind kind;
    VoxelModel (*make)(int);
    int variants;
  };
  for (const PropSpec& p : {PropSpec{PropKind::Tree, &makeTree, 5}, PropSpec{PropKind::Pine, &makePine, 3},
                            PropSpec{PropKind::Bush, &makeBush, 4}, PropSpec{PropKind::Rock, &makeRock, 3},
                            PropSpec{PropKind::SnowPine, &makeSnowPine, 4}, PropSpec{PropKind::DeadTree, &makeDeadTree, 4},
                            PropSpec{PropKind::Palm, &makePalm, 4}, PropSpec{PropKind::Scrub, &makeScrub, 4},
                            PropSpec{PropKind::JungleTree, &makeJungleTree, 4}, PropSpec{PropKind::Debris, &makeDebris, 4},
                            PropSpec{PropKind::SandRock, &makeSandRock, 3}, PropSpec{PropKind::Stump, &makeStump, 3},
                            PropSpec{PropKind::Logs, &makeLogs, 3}, PropSpec{PropKind::Haystack, &makeHaystack, 3},
                            PropSpec{PropKind::Crates, &makeCrates, 4}, PropSpec{PropKind::Fence, &makeFence, 3}}) {
    for (int v = 0; v < p.variants; ++v) {
      const VoxelModel m = p.make(v);
      BakeOptions o;
      o.yawRadians = 0.7f + v * 1.9f;
      o.pivotX = m.sizeX() / 2.0f;
      o.pivotY = m.sizeY() / 2.0f;
      o.voxelTiles = kPropVoxel;
      o.supersample = 2;
      o.outline = false;
      o.quantize = false;
      o.shadowAlpha = 110;
      BakedSprite s = bakeVoxels(m, pal, o);
      cat.props[static_cast<size_t>(p.kind)].push_back(ab.add(std::move(s.image), s.anchorX, s.anchorY));
    }
  }

  Image white(4, 4);
  for (auto& p : white.px) p = {255, 255, 255, 255};
  cat.white = ab.add(std::move(white));
  cat.selectSmall = ab.add(makeEllipseRing(14, 7), 7, 3);
  cat.selectLarge = ab.add(makeEllipseRing(38, 19), 19, 9);
  cat.moveMarker = ab.add(makeDiamondOutline(32, 16), 16, 8);

  for (const std::string& dir : options.packDirs) applyArtOverrides(ab, cat, dir, true);  // props, effects
  for (const std::string& dir : options.overrideDirs) applyArtOverrides(ab, cat, dir);
  if (!options.exportDir.empty()) exportArt(ab, cat, options.exportDir);

  for (int size : {1024, 2048, 4096, 8192}) {
    if (ab.build(size, cat.atlas, cat.frames)) {
      SLV_INFO("Placeholder art: %zu sprites packed into a %dx%d atlas", ab.count(), size, size);
      return true;
    }
  }
  error = "the art does not fit in an 8192x8192 atlas";
  return false;
}

}  // namespace slave

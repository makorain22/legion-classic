#include "slave/assets/placeholder_art.hpp"

#include <algorithm>
#include <cmath>

#include "slave/assets/voxel.hpp"
#include "slave/core/log.hpp"
#include "slave/core/rng.hpp"

namespace slave {

namespace {

// ---------------------------------------------------------------------------
// Voxel palette. Faction colours (BASE*, UNIFORM*, HELMET) change per faction;
// TEAM* entries are recoloured per player by the shader.
// ---------------------------------------------------------------------------
enum : uint8_t {
  TEAM = 1, TEAM_DARK = 2, TEAM_LIGHT = 3,
  BASE = 4, BASE_DARK = 5, BASE_LIGHT = 6, UNIFORM = 7, UNIFORM_DARK = 8,
  STEEL_DARK = 10, STEEL = 11, TRACK_A = 12, TRACK_B = 13, RUBBER = 14, SKIN = 15,
  HELMET = 16, GLASS = 17, LAMP = 18, BOOT = 19,
  TRUNK = 20, LEAF_DARK = 21, LEAF = 22, LEAF_LIGHT = 23, PINE = 24, PINE_DARK = 25,
  ROCK = 26, ROCK_DARK = 27, ROCK_LIGHT = 28, BUSH = 29,
};

struct FactionLook {
  const char* id;
  Rgba base, uniform, helmet;
  bool sloped;  // Russian armour: sloped plates and round turrets
};

const FactionLook kFactions[] = {
    {"ger", {84, 88, 90, 255}, {98, 104, 96, 255}, {78, 84, 80, 255}, false},
    {"rus", {88, 98, 58, 255}, {126, 112, 76, 255}, {72, 86, 54, 255}, true},
};

Rgba scale(Rgba c, float k) {
  auto ch = [&](uint8_t v) { return static_cast<uint8_t>(std::clamp(v * k, 0.0f, 255.0f)); };
  return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

VoxelPalette makePalette(const FactionLook& f) {
  VoxelPalette p;
  p.setTeam(TEAM, 1.0f);
  p.setTeam(TEAM_DARK, 0.72f);
  p.setTeam(TEAM_LIGHT, 1.3f);
  p.set(BASE, f.base);
  p.set(BASE_DARK, scale(f.base, 0.78f));
  p.set(BASE_LIGHT, scale(f.base, 1.22f));
  p.set(UNIFORM, f.uniform);
  p.set(UNIFORM_DARK, scale(f.uniform, 0.75f));
  p.set(HELMET, f.helmet);
  p.set(STEEL_DARK, {46, 48, 46, 255});
  p.set(STEEL, {104, 108, 102, 255});
  p.set(TRACK_A, {36, 34, 32, 255});
  p.set(TRACK_B, {82, 78, 70, 255});
  p.set(RUBBER, {30, 30, 30, 255});
  p.set(SKIN, {214, 168, 128, 255});
  p.set(GLASS, {126, 156, 168, 255});
  p.set(LAMP, {236, 214, 140, 255});
  p.set(BOOT, {40, 32, 24, 255});
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
  return p;
}

// ---------------------------------------------------------------------------
// Placeholder unit models. All face +X. `f` = animation frame 0..3.
// Team colour only on markings, so armies are told apart by faction shape
// and colour first, then by player colour.
// ---------------------------------------------------------------------------
VoxelModel makeTank(const FactionLook& look, int f) {
  VoxelModel m(22, 13, 8);
  for (int x = 1; x < 17; ++x) {  // tracks, with links that shift when driving
    const uint8_t c = ((x + f) % 2) ? TRACK_A : TRACK_B;
    m.box(x, 0, 0, x + 1, 2, 2, c);
    m.box(x, 11, 0, x + 1, 13, 2, c);
  }
  m.box(1, 2, 1, 17, 11, 3, BASE_DARK);  // lower hull
  m.box(1, 0, 2, 17, 13, 3, BASE);       // fenders over the tracks
  if (look.sloped) {
    m.box(2, 1, 3, 14, 12, 4, BASE);       // short deck
    m.box(14, 1, 2, 18, 12, 3, BASE_DARK); // long sloped glacis
    m.box(17, 2, 1, 18, 11, 2, BASE_DARK);
    m.box(2, 3, 3, 5, 10, 4, STEEL_DARK);  // engine grille
    m.ellipsoid(8.5f, 6.5f, 4.6f, 3.6f, 3.6f, 1.8f, BASE);  // round turret
    for (int y = 0; y < 13; ++y)
      for (int x = 0; x < 22; ++x)
        if (m.get(x, y, 4) == BASE) m.set(x, y, 4, TEAM);    // team band
    m.box(8, 5, 6, 10, 8, 7, BASE_LIGHT);  // hatch
    m.box(12, 6, 5, 22, 7, 6, STEEL_DARK); // long gun
  } else {
    m.box(2, 1, 3, 16, 12, 4, BASE);       // flat deck
    m.box(16, 2, 1, 18, 11, 3, BASE_DARK); // upright nose plate
    m.box(1, 0, 1, 17, 1, 3, BASE_DARK);   // side skirts
    m.box(1, 12, 1, 17, 13, 3, BASE_DARK);
    m.box(2, 3, 3, 5, 10, 4, STEEL_DARK);  // engine grille
    m.box(6, 3, 4, 13, 10, 6, BASE);       // boxy turret
    m.box(6, 3, 4, 13, 10, 5, TEAM);       // team band around the turret
    m.box(7, 4, 6, 9, 6, 7, BASE_LIGHT);   // commander's cupola
    m.box(13, 5, 4, 14, 8, 6, BASE_DARK);  // gun mantlet
    m.box(14, 6, 5, 21, 7, 6, STEEL_DARK); // gun
    m.box(20, 6, 5, 22, 7, 6, STEEL);      // muzzle brake
  }
  m.set(17, 1, 2, LAMP);
  m.set(17, 11, 2, LAMP);
  return m;
}

VoxelModel makeScoutCar(const FactionLook& look, int f) {
  VoxelModel m(14, 8, 6);
  m.box(1, 2, 1, 13, 6, 3, BASE);       // body
  m.box(1, 1, 2, 13, 7, 3, BASE_DARK);  // mudguards
  for (int wx : {2, 9})
    for (int wy : {0, 6}) {
      m.box(wx, wy, 0, wx + 3, wy + 2, 2, RUBBER);
      m.set(wx + (f % 3), wy == 0 ? 0 : 7, 1, STEEL);  // turning hub
    }
  m.box(3, 2, 3, 7, 6, 4, BASE_DARK);   // cab
  m.box(3, 2, 4, 7, 6, 5, TEAM);        // roof marking
  m.box(7, 2, 3, 8, 6, 4, GLASS);       // windscreen
  if (look.sloped) m.box(8, 2, 3, 12, 6, 4, BASE_DARK);  // long bonnet
  m.box(4, 3, 5, 5, 5, 6, STEEL_DARK);  // gun mount
  m.box(5, 4, 5, 10, 5, 6, STEEL_DARK); // machine gun
  m.set(12, 2, 2, LAMP);
  m.set(12, 5, 2, LAMP);
  return m;
}

VoxelModel makeRifleman(const FactionLook& look, int f) {
  VoxelModel m(7, 4, 10);
  const int stride = (f == 1) ? 1 : (f == 3) ? -1 : 0;
  // Legs: lower legs swing when walking.
  m.box(2 + stride, 0, 0, 4 + stride, 2, 2, UNIFORM_DARK);
  m.box(2 - stride, 2, 0, 4 - stride, 4, 2, UNIFORM_DARK);
  m.box(2 + stride, 0, 0, 4 + stride, 2, 1, BOOT);
  m.box(2 - stride, 2, 0, 4 - stride, 4, 1, BOOT);
  m.box(2, 0, 2, 4, 4, 3, UNIFORM_DARK);  // hips
  m.box(2, 0, 3, 4, 4, 6, TEAM);          // tunic in team colour (readable at 1x)
  m.box(2, 0, 3, 4, 4, 4, UNIFORM_DARK);  // belt
  m.box(1, 1, 3, 2, 3, 6, UNIFORM);       // pack
  m.set(4, 0, 4, SKIN);                   // hands
  m.set(4, 3, 4, SKIN);
  m.box(3, 1, 4, 7, 2, 5, STEEL_DARK);    // rifle
  m.box(2, 1, 6, 4, 3, 8, SKIN);          // head
  if (look.sloped) {
    m.box(2, 0, 8, 4, 4, 9, HELMET);      // round helmet
    m.box(1, 1, 8, 5, 3, 9, HELMET);
  } else {
    m.box(1, 0, 8, 5, 4, 9, HELMET);      // flared helmet
  }
  m.box(2, 1, 9, 4, 3, 10, HELMET);
  return m;
}

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

// A fallen rifleman, lying along +X.
VoxelModel makeRiflemanDead(const FactionLook& look, int) {
  VoxelModel m(11, 5, 3);
  m.box(0, 1, 0, 4, 3, 1, UNIFORM_DARK);  // legs
  m.box(0, 1, 0, 1, 3, 1, BOOT);
  m.box(4, 0, 0, 8, 4, 2, TEAM);          // torso
  m.box(4, 0, 0, 8, 4, 1, UNIFORM_DARK);
  m.box(8, 1, 0, 10, 3, 2, SKIN);         // head
  m.box(8, 1, 1, 10, 3, 2, HELMET);
  m.box(3, 4, 0, 9, 5, 1, STEEL_DARK);    // dropped rifle
  (void)look;
  return m;
}

// Copies voxels with z >= zMin into `top` and the rest into `bottom`.
void splitAt(const VoxelModel& m, int zMin, VoxelModel& bottom, VoxelModel& top) {
  bottom = VoxelModel(m.sizeX(), m.sizeY(), m.sizeZ());
  top = VoxelModel(m.sizeX(), m.sizeY(), m.sizeZ());
  for (int z = 0; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x)
        if (const uint8_t c = m.get(x, y, z)) (z >= zMin ? top : bottom).set(x, y, z, c);
}

constexpr float kTwoPi = 6.2831853f;

// Bakes a unit in 16 facings for each faction look. With turretZ > 0, every
// voxel at or above that height becomes a separate turret sheet that the
// game rotates on its own; the turret turns around its own centre.
void addUnit(AtlasBuilder& ab, SpriteCatalog& cat, const std::string& name,
             VoxelModel (*make)(const FactionLook&, int), int animFrames, int turretZ,
             VoxelModel (*makeCorpse)(const FactionLook&, int) = nullptr) {
  for (const FactionLook& look : kFactions) {
    const VoxelPalette pal = makePalette(look);
    UnitArt art;
    art.body.facings = 16;
    art.body.animFrames = animFrames;
    art.body.first = static_cast<int>(ab.count());
    VoxelModel turretModel;
    for (int a = 0; a < animFrames; ++a) {
      const VoxelModel full = make(look, a);
      VoxelModel body = full;
      if (turretZ > 0) splitAt(full, turretZ, body, turretModel);
      for (int f = 0; f < 16; ++f) {
        BakeOptions o;
        o.yawRadians = static_cast<float>(f) * kTwoPi / 16.0f;
        o.pivotX = full.sizeX() / 2.0f;
        o.pivotY = full.sizeY() / 2.0f;
        o.shadowModel = &full;
        BakedSprite s = bakeVoxels(body, pal, o);
        ab.add(std::move(s.image), s.anchorX, s.anchorY);
      }
    }
    if (turretZ > 0) {
      // Turret centre: middle of the turret's lowest layer.
      int x0 = 1000, x1 = -1, y0 = 1000, y1 = -1;
      for (int y = 0; y < turretModel.sizeY(); ++y)
        for (int x = 0; x < turretModel.sizeX(); ++x)
          if (turretModel.get(x, y, turretZ)) {
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
            y0 = std::min(y0, y);
            y1 = std::max(y1, y);
          }
      const float cx = (x0 + x1 + 1) / 2.0f, cy = (y0 + y1 + 1) / 2.0f;
      art.hasTurret = true;
      art.turretOffset = (cx - turretModel.sizeX() / 2.0f) / 16.0f * LEPTONS_PER_TILE;
      art.turret.facings = 16;
      art.turret.animFrames = 1;
      art.turret.first = static_cast<int>(ab.count());
      for (int f = 0; f < 16; ++f) {
        BakeOptions o;
        o.yawRadians = static_cast<float>(f) * kTwoPi / 16.0f;
        o.pivotX = cx;
        o.pivotY = cy;
        o.shadow = false;
        BakedSprite s = bakeVoxels(turretModel, pal, o);
        ab.add(std::move(s.image), s.anchorX, s.anchorY);
      }
    }
    if (makeCorpse) {
      const VoxelModel dead = makeCorpse(look, 0);
      art.hasCorpse = true;
      art.corpse.facings = 16;
      art.corpse.first = static_cast<int>(ab.count());
      for (int f = 0; f < 16; ++f) {
        BakeOptions o;
        o.yawRadians = static_cast<float>(f) * kTwoPi / 16.0f;
        o.pivotX = dead.sizeX() / 2.0f;
        o.pivotY = dead.sizeY() / 2.0f;
        o.shadowAlpha = 60;
        BakedSprite s = bakeVoxels(dead, pal, o);
        ab.add(std::move(s.image), s.anchorX, s.anchorY);
      }
    }
    cat.units[name + "@" + look.id] = art;
  }
}

// ---------------------------------------------------------------------------
// Procedural battle effects
// ---------------------------------------------------------------------------
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
  return noise2(x, y, seed) * 0.55f + noise2(x * 2.1f, y * 2.1f, seed + 7) * 0.3f + noise2(x * 4.3f, y * 4.3f, seed + 13) * 0.15f;
}
uint8_t u8(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f)); }

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

}  // namespace

bool buildPlaceholderArt(SpriteCatalog& cat, std::string& error) {
  const VoxelPalette pal = makePalette(kFactions[0]);
  AtlasBuilder ab;

  addUnit(ab, cat, "tank", &makeTank, 4, 4);
  addUnit(ab, cat, "scoutcar", &makeScoutCar, 3, 5);
  addUnit(ab, cat, "rifleman", &makeRifleman, 4, 0, &makeRiflemanDead);
  addEffects(ab, cat.effects);

  struct PropSpec {
    PropKind kind;
    VoxelModel (*make)(int);
    int variants;
  };
  for (const PropSpec& p : {PropSpec{PropKind::Tree, &makeTree, 5}, PropSpec{PropKind::Pine, &makePine, 3},
                            PropSpec{PropKind::Bush, &makeBush, 4}, PropSpec{PropKind::Rock, &makeRock, 3}}) {
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
  cat.selectSmall = ab.add(makeEllipseRing(22, 11), 11, 5);
  cat.selectLarge = ab.add(makeEllipseRing(46, 23), 23, 11);
  cat.moveMarker = ab.add(makeDiamondOutline(32, 16), 16, 8);

  for (int size : {1024, 2048, 4096}) {
    if (ab.build(size, cat.atlas, cat.frames)) {
      SLV_INFO("Placeholder art: %zu sprites packed into a %dx%d atlas", ab.count(), size, size);
      return true;
    }
  }
  error = "placeholder art does not fit in a 4096x4096 atlas";
  return false;
}

}  // namespace slave

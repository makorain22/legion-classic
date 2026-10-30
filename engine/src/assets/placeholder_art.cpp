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
  // Structures.
  CONCRETE = 30, CONCRETE_DARK = 31, CONCRETE_LIGHT = 32, BRICK = 33, BRICK_DARK = 34,
  ROOF = 35, ROOF_DARK = 36, ROOF_LIGHT = 37, WOOD = 38, WOOD_DARK = 39, WINDOW = 40, WINDOW_LIT = 41,
  SANDBAG = 42, SANDBAG_DARK = 43, EARTH = 44, EARTH_DARK = 45, RUST = 46, TANK_PAINT = 47, TANK_PAINT_DARK = 48,
  CANVAS = 49, CANVAS_DARK = 50, SOOT = 51,
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
// Scale of one voxel, in tiles. Vehicles and infantry use different scales so
// their sizes stay true to life: a soldier stands about two thirds as tall as
// a medium tank, and a tank is under one tile long, so masses of units read as
// an army on a big battlefield rather than toys on a board.
constexpr float kVehicleVoxel = 1.0f / 18.0f;
constexpr float kInfantryVoxel = 1.0f / 32.0f;

BakeOptions unitBake(float voxelTiles) {
  BakeOptions o;
  o.voxelTiles = voxelTiles;
  o.supersample = 2;  // soft edges, no outlines: the gritty look
  o.outline = false;
  o.quantize = false;
  return o;
}

void addUnit(AtlasBuilder& ab, SpriteCatalog& cat, const std::string& name,
             VoxelModel (*make)(const FactionLook&, int), int animFrames, int turretZ,
             VoxelModel (*makeCorpse)(const FactionLook&, int) = nullptr, float voxelTiles = kVehicleVoxel) {
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
        BakeOptions o = unitBake(voxelTiles);
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
      art.turretOffset = (cx - turretModel.sizeX() / 2.0f) * voxelTiles * LEPTONS_PER_TILE;
      art.turret.facings = 16;
      art.turret.animFrames = 1;
      art.turret.first = static_cast<int>(ab.count());
      for (int f = 0; f < 16; ++f) {
        BakeOptions o = unitBake(voxelTiles);
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
        BakeOptions o = unitBake(voxelTiles);
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

// ---------------------------------------------------------------------------
// Supply trucks and construction trucks (vehicle scale, 16 facings)
// ---------------------------------------------------------------------------
void wheels(VoxelModel& m, std::initializer_list<int> xs, int width, int f) {
  for (int wx : xs)
    for (int wy : {0, width - 2}) {
      m.box(wx, wy, 0, wx + 3, wy + 2, 3, RUBBER);
      m.set(wx + 1, wy == 0 ? 0 : width - 1, 1 + (f % 2), STEEL);  // hub, turning
    }
}

VoxelModel makeSupplyTruck(const FactionLook& look, int f) {
  VoxelModel m(20, 9, 9);
  wheels(m, {2, 7, 14}, 9, f);
  m.box(1, 1, 2, 19, 8, 3, STEEL_DARK);        // chassis
  m.box(14, 1, 3, 19, 8, 6, BASE);             // cab
  m.box(15, 1, 6, 19, 8, 7, BASE_DARK);        // cab roof
  m.box(18, 2, 4, 19, 7, 6, GLASS);            // windscreen
  if (look.sloped) m.box(18, 2, 3, 20, 7, 5, BASE_DARK);  // snub bonnet
  // Oil tank on the back: a rounded drum along the bed.
  for (int x = 2; x < 14; ++x)
    for (int y = 1; y < 8; ++y)
      for (int z = 3; z < 9; ++z) {
        const float dy = y + 0.5f - 4.5f, dz = z + 0.5f - 5.8f;
        if (dy * dy / 12.0f + dz * dz / 9.0f <= 1.0f) m.set(x, y, z, BASE_DARK);
      }
  m.box(2, 3, 8, 14, 6, 9, TEAM);              // stripe along the top
  m.box(7, 4, 8, 9, 5, 9, STEEL);              // filler cap
  m.set(19, 1, 3, LAMP);
  m.set(19, 7, 3, LAMP);
  return m;
}

VoxelModel makeConstructionTruck(const FactionLook& look, int f) {
  VoxelModel m(24, 11, 12);
  wheels(m, {2, 7, 12, 18}, 11, f);
  m.box(1, 1, 2, 23, 10, 3, STEEL_DARK);
  m.box(18, 1, 3, 23, 10, 7, BASE);            // cab
  m.box(19, 1, 7, 23, 10, 8, BASE_DARK);
  m.box(22, 2, 4, 23, 9, 6, GLASS);
  // Folded workshop: a canvas-covered box with a crane on top.
  m.box(2, 1, 3, 17, 10, 9, CANVAS);
  m.box(2, 1, 3, 17, 10, 4, CANVAS_DARK);
  for (int x = 3; x < 17; x += 4) m.box(x, 1, 4, x + 1, 10, 9, CANVAS_DARK);  // ribs
  m.box(2, 3, 9, 17, 8, 10, TEAM);
  m.box(5, 5, 10, 6, 6, 12, STEEL);            // crane mast
  m.box(5, 5, 11, 16, 6, 12, STEEL_DARK);      // jib folded forward
  (void)look;
  m.set(23, 1, 3, LAMP);
  m.set(23, 9, 3, LAMP);
  return m;
}

// ---------------------------------------------------------------------------
// Structures. One voxel = 1/12 tile, so a footprint of W x H tiles is
// 12W x 12H voxels. Baked once (they don't turn), supersampled.
// ---------------------------------------------------------------------------
constexpr float kStructureVoxel = 1.0f / 12.0f;

// Grime: random darker and lighter voxels over a material.
void weather(VoxelModel& m, uint8_t from, uint8_t dark, uint8_t light, uint32_t seed, int pd = 18, int pl = 8) {
  speckle(m, from, dark, light, seed, pd, pl);
}

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

// A vertical cylinder (tanks, chimneys).
void cylinder(VoxelModel& m, float cx, float cy, float r, int z0, int z1, uint8_t c) {
  for (int z = z0; z < z1; ++z)
    for (int y = static_cast<int>(cy - r - 1); y <= static_cast<int>(cy + r + 1); ++y)
      for (int x = static_cast<int>(cx - r - 1); x <= static_cast<int>(cx + r + 1); ++x) {
        const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        if (dx * dx + dy * dy <= r * r) m.set(x, y, z, c);
      }
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

VoxelModel makeBunker(const FactionLook& look) {
  VoxelModel m(24, 24, 12);
  m.box(0, 0, 0, 24, 24, 1, EARTH);
  // Rounded concrete pillbox with earth banked against it.
  for (int z = 1; z < 9; ++z) {
    const float r = 10.5f - std::max(0, z - 5) * 1.2f;
    cylinder(m, 12, 12, r + 1.5f - z * 0.18f, z, z + 1, z < 4 ? EARTH : CONCRETE);
  }
  weather(m, EARTH, EARTH_DARK, SANDBAG_DARK, 61, 30, 10);
  weather(m, CONCRETE, CONCRETE_DARK, CONCRETE_LIGHT, 62, 25, 10);
  cylinder(m, 12, 12, 7.0f, 9, 10, look.sloped ? CONCRETE_DARK : CONCRETE);
  // Firing slits all round.
  for (int a = 0; a < 8; ++a) {
    const float ang = a * 0.785f;
    const int x = static_cast<int>(12 + std::cos(ang) * 8.6f), y = static_cast<int>(12 + std::sin(ang) * 8.6f);
    m.box(x, y, 5, x + 1, y + 1, 6, WINDOW);
  }
  m.box(10, 10, 10, 14, 14, 11, TEAM);
  return m;
}

// The bunker's machine gun, turning on the roof.
VoxelModel makeBunkerGun(const FactionLook&) {
  VoxelModel m(24, 24, 13);
  m.box(11, 11, 10, 13, 13, 12, STEEL_DARK);
  m.box(13, 11, 11, 19, 12, 12, STEEL_DARK);
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

// A low, lumpy wall of bags filling the tile, so neighbouring pieces join up.
VoxelModel makeSandbags(const FactionLook&) {
  VoxelModel m(12, 12, 4);
  for (int y = 0; y < 12; ++y)
    for (int x = 0; x < 12; ++x) {
      const float dx = std::abs(x + 0.5f - 6.0f), dy = std::abs(y + 0.5f - 6.0f);
      const int h = (dx < 4.5f && dy < 4.5f) ? 3 : (dx < 6 && dy < 6) ? 2 : 1;
      for (int z = 0; z < h; ++z) {
        // Each bag is 3 x 2 voxels, rows offset like brickwork; seams darker.
        const bool seam = ((x + (z % 2) * 1 + (y / 2) % 2) % 3 == 0) || (y % 2 == 0 && z == h - 1 && x % 3 == 1);
        m.set(x, y, z, seam ? SANDBAG_DARK : SANDBAG);
      }
    }
  return m;
}

// Two stakes and a loose coil of wire between them: see-through, so a line of
// it reads as an entanglement rather than a wall.
VoxelModel makeWire(const FactionLook&) {
  VoxelModel m(12, 12, 5);
  m.box(2, 2, 0, 3, 3, 5, WOOD_DARK);
  m.box(9, 9, 0, 10, 10, 5, WOOD_DARK);
  for (int i = 0; i < 24; ++i) {
    const float t = i / 24.0f * 6.2831853f * 2.0f;  // two loops of the coil
    const float u = i / 24.0f;
    const int x = static_cast<int>(1.5f + u * 9.0f + std::cos(t) * 1.4f);
    const int y = static_cast<int>(1.5f + u * 9.0f + std::sin(t) * 1.4f);
    const int z = static_cast<int>(2.0f + std::sin(t) * 1.5f);
    if (i % 3 != 2) m.set(x, y, z, i % 2 ? STEEL : STEEL_DARK);
  }
  return m;
}

// Czech hedgehogs: three crossed girders each.
VoxelModel makeTankTraps(const FactionLook&) {
  VoxelModel m(12, 12, 8);
  auto hog = [&](int cx, int cy) {
    for (int i = -3; i <= 3; ++i) {
      m.set(cx + i, cy, 3 + i / 2 + 1, RUST);
      m.set(cx, cy + i, 4 - i / 2, STEEL_DARK);
      m.set(cx + i / 2, cy - i / 2, 3 + (i + 3) / 2, RUST);
      m.set(cx + i, cy + i, 1 + std::abs(i), STEEL_DARK);
    }
  };
  hog(4, 4);
  hog(8, 8);
  return m;
}

// Bakes a structure: one view for the body, and 16 facings of its turret if any.
void addStructure(AtlasBuilder& ab, SpriteCatalog& cat, const std::string& name, VoxelModel (*make)(const FactionLook&),
                  VoxelModel (*turret)(const FactionLook&) = nullptr) {
  for (const FactionLook& look : kFactions) {
    const VoxelPalette pal = makePalette(look);
    const VoxelModel body = make(look);
    UnitArt art;
    art.body.facings = 1;
    art.body.animFrames = 1;
    art.body.first = static_cast<int>(ab.count());
    BakeOptions o = unitBake(kStructureVoxel);
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
        BakeOptions to = unitBake(kStructureVoxel);
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

}  // namespace

bool buildPlaceholderArt(SpriteCatalog& cat, std::string& error) {
  const VoxelPalette pal = makePalette(kFactions[0]);
  AtlasBuilder ab;

  addUnit(ab, cat, "tank", &makeTank, 4, 4);
  addUnit(ab, cat, "scoutcar", &makeScoutCar, 3, 5);
  addUnit(ab, cat, "rifleman", &makeRifleman, 4, 0, &makeRiflemanDead, kInfantryVoxel);
  addEffects(ab, cat.effects);
  addUnit(ab, cat, "truck", &makeSupplyTruck, 2, 0);
  addUnit(ab, cat, "mcv", &makeConstructionTruck, 2, 0);
  addStructure(ab, cat, "yard", &makeYard);
  addStructure(ab, cat, "power", &makePowerStation);
  addStructure(ab, cat, "refinery", &makeRefinery);
  addStructure(ab, cat, "barracks", &makeBarracks);
  addStructure(ab, cat, "factory", &makeFactory);
  addStructure(ab, cat, "tower", &makeWatchtower);
  addStructure(ab, cat, "bunker", &makeBunker, &makeBunkerGun);
  addStructure(ab, cat, "atgun", &makeAtGunPit, &makeAtGun);
  addStructure(ab, cat, "sandbags", &makeSandbags);
  addStructure(ab, cat, "wire", &makeWire);
  addStructure(ab, cat, "traps", &makeTankTraps);
  addEconomyDecals(ab, cat);
  {
    // Trenches are ground decals; their "unit art" is the lone foxhole piece.
    for (const FactionLook& look : kFactions) {
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
  cat.selectSmall = ab.add(makeEllipseRing(14, 7), 7, 3);
  cat.selectLarge = ab.add(makeEllipseRing(38, 19), 19, 9);
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

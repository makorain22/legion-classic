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
  p.set(TRUNK, {94, 66, 40, 255});
  p.set(LEAF_DARK, {44, 80, 32, 255});
  p.set(LEAF, {62, 106, 40, 255});
  p.set(LEAF_LIGHT, {92, 138, 52, 255});
  p.set(PINE, {40, 80, 50, 255});
  p.set(PINE_DARK, {26, 58, 38, 255});
  p.set(ROCK, {130, 126, 118, 255});
  p.set(ROCK_DARK, {92, 88, 82, 255});
  p.set(ROCK_LIGHT, {164, 160, 150, 255});
  p.set(BUSH, {70, 112, 44, 255});
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

VoxelModel makeTree(int v) {
  VoxelModel m(18, 18, 24);
  const float r = 5.0f + (v % 2);
  const float cz = 14.0f + (v % 3);
  m.box(8, 8, 0, 10, 10, static_cast<int>(cz) - 2, TRUNK);
  m.ellipsoid(9, 9, cz, r, r, r * 0.9f, LEAF);
  m.ellipsoid(9 + (v % 2 ? 2.5f : -2.0f), 9 + 1.5f, cz - 2.0f, r * 0.6f, r * 0.6f, r * 0.6f, LEAF);
  speckle(m, LEAF, LEAF_DARK, LEAF_LIGHT, 1234u + v, 22, 14);
  // Sunlit crown.
  for (int z = static_cast<int>(cz) + 2; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x)
        if (m.get(x, y, z) == LEAF && hash32(x * 7 + y * 131 + z * 977 + v) % 3 == 0)
          m.set(x, y, z, LEAF_LIGHT);
  return m;
}

VoxelModel makePine(int v) {
  VoxelModel m(14, 14, 28);
  m.box(6, 6, 0, 8, 8, 6, TRUNK);
  const int top = 24 + (v % 3);
  for (int z = 4; z < top; ++z) {
    float r = 1.0f + (top - z) * 0.24f;
    if (z % 4 == 0) r -= 0.8f;  // tiered branches
    for (int y = 0; y < 14; ++y)
      for (int x = 0; x < 14; ++x) {
        const float dx = x + 0.5f - 7.0f, dy = y + 0.5f - 7.0f;
        if (dx * dx + dy * dy <= r * r) m.set(x, y, z, PINE);
      }
  }
  speckle(m, PINE, PINE_DARK, LEAF_DARK, 77u + v, 30, 8);
  return m;
}

VoxelModel makeBush(int v) {
  VoxelModel m(11, 11, 7);
  m.ellipsoid(5.5f, 5.5f, 2.5f, 4.0f + (v % 2) * 0.6f, 3.6f, 3.0f, BUSH);
  speckle(m, BUSH, LEAF_DARK, LEAF_LIGHT, 999u + v, 25, 15);
  return m;
}

VoxelModel makeRock(int v) {
  VoxelModel m(11, 9, 5);
  m.ellipsoid(5.5f, 4.5f, 1.2f, 4.2f - (v % 2), 3.2f, 2.6f, ROCK);
  speckle(m, ROCK, ROCK_DARK, ROCK_LIGHT, 4242u + v, 25, 20);
  return m;
}

// ---------------------------------------------------------------------------
// Procedural 64 x 32 terrain tiles
// ---------------------------------------------------------------------------
bool inDiamond(int x, int y) {
  const int half = y < 16 ? (y + 1) * 2 : (32 - y) * 2;
  return x >= 32 - half && x < 32 + half;
}

Image makeTile(Terrain t, int variant) {
  static const Rgba grass[5] = {{78, 110, 46, 255}, {70, 100, 42, 255}, {88, 120, 52, 255},
                                {62, 90, 38, 255},  {98, 128, 58, 255}};
  static const Rgba meadow[5] = {{92, 124, 50, 255}, {104, 134, 56, 255}, {84, 114, 46, 255},
                                 {116, 142, 62, 255}, {76, 106, 42, 255}};
  static const Rgba dirt[5] = {{124, 98, 64, 255}, {112, 88, 56, 255}, {136, 110, 74, 255},
                               {100, 80, 52, 255},  {146, 120, 82, 255}};
  const Rgba* pal = t == Terrain::Dirt ? dirt : t == Terrain::Meadow ? meadow : grass;

  Image img(64, 32);
  const uint32_t seed = static_cast<uint32_t>(t) * 1000 + variant * 37 + 5;
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 64; ++x) {
      if (!inDiamond(x, y)) continue;
      // Clumpy noise: pixels pairs share a value horizontally.
      const uint32_t h = hash2(x / 2, y, seed) % 100;
      int idx = h < 46 ? 0 : h < 68 ? 1 : h < 84 ? 2 : h < 95 ? 3 : 4;
      img.at(x, y) = pal[idx];
    }
  // Details: grass blades, flowers, pebbles.
  for (int y = 1; y < 32; ++y)
    for (int x = 0; x < 64; ++x) {
      if (!inDiamond(x, y) || !inDiamond(x, y - 1)) continue;
      const uint32_t h = hash2(x, y, seed + 17);
      if (t == Terrain::Dirt) {
        if (h % 41 == 0) img.at(x, y) = {156, 146, 128, 255};
        else if (h % 47 == 0) img.at(x, y) = {84, 66, 44, 255};
      } else {
        if (h % 29 == 0) {
          img.at(x, y) = pal[4];
          img.at(x, y - 1) = {static_cast<uint8_t>(std::min(255, pal[4].r + 14)),
                              static_cast<uint8_t>(std::min(255, pal[4].g + 14)), pal[4].b, 255};
        }
        if (t == Terrain::Meadow) {
          if (h % 89 == 0) img.at(x, y) = {232, 214, 112, 255};
          else if (h % 113 == 0) img.at(x, y) = {236, 234, 224, 255};
          else if (h % 151 == 0) img.at(x, y) = {196, 92, 84, 255};
        }
      }
    }
  return img;
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

void addSheet(AtlasBuilder& ab, SpriteCatalog& cat, const std::string& name,
              VoxelModel (*make)(const FactionLook&, int), int animFrames) {
  for (const FactionLook& look : kFactions) {
    const VoxelPalette pal = makePalette(look);
    SpriteSheet sheet;
    sheet.facings = 16;
    sheet.animFrames = animFrames;
    sheet.first = static_cast<int>(ab.count());
    for (int a = 0; a < animFrames; ++a) {
      const VoxelModel m = make(look, a);
      for (int f = 0; f < 16; ++f) {
        BakeOptions o;
        o.yawRadians = static_cast<float>(f) * 2.0f * 3.14159265f / 16.0f;
        o.pivotX = m.sizeX() / 2.0f;
        o.pivotY = m.sizeY() / 2.0f;
        BakedSprite s = bakeVoxels(m, pal, o);
        ab.add(std::move(s.image), s.anchorX, s.anchorY);
      }
    }
    cat.units[name + "@" + look.id] = sheet;
  }
}

}  // namespace

bool buildPlaceholderArt(SpriteCatalog& cat, std::string& error) {
  const VoxelPalette pal = makePalette(kFactions[0]);
  AtlasBuilder ab;

  addSheet(ab, cat, "tank", &makeTank, 4);
  addSheet(ab, cat, "scoutcar", &makeScoutCar, 3);
  addSheet(ab, cat, "rifleman", &makeRifleman, 4);

  struct PropSpec {
    PropKind kind;
    VoxelModel (*make)(int);
    int variants;
  };
  for (const PropSpec& p : {PropSpec{PropKind::Tree, &makeTree, 4}, PropSpec{PropKind::Pine, &makePine, 3},
                            PropSpec{PropKind::Bush, &makeBush, 3}, PropSpec{PropKind::Rock, &makeRock, 3}}) {
    for (int v = 0; v < p.variants; ++v) {
      const VoxelModel m = p.make(v);
      BakeOptions o;
      o.yawRadians = 0.7f + v * 1.9f;
      o.pivotX = m.sizeX() / 2.0f;
      o.pivotY = m.sizeY() / 2.0f;
      BakedSprite s = bakeVoxels(m, pal, o);
      cat.props[static_cast<size_t>(p.kind)].push_back(ab.add(std::move(s.image), s.anchorX, s.anchorY));
    }
  }

  for (int t = 0; t < static_cast<int>(Terrain::Count); ++t)
    for (int v = 0; v < 4; ++v)
      cat.tiles[static_cast<size_t>(t)][static_cast<size_t>(v)] =
          ab.add(makeTile(static_cast<Terrain>(t), v), 32, 0);

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

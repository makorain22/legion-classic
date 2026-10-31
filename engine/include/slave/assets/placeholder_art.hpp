#pragma once
// Slave Engine - placeholder art generated in code.
//
// Until the full art pipeline (Milestone 7), every sprite is built here from
// voxel models and procedural tiles. The sprite catalog maps names to atlas
// frames, so swapping in final art later changes no game code.

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "slave/assets/atlas.hpp"
#include "slave/map/map.hpp"

namespace slave {

// A sprite with 16 facings and N animation frames:
// frame index = first + anim * facings + facing.
struct SpriteSheet {
  int first = 0;
  int facings = 16;
  int animFrames = 1;
  int frame(int facing, int anim) const { return first + (anim % animFrames) * facings + facing % facings; }
};

// Everything needed to draw one kind of unit.
struct UnitArt {
  SpriteSheet body;
  bool hasTurret = false;
  SpriteSheet turret;         // 16 facings, drawn on top of the body
  float turretOffset = 0;     // turret pivot, in leptons along the body's forward axis
  bool hasCorpse = false;
  SpriteSheet corpse;         // 16 facings (fallen infantry)
};

// Procedural battle effects.
struct EffectArt {
  std::array<int, 8> explosion{};  // fireball animation
  std::array<int, 4> fire{};       // flickering flames
  std::array<int, 3> flash{};      // muzzle flash: small, medium, large
  int smoke = 0, dust = 0, spark = 0;
  int craterSmall = 0, craterLarge = 0, scorch = 0;
};

struct SpriteCatalog {
  Image atlas;
  std::vector<AtlasFrame> frames;
  std::unordered_map<std::string, UnitArt> units;  // by "model@faction", e.g. "tank@ger"
  EffectArt effects;
  std::array<int, 16> trench{};  // trench decal by neighbour mask (+X, +Y, -X, -Y)
  std::array<int, 4> oil{};      // oil stain variants
  std::array<int, 3> rubble{};   // building rubble, 2 / 3 / 4 tiles across
  std::array<std::vector<int>, static_cast<size_t>(PropKind::Count)> props;
  int white = 0;        // solid white block, for lines, bars and boxes
  int selectSmall = 0;  // selection ellipse for infantry
  int selectLarge = 0;  // selection ellipse for vehicles
  int moveMarker = 0;   // diamond shown where a move order was given

  const AtlasFrame& frame(int i) const { return frames[static_cast<size_t>(i)]; }
};

// Where art comes from besides the built-in voxel models.
struct ArtOptions {
  // PNG files here replace generated sprites of the same name (see
  // docs/ART_PIPELINE.md). Missing files keep the generated art.
  std::string overrideDir;
  // When set, every sprite is written here as a PNG, in the layout the
  // override folder uses, ready to be repainted.
  std::string exportDir;
};

// Builds every sprite (voxel models baked in 16 facings, effects, decals),
// applies PNG overrides and packs the atlas. Takes a second or two.
bool buildPlaceholderArt(SpriteCatalog& out, std::string& error, const ArtOptions& options = {});

// Calls fn(name, frame) for every named sprite in the catalog: "units/tank@ger/body_f03_a1",
// "props/palm_2", "effects/explosion_4"...
template <class F>
void forEachSprite(const SpriteCatalog& cat, F fn);

template <class F>
void forEachSprite(const SpriteCatalog& cat, F fn) {
  auto two = [](int v) { return (v < 10 ? std::string("0") : std::string()) + std::to_string(v); };
  for (const auto& [name, art] : cat.units) {
    const std::string dir = "units/" + name + "/";
    if (art.body.facings == 1 && art.body.animFrames == 1) {
      fn(dir + "body", art.body.first);
    } else {
      for (int a = 0; a < art.body.animFrames; ++a)
        for (int f = 0; f < art.body.facings; ++f)
          fn(dir + "body_f" + two(f) + "_a" + std::to_string(a), art.body.frame(f, a));
    }
    if (art.hasTurret)
      for (int f = 0; f < art.turret.facings; ++f) fn(dir + "turret_f" + two(f), art.turret.frame(f, 0));
    if (art.hasCorpse)
      for (int f = 0; f < art.corpse.facings; ++f) fn(dir + "corpse_f" + two(f), art.corpse.frame(f, 0));
  }
  for (size_t k = 0; k < cat.props.size(); ++k)
    for (size_t v = 0; v < cat.props[k].size(); ++v)
      fn("props/" + std::string(propKindId(static_cast<PropKind>(k))) + "_" + std::to_string(v), cat.props[k][v]);
  for (size_t i = 0; i < cat.effects.explosion.size(); ++i) fn("effects/explosion_" + std::to_string(i), cat.effects.explosion[i]);
  for (size_t i = 0; i < cat.effects.fire.size(); ++i) fn("effects/fire_" + std::to_string(i), cat.effects.fire[i]);
  for (size_t i = 0; i < cat.effects.flash.size(); ++i) fn("effects/flash_" + std::to_string(i), cat.effects.flash[i]);
  fn("effects/smoke", cat.effects.smoke);
  fn("effects/dust", cat.effects.dust);
  fn("effects/spark", cat.effects.spark);
  fn("effects/crater_small", cat.effects.craterSmall);
  fn("effects/crater_large", cat.effects.craterLarge);
  fn("effects/scorch", cat.effects.scorch);
  for (size_t i = 0; i < cat.trench.size(); ++i) fn("decals/trench_" + two(static_cast<int>(i)), cat.trench[i]);
  for (size_t i = 0; i < cat.oil.size(); ++i) fn("decals/oil_" + std::to_string(i), cat.oil[i]);
  for (size_t i = 0; i < cat.rubble.size(); ++i) fn("decals/rubble_" + std::to_string(i), cat.rubble[i]);
}

}  // namespace slave

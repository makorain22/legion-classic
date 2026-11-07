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

// A sprite drawn in `facings` directions (16, 8, 4 or 1) with N animation
// frames: frame index = first + anim * facings + facing. Callers always pass
// one of 16 facings; sheets with fewer use the nearest.
struct SpriteSheet {
  int first = 0;
  int facings = 16;
  int animFrames = 1;
  int frame(int facing16, int anim) const {
    const int f = facings == 16 ? facing16 : ((facing16 & 15) * facings + 8) / 16;
    return first + (anim % animFrames) * facings + f % facings;
  }
};

// Everything needed to draw one kind of unit.
struct UnitArt {
  SpriteSheet body;
  bool hasTurret = false;
  SpriteSheet turret;         // 16 facings, drawn on top of the body
  float turretOffset = 0;     // turret pivot, in leptons along the body's forward axis
  bool hasCorpse = false;
  SpriteSheet corpse;         // fallen infantry; with several frames, the fall is played and the last one stays
  int corpseMs = 0;           // per corpse frame
  // Body animations. With walkFrames = 0 the walk cycle is the simulation's
  // frame counter over all body frames (voxel art). Otherwise body frame 0
  // stands still, walkFrames frames from walkFirst play while moving and
  // shootFrames frames from shootFirst play when firing.
  int walkFirst = 0, walkFrames = 0, walkMs = 100;
  int shootFirst = 0, shootFrames = 0, shootMs = 100;
  int icon = -1;              // picture for the sidebar (atlas frame); -1 = draw the unit
};

// Procedural battle effects.
struct EffectArt {
  std::array<int, 8> explosion{};  // fireball animation
  std::array<int, 4> fire{};       // flickering flames
  std::array<int, 3> flash{};      // muzzle flash: small, medium, large
  int smoke = 0, dust = 0, spark = 0;
  int craterSmall = 0, craterLarge = 0, scorch = 0;
};

// Bridge pieces (24 voxels a tile): the deck's top stands kBridgeDeckZ voxels
// above the sprite's foot, the piers reaching kBridgePierDepth below the
// level a bridge is laid at.
constexpr int kBridgeDeckZ = 22;
constexpr int kBridgePierDepth = 16;

struct SpriteCatalog {
  Image atlas;
  std::vector<AtlasFrame> frames;
  std::unordered_map<std::string, UnitArt> units;  // by "model@faction", e.g. "tank@ger"
  EffectArt effects;
  std::array<int, 16> trench{};  // trench decal by neighbour mask (+X, +Y, -X, -Y)
  // Field works that join their neighbours (sandbags, wire): a standing
  // sprite per neighbour mask, by model name.
  std::unordered_map<std::string, std::array<int, 16>> joined;
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
  // PNG files in these folders replace generated sprites of the same name
  // (see docs/ART_PIPELINE.md); later folders (mods) win. Missing files keep
  // the generated art.
  std::vector<std::string> overrideDirs;
  // When set, every sprite is written here as a PNG, in the layout the
  // override folder uses, ready to be repainted.
  std::string exportDir;
  // Art packs (see art_pack.hpp): complete units that replace the voxel
  // models, loaded before them. Later packs win.
  std::vector<std::string> packDirs;
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
          fn(dir + "body_f" + two(f) + "_a" + std::to_string(a), art.body.first + a * art.body.facings + f);
    }
    if (art.hasTurret)
      for (int f = 0; f < art.turret.facings; ++f) fn(dir + "turret_f" + two(f), art.turret.first + f);
    if (art.hasCorpse) {
      for (int a = 0; a < art.corpse.animFrames; ++a)
        for (int f = 0; f < art.corpse.facings; ++f)
          fn(dir + "corpse_f" + two(f) + (art.corpse.animFrames > 1 ? "_a" + std::to_string(a) : std::string()),
             art.corpse.first + a * art.corpse.facings + f);
    }
    if (art.icon >= 0) fn("icons/" + name, art.icon);
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

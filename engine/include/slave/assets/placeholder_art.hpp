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

// Builds every placeholder sprite and packs the atlas. Takes about a second.
bool buildPlaceholderArt(SpriteCatalog& out, std::string& error);

}  // namespace slave

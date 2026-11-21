#pragma once
// Pioneer Engine - the hour and the weather, turned into light.
//
// Nothing here touches the graphics card: tables and arithmetic, tested
// without a window. The game says what time it is and what the weather is
// doing (Environment); lightingFor() gives what the renderers need:
//
//   look        what the post-processing pipeline puts over the drawn world:
//               the light's colour, mist, clouds' shadows, exposure
//   sun         where the light that casts shadows comes from (the moon at
//               night), for the shadow map and the model and ground shaders
//   direct      how much of the light comes straight from it: shading and
//               shadows soften under cloud and at night
//   the ground  wet, or under snow
//   the air     rain and snow falling, and the wind that carries them and smoke
//
// Noon and clear is the picture as the engine drew it before it knew the
// time: the identity look and the classic sun, from the upper left.

#include <cstdint>

#include "pioneer/core/weather.hpp"
#include "pioneer/math/mat.hpp"
#include "pioneer/math/vec.hpp"

namespace pioneer {

// Direction towards the sun at noon, in world space (tiles; z up). Matches
// the light the voxel baker and the classic terrain use.
inline Vec3f sunDirection() { return Vec3f{-0.35f, 0.45f, 0.80f}.normalized(); }

// How the drawn world is turned into what is seen (render/post.hpp). The
// default changes nothing.
struct SceneLook {
  float light[3] = {1.0f, 1.0f, 1.0f};  // multiplies the world: sun and sky together
  float saturation = 1.0f;
  float fog[3] = {0.62f, 0.66f, 0.70f};
  float fogDensity = 0.0f;     // per tile of distance beyond fogNear
  float fogNear = 0.7f;        // mist begins at this share of the eye's distance to what it looks at
  float groundFog = 0.0f;      // more of it where the ground is low
  float fogBase = 0.0f;        // the height (tiles) ground fog lies at...
  float fogHeight = 2.0f;      // ...and how far above it it thins out
  float skyFog = 0.0f;         // how much of the fog colour the sky takes (0: as drawn)
  float clouds = 0.0f;         // strength of clouds' shadows on the ground
  float cloudOffset[2] = {0.0f, 0.0f};  // they drift: tiles
  float cloudScale = 0.045f;   // 1 / the size of a cloud in tiles
  float exposure = 1.0f;
  float vignette = 0.0f;
  bool identity() const;       // nothing for the light pass to do
};
// Between two looks (t 0: a, 1: b).
SceneLook mixLooks(const SceneLook& a, const SceneLook& b, float t);

// The moment to draw.
struct Environment {
  float hour = 12.0f;                   // 0 .. 24
  Weather weather = Weather::Clear;
  Weather previous = Weather::Clear;    // the weather it is turning from...
  float change = 1.0f;                  // ...and how far it has got (0 .. 1; 1: done)
  float seconds = 0.0f;                 // time of the battle, for drifting clouds
};

struct Lighting {
  SceneLook look;
  Vec3f sun = sunDirection();  // towards the light that casts shadows (unit)
  float direct = 1.0f;         // share of the light straight from it: 1 on a clear day
  bool shadows = true;         // the light is strong enough for shadows worth drawing
  float wet = 0.0f;            // the ground shines with rain (0 .. 1)
  float snowCover = 0.0f;      // the ground lies under snow (0 .. 1)
  float rain = 0.0f;           // rain falling (0 .. 1)
  float snow = 0.0f;           // snow falling (0 .. 1)
  Vec2f wind{0.0f, 0.0f};      // tiles a second: smoke and what falls drift with it
  // The sun is the classic one at full strength: renderers draw exactly as before.
  bool classicSun() const;
};

Lighting lightingFor(const Environment& e);
// Towards the sun by day and the moon by night (unit, z up).
Vec3f lightDirectionAt(float hour);
// The sun's height above the horizon in degrees (negative below it).
float sunElevation(float hour);

}  // namespace pioneer

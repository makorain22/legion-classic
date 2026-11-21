#pragma once
// Pioneer Engine - rain and snow falling in the 3D view.
//
// A fixed set of drops (random places in a unit cube, made once) is drawn
// instanced; the vertex shader moves each one with the fall and the wind and
// wraps it inside a box in front of the camera. The box follows the camera
// but the drops stay put in the world, so panning doesn't drag the rain
// along. Nothing is sent to the card per frame but a few numbers.
//
// Drawn after the world (depth tested against it, not writing depth), so
// drops vanish behind hills and houses.

#include <string>

#include "pioneer/math/mat.hpp"
#include "pioneer/math/vec.hpp"
#include "pioneer/render/camera.hpp"
#include "pioneer/render/gl.hpp"

namespace pioneer {

struct PrecipitationLook {
  float rain = 0.0f;           // 0 .. 1
  float snow = 0.0f;           // 0 .. 1
  Vec2f wind{0.0f, 0.0f};      // tiles a second
  float light[3] = {1, 1, 1};  // the light they are seen in (the scene's, so they darken at night)
  int detail = 2;              // the graphics option: 0 none, 1 thin, 2 full
};

class Precipitation {
 public:
  Precipitation() = default;
  ~Precipitation();
  Precipitation(const Precipitation&) = delete;
  Precipitation& operator=(const Precipitation&) = delete;

  bool init(std::string& error);
  // `seconds`: the time that moves the drops (the battle's: they stop when it is paused).
  void draw(const Camera& cam, const PrecipitationLook& look, float seconds);
  int drawnLastFrame() const { return drawn_; }

  static constexpr int kMaxDrops = 6000;
  // How many drops a look comes to (rain and snow together), for the option chosen.
  static int dropsFor(const PrecipitationLook& look);

 private:
  void drawLayer(const Camera& cam, const PrecipitationLook& look, float seconds, bool snow, int count);
  gl::Shader shader_;
  GLuint vao_ = 0, corners_ = 0, seeds_ = 0;
  int drawn_ = 0;
};

}  // namespace pioneer

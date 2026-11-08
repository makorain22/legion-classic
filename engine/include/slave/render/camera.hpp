#pragma once
// Slave Engine - the camera.
//
// Two modes:
//   * Classic (2D): the isometric "screen-world" plane of baked sprites.
//     Zoom is limited to 0.5x, 1x and 2x so pixel art always stays crisp.
//   * 3D: a perspective camera looking at a target point on the ground, free
//     to turn (yaw), tilt (pitch) and zoom. In this mode the screen-world plane
//     IS the screen: worldToScreen and screenToWorld are the identity, so code
//     that places 2D overlays through the camera keeps working, with points
//     projected through project() first.
//
// 3D world space is measured in tiles (see math/mat.hpp): x, y along the map,
// z up, one tile of height = 64 height units.

#include "slave/math/mat.hpp"
#include "slave/math/vec.hpp"

namespace slave {

class Camera {
 public:
  void setViewport(int w, int h) {
    vw_ = w;
    vh_ = h;
    update3D();
  }
  int viewportW() const { return vw_; }
  int viewportH() const { return vh_; }

  // --- Classic 2D -------------------------------------------------------------
  Vec2f center() const { return center_; }
  void setCenter(Vec2f c) { center_ = c; clamp(); }
  // Moves the view by a screen distance (pixels). In 3D: along the ground.
  void pan(Vec2f screenDelta);

  // Classic: 0.5 / 1 / 2. 3D: continuous, pixels per tile at the target / 45.25
  // (so 1 shows the ground at the same scale as the classic view at 1x).
  float zoom() const { return threeD_ ? zoom3_ : zoom_; }
  // Classic: steps through 0.5 / 1 / 2, keeping the world point under
  // `screenPos` fixed. 3D: zooms by one wheel notch towards that point.
  void zoomStep(int dir, Vec2f screenPos);

  // Limits the view centre to this screen-world rectangle (classic).
  void setBounds(RectF b) { bounds_ = b; clamp(); }

  // Camera centre snapped to whole screen pixels (avoids shimmering sprites).
  Vec2f snappedCenter() const;

  Vec2f screenToWorld(Vec2f screenPx) const;
  Vec2f worldToScreen(Vec2f world) const;
  RectF visibleWorld(float margin = 0) const;

  // --- 3D ------------------------------------------------------------------------
  void set3D(bool on) {
    threeD_ = on;
    update3D();
  }
  bool is3D() const { return threeD_; }

  Vec3f target() const { return target_; }
  void setTarget(Vec3f t);
  // Limits the target to this rectangle of the map, in tiles.
  void setBounds3D(RectF tiles) {
    bounds3D_ = tiles;
    setTarget(target_);
  }
  float yaw() const { return yaw_; }      // radians; the eye sits at this angle from the target
  float pitch() const { return pitch_; }  // radians above the horizon
  void setYaw(float y);
  void setPitch(float p);
  void setZoom3D(float z);
  void rotate(float dYaw, float dPitch) {
    setYaw(yaw_ + dYaw);
    setPitch(pitch_ + dPitch);
  }
  // The classic view's angle: looking from the south-east corner, 30 degrees down.
  static constexpr float kClassicYaw = 0.7853982f;
  static constexpr float kDefaultPitch = 0.68f;  // about 39 degrees: a little steeper than the classic view
  static constexpr float kMinPitch = 0.38f, kMaxPitch = 1.45f;
  static constexpr float kMinZoom = 0.28f, kMaxZoom = 3.2f;
  static constexpr float kFovY = 0.56f;  // about 32 degrees

  Vec3f eye() const { return eye_; }
  Vec3f forward() const { return forward_; }  // unit vector, from the eye into the scene
  Vec3f right() const { return right_; }      // unit vector, screen right
  Vec3f up() const { return up_; }            // unit vector, screen up
  const Mat4& view() const { return view_; }
  const Mat4& projection() const { return proj_; }
  const Mat4& viewProj() const { return viewProj_; }

  // A world point (tiles) on screen: pixel position and depth (0 near .. 1 far).
  // False (and a point far off screen) when it is behind the camera.
  bool project(Vec3f world, Vec2f& screenPx, float* depth = nullptr) const;
  // The ray through a screen pixel: origin on the near plane, unit direction.
  void ray(Vec2f screenPx, Vec3f& origin, Vec3f& dir) const;
  // Screen pixels per tile for things at this world point (perspective scale).
  float pixelsPerTileAt(Vec3f world) const;
  // pixelsPerTileAt / the classic 45.25: how much to scale classic-sized
  // overlays (sprites, pick boxes) drawn at this point.
  float scaleAt(Vec3f world) const { return pixelsPerTileAt(world) / kClassicPxPerTile; }
  static constexpr float kClassicPxPerTile = 45.2548f;  // 32 * sqrt(2)

 private:
  void clamp();
  void update3D();
  float distance() const;

  Vec2f center_;
  float zoom_ = 1.0f;
  int vw_ = 1280, vh_ = 720;
  RectF bounds_{-1e9f, -1e9f, 1e9f, 1e9f};

  bool threeD_ = false;
  Vec3f target_;
  float yaw_ = kClassicYaw, pitch_ = kDefaultPitch, zoom3_ = 1.0f;
  RectF bounds3D_{-1e9f, -1e9f, 1e9f, 1e9f};
  Vec3f eye_, forward_, right_, up_;
  Mat4 view_, proj_, viewProj_, invViewProj_;
};

}  // namespace slave

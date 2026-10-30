#pragma once
// Slave Engine - the 2D camera over the isometric world.
// Zoom is limited to 0.5x, 1x and 2x so pixel art always stays crisp.

#include "slave/math/vec.hpp"

namespace slave {

class Camera {
 public:
  void setViewport(int w, int h) { vw_ = w; vh_ = h; }
  int viewportW() const { return vw_; }
  int viewportH() const { return vh_; }

  Vec2f center() const { return center_; }
  void setCenter(Vec2f c) { center_ = c; clamp(); }
  void pan(Vec2f screenDelta) { center_ += screenDelta / zoom_; clamp(); }

  float zoom() const { return zoom_; }
  // Steps through 0.5 / 1 / 2, keeping the world point under `screenPos` fixed.
  void zoomStep(int dir, Vec2f screenPos);

  // Limits the view centre to this screen-world rectangle.
  void setBounds(RectF b) { bounds_ = b; clamp(); }

  // Camera centre snapped to whole screen pixels (avoids shimmering sprites).
  Vec2f snappedCenter() const;

  Vec2f screenToWorld(Vec2f screenPx) const;
  Vec2f worldToScreen(Vec2f world) const;
  RectF visibleWorld(float margin = 0) const;

 private:
  void clamp();
  Vec2f center_;
  float zoom_ = 1.0f;
  int vw_ = 1280, vh_ = 720;
  RectF bounds_{-1e9f, -1e9f, 1e9f, 1e9f};
};

}  // namespace slave

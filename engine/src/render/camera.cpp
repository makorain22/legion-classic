#include "slave/render/camera.hpp"

#include <cmath>

namespace slave {

void Camera::zoomStep(int dir, Vec2f screenPos) {
  const Vec2f before = screenToWorld(screenPos);
  float z = zoom_;
  if (dir > 0) z = z < 1.0f ? 1.0f : 2.0f;
  if (dir < 0) z = z > 1.0f ? 1.0f : 0.5f;
  if (z == zoom_) return;
  zoom_ = z;
  const Vec2f after = screenToWorld(screenPos);
  center_ += before - after;
  clamp();
}

Vec2f Camera::snappedCenter() const {
  return {std::round(center_.x * zoom_) / zoom_, std::round(center_.y * zoom_) / zoom_};
}

Vec2f Camera::screenToWorld(Vec2f s) const {
  const Vec2f c = snappedCenter();
  return {(s.x - std::floor(vw_ * 0.5f)) / zoom_ + c.x, (s.y - std::floor(vh_ * 0.5f)) / zoom_ + c.y};
}

Vec2f Camera::worldToScreen(Vec2f w) const {
  const Vec2f c = snappedCenter();
  return {(w.x - c.x) * zoom_ + std::floor(vw_ * 0.5f), (w.y - c.y) * zoom_ + std::floor(vh_ * 0.5f)};
}

RectF Camera::visibleWorld(float margin) const {
  const Vec2f a = screenToWorld({0, 0});
  const Vec2f b = screenToWorld({static_cast<float>(vw_), static_cast<float>(vh_)});
  return {a.x - margin, a.y - margin, b.x + margin, b.y + margin};
}

void Camera::clamp() {
  center_.x = std::fmin(std::fmax(center_.x, bounds_.x0), bounds_.x1);
  center_.y = std::fmin(std::fmax(center_.y, bounds_.y0), bounds_.y1);
}

}  // namespace slave

#include "slave/render/camera.hpp"

#include <algorithm>
#include <cmath>

namespace slave {

void Camera::pan(Vec2f screenDelta) {
  if (!threeD_) {
    center_ += screenDelta / zoom_;
    clamp();
    return;
  }
  // Drag the ground: a pixel sideways moves a pixel's worth of ground at the
  // target; up and down are foreshortened by the tilt.
  const float ppt = kClassicPxPerTile * zoom3_;
  const Vec3f fh = Vec3f{forward_.x, forward_.y, 0}.normalized();
  const float sideways = screenDelta.x / ppt;
  const float along = -screenDelta.y / (ppt * std::max(0.35f, std::sin(pitch_)));
  setTarget(target_ + right_ * sideways + fh * along);
}

void Camera::zoomStep(int dir, Vec2f screenPos) {
  if (threeD_) {
    // Zoom towards the point under the mouse on the plane of the target.
    auto onPlane = [&](Vec2f px, Vec3f& out) {
      Vec3f o, d;
      ray(px, o, d);
      if (d.z > -1e-4f) return false;
      const float t = (target_.z - o.z) / d.z;
      out = o + d * t;
      return true;
    };
    Vec3f before, after;
    const bool hadBefore = onPlane(screenPos, before);
    const float z = std::clamp(zoom3_ * (dir > 0 ? 1.18f : 1.0f / 1.18f), kMinZoom, kMaxZoom);
    if (z == zoom3_) return;
    zoom3_ = z;
    update3D();
    if (hadBefore && onPlane(screenPos, after)) setTarget(target_ + Vec3f{before.x - after.x, before.y - after.y, 0});
    return;
  }
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
  if (threeD_) return {std::floor(vw_ * 0.5f), std::floor(vh_ * 0.5f)};
  return {std::round(center_.x * zoom_) / zoom_, std::round(center_.y * zoom_) / zoom_};
}

Vec2f Camera::screenToWorld(Vec2f s) const {
  if (threeD_) return s;
  const Vec2f c = snappedCenter();
  return {(s.x - std::floor(vw_ * 0.5f)) / zoom_ + c.x, (s.y - std::floor(vh_ * 0.5f)) / zoom_ + c.y};
}

Vec2f Camera::worldToScreen(Vec2f w) const {
  if (threeD_) return w;
  const Vec2f c = snappedCenter();
  return {(w.x - c.x) * zoom_ + std::floor(vw_ * 0.5f), (w.y - c.y) * zoom_ + std::floor(vh_ * 0.5f)};
}

RectF Camera::visibleWorld(float margin) const {
  if (threeD_) return {-margin, -margin, vw_ + margin, vh_ + margin};
  const Vec2f a = screenToWorld({0, 0});
  const Vec2f b = screenToWorld({static_cast<float>(vw_), static_cast<float>(vh_)});
  return {a.x - margin, a.y - margin, b.x + margin, b.y + margin};
}

void Camera::clamp() {
  center_.x = std::fmin(std::fmax(center_.x, bounds_.x0), bounds_.x1);
  center_.y = std::fmin(std::fmax(center_.y, bounds_.y0), bounds_.y1);
}

// --- 3D ------------------------------------------------------------------------

void Camera::setTarget(Vec3f t) {
  t.x = std::fmin(std::fmax(t.x, bounds3D_.x0), bounds3D_.x1);
  t.y = std::fmin(std::fmax(t.y, bounds3D_.y0), bounds3D_.y1);
  target_ = t;
  update3D();
}

void Camera::setYaw(float y) {
  constexpr float kTwoPi = 6.2831853f;
  yaw_ = std::fmod(y, kTwoPi);
  if (yaw_ < 0) yaw_ += kTwoPi;
  update3D();
}

void Camera::setPitch(float p) {
  pitch_ = std::clamp(p, kMinPitch, kMaxPitch);
  update3D();
}

void Camera::setZoom3D(float z) {
  zoom3_ = std::clamp(z, kMinZoom, kMaxZoom);
  update3D();
}

float Camera::distance() const {
  // Far enough that the target shows at zoom3_ times the classic scale.
  return static_cast<float>(std::max(1, vh_)) / (2.0f * std::tan(kFovY * 0.5f) * kClassicPxPerTile * zoom3_);
}

void Camera::update3D() {
  const float cp = std::cos(pitch_), sp = std::sin(pitch_);
  const float cy = std::cos(yaw_), sy = std::sin(yaw_);
  const float d = distance();
  eye_ = target_ + Vec3f{cy * cp, sy * cp, sp} * d;
  forward_ = (target_ - eye_).normalized();
  // The map is drawn as the classic view drew it: +x runs to the lower right
  // and +y to the lower left when looking from the south-east. That is a
  // mirror image of a right-handed world, so screen right is -(forward x up).
  right_ = Vec3f{sy, -cy, 0};
  up_ = cross(forward_, right_).normalized();
  Mat4 v;
  v.at(0, 0) = right_.x; v.at(0, 1) = right_.y; v.at(0, 2) = right_.z;
  v.at(1, 0) = up_.x; v.at(1, 1) = up_.y; v.at(1, 2) = up_.z;
  v.at(2, 0) = -forward_.x; v.at(2, 1) = -forward_.y; v.at(2, 2) = -forward_.z;
  v.at(0, 3) = -dot(right_, eye_);
  v.at(1, 3) = -dot(up_, eye_);
  v.at(2, 3) = dot(forward_, eye_);
  view_ = v;
  const float aspect = static_cast<float>(std::max(1, vw_)) / static_cast<float>(std::max(1, vh_));
  const float zNear = std::max(0.25f, d * 0.08f);
  const float zFar = d * 5.0f + 400.0f;
  proj_ = Mat4::perspective(kFovY, aspect, zNear, zFar);
  viewProj_ = proj_ * view_;
  invViewProj_ = viewProj_.inverse();
}

bool Camera::project(Vec3f w, Vec2f& s, float* depth) const {
  const Vec4f c = viewProj_ * Vec4f{w.x, w.y, w.z, 1.0f};
  if (c.w <= 1e-5f) {
    s = {-1e6f, -1e6f};
    if (depth) *depth = 1;
    return false;
  }
  const float ix = c.x / c.w, iy = c.y / c.w;
  s = {(ix * 0.5f + 0.5f) * vw_, (0.5f - iy * 0.5f) * vh_};
  if (depth) *depth = c.z / c.w * 0.5f + 0.5f;
  return true;
}

void Camera::ray(Vec2f px, Vec3f& origin, Vec3f& dir) const {
  const float nx = px.x / std::max(1, vw_) * 2.0f - 1.0f, ny = 1.0f - px.y / std::max(1, vh_) * 2.0f;
  const Vec4f a = invViewProj_ * Vec4f{nx, ny, -1, 1};
  const Vec4f b = invViewProj_ * Vec4f{nx, ny, 1, 1};
  const Vec3f pa{a.x / a.w, a.y / a.w, a.z / a.w}, pb{b.x / b.w, b.y / b.w, b.z / b.w};
  origin = pa;
  dir = (pb - pa).normalized();
}

float Camera::pixelsPerTileAt(Vec3f w) const {
  const float depth = std::max(0.05f, dot(w - eye_, forward_));
  return static_cast<float>(vh_) / (2.0f * std::tan(kFovY * 0.5f) * depth);
}

}  // namespace slave

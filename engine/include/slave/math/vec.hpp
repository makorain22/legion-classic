#pragma once
// Slave Engine - small vector and rectangle types.
// Floats are only for presentation (rendering, camera, UI). The simulation
// uses the integer types.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace slave {

struct Vec2f {
  float x = 0, y = 0;
  constexpr Vec2f operator+(Vec2f o) const { return {x + o.x, y + o.y}; }
  constexpr Vec2f operator-(Vec2f o) const { return {x - o.x, y - o.y}; }
  constexpr Vec2f operator*(float s) const { return {x * s, y * s}; }
  constexpr Vec2f operator/(float s) const { return {x / s, y / s}; }
  constexpr Vec2f& operator+=(Vec2f o) { x += o.x; y += o.y; return *this; }
  constexpr Vec2f& operator-=(Vec2f o) { x -= o.x; y -= o.y; return *this; }
  float length() const { return std::sqrt(x * x + y * y); }
};

struct Vec2i {
  int32_t x = 0, y = 0;
  constexpr Vec2i operator+(Vec2i o) const { return {x + o.x, y + o.y}; }
  constexpr Vec2i operator-(Vec2i o) const { return {x - o.x, y - o.y}; }
  constexpr bool operator==(const Vec2i&) const = default;
};

struct RectF {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // x1/y1 exclusive
  static RectF fromPoints(Vec2f a, Vec2f b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
  }
  constexpr float width() const { return x1 - x0; }
  constexpr float height() const { return y1 - y0; }
  constexpr bool contains(Vec2f p) const { return p.x >= x0 && p.x < x1 && p.y >= y0 && p.y < y1; }
  constexpr bool intersects(const RectF& o) const {
    return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1;
  }
};

}  // namespace slave

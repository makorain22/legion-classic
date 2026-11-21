// Debug drawing: shapes kept until their time is up (no graphics here).
#include "pioneer/core/debug_draw.hpp"

#include <algorithm>
#include <cmath>

namespace pioneer::debug {

DebugDraw& DebugDraw::global() {
  static DebugDraw d;
  return d;
}

void DebugDraw::add(Shape s) {
  if (!enabled_ || shapes_.size() >= kMaxShapes) return;
  s.life = std::max(0.0f, s.life);
  shapes_.push_back(std::move(s));
}

void DebugDraw::line(Vec3f a, Vec3f b, uint32_t colour, float seconds, const std::string& channel) {
  add({Shape::Line, a, b, 0.0f, colour, seconds, {}, channel});
}
void DebugDraw::box(Vec3f lo, Vec3f hi, uint32_t colour, float seconds, const std::string& channel) {
  add({Shape::Box, lo, hi, 0.0f, colour, seconds, {}, channel});
}
void DebugDraw::circle(Vec3f centre, float radius, uint32_t colour, float seconds, const std::string& channel) {
  add({Shape::Circle, centre, centre, radius, colour, seconds, {}, channel});
}
void DebugDraw::cross(Vec3f at, float size, uint32_t colour, float seconds, const std::string& channel) {
  add({Shape::Cross, at, at, size, colour, seconds, {}, channel});
}
void DebugDraw::text(Vec3f at, const std::string& words, uint32_t colour, float seconds, const std::string& channel) {
  add({Shape::Text, at, at, 0.0f, colour, seconds, words, channel});
}
void DebugDraw::arrow(Vec3f from, Vec3f dir, uint32_t colour, float seconds, const std::string& channel) {
  // Kept as a line whose `size` marks it as an arrow (the head is made in segments()).
  add({Shape::Line, from, from + dir, -1.0f, colour, seconds, {}, channel});
}

void DebugDraw::endFrame(float dt) {
  for (Shape& s : shapes_) s.life -= dt;
  std::erase_if(shapes_, [](const Shape& s) { return s.life <= 0.0f; });
}

void DebugDraw::clear(const std::string& channel) {
  if (channel.empty()) shapes_.clear();
  else std::erase_if(shapes_, [&](const Shape& s) { return s.channel == channel; });
}

void DebugDraw::setEnabled(bool on) {
  enabled_ = on;
  if (!on) shapes_.clear();
}

void DebugDraw::showChannel(const std::string& channel, bool shown) {
  std::erase(hidden_, channel);
  if (!shown) hidden_.push_back(channel);
}
bool DebugDraw::channelShown(const std::string& channel) const {
  return std::find(hidden_.begin(), hidden_.end(), channel) == hidden_.end();
}

std::vector<Vec3f> DebugDraw::segments(const Shape& s) {
  std::vector<Vec3f> out;
  switch (s.kind) {
    case Shape::Line: {
      out = {s.a, s.b};
      if (s.size < 0.0f) {  // an arrow: two short lines back from the tip
        const Vec3f d = s.b - s.a;
        const float len = std::sqrt(dot(d, d));
        if (len > 1e-6f) {
          const Vec3f u = d * (1.0f / len);
          // Something not along the arrow, to turn the head round.
          const Vec3f side = std::fabs(u.z) < 0.9f ? pioneer::cross(u, Vec3f{0, 0, 1}).normalized() : pioneer::cross(u, Vec3f{1, 0, 0}).normalized();
          const float h = std::min(len * 0.25f, 1.5f);
          out.insert(out.end(), {s.b, s.b - u * h + side * (h * 0.5f), s.b, s.b - u * h - side * (h * 0.5f)});
        }
      }
      break;
    }
    case Shape::Box: {
      const Vec3f& l = s.a;
      const Vec3f& h = s.b;
      const Vec3f c[8] = {{l.x, l.y, l.z}, {h.x, l.y, l.z}, {h.x, h.y, l.z}, {l.x, h.y, l.z},
                          {l.x, l.y, h.z}, {h.x, l.y, h.z}, {h.x, h.y, h.z}, {l.x, h.y, h.z}};
      const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
      for (const auto& p : e) out.insert(out.end(), {c[p[0]], c[p[1]]});
      break;
    }
    case Shape::Circle: {
      constexpr int kSteps = 32;
      for (int i = 0; i < kSteps; ++i) {
        const float a0 = 6.2831853f * static_cast<float>(i) / kSteps, a1 = 6.2831853f * static_cast<float>(i + 1) / kSteps;
        out.push_back(s.a + Vec3f{std::cos(a0) * s.size, std::sin(a0) * s.size, 0.0f});
        out.push_back(s.a + Vec3f{std::cos(a1) * s.size, std::sin(a1) * s.size, 0.0f});
      }
      break;
    }
    case Shape::Cross: {
      const float k = s.size;
      out = {s.a - Vec3f{k, 0, 0}, s.a + Vec3f{k, 0, 0}, s.a - Vec3f{0, k, 0}, s.a + Vec3f{0, k, 0}, s.a - Vec3f{0, 0, k}, s.a + Vec3f{0, 0, k}};
      break;
    }
    case Shape::Text:
      break;
  }
  return out;
}

}  // namespace pioneer::debug

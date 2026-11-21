#pragma once
// Pioneer Engine - debug drawing: lines, boxes, circles, crosses and text in
// the world, from anywhere (tools, the console, a system being debugged).
//
//   pioneer::debug::DebugDraw::global().line(a, b, rgba8(255, 0, 0), 5.0f, "paths");
//
// Shapes are collected here, without graphics; the game projects them and
// draws them over its view each frame (ui/debug_view.hpp draws them with
// ImGui, so they work in any view and need nothing from the renderer).
// Positions are in world units (the game decides which: tiles in Legion,
// z up). A shape lasts the seconds given; 0 means it is drawn once, on the
// next frame, so code that runs every frame can simply draw every frame.
// Channels group shapes so they can be hidden or cleared together.

#include <cstdint>
#include <string>
#include <vector>

#include "pioneer/math/mat.hpp"

namespace pioneer::debug {

struct Shape {
  enum Kind : uint8_t { Line, Box, Circle, Cross, Text };
  Kind kind = Line;
  Vec3f a, b;             // line: the ends; box: the lowest and highest corners; others: `a` is where
  float size = 0.0f;      // circle: radius; cross: half its width
  uint32_t colour = 0xFFFFFFFFu;  // rgba8 (red in the lowest byte)
  float life = 0.0f;      // seconds left (0: this frame only)
  std::string text;       // Text
  std::string channel;
};

class DebugDraw {
 public:
  static DebugDraw& global();

  void line(Vec3f a, Vec3f b, uint32_t colour, float seconds = 0.0f, const std::string& channel = {});
  void box(Vec3f lo, Vec3f hi, uint32_t colour, float seconds = 0.0f, const std::string& channel = {});
  void circle(Vec3f centre, float radius, uint32_t colour, float seconds = 0.0f, const std::string& channel = {});  // flat, at centre.z
  void cross(Vec3f at, float size, uint32_t colour, float seconds = 0.0f, const std::string& channel = {});
  void text(Vec3f at, const std::string& words, uint32_t colour, float seconds = 0.0f, const std::string& channel = {});
  // An arrow from `from` along `dir` (as long as it is): a line and a head.
  void arrow(Vec3f from, Vec3f dir, uint32_t colour, float seconds = 0.0f, const std::string& channel = {});

  // After the frame is drawn: time passes; what has had its time goes.
  void endFrame(float dt);
  void clear(const std::string& channel = {});  // empty: everything

  // Off: calls are ignored (cheap), nothing is kept.
  void setEnabled(bool on);
  bool enabled() const { return enabled_; }
  void showChannel(const std::string& channel, bool shown);
  bool channelShown(const std::string& channel) const;

  const std::vector<Shape>& shapes() const { return shapes_; }
  // The segments of a shape that is made of lines (line, box, circle,
  // cross, arrow), in pairs of ends. Text gives none.
  static std::vector<Vec3f> segments(const Shape& s);

  static constexpr size_t kMaxShapes = 20000;  // beyond this, new shapes are dropped

 private:
  void add(Shape s);
  std::vector<Shape> shapes_;
  std::vector<std::string> hidden_;
  bool enabled_ = true;
};

}  // namespace pioneer::debug

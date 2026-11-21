// Debug drawing: shapes kept for their time, channels, the lines shapes are made of.
#include <doctest/doctest.h>

#include <cmath>

#include "pioneer/core/debug_draw.hpp"

using namespace pioneer;
using pioneer::debug::DebugDraw;
using pioneer::debug::Shape;

TEST_CASE("debug draw: shapes last their time; once means the next frame only") {
  DebugDraw d;
  d.line({0, 0, 0}, {1, 0, 0}, 0xFF0000FFu);             // once
  d.box({0, 0, 0}, {1, 1, 1}, 0xFF00FF00u, 1.0f);        // a second
  d.text({2, 2, 0}, "hello", 0xFFFFFFFFu, 0.5f, "labels");
  REQUIRE(d.shapes().size() == 3);
  d.endFrame(1.0f / 60.0f);  // the frame that drew them is over
  CHECK(d.shapes().size() == 2);
  d.endFrame(0.5f);
  REQUIRE(d.shapes().size() == 1);
  CHECK(d.shapes()[0].kind == Shape::Box);
  d.endFrame(0.5f);
  CHECK(d.shapes().empty());
}

TEST_CASE("debug draw: channels hide and clear together; switched off it keeps nothing") {
  DebugDraw d;
  d.cross({0, 0, 0}, 1.0f, 0xFFFFFFFFu, 10.0f, "paths");
  d.cross({1, 0, 0}, 1.0f, 0xFFFFFFFFu, 10.0f, "paths");
  d.circle({0, 0, 0}, 3.0f, 0xFFFFFFFFu, 10.0f, "vision");
  CHECK(d.channelShown("paths"));
  d.showChannel("paths", false);
  CHECK_FALSE(d.channelShown("paths"));
  CHECK(d.channelShown("vision"));
  d.showChannel("paths", true);
  CHECK(d.channelShown("paths"));
  d.clear("paths");
  REQUIRE(d.shapes().size() == 1);
  CHECK(d.shapes()[0].channel == "vision");
  d.setEnabled(false);
  CHECK(d.shapes().empty());
  d.line({0, 0, 0}, {1, 1, 1}, 0xFFFFFFFFu, 5.0f);
  CHECK(d.shapes().empty());
  d.setEnabled(true);
  d.line({0, 0, 0}, {1, 1, 1}, 0xFFFFFFFFu, 5.0f);
  CHECK(d.shapes().size() == 1);
  d.clear();
  CHECK(d.shapes().empty());
  // There is a ceiling, so a runaway loop can't eat the memory.
  for (size_t i = 0; i < DebugDraw::kMaxShapes + 50; ++i) d.cross({0, 0, 0}, 1.0f, 0xFFFFFFFFu, 1.0f);
  CHECK(d.shapes().size() == DebugDraw::kMaxShapes);
}

TEST_CASE("debug draw: the lines each shape is made of") {
  DebugDraw d;
  d.line({0, 0, 0}, {3, 0, 0}, 0xFFFFFFFFu, 1.0f);
  d.box({0, 0, 0}, {1, 2, 3}, 0xFFFFFFFFu, 1.0f);
  d.circle({5, 5, 1}, 2.0f, 0xFFFFFFFFu, 1.0f);
  d.cross({0, 0, 0}, 0.5f, 0xFFFFFFFFu, 1.0f);
  d.text({0, 0, 0}, "x", 0xFFFFFFFFu, 1.0f);
  d.arrow({0, 0, 0}, {0, 0, 4}, 0xFFFFFFFFu, 1.0f);  // straight up: the head still turns round something
  const auto& s = d.shapes();
  REQUIRE(s.size() == 6);
  CHECK(DebugDraw::segments(s[0]).size() == 2);
  const auto box = DebugDraw::segments(s[1]);
  CHECK(box.size() == 24);  // twelve edges
  for (const Vec3f& p : box) {
    CHECK((p.x == 0 || p.x == 1));
    CHECK((p.y == 0 || p.y == 2));
    CHECK((p.z == 0 || p.z == 3));
  }
  const auto ring = DebugDraw::segments(s[2]);
  CHECK(ring.size() == 64);
  for (const Vec3f& p : ring) {
    CHECK(std::hypot(p.x - 5.0f, p.y - 5.0f) == doctest::Approx(2.0f).epsilon(1e-4));
    CHECK(p.z == 1.0f);
  }
  CHECK(DebugDraw::segments(s[3]).size() == 6);
  CHECK(DebugDraw::segments(s[4]).empty());
  const auto arrow = DebugDraw::segments(s[5]);
  REQUIRE(arrow.size() == 6);  // the shaft and two lines of the head
  CHECK(arrow[1].z == 4.0f);
  for (size_t i = 2; i < arrow.size(); ++i) CHECK(std::isfinite(arrow[i].x));
  CHECK(arrow[3].z < 4.0f);  // the head runs back from the tip
}

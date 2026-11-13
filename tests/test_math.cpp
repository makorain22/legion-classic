#include <doctest/doctest.h>

#include <cmath>

#include "pioneer/core/fixed.hpp"
#include "pioneer/core/rng.hpp"
#include "pioneer/math/iso.hpp"

using namespace pioneer;

TEST_CASE("sine table matches std::sin within 1/65536") {
  for (int a = 0; a < ANGLE_FULL; a += 7) {
    const double expect = std::sin(a * 2.0 * 3.141592653589793 / ANGLE_FULL) * 65536.0;
    CHECK(std::abs(sinQ16(static_cast<Angle>(a)) - expect) <= 1.0);
    const double expectCos = std::cos(a * 2.0 * 3.141592653589793 / ANGLE_FULL) * 65536.0;
    CHECK(std::abs(cosQ16(static_cast<Angle>(a)) - expectCos) <= 1.0);
  }
}

TEST_CASE("atan2Angle is accurate to a few angle units in every direction") {
  for (int a = 0; a < ANGLE_FULL; a += 13) {
    const double rad = a * 2.0 * 3.141592653589793 / ANGLE_FULL;
    for (double len : {3.0, 250.0, 70000.0, 3.0e8}) {
      const int64_t x = static_cast<int64_t>(std::llround(std::cos(rad) * len));
      const int64_t y = static_cast<int64_t>(std::llround(std::sin(rad) * len));
      if (x == 0 && y == 0) continue;
      const double exact = std::atan2(static_cast<double>(y), static_cast<double>(x)) /
                           (2.0 * 3.141592653589793) * ANGLE_FULL;
      const int32_t got = atan2Angle(y, x);
      int32_t err = std::abs(got - static_cast<int32_t>(std::lround(exact < 0 ? exact + ANGLE_FULL : exact)));
      if (err > ANGLE_FULL / 2) err = ANGLE_FULL - err;
      // Tiny vectors are inherently coarse; long ones must be precise.
      CHECK(err <= (len < 10 ? 120 : 4));
    }
  }
}

TEST_CASE("facings: 16 directions, 256 angle units each") {
  CHECK(facingOf(0) == 0);
  CHECK(facingOf(127) == 0);
  CHECK(facingOf(128) == 1);
  CHECK(facingOf(4095) == 0);
  CHECK(facingOf(2048) == 8);
  CHECK(angleOfFacing(17) == 256);
  CHECK(angleDelta(4000, 100) == 196);
  CHECK(angleDelta(100, 4000) == -196);
}

TEST_CASE("integer square root is exact floor") {
  for (uint64_t v : {0ULL, 1ULL, 2ULL, 3ULL, 4ULL, 99ULL, 100ULL, 101ULL, 65535ULL,
                     4294967295ULL, 1000000000000ULL, 18446744073709551615ULL}) {
    const uint64_t r = isqrt64(v);
    CHECK(r * r <= v);
    if (r < 0xFFFFFFFFULL) CHECK((r + 1) * (r + 1) > v);  // (r+1)^2 would overflow otherwise
  }
  CHECK(distance(3, 4) == 5);
  CHECK(distance(-300, 400) == 500);
}

TEST_CASE("fixed-point arithmetic") {
  const Fixed a = Fixed::ratio(3, 2);
  CHECK(a.raw == 98304);
  CHECK((a * Fixed::fromInt(4)).toInt() == 6);
  CHECK((Fixed::fromInt(7) / Fixed::fromInt(2)).raw == Fixed::ratio(7, 2).raw);
  CHECK((-Fixed::ratio(1, 2)).toInt() == -1);  // floor
  CHECK(mulQ16(100, 32768) == 50);
}

TEST_CASE("RNG is deterministic and seed-dependent") {
  Rng a(42), b(42), c(43);
  for (int i = 0; i < 1000; ++i) {
    const uint32_t va = a.next();
    CHECK(va == b.next());
    (void)c.next();
  }
  CHECK(a.state() != c.state());
  Rng r(7);
  for (int i = 0; i < 10000; ++i) {
    const int32_t v = r.range(-5, 5);
    CHECK(v >= -5);
    CHECK(v <= 5);
  }
}

TEST_CASE("isometric projection round-trips") {
  for (float x : {0.f, 256.f, 1000.f, 77777.f})
    for (float y : {0.f, 512.f, 1234.f, 50000.f}) {
      const Vec2f s = iso::worldToScreen(x, y);
      const Vec2f w = iso::screenToWorld(s);
      CHECK(w.x == doctest::Approx(x).epsilon(1e-4));
      CHECK(w.y == doctest::Approx(y).epsilon(1e-4));
    }
  const Vec2f top = iso::tileTop(1, 0);
  CHECK(top.x == doctest::Approx(32.f));
  CHECK(top.y == doctest::Approx(16.f));
}

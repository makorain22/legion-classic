#include "pioneer/core/fixed.hpp"

namespace pioneer {

namespace {
constexpr int32_t kSinQuarter[1025] = {
#include "sin_table.inc"
};

// atan(2^-i) in angle units (4096 per circle), rounded. Used by CORDIC.
constexpr int32_t kAtanTable[12] = {512, 302, 160, 81, 41, 20, 10, 5, 3, 1, 1, 0};
}  // namespace

int32_t sinQ16(Angle a) {
  const int32_t i = a & ANGLE_MASK;
  if (i <= 1024) return kSinQuarter[i];
  if (i <= 2048) return kSinQuarter[2048 - i];
  if (i <= 3072) return -kSinQuarter[i - 2048];
  return -kSinQuarter[4096 - i];
}

int32_t cosQ16(Angle a) { return sinQ16(wrapAngle(a + 1024)); }

Angle atan2Angle(int64_t y, int64_t x) {
  if (x == 0 && y == 0) return 0;
  int32_t angle = 0;
  // Rotate into the right half-plane first (CORDIC converges for |angle| < 99 deg).
  if (x < 0) {
    x = -x;
    y = -y;
    angle = 2048;
  }
  // Scale up small vectors for precision, and down huge ones to avoid overflow.
  while ((x < (int64_t{1} << 20)) && (y < (int64_t{1} << 20)) && (y > -(int64_t{1} << 20))) {
    x <<= 1;
    y <<= 1;
  }
  while (x > (int64_t{1} << 40) || y > (int64_t{1} << 40) || y < -(int64_t{1} << 40)) {
    x >>= 1;
    y >>= 1;
  }
  for (int i = 0; i < 12; ++i) {
    const int64_t nx = (y > 0) ? x + (y >> i) : x - (y >> i);
    const int64_t ny = (y > 0) ? y - (x >> i) : y + (x >> i);
    angle += (y > 0) ? kAtanTable[i] : -kAtanTable[i];
    x = nx;
    y = ny;
  }
  return wrapAngle(angle);
}

uint32_t isqrt64(uint64_t v) {
  uint64_t res = 0;
  uint64_t bit = uint64_t{1} << 62;
  while (bit > v) bit >>= 2;
  while (bit != 0) {
    if (v >= res + bit) {
      v -= res + bit;
      res = (res >> 1) + bit;
    } else {
      res >>= 1;
    }
    bit >>= 2;
  }
  return static_cast<uint32_t>(res);
}

}  // namespace pioneer

#pragma once
// Slave Engine - deterministic integer maths.
//
// The simulation never uses floating point. Every PC must compute exactly the
// same result from the same commands, which is what makes replays, lockstep
// multiplayer and reproducible bug reports possible.

#include <compare>
#include <cstdint>

namespace slave {

// ---------------------------------------------------------------------------
// Distances: 1 tile = 256 "leptons" (the trick the classic Westwood games used).
// ---------------------------------------------------------------------------
constexpr int32_t LEPTONS_PER_TILE = 256;
constexpr int32_t LEPTON_SHIFT = 8;

// ---------------------------------------------------------------------------
// Angles: 4096 units per full circle.
//   0    = +X world axis (screen: down-right)
//   1024 = +Y world axis (screen: down-left)
// Units have 16 visible facings, so each facing covers 256 angle units.
// ---------------------------------------------------------------------------
using Angle = uint16_t;
constexpr int32_t ANGLE_FULL = 4096;
constexpr int32_t ANGLE_MASK = ANGLE_FULL - 1;
constexpr int32_t FACINGS = 16;
constexpr int32_t ANGLE_PER_FACING = ANGLE_FULL / FACINGS;  // 256

inline constexpr Angle wrapAngle(int32_t a) { return static_cast<Angle>(a & ANGLE_MASK); }

// Nearest of the 16 facings (0..15) for an angle.
inline constexpr int32_t facingOf(Angle a) {
  return ((a + ANGLE_PER_FACING / 2) >> 8) & (FACINGS - 1);
}
inline constexpr Angle angleOfFacing(int32_t f) { return wrapAngle(f * ANGLE_PER_FACING); }

// Signed shortest difference b - a, in -2048..2047.
inline constexpr int32_t angleDelta(Angle a, Angle b) {
  int32_t d = (static_cast<int32_t>(b) - static_cast<int32_t>(a)) & ANGLE_MASK;
  return d >= ANGLE_FULL / 2 ? d - ANGLE_FULL : d;
}

// Sine / cosine in Q16.16 (65536 = 1.0), from a fixed table.
int32_t sinQ16(Angle a);
int32_t cosQ16(Angle a);

// atan2 using integer CORDIC. Returns an Angle; (0,0) returns 0.
Angle atan2Angle(int64_t y, int64_t x);

// Integer square roots (floor).
uint32_t isqrt64(uint64_t v);

// Distance between two points in leptons (exact floor of the Euclidean length).
inline uint32_t distance(int32_t dx, int32_t dy) {
  return isqrt64(static_cast<uint64_t>(static_cast<int64_t>(dx) * dx) +
                 static_cast<uint64_t>(static_cast<int64_t>(dy) * dy));
}

// ---------------------------------------------------------------------------
// Q16.16 fixed-point number, for rates and ratios inside the simulation.
// ---------------------------------------------------------------------------
struct Fixed {
  int32_t raw = 0;

  static constexpr Fixed fromRaw(int32_t r) { Fixed f; f.raw = r; return f; }
  static constexpr Fixed fromInt(int32_t i) { return fromRaw(i * 65536); }
  // num / den as a fixed value, e.g. Fixed::ratio(3, 2) == 1.5
  static constexpr Fixed ratio(int32_t num, int32_t den) {
    return fromRaw(static_cast<int32_t>((static_cast<int64_t>(num) << 16) / den));
  }

  constexpr int32_t toInt() const { return raw >> 16; }  // floor
  constexpr int32_t round() const { return (raw + 32768) >> 16; }

  constexpr Fixed operator+(Fixed o) const { return fromRaw(raw + o.raw); }
  constexpr Fixed operator-(Fixed o) const { return fromRaw(raw - o.raw); }
  constexpr Fixed operator-() const { return fromRaw(-raw); }
  constexpr Fixed operator*(Fixed o) const {
    return fromRaw(static_cast<int32_t>((static_cast<int64_t>(raw) * o.raw) >> 16));
  }
  constexpr Fixed operator/(Fixed o) const {
    return fromRaw(static_cast<int32_t>((static_cast<int64_t>(raw) << 16) / o.raw));
  }
  constexpr Fixed operator*(int32_t i) const { return fromRaw(raw * i); }
  constexpr Fixed& operator+=(Fixed o) { raw += o.raw; return *this; }
  constexpr Fixed& operator-=(Fixed o) { raw -= o.raw; return *this; }
  constexpr bool operator==(const Fixed&) const = default;
  constexpr auto operator<=>(const Fixed&) const = default;
};

// Multiply an integer by a Q16 factor, rounding toward zero.
inline constexpr int32_t mulQ16(int32_t v, int32_t q16) {
  return static_cast<int32_t>((static_cast<int64_t>(v) * q16) / 65536);
}

}  // namespace slave

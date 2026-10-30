#pragma once
// Slave Engine - seeded deterministic random numbers (PCG32).
// The simulation only ever uses this; never std::rand or <random> engines,
// whose output differs between compilers.

#include <cstdint>

namespace slave {

class Rng {
 public:
  explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL) {
    reseed(seed, stream);
  }

  void reseed(uint64_t seed, uint64_t stream = 0xda3e39cb94b95bdbULL) {
    state_ = 0;
    inc_ = (stream << 1u) | 1u;
    next();
    state_ += seed;
    next();
  }

  uint32_t next() {
    const uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    const uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    const uint32_t rot = static_cast<uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
  }

  // Uniform in [0, n), without modulo bias. n must be > 0.
  uint32_t below(uint32_t n) {
    const uint32_t threshold = (0u - n) % n;
    for (;;) {
      const uint32_t r = next();
      if (r >= threshold) return r % n;
    }
  }

  // Uniform in [lo, hi] inclusive.
  int32_t range(int32_t lo, int32_t hi) {
    return lo + static_cast<int32_t>(below(static_cast<uint32_t>(hi - lo + 1)));
  }

  // True with probability num/den.
  bool chance(uint32_t num, uint32_t den) { return below(den) < num; }

  uint64_t state() const { return state_; }

 private:
  uint64_t state_ = 0;
  uint64_t inc_ = 0;
};

// Stateless hash, for things like picking a tile variant from its coordinates.
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}
inline uint32_t hash2(int32_t a, int32_t b, uint32_t seed = 0) {
  return hash32(static_cast<uint32_t>(a) * 73856093U ^ static_cast<uint32_t>(b) * 19349663U ^
                seed * 83492791U);
}

}  // namespace slave

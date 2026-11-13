#pragma once
// Pioneer Engine - spatial hash: "which units are near this point?" in a few
// lookups instead of scanning every unit. Rebuilt once per tick with a
// counting sort, so the order is deterministic.

#include <cstdint>
#include <vector>

#include "pioneer/core/fixed.hpp"

namespace pioneer {

class SpatialHash {
 public:
  static constexpr int32_t kCellShift = 9;  // 512 leptons = 2 tiles per cell

  void reset(int32_t worldW, int32_t worldH);
  // Two-pass build: count every item, then insert every item in the same order.
  void beginBuild();
  void count(int32_t x, int32_t y) { ++counts_[cellOf(x, y)]; }
  void endCount();
  void insert(uint32_t id, int32_t x, int32_t y);

  // Calls fn(id) for every item in the cells overlapping the square around (x, y).
  template <class Fn>
  void query(int32_t x, int32_t y, int32_t radius, Fn&& fn) const {
    const int cx0 = clampX((x - radius) >> kCellShift), cx1 = clampX((x + radius) >> kCellShift);
    const int cy0 = clampY((y - radius) >> kCellShift), cy1 = clampY((y + radius) >> kCellShift);
    for (int cy = cy0; cy <= cy1; ++cy)
      for (int cx = cx0; cx <= cx1; ++cx) {
        const size_t c = static_cast<size_t>(cy) * cellsX_ + cx;
        for (uint32_t i = start_[c]; i < start_[c + 1]; ++i) fn(items_[i]);
      }
  }

 private:
  int clampX(int c) const { return c < 0 ? 0 : (c >= cellsX_ ? cellsX_ - 1 : c); }
  int clampY(int c) const { return c < 0 ? 0 : (c >= cellsY_ ? cellsY_ - 1 : c); }
  size_t cellOf(int32_t x, int32_t y) const {
    return static_cast<size_t>(clampY(y >> kCellShift)) * cellsX_ + clampX(x >> kCellShift);
  }
  int cellsX_ = 1, cellsY_ = 1;
  std::vector<uint32_t> counts_, start_, fill_, items_;
};

}  // namespace pioneer

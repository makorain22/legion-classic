#include "pioneer/sim/spatial_hash.hpp"

#include <algorithm>

namespace pioneer {

void SpatialHash::reset(int32_t worldW, int32_t worldH) {
  cellsX_ = std::max(1, (worldW >> kCellShift) + 1);
  cellsY_ = std::max(1, (worldH >> kCellShift) + 1);
  const size_t n = static_cast<size_t>(cellsX_) * cellsY_;
  counts_.assign(n, 0);
  start_.assign(n + 1, 0);
  fill_.assign(n, 0);
  items_.clear();
}

void SpatialHash::beginBuild() { std::fill(counts_.begin(), counts_.end(), 0); }

void SpatialHash::endCount() {
  uint32_t total = 0;
  for (size_t c = 0; c < counts_.size(); ++c) {
    start_[c] = total;
    fill_[c] = total;
    total += counts_[c];
  }
  start_[counts_.size()] = total;
  items_.resize(total);
}

void SpatialHash::insert(uint32_t id, int32_t x, int32_t y) { items_[fill_[cellOf(x, y)]++] = id; }

}  // namespace pioneer

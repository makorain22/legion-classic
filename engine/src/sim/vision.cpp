#include "pioneer/sim/vision.hpp"

#include <algorithm>

namespace pioneer {

void Vision::resize(int w, int h) {
  w_ = w;
  h_ = h;
  for (auto& g : grid_) g.assign(static_cast<size_t>(w) * h, 0);
  active_.fill(false);
  ++revision_;
}

void Vision::begin() {
  for (int p = 0; p < kMaxPlayers; ++p) {
    if (!active_[static_cast<size_t>(p)]) continue;
    for (uint8_t& v : grid_[static_cast<size_t>(p)]) v &= static_cast<uint8_t>(~kVisible);
  }
}

const std::vector<int16_t>& Vision::circle(int r) {
  auto it = circles_.find(r);
  if (it != circles_.end()) return it->second;
  std::vector<int16_t> rows;
  for (int dy = -r; dy <= r; ++dy) {
    int half = 0;
    while ((half + 1) * (half + 1) + dy * dy <= r * r + r) ++half;  // integer circle
    rows.push_back(static_cast<int16_t>(dy));
    rows.push_back(static_cast<int16_t>(half));
  }
  return circles_.emplace(r, std::move(rows)).first->second;
}

void Vision::stamp(int player, int tx, int ty, int radiusTiles) {
  if (player < 0 || player >= kMaxPlayers || radiusTiles <= 0) return;
  active_[static_cast<size_t>(player)] = true;
  auto& g = grid_[static_cast<size_t>(player)];
  const auto& rows = circle(radiusTiles);
  for (size_t i = 0; i < rows.size(); i += 2) {
    const int y = ty + rows[i];
    if (y < 0 || y >= h_) continue;
    const int x0 = std::max(0, tx - rows[i + 1]), x1 = std::min(w_ - 1, tx + rows[i + 1]);
    uint8_t* row = &g[static_cast<size_t>(y) * w_];
    for (int x = x0; x <= x1; ++x) row[x] = kVisible | kExplored;
  }
}

}  // namespace pioneer

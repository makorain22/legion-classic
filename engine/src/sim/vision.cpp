#include "pioneer/sim/vision.hpp"

#include <algorithm>

namespace pioneer {

void Vision::resize(int w, int h) {
  w_ = w;
  h_ = h;
  for (auto& g : grid_) g.assign(static_cast<size_t>(w) * h, 0);
  ground_.assign(static_cast<size_t>(w) * h, 0);
  smoke_ = false;
  active_.fill(false);
  ++revision_;
}

void Vision::begin() {
  for (int p = 0; p < kMaxPlayers; ++p) {
    if (!active_[static_cast<size_t>(p)]) continue;
    for (uint8_t& v : grid_[static_cast<size_t>(p)]) v &= static_cast<uint8_t>(~(kVisible | kScouted));
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

void Vision::stamp(int player, int tx, int ty, int radiusTiles, uint8_t bits) {
  if (player < 0 || player >= kMaxPlayers || radiusTiles <= 0) return;
  active_[static_cast<size_t>(player)] = true;
  auto& g = grid_[static_cast<size_t>(player)];
  const auto& rows = circle(radiusTiles);
  for (size_t i = 0; i < rows.size(); i += 2) {
    const int y = ty + rows[i];
    if (y < 0 || y >= h_) continue;
    const int x0 = std::max(0, tx - rows[i + 1]), x1 = std::min(w_ - 1, tx + rows[i + 1]);
    uint8_t* row = &g[static_cast<size_t>(y) * w_];
    for (int x = x0; x <= x1; ++x) row[x] |= bits;
  }
}

// Rays to every tile on the rim of the circle. Along each the eye keeps the
// steepest upward slope it has looked along so far: ground that lies below
// that line is hidden behind the rise (a ridge hides the valley beyond it,
// not the next hill). A ray that enters smoke ends there.
void Vision::stampRays(int player, int tx, int ty, int radiusTiles, uint8_t bits, const int32_t* heights, int32_t eye) {
  if (player < 0 || player >= kMaxPlayers || radiusTiles <= 0 || !inBounds(tx, ty)) return;
  active_[static_cast<size_t>(player)] = true;
  auto& g = grid_[static_cast<size_t>(player)];
  g[index(tx, ty)] |= bits;
  const int32_t eyeZ = heights ? heights[index(tx, ty)] + eye : 0;
  const auto& rows = circle(radiusTiles);
  auto ray = [&](int ex, int ey) {
    const int dx = ex - tx, dy = ey - ty;
    const int steps = std::max(std::abs(dx), std::abs(dy));
    // Slopes are compared as fractions (rise / run) without dividing.
    int64_t topRise = -(int64_t{1} << 40), topRun = 1;
    for (int i = 1; i <= steps; ++i) {
      const int x = tx + (dx * i + (dx >= 0 ? steps / 2 : -steps / 2)) / steps;
      const int y = ty + (dy * i + (dy >= 0 ? steps / 2 : -steps / 2)) / steps;
      if (!inBounds(x, y)) return;
      const size_t at = index(x, y);
      if (heights) {
        const int64_t rise = heights[at] - eyeZ, run = i;
        // Seen if something a man's height above the ground there shows over
        // the line of the highest ground so far (so the flat top of a hill is
        // seen from below, the valley behind it is not).
        if ((rise + eye) * topRun >= topRise * run) g[at] |= bits;
        if (rise * topRun > topRise * run) {
          topRise = rise;
          topRun = run;
        }
      } else {
        g[at] |= bits;
      }
      if (ground_[at] & kSmoke) return;  // the edge of the smoke is seen; nothing in or behind it
    }
  };
  for (size_t i = 0; i < rows.size(); i += 2) {
    const int y = ty + rows[i], half = rows[i + 1];
    // The rim: both ends of this row, and the whole row where the circle closes.
    const int prevHalf = i >= 2 ? rows[i - 1] : -1, nextHalf = i + 3 < rows.size() ? rows[i + 3] : -1;
    const int inner = std::min(prevHalf, nextHalf);
    for (int x = -half; x <= half; ++x)
      if (x <= -inner || x >= inner || inner < 0) ray(tx + x, y);
  }
}

}  // namespace pioneer

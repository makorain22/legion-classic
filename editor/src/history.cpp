#include "history.hpp"

#include <algorithm>

using namespace slave;

namespace legion {

namespace {
// Corner range owned by chunk index c along an axis of `tiles` tiles.
void cornerRange(int c, int tiles, int& a, int& b) {
  a = c * History::kChunk;
  b = a + History::kChunk - 1;
  if (b + 1 == tiles) b = tiles;  // the last chunk also owns the map's far edge
}
}  // namespace

void History::begin(const std::string& label) {
  current_ = Step{};
  current_.label = label;
  recording_ = true;
}

void History::touchTerrain(const Map& map, int tx0, int ty0, int tx1, int ty1) {
  if (!recording_) return;
  const int cx0 = std::max(0, tx0 / kChunk), cy0 = std::max(0, ty0 / kChunk);
  const int cx1 = std::min(map.width() / kChunk - 1, tx1 / kChunk);
  const int cy1 = std::min(map.height() / kChunk - 1, ty1 / kChunk);
  for (int cy = cy0; cy <= cy1; ++cy)
    for (int cx = cx0; cx <= cx1; ++cx) {
      const bool seen = std::any_of(current_.chunks.begin(), current_.chunks.end(),
                                    [&](const ChunkCopy& c) { return c.cx == cx && c.cy == cy; });
      if (seen) continue;
      ChunkCopy copy{cx, cy, {}, {}};
      int ax, bx, ay, by;
      cornerRange(cx, map.width(), ax, bx);
      cornerRange(cy, map.height(), ay, by);
      for (int y = ay; y <= by; ++y)
        for (int x = ax; x <= bx; ++x) copy.heights.push_back(map.corner(x, y));
      const int s = kChunk * SPLAT_PER_TILE;
      for (int y = cy * s; y < (cy + 1) * s; ++y) {
        const uint8_t* row = map.splat(cx * s, y);
        copy.splat.insert(copy.splat.end(), row, row + s * LAYER_COUNT);
      }
      current_.chunks.push_back(std::move(copy));
    }
}

void History::touchProps(const Map& map) {
  if (!recording_ || current_.hasProps) return;
  current_.hasProps = true;
  current_.props = map.props();
}

void History::touchWater(const Map& map) {
  if (!recording_ || current_.hasWater) return;
  current_.hasWater = true;
  current_.water = map.waterLevel();
}

void History::end() {
  if (!recording_) return;
  recording_ = false;
  if (current_.chunks.empty() && !current_.hasProps && !current_.hasWater) return;
  undo_.push_back(std::move(current_));
  redo_.clear();
  if (undo_.size() > kMaxSteps) undo_.erase(undo_.begin());
}

void History::swapChunk(Map& map, ChunkCopy& c) {
  int ax, bx, ay, by;
  cornerRange(c.cx, map.width(), ax, bx);
  cornerRange(c.cy, map.height(), ay, by);
  size_t i = 0;
  for (int y = ay; y <= by; ++y)
    for (int x = ax; x <= bx; ++x) {
      const uint16_t cur = map.corner(x, y);
      map.setCorner(x, y, c.heights[i]);
      c.heights[i++] = cur;
    }
  const int s = kChunk * SPLAT_PER_TILE;
  size_t j = 0;
  for (int y = c.cy * s; y < (c.cy + 1) * s; ++y) {
    uint8_t* row = map.splat(c.cx * s, y);
    for (int k = 0; k < s * LAYER_COUNT; ++k) std::swap(row[k], c.splat[j++]);
  }
}

History::Region History::swapStep(Map& map, Step& s) {
  Region r;
  r.tx0 = map.width();
  r.ty0 = map.height();
  for (ChunkCopy& c : s.chunks) {
    swapChunk(map, c);
    r.tx0 = std::min(r.tx0, c.cx * kChunk);
    r.ty0 = std::min(r.ty0, c.cy * kChunk);
    r.tx1 = std::max(r.tx1, (c.cx + 1) * kChunk - 1);
    r.ty1 = std::max(r.ty1, (c.cy + 1) * kChunk - 1);
  }
  if (s.hasProps) {
    std::swap(map.props(), s.props);
    r.props = true;
  }
  if (s.hasWater) {
    const uint16_t cur = map.waterLevel();
    map.setWaterLevel(s.water);
    s.water = cur;
    r.water = true;
  }
  return r;
}

History::Region History::undo(Map& map) {
  if (undo_.empty()) return {};
  Step s = std::move(undo_.back());
  undo_.pop_back();
  const Region r = swapStep(map, s);
  redo_.push_back(std::move(s));
  return r;
}

History::Region History::redo(Map& map) {
  if (redo_.empty()) return {};
  Step s = std::move(redo_.back());
  redo_.pop_back();
  const Region r = swapStep(map, s);
  undo_.push_back(std::move(s));
  return r;
}

void History::clear() {
  undo_.clear();
  redo_.clear();
  recording_ = false;
}

size_t History::memoryBytes() const {
  size_t n = 0;
  for (const auto* stack : {&undo_, &redo_})
    for (const Step& s : *stack) {
      for (const ChunkCopy& c : s.chunks) n += c.heights.size() * 2 + c.splat.size();
      n += s.props.size() * sizeof(Prop);
    }
  return n;
}

}  // namespace legion

#include "slave/sim/pathfinder.hpp"

#include <algorithm>
#include <cstdlib>
#include <queue>

namespace slave {

namespace {
constexpr int kDX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
constexpr int kDY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
constexpr uint32_t kCost[8] = {10, 10, 10, 10, 14, 14, 14, 14};
constexpr uint32_t kEdgeCost = 5;  // extra for hugging an obstacle

uint32_t octile(int dx, int dy) {
  dx = std::abs(dx);
  dy = std::abs(dy);
  return 10u * static_cast<uint32_t>(std::max(dx, dy)) + 4u * static_cast<uint32_t>(std::min(dx, dy));
}

Vec2i tileCentre(int tx, int ty) { return {tx * LEPTONS_PER_TILE + 128, ty * LEPTONS_PER_TILE + 128}; }

struct OpenNode {
  uint32_t f, h, node;
  bool operator>(const OpenNode& o) const {
    if (f != o.f) return f > o.f;
    if (h != o.h) return h > o.h;
    return node > o.node;  // deterministic tie-break
  }
};
}  // namespace

void Pathfinder::refresh() {
  if (builtRevision_ == map_.revision() && builtW_ == map_.width() && builtH_ == map_.height()) return;
  buildRegions();
  builtRevision_ = map_.revision();
  builtW_ = map_.width();
  builtH_ = map_.height();
}

void Pathfinder::buildRegions() {
  const int W = map_.width(), H = map_.height();
  std::vector<uint32_t> queue;
  queue.reserve(static_cast<size_t>(W) * H / 4);
  for (int li = 0; li < 3; ++li) {
    const Locomotor loco = static_cast<Locomotor>(li);
    auto& reg = regions_[static_cast<size_t>(li)];
    reg.assign(static_cast<size_t>(W) * H, 0);
    uint32_t next = 1;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const size_t i = static_cast<size_t>(y) * W + x;
        if (reg[i] || !map_.passable(x, y, loco)) continue;
        // 4-connected flood fill (diagonal steps need both sides open anyway).
        reg[i] = next;
        queue.clear();
        queue.push_back(static_cast<uint32_t>(i));
        for (size_t q = 0; q < queue.size(); ++q) {
          const int cx = static_cast<int>(queue[q] % W), cy = static_cast<int>(queue[q] / W);
          for (int d = 0; d < 4; ++d) {
            const int nx = cx + kDX[d], ny = cy + kDY[d];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            const size_t ni = static_cast<size_t>(ny) * W + nx;
            if (reg[ni] || !map_.passable(nx, ny, loco)) continue;
            reg[ni] = next;
            queue.push_back(static_cast<uint32_t>(ni));
          }
        }
        ++next;
      }
    auto& edge = edge_[static_cast<size_t>(li)];
    edge.assign(static_cast<size_t>(W) * H, 0);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        if (!reg[static_cast<size_t>(y) * W + x]) continue;
        bool near = false;
        for (int d = 0; d < 8 && !near; ++d) near = !map_.passable(x + kDX[d], y + kDY[d], loco);
        edge[static_cast<size_t>(y) * W + x] = near ? 1 : 0;
      }
  }
}

uint32_t Pathfinder::region(int tx, int ty, Locomotor loco) const {
  if (!map_.inBounds(tx, ty)) return 0;
  const auto& reg = regions_[static_cast<size_t>(loco)];
  const size_t i = static_cast<size_t>(ty) * map_.width() + tx;
  return i < reg.size() ? reg[i] : 0;
}

bool Pathfinder::lineClear(Vec2i a, Vec2i b, Locomotor loco, int minSpeed) const {
  const int64_t dx = b.x - a.x, dy = b.y - a.y;
  const int64_t len = std::max(std::abs(dx), std::abs(dy));
  const int64_t steps = std::max<int64_t>(1, len / 64);
  int px = a.x >> LEPTON_SHIFT, py = a.y >> LEPTON_SHIFT;
  for (int64_t i = 1; i <= steps; ++i) {
    const int32_t x = static_cast<int32_t>(a.x + dx * i / steps);
    const int32_t y = static_cast<int32_t>(a.y + dy * i / steps);
    const int tx = x >> LEPTON_SHIFT, ty = y >> LEPTON_SHIFT;
    if (tx == px && ty == py) continue;
    if (!map_.passable(tx, ty, loco)) return false;
    if (minSpeed > 0 && map_.speedPercent(tx, ty, loco) < minSpeed) return false;
    // Moving diagonally between tiles: both side tiles must be open too.
    if (tx != px && ty != py && (!map_.passable(tx, py, loco) || !map_.passable(px, ty, loco))) return false;
    px = tx;
    py = ty;
  }
  return true;
}

Vec2i Pathfinder::nearestReachable(Vec2i from, Vec2i to, Locomotor loco) const {
  const int fx = from.x >> LEPTON_SHIFT, fy = from.y >> LEPTON_SHIFT;
  uint32_t want = region(fx, fy, loco);
  if (!want) {  // standing somewhere impassable: use the nearest open tile's region
    for (int r = 1; r <= 3 && !want; ++r)
      for (int dy = -r; dy <= r && !want; ++dy)
        for (int dx = -r; dx <= r && !want; ++dx) want = region(fx + dx, fy + dy, loco);
    if (!want) return from;
  }
  const int tx = std::clamp(to.x >> LEPTON_SHIFT, 0, map_.width() - 1);
  const int ty = std::clamp(to.y >> LEPTON_SHIFT, 0, map_.height() - 1);
  if (region(tx, ty, loco) == want) return {std::clamp(to.x, 0, map_.widthLeptons() - 1),
                                            std::clamp(to.y, 0, map_.heightLeptons() - 1)};
  // Spiral outwards; in each ring take the tile nearest the target.
  for (int r = 1; r <= 48; ++r) {
    int bestD = INT32_MAX, bx = 0, by = 0;
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx) {
        if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
        if (region(tx + dx, ty + dy, loco) != want) continue;
        const int d = dx * dx + dy * dy;
        if (d < bestD) {
          bestD = d;
          bx = tx + dx;
          by = ty + dy;
        }
      }
    if (bestD != INT32_MAX) return tileCentre(bx, by);
  }
  return from;  // nothing reachable nearby: stay put
}

bool Pathfinder::findPath(Vec2i from, Vec2i to, Locomotor loco, std::vector<Vec2i>& out, int maxExpansions) {
  refresh();
  ++searches_;
  out.clear();
  lastExpansions_ = 0;
  const int W = map_.width(), H = map_.height();
  const Vec2i goal = nearestReachable(from, to, loco);
  const int sx = std::clamp(from.x >> LEPTON_SHIFT, 0, W - 1), sy = std::clamp(from.y >> LEPTON_SHIFT, 0, H - 1);
  const int gx = goal.x >> LEPTON_SHIFT, gy = goal.y >> LEPTON_SHIFT;
  if (goal == from) return false;
  // Straight there when nothing is in the way and the line crosses no ground
  // slower than where it starts and ends (else A* weighs going round).
  const int ends = std::min(map_.speedPercent(sx, sy, loco), map_.speedPercent(gx, gy, loco));
  if ((sx == gx && sy == gy) || lineClear(from, goal, loco, ends)) {
    out.push_back(goal);
    return true;
  }

  const size_t n = static_cast<size_t>(W) * H;
  if (gScore_.size() != n) {
    gScore_.assign(n, 0);
    stamp_.assign(n, 0);
    parent_.assign(n, 0);
    closed_.assign(n, 0);
    generation_ = 0;
  }
  if (++generation_ == 0) {  // wrapped: reset stamps
    std::fill(stamp_.begin(), stamp_.end(), 0);
    generation_ = 1;
  }
  auto visit = [&](size_t i) {
    if (stamp_[i] != generation_) {
      stamp_[i] = generation_;
      gScore_[i] = UINT32_MAX;
      closed_[i] = 0;
    }
  };

  std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;
  const uint32_t start = static_cast<uint32_t>(sy * W + sx), target = static_cast<uint32_t>(gy * W + gx);
  visit(start);
  gScore_[start] = 0;
  parent_[start] = start;
  open.push({octile(gx - sx, gy - sy), octile(gx - sx, gy - sy), start});
  uint32_t best = start, bestH = octile(gx - sx, gy - sy);
  bool reached = false;

  while (!open.empty()) {
    const OpenNode cur = open.top();
    open.pop();
    if (closed_[cur.node]) continue;
    closed_[cur.node] = 1;
    if (cur.node == target) {
      reached = true;
      break;
    }
    if (cur.h < bestH) {
      bestH = cur.h;
      best = cur.node;
    }
    if (static_cast<int>(++lastExpansions_) > maxExpansions) break;
    const int cx = static_cast<int>(cur.node % W), cy = static_cast<int>(cur.node / W);
    for (int d = 0; d < 8; ++d) {
      const int nx = cx + kDX[d], ny = cy + kDY[d];
      if (nx < 0 || ny < 0 || nx >= W || ny >= H || !map_.passable(nx, ny, loco)) continue;
      if (d >= 4 && (!map_.passable(nx, cy, loco) || !map_.passable(cx, ny, loco))) continue;
      const size_t ni = static_cast<size_t>(ny) * W + nx;
      visit(ni);
      if (closed_[ni]) continue;
      // Slow ground costs more: the step's cost over the speed there.
      const uint32_t step = kCost[d] * 100u / static_cast<uint32_t>(map_.speedPercent(nx, ny, loco));
      const uint32_t g = gScore_[cur.node] + step + (edge_[static_cast<size_t>(loco)][ni] ? kEdgeCost : 0);
      if (g >= gScore_[ni]) continue;
      gScore_[ni] = g;
      parent_[ni] = cur.node;
      const uint32_t h = octile(gx - nx, gy - ny);
      open.push({g + h, h, static_cast<uint32_t>(ni)});
    }
  }

  const uint32_t end = reached ? target : best;
  if (end == start) return false;
  std::vector<Vec2i> tiles;
  for (uint32_t i = end; i != start; i = parent_[i])
    tiles.push_back(tileCentre(static_cast<int>(i % W), static_cast<int>(i / W)));
  std::reverse(tiles.begin(), tiles.end());
  if (reached) tiles.back() = goal;
  smooth(from, tiles, loco);
  out = std::move(tiles);
  return true;
}

void Pathfinder::smooth(Vec2i from, std::vector<Vec2i>& path, Locomotor loco) const {
  std::vector<Vec2i> out;
  Vec2i cur = from;
  size_t i = 0;
  while (i < path.size()) {
    size_t pick = i;
    const size_t limit = std::min(path.size() - 1, i + 32);
    // A shortcut may not cross slower ground than the path it replaces (else
    // it would cut back through the mud the path went round).
    int slowest[33];
    int low = map_.speedPercent(cur.x >> LEPTON_SHIFT, cur.y >> LEPTON_SHIFT, loco);
    for (size_t j = i; j <= limit; ++j) {
      low = std::min(low, map_.speedPercent(path[j].x >> LEPTON_SHIFT, path[j].y >> LEPTON_SHIFT, loco));
      slowest[j - i] = low;
    }
    for (size_t j = limit; j > i; --j)
      if (lineClear(cur, path[j], loco, slowest[j - i])) {
        pick = j;
        break;
      }
    out.push_back(path[pick]);
    cur = path[pick];
    i = pick + 1;
  }
  path = std::move(out);
}

}  // namespace slave

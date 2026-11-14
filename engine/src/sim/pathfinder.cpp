#include "pioneer/sim/pathfinder.hpp"

#include <algorithm>
#include <cstdlib>
#include <queue>

namespace pioneer {

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

// ---------------------------------------------------------------------------
// Sectors
// ---------------------------------------------------------------------------
namespace {
constexpr int kS = Pathfinder::kSectorSize;
constexpr uint32_t kPartsPerSector = 256;  // part id = sector * 256 + part (1..)
constexpr size_t kCacheLimit = 4096;
constexpr size_t kFlowFields = 8;
constexpr size_t kGuides = 6;
// The sector estimate runs through the middles of parts, so it is rough by about a sector: it only takes
// over from the straight-line estimate when it says the way is this much longer (an obstacle to go round).
constexpr uint32_t kGuideSlack = 10 * 8;
}  // namespace

uint32_t Pathfinder::partAt(int tx, int ty, Locomotor loco) const {
  if (!map_.inBounds(tx, ty)) return 0;
  const auto& l = local_[static_cast<size_t>(loco)];
  const size_t i = static_cast<size_t>(ty) * map_.width() + tx;
  if (i >= l.size() || !l[i]) return 0;
  return static_cast<uint32_t>(sectorOf(tx, ty)) * kPartsPerSector + l[i];
}

uint64_t Pathfinder::sectorHash(int sx, int sy) const {
  const int W = map_.width(), H = map_.height();
  uint64_t h = 1469598103934665603ULL;
  for (int y = sy * kS; y < std::min(H, (sy + 1) * kS); ++y)
    for (int x = sx * kS; x < std::min(W, (sx + 1) * kS); ++x) {
      const uint64_t bits = (map_.passable(x, y, Locomotor::Foot) ? 1u : 0u) | (map_.passable(x, y, Locomotor::Wheeled) ? 2u : 0u) |
                            (map_.passable(x, y, Locomotor::Tracked) ? 4u : 0u);
      h = (h ^ bits) * 1099511628211ULL;
    }
  return h | 1;  // never 0: that means "not built"
}

// Labels the connected parts of one sector (4-connected, like the regions).
void Pathfinder::buildSector(int sx, int sy) {
  const int W = map_.width(), H = map_.height();
  const int x0 = sx * kS, y0 = sy * kS, x1 = std::min(W, x0 + kS), y1 = std::min(H, y0 + kS);
  Sector& sec = sectors_[static_cast<size_t>(sy) * sw_ + sx];
  uint32_t queue[kS * kS];
  for (int li = 0; li < 3; ++li) {
    const Locomotor loco = static_cast<Locomotor>(li);
    auto& local = local_[static_cast<size_t>(li)];
    auto& parts = sec.parts[static_cast<size_t>(li)];
    parts.clear();
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) local[static_cast<size_t>(y) * W + x] = 0;
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        const size_t i = static_cast<size_t>(y) * W + x;
        if (local[i] || !map_.passable(x, y, loco)) continue;
        parts.emplace_back();
        const uint8_t label = static_cast<uint8_t>(parts.size());
        Part& part = parts.back();
        size_t head = 0, tail = 0;
        queue[tail++] = static_cast<uint32_t>(i);
        local[i] = label;
        while (head < tail) {
          const int cx = static_cast<int>(queue[head] % W), cy = static_cast<int>(queue[head] / W);
          ++head;
          part.sumX += cx;
          part.sumY += cy;
          ++part.tiles;
          part.costSum += 10000u / static_cast<uint32_t>(std::max(1, map_.speedPercent(cx, cy, loco)));
          for (int d = 0; d < 4; ++d) {
            const int nx = cx + kDX[d], ny = cy + kDY[d];
            if (nx < x0 || ny < y0 || nx >= x1 || ny >= y1) continue;
            const size_t ni = static_cast<size_t>(ny) * W + nx;
            if (local[ni] || !map_.passable(nx, ny, loco)) continue;
            local[ni] = label;
            queue[tail++] = static_cast<uint32_t>(ni);
          }
        }
      }
  }
  sec.built = true;
  ++stats_.sectorRebuilds;
}

// Which parts of the four neighbouring sectors each part of this one touches.
void Pathfinder::linkSector(int sx, int sy) {
  const int W = map_.width(), H = map_.height();
  const int x0 = sx * kS, y0 = sy * kS, x1 = std::min(W, x0 + kS), y1 = std::min(H, y0 + kS);
  Sector& sec = sectors_[static_cast<size_t>(sy) * sw_ + sx];
  for (int li = 0; li < 3; ++li) {
    const Locomotor loco = static_cast<Locomotor>(li);
    const auto& local = local_[static_cast<size_t>(li)];
    auto& links = sec.links[static_cast<size_t>(li)];
    links.clear();
    auto touch = [&](int x, int y, int nx, int ny) {
      if (nx < 0 || ny < 0 || nx >= W || ny >= H) return;
      const uint8_t from = local[static_cast<size_t>(y) * W + x];
      if (!from) return;
      const uint32_t to = partAt(nx, ny, loco);
      if (!to) return;
      for (const Link& l : links)
        if (l.from == from && l.to == to) return;
      links.push_back({from, to});
    };
    for (int x = x0; x < x1; ++x) {
      touch(x, y0, x, y0 - 1);
      touch(x, y1 - 1, x, y1);
    }
    for (int y = y0; y < y1; ++y) {
      touch(x0, y, x0 - 1, y);
      touch(x1 - 1, y, x1, y);
    }
    // Across a corner, where a unit may step diagonally (both side tiles open).
    auto corner = [&](int x, int y, int dx, int dy) {
      if (map_.passable(x + dx, y, loco) && map_.passable(x, y + dy, loco)) touch(x, y, x + dx, y + dy);
    };
    corner(x0, y0, -1, -1);
    corner(x1 - 1, y0, 1, -1);
    corner(x0, y1 - 1, -1, 1);
    corner(x1 - 1, y1 - 1, 1, 1);
  }
}

void Pathfinder::markEdges(int sx, int sy) {
  const int W = map_.width(), H = map_.height();
  const int x0 = sx * kS, y0 = sy * kS, x1 = std::min(W, x0 + kS), y1 = std::min(H, y0 + kS);
  for (int li = 0; li < 3; ++li) {
    const Locomotor loco = static_cast<Locomotor>(li);
    const auto& local = local_[static_cast<size_t>(li)];
    auto& edge = edge_[static_cast<size_t>(li)];
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) {
        const size_t i = static_cast<size_t>(y) * W + x;
        bool near = false;
        if (local[i])
          for (int d = 0; d < 8 && !near; ++d) near = !map_.passable(x + kDX[d], y + kDY[d], loco);
        edge[i] = near ? 1 : 0;
      }
  }
}

// Regions: parts joined through their links (union-find), one label per set.
void Pathfinder::buildRegions() {
  const size_t n = sectors_.size() * kPartsPerSector;
  std::vector<uint32_t> parent(n);
  stats_.parts = stats_.links = 0;
  for (int li = 0; li < 3; ++li) {
    for (uint32_t i = 0; i < n; ++i) parent[i] = i;
    auto find = [&](uint32_t v) {
      while (parent[v] != v) {
        parent[v] = parent[parent[v]];
        v = parent[v];
      }
      return v;
    };
    for (size_t s = 0; s < sectors_.size(); ++s) {
      stats_.parts += static_cast<uint32_t>(sectors_[s].parts[static_cast<size_t>(li)].size());
      for (const Link& l : sectors_[s].links[static_cast<size_t>(li)]) {
        ++stats_.links;
        const uint32_t a = find(static_cast<uint32_t>(s) * kPartsPerSector + l.from), b = find(l.to);
        if (a != b) parent[std::max(a, b)] = std::min(a, b);  // the lower id leads: the same labels on every machine
      }
    }
    auto& reg = regionOf_[static_cast<size_t>(li)];
    reg.assign(n, 0);
    for (size_t s = 0; s < sectors_.size(); ++s)
      for (size_t p = 1; p <= sectors_[s].parts[static_cast<size_t>(li)].size(); ++p) {
        const uint32_t id = static_cast<uint32_t>(s * kPartsPerSector + p);
        reg[id] = find(id) + 1;
      }
  }
}

void Pathfinder::refresh() {
  const int W = map_.width(), H = map_.height();
  if (builtRevision_ == map_.revision() && builtW_ == W && builtH_ == H) return;
  ++stats_.refreshes;
  if (builtW_ != W || builtH_ != H) {
    sw_ = (W + kS - 1) / kS;
    sh_ = (H + kS - 1) / kS;
    sectors_.assign(static_cast<size_t>(sw_) * sh_, Sector{});
    for (int li = 0; li < 3; ++li) {
      local_[static_cast<size_t>(li)].assign(static_cast<size_t>(W) * H, 0);
      edge_[static_cast<size_t>(li)].assign(static_cast<size_t>(W) * H, 0);
    }
    stats_.sectors = static_cast<uint32_t>(sectors_.size());
  }
  // Only sectors whose ground changed are worked out again.
  std::vector<uint8_t> dirty(sectors_.size(), 0);
  bool any = false;
  for (int sy = 0; sy < sh_; ++sy)
    for (int sx = 0; sx < sw_; ++sx) {
      Sector& sec = sectors_[static_cast<size_t>(sy) * sw_ + sx];
      const uint64_t h = sectorHash(sx, sy);
      if (sec.built && sec.hash == h) continue;
      sec.hash = h;
      dirty[static_cast<size_t>(sy) * sw_ + sx] = 1;
      any = true;
    }
  if (any) {
    for (int sy = 0; sy < sh_; ++sy)
      for (int sx = 0; sx < sw_; ++sx)
        if (dirty[static_cast<size_t>(sy) * sw_ + sx]) buildSector(sx, sy);
    for (int sy = 0; sy < sh_; ++sy)
      for (int sx = 0; sx < sw_; ++sx) {
        bool link = false, edges = false;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = sx + dx, ny = sy + dy;
            if (nx < 0 || ny < 0 || nx >= sw_ || ny >= sh_ || !dirty[static_cast<size_t>(ny) * sw_ + nx]) continue;
            edges = true;
            if (dx == 0 || dy == 0) link = true;
          }
        if (link) linkSector(sx, sy);
        if (edges) markEdges(sx, sy);
      }
    buildRegions();
  }
  // Speeds may have changed even where nothing became blocked: routes are looked for afresh.
  cache_.clear();
  fields_.clear();
  guides_.clear();
  builtRevision_ = map_.revision();
  builtW_ = W;
  builtH_ = H;
}

uint32_t Pathfinder::region(int tx, int ty, Locomotor loco) const {
  const uint32_t part = partAt(tx, ty, loco);
  if (!part) return 0;
  const auto& reg = regionOf_[static_cast<size_t>(loco)];
  return part < reg.size() ? reg[part] : 0;
}

// The way to one sector part from every other, over the graph of parts: how
// far each is from it (through the middles of the parts between) and which
// neighbour comes next. The tile search uses it to know which way round an
// obstacle the goal lies. One field serves every unit bound for that part.
const Pathfinder::Guide& Pathfinder::guideTo(uint32_t goalPart, Locomotor loco) {
  for (size_t i = 0; i < guides_.size(); ++i)
    if (guides_[i]->goalPart == goalPart && guides_[i]->loco == loco) {
      std::rotate(guides_.begin(), guides_.begin() + static_cast<long>(i), guides_.begin() + static_cast<long>(i) + 1);
      return *guides_.front();
    }
  const size_t li = static_cast<size_t>(loco);
  auto guide = std::make_unique<Guide>();
  Guide& g = *guide;
  g.goalPart = goalPart;
  g.loco = loco;
  const size_t n = sectors_.size() * kPartsPerSector;
  g.dist.assign(n, UINT32_MAX);
  g.next.assign(n, 0);
  std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;
  g.dist[goalPart] = 0;
  g.next[goalPart] = goalPart;
  open.push({0, 0, goalPart});
  while (!open.empty()) {
    const OpenNode cur = open.top();
    open.pop();
    if (cur.f > g.dist[cur.node]) continue;
    ++g.settled;
    const Vec2i c = partCentre(cur.node, loco);
    for (const Link& l : sectors_[cur.node / kPartsPerSector].links[li]) {
      if (l.from != cur.node % kPartsPerSector) continue;
      const Vec2i tc = partCentre(l.to, loco);
      const uint32_t d = cur.f + std::max<uint32_t>(1, octile(tc.x - c.x, tc.y - c.y));
      if (d >= g.dist[l.to]) continue;
      g.dist[l.to] = d;
      g.next[l.to] = cur.node;
      open.push({d, 0, l.to});
    }
  }
  guides_.insert(guides_.begin(), std::move(guide));
  if (guides_.size() > kGuides) guides_.pop_back();
  return *guides_.front();
}

Vec2i Pathfinder::partCentre(uint32_t part, Locomotor loco) const {
  const Part& p = sectors_[part / kPartsPerSector].parts[static_cast<size_t>(loco)][part % kPartsPerSector - 1];
  return {p.sumX / static_cast<int32_t>(p.tiles), p.sumY / static_cast<int32_t>(p.tiles)};
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
    ++stats_.straight;
    out.push_back(goal);
    return true;
  }
  ++stats_.searches;

  // Asked before? The answer, and what it cost then.
  const CacheKey key{static_cast<uint32_t>(sy * W + sx), goal, static_cast<uint8_t>(loco)};
  if (useCache_) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      ++stats_.cacheHits;
      lastExpansions_ = it->second.expansions;
      if (!it->second.found) return false;
      out = it->second.tiles;
      smooth(from, out, loco);
      return true;
    }
  }

  std::vector<Vec2i> tiles;
  bool reached = false, found = false;
  // A long way: the sector graph tells the tile search which way round the obstacles the goal lies.
  const Guide* guide = nullptr;
  if (useCorridors_ && std::max(std::abs(gx - sx), std::abs(gy - sy)) > kCorridorFrom) {
    const uint32_t a = partAt(sx, sy, loco), b = partAt(gx, gy, loco);
    if (a && b) {
      guide = &guideTo(b, loco);
      lastExpansions_ += guide->settled;  // the same whether the guide was at hand or made now
      ++stats_.corridors;
    }
  }
  found = search(sx, sy, gx, gy, loco, guide, maxExpansions, tiles, reached);
  if (found && reached) tiles.back() = goal;
  if (useCache_) {
    if (cache_.size() >= kCacheLimit) cache_.clear();
    cache_[key] = CacheEntry{tiles, lastExpansions_, found};
  }
  if (!found) return false;
  smooth(from, tiles, loco);
  out = std::move(tiles);
  return true;
}

// A* over the tiles from (sx, sy) to (gx, gy); inside the planned corridor
// only, if asked. `tiles` receives the tile centres of the way (unsmoothed),
// to the goal or to the nearest tile found.
bool Pathfinder::search(int sx, int sy, int gx, int gy, Locomotor loco, const Guide* guide, int maxExpansions,
                        std::vector<Vec2i>& tiles, bool& reached) {
  const int W = map_.width(), H = map_.height();
  tiles.clear();
  reached = false;
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

  // How far it still is from a tile: straight there, or, where the sector
  // graph knows of a longer way round, to the middle of the next part and on
  // from there.
  auto estimate = [&](int x, int y) {
    const uint32_t direct = octile(gx - x, gy - y);
    if (!guide) return direct;
    const uint32_t part = partAt(x, y, loco);
    if (!part || part == guide->goalPart || guide->dist[part] == UINT32_MAX) return direct;
    const uint32_t next = guide->next[part];
    if (next == guide->goalPart) return direct;
    const Vec2i c = partCentre(next, loco);
    const uint32_t round = octile(c.x - x, c.y - y) + guide->dist[next];
    return round > direct + kGuideSlack ? round - kGuideSlack : direct;
  };
  std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;
  const uint32_t start = static_cast<uint32_t>(sy * W + sx), target = static_cast<uint32_t>(gy * W + gx);
  visit(start);
  gScore_[start] = 0;
  parent_[start] = start;
  open.push({estimate(sx, sy), estimate(sx, sy), start});
  uint32_t best = start, bestH = estimate(sx, sy);
  uint64_t expansions = 0;

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
    ++lastExpansions_;
    if (static_cast<int>(++expansions) > maxExpansions) break;
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
      const uint32_t h = estimate(nx, ny);
      open.push({g + h, h, static_cast<uint32_t>(ni)});
    }
  }

  const uint32_t end = reached ? target : best;
  if (end == start) return false;
  for (uint32_t i = end; i != start; i = parent_[i])
    tiles.push_back(tileCentre(static_cast<int>(i % W), static_cast<int>(i / W)));
  std::reverse(tiles.begin(), tiles.end());
  return true;
}

// ---------------------------------------------------------------------------
// Flow fields
// ---------------------------------------------------------------------------
bool FlowField::next(int tx, int ty, int& nx, int& ny) const {
  const uint8_t d = dirAt(tx, ty);
  if (d >= 8) return false;
  nx = tx + kDX[d];
  ny = ty + kDY[d];
  return true;
}

const FlowField* Pathfinder::flowField(Vec2i goal, Locomotor loco, int x0, int y0, int x1, int y1) {
  refresh();
  const int W = map_.width(), H = map_.height();
  if (x1 < x0 || y1 < y0) {
    x0 = y0 = 0;
    x1 = W - 1;
    y1 = H - 1;
  }
  x0 = std::clamp(x0, 0, W - 1);
  y0 = std::clamp(y0, 0, H - 1);
  x1 = std::clamp(x1, x0, W - 1);
  y1 = std::clamp(y1, y0, H - 1);
  const int gx = std::clamp(goal.x >> LEPTON_SHIFT, x0, x1), gy = std::clamp(goal.y >> LEPTON_SHIFT, y0, y1);
  for (size_t i = 0; i < fields_.size(); ++i) {
    FlowField& f = *fields_[i];
    if (f.loco != loco || f.x0 != x0 || f.y0 != y0 || f.w != x1 - x0 + 1 || f.h != y1 - y0 + 1) continue;
    if ((f.goal.x >> LEPTON_SHIFT) != gx || (f.goal.y >> LEPTON_SHIFT) != gy) continue;
    std::rotate(fields_.begin(), fields_.begin() + static_cast<long>(i), fields_.begin() + static_cast<long>(i) + 1);
    return fields_.front().get();
  }
  auto field = std::make_unique<FlowField>();
  FlowField& f = *field;
  f.goal = tileCentre(gx, gy);
  f.loco = loco;
  f.x0 = x0;
  f.y0 = y0;
  f.w = x1 - x0 + 1;
  f.h = y1 - y0 + 1;
  f.revision = map_.revision();
  const size_t n = static_cast<size_t>(f.w) * f.h;
  f.dir.assign(n, FlowField::kNone);
  f.cost.assign(n, UINT32_MAX);
  ++stats_.flowFields;
  if (map_.passable(gx, gy, loco)) {
    // Dijkstra outwards from the goal: a tile's cost is the cost of walking
    // from it to the goal, so the step back along the search is its way.
    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;
    const uint32_t gi = static_cast<uint32_t>((gy - y0) * f.w + (gx - x0));
    f.cost[gi] = 0;
    f.dir[gi] = FlowField::kGoal;
    open.push({0, 0, gi});
    while (!open.empty()) {
      const OpenNode cur = open.top();
      open.pop();
      if (cur.f > f.cost[cur.node]) continue;
      const int cx = x0 + static_cast<int>(cur.node % static_cast<uint32_t>(f.w)), cy = y0 + static_cast<int>(cur.node / static_cast<uint32_t>(f.w));
      // Leaving (cx, cy) towards the goal costs what entering it would on the way out.
      const uint32_t speed = static_cast<uint32_t>(std::max(1, map_.speedPercent(cx, cy, loco)));
      const uint32_t edge = edge_[static_cast<size_t>(loco)][static_cast<size_t>(cy) * W + cx] ? kEdgeCost : 0;
      for (int d = 0; d < 8; ++d) {
        const int nx = cx + kDX[d], ny = cy + kDY[d];
        if (nx < x0 || ny < y0 || nx > x1 || ny > y1 || !map_.passable(nx, ny, loco)) continue;
        if (d >= 4 && (!map_.passable(nx, cy, loco) || !map_.passable(cx, ny, loco))) continue;
        const uint32_t ni = static_cast<uint32_t>((ny - y0) * f.w + (nx - x0));
        const uint32_t g = cur.f + kCost[d] * 100u / speed + edge;
        if (g >= f.cost[ni]) continue;
        f.cost[ni] = g;
        // From the neighbour, the step is the opposite of d.
        static constexpr uint8_t kBack[8] = {1, 0, 3, 2, 7, 6, 5, 4};
        f.dir[ni] = kBack[d];
        open.push({g, 0, ni});
      }
    }
  }
  fields_.insert(fields_.begin(), std::move(field));
  if (fields_.size() > kFlowFields) fields_.pop_back();
  return fields_.front().get();
}

bool Pathfinder::flowPath(const FlowField& field, Vec2i from, std::vector<Vec2i>& out) const {
  out.clear();
  int tx = from.x >> LEPTON_SHIFT, ty = from.y >> LEPTON_SHIFT;
  if (field.dirAt(tx, ty) == FlowField::kNone) return false;
  const size_t limit = static_cast<size_t>(field.w) * field.h;
  int nx = 0, ny = 0;
  while (field.next(tx, ty, nx, ny) && out.size() < limit) {
    tx = nx;
    ty = ny;
    out.push_back(tileCentre(tx, ty));
  }
  if (out.empty()) out.push_back(field.goal);
  smooth(from, out, field.loco);
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

}  // namespace pioneer

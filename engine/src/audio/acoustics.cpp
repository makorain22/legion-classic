// Pioneer Engine - acoustics. See acoustics.hpp.
#include "pioneer/audio/acoustics.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace pioneer::audio {

const char* spaceName(Space s) {
  switch (s) {
    case Space::Open: return "open";
    case Space::Forest: return "forest";
    case Space::Town: return "town";
    case Space::Valley: return "valley";
    default: return "?";
  }
}

bool parseSpace(const std::string& s, Space& out) {
  for (int i = 0; i < kSpaceCount; ++i)
    if (s == spaceName(static_cast<Space>(i))) {
      out = static_cast<Space>(i);
      return true;
    }
  return false;
}

namespace {
std::array<SpaceSound, kSpaceCount> makeSpaces() {
  std::array<SpaceSound, kSpaceCount> sp{};
  // Open ground: the big soft hall (the mixer's only reverb before part 7).
  sp[0] = SpaceSound{};
  // Woods: a short, dense, dark tail; trunks scatter the sound, leaves eat the top.
  sp[1].feedback = 0.84f;
  sp[1].damp = 0.62f;
  sp[1].preDelay = 0.012f;
  sp[1].level = 1.15f;
  // Streets: a hard, bright, shorter tail and slapback off the house fronts.
  sp[2].feedback = 0.86f;
  sp[2].damp = 0.12f;
  sp[2].preDelay = 0.008f;
  sp[2].level = 0.9f;
  sp[2].echoes[0] = {0.055f, 0.34f};
  sp[2].echoes[1] = {0.083f, 0.27f};
  sp[2].echoes[2] = {0.131f, 0.18f};
  sp[2].echoCount = 3;
  // A valley: a long tail and echoes rolling back off the slopes.
  sp[3].feedback = 0.95f;
  sp[3].damp = 0.4f;
  sp[3].preDelay = 0.04f;
  sp[3].level = 0.85f;
  sp[3].echoes[0] = {0.31f, 0.3f};
  sp[3].echoes[1] = {0.52f, 0.2f};
  sp[3].echoes[2] = {0.78f, 0.12f};
  sp[3].echoes[3] = {1.1f, 0.07f};
  sp[3].echoCount = 4;
  return sp;
}
}  // namespace

const SpaceSound& spaceSound(Space s) {
  static const std::array<SpaceSound, kSpaceCount> kSpaces = makeSpaces();
  return kSpaces[static_cast<size_t>(std::clamp(static_cast<int>(s), 0, kSpaceCount - 1))];
}

void Acoustics::reset(int widthTiles, int heightTiles, int cellTiles) {
  cell_ = std::max(1, cellTiles);
  cw_ = std::max(1, (widthTiles + cell_ - 1) / cell_);
  ch_ = std::max(1, (heightTiles + cell_ - 1) / cell_);
  cells_.assign(static_cast<size_t>(cw_) * static_cast<size_t>(ch_), Cell{});
}

const Acoustics::Cell& Acoustics::cellAt(float x, float y) const {
  static const Cell kNone{};
  if (cells_.empty()) return kNone;
  const int cx = std::clamp(static_cast<int>(std::floor(x / static_cast<float>(cell_))), 0, cw_ - 1);
  const int cy = std::clamp(static_cast<int>(std::floor(y / static_cast<float>(cell_))), 0, ch_ - 1);
  return cell(cx, cy);
}

void Acoustics::classify() {
  // Relief: how far the highest ground within three cells rises above this one.
  const int r = 3;
  const float valleyRelief = 1.25f;  // tiles
  for (int cy = 0; cy < ch_; ++cy)
    for (int cx = 0; cx < cw_; ++cx) {
      Cell& c = cell(cx, cy);
      if (c.solid >= 0.12f) {
        c.space = Space::Town;
        continue;
      }
      if (c.foliage >= 0.25f) {
        c.space = Space::Forest;
        continue;
      }
      // A valley needs high ground on two opposite sides, not just one hill
      // nearby: look along four lines through the cell (across, down and both
      // diagonals), each a cell wide, for a rise on both ends of the same line.
      float rise[4][2] = {};  // line, side
      for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
          const int x = cx + dx, y = cy + dy;
          if ((dx == 0 && dy == 0) || x < 0 || y < 0 || x >= cw_ || y >= ch_) continue;
          const float up = cell(x, y).ground - c.ground;
          auto note = [&](int line, int along, int off) {
            if (std::abs(off) <= 1 && along != 0) rise[line][along > 0] = std::max(rise[line][along > 0], up);
          };
          note(0, dx, dy);
          note(1, dy, dx);
          note(2, dx + dy, dx - dy);
          note(3, dx - dy, dx + dy);
        }
      bool valley = false;
      for (const auto& line : rise) valley = valley || std::min(line[0], line[1]) >= valleyRelief;
      c.space = valley ? Space::Valley : Space::Open;
    }
}

Space Acoustics::spaceAt(float x, float y) const {
  // The last zone laid down wins where zones overlap.
  for (auto it = zones_.rbegin(); it != zones_.rend(); ++it) {
    const float dx = x - it->x, dy = y - it->y;
    if (dx * dx + dy * dy <= it->radius * it->radius) return it->space;
  }
  return cells_.empty() ? Space::Open : cellAt(x, y).space;
}

void Acoustics::clearZones(const std::string& tag) {
  if (tag.empty()) zones_.clear();
  else std::erase_if(zones_, [&](const ReverbZone& z) { return z.tag == tag; });
}

float Acoustics::occlusion(float sx, float sy, float ex, float ey, float standHeight) const {
  if (cells_.empty()) return 0.0f;
  const float dx = ex - sx, dy = ey - sy;
  const float len = std::sqrt(dx * dx + dy * dy);
  const float c = static_cast<float>(cell_);
  if (len < c) return 0.0f;  // the same or the next cell: nothing between
  const float hs = cellAt(sx, sy).ground + standHeight;
  const float he = cellAt(ex, ey).ground + standHeight;
  // Walk in half-cell steps, leaving out the cells the sound and the ear stand in.
  const float step = c * 0.5f;
  const int steps = static_cast<int>(len / step);
  float rise = 0.0f;    // highest ground above the line, tiles
  float soak = 0.0f;    // buildings and trees passed through
  for (int i = 1; i < steps; ++i) {
    const float d = static_cast<float>(i) * step;
    if (d < c * 0.75f || len - d < c * 0.75f) continue;
    const float t = d / len;
    const Cell& k = cellAt(sx + dx * t, sy + dy * t);
    rise = std::max(rise, k.ground - (hs + (he - hs) * t));
    soak += (k.solid * 0.35f + k.foliage * 0.08f) * (step / c);
  }
  // A crest leaks over the top (diffraction): never quite a wall.
  const float hill = std::clamp(rise / 1.5f, 0.0f, 1.0f) * 0.85f;
  const float through = std::exp(-soak);
  return std::clamp(1.0f - (1.0f - hill) * through, 0.0f, 1.0f);
}

}  // namespace pioneer::audio

#pragma once
// Pioneer Engine - isometric projection.
//
// Three coordinate spaces:
//   * World (leptons): the simulation's integer grid, 256 leptons per tile.
//   * Tile: whole tiles, (tx, ty).
//   * Screen-world (pixels): the flat 2D plane the renderer draws in, before
//     the camera is applied. A tile is a 64 x 32 diamond.
//
// Tile (tx, ty) covers leptons [tx*256, (tx+1)*256) x [ty*256, (ty+1)*256).
// Its diamond's top corner is at tileTop(tx, ty); the diamond spans
// x - 32 .. x + 32 and y .. y + 32 on screen.

#include "pioneer/core/fixed.hpp"
#include "pioneer/math/vec.hpp"

namespace pioneer::iso {

constexpr int TILE_W = 64;
constexpr int TILE_H = 32;
constexpr float HALF_W = TILE_W / 2.0f;
constexpr float HALF_H = TILE_H / 2.0f;
// Screen pixels per tile of vertical height (true 2:1 dimetric projection).
constexpr float HEIGHT_PX_PER_TILE = 39.19f;
// Terrain height levels (Milestone 2): 16 px each.
constexpr float HEIGHT_PX_PER_LEVEL = 16.0f;

inline Vec2f worldToScreen(float lx, float ly) {
  return {(lx - ly) * (HALF_W / LEPTONS_PER_TILE), (lx + ly) * (HALF_H / LEPTONS_PER_TILE)};
}
inline Vec2f worldToScreen(Vec2i w) {
  return worldToScreen(static_cast<float>(w.x), static_cast<float>(w.y));
}

// Inverse of worldToScreen on the ground plane (height 0). Returns leptons.
inline Vec2f screenToWorld(Vec2f s) {
  const float a = s.x / HALF_W;  // (lx - ly) / 256
  const float b = s.y / HALF_H;  // (lx + ly) / 256
  return {(a + b) * 0.5f * LEPTONS_PER_TILE, (b - a) * 0.5f * LEPTONS_PER_TILE};
}

inline Vec2f tileTop(int tx, int ty) {
  return worldToScreen(static_cast<float>(tx * LEPTONS_PER_TILE),
                       static_cast<float>(ty * LEPTONS_PER_TILE));
}

// Draw-order key: larger is nearer the viewer (drawn later).
inline int64_t depthKey(int32_t lx, int32_t ly) { return static_cast<int64_t>(lx) + ly; }

}  // namespace pioneer::iso

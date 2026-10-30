#include "slave/render/terrain_renderer.hpp"

#include <algorithm>

#include "slave/math/iso.hpp"

namespace slave {

void TerrainRenderer::build(const Map& map, const SpriteCatalog& art, const SpriteRenderer& renderer) {
  map_ = &map;
  art_ = &art;
  renderer_ = &renderer;
  chunksX_ = (map.width() + kChunk - 1) / kChunk;
  chunksY_ = (map.height() + kChunk - 1) / kChunk;
  chunks_.clear();
  chunks_.resize(static_cast<size_t>(chunksX_) * chunksY_);
  for (int cy = 0; cy < chunksY_; ++cy)
    for (int cx = 0; cx < chunksX_; ++cx) {
      Chunk& c = chunks_[static_cast<size_t>(cy) * chunksX_ + cx];
      c.cx = cx;
      c.cy = cy;
      c.sprites = std::make_unique<StaticSprites>();
      buildChunk(c);
    }
}

void TerrainRenderer::buildChunk(Chunk& c) {
  std::vector<SpriteInstance> inst;
  inst.reserve(kChunk * kChunk);
  const int x0 = c.cx * kChunk, y0 = c.cy * kChunk;
  const int x1 = std::min(map_->width(), x0 + kChunk), y1 = std::min(map_->height(), y0 + kChunk);
  RectF b{1e9f, 1e9f, -1e9f, -1e9f};
  for (int ty = y0; ty < y1; ++ty)
    for (int tx = x0; tx < x1; ++tx) {
      const Tile& t = map_->at(tx, ty);
      const int frame = art_->tiles[static_cast<size_t>(t.terrain)][t.variant & 3];
      const Vec2f top = iso::tileTop(tx, ty);
      const SpriteInstance s = renderer_->makeInstance(frame, top.x, top.y);
      b.x0 = std::min(b.x0, s.x);
      b.y0 = std::min(b.y0, s.y);
      b.x1 = std::max(b.x1, s.x + s.w);
      b.y1 = std::max(b.y1, s.y + s.h);
      inst.push_back(s);
    }
  c.bounds = b;
  c.sprites->upload(*renderer_, inst);
}

void TerrainRenderer::rebuildTile(int tx, int ty) {
  if (!map_ || !map_->inBounds(tx, ty)) return;
  buildChunk(chunks_[static_cast<size_t>(ty / kChunk) * chunksX_ + tx / kChunk]);
}

void TerrainRenderer::draw(SpriteRenderer& renderer, const Camera& cam) {
  const RectF view = cam.visibleWorld(4);
  drawn_ = 0;
  for (const Chunk& c : chunks_) {
    if (!c.bounds.intersects(view)) continue;
    renderer.drawStatic(*c.sprites);
    ++drawn_;
  }
}

}  // namespace slave

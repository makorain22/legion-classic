#pragma once
// Slave Engine - terrain drawing.
// The map is cut into 32 x 32-tile chunks, each uploaded to the GPU once and
// rebuilt only when edited. Only chunks on screen are drawn, so a 512 x 512
// map costs about the same per frame as a small one.

#include <memory>
#include <vector>

#include "slave/assets/placeholder_art.hpp"
#include "slave/map/map.hpp"
#include "slave/render/sprite_renderer.hpp"

namespace slave {

class TerrainRenderer {
 public:
  static constexpr int kChunk = 32;

  void build(const Map& map, const SpriteCatalog& art, const SpriteRenderer& renderer);
  // Rebuilds the chunk containing tile (tx, ty), e.g. after an editor change.
  void rebuildTile(int tx, int ty);
  void draw(SpriteRenderer& renderer, const Camera& cam);

  size_t chunksDrawnLastFrame() const { return drawn_; }
  size_t chunkCount() const { return chunks_.size(); }

 private:
  struct Chunk {
    int cx = 0, cy = 0;
    RectF bounds;
    std::unique_ptr<StaticSprites> sprites;
  };
  void buildChunk(Chunk& c);

  const Map* map_ = nullptr;
  const SpriteCatalog* art_ = nullptr;
  const SpriteRenderer* renderer_ = nullptr;
  int chunksX_ = 0, chunksY_ = 0;
  std::vector<Chunk> chunks_;
  size_t drawn_ = 0;
};

}  // namespace slave

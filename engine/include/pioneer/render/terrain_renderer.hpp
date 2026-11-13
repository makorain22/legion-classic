#pragma once
// Pioneer Engine - terrain and water drawing.
//
// The ground is a lit height mesh, cut into 32 x 32-tile chunks that are
// uploaded once and rebuilt only when edited. A shader blends eight tileable
// ground textures per pixel using the map's splat weights, with height-based
// blending and large-scale tone variation so no tile grid is visible. Water
// is a translucent animated surface whose colour follows the depth below it.
// A depth buffer lets hills hide the water and terrain behind them.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "pioneer/map/map.hpp"
#include "pioneer/math/mat.hpp"
#include "pioneer/render/camera.hpp"
#include "pioneer/render/gl.hpp"

namespace pioneer {

class ShadowMap;

class TerrainRenderer {
 public:
  static constexpr int kChunk = 32;
  // Ground vertices per tile edge (9.2): the smooth surface of Map::heightAt
  // is sampled twice per tile.
  static constexpr int kSub = 2;

  TerrainRenderer();
  ~TerrainRenderer();
  bool init(std::string& error);

  void setMap(const Map& map);  // builds every chunk and the splat textures
  void markHeightsDirty(int tx0, int ty0, int tx1, int ty1);
  void markSplatDirty(int sx0, int sy0, int sx1, int sy1);
  void markWaterDirty();  // water level changed
  void update();          // applies pending rebuilds (call once per frame)
  void draw(const Camera& cam, float timeSeconds);
  // The 3D view: the ground (opaque, writes depth), then the water on top
  // (translucent; call after the opaque models). `shadow` may be null.
  void draw3D(const Camera& cam, float timeSeconds, const ShadowMap* shadow);
  void drawWater3D(const Camera& cam, float timeSeconds);
  // The ground into a depth-only target (the sun's shadow map).
  void drawDepth(const Mat4& viewProj);
  // Lowest and highest ground (and water), in tiles.
  void heightRange(float& zMin, float& zMax) const;
  // Chunks (index = cy * chunksX + cx) whose ground was (re)built since the
  // last call: things laid on the ground (rock faces) follow them.
  std::vector<int> takeRebuiltChunks() { return std::exchange(rebuilt_, {}); }
  int chunksX() const { return chunksX_; }
  int chunksY() const { return chunksY_; }
  // For shaders that paint the ground over other things (models' feet):
  // binds the ground textures from `firstUnit` (layers, splat 0, splat 1,
  // macro) and sets uLayers, uSplat0, uSplat1, uMacro, uMapSize, uTexScale,
  // uTint and uSat on the shader in use.
  void bindGround(const gl::Shader& s, int firstUnit) const;
  bool hasMap() const { return map_ != nullptr; }
  // Texture unit the terrain shader keeps for the shadow map.
  static constexpr int kShadowUnit = 7;

  // Fog of war. `grid` holds Vision bits per tile (1 = visible, 2 = explored).
  // Pass nullptr to show everything. Changes fade in over a few frames.
  void setFog(const std::vector<uint8_t>* grid, uint32_t revision);
  // Advances the fog fade; call once per frame with the frame time.
  void updateFog(float dt);
  // Fog brightness at a tile as currently drawn: 0 = unexplored, 255 = visible.
  uint8_t fogAt(int tx, int ty) const;

  size_t chunksDrawnLastFrame() const { return drawn_; }
  size_t chunkCount() const { return chunks_.size(); }

 private:
  struct Chunk;
  void buildChunk(Chunk& c);
  bool chunkVisible(const Chunk& c, const Mat4& viewProj) const;
  void buildSkirt();
  std::vector<int> rebuilt_;
  GLuint skirtVao_ = 0, skirtVbo_ = 0;
  GLsizei skirtVerts_ = 0;
  void uploadSplat(int sx0, int sy0, int sx1, int sy1);

  const Map* map_ = nullptr;
  gl::Shader terrainShader_, waterShader_;
  gl::Shader terrain3D_, water3D_, depth3D_;
  gl::TextureArray layers_;
  gl::Texture macro_, splat0_, splat1_, fog_;
  std::vector<uint8_t> fogTarget_, fogCur_;
  std::vector<uint32_t> fogPixels_;
  uint32_t fogRevision_ = UINT32_MAX;
  bool fogOn_ = false, fogAnimating_ = false;
  GLuint indexBuffer_ = 0;
  GLsizei indexCount_ = 0;
  int chunksX_ = 0, chunksY_ = 0;
  std::vector<std::unique_ptr<Chunk>> chunks_;
  int splatDirty_[4] = {1, 1, 0, 0};  // x0, y0, x1, y1 (empty when x0 > x1)
  float depthMax_ = 1.0f;
  size_t drawn_ = 0;
};

}  // namespace pioneer

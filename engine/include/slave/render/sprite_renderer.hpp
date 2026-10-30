#pragma once
// Slave Engine - batched sprite rendering.
//
// Every sprite (terrain tiles, units, trees, overlays) is one instance of a
// unit quad, drawn with GPU instancing straight from the texture atlas. Team
// colours are applied in the shader, so one sprite sheet serves all players.

#include <cstdint>
#include <memory>
#include <vector>

#include "slave/assets/atlas.hpp"
#include "slave/render/camera.hpp"
#include "slave/render/gl.hpp"

namespace slave {

struct SpriteInstance {
  float x, y, w, h;          // rectangle in screen-world (or screen) pixels
  float u0, v0, u1, v1;      // atlas coordinates
  uint32_t tint;             // RGBA8 multiplier (0xFFFFFFFF = unchanged)
  uint32_t team;             // RGBA8 team colour for marked pixels
};

constexpr uint32_t rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}
constexpr uint32_t kWhite = 0xFFFFFFFFu;

class SpriteRenderer;

// A GPU buffer of instances that stays put (terrain chunks).
class StaticSprites {
 public:
  StaticSprites() = default;
  StaticSprites(const StaticSprites&) = delete;
  StaticSprites& operator=(const StaticSprites&) = delete;
  ~StaticSprites();
  void upload(const SpriteRenderer& renderer, const std::vector<SpriteInstance>& instances);
  void draw() const;
  size_t count() const { return count_; }

 private:
  friend class SpriteRenderer;
  GLuint vao_ = 0, vbo_ = 0;
  size_t count_ = 0;
};

class SpriteRenderer {
 public:
  ~SpriteRenderer();
  bool init(std::string& error);

  void setAtlas(const Image& atlas, std::vector<AtlasFrame> frames);
  const AtlasFrame& frame(int i) const { return frames_[static_cast<size_t>(i)]; }
  GLuint atlasTexture() const { return atlas_.id(); }  // for UI previews

  // Starts a pass in world space (through the camera) or plain screen space.
  void beginWorld(const Camera& cam);
  void beginScreen(int viewportW, int viewportH);

  // Queues a sprite whose anchor sits at (x, y). Drawn in queue order.
  void sprite(int frameIndex, float x, float y, uint32_t team = kWhite, uint32_t tint = kWhite);
  // Same, scaled around the anchor (smoke puffs, flashes). Not pixel-snapped.
  void spriteScaled(int frameIndex, float x, float y, float scale, uint32_t tint = kWhite);
  // Switches between normal and additive blending (fire, flashes). Flushes.
  void setAdditive(bool on);
  // Solid rectangle (uses the atlas' white block).
  void rect(float x, float y, float w, float h, uint32_t color);
  void rectOutline(float x, float y, float w, float h, float thickness, uint32_t color);

  // Builds instances for a static buffer instead of drawing now.
  SpriteInstance makeInstance(int frameIndex, float x, float y, uint32_t team = kWhite) const;
  void drawStatic(const StaticSprites& s);

  void flush();
  void setWhiteFrame(int i) { whiteFrame_ = i; }
  size_t drawCallsThisFrame() const { return drawCalls_; }
  size_t spritesThisFrame() const { return spritesDrawn_; }
  void resetStats() { drawCalls_ = 0; spritesDrawn_ = 0; }

  // Sets up a VAO with the instance attribute layout (also used by StaticSprites).
  void setupInstanceVao(GLuint vao, GLuint instanceVbo) const;

 private:
  gl::Shader shader_;
  gl::Texture atlas_;
  std::vector<AtlasFrame> frames_;
  GLuint cornerVbo_ = 0, streamVao_ = 0, streamVbo_ = 0;
  size_t streamCapacity_ = 0;
  std::vector<SpriteInstance> queue_;
  int whiteFrame_ = 0;
  bool additive_ = false;
  size_t drawCalls_ = 0, spritesDrawn_ = 0;
};

}  // namespace slave

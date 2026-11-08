#pragma once
// Slave Engine - batched sprite rendering.
//
// Every sprite (terrain tiles, units, trees, overlays) is one instance of a
// unit quad, drawn with GPU instancing straight from the texture atlas. Team
// colours are applied in the shader, so one sprite sheet serves all players.

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "slave/assets/atlas.hpp"
#include "slave/math/mat.hpp"
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
  // Colour grade for the world pass: tint and saturation (1 = untouched).
  void setGrade(float r, float g, float b, float saturation) { grade_[0] = r; grade_[1] = g; grade_[2] = b; grade_[3] = saturation; }
  void beginScreen(int viewportW, int viewportH);

  // Queues a sprite whose anchor sits at (x, y). Drawn in queue order.
  void sprite(int frameIndex, float x, float y, uint32_t team = kWhite, uint32_t tint = kWhite);
  // Same, scaled around the anchor (smoke puffs, flashes). Not pixel-snapped.
  void spriteScaled(int frameIndex, float x, float y, float scale, uint32_t tint = kWhite);
  // A ground decal draped over uneven ground: drawn as a grid of pieces, each
  // raised by lift(dx, dy) pixels, where (dx, dy) is the piece's offset from
  // the anchor in screen pixels.
  template <class Lift>
  void spriteDraped(int frameIndex, float x, float y, uint32_t tint, int grid, Lift&& lift) {
    const AtlasFrame& f = frames_[static_cast<size_t>(frameIndex)];
    const float x0 = std::floor(x + 0.5f) - f.anchorX, y0 = std::floor(y + 0.5f) - f.anchorY;
    const float cw = static_cast<float>(f.w) / grid, ch = static_cast<float>(f.h) / grid;
    const float du = (f.u1 - f.u0) / grid, dv = (f.v1 - f.v0) / grid;
    for (int j = 0; j < grid; ++j)
      for (int i = 0; i < grid; ++i) {
        const float px = x0 + i * cw, py = y0 + j * ch;
        const float up = lift(px + cw * 0.5f - x, py + ch * 0.5f - y);
        queue_.push_back({px, py - up, cw + 0.02f, ch + 0.6f, f.u0 + i * du, f.v0 + j * dv, f.u0 + (i + 1) * du,
                          f.v0 + (j + 1) * dv, tint, kWhite});
      }
  }
  // Switches between normal and additive blending (fire, flashes). Flushes.
  void setAdditive(bool on);
  // Solid rectangle (uses the atlas' white block).
  void rect(float x, float y, float w, float h, uint32_t color);
  void rectOutline(float x, float y, float w, float h, float thickness, uint32_t color);

  // --- Sprites in the 3D world --------------------------------------------------
  // Starts a pass of sprites placed in the 3D view: depth-tested against the
  // ground and the models, without writing depth. Ends with flush() or the
  // next begin call.
  void begin3D(const Camera& cam);
  bool in3D() const { return in3D_; }
  // A sprite facing the camera, its anchor at a world point (tiles). One
  // sprite pixel is `tilesPerPixel` tiles across (1/45.25 = the classic scale).
  void billboard(int frameIndex, Vec3f at, float tilesPerPixel, uint32_t team = kWhite, uint32_t tint = kWhite);
  // A strip of a sprite as a streak between two world points (tracers),
  // `widthTiles` wide, facing the camera.
  void streak(int frameIndex, Vec3f a, Vec3f b, float widthTiles, uint32_t tint);
  // A ground sprite of the classic view (drawn in its 2:1 diamond) laid back
  // flat onto the ground: each piece of a grid goes where its pixels lay on the
  // ground in the classic view, at height(lx, ly) tiles. (lx, ly) = anchor, leptons.
  template <class Height>
  void decal(int frameIndex, float lx, float ly, uint32_t tint, uint32_t team, int grid, Height&& height) {
    const AtlasFrame& f = frames_[static_cast<size_t>(frameIndex)];
    const float du = (f.u1 - f.u0) / grid, dv = (f.v1 - f.v0) / grid;
    const float cw = static_cast<float>(f.w) / grid, ch = static_cast<float>(f.h) / grid;
    auto corner = [&](int i, int j) {
      // Sprite pixel offset from the anchor -> ground offset in leptons.
      const float px = i * cw - f.anchorX, py = j * ch - f.anchorY;
      const float gx = lx + (px / 32.0f + py / 16.0f) * 128.0f, gy = ly + (py / 16.0f - px / 32.0f) * 128.0f;
      return Vertex3{gx / 256.0f, gy / 256.0f, height(gx, gy), f.u0 + i * du, f.v0 + j * dv, tint, team};
    };
    for (int j = 0; j < grid; ++j)
      for (int i = 0; i < grid; ++i) {
        const Vertex3 a = corner(i, j), b = corner(i + 1, j), c = corner(i + 1, j + 1), d = corner(i, j + 1);
        queue3D_.insert(queue3D_.end(), {a, b, c, a, c, d});
      }
  }
  // A flat quad on the ground with explicit corners (selection rings, lines).
  void groundQuad(int frameIndex, const Vec3f corners[4], uint32_t tint);
  // A solid quad of colour between four world points.
  void solidQuad(const Vec3f corners[4], uint32_t color);

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
  struct Vertex3 {
    float x, y, z, u, v;
    uint32_t tint, team;
  };
  void flush3D();
  gl::Shader shader3D_;
  gl::Texture atlasSmooth_;  // the same atlas, filtered: sprites in 3D are scaled freely
  GLuint vao3D_ = 0, vbo3D_ = 0;
  size_t capacity3D_ = 0;
  std::vector<Vertex3> queue3D_;
  bool in3D_ = false;
  Vec3f camRight_, camUp_;

  gl::Shader shader_;
  gl::Texture atlas_;
  std::vector<AtlasFrame> frames_;
  GLuint cornerVbo_ = 0, streamVao_ = 0, streamVbo_ = 0;
  size_t streamCapacity_ = 0;
  std::vector<SpriteInstance> queue_;
  int whiteFrame_ = 0;
  bool additive_ = false;
  size_t drawCalls_ = 0, spritesDrawn_ = 0;
  float grade_[4] = {1, 1, 1, 1};
};

}  // namespace slave

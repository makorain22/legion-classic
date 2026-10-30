#include "slave/render/sprite_renderer.hpp"

#include <cmath>
#include <cstddef>

namespace slave {

namespace {
const char* kVertex = R"(#version 330 core
layout(location = 0) in vec2 aCorner;
layout(location = 1) in vec4 aRect;
layout(location = 2) in vec4 aUV;
layout(location = 3) in vec4 aTint;
layout(location = 4) in vec4 aTeam;
uniform vec2 uCamera;
uniform vec2 uHalf;
uniform vec2 uViewport;
uniform float uZoom;
out vec2 vUV;
out vec4 vTint;
out vec4 vTeam;
void main() {
  vec2 world = aRect.xy + aCorner * aRect.zw;
  vec2 screen = (world - uCamera) * uZoom + uHalf;
  gl_Position = vec4(screen.x / uViewport.x * 2.0 - 1.0, 1.0 - screen.y / uViewport.y * 2.0, 0.0, 1.0);
  vUV = mix(aUV.xy, aUV.zw, aCorner);
  vTint = aTint;
  vTeam = aTeam;
}
)";

const char* kFragment = R"(#version 330 core
in vec2 vUV;
in vec4 vTint;
in vec4 vTeam;
uniform sampler2D uAtlas;
out vec4 fragColor;
void main() {
  vec4 c = texture(uAtlas, vUV);
  if (c.a < 0.004) discard;
  // Team-colour pixels: alpha 254, red channel = brightness (128 = team colour).
  if (abs(c.a - 254.0 / 255.0) < 0.002) {
    float b = c.r * (255.0 / 128.0);
    vec3 col = b <= 1.0 ? vTeam.rgb * b : mix(vTeam.rgb, vec3(1.0), clamp(b - 1.0, 0.0, 1.0));
    c = vec4(col, 1.0);
  }
  fragColor = c * vTint;
}
)";
}  // namespace

// ---------------------------------------------------------------------------
StaticSprites::~StaticSprites() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
}

void StaticSprites::upload(const SpriteRenderer& renderer, const std::vector<SpriteInstance>& instances) {
  if (!vao_) {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    renderer.setupInstanceVao(vao_, vbo_);
  }
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instances.size() * sizeof(SpriteInstance)),
               instances.data(), GL_STATIC_DRAW);
  count_ = instances.size();
}

void StaticSprites::draw() const {
  if (!count_) return;
  glBindVertexArray(vao_);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(count_));
}

// ---------------------------------------------------------------------------
SpriteRenderer::~SpriteRenderer() {
  if (cornerVbo_) glDeleteBuffers(1, &cornerVbo_);
  if (streamVbo_) glDeleteBuffers(1, &streamVbo_);
  if (streamVao_) glDeleteVertexArrays(1, &streamVao_);
}

bool SpriteRenderer::init(std::string& error) {
  if (!shader_.compile(kVertex, kFragment, error)) return false;
  const float corners[8] = {0, 0, 1, 0, 0, 1, 1, 1};
  glGenBuffers(1, &cornerVbo_);
  glBindBuffer(GL_ARRAY_BUFFER, cornerVbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);

  glGenVertexArrays(1, &streamVao_);
  glGenBuffers(1, &streamVbo_);
  setupInstanceVao(streamVao_, streamVbo_);
  queue_.reserve(16384);
  return true;
}

void SpriteRenderer::setupInstanceVao(GLuint vao, GLuint instanceVbo) const {
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, cornerVbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

  glBindBuffer(GL_ARRAY_BUFFER, instanceVbo);
  const GLsizei stride = sizeof(SpriteInstance);
  auto attr = [&](GLuint loc, GLint n, GLenum type, GLboolean norm, size_t offset) {
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, n, type, norm, stride, reinterpret_cast<const void*>(offset));
    glVertexAttribDivisor(loc, 1);
  };
  attr(1, 4, GL_FLOAT, GL_FALSE, offsetof(SpriteInstance, x));
  attr(2, 4, GL_FLOAT, GL_FALSE, offsetof(SpriteInstance, u0));
  attr(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(SpriteInstance, tint));
  attr(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(SpriteInstance, team));
  glBindVertexArray(0);
}

void SpriteRenderer::setAtlas(const Image& atlas, std::vector<AtlasFrame> frames) {
  atlas_.create(atlas);
  frames_ = std::move(frames);
}

void SpriteRenderer::beginWorld(const Camera& cam) {
  flush();
  shader_.use();
  const Vec2f c = cam.snappedCenter();
  glUniform2f(shader_.uniform("uCamera"), c.x, c.y);
  glUniform2f(shader_.uniform("uHalf"), std::floor(cam.viewportW() * 0.5f), std::floor(cam.viewportH() * 0.5f));
  glUniform2f(shader_.uniform("uViewport"), static_cast<float>(cam.viewportW()), static_cast<float>(cam.viewportH()));
  glUniform1f(shader_.uniform("uZoom"), cam.zoom());
  glUniform1i(shader_.uniform("uAtlas"), 0);
  atlas_.bind(0);
}

void SpriteRenderer::beginScreen(int viewportW, int viewportH) {
  flush();
  shader_.use();
  glUniform2f(shader_.uniform("uCamera"), 0, 0);
  glUniform2f(shader_.uniform("uHalf"), 0, 0);
  glUniform2f(shader_.uniform("uViewport"), static_cast<float>(viewportW), static_cast<float>(viewportH));
  glUniform1f(shader_.uniform("uZoom"), 1.0f);
  glUniform1i(shader_.uniform("uAtlas"), 0);
  atlas_.bind(0);
}

SpriteInstance SpriteRenderer::makeInstance(int frameIndex, float x, float y, uint32_t team) const {
  const AtlasFrame& f = frames_[static_cast<size_t>(frameIndex)];
  return {std::floor(x + 0.5f) - f.anchorX, std::floor(y + 0.5f) - f.anchorY, static_cast<float>(f.w),
          static_cast<float>(f.h), f.u0, f.v0, f.u1, f.v1, kWhite, team};
}

void SpriteRenderer::sprite(int frameIndex, float x, float y, uint32_t team, uint32_t tint) {
  SpriteInstance s = makeInstance(frameIndex, x, y, team);
  s.tint = tint;
  queue_.push_back(s);
}

void SpriteRenderer::spriteScaled(int frameIndex, float x, float y, float scale, uint32_t tint) {
  const AtlasFrame& f = frames_[static_cast<size_t>(frameIndex)];
  queue_.push_back({x - f.anchorX * scale, y - f.anchorY * scale, f.w * scale, f.h * scale, f.u0, f.v0, f.u1, f.v1,
                    tint, kWhite});
}

void SpriteRenderer::setAdditive(bool on) {
  if (on == additive_) return;
  flush();
  additive_ = on;
  glBlendFunc(GL_SRC_ALPHA, on ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
}

void SpriteRenderer::rect(float x, float y, float w, float h, uint32_t color) {
  const AtlasFrame& f = frames_[static_cast<size_t>(whiteFrame_)];
  const float u = (f.u0 + f.u1) * 0.5f, v = (f.v0 + f.v1) * 0.5f;
  queue_.push_back({x, y, w, h, u, v, u, v, color, kWhite});
}

void SpriteRenderer::rectOutline(float x, float y, float w, float h, float t, uint32_t color) {
  rect(x, y, w, t, color);
  rect(x, y + h - t, w, t, color);
  rect(x, y + t, t, h - 2 * t, color);
  rect(x + w - t, y + t, t, h - 2 * t, color);
}

void SpriteRenderer::drawStatic(const StaticSprites& s) {
  flush();
  s.draw();
  ++drawCalls_;
  spritesDrawn_ += s.count();
}

void SpriteRenderer::flush() {
  if (queue_.empty()) return;
  glBindBuffer(GL_ARRAY_BUFFER, streamVbo_);
  const size_t bytes = queue_.size() * sizeof(SpriteInstance);
  if (queue_.size() > streamCapacity_) {
    streamCapacity_ = queue_.size() * 2;
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(streamCapacity_ * sizeof(SpriteInstance)), nullptr,
                 GL_STREAM_DRAW);
  } else {
    // Orphan the old storage so the driver never waits on the GPU.
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(streamCapacity_ * sizeof(SpriteInstance)), nullptr,
                 GL_STREAM_DRAW);
  }
  glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), queue_.data());
  glBindVertexArray(streamVao_);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(queue_.size()));
  ++drawCalls_;
  spritesDrawn_ += queue_.size();
  queue_.clear();
}

}  // namespace slave

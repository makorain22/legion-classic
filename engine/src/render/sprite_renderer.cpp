#include "pioneer/render/sprite_renderer.hpp"

#include <cmath>
#include <cstddef>

namespace pioneer {

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

const char* kVertex3D = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aTint;
layout(location = 3) in vec4 aTeam;
uniform mat4 uViewProj;
out vec2 vUV;
out vec4 vTint;
out vec4 vTeam;
void main() {
  gl_Position = uViewProj * vec4(aPos, 1.0);
  vUV = aUV;
  vTint = aTint;
  vTeam = aTeam;
}
)";

const char* kFragment = R"(#version 330 core
in vec2 vUV;
in vec4 vTint;
in vec4 vTeam;
uniform sampler2D uAtlas;
uniform vec4 uGrade;  // colour grade: rgb tint, a = saturation
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
  c *= vTint;
  c.rgb = mix(vec3(dot(c.rgb, vec3(0.30, 0.59, 0.11))), c.rgb, uGrade.a) * uGrade.rgb;
  fragColor = c;
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
  if (vbo3D_) glDeleteBuffers(1, &vbo3D_);
  if (vao3D_) glDeleteVertexArrays(1, &vao3D_);
  if (cornerVbo_) glDeleteBuffers(1, &cornerVbo_);
  if (streamVbo_) glDeleteBuffers(1, &streamVbo_);
  if (streamVao_) glDeleteVertexArrays(1, &streamVao_);
}

bool SpriteRenderer::init(std::string& error) {
  if (!shader_.compile(kVertex, kFragment, error)) return false;
  if (!shader3D_.compile(kVertex3D, kFragment, error)) return false;
  glGenVertexArrays(1, &vao3D_);
  glGenBuffers(1, &vbo3D_);
  glBindVertexArray(vao3D_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo3D_);
  {
    const GLsizei st = sizeof(Vertex3);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, reinterpret_cast<void*>(offsetof(Vertex3, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, st, reinterpret_cast<void*>(offsetof(Vertex3, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, reinterpret_cast<void*>(offsetof(Vertex3, tint)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, reinterpret_cast<void*>(offsetof(Vertex3, team)));
  }
  glBindVertexArray(0);
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
  atlasSmooth_.create(atlas, {true, false, false});
  frames_ = std::move(frames);
}

void SpriteRenderer::beginWorld(const Camera& cam) {
  flush();
  in3D_ = false;
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  shader_.use();
  const Vec2f c = cam.snappedCenter();
  glUniform2f(shader_.uniform("uCamera"), c.x, c.y);
  glUniform2f(shader_.uniform("uHalf"), std::floor(cam.viewportW() * 0.5f), std::floor(cam.viewportH() * 0.5f));
  glUniform2f(shader_.uniform("uViewport"), static_cast<float>(cam.viewportW()), static_cast<float>(cam.viewportH()));
  // In 3D the camera's "world" plane is the screen itself (overlays placed at projected points).
  glUniform1f(shader_.uniform("uZoom"), cam.is3D() ? 1.0f : cam.zoom());
  glUniform1i(shader_.uniform("uAtlas"), 0);
  glUniform4f(shader_.uniform("uGrade"), grade_[0], grade_[1], grade_[2], grade_[3]);
  atlas_.bind(0);
}

void SpriteRenderer::begin3D(const Camera& cam) {
  flush();
  in3D_ = true;
  camRight_ = cam.right();
  camUp_ = cam.up();
  shader3D_.use();
  glUniformMatrix4fv(shader3D_.uniform("uViewProj"), 1, GL_FALSE, cam.viewProj().m);
  glUniform1i(shader3D_.uniform("uAtlas"), 0);
  glUniform4f(shader3D_.uniform("uGrade"), grade_[0], grade_[1], grade_[2], grade_[3]);
  atlasSmooth_.bind(0);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
}

void SpriteRenderer::billboard(int frameIndex, Vec3f at, float tpp, uint32_t team, uint32_t tint) {
  const AtlasFrame& f = frames_[static_cast<size_t>(frameIndex)];
  auto corner = [&](float px, float py, float u, float v) {
    const Vec3f p = at + camRight_ * ((px - f.anchorX) * tpp) + camUp_ * ((f.anchorY - py) * tpp);
    return Vertex3{p.x, p.y, p.z, u, v, tint, team};
  };
  const float w = static_cast<float>(f.w), h = static_cast<float>(f.h);
  const Vertex3 a = corner(0, 0, f.u0, f.v0), b = corner(w, 0, f.u1, f.v0), c = corner(w, h, f.u1, f.v1),
                d = corner(0, h, f.u0, f.v1);
  queue3D_.insert(queue3D_.end(), {a, b, c, a, c, d});
}

void SpriteRenderer::streak(int frameIndex, Vec3f a, Vec3f b, float width, uint32_t tint) {
  const AtlasFrame& f = frames_[static_cast<size_t>(frameIndex)];
  // Across the streak, in the screen's plane.
  const Vec3f along = b - a;
  Vec3f side = cross(along, cross(camRight_, camUp_));
  if (side.length() < 1e-6f) side = camRight_;
  side = side.normalized() * (width * 0.5f);
  const Vertex3 p0{a.x - side.x, a.y - side.y, a.z - side.z, f.u0, f.v0, tint, kWhite};
  const Vertex3 p1{a.x + side.x, a.y + side.y, a.z + side.z, f.u1, f.v0, tint, kWhite};
  const Vertex3 p2{b.x + side.x, b.y + side.y, b.z + side.z, f.u1, f.v1, tint, kWhite};
  const Vertex3 p3{b.x - side.x, b.y - side.y, b.z - side.z, f.u0, f.v1, tint, kWhite};
  queue3D_.insert(queue3D_.end(), {p0, p1, p2, p0, p2, p3});
}

void SpriteRenderer::groundQuad(int frameIndex, const Vec3f c[4], uint32_t tint) {
  const AtlasFrame& f = frames_[static_cast<size_t>(frameIndex)];
  const Vertex3 a{c[0].x, c[0].y, c[0].z, f.u0, f.v0, tint, kWhite}, b{c[1].x, c[1].y, c[1].z, f.u1, f.v0, tint, kWhite},
                d{c[2].x, c[2].y, c[2].z, f.u1, f.v1, tint, kWhite}, e{c[3].x, c[3].y, c[3].z, f.u0, f.v1, tint, kWhite};
  queue3D_.insert(queue3D_.end(), {a, b, d, a, d, e});
}

void SpriteRenderer::solidQuad(const Vec3f c[4], uint32_t color) {
  const AtlasFrame& f = frames_[static_cast<size_t>(whiteFrame_)];
  const float u = (f.u0 + f.u1) * 0.5f, v = (f.v0 + f.v1) * 0.5f;
  Vertex3 q[4];
  for (int i = 0; i < 4; ++i) q[i] = {c[i].x, c[i].y, c[i].z, u, v, color, kWhite};
  queue3D_.insert(queue3D_.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
}

void SpriteRenderer::flush3D() {
  if (queue3D_.empty()) return;
  glBindBuffer(GL_ARRAY_BUFFER, vbo3D_);
  if (queue3D_.size() > capacity3D_) capacity3D_ = queue3D_.size() * 2;
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity3D_ * sizeof(Vertex3)), nullptr, GL_STREAM_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(queue3D_.size() * sizeof(Vertex3)), queue3D_.data());
  glBindVertexArray(vao3D_);
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(queue3D_.size()));
  glBindVertexArray(0);
  ++drawCalls_;
  spritesDrawn_ += queue3D_.size() / 6;
  queue3D_.clear();
}

void SpriteRenderer::beginScreen(int viewportW, int viewportH) {
  flush();
  in3D_ = false;
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  shader_.use();
  glUniform2f(shader_.uniform("uCamera"), 0, 0);
  glUniform2f(shader_.uniform("uHalf"), 0, 0);
  glUniform2f(shader_.uniform("uViewport"), static_cast<float>(viewportW), static_cast<float>(viewportH));
  glUniform1f(shader_.uniform("uZoom"), 1.0f);
  glUniform1i(shader_.uniform("uAtlas"), 0);
  glUniform4f(shader_.uniform("uGrade"), 1, 1, 1, 1);  // the interface is never graded
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
  flush3D();
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

}  // namespace pioneer

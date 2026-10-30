#include "slave/render/terrain_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "slave/assets/ground_textures.hpp"
#include "slave/core/log.hpp"
#include "slave/math/iso.hpp"

namespace slave {

namespace {

constexpr float kPxPerUnit = iso::HEIGHT_PX_PER_TILE / HEIGHT_UNITS_PER_TILE;
constexpr float kTexScale = 1.0f / 4.0f;  // ground textures repeat every 4 tiles

struct Vertex {
  float sx, sy;  // screen-world position
  float tx, ty;  // tile coordinates (texture and splat lookups)
  float z;       // height in tiles (depth sorting)
  float extra;   // terrain: light; water: depth in height units
};

const char* kCommonVertex = R"(#version 330 core
layout(location = 0) in vec2 aScreen;
layout(location = 1) in vec2 aTile;
layout(location = 2) in float aZ;
layout(location = 3) in float aExtra;
uniform vec2 uCamera;
uniform vec2 uHalf;
uniform vec2 uViewport;
uniform float uZoom;
uniform float uDepthMax;
out vec2 vTile;
out float vExtra;
void main() {
  vec2 s = (aScreen - uCamera) * uZoom + uHalf;
  // Distance toward the viewer: farther tiles and lower ground are deeper.
  float d = (aTile.x + aTile.y) * 0.6124 + aZ * 0.5;
  float z = 1.0 - 2.0 * clamp(d / uDepthMax, 0.0, 1.0);
  gl_Position = vec4(s.x / uViewport.x * 2.0 - 1.0, 1.0 - s.y / uViewport.y * 2.0, z, 1.0);
  vTile = aTile;
  vExtra = aExtra;
}
)";

const char* kTerrainFragment = R"(#version 330 core
in vec2 vTile;
in float vExtra;
uniform sampler2DArray uLayers;
uniform sampler2D uSplat0;
uniform sampler2D uSplat1;
uniform sampler2D uMacro;
uniform vec2 uMapSize;
uniform float uTexScale;
out vec4 fragColor;
void main() {
  vec2 suv = vTile / uMapSize;
  vec4 s0 = texture(uSplat0, suv);
  vec4 s1 = texture(uSplat1, suv);
  float w[8] = float[8](s0.r, s0.g, s0.b, s0.a, s1.r, s1.g, s1.b, s1.a);
  vec2 uv = vTile * uTexScale;

  // Height-based blending: where two layers meet, the "taller" texture wins,
  // giving ragged natural edges instead of soft smears.
  vec4 c[8];
  float hb[8];
  float top = 0.0;
  for (int i = 0; i < 8; ++i) {
    if (w[i] > 0.004) {
      c[i] = texture(uLayers, vec3(uv, float(i)));
      hb[i] = w[i] + c[i].a * 0.45;
    } else {
      c[i] = vec4(0.0);
      hb[i] = 0.0;
    }
    top = max(top, hb[i]);
  }
  vec3 col = vec3(0.0);
  float total = 0.0;
  for (int i = 0; i < 8; ++i) {
    float k = max(hb[i] - top + 0.22, 0.0);
    col += c[i].rgb * k;
    total += k;
  }
  col /= max(total, 1e-4);

  // Large-scale patchiness so the ground never looks tiled.
  float tone = texture(uMacro, vTile / 29.0).r;
  float tone2 = texture(uMacro, vTile / 9.7 + vec2(0.37, 0.11)).r;
  float hue = texture(uMacro, vTile / 17.0 + vec2(0.5, 0.2)).g;
  col *= 0.78 + 0.34 * tone + 0.16 * (tone2 - 0.5);
  // Patches drift between greener and browner, like trampled, uneven fields.
  col = mix(col, col * vec3(1.06, 1.01, 0.86), clamp((hue - 0.55) * 2.0, 0.0, 1.0) * 0.5);
  col = mix(col, col * vec3(0.90, 1.05, 0.92), clamp((0.45 - hue) * 2.0, 0.0, 1.0) * 0.5);

  col *= vExtra;  // slope lighting
  fragColor = vec4(col, 1.0);
}
)";

const char* kWaterFragment = R"(#version 330 core
in vec2 vTile;
in float vExtra;  // depth in height units (64 per tile)
uniform sampler2D uMacro;
uniform float uTime;
out vec4 fragColor;
void main() {
  float d = vExtra;
  if (d <= 0.0) discard;
  float r1 = texture(uMacro, vTile * 0.11 + vec2(uTime * 0.010, uTime * 0.006)).b;
  float r2 = texture(uMacro, vTile * 0.19 - vec2(uTime * 0.007, -uTime * 0.009)).b;
  float r = r1 * 0.55 + r2 * 0.45;
  vec3 shallow = vec3(0.29, 0.32, 0.22);
  vec3 deep = vec3(0.09, 0.16, 0.17);
  vec3 col = mix(shallow, deep, smoothstep(3.0, 45.0, d));
  col += (r - 0.5) * 0.10;
  col += pow(clamp(r, 0.0, 1.0), 7.0) * 0.35;       // glints
  float shore = 1.0 - smoothstep(0.0, 4.0, d);
  col = mix(col, vec3(0.50, 0.47, 0.38), shore * 0.45 * smoothstep(0.35, 0.65, r));
  float alpha = mix(0.25, 0.92, smoothstep(0.0, 18.0, d));
  fragColor = vec4(col, alpha);
}
)";

void setupVao(GLuint vao, GLuint vbo, GLuint ibo) {
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  if (ibo) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
  const GLsizei stride = sizeof(Vertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, sx)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, tx)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, z)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, extra)));
  glBindVertexArray(0);
}

void setCameraUniforms(const gl::Shader& s, const Camera& cam, float depthMax) {
  const Vec2f c = cam.snappedCenter();
  glUniform2f(s.uniform("uCamera"), c.x, c.y);
  glUniform2f(s.uniform("uHalf"), std::floor(cam.viewportW() * 0.5f), std::floor(cam.viewportH() * 0.5f));
  glUniform2f(s.uniform("uViewport"), static_cast<float>(cam.viewportW()), static_cast<float>(cam.viewportH()));
  glUniform1f(s.uniform("uZoom"), cam.zoom());
  glUniform1f(s.uniform("uDepthMax"), depthMax);
}
}  // namespace

struct TerrainRenderer::Chunk {
  int cx = 0, cy = 0;
  RectF bounds;
  GLuint vao = 0, vbo = 0;
  GLuint waterVao = 0, waterVbo = 0;
  GLsizei waterVerts = 0;
  bool dirty = true;
  ~Chunk() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    if (waterVbo) glDeleteBuffers(1, &waterVbo);
    if (waterVao) glDeleteVertexArrays(1, &waterVao);
  }
};

TerrainRenderer::TerrainRenderer() = default;
TerrainRenderer::~TerrainRenderer() {
  chunks_.clear();
  if (indexBuffer_) glDeleteBuffers(1, &indexBuffer_);
}

bool TerrainRenderer::init(std::string& error) {
  if (!terrainShader_.compile(kCommonVertex, kTerrainFragment, error)) return false;
  if (!waterShader_.compile(kCommonVertex, kWaterFragment, error)) return false;

  const GroundTextureSet tex = makeGroundTextures(512);
  layers_.create(tex.layers);
  macro_.create(tex.macro, {true, true, true});

  // Every chunk shares one index buffer: a 32 x 32 grid of quads.
  std::vector<uint16_t> idx;
  const int n = kChunk + 1;
  for (int y = 0; y < kChunk; ++y)
    for (int x = 0; x < kChunk; ++x) {
      const uint16_t a = static_cast<uint16_t>(y * n + x), b = static_cast<uint16_t>(a + 1);
      const uint16_t c = static_cast<uint16_t>(a + n), d = static_cast<uint16_t>(c + 1);
      // Alternate the split diagonal so lighting has no directional bias.
      if ((x + y) & 1) idx.insert(idx.end(), {a, b, d, a, d, c});
      else idx.insert(idx.end(), {a, b, c, b, d, c});
    }
  glGenBuffers(1, &indexBuffer_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(uint16_t)), idx.data(),
               GL_STATIC_DRAW);
  indexCount_ = static_cast<GLsizei>(idx.size());
  return true;
}

void TerrainRenderer::setMap(const Map& map) {
  map_ = &map;
  chunksX_ = map.width() / kChunk;
  chunksY_ = map.height() / kChunk;
  chunks_.clear();
  for (int cy = 0; cy < chunksY_; ++cy)
    for (int cx = 0; cx < chunksX_; ++cx) {
      auto c = std::make_unique<Chunk>();
      c->cx = cx;
      c->cy = cy;
      chunks_.push_back(std::move(c));
    }
  splat0_.allocate(map.splatW(), map.splatH(), {true, false, false});
  splat1_.allocate(map.splatW(), map.splatH(), {true, false, false});
  splatDirty_[0] = 0;
  splatDirty_[1] = 0;
  splatDirty_[2] = map.splatW() - 1;
  splatDirty_[3] = map.splatH() - 1;
  update();
}

void TerrainRenderer::markHeightsDirty(int tx0, int ty0, int tx1, int ty1) {
  if (!map_) return;
  // Neighbouring chunks too: lighting at a chunk edge uses the next corners.
  const int c0x = std::max(0, (tx0 - 1) / kChunk), c0y = std::max(0, (ty0 - 1) / kChunk);
  const int c1x = std::min(chunksX_ - 1, (tx1 + 1) / kChunk), c1y = std::min(chunksY_ - 1, (ty1 + 1) / kChunk);
  for (int cy = c0y; cy <= c1y; ++cy)
    for (int cx = c0x; cx <= c1x; ++cx) chunks_[static_cast<size_t>(cy) * chunksX_ + cx]->dirty = true;
}

void TerrainRenderer::markSplatDirty(int sx0, int sy0, int sx1, int sy1) {
  if (splatDirty_[0] > splatDirty_[2]) {
    splatDirty_[0] = sx0;
    splatDirty_[1] = sy0;
    splatDirty_[2] = sx1;
    splatDirty_[3] = sy1;
  } else {
    splatDirty_[0] = std::min(splatDirty_[0], sx0);
    splatDirty_[1] = std::min(splatDirty_[1], sy0);
    splatDirty_[2] = std::max(splatDirty_[2], sx1);
    splatDirty_[3] = std::max(splatDirty_[3], sy1);
  }
}

void TerrainRenderer::markWaterDirty() {
  for (auto& c : chunks_) c->dirty = true;
}

void TerrainRenderer::uploadSplat(int sx0, int sy0, int sx1, int sy1) {
  sx0 = std::max(0, sx0);
  sy0 = std::max(0, sy0);
  sx1 = std::min(map_->splatW() - 1, sx1);
  sy1 = std::min(map_->splatH() - 1, sy1);
  if (sx0 > sx1 || sy0 > sy1) return;
  const int w = sx1 - sx0 + 1, h = sy1 - sy0 + 1;
  std::vector<uint8_t> a(static_cast<size_t>(w) * h * 4), b(a.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const uint8_t* s = map_->splat(sx0 + x, sy0 + y);
      const size_t o = (static_cast<size_t>(y) * w + x) * 4;
      for (int k = 0; k < 4; ++k) {
        a[o + k] = s[k];
        b[o + k] = s[4 + k];
      }
    }
  splat0_.update(sx0, sy0, w, h, a.data());
  splat1_.update(sx0, sy0, w, h, b.data());
}

void TerrainRenderer::update() {
  if (!map_) return;
  if (splatDirty_[0] <= splatDirty_[2]) {
    uploadSplat(splatDirty_[0], splatDirty_[1], splatDirty_[2], splatDirty_[3]);
    splatDirty_[0] = 1;
    splatDirty_[2] = 0;
  }
  bool any = false;
  for (auto& c : chunks_)
    if (c->dirty) {
      buildChunk(*c);
      c->dirty = false;
      any = true;
    }
  if (any) {
    const float maxZ = std::max<float>(map_->maxHeight(), map_->waterLevel()) / HEIGHT_UNITS_PER_TILE;
    depthMax_ = (map_->width() + map_->height()) * 0.6124f + maxZ * 0.5f + 4.0f;
  }
}

void TerrainRenderer::buildChunk(Chunk& c) {
  const Map& m = *map_;
  const int n = kChunk + 1;
  const int x0 = c.cx * kChunk, y0 = c.cy * kChunk;

  // Light from the upper left, matching the sprites.
  const float lx = -0.35f, ly = 0.45f, lz = 0.80f;
  const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
  const float Lx = lx / ll, Ly = ly / ll, Lz = lz / ll;

  std::vector<Vertex> verts(static_cast<size_t>(n) * n);
  RectF b{1e9f, 1e9f, -1e9f, -1e9f};
  auto hc = [&](int x, int y) {
    return static_cast<float>(m.corner(std::clamp(x, 0, m.width()), std::clamp(y, 0, m.height())));
  };
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      const int tx = x0 + x, ty = y0 + y;
      const float h = hc(tx, ty);
      Vec2f s = iso::worldToScreen(static_cast<float>(tx * LEPTONS_PER_TILE), static_cast<float>(ty * LEPTONS_PER_TILE));
      s.y -= h * kPxPerUnit;
      const float dx = (hc(tx + 1, ty) - hc(tx - 1, ty)) / (2.0f * HEIGHT_UNITS_PER_TILE);
      const float dy = (hc(tx, ty + 1) - hc(tx, ty - 1)) / (2.0f * HEIGHT_UNITS_PER_TILE);
      const float nl = std::sqrt(dx * dx + dy * dy + 1.0f);
      const float lambert = (-dx * Lx - dy * Ly + Lz) / nl;
      const float light = std::clamp(1.0f + (lambert - Lz) * 1.9f, 0.42f, 1.35f);
      verts[static_cast<size_t>(y) * n + x] = {s.x, s.y, static_cast<float>(tx), static_cast<float>(ty),
                                               h / HEIGHT_UNITS_PER_TILE, light};
      b.x0 = std::min(b.x0, s.x);
      b.y0 = std::min(b.y0, s.y);
      b.x1 = std::max(b.x1, s.x);
      b.y1 = std::max(b.y1, s.y);
    }
  if (!c.vao) {
    glGenVertexArrays(1, &c.vao);
    glGenBuffers(1, &c.vbo);
    setupVao(c.vao, c.vbo, indexBuffer_);
  }
  glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)), verts.data(),
               GL_STATIC_DRAW);

  // Water surface over every tile that dips below the water level.
  std::vector<Vertex> water;
  const float wl = m.waterLevel();
  if (wl > 0) {
    for (int y = 0; y < kChunk; ++y)
      for (int x = 0; x < kChunk; ++x) {
        const int tx = x0 + x, ty = y0 + y;
        const float lo = std::min({hc(tx, ty), hc(tx + 1, ty), hc(tx, ty + 1), hc(tx + 1, ty + 1)});
        if (lo >= wl) continue;
        auto wv = [&](int ax, int ay) {
          Vec2f s = iso::worldToScreen(static_cast<float>(ax * LEPTONS_PER_TILE), static_cast<float>(ay * LEPTONS_PER_TILE));
          s.y -= wl * kPxPerUnit;
          b.y0 = std::min(b.y0, s.y);
          return Vertex{s.x, s.y, static_cast<float>(ax), static_cast<float>(ay), wl / HEIGHT_UNITS_PER_TILE,
                        wl - hc(ax, ay)};
        };
        const Vertex v00 = wv(tx, ty), v10 = wv(tx + 1, ty), v01 = wv(tx, ty + 1), v11 = wv(tx + 1, ty + 1);
        water.insert(water.end(), {v00, v10, v11, v00, v11, v01});
      }
  }
  c.waterVerts = static_cast<GLsizei>(water.size());
  if (!water.empty()) {
    if (!c.waterVao) {
      glGenVertexArrays(1, &c.waterVao);
      glGenBuffers(1, &c.waterVbo);
      setupVao(c.waterVao, c.waterVbo, 0);
    }
    glBindBuffer(GL_ARRAY_BUFFER, c.waterVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(water.size() * sizeof(Vertex)), water.data(),
                 GL_STATIC_DRAW);
  }
  c.bounds = b;
}

void TerrainRenderer::draw(const Camera& cam, float timeSeconds) {
  drawn_ = 0;
  if (!map_) return;
  const RectF view = cam.visibleWorld(8);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);

  terrainShader_.use();
  setCameraUniforms(terrainShader_, cam, depthMax_);
  glUniform1i(terrainShader_.uniform("uLayers"), 0);
  glUniform1i(terrainShader_.uniform("uSplat0"), 1);
  glUniform1i(terrainShader_.uniform("uSplat1"), 2);
  glUniform1i(terrainShader_.uniform("uMacro"), 3);
  glUniform2f(terrainShader_.uniform("uMapSize"), static_cast<float>(map_->width()), static_cast<float>(map_->height()));
  glUniform1f(terrainShader_.uniform("uTexScale"), kTexScale);
  layers_.bind(0);
  splat0_.bind(1);
  splat1_.bind(2);
  macro_.bind(3);
  for (const auto& c : chunks_) {
    if (!c->bounds.intersects(view)) continue;
    glBindVertexArray(c->vao);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_SHORT, nullptr);
    ++drawn_;
  }

  if (map_->waterLevel() > 0) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    waterShader_.use();
    setCameraUniforms(waterShader_, cam, depthMax_);
    glUniform1i(waterShader_.uniform("uMacro"), 0);
    glUniform1f(waterShader_.uniform("uTime"), timeSeconds);
    macro_.bind(0);
    for (const auto& c : chunks_) {
      if (!c->waterVerts || !c->bounds.intersects(view)) continue;
      glBindVertexArray(c->waterVao);
      glDrawArrays(GL_TRIANGLES, 0, c->waterVerts);
    }
  }

  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

}  // namespace slave

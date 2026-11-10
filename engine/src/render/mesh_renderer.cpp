#include "slave/render/mesh_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "slave/assets/voxel_mesh.hpp"
#include "slave/render/shadow.hpp"
#include "slave/render/terrain_renderer.hpp"

namespace slave {

namespace {

const char* kVertex = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in uvec4 aInfo;   // normal, occlusion x2, team level x100, palette index
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 iPosYaw;
layout(location = 4) in vec4 iPivot;   // pivot x, y, scale (tiles per voxel), ambient occlusion
layout(location = 5) in vec4 iMisc;    // grain, pitch, roll, ground blend (tiles)
layout(location = 6) in vec4 iTint;
layout(location = 7) in vec4 iTeam;
uniform mat4 uViewProj;
out vec3 vModel;
out vec3 vWorld;
out vec3 vNormal;
out vec4 vColor;
out vec4 vTint;
out vec3 vTeam;
flat out uvec4 vInfo;
flat out vec2 vShade;  // ambient occlusion, grain
out float vLocalZ;     // height above the model's foot, tiles
flat out float vBlend;
const vec3 kNormals[6] = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
mat3 orient() {
  float cy = cos(iPosYaw.w), sy = sin(iPosYaw.w);
  float cp = cos(iMisc.y), sp = sin(iMisc.y);
  float cr = cos(iMisc.z), sr = sin(iMisc.z);
  mat3 yaw = mat3(cy, sy, 0, -sy, cy, 0, 0, 0, 1);
  mat3 pitch = mat3(cp, 0, sp, 0, 1, 0, -sp, 0, cp);   // nose up for positive pitch
  mat3 roll = mat3(1, 0, 0, 0, cr, sr, 0, -sr, cr);
  return yaw * pitch * roll;
}
void main() {
  mat3 R = orient();
  vec3 local = vec3(aPos.xy - iPivot.xy, aPos.z) * iPivot.z;
  vWorld = iPosYaw.xyz + R * local;
  gl_Position = uViewProj * vec4(vWorld, 1.0);
  vModel = aPos;
  vNormal = R * kNormals[aInfo.x];
  vColor = aColor;
  vTint = iTint;
  vTeam = iTeam.rgb;
  vInfo = aInfo;
  vShade = vec2(iPivot.w, iMisc.x);
  vLocalZ = aPos.z * iPivot.z;
  vBlend = iMisc.w;
}
)";

const char* kFragment = R"(#version 330 core
in vec3 vModel;
in vec3 vWorld;
in vec3 vNormal;
in vec4 vColor;
in vec4 vTint;
in vec3 vTeam;
flat in uvec4 vInfo;
flat in vec2 vShade;
in float vLocalZ;
flat in float vBlend;
uniform vec3 uLight;
uniform float uGroundOn;
uniform sampler2DArray uLayers;
uniform sampler2D uSplat0;
uniform sampler2D uSplat1;
uniform sampler2D uMacro;
uniform vec2 uMapSize;
uniform float uTexScale;
uniform vec3 uTint;
uniform float uSat;
uniform vec4 uGrade;
uniform sampler2DShadow uShadowMap;
uniform mat4 uShadowMat;
uniform float uShadowOn;
uniform float uShadowDark;
out vec4 fragColor;
const vec3 kNormals[6] = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
)" SLAVE_SHADOW_GLSL R"(
// The ground's colour at a point (the terrain shader's mix, simplified).
vec3 groundColour(vec2 tile) {
  vec2 suv = tile / uMapSize;
  vec4 s0 = texture(uSplat0, suv);
  vec4 s1 = texture(uSplat1, suv);
  float w[8] = float[8](s0.r, s0.g, s0.b, s0.a, s1.r, s1.g, s1.b, s1.a);
  vec3 col = vec3(0.0);
  float total = 0.0;
  for (int i = 0; i < 8; ++i)
    if (w[i] > 0.02) {
      col += texture(uLayers, vec3(tile * uTexScale, float(i))).rgb * w[i];
      total += w[i];
    }
  col /= max(total, 1e-4);
  col *= 0.78 + 0.34 * texture(uMacro, tile / 29.0).r;
  return mix(vec3(dot(col, vec3(0.30, 0.59, 0.11))), col, uSat) * uTint;
}
float bayer4(vec2 p) {
  int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0));
  int m[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
  return (float(m[y * 4 + x]) + 0.5) / 16.0;
}
void main() {
  if (vTint.a < 0.999 && vTint.a <= bayer4(gl_FragCoord.xy)) discard;  // fading: screen-door
  // The voxel this fragment belongs to (the face sits on its outer side).
  ivec3 v = ivec3(floor(vModel - kNormals[vInfo.x] * 0.5));
  // The sprite baker's shading: sun from the upper left, darker near the
  // ground, crowded corners darker, a little grain per voxel.
  float lambert = max(0.0, dot(normalize(vNormal), uLight));
  float sun = 1.0;
  if (uShadowOn > 0.5) sun = mix(uShadowDark, 1.0, sunShadow(uShadowMap, uShadowMat, vWorld + normalize(vNormal) * 0.02));
  float shade = (0.40 + 0.66 * lambert * sun) * (0.80 + 0.20 * min(1.0, (float(v.z) + 0.5) / 3.0));
  shade *= 1.0 - vShade.x * float(vInfo.y) / 12.0;
  if (vShade.y > 0.0) {
    uint h = uint(v.x) * 73856093u ^ uint(v.y) * 19349663u ^ uint(v.z) * 83492791u ^ vInfo.w * 2654435761u;
    h ^= h >> 13u;
    h *= 0x5bd1e995u;
    h ^= h >> 15u;
    shade *= 1.0 + vShade.y * (float(h & 255u) / 255.0 - 0.5);
  }
  vec3 col;
  if (vInfo.z > 0u) {
    float b = float(vInfo.z) / 100.0 * 1.08 * shade;
    col = b <= 1.0 ? vTeam * b : mix(vTeam, vec3(1.0), clamp(b - 1.0, 0.0, 1.0));
  } else {
    col = vColor.rgb * shade;
  }
  if (uGroundOn > 0.5 && vBlend > 0.0) {
    // The ground creeps up the foot of buildings, wrecks and trees in a
    // ragged line: earth, grass, snow or sand, whatever lies there.
    float n = texture(uMacro, vWorld.xy * 1.7).r;
    float reach = vBlend * (0.45 + 1.1 * n);
    float k = 1.0 - smoothstep(reach * 0.55, reach, vLocalZ);
    if (k > 0.0) col = mix(col, groundColour(vWorld.xy) * (0.62 + 0.4 * lambert * sun), k * 0.9);
  }
  col *= vTint.rgb;
  col = mix(vec3(dot(col, vec3(0.30, 0.59, 0.11))), col, uGrade.a) * uGrade.rgb;
  fragColor = vec4(col, 1.0);
}
)";

const char* kDepthFragment = R"(#version 330 core
void main() {}
)";

constexpr int kShadowUnit = 7;

}  // namespace

MeshRenderer::~MeshRenderer() {
  for (Mesh& m : meshes_) {
    if (m.ownVbo) glDeleteBuffers(1, &m.ownVbo);
    if (m.ownVao) glDeleteVertexArrays(1, &m.ownVao);
  }
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (instanceVbo_) glDeleteBuffers(1, &instanceVbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool MeshRenderer::init(std::string& error) {
  if (!shader_.compile(kVertex, kFragment, error)) return false;
  if (!depthShader_.compile(kVertex, kDepthFragment, error)) return false;
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glGenBuffers(1, &instanceVbo_);
  vertexCapacity_ = 1 << 18;
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexCapacity_ * sizeof(MeshVertex)), nullptr, GL_STATIC_DRAW);

  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  const GLsizei vs = sizeof(MeshVertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, normal)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, r)));
  for (GLuint loc = 3; loc <= 7; ++loc) {
    glEnableVertexAttribArray(loc);
    glVertexAttribDivisor(loc, 1);
  }
  glBindVertexArray(0);
  return true;
}

void MeshRenderer::upload(const std::vector<MeshVertex>& verts, Mesh& m) {
  m.first = static_cast<GLint>(vertexCount_);
  m.count = static_cast<GLsizei>(verts.size());
  if (verts.empty()) return;
  if (vertexCount_ + verts.size() > vertexCapacity_) {
    // Grow: copy what is there into a buffer twice the size.
    size_t cap = vertexCapacity_;
    while (vertexCount_ + verts.size() > cap) cap *= 2;
    GLuint bigger = 0;
    glGenBuffers(1, &bigger);
    glBindBuffer(GL_COPY_WRITE_BUFFER, bigger);
    glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(cap * sizeof(MeshVertex)), nullptr, GL_STATIC_DRAW);
    glBindBuffer(GL_COPY_READ_BUFFER, vbo_);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0,
                        static_cast<GLsizeiptr>(vertexCount_ * sizeof(MeshVertex)));
    glDeleteBuffers(1, &vbo_);
    vbo_ = bigger;
    vertexCapacity_ = cap;
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const GLsizei vs = sizeof(MeshVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, x)));
    glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, normal)));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, r)));
    glBindVertexArray(0);
  }
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(vertexCount_ * sizeof(MeshVertex)),
                  static_cast<GLsizeiptr>(verts.size() * sizeof(MeshVertex)), verts.data());
  vertexCount_ += verts.size();
}

void MeshRenderer::setupVertexAttribs(GLuint vbo) const {
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  const GLsizei vs = sizeof(MeshVertex);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, normal)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, vs, reinterpret_cast<void*>(offsetof(MeshVertex, r)));
  for (GLuint loc = 3; loc <= 7; ++loc) {
    glEnableVertexAttribArray(loc);
    glVertexAttribDivisor(loc, 1);
  }
}

int MeshRenderer::createOwnedMesh() {
  Mesh m;
  glGenVertexArrays(1, &m.ownVao);
  glGenBuffers(1, &m.ownVbo);
  glBindVertexArray(m.ownVao);
  setupVertexAttribs(m.ownVbo);
  glBindVertexArray(0);
  meshes_.push_back(m);
  return static_cast<int>(meshes_.size() - 1);
}

void MeshRenderer::setOwnedMesh(int id, const std::vector<MeshVertex>& verts, float voxelTiles, float ao, float grain) {
  if (id < 0 || id >= static_cast<int>(meshes_.size())) return;
  Mesh& m = meshes_[static_cast<size_t>(id)];
  if (!m.ownVbo) return;
  m.first = 0;
  m.count = static_cast<GLsizei>(verts.size());
  m.voxelTiles = voxelTiles;
  m.ao = ao;
  m.grain = grain;
  m.pivotX = m.pivotY = 0;
  glBindBuffer(GL_ARRAY_BUFFER, m.ownVbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(MeshVertex)), verts.empty() ? nullptr : verts.data(),
               GL_STATIC_DRAW);
}

int MeshRenderer::meshFor(const VoxelSourcePtr& source) {
  if (!source) return -1;
  if (auto it = bySource_.find(source.get()); it != bySource_.end()) return it->second;
  Mesh m;
  m.pivotX = source->pivotX;
  m.pivotY = source->pivotY;
  m.voxelTiles = source->voxelTiles;
  m.ao = source->ao;
  m.grain = source->grain;
  m.castsShadow = source->castsShadow;
  const VoxelModel& vm = source->model;
  // Size of what is actually there (models are built in roomy grids).
  int top = 0;
  float r2 = 0;
  for (int z = 0; z < vm.sizeZ(); ++z)
    for (int y = 0; y < vm.sizeY(); ++y)
      for (int x = 0; x < vm.sizeX(); ++x)
        if (vm.get(x, y, z)) {
          top = std::max(top, z + 1);
          const float dx = std::max(std::abs(x - m.pivotX), std::abs(x + 1 - m.pivotX));
          const float dy = std::max(std::abs(y - m.pivotY), std::abs(y + 1 - m.pivotY));
          r2 = std::max(r2, dx * dx + dy * dy);
        }
  m.height = top * m.voxelTiles;
  m.radius = std::sqrt(r2) * m.voxelTiles;
  upload(meshVoxels(vm, source->palette), m);
  const int id = static_cast<int>(meshes_.size());
  meshes_.push_back(m);
  // Two coarser levels for the distance (only worth it for bigger models).
  int prev = id;
  VoxelModel coarse = vm;
  for (int level = 1; level <= 2 && meshes_[static_cast<size_t>(prev)].count > 600; ++level) {
    coarse = halveVoxels(coarse);
    Mesh c = meshes_[static_cast<size_t>(prev)];
    c.pivotX *= 0.5f;
    c.pivotY *= 0.5f;
    c.voxelTiles *= 2.0f;
    c.coarser = -1;
    upload(meshVoxels(coarse, source->palette), c);
    if (c.count == 0) break;
    meshes_[static_cast<size_t>(prev)].coarser = static_cast<int>(meshes_.size());
    prev = static_cast<int>(meshes_.size());
    meshes_.push_back(c);
  }
  bySource_[source.get()] = id;
  keepAlive_.push_back(source);
  return id;
}

void MeshRenderer::add(int mesh, const MeshInstance& in) {
  if (mesh < 0 || mesh >= static_cast<int>(meshes_.size())) return;
  if (view_) {
    // A voxel should cover at least about a pixel and a half; else go coarser.
    const float ppt = view_->pixelsPerTileAt(in.pos) * in.scale;
    while (meshes_[static_cast<size_t>(mesh)].coarser >= 0 && ppt * meshes_[static_cast<size_t>(mesh)].voxelTiles < 1.5f)
      mesh = meshes_[static_cast<size_t>(mesh)].coarser;
  }
  const Mesh& m = meshes_[static_cast<size_t>(mesh)];
  if (m.count == 0 || (in.tint >> 24) == 0) return;
  Queued q;
  q.mesh = mesh;
  q.inst = {in.pos.x, in.pos.y, in.pos.z, in.yaw, m.pivotX, m.pivotY, m.voxelTiles * in.scale, m.ao,
            m.grain, in.pitch, in.roll, in.blend, in.tint, in.team};
  queue_.push_back(q);
  prepared_ = false;
}

void MeshRenderer::prepare() {
  if (prepared_) return;
  prepared_ = true;
  std::stable_sort(queue_.begin(), queue_.end(), [](const Queued& a, const Queued& b) { return a.mesh < b.mesh; });
  staged_.clear();
  batches_.clear();
  for (size_t i = 0; i < queue_.size(); ++i) {
    if (batches_.empty() || batches_.back().first != queue_[i].mesh) batches_.push_back({queue_[i].mesh, {i, 0}});
    ++batches_.back().second.second;
    staged_.push_back(queue_[i].inst);
  }
  glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
  if (staged_.size() > instanceCapacity_) {
    instanceCapacity_ = std::max<size_t>(staged_.size() * 3 / 2, 1024);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instanceCapacity_ * sizeof(GpuInstance)), nullptr, GL_STREAM_DRAW);
  }
  if (!staged_.empty())
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(staged_.size() * sizeof(GpuInstance)), staged_.data());
}

void MeshRenderer::drawBatches(const gl::Shader&, bool shadowPass) {
  glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
  const GLsizei is = sizeof(GpuInstance);
  GLuint bound = 0;
  for (const auto& [mesh, range] : batches_) {
    const Mesh& m = meshes_[static_cast<size_t>(mesh)];
    if (shadowPass && !m.castsShadow) continue;
    const GLuint want = m.ownVao ? m.ownVao : vao_;
    if (want != bound) {
      glBindVertexArray(want);
      glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
      bound = want;
    }
    const size_t base = range.first * sizeof(GpuInstance);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, is, reinterpret_cast<void*>(base + offsetof(GpuInstance, x)));
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, is, reinterpret_cast<void*>(base + offsetof(GpuInstance, pivotX)));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, is, reinterpret_cast<void*>(base + offsetof(GpuInstance, grain)));
    glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, is, reinterpret_cast<void*>(base + offsetof(GpuInstance, tint)));
    glVertexAttribPointer(7, 4, GL_UNSIGNED_BYTE, GL_TRUE, is, reinterpret_cast<void*>(base + offsetof(GpuInstance, team)));
    glDrawArraysInstanced(GL_TRIANGLES, m.first, m.count, static_cast<GLsizei>(range.second));
    if (!shadowPass) {
      drawnInstances_ += range.second;
      drawnTriangles_ += static_cast<size_t>(m.count / 3) * range.second;
    }
  }
  glBindVertexArray(0);
}

void MeshRenderer::draw(const Camera& cam, const ShadowMap* shadow) {
  drawnInstances_ = drawnTriangles_ = 0;
  if (queue_.empty()) return;
  prepare();
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  shader_.use();
  glUniformMatrix4fv(shader_.uniform("uViewProj"), 1, GL_FALSE, cam.viewProj().m);
  const Vec3f L = sunDirection();
  glUniform3f(shader_.uniform("uLight"), L.x, L.y, L.z);
  glUniform4f(shader_.uniform("uGrade"), grade_[0], grade_[1], grade_[2], grade_[3]);
  // Every sampler keeps its own unit, used or not (two sampler types on one unit is an error).
  glUniform1i(shader_.uniform("uLayers"), 8);
  glUniform1i(shader_.uniform("uSplat0"), 9);
  glUniform1i(shader_.uniform("uSplat1"), 10);
  glUniform1i(shader_.uniform("uMacro"), 11);
  glUniform1f(shader_.uniform("uGroundOn"), ground_ && ground_->hasMap() ? 1.0f : 0.0f);
  if (ground_ && ground_->hasMap()) ground_->bindGround(shader_, 8);
  if (shadow) shadow->setUniforms(shader_, kShadowUnit, true, 0.55f);
  else {
    glUniform1i(shader_.uniform("uShadowMap"), kShadowUnit);
    glUniform1f(shader_.uniform("uShadowOn"), 0.0f);
  }
  drawBatches(shader_, false);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void MeshRenderer::drawDepth(const Mat4& vp) {
  if (queue_.empty()) return;
  prepare();
  depthShader_.use();
  glUniformMatrix4fv(depthShader_.uniform("uViewProj"), 1, GL_FALSE, vp.m);
  drawBatches(depthShader_, true);
}

}  // namespace slave

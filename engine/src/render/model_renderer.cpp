// Pioneer Engine - drawing one glTF model for the model viewer. See model_renderer.hpp.
#include "pioneer/render/model_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "pioneer/render/graphics.hpp"

namespace pioneer {

namespace {

const char* kMeshVertex = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aColor;
uniform mat4 uViewProj;
out vec3 vPos;
out vec3 vNormal;
out vec4 vColor;
void main() {
  vPos = aPos;
  vNormal = aNormal;
  vColor = aColor;
  gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

// Team paint: the vertex colour's grey is the brightness relative to the team
// colour (voxelModelAsset writes 200/255 for "exactly the team colour").
const char* kMeshFragment = R"(#version 330 core
in vec3 vPos;
in vec3 vNormal;
in vec4 vColor;
uniform vec4 uBase;
uniform vec3 uEmissive;
uniform int uTeam;
uniform vec3 uTeamColour;
uniform vec3 uLight;
uniform vec3 uEye;
uniform float uMetal;
uniform float uRough;
out vec4 fragColor;
void main() {
  vec3 n = normalize(vNormal);
  if (!gl_FrontFacing) n = -n;
  vec3 albedo = vColor.rgb * uBase.rgb;
  if (uTeam == 1) albedo = uTeamColour * (vColor.r * 1.275) * uBase.rgb;
  float diffuse = max(dot(n, uLight), 0.0);
  float sky = 0.5 + 0.5 * n.z;
  vec3 h = normalize(uLight + normalize(uEye - vPos));
  float shine = mix(8.0, 64.0, 1.0 - uRough);
  float spec = pow(max(dot(n, h), 0.0), shine) * mix(0.04, 0.5, uMetal) * (1.0 - 0.6 * uRough);
  vec3 c = albedo * (0.22 + 0.18 * sky + 0.78 * diffuse) + vec3(spec) + uEmissive;
  fragColor = vec4(c, uBase.a * vColor.a);
}
)";

const char* kLineVertex = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() {
  vColor = aColor;
  gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

const char* kLineFragment = R"(#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)";

// 7 floats a point: x y z r g b a.
void line(std::vector<float>& out, Vec3f a, Vec3f b, float r, float g, float bl, float al) {
  for (const Vec3f& p : {a, b}) out.insert(out.end(), {p.x, p.y, p.z, r, g, bl, al});
}

}  // namespace

ModelRenderer::~ModelRenderer() {
  release();
  if (lineVbo_) glDeleteBuffers(1, &lineVbo_);
  if (lineVao_) glDeleteVertexArrays(1, &lineVao_);
}

bool ModelRenderer::init(std::string& error) {
  const gfx::Api api = gfx::caps().api;
  if (!mesh_.compile(gfx::shaderSource(kMeshVertex, api).c_str(), gfx::shaderSource(kMeshFragment, api).c_str(), error)) return false;
  if (!line_.compile(gfx::shaderSource(kLineVertex, api).c_str(), gfx::shaderSource(kLineFragment, api).c_str(), error)) return false;
  glGenVertexArrays(1, &lineVao_);
  glGenBuffers(1, &lineVbo_);
  glBindVertexArray(lineVao_);
  glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
  glBindVertexArray(0);
  ready_ = true;
  return true;
}

void ModelRenderer::release() {
  for (GpuMesh& g : meshes_) {
    if (g.ebo) glDeleteBuffers(1, &g.ebo);
    if (g.vbo) glDeleteBuffers(1, &g.vbo);
    if (g.vao) glDeleteVertexArrays(1, &g.vao);
  }
  meshes_.clear();
}

void ModelRenderer::upload(GpuMesh& g, const std::vector<MeshVertexFull>& verts, bool dynamic) {
  std::vector<Vertex> packed(verts.size());
  for (size_t i = 0; i < verts.size(); ++i) {
    std::copy(verts[i].pos, verts[i].pos + 3, packed[i].pos);
    std::copy(verts[i].normal, verts[i].normal + 3, packed[i].normal);
    std::copy(verts[i].color, verts[i].color + 4, packed[i].color);
  }
  glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
  const GLsizeiptr bytes = static_cast<GLsizeiptr>(packed.size() * sizeof(Vertex));
  if (dynamic) glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, packed.data());
  else glBufferData(GL_ARRAY_BUFFER, bytes, packed.data(), g.skinned ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
}

void ModelRenderer::setModel(std::shared_ptr<const ModelAsset> model) {
  if (!ready_) return;
  release();
  model_ = std::move(model);
  if (!model_) return;
  GLint prevVao = 0;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
  for (const MeshPart& p : model_->meshes) {
    GpuMesh g;
    g.material = p.material;
    g.skinned = p.skinned;
    g.indices = static_cast<GLsizei>(p.indices.size());
    glGenVertexArrays(1, &g.vao);
    glGenBuffers(1, &g.vbo);
    glGenBuffers(1, &g.ebo);
    glBindVertexArray(g.vao);
    upload(g, p.vertices, false);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(p.indices.size() * sizeof(uint32_t)), p.indices.data(),
                 GL_STATIC_DRAW);
    meshes_.push_back(g);
  }
  glBindVertexArray(static_cast<GLuint>(prevVao));
}

void ModelRenderer::setPose(const std::vector<std::vector<MeshVertexFull>>& posed) {
  if (!ready_ || !model_) return;
  for (size_t i = 0; i < meshes_.size() && i < posed.size(); ++i) {
    if (!meshes_[i].skinned || posed[i].size() != model_->meshes[i].vertices.size()) continue;
    upload(meshes_[i], posed[i], true);
  }
}

void ModelRenderer::drawLines(const std::vector<float>& lines, const Mat4& viewProj, bool depthTest) {
  if (lines.empty()) return;
  glBindVertexArray(lineVao_);
  glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
  const size_t bytes = lines.size() * sizeof(float);
  if (bytes > lineCapacity_) {
    lineCapacity_ = std::max(bytes, lineCapacity_ * 2);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lineCapacity_), nullptr, GL_STREAM_DRAW);
  }
  glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), lines.data());
  line_.use();
  glUniformMatrix4fv(line_.uniform("uViewProj"), 1, GL_FALSE, viewProj.m);
  if (depthTest) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST);
  glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size() / 7));
}

GLuint ModelRenderer::render(int w, int h, const tools::OrbitCamera& cam, const tools::ModelView::Options& opt,
                             const std::vector<tools::Bone>& bones) {
  if (!ready_ || w < 8 || h < 8) return 0;
  // What we change, to put back afterwards (ImGui is in the middle of a frame).
  GLint prevFbo = 0, prevViewport[4] = {0, 0, 0, 0}, prevProgram = 0, prevVao = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glGetIntegerv(GL_VIEWPORT, prevViewport);
  glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
  const GLboolean depthWas = glIsEnabled(GL_DEPTH_TEST), blendWas = glIsEnabled(GL_BLEND), cullWas = glIsEnabled(GL_CULL_FACE),
                  scissorWas = glIsEnabled(GL_SCISSOR_TEST);

  if (target_.width() != w || target_.height() != h || !target_.ready())
    if (!target_.create(w, h, gfx::Format::RGBA8, true, true)) return 0;
  target_.bind();
  glDisable(GL_SCISSOR_TEST);
  glClearColor(0.16f, 0.17f, 0.18f, 1.0f);
  glClearDepth(1.0);
  glDepthMask(GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  const Mat4 viewProj = cam.projection(static_cast<float>(w) / static_cast<float>(h)) * cam.view();
  const Vec3f eye = cam.eye();
  triangles_ = 0;

  // The ground grid, one line per step, round the target.
  std::vector<float> lines;
  if (opt.grid) {
    const float span = std::max(2.0f, cam.distance * 1.5f);
    const float step = span > 40 ? 10.0f : span > 8 ? 1.0f : 0.25f;
    const int n = static_cast<int>(std::ceil(span / step));
    const float cx = std::round(cam.target.x / step) * step, cy = std::round(cam.target.y / step) * step;
    for (int i = -n; i <= n; ++i) {
      const float k = static_cast<float>(i) * step;
      const bool axisX = std::fabs(cy + k) < step * 0.01f, axisY = std::fabs(cx + k) < step * 0.01f;
      line(lines, {cx - n * step, cy + k, 0}, {cx + n * step, cy + k, 0}, axisX ? 0.75f : 0.4f, 0.4f, 0.4f, axisX ? 0.9f : 0.45f);
      line(lines, {cx + k, cy - n * step, 0}, {cx + k, cy + n * step, 0}, 0.4f, axisY ? 0.75f : 0.4f, 0.4f, axisY ? 0.9f : 0.45f);
    }
  }
  drawLines(lines, viewProj, true);

  if (model_) {
    mesh_.use();
    glUniformMatrix4fv(mesh_.uniform("uViewProj"), 1, GL_FALSE, viewProj.m);
    const Vec3f light = Vec3f{-0.45f, -0.55f, 0.7f}.normalized();
    glUniform3f(mesh_.uniform("uLight"), light.x, light.y, light.z);
    glUniform3f(mesh_.uniform("uEye"), eye.x, eye.y, eye.z);
    const float tr = static_cast<float>(opt.team & 0xFF) / 255.0f, tg = static_cast<float>((opt.team >> 8) & 0xFF) / 255.0f,
                tb = static_cast<float>((opt.team >> 16) & 0xFF) / 255.0f;
    glUniform3f(mesh_.uniform("uTeamColour"), tr, tg, tb);
    const GLint uBase = mesh_.uniform("uBase"), uEm = mesh_.uniform("uEmissive"), uTeam = mesh_.uniform("uTeam"),
                uMetal = mesh_.uniform("uMetal"), uRough = mesh_.uniform("uRough");
#ifdef GL_LINE
    const bool wire = opt.wireframe && gfx::caps().api == gfx::Api::GL33;
    if (wire) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif
    for (const GpuMesh& g : meshes_) {
      Material m;
      if (g.material >= 0 && g.material < static_cast<int>(model_->materials.size())) m = model_->materials[static_cast<size_t>(g.material)];
      glUniform4f(uBase, m.baseColor[0], m.baseColor[1], m.baseColor[2], m.baseColor[3]);
      glUniform3f(uEm, m.emissive[0], m.emissive[1], m.emissive[2]);
      glUniform1i(uTeam, m.team ? 1 : 0);
      glUniform1f(uMetal, m.metallic);
      glUniform1f(uRough, m.roughness);
      glBindVertexArray(g.vao);
      glDrawElements(GL_TRIANGLES, g.indices, GL_UNSIGNED_INT, nullptr);
      triangles_ += static_cast<size_t>(g.indices / 3);
    }
#ifdef GL_LINE
    if (wire) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
    lines.clear();
    if (opt.bounds && !model_->meshes.empty()) {
      float lo[3], hi[3];
      model_->bounds(lo, hi);
      const Vec3f c[8] = {{lo[0], lo[1], lo[2]}, {hi[0], lo[1], lo[2]}, {hi[0], hi[1], lo[2]}, {lo[0], hi[1], lo[2]},
                          {lo[0], lo[1], hi[2]}, {hi[0], lo[1], hi[2]}, {hi[0], hi[1], hi[2]}, {lo[0], hi[1], hi[2]}};
      for (int i = 0; i < 4; ++i) {
        line(lines, c[i], c[(i + 1) % 4], 0.3f, 0.8f, 1.0f, 0.8f);
        line(lines, c[4 + i], c[4 + (i + 1) % 4], 0.3f, 0.8f, 1.0f, 0.8f);
        line(lines, c[i], c[4 + i], 0.3f, 0.8f, 1.0f, 0.8f);
      }
    }
    drawLines(lines, viewProj, true);
    // Bones over everything, so they show through the body.
    lines.clear();
    if (opt.skeleton) {
      const float tick = std::max(0.01f, cam.distance * 0.008f);
      for (const tools::Bone& b : bones) {
        line(lines, b.from, b.to, 1.0f, 0.85f, 0.2f, 1.0f);
        for (const Vec3f& d : {Vec3f{tick, 0, 0}, Vec3f{0, tick, 0}, Vec3f{0, 0, tick}}) line(lines, b.to - d, b.to + d, 1.0f, 0.4f, 0.2f, 1.0f);
      }
    }
    drawLines(lines, viewProj, false);
  }

  // Back as we found it.
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
  glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
  glUseProgram(static_cast<GLuint>(prevProgram));
  glBindVertexArray(static_cast<GLuint>(prevVao));
  if (depthWas) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST);
  if (blendWas) glEnable(GL_BLEND);
  else glDisable(GL_BLEND);
  if (cullWas) glEnable(GL_CULL_FACE);
  else glDisable(GL_CULL_FACE);
  if (scissorWas) glEnable(GL_SCISSOR_TEST);
  return target_.colour();
}

bool ModelRenderer::readPixels(Image& out) const {
  if (!target_.ready()) return false;
  GLint prevFbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glBindFramebuffer(GL_FRAMEBUFFER, target_.fbo());
  const int w = target_.width(), h = target_.height();
  std::vector<Rgba> px(static_cast<size_t>(w) * h);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
  out = Image(w, h);
  for (int y = 0; y < h; ++y)  // GL's rows start at the bottom
    std::copy(px.begin() + static_cast<long>(y) * w, px.begin() + static_cast<long>(y + 1) * w,
              out.px.begin() + static_cast<long>(h - 1 - y) * w);
  return true;
}

}  // namespace pioneer

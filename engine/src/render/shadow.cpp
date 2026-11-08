#include "slave/render/shadow.hpp"

#include <algorithm>
#include <cmath>

namespace slave {

ShadowMap::~ShadowMap() {
  if (depth_) glDeleteTextures(1, &depth_);
  if (fbo_) glDeleteFramebuffers(1, &fbo_);
}

bool ShadowMap::init(int size, std::string& error) {
  size_ = size;
  glGenTextures(1, &depth_);
  glBindTexture(GL_TEXTURE_2D, depth_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  glGenFramebuffers(1, &fbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    error = "shadow map framebuffer incomplete";
    glDeleteFramebuffers(1, &fbo_);
    glDeleteTextures(1, &depth_);
    fbo_ = depth_ = 0;
    return false;
  }
  return true;
}

void ShadowMap::fit(const Camera& cam, float minZ, float maxZ, float reach) {
  // Where the screen's corners and edge midpoints meet the ground (a plane at
  // the target's height), kept within `reach` of the target.
  const Vec3f t = cam.target();
  Vec3f pts[16];
  int n = 0;
  const float w = static_cast<float>(cam.viewportW()), h = static_cast<float>(cam.viewportH());
  const Vec2f px[8] = {{0, 0}, {w * 0.5f, 0}, {w, 0}, {w, h * 0.5f}, {w, h}, {w * 0.5f, h}, {0, h}, {0, h * 0.5f}};
  for (const Vec2f& p : px) {
    Vec3f o, d;
    cam.ray(p, o, d);
    Vec3f g;
    if (d.z < -1e-3f) {
      const float k = (t.z - o.z) / d.z;
      g = o + d * k;
    } else {
      g = o + d * reach * 4.0f;
    }
    Vec3f off{g.x - t.x, g.y - t.y, 0};
    const float len = off.length();
    if (len > reach) off = off * (reach / len);
    pts[n++] = Vec3f{t.x + off.x, t.y + off.y, minZ};
    pts[n++] = Vec3f{t.x + off.x, t.y + off.y, maxZ};
  }
  const Vec3f L = sunDirection();
  const Mat4 view = Mat4::lookAt(t + L * 200.0f, t, Vec3f{0, 0, 1});
  float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
  for (int i = 0; i < n; ++i) {
    const Vec4f v = view * Vec4f{pts[i].x, pts[i].y, pts[i].z, 1};
    x0 = std::min(x0, v.x);
    x1 = std::max(x1, v.x);
    y0 = std::min(y0, v.y);
    y1 = std::max(y1, v.y);
    z0 = std::min(z0, v.z);
    z1 = std::max(z1, v.z);
  }
  // Snap the box to whole texels so shadows don't crawl as the camera moves.
  const float span = std::max(x1 - x0, y1 - y0) + 2.0f;
  const float texel = span / static_cast<float>(std::max(1, size_));
  const float cx = std::floor((x0 + x1) * 0.5f / texel) * texel, cy = std::floor((y0 + y1) * 0.5f / texel) * texel;
  const Mat4 proj = Mat4::ortho(cx - span * 0.5f, cx + span * 0.5f, cy - span * 0.5f, cy + span * 0.5f,
                                -z1 - 40.0f, -z0 + 40.0f);
  lightViewProj_ = proj * view;
}

void ShadowMap::begin() {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, size_, size_);
  glClearDepth(1.0);
  glClear(GL_DEPTH_BUFFER_BIT);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(1.6f, 3.0f);
}

void ShadowMap::end(int viewportW, int viewportH) {
  glDisable(GL_POLYGON_OFFSET_FILL);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, viewportW, viewportH);
}

void ShadowMap::bind(int unit) const {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, depth_);
  glActiveTexture(GL_TEXTURE0);
}

void ShadowMap::setUniforms(const gl::Shader& s, int unit, bool on, float dark) const {
  glUniform1i(s.uniform("uShadowMap"), unit);
  glUniformMatrix4fv(s.uniform("uShadowMat"), 1, GL_FALSE, lightViewProj_.m);
  glUniform1f(s.uniform("uShadowOn"), on && fbo_ ? 1.0f : 0.0f);
  glUniform1f(s.uniform("uShadowDark"), dark);
  if (on && fbo_) bind(unit);
}

}  // namespace slave

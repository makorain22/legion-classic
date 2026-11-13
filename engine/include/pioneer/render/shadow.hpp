#pragma once
// Pioneer Engine - the sun's shadow map for the 3D view.
//
// One directional light (the same sun the sprites were baked under: from the
// upper left of the classic view, high in the sky). Each frame the light's
// box is fitted over the ground the camera sees, terrain and models are drawn
// into a depth texture from the sun, and the terrain and model shaders look it
// up with a small filter so edges stay soft.

#include <string>

#include "pioneer/math/mat.hpp"
#include "pioneer/render/camera.hpp"
#include "pioneer/render/gl.hpp"

// GLSL shared by every shader that receives shadows: 1 = in sunlight, 0 = shadowed.
#define PIONEER_SHADOW_GLSL                                                                   \
  "float sunShadow(sampler2DShadow map, mat4 m, vec3 w) {\n"                                \
  "  vec4 p = m * vec4(w, 1.0);\n"                                                          \
  "  vec3 c = p.xyz / p.w * 0.5 + 0.5;\n"                                                   \
  "  if (c.x <= 0.0 || c.y <= 0.0 || c.x >= 1.0 || c.y >= 1.0 || c.z >= 1.0) return 1.0;\n" \
  "  vec2 t = 1.0 / vec2(textureSize(map, 0));\n"                                           \
  "  float s = 0.0;\n"                                                                      \
  "  for (int y = -1; y <= 1; ++y)\n"                                                       \
  "    for (int x = -1; x <= 1; ++x) s += texture(map, vec3(c.xy + vec2(x, y) * t * 1.2, c.z - 0.0008));\n" \
  "  return s / 9.0;\n"                                                                     \
  "}\n"

namespace pioneer {

// Direction towards the sun, in world space (tiles; z up). Matches the light
// the voxel baker and the classic terrain use.
inline Vec3f sunDirection() { return Vec3f{-0.35f, 0.45f, 0.80f}.normalized(); }

class ShadowMap {
 public:
  ShadowMap() = default;
  ~ShadowMap();
  ShadowMap(const ShadowMap&) = delete;
  ShadowMap& operator=(const ShadowMap&) = delete;

  bool init(int size, std::string& error);
  bool ready() const { return fbo_ != 0; }
  int size() const { return size_; }

  // Fits the sun's view over what the camera sees on the ground (whose
  // heights run from minZ to maxZ, in tiles), out to `reach` tiles from the
  // camera's target.
  void fit(const Camera& cam, float minZ, float maxZ, float reach);
  // Renders into the map until end(): bind, set the viewport, clear.
  void begin();
  void end(int viewportW, int viewportH);

  const Mat4& matrix() const { return lightViewProj_; }  // world -> the sun's clip space
  void bind(int unit) const;
  // Sets uShadowMap / uShadowMat / uShadowOn / uShadowDark on a shader in use.
  void setUniforms(const gl::Shader& s, int unit, bool on, float dark) const;

 private:
  GLuint fbo_ = 0, depth_ = 0;
  int size_ = 0;
  Mat4 lightViewProj_;
};

}  // namespace pioneer

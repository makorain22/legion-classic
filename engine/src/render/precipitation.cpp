#include "pioneer/render/precipitation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pioneer {

namespace {

const char* kVertex = R"(#version 330 core
layout(location = 0) in vec2 aCorner;  // x -0.5 .. 0.5 across, y 0 .. 1 along
layout(location = 1) in vec4 aSeed;    // xyz: its place in the box (0..1); w: its own speed and sway
uniform mat4 uViewProj;
uniform vec3 uOrigin;    // the box's lowest corner (world, tiles)
uniform float uSize;     // its edge
uniform vec3 uVelocity;  // tiles a second
uniform float uTime;
uniform vec3 uEye;
uniform vec3 uRight;
uniform vec3 uUp;
uniform float uLength;   // rain: the streak's length; snow: the flake's size
uniform float uWidth;
uniform float uNear;     // closer than this to the eye: faded out
uniform float uSnow;     // 1: flakes that sway, 0: streaks
out vec2 vCorner;
out float vFade;
void main() {
  float speed = 0.85 + 0.3 * aSeed.w;
  vec3 p = aSeed.xyz * uSize + uVelocity * speed * uTime;
  if (uSnow > 0.5) {
    float t = uTime * (0.9 + 0.5 * aSeed.w) + aSeed.w * 6.2832;
    p.xy += vec2(sin(t), cos(t * 0.8)) * uSize * 0.012;
  }
  // Wrapped into the box: the drops stay put in the world as the box moves.
  vec3 f = mod(p - uOrigin, uSize) / uSize;
  vec3 world = uOrigin + f * uSize;
  vec3 edge = min(f, 1.0 - f);
  vFade = smoothstep(0.0, 0.12, min(edge.x, min(edge.y, edge.z))) *
          smoothstep(uNear, uNear * 2.0, length(world - uEye));
  vec3 pos;
  if (uSnow > 0.5) {
    pos = world + uRight * aCorner.x * uLength + uUp * (aCorner.y - 0.5) * uLength;
  } else {
    vec3 axis = normalize(uVelocity);
    vec3 side = normalize(cross(axis, uEye - world));
    pos = world + side * aCorner.x * uWidth + axis * (aCorner.y - 0.5) * uLength;
  }
  vCorner = aCorner;
  gl_Position = uViewProj * vec4(pos, 1.0);
}
)";

const char* kFragment = R"(#version 330 core
in vec2 vCorner;
in float vFade;
uniform vec3 uLight;
uniform float uSnow;
uniform float uAlpha;
out vec4 fragColor;
void main() {
  float a;
  vec3 c;
  if (uSnow > 0.5) {
    float r = length(vec2(vCorner.x, vCorner.y - 0.5)) * 2.0;
    a = smoothstep(1.0, 0.35, r);
    c = vec3(0.96, 0.97, 1.0);
  } else {
    float across = 1.0 - (2.0 * vCorner.x) * (2.0 * vCorner.x);
    a = across * smoothstep(0.0, 0.35, vCorner.y) * smoothstep(1.0, 0.65, vCorner.y);
    c = vec3(0.72, 0.77, 0.84);
  }
  a *= vFade * uAlpha;
  if (a < 0.004) discard;
  fragColor = vec4(c * uLight, a);
}
)";

}  // namespace

Precipitation::~Precipitation() {
  if (corners_) glDeleteBuffers(1, &corners_);
  if (seeds_) glDeleteBuffers(1, &seeds_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool Precipitation::init(std::string& error) {
  if (!shader_.compile(kVertex, kFragment, error)) return false;
  const float corners[8] = {-0.5f, 0.0f, 0.5f, 0.0f, -0.5f, 1.0f, 0.5f, 1.0f};
  // The same drops every time (a fixed seed): nothing here is random from run to run.
  std::vector<float> seeds(static_cast<size_t>(kMaxDrops) * 4);
  uint32_t s = 0x2545F491u;
  for (float& v : seeds) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    v = static_cast<float>(s & 0xFFFFFF) / 16777216.0f;
  }
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &corners_);
  glGenBuffers(1, &seeds_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, corners_);
  glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
  glBindBuffer(GL_ARRAY_BUFFER, seeds_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(seeds.size() * sizeof(float)), seeds.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glVertexAttribDivisor(1, 1);
  glBindVertexArray(0);
  return true;
}

int Precipitation::dropsFor(const PrecipitationLook& look) {
  if (look.detail <= 0) return 0;
  const float share = look.detail == 1 ? 0.35f : 1.0f;
  const float amount = std::clamp(std::max(look.rain, look.snow * 0.8f), 0.0f, 1.0f);
  return static_cast<int>(static_cast<float>(kMaxDrops) * share * amount);
}

void Precipitation::draw(const Camera& cam, const PrecipitationLook& look, float seconds) {
  drawn_ = 0;
  if (!vao_ || look.detail <= 0 || (look.rain <= 0.0f && look.snow <= 0.0f)) return;
  // Rain and snow can fall together while the weather turns from one to the other.
  const int total = dropsFor(look);
  const float sum = std::max(1e-3f, look.rain + look.snow);
  const int rain = static_cast<int>(static_cast<float>(total) * look.rain / sum);
  const int snow = total - rain;
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  shader_.use();
  glBindVertexArray(vao_);
  if (rain > 0) drawLayer(cam, look, seconds, false, rain);
  if (snow > 0) drawLayer(cam, look, seconds, true, snow);
  glBindVertexArray(0);
  glDepthMask(GL_TRUE);
}

void Precipitation::drawLayer(const Camera& cam, const PrecipitationLook& look, float seconds, bool snow, int count) {
  // The box sits between the eye and what it looks at, as big as the view is
  // far: near the eye the drops would be huge, far off too small to see.
  const Vec3f eye = cam.eye(), target = cam.target();
  const Vec3f toTarget = target - eye;
  const float dist = std::sqrt(dot(toTarget, toTarget));
  const float size = std::clamp(dist * 0.8f, 12.0f, 140.0f);
  const Vec3f centre = eye + toTarget * 0.5f;
  const Vec3f origin = centre - Vec3f{size, size, size} * 0.5f;
  const float amount = snow ? look.snow : look.rain;
  Vec3f vel;
  float length, width, alpha;
  if (snow) {
    vel = Vec3f{look.wind.x * 0.03f * size, look.wind.y * 0.03f * size, -0.12f * size};  // a blizzard drives it sideways
    length = size * (0.0020f + 0.0010f * amount);
    width = length;
    alpha = 0.45f + 0.30f * amount;
  } else {
    vel = Vec3f{look.wind.x * 0.05f * size, look.wind.y * 0.05f * size, -0.9f * size};  // a storm slants it
    const float speed = std::sqrt(dot(vel, vel));
    length = speed * 0.035f;
    width = size * 0.0016f;
    alpha = 0.22f + 0.18f * amount;
  }
  const Vec3f r = cam.right(), u = cam.up();
  glUniformMatrix4fv(shader_.uniform("uViewProj"), 1, GL_FALSE, cam.viewProj().m);
  glUniform3f(shader_.uniform("uOrigin"), origin.x, origin.y, origin.z);
  glUniform1f(shader_.uniform("uSize"), size);
  glUniform3f(shader_.uniform("uVelocity"), vel.x, vel.y, vel.z);
  // Kept well inside float precision however long the battle runs (the drops shift once an hour).
  glUniform1f(shader_.uniform("uTime"), std::fmod(seconds, 3600.0f));
  glUniform3f(shader_.uniform("uEye"), eye.x, eye.y, eye.z);
  glUniform3f(shader_.uniform("uRight"), r.x, r.y, r.z);
  glUniform3f(shader_.uniform("uUp"), u.x, u.y, u.z);
  glUniform1f(shader_.uniform("uLength"), length);
  glUniform1f(shader_.uniform("uWidth"), width);
  glUniform1f(shader_.uniform("uNear"), dist * (snow ? 0.22f : 0.12f));
  glUniform1f(shader_.uniform("uSnow"), snow ? 1.0f : 0.0f);
  glUniform3f(shader_.uniform("uLight"), look.light[0], look.light[1], look.light[2]);
  glUniform1f(shader_.uniform("uAlpha"), alpha);
  glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
  drawn_ += count;
}

}  // namespace pioneer

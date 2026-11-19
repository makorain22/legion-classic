#include "pioneer/render/post.hpp"

#include <algorithm>
#include <cmath>

#include "pioneer/core/log.hpp"

namespace pioneer {

namespace {
// --- Light: tint, saturation, clouds' shadows, mist -------------------------------------
const char* kLightFragment = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform mat4 uInvViewProj;
uniform vec3 uEye;
uniform vec3 uLight;
uniform float uSaturation;
uniform vec3 uFog;
uniform vec4 uFogParams;   // density, ground fog, base height, height it thins over
uniform float uSkyFog;
uniform vec4 uCloud;       // strength, 1 / size, offset x, offset y
out vec4 fragColor;

float hash(vec2 p) {
  p = fract(p * vec2(0.3183099, 0.3678794)) * 50.0;
  return fract(p.x * p.y * (p.x + p.y));
}
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
float clouds(vec2 p) {
  return noise(p) * 0.55 + noise(p * 2.13 + 7.1) * 0.30 + noise(p * 4.37 + 3.7) * 0.15;
}

void main() {
  vec3 c = texture(uScene, vUV).rgb;
  float d = texture(uDepth, vUV).r;
  bool sky = d >= 0.99999;
  vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  vec3 world = w.xyz / w.w;

  if (!sky && uCloud.x > 0.0) {
    float n = clouds(world.xy * uCloud.y + uCloud.zw);
    c *= mix(1.0, mix(0.58, 1.0, smoothstep(0.38, 0.62, n)), uCloud.x);
  }
  float lum = dot(c, vec3(0.30, 0.59, 0.11));
  c = mix(vec3(lum), c, uSaturation) * uLight;

  if (sky) {
    c = mix(c, uFog, uSkyFog);
  } else if (uFogParams.x > 0.0 || uFogParams.y > 0.0) {
    float dist = length(world - uEye);
    float low = exp(-max(world.z - uFogParams.z, 0.0) / max(uFogParams.w, 0.05));
    float f = 1.0 - exp(-dist * (uFogParams.x + uFogParams.y * low));
    c = mix(c, uFog, clamp(f, 0.0, 1.0));
  }
  fragColor = vec4(c, 1.0);
}
)";

// --- Glow: what is brighter than white, blurred ------------------------------------------
const char* kBrightFragment = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform vec2 uTexel;
out vec4 fragColor;
vec3 bright(vec2 uv) {
  vec3 c = texture(uScene, uv).rgb;
  float m = max(c.r, max(c.g, c.b));
  // Only what is over white glows, coming in gently from 0.9.
  float k = clamp((m - 0.9) / 0.35, 0.0, 1.0);
  return c * k * k;
}
void main() {
  vec3 c = bright(vUV + uTexel * vec2(-1.0, -1.0)) + bright(vUV + uTexel * vec2(1.0, -1.0)) +
           bright(vUV + uTexel * vec2(-1.0, 1.0)) + bright(vUV + uTexel * vec2(1.0, 1.0));
  fragColor = vec4(c * 0.25, 1.0);
}
)";

const char* kBlurFragment = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uSource;
uniform vec2 uTexel;    // of the source
uniform float uSpread;  // 1: halving; wider when coming back up
out vec4 fragColor;
void main() {
  vec2 o = uTexel * uSpread;
  vec3 c = texture(uSource, vUV).rgb * 4.0;
  c += texture(uSource, vUV + vec2(-o.x, -o.y)).rgb + texture(uSource, vUV + vec2(o.x, -o.y)).rgb +
       texture(uSource, vUV + vec2(-o.x, o.y)).rgb + texture(uSource, vUV + vec2(o.x, o.y)).rgb;
  c += (texture(uSource, vUV + vec2(-o.x, 0.0)).rgb + texture(uSource, vUV + vec2(o.x, 0.0)).rgb +
        texture(uSource, vUV + vec2(0.0, -o.y)).rgb + texture(uSource, vUV + vec2(0.0, o.y)).rgb) * 2.0;
  fragColor = vec4(c / 16.0, 1.0);
}
)";

// --- The finished picture -------------------------------------------------------------------
const char* kCompositeFragment = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomOn;
uniform float uExposure;
uniform float uShoulder;   // 1: bright parts roll off to white instead of clipping
uniform float uVignette;
uniform float uGrain;
uniform float uTime;
uniform mat3 uColour;
out vec4 fragColor;
float hash(vec2 p) {
  p = fract(p * vec2(0.1031, 0.1030));
  p += dot(p, p.yx + 33.33);
  return fract((p.x + p.y) * p.x);
}
void main() {
  vec3 c = texture(uScene, vUV).rgb;
  c += texture(uBloom, vUV).rgb * uBloomOn;
  c *= uExposure;
  if (uShoulder > 0.0) {
    // Unchanged up to 0.8; above it a curve that reaches white only for very bright light.
    vec3 over = max(c - 0.8, 0.0);
    c = min(c, 0.8) + 0.2 * (1.0 - exp(-over / 0.2));
  }
  c = uColour * c;
  if (uVignette > 0.0) {
    float r = length(vUV - 0.5) * 1.41421;
    c *= 1.0 - uVignette * smoothstep(0.55, 1.15, r);
  }
  if (uGrain > 0.0) c += (hash(gl_FragCoord.xy + fract(uTime) * 61.0) - 0.5) * uGrain;
  c = clamp(c, 0.0, 1.0);
  fragColor = vec4(c, dot(c, vec3(0.299, 0.587, 0.114)));  // brightness in alpha, for edge smoothing
}
)";

// --- Smoothed edges (FXAA, the short form) -------------------------------------------------
const char* kFxaaFragment = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uImage;  // rgb + brightness in alpha
uniform vec2 uTexel;
out vec4 fragColor;
void main() {
  vec4 m = texture(uImage, vUV);
  float lNW = texture(uImage, vUV + uTexel * vec2(-1.0, -1.0)).a;
  float lNE = texture(uImage, vUV + uTexel * vec2(1.0, -1.0)).a;
  float lSW = texture(uImage, vUV + uTexel * vec2(-1.0, 1.0)).a;
  float lSE = texture(uImage, vUV + uTexel * vec2(1.0, 1.0)).a;
  float lM = m.a;
  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
  float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
  if (lMax - lMin < max(0.0312, lMax * 0.125)) {  // no edge here
    fragColor = vec4(m.rgb, 1.0);
    return;
  }
  vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
  float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);
  float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
  dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * uTexel;
  vec4 a = 0.5 * (texture(uImage, vUV + dir * (1.0 / 3.0 - 0.5)) + texture(uImage, vUV + dir * (2.0 / 3.0 - 0.5)));
  vec4 b = a * 0.5 + 0.25 * (texture(uImage, vUV + dir * -0.5) + texture(uImage, vUV + dir * 0.5));
  fragColor = vec4((b.a < lMin || b.a > lMax) ? a.rgb : b.rgb, 1.0);
}
)";

void passState() {
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
}
void bindTexture(int unit, GLuint tex) {
  glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
  glBindTexture(GL_TEXTURE_2D, tex);
}
}  // namespace

bool SceneLook::identity() const {
  return light[0] == 1.0f && light[1] == 1.0f && light[2] == 1.0f && saturation == 1.0f && fogDensity <= 0.0f &&
         groundFog <= 0.0f && skyFog <= 0.0f && clouds <= 0.0f;
}

PostProcess::~PostProcess() {
  if (litDepthFbo_) glDeleteFramebuffers(1, &litDepthFbo_);
  destroyMultisample();
}

void PostProcess::destroyMultisample() {
  if (msFbo_) glDeleteFramebuffers(1, &msFbo_);
  if (msColour_) glDeleteRenderbuffers(1, &msColour_);
  if (msDepth_) glDeleteRenderbuffers(1, &msDepth_);
  msFbo_ = msColour_ = msDepth_ = 0;
  msSamples_ = 0;
}

void PostProcess::resolve(bool depth) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo_);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scene_.fbo());
  glBlitFramebuffer(0, 0, sceneW_, sceneH_, 0, 0, sceneW_, sceneH_, GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0), GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, scene_.fbo());
}

bool PostProcess::init(std::string& error) {
  ready_ = false;
  const struct {
    gl::Shader* shader;
    const char* fragment;
    const char* name;
  } passes[] = {{&lightShader_, kLightFragment, "light"},
                {&compositeShader_, kCompositeFragment, "composite"},
                {&fxaaShader_, kFxaaFragment, "edge smoothing"},
                {&brightShader_, kBrightFragment, "glow"},
                {&blurShader_, kBlurFragment, "blur"}};
  for (const auto& p : passes) {
    std::string why;
    if (!p.shader->compile(gfx::kFullscreenVertex, p.fragment, why)) {
      error = std::string(p.name) + " pass: " + why;
      return false;
    }
  }
  hdr_ = gfx::caps().floatTargets;
  ready_ = true;
  return true;
}

bool PostProcess::ensureTargets(int w, int h, bool bloom, bool fxaa, int samples) {
  const gfx::Format fmt = hdr_ ? gfx::Format::RGBA16F : gfx::Format::RGBA8;
  if (!scene_.ready() || scene_.width() != w || scene_.height() != h) {
    if (litDepthFbo_) glDeleteFramebuffers(1, &litDepthFbo_);
    litDepthFbo_ = 0;
    destroyMultisample();
    ldr_.destroy();
    for (gfx::RenderTarget& b : bloom_) b.destroy();
    if (!scene_.create(w, h, fmt, true) || !litTarget_.create(w, h, fmt, false)) {
      if (hdr_) {  // once more without float targets
        hdr_ = false;
        return ensureTargets(w, h, bloom, fxaa, samples);
      }
      scene_.destroy();
      litTarget_.destroy();
      return false;
    }
    // The lit colour with the scene's depth: for what is drawn after light().
    GLint before = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
    glGenFramebuffers(1, &litDepthFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, litDepthFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, litTarget_.colour(), 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, scene_.depth(), 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(before));
    if (!ok) {
      glDeleteFramebuffers(1, &litDepthFbo_);
      litDepthFbo_ = 0;
      scene_.destroy();
      litTarget_.destroy();
      return false;
    }
  }
  if (samples > 1 && msSamples_ != samples) {
    destroyMultisample();
    GLint most = 0, before = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &most);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
    const int n = std::min(samples, static_cast<int>(most));
    if (n > 1) {
      glGenRenderbuffers(1, &msColour_);
      glBindRenderbuffer(GL_RENDERBUFFER, msColour_);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, hdr_ ? GL_RGBA16F : GL_RGBA8, w, h);
      glGenRenderbuffers(1, &msDepth_);
      glBindRenderbuffer(GL_RENDERBUFFER, msDepth_);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_DEPTH_COMPONENT24, w, h);
      glBindRenderbuffer(GL_RENDERBUFFER, 0);
      glGenFramebuffers(1, &msFbo_);
      glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColour_);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth_);
      const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
      glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(before));
      if (ok) msSamples_ = samples;  // (asked for; the card may give fewer)
      else destroyMultisample();     // edges go unsmoothed rather than the scene undrawn
    }
  }
  if (fxaa && !ldr_.ready()) ldr_.create(w, h, gfx::Format::RGBA8, false);
  if (bloom && hdr_ && !bloom_[0].ready()) {
    int bw = w, bh = h;
    for (gfx::RenderTarget& b : bloom_) {
      bw = std::max(1, bw / 2);
      bh = std::max(1, bh / 2);
      b.create(bw, bh, gfx::Format::RGBA16F, false);
    }
  }
  return true;
}

bool PostProcess::beginScene(int screenW, int screenH, const gfx::Options& opt) {
  inScene_ = false;
  lit_ = false;
  if (!ready_ || !opt.post || screenW < 8 || screenH < 8) return false;
  const float scale = std::clamp(opt.renderScale, 0.5f, 1.0f);
  const int w = std::max(8, static_cast<int>(std::lround(static_cast<float>(screenW) * scale)));
  const int h = std::max(8, static_cast<int>(std::lround(static_cast<float>(screenH) * scale)));
  if (!ensureTargets(w, h, opt.bloom, opt.antialias == 1, opt.antialias == 2 ? 4 : 0)) {
    if (ready_) PNR_WARN("Post-processing switched off: the graphics card can't make its targets (%dx%d)", w, h);
    ready_ = false;
    return false;
  }
  sceneW_ = w;
  sceneH_ = h;
  multisampled_ = opt.antialias == 2 && msFbo_ != 0;
  if (multisampled_) {
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glViewport(0, 0, w, h);
  } else {
    scene_.bind();
  }
  inScene_ = true;
  return true;
}

void PostProcess::light(const SceneLook& look, const Camera& cam, const gfx::Options& opt) {
  if (!inScene_ || lit_) return;
  SceneLook l = look;
  if (!opt.groundFog) l.groundFog = 0.0f;
  if (!opt.cloudShadows) l.clouds = 0.0f;
  if (l.identity()) return;  // what is drawn next goes on in the same target
  passState();
  if (multisampled_) resolve(true);
  litTarget_.bind();
  lightShader_.use();
  bindTexture(0, scene_.colour());
  bindTexture(1, scene_.depth());
  glUniform1i(lightShader_.uniform("uScene"), 0);
  glUniform1i(lightShader_.uniform("uDepth"), 1);
  glUniformMatrix4fv(lightShader_.uniform("uInvViewProj"), 1, GL_FALSE, cam.viewProj().inverse().m);
  const Vec3f eye = cam.eye();
  glUniform3f(lightShader_.uniform("uEye"), eye.x, eye.y, eye.z);
  glUniform3f(lightShader_.uniform("uLight"), l.light[0], l.light[1], l.light[2]);
  glUniform1f(lightShader_.uniform("uSaturation"), l.saturation);
  glUniform3f(lightShader_.uniform("uFog"), l.fog[0], l.fog[1], l.fog[2]);
  glUniform4f(lightShader_.uniform("uFogParams"), l.fogDensity, l.groundFog, l.fogBase, l.fogHeight);
  glUniform1f(lightShader_.uniform("uSkyFog"), l.skyFog);
  glUniform4f(lightShader_.uniform("uCloud"), l.clouds, l.cloudScale, l.cloudOffset[0] * l.cloudScale, l.cloudOffset[1] * l.cloudScale);
  gfx::drawFullscreen();
  bindTexture(1, 0);
  glActiveTexture(GL_TEXTURE0);
  // On with the scene, in the lit picture, in front of and behind the world as before.
  glBindFramebuffer(GL_FRAMEBUFFER, litDepthFbo_);
  glViewport(0, 0, sceneW_, sceneH_);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  lit_ = true;
}

GLuint PostProcess::sceneTexture() const { return lit_ ? litTarget_.colour() : scene_.colour(); }

void PostProcess::bloomChain(GLuint source) {
  // Down: what glows, then each level half the last.
  brightShader_.use();
  bloom_[0].bind();
  bindTexture(0, source);
  glUniform1i(brightShader_.uniform("uScene"), 0);
  glUniform2f(brightShader_.uniform("uTexel"), 0.5f / static_cast<float>(sceneW_), 0.5f / static_cast<float>(sceneH_));
  gfx::drawFullscreen();
  blurShader_.use();
  glUniform1i(blurShader_.uniform("uSource"), 0);
  for (int i = 1; i < kBloomLevels; ++i) {
    bloom_[i].bind();
    bindTexture(0, bloom_[i - 1].colour());
    glUniform2f(blurShader_.uniform("uTexel"), 1.0f / static_cast<float>(bloom_[i - 1].width()), 1.0f / static_cast<float>(bloom_[i - 1].height()));
    glUniform1f(blurShader_.uniform("uSpread"), 1.0f);
    gfx::drawFullscreen();
  }
  // Up: each level added onto the one above it, so the glow is wide and soft.
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  for (int i = kBloomLevels - 1; i > 0; --i) {
    bloom_[i - 1].bind();
    bindTexture(0, bloom_[i].colour());
    glUniform2f(blurShader_.uniform("uTexel"), 1.0f / static_cast<float>(bloom_[i].width()), 1.0f / static_cast<float>(bloom_[i].height()));
    glUniform1f(blurShader_.uniform("uSpread"), 1.5f);
    gfx::drawFullscreen();
  }
  glDisable(GL_BLEND);
}

void PostProcess::present(const SceneLook& look, const gfx::Options& opt, int screenW, int screenH, float time) {
  if (!inScene_) return;
  inScene_ = false;
  passState();
  if (multisampled_ && !lit_) resolve(false);
  const GLuint source = sceneTexture();
  const bool bloom = opt.bloom && hdr_ && bloom_[0].ready();
  if (bloom) bloomChain(source);
  const bool fxaa = opt.antialias == 1 && ldr_.ready();

  if (fxaa) ldr_.bind();
  else gfx::bindScreen(screenW, screenH);
  compositeShader_.use();
  bindTexture(0, source);
  bindTexture(1, bloom ? bloom_[0].colour() : source);
  glUniform1i(compositeShader_.uniform("uScene"), 0);
  glUniform1i(compositeShader_.uniform("uBloom"), 1);
  glUniform1f(compositeShader_.uniform("uBloomOn"), bloom ? 0.85f : 0.0f);
  glUniform1f(compositeShader_.uniform("uExposure"), look.exposure);
  // Bright parts roll off only where something can be brighter than white.
  glUniform1f(compositeShader_.uniform("uShoulder"), bloom || look.exposure > 1.0f ? 1.0f : 0.0f);
  glUniform1f(compositeShader_.uniform("uVignette"), look.vignette);
  // A breath of grain hides the steps in dark, smooth skies; none when the picture is as drawn.
  glUniform1f(compositeShader_.uniform("uGrain"), lit_ ? 1.5f / 255.0f : 0.0f);
  glUniform1f(compositeShader_.uniform("uTime"), time);
  const float* m = gfx::colourBlindMatrix(opt.colourBlind);
  glUniformMatrix3fv(compositeShader_.uniform("uColour"), 1, GL_TRUE, m);  // row-major in, column-major wanted
  gfx::drawFullscreen();

  if (fxaa) {
    gfx::bindScreen(screenW, screenH);
    fxaaShader_.use();
    bindTexture(0, ldr_.colour());
    glUniform1i(fxaaShader_.uniform("uImage"), 0);
    glUniform2f(fxaaShader_.uniform("uTexel"), 1.0f / static_cast<float>(ldr_.width()), 1.0f / static_cast<float>(ldr_.height()));
    gfx::drawFullscreen();
  }
  bindTexture(1, 0);
  glActiveTexture(GL_TEXTURE0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  lit_ = false;
}

}  // namespace pioneer

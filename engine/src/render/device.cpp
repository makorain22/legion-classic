#include "pioneer/render/device.hpp"

#include <cstring>

#include "pioneer/core/log.hpp"

namespace pioneer::gfx {

namespace {
Caps g_caps;
GLuint g_fullscreenVao = 0;

const char* str(GLenum name) {
  const GLubyte* s = glGetString(name);
  return s ? reinterpret_cast<const char*>(s) : "";
}
}  // namespace

const char* const kFullscreenVertex = R"(#version 330 core
out vec2 vUV;
void main() {
  // One triangle over the whole target: (-1,-1), (3,-1), (-1,3).
  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
  vUV = p;
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

void queryCaps() {
  Caps c;
  c.vendor = str(GL_VENDOR);
  c.renderer = str(GL_RENDERER);
  c.version = str(GL_VERSION);
  glGetIntegerv(GL_MAJOR_VERSION, &c.major);
  glGetIntegerv(GL_MINOR_VERSION, &c.minor);
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &c.maxTextureSize);
  c.api = c.version.find("OpenGL ES") != std::string::npos ? Api::GLES30 : Api::GL33;
  c.timerQueries = c.api == Api::GL33;  // core since 3.3
  c.software = softwareRenderer(c.renderer);
  // Can a half-float texture be drawn into? (Core in 3.3; ES 3.0 needs an extension: ask by trying.)
  {
    GLuint tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 4, 4, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glGenFramebuffers(1, &fbo);
    GLint before = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    c.floatTargets = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(before));
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    while (glGetError() != GL_NO_ERROR) {
    }
  }
  g_caps = c;
  PNR_INFO("Graphics: %s, %s (%s); textures to %d; float targets %s%s", c.renderer.c_str(), c.version.c_str(), c.vendor.c_str(),
           c.maxTextureSize, c.floatTargets ? "yes" : "no", c.software ? "; drawn by the processor" : "");
}

const Caps& caps() { return g_caps; }

RenderTarget::~RenderTarget() { destroy(); }

void RenderTarget::destroy() {
  if (fbo_) glDeleteFramebuffers(1, &fbo_);
  if (colour_) glDeleteTextures(1, &colour_);
  if (depth_ && ownsDepth_) glDeleteTextures(1, &depth_);
  fbo_ = colour_ = depth_ = 0;
  ownsDepth_ = false;
  w_ = h_ = 0;
}

bool RenderTarget::create(int w, int h, Format format, bool depth, bool linear, GLuint sharedDepth) {
  destroy();
  if (w < 1 || h < 1) return false;
  w_ = w;
  h_ = h;
  format_ = format;
  GLint before = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &before);
  glGenTextures(1, &colour_);
  glBindTexture(GL_TEXTURE_2D, colour_);
  if (format == Format::RGBA16F) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
  else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (sharedDepth) {
    depth_ = sharedDepth;
    ownsDepth_ = false;
  } else if (depth) {
    glGenTextures(1, &depth_);
    ownsDepth_ = true;
    glBindTexture(GL_TEXTURE_2D, depth_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  glGenFramebuffers(1, &fbo_);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour_, 0);
  if (depth_) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
  const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(before));
  if (!ok) destroy();
  return ok;
}

void RenderTarget::bind() const {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  glViewport(0, 0, w_, h_);
}

void bindScreen(int w, int h) {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, w, h);
}

void drawFullscreen() {
  if (!g_fullscreenVao) glGenVertexArrays(1, &g_fullscreenVao);  // no buffers: the shader makes the corners
  glBindVertexArray(g_fullscreenVao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
}

GpuTimer::~GpuTimer() {
  if (query_[0]) glDeleteQueries(2, query_);
}

void GpuTimer::begin() {
  if (!caps().timerQueries || running_) return;
  if (!query_[0]) glGenQueries(2, query_);
  // Read the one started a frame ago (if the card has it), then start the other.
  if (primed_) {
    GLint ready = 0;
    glGetQueryObjectiv(query_[1 - at_], GL_QUERY_RESULT_AVAILABLE, &ready);
    if (ready) {
      GLuint64 ns = 0;
      glGetQueryObjectui64v(query_[1 - at_], GL_QUERY_RESULT, &ns);
      ms_ = static_cast<float>(static_cast<double>(ns) / 1.0e6);
    }
  }
  glBeginQuery(GL_TIME_ELAPSED, query_[at_]);
  running_ = true;
}

void GpuTimer::end() {
  if (!running_) return;
  glEndQuery(GL_TIME_ELAPSED);
  running_ = false;
  primed_ = true;
  at_ = 1 - at_;
}

}  // namespace pioneer::gfx

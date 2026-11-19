#pragma once
// Pioneer Engine - the graphics device: what the card can do, and the few
// objects every pass needs (render targets, a full-screen triangle, timers).
//
// The renderers speak OpenGL 3.3 core. This layer is where a second API
// would be told apart (OpenGL ES 3.0 is the one planned: same calls for
// everything used here, another shader header, no timer queries).

#include <glad/gl.h>

#include <string>

#include "pioneer/render/graphics.hpp"

namespace pioneer::gfx {

struct Caps {
  Api api = Api::GL33;
  int major = 3, minor = 3;
  std::string vendor, renderer, version;
  int maxTextureSize = 2048;
  bool floatTargets = false;  // RGBA16F can be drawn into
  bool timerQueries = false;
  bool software = false;
};
// Asks the driver (once the context exists). Safe to call again.
void queryCaps();
const Caps& caps();

enum class Format { RGBA8, RGBA16F };

// A texture to draw into, with or without a depth texture of its own.
class RenderTarget {
 public:
  RenderTarget() = default;
  ~RenderTarget();
  RenderTarget(const RenderTarget&) = delete;
  RenderTarget& operator=(const RenderTarget&) = delete;

  // (Re)creates it. `sharedDepth`: a depth texture of the same size to use
  // instead of one of its own (0 with depth = false: colour only).
  bool create(int w, int h, Format format, bool depth, bool linear = true, GLuint sharedDepth = 0);
  void destroy();
  bool ready() const { return fbo_ != 0; }
  void bind() const;  // as the draw target, viewport set to its size
  int width() const { return w_; }
  int height() const { return h_; }
  GLuint fbo() const { return fbo_; }
  GLuint colour() const { return colour_; }
  GLuint depth() const { return depth_; }
  Format format() const { return format_; }

 private:
  GLuint fbo_ = 0, colour_ = 0, depth_ = 0;
  bool ownsDepth_ = false;
  int w_ = 0, h_ = 0;
  Format format_ = Format::RGBA8;
};

// The window itself as the draw target.
void bindScreen(int w, int h);
// Draws one triangle that covers the target (the vertex shader makes it from
// gl_VertexID: see kFullscreenVertex). Depth test and writes are the caller's.
void drawFullscreen();
extern const char* const kFullscreenVertex;  // out vec2 vUV

// How long the card took over a stretch of drawing (the result is a frame
// or two late; 0 where timer queries don't exist).
class GpuTimer {
 public:
  ~GpuTimer();
  void begin();
  void end();
  float ms() const { return ms_; }

 private:
  GLuint query_[2] = {0, 0};
  int at_ = 0;
  bool running_ = false, primed_ = false;
  float ms_ = 0.0f;
};

}  // namespace pioneer::gfx

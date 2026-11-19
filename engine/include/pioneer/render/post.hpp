#pragma once
// Pioneer Engine - the post-processing pipeline of the 3D view.
//
//   beginScene()   the world is drawn into a half-float target (at the
//                  render scale) instead of the window
//   light()        the drawn world is lit for the hour and the weather: the
//                  light's colour, mist, the shadows of clouds. Effects in
//                  the air (fire, flashes, rain) are drawn after it and so
//                  keep their own brightness in the dark
//   present()      glow, exposure, the colour filter, smoothed edges, and
//                  onto the screen at full size; the interface is drawn
//                  over that, untouched
//
// With the default look (noon, clear) light() does nothing and present()
// copies: the picture is the one the engine drew before it had a pipeline.
// If the card can't do it, beginScene() says no and the scene goes straight
// to the screen.

#include <string>

#include "pioneer/render/camera.hpp"
#include "pioneer/render/device.hpp"
#include "pioneer/render/gl.hpp"
#include "pioneer/render/graphics.hpp"

namespace pioneer {

// How the drawn world is turned into what is seen. render/environment.hpp
// makes one from the hour and the weather; the default changes nothing.
struct SceneLook {
  float light[3] = {1.0f, 1.0f, 1.0f};  // multiplies the world: sun and sky together
  float saturation = 1.0f;
  float fog[3] = {0.62f, 0.66f, 0.70f};
  float fogDensity = 0.0f;     // per tile of distance from the eye
  float groundFog = 0.0f;      // more of it where the ground is low
  float fogBase = 0.0f;        // the height (tiles) ground fog lies at...
  float fogHeight = 2.0f;      // ...and how far above it it thins out
  float skyFog = 0.0f;         // how much of the fog colour the sky takes (0: as drawn)
  float clouds = 0.0f;         // strength of clouds' shadows on the ground
  float cloudOffset[2] = {0.0f, 0.0f};  // they drift: tiles
  float cloudScale = 0.045f;   // 1 / the size of a cloud in tiles
  float exposure = 1.0f;
  float vignette = 0.0f;
  bool identity() const;       // nothing for light() to do
};

class PostProcess {
 public:
  PostProcess() = default;
  ~PostProcess();
  PostProcess(const PostProcess&) = delete;
  PostProcess& operator=(const PostProcess&) = delete;

  bool init(std::string& error);            // compiles the passes (once, after the context exists)
  bool available() const { return ready_; }
  bool hdr() const { return hdr_; }         // float targets: things can be brighter than white

  // Start of the scene: its target is bound and sized. False: not available,
  // or switched off; draw to the screen.
  bool beginScene(int screenW, int screenH, const gfx::Options& opt);
  bool inScene() const { return inScene_; }
  // The world is drawn: light it. What is drawn next is not lit.
  void light(const SceneLook& look, const Camera& cam, const gfx::Options& opt);
  // The scene is drawn: put it on the screen. `time` in seconds (for the grain that hides banding).
  void present(const SceneLook& look, const gfx::Options& opt, int screenW, int screenH, float time);

  int sceneWidth() const { return sceneW_; }
  int sceneHeight() const { return sceneH_; }
  // The lit scene's colour texture after present() (for screenshots of the scene alone, tools).
  GLuint sceneTexture() const;

 private:
  bool ensureTargets(int w, int h, bool bloom, bool fxaa, int samples);
  void destroyMultisample();
  void resolve(bool depth);  // the multisampled scene into the plain one
  void bloomChain(GLuint source);

  bool ready_ = false, hdr_ = false, inScene_ = false, lit_ = false;
  int sceneW_ = 0, sceneH_ = 0;
  // The scene with 4x multisampling (renderbuffers), when edges are smoothed
  // the best way: drawn into, then resolved into scene_.
  GLuint msFbo_ = 0, msColour_ = 0, msDepth_ = 0;
  int msSamples_ = 0;
  bool multisampled_ = false;   // this frame's scene is being drawn into it
  gfx::RenderTarget scene_;     // the world as drawn (with depth)
  gfx::RenderTarget litTarget_;  // the world lit (colour only)
  GLuint litDepthFbo_ = 0;      // the lit colour with the scene's depth, for what is drawn after light()
  gfx::RenderTarget ldr_;       // the finished picture before edge smoothing
  static constexpr int kBloomLevels = 4;
  gfx::RenderTarget bloom_[kBloomLevels];
  gl::Shader lightShader_, compositeShader_, fxaaShader_, brightShader_, blurShader_;
};

}  // namespace pioneer

#pragma once
// Slave Engine - the application: window, OpenGL context, main loop.
//
// The loop runs the simulation at a fixed 15 ticks per second and renders as
// fast as the screen allows, smoothing movement between ticks. If the PC
// can't keep up, the simulation slows down instead of stuttering.

#include <SDL3/SDL.h>

#include <string>

namespace slave {

struct AppConfig {
  std::string title = "Slave Engine";
  int width = 1280, height = 720;
  bool fullscreen = false;
  bool vsync = true;
  bool offscreen = false;        // no visible window (automated tests)
  int maxFrames = 0;             // quit after N frames (0 = run until closed)
  std::string screenshotOnExit;  // save a PNG of the last frame
  bool fixedFrameTime = false;   // pretend every frame took 1/60 s (repeatable tests)
};

class Scene {
 public:
  virtual ~Scene() = default;
  // uiWantsMouse / uiWantsKeyboard: true when an ImGui window has focus.
  virtual void onEvent(const SDL_Event& e, bool uiWantsMouse, bool uiWantsKeyboard) {}
  virtual void onTick() {}                        // fixed 15 Hz
  virtual void onUpdate(float dt, float alpha) {}  // every frame; alpha = progress to next tick
  virtual void onRender() {}
  virtual void onImGui() {}
  virtual float timeScale() const { return 1.0f; }  // 0 = paused
  virtual bool wantsQuit() const { return false; }
};

class App {
 public:
  bool init(const AppConfig& cfg);
  int run(Scene& scene);
  void shutdown();

  SDL_Window* window() const { return window_; }
  int pixelWidth() const { return pw_; }
  int pixelHeight() const { return ph_; }
  // Window coordinates (from SDL events) -> framebuffer pixels.
  float pixelScale() const { return pixelScale_; }

  void toggleFullscreen();
  void requestScreenshot(const std::string& path) { pendingShot_ = path; }
  bool saveScreenshot(const std::string& path) const;

  double fps() const { return fps_; }
  double frameMs() const { return frameMs_; }
  int ticksLastFrame() const { return ticksLastFrame_; }
  bool simBehind() const { return simBehind_; }
  const std::string& userDir() const { return userDir_; }
  bool offscreen() const { return cfg_.offscreen; }

  // Frame limiter: 0 = unlimited (vsync still applies if on).
  void setFpsLimit(int fps) { fpsLimit_ = fps; }
  int fpsLimit() const { return fpsLimit_; }
  void setVsync(bool on);
  // Keeps the mouse inside the window (for edge scrolling) while it has focus.
  void setMouseLock(bool on);
  bool mouseLock() const { return mouseLock_; }

 private:
  void updateSize();

  AppConfig cfg_;
  SDL_Window* window_ = nullptr;
  SDL_GLContext gl_ = nullptr;
  int pw_ = 0, ph_ = 0;
  float pixelScale_ = 1.0f;
  bool fullscreen_ = false;
  double fps_ = 0, frameMs_ = 0;
  int ticksLastFrame_ = 0;
  bool simBehind_ = false;
  std::string pendingShot_;
  std::string userDir_;
  int fpsLimit_ = 0;
  bool mouseLock_ = false;
};

}  // namespace slave

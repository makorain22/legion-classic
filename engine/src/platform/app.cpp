#include "slave/platform/app.hpp"

#include <algorithm>
#include <utility>

#include <glad/gl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include "slave/assets/image.hpp"
#include "slave/core/log.hpp"
#include "slave/render/gl.hpp"

namespace slave {

namespace {
constexpr double kTickSeconds = 1.0 / 15.0;
constexpr int kMaxTicksPerFrame = 4;

void applyImGuiStyle() {
  ImGuiStyle& s = ImGui::GetStyle();
  ImGui::StyleColorsDark();
  s.WindowRounding = 0;
  s.FrameRounding = 0;
  s.ScrollbarRounding = 0;
  s.GrabRounding = 0;
  s.TabRounding = 0;
  s.WindowBorderSize = 1;
  s.FrameBorderSize = 1;
  ImVec4* c = s.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.11f, 0.09f, 0.94f);
  c[ImGuiCol_Border] = ImVec4(0.45f, 0.40f, 0.28f, 0.80f);
  c[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.18f, 0.12f, 1.0f);
  c[ImGuiCol_TitleBgActive] = ImVec4(0.32f, 0.27f, 0.15f, 1.0f);
  c[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.17f, 0.13f, 1.0f);
  c[ImGuiCol_Button] = ImVec4(0.30f, 0.27f, 0.18f, 1.0f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.39f, 0.22f, 1.0f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.58f, 0.48f, 0.24f, 1.0f);
  c[ImGuiCol_Header] = ImVec4(0.30f, 0.27f, 0.18f, 1.0f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.39f, 0.22f, 1.0f);
  c[ImGuiCol_CheckMark] = ImVec4(0.92f, 0.78f, 0.36f, 1.0f);
  c[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.66f, 0.30f, 1.0f);
  c[ImGuiCol_Text] = ImVec4(0.93f, 0.90f, 0.82f, 1.0f);
}
}  // namespace

bool App::init(const AppConfig& cfg) {
  cfg_ = cfg;
  if (cfg.offscreen) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    SLV_ERROR("SDL_Init failed: %s", SDL_GetError());
    return false;
  }

  if (char* pref = SDL_GetPrefPath("Makorain", "LegionClassic")) {
    userDir_ = pref;
    SDL_free(pref);
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
#ifndef NDEBUG
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

  SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (cfg.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
  window_ = SDL_CreateWindow(cfg.title.c_str(), cfg.width, cfg.height, flags);
  if (!window_) {
    SLV_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
    return false;
  }
  fullscreen_ = cfg.fullscreen;
  gl_ = SDL_GL_CreateContext(window_);
  if (!gl_) {
    SLV_ERROR("Could not create an OpenGL 3.3 context: %s", SDL_GetError());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Legion Classic",
                             "Your graphics driver does not support OpenGL 3.3.\n"
                             "Please update your graphics driver.",
                             window_);
    return false;
  }
  SDL_GL_MakeCurrent(window_, gl_);
  if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress))) {
    SLV_ERROR("Failed to load OpenGL functions");
    return false;
  }
  SDL_GL_SetSwapInterval(cfg.vsync && !cfg.offscreen ? 1 : 0);
  gl::installDebugOutput();
  SLV_INFO("OpenGL %s | %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
           reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // layout is saved by the game itself later
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  applyImGuiStyle();
  ImGui_ImplSDL3_InitForOpenGL(window_, gl_);
  ImGui_ImplOpenGL3_Init("#version 330");

  updateSize();
  return true;
}

void App::updateSize() {
  SDL_GetWindowSizeInPixels(window_, &pw_, &ph_);
  int ww = 0, wh = 0;
  SDL_GetWindowSize(window_, &ww, &wh);
  pixelScale_ = ww > 0 ? static_cast<float>(pw_) / static_cast<float>(ww) : 1.0f;
}

void App::toggleFullscreen() {
  fullscreen_ = !fullscreen_;
  SDL_SetWindowFullscreen(window_, fullscreen_);
}

bool App::saveScreenshot(const std::string& path) const {
  Image img(pw_, ph_);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, pw_, ph_, GL_RGBA, GL_UNSIGNED_BYTE, img.px.data());
  // OpenGL's origin is bottom-left; PNGs are top-down.
  for (int y = 0; y < ph_ / 2; ++y)
    for (int x = 0; x < pw_; ++x) std::swap(img.at(x, y), img.at(x, ph_ - 1 - y));
  for (auto& p : img.px) p.a = 255;
  const bool ok = img.savePng(path);
  if (ok) SLV_INFO("Screenshot saved: %s", path.c_str());
  else SLV_ERROR("Could not save screenshot: %s", path.c_str());
  return ok;
}

int App::run(Scene& scene) {
  uint64_t prev = SDL_GetTicksNS();
  double acc = 0;
  int fpsFrames = 0, frame = 0;
  bool running = true;

  while (running && !scene.wantsQuit()) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      ImGui_ImplSDL3_ProcessEvent(&e);
      const ImGuiIO& io = ImGui::GetIO();
      if (e.type == SDL_EVENT_QUIT) running = false;
      if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || e.type == SDL_EVENT_WINDOW_RESIZED) updateSize();
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        const bool altEnter = e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT);
        if (e.key.key == SDLK_F11 || altEnter) {
          toggleFullscreen();
          continue;
        }
      }
      scene.onEvent(e, io.WantCaptureMouse, io.WantCaptureKeyboard);
    }

    const uint64_t now = SDL_GetTicksNS();
    double dt = cfg_.fixedFrameTime ? 1.0 / 60.0 : static_cast<double>(now - prev) / 1e9;
    prev = now;
    if (dt > 0.25) dt = 0.25;

    // Fixed-rate simulation. If we fall behind, drop time (slow-motion)
    // rather than running ever more ticks per frame.
    acc += dt * scene.timeScale();
    int ticks = 0;
    while (acc >= kTickSeconds && ticks < kMaxTicksPerFrame) {
      scene.onTick();
      acc -= kTickSeconds;
      ++ticks;
    }
    simBehind_ = acc >= kTickSeconds;
    if (simBehind_) acc = 0;
    ticksLastFrame_ = ticks;

    scene.onUpdate(static_cast<float>(dt), static_cast<float>(acc / kTickSeconds));

    glViewport(0, 0, pw_, ph_);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    scene.onRender();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    scene.onImGui();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ++frame;
    if (!pendingShot_.empty()) {
      saveScreenshot(pendingShot_);
      pendingShot_.clear();
    }
    if (cfg_.maxFrames > 0 && frame >= cfg_.maxFrames) {
      if (!cfg_.screenshotOnExit.empty()) saveScreenshot(cfg_.screenshotOnExit);
      running = false;
    }
    SDL_GL_SwapWindow(window_);

    ++fpsFrames;
    const double real = static_cast<double>(SDL_GetTicksNS() - now) / 1e6;
    frameMs_ = frameMs_ * 0.9 + real * 0.1;
    if (fpsFrames >= 30) {
      fps_ = 1000.0 / std::max(0.001, frameMs_);
      fpsFrames = 0;
    }
  }
  return 0;
}

void App::shutdown() {
  if (ImGui::GetCurrentContext()) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
  }
  if (gl_) SDL_GL_DestroyContext(gl_);
  if (window_) SDL_DestroyWindow(window_);
  SDL_Quit();
}

}  // namespace slave

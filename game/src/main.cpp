// Legion Classic - entry point.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game_scene.hpp"
#include "slave/core/log.hpp"
#include "slave/platform/app.hpp"

namespace {
void usage() {
  SLV_INFO("Options: --windowed WxH | --fullscreen | --map N | --seed N | --zoom 0.5|1|2 | --spawn N\n"
           "         --offscreen --frames N --screenshot out.png --demo --fixed-dt --no-help --debug\n"
           "         --dump-atlas atlas.png --editor --open map.lmap");
}
}  // namespace

int main(int argc, char** argv) {
  slave::AppConfig cfg;
  cfg.title = "Legion Classic";
  legion::SceneOptions opt;
  std::string dumpAtlas;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--offscreen") cfg.offscreen = true;
    else if (a == "--frames") cfg.maxFrames = std::atoi(next());
    else if (a == "--screenshot") cfg.screenshotOnExit = next();
    else if (a == "--fixed-dt") cfg.fixedFrameTime = true;
    else if (a == "--fullscreen") cfg.fullscreen = true;
    else if (a == "--windowed") std::sscanf(next(), "%dx%d", &cfg.width, &cfg.height);
    else if (a == "--map") opt.mapSize = std::max(16, std::min(1024, std::atoi(next())));
    else if (a == "--seed") opt.seed = std::strtoull(next(), nullptr, 10);
    else if (a == "--zoom") opt.zoom = static_cast<float>(std::atof(next()));
    else if (a == "--spawn") opt.stressUnits = std::atoi(next());
    else if (a == "--demo") opt.demo = true;
    else if (a == "--no-help") opt.showHelp = false;
    else if (a == "--debug") opt.showDebug = true;
    else if (a == "--dump-atlas") dumpAtlas = next();
    else if (a == "--editor") opt.startInEditor = true;
    else if (a == "--editor-demo") opt.editorDemo = true;
    else if (a == "--open") opt.loadMap = next();
    else if (a == "--help" || a == "-h") { usage(); return 0; }
    else SLV_WARN("Unknown option: %s", a.c_str());
  }

  slave::App app;
  if (!app.init(cfg)) return 1;
  slave::log::init((app.userDir() + "legion.log").c_str());
  SLV_INFO("Legion Classic 0.1 (Milestone 2) on Slave Engine 1.0");

  std::string contentDir = "content/";
  if (const char* base = SDL_GetBasePath()) contentDir = std::string(base) + "content/";

  int exitCode = 0;
  {
    // The scene owns GPU resources, so it must be destroyed before the
    // OpenGL context (app.shutdown()).
    legion::GameScene scene(app, opt);
    std::string error;
    if (!scene.init(contentDir, error)) {
      SLV_ERROR("Startup failed: %s", error.c_str());
      if (!cfg.offscreen)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Legion Classic", ("Could not start:\n" + error).c_str(),
                                 app.window());
      exitCode = 1;
    } else {
      if (!dumpAtlas.empty()) scene.art().atlas.savePng(dumpAtlas);
      app.run(scene);
    }
  }
  app.shutdown();
  slave::log::shutdown();
  return exitCode;
}

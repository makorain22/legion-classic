// Legion Classic - entry point.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "slave/platform/crash.hpp"
#include <cstring>
#include <string>

#include "game_scene.hpp"
#include "slave/assets/art_pack.hpp"
#include "slave/core/log.hpp"
#include "slave/platform/app.hpp"
#include "slave/sim/unit_types.hpp"

namespace {
void usage() {
  SLV_INFO("Options: --windowed WxH | --fullscreen | --map N | --seed N | --zoom 0.5|1|2 | --spawn N\n"
           "         --offscreen --frames N --screenshot out.png --demo --fixed-dt --no-help --debug\n"
           "         --dump-atlas atlas.png --editor --open map.lmap --editor-demo --live-demo\n"
           "         --tool rules[:unit_id]|mods --mods-selftest\n"
           "         --menu --ai-battle --side N --biome temperate|winter|desert|tropical|urban --gallery --works-demo --formation-demo --town-demo --bridge-demo --logistics-demo\n"
           "         --ffa (every side for itself) --bench MINUTES (an --ai-battle played without drawing; prints what each side did)\n"
           "         --export-sprites DIR (write every sprite as PNG for repainting; see docs/ART_PIPELINE.md)\n"
           "         --import-blitzkrieg DATA (make the Blitzkrieg art pack from the game's Data folder)");
}

// Renders the Blitzkrieg art pack into content/art/blitzkrieg (no window needed).
int importBlitzkrieg(const std::string& dataDir, const std::string& contentDir) {
  slave::BlitzkriegImport job;
  job.dataDir = dataDir;
  job.mappingFile = contentDir + "art/blitzkrieg.json";
  job.outDir = contentDir + "art/blitzkrieg";
  slave::UnitTypeRegistry types;
  std::string error;
  if (!types.loadFile(contentDir + "rules/buildings.json", error)) {
    std::fprintf(stderr, "Can't read the building rules: %s\n", error.c_str());
    return 1;
  }
  for (const slave::UnitTypeDef& t : types.all())
    if (t.isStructure()) job.footprints[t.model] = {t.footW, t.footH};
  std::string report;
  std::printf("Importing Blitzkrieg art from %s ...\n", dataDir.c_str());
  if (!slave::importBlitzkrieg(job, error, report)) {
    std::fprintf(stderr, "Import failed: %s\n", error.c_str());
    return 1;
  }
  std::printf("%s", report.c_str());
  std::printf("Blitzkrieg is (c) Nival and licensed for non-commercial use only: keep this pack to yourself.\n");
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  slave::AppConfig cfg;
  cfg.title = "Legion Classic";
  legion::SceneOptions opt;
  std::string dumpAtlas, importDir;
  // Started with no options (double-click): the main menu.
  opt.showMenu = argc <= 1;

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
    else if (a == "--classic") opt.classic = true;
    else if (a == "--no-shadows") opt.noShadows = true;
    else if (a == "--intel") opt.intel = true;
    else if (a == "--ffa") opt.ffa = true;
    else if (a == "--bench") {
      opt.bench = std::max(1, std::atoi(next()));
      opt.aiBattle = true;
    }
    else if (a == "--yaw") opt.yaw = static_cast<float>(std::atof(next()));
    else if (a == "--pitch") opt.pitch = static_cast<float>(std::atof(next()));
    else if (a == "--spawn") opt.stressUnits = std::atoi(next());
    else if (a == "--demo") opt.demo = true;
    else if (a == "--no-help") opt.showHelp = false;
    else if (a == "--debug") opt.showDebug = true;
    else if (a == "--dump-atlas") dumpAtlas = next();
    else if (a == "--editor") opt.startInEditor = true;
    else if (a == "--editor-demo") opt.editorDemo = true;
    else if (a == "--live-demo") opt.liveDemo = true;
    else if (a == "--editor-selftest") opt.editorSelfTest = true;
    else if (a == "--economy-demo") opt.economyDemo = true;
    else if (a == "--gallery") opt.gallery = true;
    else if (a == "--battle-demo") opt.battleDemo = true;
    else if (a == "--org-demo") opt.orgDemo = true;
    else if (a == "--works-demo") opt.worksDemo = true;
    else if (a == "--formation-demo") opt.formationDemo = true;
    else if (a == "--town-demo") opt.townDemo = 1;
    else if (a == "--bridge-demo") opt.townDemo = 2;
    else if (a == "--cliff-demo") opt.townDemo = 3;
    else if (a == "--logistics-demo") opt.townDemo = 4;
    else if (a == "--export-sprites") opt.exportSprites = next();
    else if (a == "--import-blitzkrieg") importDir = next();
    else if (a == "--no-art-pack") opt.noArtPack = true;
    else if (a == "--encyclopedia") opt.encyclopedia = next();
    else if (a == "--editor-tab") {
      opt.startInEditor = true;
      std::sscanf(next(), "%d.%d", &opt.editorTab, &opt.editorPage);
    }
    else if (a == "--no-bases") opt.noBases = true;
    else if (a == "--menu") opt.showMenu = true;
    else if (a == "--ai-battle") opt.aiBattle = true;
    else if (a == "--side") opt.side = std::atoi(next());
    else if (a == "--speed") opt.speed = static_cast<float>(std::atof(next()));
    else if (a == "--monkey") opt.monkey = std::atoi(next());
    else if (a == "--sides") opt.sides = std::atoi(next());
    else if (a == "--factions") opt.factions = next();
    else if (a == "--no-sound") opt.noSound = true;
    else if (a == "--sound") opt.forceSound = true;
    else if (a == "--layout") opt.layout = std::atoi(next());
    else if (a == "--showcase") {
      opt.showcase = next();
      opt.aiBattle = true;
    } else if (a == "--showcase-warm") opt.showcaseWarm = std::atoi(next()) * 15;
    else if (a == "--showcase-seconds") opt.showcaseSeconds = std::atoi(next());
    else if (a == "--flow-selftest") opt.flowSelfTest = true;
    else if (a == "--mods-selftest") {
      opt.modsSelfTest = true;
      opt.aiBattle = true;
    } else if (a == "--tool") opt.tool = next();
    else if (a == "--campaign") opt.campaign = next();
    else if (a == "--mission") opt.mission = next();
    else if (a == "--biome") {
      slave::Biome b;
      if (slave::parseBiome(next(), b)) opt.biome = static_cast<int>(b);
    }
    else if (a == "--open") opt.loadMap = next();
    else if (a == "--help" || a == "-h") { usage(); return 0; }
    else SLV_WARN("Unknown option: %s", a.c_str());
  }

  if (!importDir.empty()) {
    std::string contentDir = "content/";
    if (const char* base = SDL_GetBasePath()) contentDir = std::string(base) + "content/";
    return importBlitzkrieg(importDir, contentDir);
  }

  slave::App app;
  if (!app.init(cfg)) return 1;
  slave::log::init((app.userDir() + "legion.log").c_str());
  slave::crash::install(app.userDir(), "0.9.0");
  SLV_INFO("Legion Classic 0.9.0 on Slave Engine 1.0");

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
      if (opt.exportSprites.empty()) app.run(scene);  // exporting sprites: done once the art is built
    }
  }
  app.shutdown();
  slave::log::shutdown();
  return exitCode;
}

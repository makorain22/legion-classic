// Graphics quality: the preset in use, choosing one for this machine, the
// Settings page's part and the console's commands.
#include <imgui.h>

#include <thread>

#include "game_scene.hpp"
#include "pioneer/core/console.hpp"
#include "pioneer/render/device.hpp"
#include "pioneer/ui/fonts.hpp"

using namespace pioneer;

namespace legion {

void GameScene::initGraphics() {
  gfx::queryCaps();
  std::string why;
  if (!post_.init(why)) PNR_WARN("No post-processing: %s", why.c_str());

  gfx::Preset fromOption;
  if (!opt_.gfx.empty() && gfx::presetFromId(opt_.gfx, fromOption)) {
    // --gfx: for this run only, nothing written.
    gfx::Options o = gfx::presetOptions(fromOption);
    o.colourBlind = settings_.graphics().colourBlind;
    settings_.setGraphics(o);
    settings_.graphicsPreset = gfx::presetId(fromOption);
  } else if (app_.offscreen()) {
    // Automated runs draw the same way on every machine: Medium, so the pipeline is exercised.
    settings_.setGraphics(gfx::presetOptions(gfx::Preset::Medium));
    settings_.graphicsPreset = "medium";
  } else if (settings_.graphicsPreset == "auto") {
    detectGraphics(true);
  }
  applyGraphics();
}

void GameScene::applyGraphics() {
  gfx_ = gfx::effective(settings_.graphics(), post_.available(), post_.hdr());
  if (opt_.noShadows) gfx_.shadows = false;
  if (gfx_.shadows && (!shadow_.ready() || shadow_.size() != gfx_.shadowSize)) {
    std::string err;
    if (!shadow_.init(gfx_.shadowSize, err) && gfx_.shadowSize > 1024) shadow_.init(1024, err);
    if (!shadow_.ready()) PNR_WARN("No shadows: %s", err.c_str());
  }
}

void GameScene::setGraphicsPreset(gfx::Preset p, bool byGame) {
  gfx::Options o = gfx::presetOptions(p);
  o.colourBlind = settings_.graphics().colourBlind;
  settings_.setGraphics(o);
  settings_.graphicsPreset = gfx::presetId(p);
  settings_.graphicsAuto = byGame;
  detectFrames_ = -1;
  applyGraphics();
}

void GameScene::detectGraphics(bool measure) {
  const gfx::Caps& c = gfx::caps();
  gfx::Machine m;
  m.vendor = c.vendor;
  m.renderer = c.renderer;
  m.software = c.software;
  m.floatTargets = c.floatTargets && post_.available();
  m.maxTextureSize = c.maxTextureSize;
  m.cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
  m.pixels = app_.pixelWidth() * app_.pixelHeight();
  std::string why;
  const gfx::Preset first = gfx::autoDetect(m, &why);
  PNR_INFO("Graphics preset for this machine: %s (%s)", gfx::presetName(first), why.c_str());
  setGraphicsPreset(first, true);
  // A card that passed by name is then timed at Medium over the first frames
  // of a battle, and the preset settled by what it managed.
  if (measure && first != gfx::Preset::Low) {
    gfx::Options o = gfx::presetOptions(gfx::Preset::Medium);
    o.colourBlind = settings_.graphics().colourBlind;
    settings_.setGraphics(o);
    applyGraphics();
    detectFrames_ = 0;
    detectMs_ = 0;
  }
}

void GameScene::detectFrame(double sceneMs) {
  // The first frames build meshes and textures: not counted.
  static constexpr int kSkip = 45, kCount = 120;
  if (detectFrames_ < 0) return;
  if (++detectFrames_ <= kSkip) return;
  detectMs_ += sceneMs;
  if (detectFrames_ < kSkip + kCount) return;
  const gfx::Caps& c = gfx::caps();
  gfx::Machine m;
  m.vendor = c.vendor;
  m.renderer = c.renderer;
  m.software = c.software;
  m.floatTargets = c.floatTargets && post_.available();
  m.maxTextureSize = c.maxTextureSize;
  m.cores = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
  m.sceneMs = static_cast<float>(detectMs_ / kCount);
  std::string why;
  const gfx::Preset p = gfx::autoDetect(m, &why);
  PNR_INFO("Graphics preset settled: %s (%s)", gfx::presetName(p), why.c_str());
  setGraphicsPreset(p, true);
  saveSettings();
}

void GameScene::drawGraphicsSettings() {
  Settings& st = settings_;
  const float s = pioneer::ui::scale();
  bool changed = false;
  gfx::Options o = st.graphics();
  const gfx::Preset now = gfx::matchPreset(o);

  ImGui::SeparatorText("Graphics quality");
  ImGui::SetNextItemWidth(200 * s);
  if (ImGui::BeginCombo("Quality", gfx::presetName(now))) {
    for (int i = 0; i < gfx::kPresetCount; ++i) {
      const gfx::Preset p = static_cast<gfx::Preset>(i);
      if (ImGui::Selectable(gfx::presetName(p), p == now)) {
        setGraphicsPreset(p, false);
        saveSettings();
        o = st.graphics();
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", gfx::presetSummary(p));
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Choose for this machine")) {
    detectGraphics(mode_ != Mode::Menu || sim_ != nullptr);
    saveSettings();
    o = st.graphics();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Looks at the graphics card, times the battlefield for a couple of seconds\nand picks the quality it can keep up with.");
  if (detectFrames_ >= 0) ImGui::TextDisabled("Timing the battlefield...");
  else if (st.graphicsAuto && now != gfx::Preset::Custom) ImGui::TextDisabled("%s (chosen for this machine). %s", gfx::presetName(now), gfx::presetSummary(now));
  else ImGui::TextDisabled("%s", gfx::presetSummary(now));

  if (ImGui::TreeNode("Details")) {
    const bool canPost = post_.available();
    changed |= ImGui::Checkbox("Shadows", &o.shadows);
    if (o.shadows) {
      ImGui::SameLine();
      static const char* const kSizes[] = {"soft (1024)", "sharp (2048)", "sharpest (4096)"};
      int idx = o.shadowSize <= 1024 ? 0 : o.shadowSize <= 2048 ? 1 : 2;
      ImGui::SetNextItemWidth(160 * s);
      if (ImGui::Combo("##shadowsize", &idx, kSizes, 3)) {
        o.shadowSize = 1024 << idx;
        changed = true;
      }
    }
    ImGui::BeginDisabled(!canPost);
    changed |= ImGui::Checkbox("Effects over the picture (time of day, mist, glow)", &o.post);
    if (!canPost) ImGui::TextDisabled("This graphics card can't draw them.");
    ImGui::BeginDisabled(!o.post);
    ImGui::Indent();
    ImGui::BeginDisabled(!post_.hdr());
    changed |= ImGui::Checkbox("Glow round fire and flashes", &o.bloom);
    ImGui::EndDisabled();
    {
      static const char* const kEdges[] = {"Off", "Fast", "Best (4x)"};
      ImGui::SetNextItemWidth(160 * s);
      changed |= ImGui::Combo("Smoothed edges", &o.antialias, kEdges, 3);
    }
    changed |= ImGui::Checkbox("Mist in low ground", &o.groundFog);
    changed |= ImGui::Checkbox("Clouds' shadows", &o.cloudShadows);
    int percent = static_cast<int>(o.renderScale * 100.0f + 0.5f);
    ImGui::SetNextItemWidth(200 * s);
    if (ImGui::SliderInt("Battlefield resolution", &percent, 50, 100, "%d%%")) {
      o.renderScale = static_cast<float>(percent) / 100.0f;
      changed = true;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Draws the battlefield smaller and stretches it to the window: faster on a slow\ngraphics card, softer to look at. The interface stays sharp.");
    ImGui::Unindent();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    static const char* const kWeather[] = {"None", "Thin", "Full"};
    ImGui::SetNextItemWidth(160 * s);
    changed |= ImGui::Combo("Rain and snow in the air", &o.weather, kWeather, 3);
    ImGui::TreePop();
  }
  {
    int cb = static_cast<int>(o.colourBlind);
    ImGui::SetNextItemWidth(260 * s);
    if (ImGui::BeginCombo("Colour filter", gfx::colourBlindName(o.colourBlind))) {
      for (int i = 0; i < 4; ++i)
        if (ImGui::Selectable(gfx::colourBlindName(static_cast<gfx::ColourBlind>(i)), cb == i)) {
          o.colourBlind = static_cast<gfx::ColourBlind>(i);
          changed = true;
        }
      ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Shifts colours that are hard to tell apart with a colour vision deficiency.\nNeeds \"Effects over the picture\"; the interface's own colours are not changed.");
  }
  if (changed) {
    st.setGraphics(o);
    st.graphicsPreset = gfx::presetId(gfx::matchPreset(st.graphics()));
    st.graphicsAuto = false;
    detectFrames_ = -1;
    applyGraphics();
    saveSettings();
  }
}

void GameScene::registerGraphicsCommands() {
  Console& con = Console::global();
  con.addCommand("gfx.info", "gfx.info - the graphics card, the quality in use and what each option is set to.",
                 [this](const ConsoleArgs&, Console& c) {
                   const gfx::Caps& k = gfx::caps();
                   c.printFmt("%s, %s (%s)", k.renderer.c_str(), k.version.c_str(), k.vendor.c_str());
                   c.printFmt("textures to %d, float targets %s, post-processing %s%s", k.maxTextureSize, k.floatTargets ? "yes" : "no",
                              post_.available() ? "available" : "not available", k.software ? ", drawn by the processor" : "");
                   const gfx::Options& o = gfx_;
                   c.printFmt("quality: %s%s", gfx::presetName(gfx::matchPreset(settings_.graphics())), settings_.graphicsAuto ? " (chosen for this machine)" : "");
                   c.printFmt("post %d, scale %d%%, shadows %d (%d), bloom %d, edges %d, groundfog %d, clouds %d, weather %d, filter %s", o.post,
                              static_cast<int>(o.renderScale * 100.0f + 0.5f), o.shadows, o.shadowSize, o.bloom, o.antialias, o.groundFog,
                              o.cloudShadows, o.weather, gfx::colourBlindName(o.colourBlind));
                   if (post_.available() && gfx_.post) c.printFmt("scene %dx%d; the card took %.2f ms over it", post_.sceneWidth(), post_.sceneHeight(), static_cast<double>(gpuScene_.ms()));
                 });
  con.addCommand("gfx.preset", "gfx.preset [low|medium|high|ultra|auto] - the graphics quality (auto: chosen for this machine again).",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 2) return c.print(gfx::presetName(gfx::matchPreset(settings_.graphics())));
                   gfx::Preset p;
                   if (a.str(1) == "auto") detectGraphics(sim_ != nullptr);
                   else if (gfx::presetFromId(a.str(1), p) && p != gfx::Preset::Custom) setGraphicsPreset(p, false);
                   else return c.error("low, medium, high, ultra or auto");
                   if (!app_.offscreen()) saveSettings();
                   c.print(gfx::presetName(gfx::matchPreset(settings_.graphics())));
                 });
  con.addCommand("gfx.set", "gfx.set <post|scale|shadows|shadowsize|bloom|edges|groundfog|clouds|weather|filter> <value> - one "
                            "graphics option (scale in percent, edges 0-2, filter 0-3).",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 3) return c.print("gfx.set <option> <value>; gfx.info shows them");
                   gfx::Options o = settings_.graphics();
                   const std::string k = a.str(1);
                   const int v = std::atoi(a.str(2).c_str());
                   if (k == "post") o.post = v != 0;
                   else if (k == "scale") o.renderScale = static_cast<float>(v) / 100.0f;
                   else if (k == "shadows") o.shadows = v != 0;
                   else if (k == "shadowsize") o.shadowSize = v;
                   else if (k == "bloom") o.bloom = v != 0;
                   else if (k == "edges") o.antialias = v;
                   else if (k == "groundfog") o.groundFog = v != 0;
                   else if (k == "clouds") o.cloudShadows = v != 0;
                   else if (k == "weather") o.weather = v;
                   else if (k == "filter") o.colourBlind = static_cast<gfx::ColourBlind>(std::clamp(v, 0, 3));
                   else return c.error("no such option: " + k);
                   settings_.setGraphics(o);
                   settings_.graphicsPreset = gfx::presetId(gfx::matchPreset(settings_.graphics()));
                   settings_.graphicsAuto = false;
                   detectFrames_ = -1;
                   applyGraphics();
                   if (!app_.offscreen()) saveSettings();
                 });
}

}  // namespace legion

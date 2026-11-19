// Graphics options, presets and auto-detection (no graphics calls: built
// into the tests too).
#include "pioneer/render/graphics.hpp"

#include <algorithm>
#include <cstdio>

namespace pioneer::gfx {

bool Options::operator==(const Options& o) const {
  return post == o.post && renderScale == o.renderScale && shadows == o.shadows && shadowSize == o.shadowSize &&
         bloom == o.bloom && antialias == o.antialias && groundFog == o.groundFog && cloudShadows == o.cloudShadows &&
         weather == o.weather && colourBlind == o.colourBlind;
}

Options presetOptions(Preset p) {
  Options o;
  switch (p) {
    case Preset::Low:  // as the engine drew before 2.0, at three quarters of the size
      o.post = false;
      o.renderScale = 1.0f;  // (scaling needs the pipeline: without it the scene is drawn at full size)
      o.shadows = false;
      o.shadowSize = 1024;
      o.bloom = o.groundFog = o.cloudShadows = false;
      o.antialias = 2;  // (the window's own multisampling, as before)
      o.weather = 1;
      break;
    case Preset::Medium:
      o.post = true;
      o.renderScale = 1.0f;
      o.shadows = true;
      o.shadowSize = 1024;
      o.bloom = false;
      o.antialias = 1;
      o.groundFog = true;
      o.cloudShadows = true;
      o.weather = 1;
      break;
    case Preset::High:
    case Preset::Custom:
      o.post = true;
      o.renderScale = 1.0f;
      o.shadows = true;
      o.shadowSize = 2048;
      o.bloom = true;
      o.antialias = 2;
      o.groundFog = true;
      o.cloudShadows = true;
      o.weather = 2;
      break;
    case Preset::Ultra:
      o.post = true;
      o.renderScale = 1.0f;
      o.shadows = true;
      o.shadowSize = 4096;
      o.bloom = true;
      o.antialias = 2;
      o.groundFog = true;
      o.cloudShadows = true;
      o.weather = 2;
      break;
  }
  return o;
}

Preset matchPreset(const Options& o) {
  Options plain = o;
  plain.colourBlind = ColourBlind::Off;
  for (int i = 0; i < kPresetCount; ++i)
    if (presetOptions(static_cast<Preset>(i)) == plain) return static_cast<Preset>(i);
  return Preset::Custom;
}

const char* presetName(Preset p) {
  static const char* const kNames[] = {"Low", "Medium", "High", "Ultra", "Custom"};
  return kNames[std::clamp(static_cast<int>(p), 0, 4)];
}
const char* presetId(Preset p) {
  static const char* const kIds[] = {"low", "medium", "high", "ultra", "custom"};
  return kIds[std::clamp(static_cast<int>(p), 0, 4)];
}
bool presetFromId(std::string_view id, Preset& out) {
  for (int i = 0; i <= 4; ++i)
    if (id == presetId(static_cast<Preset>(i))) {
      out = static_cast<Preset>(i);
      return true;
    }
  return false;
}
const char* presetSummary(Preset p) {
  switch (p) {
    case Preset::Low: return "For old and slow machines: no shadows, no effects over the picture, thin rain and snow.";
    case Preset::Medium: return "Shadows, mist and clouds' shadows, time of day. No glow; edges smoothed the fast way.";
    case Preset::High: return "Sharper shadows, glow round fire and flashes, the best edge smoothing, full rain and snow.";
    case Preset::Ultra: return "As High with the sharpest shadows.";
    default: return "Your own choice of the options below.";
  }
}

Options sanitized(Options o) {
  o.renderScale = std::clamp(o.renderScale, 0.5f, 1.0f);
  o.shadowSize = o.shadowSize <= 1024 ? 1024 : o.shadowSize <= 2048 ? 2048 : 4096;
  o.weather = std::clamp(o.weather, 0, 2);
  o.antialias = std::clamp(o.antialias, 0, 2);
  if (static_cast<int>(o.colourBlind) < 0 || static_cast<int>(o.colourBlind) > 3) o.colourBlind = ColourBlind::Off;
  return o;
}

Options effective(Options o, bool postAvailable, bool hdrAvailable) {
  o = sanitized(o);
  if (!postAvailable) o.post = false;
  if (!o.post) {
    o.bloom = o.groundFog = o.cloudShadows = false;
    o.antialias = 2;  // the window's own
    o.renderScale = 1.0f;
  }
  if (!hdrAvailable) o.bloom = false;  // nothing is brighter than white without float targets
  return o;
}

bool softwareRenderer(std::string_view renderer) {
  std::string r(renderer);
  std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const char* name : {"llvmpipe", "softpipe", "swiftshader", "software rasterizer", "microsoft basic render", "gdi generic", "swrast"})
    if (r.find(name) != std::string::npos) return true;
  return false;
}

Preset autoDetect(const Machine& m, std::string* why) {
  auto say = [&](Preset p, const std::string& reason) {
    if (why) *why = reason;
    return p;
  };
  if (m.software || softwareRenderer(m.renderer))
    return say(Preset::Low, "the picture is drawn by the processor (" + m.renderer + "), not a graphics card");
  if (!m.floatTargets) return say(Preset::Low, "the graphics card can't draw the effects (" + m.renderer + ")");
  if (m.maxTextureSize < 4096) return say(Preset::Low, "an old graphics card (" + m.renderer + ")");
  if (m.sceneMs > 0.0f) {
    char buf[160];
    // The scene was drawn at Medium. What it may cost at the chosen preset:
    // a frame at 60 a second has 16.7 ms for everything, the scene about half.
    const Preset p = m.sceneMs > 14.0f ? Preset::Low : m.sceneMs > 6.5f ? Preset::Medium : m.sceneMs > 2.5f ? Preset::High : Preset::Ultra;
    std::snprintf(buf, sizeof buf, "the battlefield took %.1f ms a frame to draw on %s", static_cast<double>(m.sceneMs), m.renderer.c_str());
    return say(p, buf);
  }
  // Not measured: by what the card is called. Integrated graphics of the
  // early 2010s and before get Medium, anything else High.
  std::string r = m.renderer;
  std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const char* old : {"gma", "hd graphics 2", "hd graphics 3", "hd graphics 4", "hd graphics 5", "mali-4", "adreno (tm) 3", "powervr sgx", "geforce 8", "geforce 9", "radeon hd 4", "radeon hd 5"})
    if (r.find(old) != std::string::npos) return say(Preset::Medium, "an older graphics card (" + m.renderer + ")");
  if (r.find("intel") != std::string::npos && r.find("arc") == std::string::npos && r.find("iris") == std::string::npos)
    return say(Preset::Medium, "integrated graphics (" + m.renderer + ")");
  if (m.cores <= 2) return say(Preset::Medium, "a processor with two cores or fewer");
  return say(Preset::High, "a graphics card that should manage it (" + m.renderer + ")");
}

std::string shaderSource(std::string_view source, Api api) {
  if (api == Api::GL33) return std::string(source);
  // Leading blank lines aside, the first line must be the #version line.
  size_t at = source.find_first_not_of(" \t\r\n");
  if (at == std::string_view::npos || source.compare(at, 8, "#version") != 0) return std::string(source);
  size_t end = source.find('\n', at);
  if (end == std::string_view::npos) end = source.size();
  std::string out = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2DArray;\n"
                    "precision highp sampler2DShadow;\n";
  out.append(source.substr(std::min(end + 1, source.size())));
  return out;
}

const float* colourBlindMatrix(ColourBlind kind) {
  // Daltonisation: the part of a colour a viewer can't see (the difference
  // between it and a simulation of their sight) is moved into the channels
  // they can: corrected = colour + shift * (colour - simulated). The
  // simulations are the full-severity matrices of Machado, Oliveira and
  // Fernandes (2009); the shifts are those of the common "daltonize" filters
  // (the lost channel's error goes to the other two at 0.7). White stays white.
  static const float kOff[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  static const float kProtan[9] = {1.0000f, 0.0000f, 0.0000f, 0.4789f, 0.4769f, 0.0442f, 0.5973f, -0.6887f, 1.0914f};
  static const float kDeutan[9] = {1.0000f, 0.0000f, 0.0000f, 0.1628f, 0.7250f, 0.1122f, 0.4547f, -0.6454f, 1.1907f};
  static const float kTritan[9] = {0.7412f, -0.4072f, 0.6660f, 0.0751f, 0.5852f, 0.3397f, 0.0000f, 0.0000f, 1.0000f};
  switch (kind) {
    case ColourBlind::Protan: return kProtan;
    case ColourBlind::Deutan: return kDeutan;
    case ColourBlind::Tritan: return kTritan;
    default: return kOff;
  }
}
const char* colourBlindName(ColourBlind kind) {
  static const char* const kNames[] = {"Off", "Red-weak (protan)", "Green-weak (deutan)", "Blue-weak (tritan)"};
  return kNames[std::clamp(static_cast<int>(kind), 0, 3)];
}

}  // namespace pioneer::gfx

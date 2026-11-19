#pragma once
// Pioneer Engine - graphics options, presets and choosing one for a machine.
//
// Nothing here touches the graphics card: it is tables and decisions, so it
// is tested without a window. render/device.hpp asks the card what it can
// do; render/post.hpp does the drawing.
//
// The aim of the presets is that the game runs on anything. Low draws
// straight to the screen exactly as the engine did before 2.0; each step up
// adds what a faster machine can afford.

#include <string>
#include <string_view>

namespace pioneer::gfx {

enum class Api { GL33, GLES30 };

enum class Preset { Low, Medium, High, Ultra, Custom };
constexpr int kPresetCount = 4;  // without Custom

enum class ColourBlind { Off, Protan, Deutan, Tritan };

struct Options {
  bool post = true;           // the scene goes through the post-processing pipeline (off: straight to the screen)
  float renderScale = 1.0f;   // the 3D scene at this fraction of the window's size (0.5 .. 1), stretched to fit
  bool shadows = true;
  int shadowSize = 2048;      // 1024, 2048 or 4096
  bool bloom = false;         // a glow round fire and flashes
  int antialias = 2;          // smoothed edges: 0 none, 1 fast (FXAA, after the picture is made), 2 best (4x multisampling)
  bool groundFog = true;      // mist that lies in low ground (needs post)
  bool cloudShadows = true;   // clouds' shadows drifting over the ground (needs post)
  int weather = 2;            // rain and snow in the air: 0 none, 1 thin, 2 full
  ColourBlind colourBlind = ColourBlind::Off;  // not part of a preset: kept when the preset changes

  bool operator==(const Options& o) const;
};

// What a preset switches on. (colourBlind is left at Off: carry it over.)
Options presetOptions(Preset p);
// The preset these options are, or Custom.
Preset matchPreset(const Options& o);
const char* presetName(Preset p);                       // "Low" ...
const char* presetId(Preset p);                         // "low" ... (settings files, the console)
bool presetFromId(std::string_view id, Preset& out);    // false: not a preset's id
const char* presetSummary(Preset p);                    // one line for the settings page
// Keeps options within what the engine draws (scale 0.5..1, shadow sizes that exist...).
Options sanitized(Options o);
// What the options come to on a card that can't do everything: no float
// targets or no post means no bloom, fog or cloud shadows.
Options effective(Options o, bool postAvailable, bool hdrAvailable);

// What is known about the machine when a preset is chosen for it.
struct Machine {
  std::string vendor, renderer;  // as the driver names itself
  bool software = false;         // drawn by the processor (llvmpipe, SwiftShader, WARP...)
  bool floatTargets = true;      // can draw into half-float textures
  int maxTextureSize = 8192;
  int cores = 4;
  float sceneMs = 0.0f;          // measured: the 3D scene at Medium, per frame (0: not measured)
  int pixels = 1920 * 1080;      // of the window
};
bool softwareRenderer(std::string_view renderer);
// The preset for a machine and, in `why`, the reason in a sentence.
Preset autoDetect(const Machine& m, std::string* why = nullptr);

// Shaders are written as "#version 330 core"; this gives the source for the
// API in use (the first line replaced for OpenGL ES 3.0, with the precision
// lines it needs). Source that doesn't start with a #version line is
// returned as it is.
std::string shaderSource(std::string_view source, Api api);

// The matrix that makes colours easier to tell apart for a kind of colour
// blindness (row-major 3x3, applied to linear RGB; identity for Off).
const float* colourBlindMatrix(ColourBlind kind);
const char* colourBlindName(ColourBlind kind);

}  // namespace pioneer::gfx

// Graphics options without a window: presets, what a card can't do,
// choosing a preset for a machine, shaders for OpenGL ES, and the colour
// filters for colour-blind players.
#include <doctest/doctest.h>

#include <cmath>
#include <string>

#include "pioneer/render/graphics.hpp"

using namespace pioneer;
using namespace pioneer::gfx;

TEST_CASE("graphics: every preset is recognised as itself, and ids round-trip") {
  for (int i = 0; i < kPresetCount; ++i) {
    const Preset p = static_cast<Preset>(i);
    const Options o = presetOptions(p);
    CHECK(matchPreset(o) == p);
    CHECK(sanitized(o) == o);  // presets are within range already
    Preset back = Preset::Custom;
    REQUIRE(presetFromId(presetId(p), back));
    CHECK(back == p);
    CHECK(std::string(presetName(p)).size() > 2);
    CHECK(std::string(presetSummary(p)).size() > 10);
  }
  Preset back = Preset::Low;
  CHECK(presetFromId("custom", back));
  CHECK(back == Preset::Custom);
  CHECK_FALSE(presetFromId("extreme", back));
  CHECK_FALSE(presetFromId("", back));
}

TEST_CASE("graphics: the colour filter is not part of a preset; any other change makes it Custom") {
  Options o = presetOptions(Preset::High);
  o.colourBlind = ColourBlind::Deutan;
  CHECK(matchPreset(o) == Preset::High);
  o.bloom = false;
  CHECK(matchPreset(o) == Preset::Custom);
  Options m = presetOptions(Preset::Medium);
  m.renderScale = 0.75f;
  CHECK(matchPreset(m) == Preset::Custom);
}

TEST_CASE("graphics: presets only ever add as they go up") {
  // Low draws as the engine did before 2.0: no pipeline, no shadows.
  const Options low = presetOptions(Preset::Low);
  CHECK_FALSE(low.post);
  CHECK_FALSE(low.shadows);
  CHECK_FALSE(low.bloom);
  for (int i = 1; i < kPresetCount; ++i) {
    const Options a = presetOptions(static_cast<Preset>(i - 1)), b = presetOptions(static_cast<Preset>(i));
    CHECK(b.shadowSize >= a.shadowSize);
    CHECK(b.weather >= a.weather);
    CHECK(b.renderScale >= a.renderScale);
    CHECK((!a.post || b.post));
    CHECK((!a.shadows || b.shadows));
    CHECK((!a.bloom || b.bloom));
    CHECK(b.colourBlind == ColourBlind::Off);
  }
}

TEST_CASE("graphics: options are kept within what the engine draws") {
  Options o;
  o.renderScale = 0.1f;
  o.shadowSize = 3000;
  o.weather = 7;
  o.antialias = -3;
  o.colourBlind = static_cast<ColourBlind>(9);
  const Options s = sanitized(o);
  CHECK(s.renderScale == doctest::Approx(0.5f));
  CHECK(s.shadowSize == 4096);
  CHECK(s.weather == 2);
  CHECK(s.antialias == 0);
  CHECK(s.colourBlind == ColourBlind::Off);
  o.renderScale = 3.0f;
  o.shadowSize = 100;
  CHECK(sanitized(o).renderScale == doctest::Approx(1.0f));
  CHECK(sanitized(o).shadowSize == 1024);
  o.shadowSize = 2048;
  CHECK(sanitized(o).shadowSize == 2048);
}

TEST_CASE("graphics: what the options come to on a card that can't do everything") {
  Options ultra = presetOptions(Preset::Ultra);
  ultra.renderScale = 0.75f;
  ultra.antialias = 1;
  CHECK(effective(ultra, true, true) == ultra);
  // No float targets: nothing can be brighter than white, so no glow; the rest stays.
  const Options noHdr = effective(ultra, true, false);
  CHECK_FALSE(noHdr.bloom);
  CHECK(noHdr.post);
  CHECK(noHdr.groundFog);
  // No pipeline: straight to the screen at full size, with the window's own smoothing.
  const Options noPost = effective(ultra, false, true);
  CHECK_FALSE(noPost.post);
  CHECK_FALSE(noPost.bloom);
  CHECK_FALSE(noPost.groundFog);
  CHECK_FALSE(noPost.cloudShadows);
  CHECK(noPost.renderScale == doctest::Approx(1.0f));
  CHECK(noPost.antialias == 2);
  CHECK(noPost.shadows);  // shadows don't need the pipeline
  // Switched off by the player: the same.
  Options off = ultra;
  off.post = false;
  CHECK(effective(off, true, true) == noPost);
}

TEST_CASE("graphics: choosing a preset for a machine") {
  std::string why;
  Machine m;
  m.renderer = "llvmpipe (LLVM 17.0.6, 256 bits)";
  CHECK(softwareRenderer(m.renderer));
  CHECK(autoDetect(m, &why) == Preset::Low);
  CHECK(why.find("processor") != std::string::npos);
  CHECK(softwareRenderer("Microsoft Basic Render Driver"));
  CHECK(softwareRenderer("Google SwiftShader"));
  CHECK_FALSE(softwareRenderer("NVIDIA GeForce RTX 3060/PCIe/SSE2"));

  m = Machine{};
  m.renderer = "NVIDIA GeForce RTX 3060/PCIe/SSE2";
  m.software = true;  // said by the device layer, whatever the name
  CHECK(autoDetect(m) == Preset::Low);
  m.software = false;
  m.floatTargets = false;
  CHECK(autoDetect(m) == Preset::Low);
  m.floatTargets = true;
  m.maxTextureSize = 2048;
  CHECK(autoDetect(m) == Preset::Low);
  m.maxTextureSize = 16384;
  CHECK(autoDetect(m, &why) == Preset::High);
  CHECK(why.find("RTX 3060") != std::string::npos);
  m.cores = 2;
  CHECK(autoDetect(m) == Preset::Medium);
  m.cores = 8;

  // Measured: the time the battlefield took at Medium decides.
  m.sceneMs = 20.0f;
  CHECK(autoDetect(m) == Preset::Low);
  m.sceneMs = 9.0f;
  CHECK(autoDetect(m) == Preset::Medium);
  m.sceneMs = 4.0f;
  CHECK(autoDetect(m) == Preset::High);
  m.sceneMs = 1.0f;
  CHECK(autoDetect(m, &why) == Preset::Ultra);
  CHECK(why.find("1.0 ms") != std::string::npos);

  // Not measured: by the card's name.
  m.sceneMs = 0.0f;
  m.renderer = "Intel(R) HD Graphics 4000";
  CHECK(autoDetect(m) == Preset::Medium);
  m.renderer = "Mesa Intel(R) UHD Graphics 620 (KBL GT2)";
  CHECK(autoDetect(m) == Preset::Medium);
  m.renderer = "Intel(R) Arc(TM) A770 Graphics";
  CHECK(autoDetect(m) == Preset::High);
  m.renderer = "AMD Radeon HD 5450";
  CHECK(autoDetect(m) == Preset::Medium);
  m.renderer = "AMD Radeon RX 6700 XT";
  CHECK(autoDetect(m) == Preset::High);
}

TEST_CASE("graphics: shaders for OpenGL ES 3.0") {
  const std::string src = "#version 330 core\nin vec2 vUV;\nout vec4 c;\nvoid main() { c = vec4(vUV, 0.0, 1.0); }\n";
  CHECK(shaderSource(src, Api::GL33) == src);
  const std::string es = shaderSource(src, Api::GLES30);
  CHECK(es.rfind("#version 300 es\n", 0) == 0);
  CHECK(es.find("precision highp float;") != std::string::npos);
  CHECK(es.find("precision highp sampler2DShadow;") != std::string::npos);
  CHECK(es.find("330") == std::string::npos);
  CHECK(es.find("in vec2 vUV;\nout vec4 c;\nvoid main()") != std::string::npos);
  // Blank lines before the #version line are fine; no #version line: left alone.
  CHECK(shaderSource("\n  \n" + src, Api::GLES30) == es);
  CHECK(shaderSource("void main() {}\n", Api::GLES30) == "void main() {}\n");
  CHECK(shaderSource("#version 330 core", Api::GLES30).rfind("#version 300 es\n", 0) == 0);
}

namespace {
void apply(const float* m, const float in[3], float out[3]) {
  for (int r = 0; r < 3; ++r) out[r] = m[r * 3] * in[0] + m[r * 3 + 1] * in[1] + m[r * 3 + 2] * in[2];
}
}  // namespace

TEST_CASE("graphics: colour filters keep white white and grey grey") {
  const float* off = colourBlindMatrix(ColourBlind::Off);
  for (int i = 0; i < 9; ++i) CHECK(off[i] == (i % 4 == 0 ? 1.0f : 0.0f));
  for (ColourBlind k : {ColourBlind::Protan, ColourBlind::Deutan, ColourBlind::Tritan}) {
    CAPTURE(colourBlindName(k));
    const float* m = colourBlindMatrix(k);
    for (float g : {1.0f, 0.5f, 0.1f}) {
      const float grey[3] = {g, g, g};
      float out[3];
      apply(m, grey, out);
      for (float c : out) CHECK(c == doctest::Approx(g).epsilon(0.002));
    }
    // It does change colours: red and green come out further apart for the
    // red- and green-weak, blue and green for the blue-weak.
    const float red[3] = {1, 0, 0};
    float out[3];
    apply(m, red, out);
    CHECK(std::fabs(out[0] - 1.0f) + std::fabs(out[1]) + std::fabs(out[2]) > 0.2f);
  }
  CHECK(std::string(colourBlindName(ColourBlind::Deutan)).find("deutan") != std::string::npos);
}

TEST_CASE("graphics: the colour filters are the daltonisation of Machado et al.'s simulations") {
  // Full-severity simulation matrices (Machado, Oliveira and Fernandes 2009)
  // and the usual shifts of what is lost into what is seen:
  // corrected = c + shift * (c - simulated(c)).
  const float simP[9] = {0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f, 1.051998f};
  const float simD[9] = {0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f, 0.968881f};
  const float simT[9] = {1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f, 0.303900f};
  const float shiftRG[9] = {0, 0, 0, 0.7f, 1, 0, 0.7f, 0, 1};  // red's error into green and blue
  const float shiftB[9] = {1, 0, 0.7f, 0, 1, 0.7f, 0, 0, 0};   // blue's error into red and green
  struct Case {
    ColourBlind kind;
    const float* sim;
    const float* shift;
  };
  for (const Case& c : {Case{ColourBlind::Protan, simP, shiftRG}, Case{ColourBlind::Deutan, simD, shiftRG}, Case{ColourBlind::Tritan, simT, shiftB}}) {
    CAPTURE(colourBlindName(c.kind));
    const float* m = colourBlindMatrix(c.kind);
    for (int r = 0; r < 3; ++r)
      for (int col = 0; col < 3; ++col) {
        float want = r == col ? 1.0f : 0.0f;
        for (int k = 0; k < 3; ++k) want += c.shift[r * 3 + k] * ((k == col ? 1.0f : 0.0f) - c.sim[k * 3 + col]);
        CHECK(m[r * 3 + col] == doctest::Approx(want).epsilon(0.0005));
      }
  }
}

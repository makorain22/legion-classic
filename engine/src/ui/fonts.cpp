#include "pioneer/ui/fonts.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "pioneer/core/log.hpp"

namespace pioneer::ui {

namespace {
FontSet g_fonts;
float g_scale = 1.0f;

// The atlas takes ownership of the bytes (it frees them with ImGui's allocator).
ImFont* add(const vfs::Vfs& fs, const char* path, bool merge) {
  std::vector<uint8_t> bytes;
  std::string err;
  if (!fs.read(path, bytes, &err) || bytes.empty()) return nullptr;
  void* copy = ImGui::MemAlloc(bytes.size());
  std::memcpy(copy, bytes.data(), bytes.size());
  ImFontConfig cfg;
  cfg.MergeMode = merge;
  cfg.FontDataOwnedByAtlas = true;
  if (merge) {
    // Icons sit on the text's line: a little lower and a little smaller than their box.
    cfg.GlyphOffset = ImVec2(0.0f, 2.0f);
    cfg.GlyphMinAdvanceX = 14.0f;
  }
  std::snprintf(cfg.Name, sizeof(cfg.Name), "%s", path);
  return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(copy, static_cast<int>(bytes.size()), 0.0f, &cfg);
}
}  // namespace

bool loadFonts(const vfs::Vfs& fs, float baseSize) {
  ImGuiIO& io = ImGui::GetIO();
  g_fonts = FontSet{};
  g_fonts.baseSize = baseSize;
  io.Fonts->Clear();
  g_fonts.regular = add(fs, "fonts/ui-regular.ttf", false);
  if (!g_fonts.regular) {
    PNR_WARN_CH(Core, "fonts/ui-regular.ttf not found: the built-in font is used");
    io.Fonts->AddFontDefault();
    ImGui::GetStyle().FontSizeBase = 13.0f;
    return false;
  }
  g_fonts.icons = add(fs, "fonts/icons.ttf", true) != nullptr;
  g_fonts.bold = add(fs, "fonts/ui-bold.ttf", false);
  if (g_fonts.bold) {
    if (g_fonts.icons) add(fs, "fonts/icons.ttf", true);
  } else {
    g_fonts.bold = g_fonts.regular;
  }
  g_fonts.mono = add(fs, "fonts/mono.ttf", false);
  if (!g_fonts.mono) g_fonts.mono = g_fonts.regular;
  g_fonts.loaded = true;
  io.FontDefault = g_fonts.regular;
  ImGui::GetStyle().FontSizeBase = baseSize;
  ImGui::GetStyle().FontScaleMain = g_scale;
  PNR_INFO_CH(Core, "Interface fonts loaded (%s icons), %.0f px at 100%%", g_fonts.icons ? "with" : "without", static_cast<double>(baseSize));
  return true;
}

const FontSet& fonts() { return g_fonts; }

void setScale(float s) {
  s = std::clamp(s, 0.75f, 2.5f);
  if (s == g_scale) return;
  ImGuiStyle& style = ImGui::GetStyle();
  style.ScaleAllSizes(s / g_scale);  // paddings, spacings, rounding, scrollbars
  style.FontScaleMain = s;
  g_scale = s;
}
float scale() { return g_scale; }

Bold::Bold() { ImGui::PushFont(g_fonts.bold, 0.0f); }
Bold::~Bold() { ImGui::PopFont(); }
Mono::Mono() { ImGui::PushFont(g_fonts.mono, 0.0f); }
Mono::~Mono() { ImGui::PopFont(); }
Sized::Sized(float times) { ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * times); }
Sized::~Sized() { ImGui::PopFont(); }

struct Unscaled::Saved {
  ImGuiStyle style;
};
Unscaled::Unscaled() {
  if (g_scale == 1.0f) return;
  saved_ = new Saved{ImGui::GetStyle()};
  ImGui::GetStyle().ScaleAllSizes(1.0f / g_scale);
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase / g_scale);
}
Unscaled::~Unscaled() {
  if (!saved_) return;
  ImGui::PopFont();
  ImGui::GetStyle() = saved_->style;
  delete saved_;
}

}  // namespace pioneer::ui

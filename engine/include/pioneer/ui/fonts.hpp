#pragma once
// Pioneer Engine - interface fonts and scale.
//
// The interface is set in real fonts: a text face in two weights, a
// fixed-width face and an icon font merged into both text faces, loaded
// through the virtual file system from fonts/ (so a mod can replace them).
// Dear ImGui (1.92+) rasterises glyphs at whatever size is asked for, so
// text is sharp at every interface size.
//
// The interface size is one number, ui::scale(): 1 is the size the screens
// were laid out for. It scales fonts and the style's paddings and spacings.
// Layout code that uses fixed pixel sizes multiplies them with ui::px().

#include "pioneer/core/vfs.hpp"

struct ImFont;  // (Dear ImGui is not included here: files set their own options before including it)

namespace pioneer::ui {

struct FontSet {
  ImFont* regular = nullptr;
  ImFont* bold = nullptr;
  ImFont* mono = nullptr;
  bool icons = false;   // the icon font was found and merged in
  bool loaded = false;  // false: Dear ImGui's built-in font is in use
  float baseSize = 15.0f;
};

// Loads fonts/ui-regular.ttf, ui-bold.ttf, mono.ttf and icons.ttf. Call once
// after the content is mounted and before the first frame. False (and the
// built-in font) if the text face is missing.
bool loadFonts(const vfs::Vfs& fs, float baseSize = 15.0f);
const FontSet& fonts();

// The interface size: 1 = as designed. Clamped to 0.75..2.5.
void setScale(float scale);
float scale();
// A length in pixels at the current interface size.
inline float px(float v) { return v * scale(); }

// Text in another face or size while it is alive:  { ui::Bold b; ImGui::Text(...); }
struct Bold {
  Bold();
  ~Bold();
};
struct Mono {
  Mono();
  ~Mono();
};
struct Sized {  // `times` the normal size
  explicit Sized(float times);
  ~Sized();
};
// A screen laid out in fixed pixels that has not been made to follow the
// interface size yet: while this is alive, fonts and style are as at 100%.
struct Unscaled {
  Unscaled();
  ~Unscaled();
  Unscaled(const Unscaled&) = delete;
  Unscaled& operator=(const Unscaled&) = delete;

 private:
  struct Saved;
  Saved* saved_ = nullptr;
};

}  // namespace pioneer::ui

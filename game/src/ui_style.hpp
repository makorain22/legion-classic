#pragma once
// Legion Classic - the look of the command interface: steel plates
// with bevelled edges and rivets, brass highlights and stencilled labels, in
// the manner of the period RTS games it follows.

#include <cstdint>
#include <imgui.h>

#include <functional>
#include <string>

namespace legion::ui {

// Palette. Set by setFaction: olive steel for the menus, field grey for the
// Germans, khaki for the Soviets, brown service drab and stone for the British.
inline ImU32 kPlate = IM_COL32(58, 60, 46, 255);       // steel plate
inline ImU32 kPlateDark = IM_COL32(34, 36, 28, 255);
inline ImU32 kPlateLight = IM_COL32(96, 98, 76, 255);
inline ImU32 kInset = IM_COL32(22, 23, 18, 255);        // recessed wells
inline ImU32 kBrass = IM_COL32(214, 176, 92, 255);      // highlight metal
inline ImU32 kBrassDim = IM_COL32(150, 122, 64, 255);
inline ImU32 kStencil = IM_COL32(222, 212, 182, 255);   // label text
inline ImU32 kStencilDim = IM_COL32(140, 134, 114, 255);
constexpr ImU32 kGood = IM_COL32(124, 214, 96, 255);
constexpr ImU32 kBad = IM_COL32(226, 92, 70, 255);

// Good and bad on the battlefield (health, valid placement, orders): green and red, or colour-blind safe ones
// (9.15). setColourBlind: 0 as designed, 1 red-green safe, 2 blue-yellow safe.
void setColourBlind(int mode);
int colourBlind();
uint32_t uiGood(uint8_t alpha = 255);
uint32_t uiBad(uint8_t alpha = 255);
uint32_t uiMiddling();
// A side's colour as the player sees it: its own, or slot `index` of a colour-blind safe palette.
uint32_t sideColour(uint32_t designed, int index);

enum class Theme { Olive, German, Soviet, British };
// Switches the palette: "ger", "rus", "gbr", anything else = olive.
void setFaction(const char* faction);
Theme theme();
// The side's emblem for decorations - a plain cross for the Germans, a red
// star for the Soviets, a blue-white-red roundel for the British, a plain brass
// ring otherwise.
void emblem(ImDrawList* dl, ImVec2 centre, float size);
// A painted stencil strip (side number, unit marking) across a plate.
void stencilStrip(ImDrawList* dl, ImVec2 a, ImVec2 b);

// A raised steel panel (or recessed when `inset`).
void panel(ImDrawList* dl, ImVec2 a, ImVec2 b, bool inset = false, float rounding = 2.0f);
// Rivets in the four corners, `margin` pixels in.
void rivets(ImDrawList* dl, ImVec2 a, ImVec2 b, float margin = 5.0f);
// An engraved section title: a dark strip with the label and a brass rule.
void sectionTitle(ImDrawList* dl, ImVec2 pos, float width, const char* label);
// A plate button. Returns true when clicked. `lit` = selected (brass),
// `flash` = blinking green (something is ready).
bool plateButton(const char* id, const char* label, ImVec2 size, bool lit = false, bool flash = false,
                 bool enabled = true);

// Little pictograms drawn with lines, for tabs and buttons.
enum class Picto { Building, Defence, Infantry, Vehicle, Book, Sell, Deploy, Stance, Formation, Plane };
void pictogram(ImDrawList* dl, Picto p, ImVec2 centre, float size, ImU32 col);
// A tab with a pictogram above a small label.
bool iconTab(const char* id, Picto p, const char* label, ImVec2 size, bool lit, bool flash);

// Called when a plate button or tab is clicked (the interface's click sounds).
void setClickSound(std::function<void(bool tab)> fn);
void clickSound(bool tab);
// Tutorials: the interface element to make glow ("tab.train", "build:gbr_power", "order.evacuate", "minimap"...).
void setHighlight(const std::string& id);
// After drawing an element: a pulsing frame round it when it is the one highlighted.
void glowIfHighlighted(const char* id);
// The hint panel a tutorial or mission shows: a title line, then the text.
void hintPanel(const std::string& text, float x, float y, float width);

// The theme for ordinary Dear ImGui widgets drawn on the plates.
void pushTheme();
void popTheme();

}  // namespace legion::ui

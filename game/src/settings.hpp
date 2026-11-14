#pragma once
// Legion Classic - player settings: display options and key bindings, saved
// as settings.json in the user folder.

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

namespace legion {

// Everything a key can be bound to.
enum class InputAction : uint8_t {
  ScrollUp, ScrollDown, ScrollLeft, ScrollRight,
  AttackMove, Stop, Deploy, SelectAll, BindGroup, UnbindGroup,
  Pause, SpeedUp, SpeedDown, CentreSelection, NextSide, Menu,
  Help, Editor, Debug, Screenshot, ToggleInfo, Encyclopedia, Evacuate,
  RotateLeft, RotateRight, TiltUp, TiltDown, ResetCamera, NextFormation,
  RadioLog, RangeRings,
  Console, Profiler,
  Count
};
constexpr int ACTION_COUNT = static_cast<int>(InputAction::Count);
const char* actionLabel(InputAction a);

enum : uint8_t { kModCtrl = 1, kModShift = 2, kModAlt = 4 };

struct KeyCombo {
  SDL_Keycode key = 0;  // 0 = unbound
  uint8_t mods = 0;
  bool empty() const { return key == 0; }
};
std::string comboName(const KeyCombo& k);
uint8_t modsOf(SDL_Keymod m);

struct Settings {
  // Display.
  int fpsLimit = 144;      // 0 = unlimited
  bool vsync = true;
  bool mouseLock = true;   // keep the cursor inside the window while playing
  bool edgeScroll = true;
  float scrollSpeed = 1.0f;
  float uiScale = 1.0f;    // interface size: 1 = as designed (0.9 .. 2)
  bool showInfoPanel = false;  // the top-left game info box
  bool showCards = true;       // NATO cards over bound units
  bool showEnemyCards = true;  // cards over the other sides' units too (when seen)
  bool showChips = true;       // chips for enemies detected in the fog
  int rangeRings = 1;          // circles showing weapon range: 0 off, 1 selection and the unit under the mouse, 2 every unit in view
  int campaignDifficulty = 1;  // 0 easy, 1 normal, 2 hard: enemy reinforcements and computer skill in missions
  bool atmosphere = true;      // colour grading and a vignette over the battlefield
  bool view3D = true;          // the 3D battlefield (false: the classic 2D sprite view)
  bool shadows = true;         // 3D: the sun's shadows
  bool artPack = true;
  // Sound: 0..1 each.
  float volMaster = 0.85f, volEffects = 1.0f, volVoices = 0.9f, volMusic = 0.6f, volAmbience = 0.8f, volInterface = 0.8f;         // use content/art/blitzkrieg when present (takes effect on restart)
  std::vector<std::string> mods;  // enabled mods (folder names), in load order
  std::string toolMod;            // the mod the rules editor and sprite studio save into
  // Two bindings per action.
  std::array<std::array<KeyCombo, 2>, ACTION_COUNT> keys{};

  Settings() { resetKeys(); }
  void resetKeys();
  // Exact match: the key with exactly these modifiers.
  bool pressed(InputAction a, const SDL_KeyboardEvent& e) const;
  // Held right now (for scrolling): modifiers must match too, so Ctrl+S never scrolls.
  bool held(InputAction a, const bool* keyboard, SDL_Keymod mods) const;

  bool load(const std::string& path);
  bool save(const std::string& path) const;
};

}  // namespace legion

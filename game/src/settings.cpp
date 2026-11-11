#include "settings.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

namespace legion {

const char* actionLabel(InputAction a) {
  static const char* names[] = {"Scroll up", "Scroll down", "Scroll left", "Scroll right",
                                "Attack-move", "Stop", "Deploy", "Select all units", "Bind selection into a unit",
                                "Unbind unit", "Pause", "Game speed up", "Game speed down", "Centre on selection",
                                "Command next side", "Menu", "Controls panel", "Scenario editor", "Debug panel",
                                "Screenshot", "Game info panel", "Encyclopedia",
                                "Evacuate (leave trenches and buildings)",
                                "Turn the view left", "Turn the view right", "Tilt the view up", "Tilt the view down",
                                "Reset the view", "Next formation for the selection"};
  return names[static_cast<int>(a)];
}

uint8_t modsOf(SDL_Keymod m) {
  return static_cast<uint8_t>(((m & SDL_KMOD_CTRL) ? kModCtrl : 0) | ((m & SDL_KMOD_SHIFT) ? kModShift : 0) |
                              ((m & SDL_KMOD_ALT) ? kModAlt : 0));
}

std::string comboName(const KeyCombo& k) {
  if (k.empty()) return "-";
  std::string s;
  if (k.mods & kModCtrl) s += "Ctrl+";
  if (k.mods & kModShift) s += "Shift+";
  if (k.mods & kModAlt) s += "Alt+";
  return s + SDL_GetKeyName(k.key);
}

void Settings::resetKeys() {
  for (auto& k : keys) k = {};
  auto set = [&](InputAction a, KeyCombo k1, KeyCombo k2 = {}) { keys[static_cast<size_t>(a)] = {k1, k2}; };
  set(InputAction::ScrollUp, {SDLK_W, 0}, {SDLK_UP, 0});
  set(InputAction::ScrollDown, {SDLK_S, 0}, {SDLK_DOWN, 0});
  set(InputAction::ScrollLeft, {SDLK_A, 0}, {SDLK_LEFT, 0});
  set(InputAction::ScrollRight, {SDLK_D, 0}, {SDLK_RIGHT, 0});
  set(InputAction::AttackMove, {SDLK_A, kModCtrl}, {SDLK_F, 0});
  set(InputAction::Stop, {SDLK_S, kModCtrl}, {SDLK_X, 0});
  set(InputAction::Deploy, {SDLK_D, kModCtrl}, {SDLK_G, 0});
  set(InputAction::SelectAll, {SDLK_A, kModCtrl | kModShift});
  set(InputAction::BindGroup, {SDLK_B, kModCtrl});
  set(InputAction::UnbindGroup, {SDLK_U, kModCtrl});
  set(InputAction::Pause, {SDLK_SPACE, 0}, {SDLK_PAUSE, 0});
  set(InputAction::SpeedUp, {SDLK_EQUALS, 0}, {SDLK_KP_PLUS, 0});
  set(InputAction::SpeedDown, {SDLK_MINUS, 0}, {SDLK_KP_MINUS, 0});
  set(InputAction::CentreSelection, {SDLK_HOME, 0}, {SDLK_C, 0});
  set(InputAction::NextSide, {SDLK_TAB, 0});
  set(InputAction::Menu, {SDLK_ESCAPE, 0}, {SDLK_F10, 0});
  set(InputAction::Help, {SDLK_F1, 0});
  set(InputAction::Editor, {SDLK_F2, 0});
  set(InputAction::Debug, {SDLK_F3, 0});
  set(InputAction::Screenshot, {SDLK_F12, 0});
  set(InputAction::ToggleInfo, {SDLK_I, 0});
  set(InputAction::Encyclopedia, {SDLK_K, 0}, {SDLK_F4, 0});
  set(InputAction::Evacuate, {SDLK_E, 0}, {SDLK_E, kModCtrl});
  set(InputAction::RotateLeft, {SDLK_Q, 0});
  set(InputAction::RotateRight, {SDLK_R, 0});
  set(InputAction::TiltUp, {SDLK_PAGEUP, 0});
  set(InputAction::TiltDown, {SDLK_PAGEDOWN, 0});
  set(InputAction::ResetCamera, {SDLK_BACKSPACE, 0});
  set(InputAction::NextFormation, {SDLK_Z, 0});
}

bool Settings::pressed(InputAction a, const SDL_KeyboardEvent& e) const {
  const uint8_t m = modsOf(e.mod);
  for (const KeyCombo& k : keys[static_cast<size_t>(a)])
    if (!k.empty() && k.key == e.key && k.mods == m) return true;
  return false;
}

bool Settings::held(InputAction a, const bool* keyboard, SDL_Keymod mods) const {
  const uint8_t m = modsOf(mods);
  for (const KeyCombo& k : keys[static_cast<size_t>(a)]) {
    if (k.empty() || k.mods != m) continue;
    const SDL_Scancode sc = SDL_GetScancodeFromKey(k.key, nullptr);
    if (sc != SDL_SCANCODE_UNKNOWN && keyboard[sc]) return true;
  }
  return false;
}

bool Settings::load(const std::string& path) {
  std::ifstream in(path);
  if (!in) return false;
  const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (!j.is_object()) return false;
  fpsLimit = j.value("fpsLimit", fpsLimit);
  vsync = j.value("vsync", vsync);
  mouseLock = j.value("mouseLock", mouseLock);
  edgeScroll = j.value("edgeScroll", edgeScroll);
  scrollSpeed = j.value("scrollSpeed", scrollSpeed);
  showInfoPanel = j.value("showInfoPanel", showInfoPanel);
  showCards = j.value("showCards", showCards);
  showChips = j.value("showChips", showChips);
  artPack = j.value("artPack", artPack);
  showEnemyCards = j.value("showEnemyCards", showEnemyCards);
  atmosphere = j.value("atmosphere", atmosphere);
  view3D = j.value("view3D", view3D);
  shadows = j.value("shadows", shadows);
  if (j.contains("sound") && j["sound"].is_object()) {
    const auto& s = j["sound"];
    volMaster = s.value("master", volMaster);
    volEffects = s.value("effects", volEffects);
    volVoices = s.value("voices", volVoices);
    volMusic = s.value("music", volMusic);
    volAmbience = s.value("ambience", volAmbience);
    volInterface = s.value("interface", volInterface);
  }
  mods.clear();
  if (j.contains("mods") && j["mods"].is_array())
    for (const auto& m : j["mods"])
      if (m.is_string()) mods.push_back(m.get<std::string>());
  toolMod = j.value("toolMod", toolMod);
  if (j.contains("keys") && j["keys"].is_object()) {
    for (int a = 0; a < ACTION_COUNT; ++a) {
      const std::string name = actionLabel(static_cast<InputAction>(a));
      if (!j["keys"].contains(name)) continue;
      const auto& arr = j["keys"][name];
      for (size_t i = 0; i < 2 && i < arr.size(); ++i)
        keys[static_cast<size_t>(a)][i] = {static_cast<SDL_Keycode>(arr[i].value("key", 0u)), arr[i].value("mods", uint8_t{0})};
    }
  }
  return true;
}

bool Settings::save(const std::string& path) const {
  nlohmann::json j;
  j["fpsLimit"] = fpsLimit;
  j["vsync"] = vsync;
  j["mouseLock"] = mouseLock;
  j["edgeScroll"] = edgeScroll;
  j["scrollSpeed"] = scrollSpeed;
  j["showInfoPanel"] = showInfoPanel;
  j["showCards"] = showCards;
  j["showChips"] = showChips;
  j["artPack"] = artPack;
  j["showEnemyCards"] = showEnemyCards;
  j["atmosphere"] = atmosphere;
  j["view3D"] = view3D;
  j["shadows"] = shadows;
  j["sound"] = {{"master", volMaster}, {"effects", volEffects}, {"voices", volVoices},
                {"music", volMusic},   {"ambience", volAmbience}, {"interface", volInterface}};
  j["mods"] = mods;
  j["toolMod"] = toolMod;
  auto& ks = j["keys"] = nlohmann::json::object();
  for (int a = 0; a < ACTION_COUNT; ++a) {
    auto& arr = ks[actionLabel(static_cast<InputAction>(a))] = nlohmann::json::array();
    for (const KeyCombo& k : keys[static_cast<size_t>(a)]) arr.push_back({{"key", k.key}, {"mods", k.mods}});
  }
  std::ofstream out(path);
  if (!out) return false;
  out << j.dump(1) << "\n";
  return true;
}

}  // namespace legion

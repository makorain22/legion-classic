#pragma once
// Legion Classic - menus: the main menu (over a battle between two computer
// players), skirmish setup, scenario, save and replay lists, the in-game menu
// and the replay controls.

#include <string>
#include <vector>

#include "game_scene.hpp"

namespace legion {

class Menus {
 public:
  explicit Menus(GameScene& scene);

  void draw();  // whatever is open
  bool inGameMenuOpen() const { return page_ == Page::InGame || page_ == Page::SaveGame || page_ == Page::LoadGame ||
                                       page_ == Page::Replays || page_ == Page::Settings; }
  void openInGameMenu();
  void close();
  bool wantsInput() const { return page_ != Page::None; }
  // Rebinding a key: the next key press goes here.
  bool capturingKey() const { return capture_ >= 0; }
  void captureKey(const SDL_KeyboardEvent& e);

 private:
  enum class Page { None, Main, Skirmish, Scenarios, LoadGame, Replays, InGame, SaveGame, About, Settings, Mods };
  struct FileEntry {
    std::string path, file, title, detail;
  };

  void drawMain();
  void drawSkirmish();
  void drawScenarios();
  void drawFileList(bool replays);
  void drawInGame();
  void drawSaveGame();
  void drawReplayBar();
  void drawAbout();
  void drawSettings();
  void drawMods();
  void openMods();
  void refreshFiles(const std::string& dir, const char* ext, bool replays);
  bool backButton();

  GameScene& scene_;
  Page page_ = Page::Main;
  Page returnTo_ = Page::Main;
  SkirmishSetup setup_;
  std::vector<FileEntry> files_;
  int selected_ = -1;
  char saveName_[96] = "";
  std::string error_;
  int capture_ = -1, captureSlot_ = 0;  // action being rebound
  std::vector<std::string> modsDraft_;  // the Mods page's enabled list, in load order
  std::string modSelected_;
  char newModName_[64] = "";
 public:
  void showMods() { openMods(); }  // (screenshots)
};

}  // namespace legion

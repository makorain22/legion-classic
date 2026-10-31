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
                                       page_ == Page::Replays; }
  void openInGameMenu();
  void close();
  bool wantsInput() const { return page_ != Page::None; }

 private:
  enum class Page { None, Main, Skirmish, Scenarios, LoadGame, Replays, InGame, SaveGame, About };
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
};

}  // namespace legion

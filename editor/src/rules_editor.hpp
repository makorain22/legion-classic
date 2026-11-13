#pragma once
// Legion Classic - the rules editor (Milestone 8 creation tools).
//
// Edits the merged rules (units, buildings, weapons, warheads) as forms, with
// every field explained, checks them as you type, and puts them into the
// running battle at once (the battle behind the main menu, or the one you are
// playing). "Save to mod" writes only what differs from the rules below the
// mod, as a mod's rules/*.json, so the base game is never touched.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "pioneer/core/content.hpp"

namespace legion {

class GameScene;

class RulesEditor {
 public:
  enum Kind { Units = 0, Buildings, Weapons, Warheads, KindCount };

  explicit RulesEditor(GameScene& scene);
  void open();
  void close() { open_ = false; }
  bool isOpen() const { return open_; }
  void draw();
  // The rules were reloaded (mods switched): start again from them.
  void reset();

  // For self-tests and screenshots.
  void select(Kind kind, const std::string& id);
  nlohmann::json* entry(Kind kind, const std::string& id);
  void edited();                                  // after changing entry() or working() from code
  nlohmann::json& working(int file) { return work_[file]; }  // pioneer::kRuleFiles order
  bool apply(std::string& message);               // into the running battle
  bool saveToMod(const std::string& modId, std::string& message);
  const std::string& problem() const { return problem_; }

 private:
  nlohmann::json& listOf(Kind k);
  const nlohmann::json* originalOf(Kind k, const std::string& id) const;
  void drawList();
  void drawForm();
  void drawFacts(const nlohmann::json& e);
  void drawBottomBar();
  void validate();
  void newEntry(bool copy);
  void deleteEntry();
  std::vector<std::string> idsOf(Kind k) const;
  std::vector<std::string> models() const;

  GameScene& scene_;
  bool open_ = false;
  bool loaded_ = false;
  nlohmann::json work_[pioneer::kRuleFileCount];  // what is being edited
  nlohmann::json orig_[pioneer::kRuleFileCount];  // the rules when the editor opened (for "changed")
  Kind kind_ = Units;
  bool syncTab_ = true;       // show kind_'s tab (chosen from code) on the next frame
  std::string selected_;
  char filter_[64] = "";
  int faction_ = 0;           // list filter: 0 all, else index into factions_
  std::vector<std::string> factions_;
  bool live_ = true;          // put every valid change into the battle at once
  bool pendingApply_ = false;
  float applyTimer_ = 0;
  bool changedSinceSave_ = false;
  std::string problem_;       // why the rules can't be used as they are ("" = fine)
  std::string status_, notes_;
  char newModName_[64] = "";
};

}  // namespace legion

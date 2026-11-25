#pragma once
// Pioneer Engine - the sound bank editor's document: every sound event as the
// game layers them, and edits that go into one bank file of the user's
// choosing (a mod's), without the window (so it is tested without graphics).
//
// The game reads audio/audio.json and audio/banks/*.json in every layer, base
// first; a later event replaces an earlier one of the same name whole (see
// audio/bank.hpp). So editing an event the base game gives copies it, as it
// is, into the target file and changes the copy there: the base stays as it
// was, the mod carries only what it changes, and "revert" takes the copy out
// again.
//
//   tools::BankDocument doc;
//   doc.load(fileSystem);                                 // every layer's banks
//   doc.setTarget("mods/my_sounds/", "audio/banks/edits.json");
//   doc.set("rifle", "volume", 0.3);                      // one step of undo
//   doc.addFile("rifle", "rec/rifle_new.ogg");
//   doc.save(error);                                      // hot reload hears it
//
// Game-agnostic: events, fields and files are the engine's.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "pioneer/audio/bank.hpp"
#include "pioneer/core/vfs.hpp"
#include "pioneer/tools/validate.hpp"

namespace pioneer::tools {

class BankDocument {
 public:
  struct Field {
    const char* name;  // "volume"
    enum Kind : uint8_t { Number, Integer, Bool, Bus } kind;
    double min, max, fallback;  // the range the editor offers, and the engine's default
    const char* help;
  };
  // The fields of an event, in the order the editor shows them (files apart).
  static const std::vector<Field>& fields();
  static const Field* field(const std::string& name);

  struct Row {
    std::string id;
    audio::SoundEvent event;  // as the game will have it (the target's version if it has one)
    std::string from;         // where that comes from: "base: audio/audio.json", "edits.json (target)"
    std::string below;        // where the version under the target comes from ("" if none: a new event)
    bool inTarget = false;    // the target file gives it
    bool changed = false;     // edited since the last save or load
  };

  // Reads the banks of every layer. The target (if set) is read from disk
  // afterwards, on top; a layer whose bank file *is* the target is left out
  // of what lies below it. The document keeps the file system (shared, so a
  // mod switch that replaces the game's can't pull it away) until the next load.
  void load(std::shared_ptr<const vfs::Vfs> fs);
  const vfs::Vfs* fileSystem() const { return fs_.get(); }
  // Where edits go: a folder on disk (a mod's, ending in '/') and the bank in
  // it ("audio/banks/edits.json"). Reads what that file already holds.
  bool setTarget(const std::string& folder, const std::string& bankFile, std::string* error = nullptr);
  bool hasTarget() const { return !targetFolder_.empty(); }
  std::string targetPath() const { return targetFolder_ + targetFile_; }
  const std::string& targetFolder() const { return targetFolder_; }
  const std::string& targetFile() const { return targetFile_; }

  std::vector<std::string> ids() const;  // every event, sorted
  const Row* row(const std::string& id) const;
  size_t size() const { return rows_.size(); }

  // ---- edits: each is one step of undo; false (and why) if refused
  bool set(const std::string& id, const std::string& field, const nlohmann::json& value, std::string* error = nullptr);
  bool addFile(const std::string& id, const std::string& file, bool distant = false, std::string* error = nullptr);
  bool removeFile(const std::string& id, size_t index, bool distant = false, std::string* error = nullptr);
  bool moveFile(const std::string& id, size_t index, int delta, bool distant = false);
  bool create(const std::string& id, std::string* error = nullptr);
  bool duplicate(const std::string& from, const std::string& to, std::string* error = nullptr);
  // Takes the event out of the target: the version below comes back (an
  // event only the target had is gone).
  bool revert(const std::string& id);
  bool undo();
  bool redo();
  bool canUndo() const { return at_ > 0; }
  bool canRedo() const { return at_ + 1 < history_.size(); }
  bool dirty() const { return history_.size() > 1 && history_[at_] != saved_; }

  // The target bank as it will be written: {"sounds": {...}}, events by name,
  // fields in the editor's order, and anything else the file held kept.
  nlohmann::ordered_json targetJson() const { return history_.empty() ? nlohmann::ordered_json::object() : history_[at_]; }
  std::string targetText() const;
  bool save(std::string& error);

  // What is wrong with one event (or every event in the target, id ""): the
  // validator's rules for sounds. Files are looked for in the layers and in
  // the target folder's audio/.
  std::vector<Issue> check(const std::string& id = {}) const;
  // A file named in a bank, as a path on disk ("" if no layer has it).
  std::string diskPath(const std::string& file) const;

 private:
  void rebuildRows();
  bool commit(nlohmann::ordered_json next, const std::string& changedId);
  nlohmann::ordered_json eventJson(const std::string& id) const;  // the target's version, or a copy of the one below
  static nlohmann::ordered_json tidy(const nlohmann::ordered_json& event);

  std::shared_ptr<const vfs::Vfs> fs_;
  struct Below {
    nlohmann::ordered_json json;
    std::string from;
  };
  std::map<std::string, Below> below_;  // the last version of each event under the target
  std::map<std::string, Row> rows_;
  std::string targetFolder_, targetFile_;
  std::vector<nlohmann::ordered_json> history_;  // the target file, step by step
  size_t at_ = 0;
  nlohmann::ordered_json saved_;
  std::map<std::string, bool> changed_;
};

}  // namespace pioneer::tools

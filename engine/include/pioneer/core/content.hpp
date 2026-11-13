#pragma once
// Pioneer Engine - content layers and mods.
//
// Game data lives in a base folder (content/) laid out as rules/, art/ and
// scenarios/. A mod is a folder with the same layout plus a mod.json; enabled
// mods sit on top of the base in load order, later ones winning:
//
//   * rules/*.json are merged, not replaced: entries are matched by "id"
//     (units, buildings, weapons) or by key (warheads). A mod entry changes
//     only the fields it names; "_remove": true deletes the entry; a new id
//     adds one. See mergeRules().
//   * art/ PNG files replace sprites of the same name (later layers win).
//   * scenarios/ add missions to the list.
//
// Everything here is plain data and file handling, with no graphics, so it is
// shared by the game, the tools and the tests.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pioneer {

// A mod as described by its mod.json.
struct ModInfo {
  std::string id;           // folder name; what settings and saves refer to
  std::string dir;          // full path, ending in '/'
  std::string name;         // shown to players (defaults to the id)
  std::string version = "1.0";
  std::string author;
  std::string description;
  std::vector<std::string> dependencies;  // ids of mods that must load before this one ("requires" in mod.json)
  bool user = false;        // lives in the player's own folder (the rules editor writes there)
  std::string error;        // bad mod.json: listed, but can't be enabled
};

// Every mod in these folders (later folders win for the same id), sorted by name.
std::vector<ModInfo> scanMods(const std::vector<std::string>& modDirs, size_t firstUserDir = 1);
const ModInfo* findMod(const std::vector<ModInfo>& mods, const std::string& id);
bool writeModInfo(const ModInfo& m, std::string& error);
// Creates <dir>/<id>/ with a mod.json and empty rules/, art/, scenarios/ folders.
bool createMod(const std::string& modsDir, const std::string& id, const std::string& name, ModInfo& out,
               std::string& error);
// A folder-safe id from a display name ("My Tanks!" -> "my_tanks").
std::string modIdFromName(const std::string& name);

// Checks an enabled list against what is installed: drops missing or broken
// mods and moves each mod after the ones it requires. Problems are reported
// one per line in `warnings`.
std::vector<std::string> resolveLoadOrder(const std::vector<std::string>& enabled, const std::vector<ModInfo>& mods,
                                          std::string& warnings);

// The rule files, in the order they must be loaded.
inline const char* const kRuleFiles[] = {"weapons.json", "units.json", "buildings.json"};
constexpr int kRuleFileCount = 3;

// Merges a mod's rule document into a base one (see the top of this file).
// Arrays of objects with an "id" merge entry by entry; objects merge key by
// key; anything else is replaced.
void mergeRules(nlohmann::json& base, const nlohmann::json& patch);
// The smallest patch that turns `base` into `target` (what a mod file holds).
nlohmann::json diffRules(const nlohmann::json& base, const nlohmann::json& target);

// A stack of content folders: the base first, then each enabled mod.
class ContentLayers {
 public:
  void clear() { roots_.clear(); modIds_.clear(); }
  void addRoot(const std::string& dir, const std::string& modId = {});
  const std::vector<std::string>& roots() const { return roots_; }
  const std::vector<std::string>& modIds() const { return modIds_; }  // enabled mods, in load order

  // Every existing copy of a relative path, base first.
  std::vector<std::string> all(const std::string& rel) const;
  // The rules merged through every layer, with the "_comment" of the base kept.
  // Fails on unreadable JSON, naming the file.
  bool loadRules(const std::string& file, nlohmann::json& out, std::string& error) const;
  // Same, stopping before the layer of mod `stopAt` (the base the rules editor diffs against).
  bool loadRulesBelow(const std::string& file, const std::string& stopAt, nlohmann::json& out,
                      std::string& error) const;

 private:
  std::vector<std::string> roots_;
  std::vector<std::string> modIds_;  // "" for the base
};

// 64-bit FNV-1a of the merged rules, as hex: saves and replays carry it.
std::string rulesHash(const nlohmann::json (&docs)[kRuleFileCount]);

// For changing rules in a running battle: puts the entries of `next` in the
// order of `current` (so type and weapon numbers in the battle stay valid),
// keeps entries that were deleted (hidden, so nobody builds them) and keeps
// what the battle can't change on the fly: footprints and blocking of
// structures already on the map. Each kept change is noted in `notes`.
// All three rule documents at once (kRuleFiles order): units and buildings are
// numbered as one list.
void stabiliseRules(nlohmann::json (&next)[kRuleFileCount], const nlohmann::json (&current)[kRuleFileCount],
                    std::string& notes);

}  // namespace pioneer

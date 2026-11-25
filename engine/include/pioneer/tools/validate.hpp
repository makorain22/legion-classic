#pragma once
// Pioneer Engine - the content validator: everything a game reads, checked
// before a player ever sees it.
//
// A Validator holds named checks. Each check looks at the merged content tree
// (a vfs::Vfs, base content and mods layered as the game layers them) and its
// asset registry, and reports issues: an error (the game would fail or
// misbehave), a warning (it works, but probably isn't what was meant) or a
// note (worth knowing). Every issue names the file, where in it when that is
// known ("line 12, column 4", "event rifle", "trigger 3") and the check that
// found it.
//
//   tools::Validator v;
//   tools::addEngineChecks(v);           // the engine's own: JSON, references, sounds, models ...
//   v.add({"rules", "...", myRulesCheck}); // a game adds what only it understands
//   tools::Report r = v.run(fs, registry);
//   std::puts(r.text().c_str());         // or r.json() for machines
//
// The engine's checks are game-agnostic: they know the engine's formats
// (sound banks, voice lines, string tables, interface scripts, glTF, .vox,
// packs, mods) and nothing of any one game. pioneer_tool runs them from the
// command line (and in CI); the asset browser runs them in the game.
//
// Checks run one after another in the order added (each may use every core
// inside); the report is sorted, so it reads the same however the work was
// split.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "pioneer/core/assets.hpp"
#include "pioneer/core/vfs.hpp"

namespace pioneer::tools {

struct Issue {
  enum class Severity : uint8_t { Note, Warning, Error };
  Severity severity = Severity::Error;
  std::string path;   // virtual path of the file ("" for the content as a whole)
  std::string where;  // inside it: "line 3, column 7", "event rifle", "" = the whole file
  std::string text;
  std::string check;  // the check that found it
  // Where the file comes from, for people: "content/rules/units.json",
  // "veterans.pnpack:rules/units.json" (filled in by the validator).
  std::string source;
};
const char* severityName(Issue::Severity s);  // "error", "warning", "note"

struct Report {
  std::vector<Issue> issues;  // sorted: by path, then where, then severity (errors first), then text
  std::map<std::string, size_t> checked;  // check -> how many things it looked at
  std::vector<std::string> checksRun;
  double ms = 0;

  size_t count(Issue::Severity s) const;
  size_t errors() const { return count(Issue::Severity::Error); }
  size_t warnings() const { return count(Issue::Severity::Warning); }
  size_t notes() const { return count(Issue::Severity::Note); }
  bool ok() const { return errors() == 0; }
  // The issues of one file.
  std::vector<const Issue*> of(const std::string& path) const;
  // "rules/units.json: error: line 3, column 7: ..." per issue (notes only
  // when `notes`), then a summary line.
  std::string text(bool notes = false) const;
  nlohmann::json json() const;
};

class Context;

struct Check {
  std::string name;  // short id, used on the command line: "json", "sounds"
  std::string help;  // one sentence: what it looks for
  std::function<void(Context&)> run;
  bool enabledByDefault = true;
};

struct Options {
  std::vector<std::string> only;  // check names to run (empty: every check enabled by default)
  std::vector<std::string> skip;  // check names to leave out
  // Files to leave alone (a path containing any of these): third-party
  // files, work in progress.
  std::vector<std::string> ignore;
};

// What a check sees.
class Context {
 public:
  Context(const vfs::Vfs& fs, assets::Registry& registry, Report& report, std::string check, const Options& options);

  const vfs::Vfs& fs() const { return fs_; }
  assets::Registry& registry() { return reg_; }
  const Options& options() const { return opt_; }

  void error(const std::string& path, const std::string& text, const std::string& where = {});
  void warning(const std::string& path, const std::string& text, const std::string& where = {});
  void note(const std::string& path, const std::string& text, const std::string& where = {});
  void report(Issue::Severity s, const std::string& path, const std::string& text, const std::string& where = {});
  // Counts what was looked at (for the summary).
  void counted(size_t n = 1);

  // Files of the merged tree under `dir` ("" = all) with one of the
  // extensions (".json"; empty = any), skipping ignored ones, by path.
  std::vector<std::string> files(const std::string& dir, const std::vector<std::string>& exts = {}) const;
  bool ignored(const std::string& path) const;
  bool read(const std::string& path, std::vector<uint8_t>& out);
  // Parsed JSON, cached for the whole run (every check that reads the file
  // shares one parse). Null (and an error reported once, by the json check's
  // wording) if it doesn't parse.
  const nlohmann::json* json(const std::string& path);

 private:
  friend class Validator;
  const vfs::Vfs& fs_;
  assets::Registry& reg_;
  Report& report_;
  std::string check_;
  const Options& opt_;
  std::shared_ptr<std::map<std::string, std::shared_ptr<nlohmann::json>>> jsonCache_;
  std::shared_ptr<std::map<std::string, std::string>> jsonErrors_;
};

class Validator {
 public:
  void add(Check c);  // a check with the same name is replaced
  const std::vector<Check>& checks() const { return checks_; }
  const Check* find(const std::string& name) const;
  // Runs the checks. The registry should be attached to `fs` and rebuilt;
  // dependencies are scanned here if they haven't been.
  Report run(const vfs::Vfs& fs, assets::Registry& registry, const Options& options = {}) const;

 private:
  std::vector<Check> checks_;
};

// The engine's checks, in this order:
//   paths       names that break on another system: two files differing only
//               in case, names Windows reserves (con, aux, nul ...), characters
//               it refuses, trailing dots and spaces, very long paths
//   json        every .json file parses (line and column of the first fault)
//   references  every file a JSON file names is there (asset registry links)
//   packs       every pack in the tree matches its manifest and runs on this engine
//   mods        mod.json files: id, version, requirements that can be read
//   sounds      sound banks (audio.json, banks/*.json): fields of the right
//               kind and range, files that exist and decode; voice lines likewise
//   audio       every .ogg and .wav decodes; ones nothing plays are noted
//   images      every .png decodes, sizes within what old hardware takes
//   models      every .gltf/.glb and .vox reads; meshes, materials, skins and
//               clips that make sense
//   strings     string tables (lang/*.json): parse, English is complete,
//               other languages' keys and {placeholders} match English
//   scripts     interface scripts (ui/*.lua) load in the sandbox
//   fonts       .ttf/.otf files are fonts
void addEngineChecks(Validator& v);

// Takes the text of a JSON parse error ("[json.exception.parse_error.101]
// parse error at line 3, column 7: ...") apart into "line 3, column 7" and
// the rest.
void splitJsonError(const std::string& what, std::string& where, std::string& text);

// Names that Windows refuses ("CON", "aux.txt", "a:b", "x."): why, or "" if fine.
std::string portableNameProblem(const std::string& fileName);

}  // namespace pioneer::tools

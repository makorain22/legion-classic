#pragma once
// Pioneer Engine - packs: one file to hand a mod, a campaign or a set of
// assets to somebody else.
//
// A pack is a zip archive (".pnpack", or plain ".zip") laid out like a content
// folder, with a manifest "pack.json" at its top:
//
//   {
//     "format": 1,
//     "id": "veterans", "name": "Veterans", "version": "1.2.0",
//     "author": "...", "description": "...",
//     "kinds": ["rules", "campaigns"],            // what is inside, for lists
//     "engine": ">=2.0.0",                        // Pioneer versions it runs on
//     "requires": ["base_units >= 1.1", "maps"],  // other packs / mods
//     "files": [ {"path": "rules/units.json", "size": 1234, "crc": "9a0b1c2d"}, ... ],
//     "checksum": "0123456789abcdef"              // over every file's path, size and CRC
//   }
//
// Mods, the Mission Maker's exports and (later) asset packs all use it. A
// plain zipped mod folder with only a mod.json is accepted too; its file list
// is then taken from the archive itself.
//
// The game mounts a pack straight into the virtual file system (vfs.hpp);
// nothing has to be unpacked by hand.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace pioneer::pack {

constexpr int kFormat = 1;
constexpr const char* kManifestName = "pack.json";
constexpr const char* kExtension = ".pnpack";

// "1", "1.2", "1.2.3" (anything after the third number, like "-beta", is ignored).
struct Version {
  int major = 0, minor = 0, patch = 0;
  static bool parse(std::string_view text, Version& out);
  std::string str() const;
  int compare(const Version& o) const;
  bool operator==(const Version& o) const { return compare(o) == 0; }
  bool operator<(const Version& o) const { return compare(o) < 0; }
};

// "veterans", "veterans >= 1.2", "veterans=1.0", "veterans ^1.2" (same major,
// at least 1.2), "veterans ~1.2" (same major and minor, at least 1.2.0).
struct Requirement {
  std::string id;
  std::string op;  // "", ">=", ">", "<=", "<", "=", "^", "~"
  Version version;
  static bool parse(std::string_view text, Requirement& out);
  // The same without an id: ">=2.0.0" (for "engine").
  static bool parseRange(std::string_view text, Requirement& out);
  bool allows(const Version& have) const;
  std::string str() const;        // "veterans >= 1.2.0"
  std::string wording() const;    // "1.2.0 or newer", for messages
};

struct FileEntry {
  std::string path;
  uint64_t size = 0;
  uint32_t crc = 0;
};

struct Manifest {
  int format = kFormat;
  std::string id;
  std::string name;
  std::string version;                   // "" reads as 1.0
  std::string author;
  std::string description;
  std::vector<std::string> kinds;
  std::string engine;                    // "" = any
  std::vector<Requirement> requirements;
  std::vector<FileEntry> files;          // sorted by path; never lists pack.json itself
  std::string checksum;
  bool fromModJson = false;              // the archive had no pack.json
  uint64_t totalBytes() const;
};

nlohmann::json toJson(const Manifest& m);
bool fromJson(const nlohmann::json& j, Manifest& m, std::string& error);
// 16 hex digits over every file's path, size and CRC, in path order.
std::string checksumOf(const std::vector<FileEntry>& files);
// "rules", "campaigns", "maps", "scenarios", "audio", "art", "models",
// "scripts", "shaders": the top folders a pack fills.
std::vector<std::string> kindsOf(const std::vector<FileEntry>& files);
// True if this Pioneer satisfies the manifest's "engine" range.
bool engineAllows(const Manifest& m, std::string* why = nullptr);

struct BuildOptions {
  int level = 6;                        // 0 store .. 9 smallest
  std::vector<std::string> exclude;     // paths containing any of these are left out
};
// Packs a content folder. Fields of `m` left empty are filled from the
// folder's mod.json (name, version, author, description, requires) and the
// folder's name (id). On success `m` holds the full manifest as written.
bool build(const std::string& dir, const std::string& outFile, Manifest& m, std::string& error,
           const BuildOptions& options = {});
// Reads the manifest of a pack (or a zipped mod).
bool readManifest(const std::string& file, Manifest& m, std::string& error);

struct Report {
  bool ok = false;
  size_t files = 0;
  uint64_t bytes = 0;
  std::vector<std::string> problems;
};
// Unpacks every file in memory and checks it against the manifest.
Report verify(const std::string& file);
// Unpacks into a folder (created if needed). Entries that would land outside it are refused.
bool extract(const std::string& file, const std::string& dir, std::string& error);

}  // namespace pioneer::pack

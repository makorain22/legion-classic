#pragma once
// Pioneer Engine - cooking: content made ready to ship.
//
// Cooking reads the merged content tree (base content and mods, layered as
// the game layers them) and writes one folder the game can run from as it
// is, or pack into a single .pnpack:
//
//   - JSON is minified (key order and every value kept, comments dropped);
//   - everything else is copied byte for byte;
//   - editors' leftovers (hidden files, *~, *.tmp, *.bak) and anything the
//     options exclude stay behind;
//   - asset_index.json lists every cooked file: kind, size, hash and what it
//     uses, with one hash over the whole content (what multiplayer and patch
//     notes compare).
//
// Cooking is deterministic - the same input gives the same bytes, on any
// machine and with any number of threads - and incremental: cooking again
// into the same folder rewrites only what changed and removes what is gone
// (it remembers what it did in .cook_state.json, which packs leave out).
//
//   tools::CookOptions o;
//   o.packFile = "dist/legion-content.pnpack";
//   tools::CookResult r;
//   if (!tools::cook(fs, registry, "build/cooked/", o, r)) for (auto& e : r.errors) ...
//
// Game-agnostic: what a file is comes from the asset registry.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "pioneer/core/assets.hpp"
#include "pioneer/core/pack.hpp"
#include "pioneer/core/vfs.hpp"

namespace pioneer::tools {

constexpr const char* kIndexFile = "asset_index.json";
constexpr const char* kCookStateFile = ".cook_state.json";

struct CookOptions {
  bool minifyJson = true;
  std::vector<std::string> exclude;  // paths containing any of these stay behind
  bool incremental = true;           // false: write every file again
  bool clean = false;                // empty the output folder first
  std::string packFile;              // also pack the result into this file ("" = no pack)
  pack::Manifest manifest;           // id, name, version ... of that pack
  int packLevel = 6;
};

struct CookResult {
  size_t files = 0;     // cooked files (the index not counted)
  size_t written = 0;   // written this time
  size_t reused = 0;    // unchanged since the last cook
  size_t removed = 0;   // gone from the content since the last cook
  size_t minified = 0;  // JSON files minified
  uint64_t bytesIn = 0, bytesOut = 0;
  std::string contentHash;  // 16 hex digits over every cooked file's path and hash
  std::vector<std::string> errors;
  double ms = 0;
};

bool cook(const vfs::Vfs& fs, assets::Registry& registry, const std::string& outDir, const CookOptions& options,
          CookResult& result);

// What a JSON file becomes when cooked (the same bytes back if it doesn't parse).
std::vector<uint8_t> minifiedJson(const std::vector<uint8_t>& bytes, bool* changed = nullptr);

// ---- the asset index

struct AssetIndex {
  struct Entry {
    std::string path;
    std::string type;  // assets::typeName
    uint64_t size = 0;
    uint64_t hash = 0;  // FNV-1a of the bytes (vfs::hash64)
    std::vector<std::string> uses;
  };
  std::vector<Entry> entries;  // by path
  std::string contentHash;
  std::string engine;          // the engine version that wrote it
  const Entry* find(const std::string& path) const;
};

// Every present asset of a registry (hashing each file).
AssetIndex indexOf(assets::Registry& registry);
// 16 hex digits over every entry's path and hash, in path order.
std::string contentHashOf(const AssetIndex& index);
nlohmann::json toJson(const AssetIndex& index);
bool fromJson(const nlohmann::json& j, AssetIndex& out, std::string& error);
bool readIndex(const std::string& file, AssetIndex& out, std::string& error);

// What changed from one index to another: for patch notes and mod updates.
struct IndexDiff {
  std::vector<std::string> added, removed, changed;  // paths, sorted
  bool empty() const { return added.empty() && removed.empty() && changed.empty(); }
};
IndexDiff diff(const AssetIndex& from, const AssetIndex& to);

}  // namespace pioneer::tools

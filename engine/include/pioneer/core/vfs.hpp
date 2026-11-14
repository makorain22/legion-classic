#pragma once
// Pioneer Engine - the virtual file system.
//
// Everything the game reads lives under one tree of virtual paths
// ("rules/units.json", "audio/sfx/rifle.ogg"), whatever is behind it:
//
//   * a folder on disk               mountDir
//   * a zip archive / pack           mountZip   (read straight out of the archive)
//   * files held in memory           mountMemory (tests, tools, generated data)
//
// Mounts are layered. For any path the mount with the highest priority that
// has it wins (of equal priorities, the one mounted last); layers() gives
// every copy, lowest first, for data that is merged rather than replaced
// (the rules). A mount can sit at a point in the tree: a pack mounted at
// "mods/veterans/" shows its files below that folder.
//
// Paths use '/', are relative, and never climb out with "..". Look-ups in
// archives fall back to ignoring case, so a pack made on Windows behaves the
// same everywhere.
//
// CHANGES ON DISK. scan() walks the mounted folders (safe on a worker thread)
// and apply() compares the result with the last one, returning what was
// added, modified or removed. HotReload (hot_reload.hpp) does this in the
// background and tells the systems concerned.
//
// OLD CODE THAT WANTS A REAL FILE. diskPath() gives a path on disk for any
// virtual file: the file itself for a folder mount, a copy unpacked into the
// cache folder for an archive. The cache is keyed by the archive's contents,
// so nothing is unpacked twice.
//
// Thread-safe: reading from any thread; mounting from one thread at a time.

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace pioneer::vfs {

// "a\\b/./c//d" -> "a/b/c/d". Leading '/' and "./" are dropped. Returns false
// for a path that tries to leave the tree ("../x") or names a drive.
bool normalise(std::string_view in, std::string& out);
// 64-bit FNV-1a, for fingerprints and content hashes (not for security).
uint64_t hash64(const void* data, size_t size, uint64_t seed = 0xcbf29ce484222325ull);
std::string hex64(uint64_t v);
uint32_t crc32(const void* data, size_t size);

struct FileInfo {
  std::string path;     // virtual path
  uint64_t size = 0;
  int64_t mtime = 0;    // a stamp to compare, not a calendar time
  uint32_t crc = 0;     // known for archive entries only (0 otherwise)
  int mount = -1;       // id of the mount that provides it
  bool packed = false;  // inside an archive or in memory
};

struct MountInfo {
  int id = -1;
  std::string name;     // "base", a mod's id ...
  std::string source;   // folder (ending in '/') or archive file
  std::string point;    // where it sits in the tree ("" = the root; else ends in '/')
  int priority = 0;
  bool packed = false;
  bool memory = false;
  size_t files = 0;     // archives and memory mounts; folders are not counted until scanned
  uint64_t bytes = 0;
  std::string fingerprint;  // archives: changes when the contents do
};

enum class ChangeKind : uint8_t { Added, Modified, Removed };
struct Change {
  std::string path;
  int mount = -1;
  ChangeKind kind = ChangeKind::Modified;
};
const char* changeName(ChangeKind k);

struct Stamp {
  uint64_t size = 0;
  int64_t mtime = 0;
  bool operator==(const Stamp& o) const { return size == o.size && mtime == o.mtime; }
  bool operator!=(const Stamp& o) const { return !(*this == o); }
};

// What scan() saw: every file of every folder mount, and each archive's own stamp.
struct Snapshot {
  struct MountScan {
    int id = -1;
    bool archive = false;
    Stamp archiveStamp;
    std::map<std::string, Stamp> files;  // relative to the mount
  };
  std::vector<MountScan> mounts;
  size_t fileCount = 0;
  double ms = 0;  // how long the walk took
};

class Vfs {
 public:
  Vfs();
  ~Vfs();
  Vfs(const Vfs&) = delete;
  Vfs& operator=(const Vfs&) = delete;

  // ---- mounts. Each returns the mount's id, or -1 with `error` set.
  int mountDir(const std::string& dir, const std::string& name = {}, int priority = 0, const std::string& point = {});
  // If every file of the archive sits in one top folder that holds a pack.json
  // or mod.json (the way people zip a folder), that folder is skipped.
  int mountZip(const std::string& file, const std::string& name, int priority, const std::string& point,
               std::string& error);
  int mountMemory(const std::string& name = {}, int priority = 0, const std::string& point = {});
  bool addMemoryFile(int mount, const std::string& path, std::vector<uint8_t> bytes);
  bool addMemoryFile(int mount, const std::string& path, std::string_view text);
  bool unmount(int id);
  void clear();
  std::vector<MountInfo> mounts() const;  // lowest layer first
  bool mountInfo(int id, MountInfo& out) const;
  int findMount(const std::string& name) const;  // -1 if there is none

  // ---- reading the merged tree
  bool exists(std::string_view path) const;
  bool stat(std::string_view path, FileInfo& out) const;
  bool read(std::string_view path, std::vector<uint8_t>& out, std::string* error = nullptr) const;
  bool readText(std::string_view path, std::string& out, std::string* error = nullptr) const;
  // Every copy of the path, lowest layer first.
  std::vector<FileInfo> layers(std::string_view path) const;
  // Files below a folder ("" = everything), the winning copy of each, sorted
  // by path. ext: ".json" keeps only that extension (case ignored).
  std::vector<FileInfo> list(std::string_view dir, bool recursive = false, std::string_view ext = {}) const;
  // Names of the folders directly below a folder, sorted.
  std::vector<std::string> listDirs(std::string_view dir) const;

  // ---- one mount at a time
  bool existsIn(int mount, std::string_view path) const;     // a file or a folder
  bool statIn(int mount, std::string_view path, FileInfo& out) const;
  bool readIn(int mount, std::string_view path, std::vector<uint8_t>& out, std::string* error = nullptr) const;
  // Where a file comes from, for messages: "content/rules/units.json" or
  // "veterans.zip:rules/units.json".
  std::string describe(int mount, std::string_view path) const;

  // ---- real files for code that needs a path
  void setCacheDir(const std::string& dir);
  const std::string& cacheDir() const { return cacheDir_; }
  // The winning copy's path on disk ("" if the file does not exist or can't be unpacked).
  std::string diskPath(std::string_view path) const;
  // Same within one mount; `path` may be a folder (keep its trailing '/'),
  // which unpacks everything below it.
  std::string diskPathIn(int mount, std::string_view path) const;

  // ---- writing: into one folder mount chosen with setWriteMount (the player's folder)
  void setWriteMount(int mount) { writeMount_ = mount; }
  int writeMount() const { return writeMount_; }
  // Written to a temporary file first and renamed, so a reader never sees half a file.
  bool write(std::string_view path, const void* data, size_t size, std::string* error = nullptr);
  bool writeText(std::string_view path, std::string_view text, std::string* error = nullptr);

  // ---- changes on disk
  Snapshot scan() const;                             // any thread
  std::vector<Change> apply(const Snapshot& snap);   // the thread that mounts
  std::vector<Change> poll() { return apply(scan()); }
  // Goes up whenever mounts or files change: cheap "is my cache stale?" test.
  uint64_t revision() const { return revision_.load(); }

 private:
  struct Mount;
  const Mount* mountById(int id) const;
  Mount* mountById(int id);
  bool inside(const Mount& m, std::string_view path, std::string& rel) const;
  bool lookup(const Mount& m, const std::string& rel, FileInfo& out) const;
  bool hasDir(const Mount& m, const std::string& rel) const;
  bool readFrom(const Mount& m, const std::string& rel, std::vector<uint8_t>& out, std::string* error) const;
  std::string describeMount(const Mount& m, const std::string& rel) const;
  static void fill(const Mount& m, MountInfo& out);
  std::string cacheRoot(const Mount& m) const;
  std::string unpack(const Mount& m, const std::string& rel, bool folder) const;
  void sortMounts();

  mutable std::shared_mutex mu_;
  mutable std::mutex cacheMu_;
  std::vector<std::unique_ptr<Mount>> mounts_;  // lowest layer first
  std::string cacheDir_;
  int nextId_ = 1;
  uint64_t nextSeq_ = 1;
  int writeMount_ = -1;
  std::atomic<uint64_t> revision_{1};
};

}  // namespace pioneer::vfs

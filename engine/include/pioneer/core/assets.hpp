#pragma once
// Pioneer Engine - the asset registry: every file the game can read, known by
// name, with what it is, where it comes from, which version of it is loaded
// and what it depends on.
//
//   Registry      one record per virtual path (see vfs.hpp). Ids are stable
//                 for as long as the registry lives, also when mods are
//                 switched or a file disappears and comes back.
//   dependencies  "campaign.json uses g01.lmap and g01.md": recorded by hand
//                 (setDependencies) or found by a scanner per asset type. When
//                 a file changes on disk, apply() also reports everything
//                 that uses it, so a campaign reloads when its mission does.
//   versions      a record's version goes up each time its file changes;
//                 caches compare it to know that what they hold is stale.
//   Cache<T>      loaded objects by path: loads on first use (or ahead of
//                 time on every core), hands out shared pointers, and swaps
//                 in a fresh object when the file changes. Whoever still
//                 holds the old one keeps it until they let go; a file that
//                 fails to load keeps its last good version.
//
// The registry is used from one thread (the main thread). Cache loaders may
// run on worker threads and must not touch shared state.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pioneer/core/jobs.hpp"
#include "pioneer/core/vfs.hpp"

namespace pioneer::assets {

enum class Type : uint8_t {
  Unknown, Rules, Map, Campaign, Briefing, Sound, Music, Texture, Model, Shader, Script, Font, Pack, Data, Text,
  Count
};
constexpr int kTypeCount = static_cast<int>(Type::Count);
const char* typeName(Type t);               // "rules", "map", ...
bool typeFromName(std::string_view name, Type& out);
// What a file is, from its folder and extension.
Type typeOf(std::string_view path);

using Id = uint32_t;
constexpr Id kNone = 0;

struct Record {
  Id id = kNone;
  std::string path;
  Type type = Type::Unknown;
  bool present = false;     // false: named by something, but no mount has it
  int mount = -1;           // the mount whose copy wins
  std::string source;       // that mount's name ("base", a mod's id)
  uint32_t layers = 0;      // how many mounts have a copy
  uint64_t size = 0;
  int64_t mtime = 0;
  uint32_t version = 0;     // 1 when first seen, +1 for every change
  uint64_t hash = 0;        // of the contents; 0 until asked for (contentHash)
  uint32_t hashedVersion = 0;
  std::vector<Id> uses;     // what it depends on
  std::vector<Id> usedBy;   // what depends on it
};

struct TypeStats {
  size_t files = 0;
  uint64_t bytes = 0;
};

class Registry {
 public:
  // Reads an asset and names the files it refers to.
  using Scanner = std::function<std::vector<std::string>(const Record& asset, const std::vector<uint8_t>& bytes,
                                                         const vfs::Vfs& fs)>;
  using Listener = std::function<void(const Record& asset)>;

  Registry();
  // The file system the records describe. Call rebuild() after changing it.
  void attach(std::shared_ptr<vfs::Vfs> fs);
  const std::shared_ptr<vfs::Vfs>& fileSystem() const { return fs_; }

  // Looks at everything mounted. Records of files that are gone stay (absent);
  // records whose file changed since the last look get a new version.
  void rebuild();

  Id find(std::string_view path) const;
  // The record's id, made (absent) if the path is not known yet.
  Id ensure(std::string_view path);
  const Record* get(Id id) const;
  const Record* get(std::string_view path) const { return get(find(path)); }
  size_t size() const { return records_.size(); }
  std::vector<Id> ofType(Type t) const;   // present assets only, by path
  std::vector<Id> all() const;            // present assets only, by path
  std::array<TypeStats, kTypeCount> stats() const;

  // ---- dependencies
  void setDependencies(Id user, const std::vector<std::string>& paths);
  void addDependency(Id user, Id dependency);
  std::vector<Id> usersOf(Id id, bool recursive = true) const;         // nearest first, each once
  std::vector<Id> dependenciesOf(Id id, bool recursive = true) const;  // nearest first, each once
  void setScanner(Type t, Scanner s);
  // Runs the scanners over every present asset (reading and parsing on all
  // cores, recording in path order). Returns how many links were found.
  size_t scanDependencies();
  // The stock scanner for JSON: every string in the document that names an
  // existing file, as written, relative to the asset's folder, or with one of
  // the usual extensions added (.lmap, .md, .ogg, .json, .png).
  static std::vector<std::string> jsonReferences(const Record& asset, const std::vector<uint8_t>& bytes,
                                                 const vfs::Vfs& fs);

  // FNV-1a of the file's bytes, worked out once per version (0 if unreadable).
  uint64_t contentHash(Id id);

  // ---- changes. Give it what Vfs::apply returned. Updates the records
  // concerned, looks again at what changed assets depend on, and returns the
  // changed assets followed by everything that uses them (each once), after
  // telling the listeners in that order.
  std::vector<Id> apply(const std::vector<vfs::Change>& changes);
  int listen(Listener fn);
  void unlisten(int handle);
  // Goes up with every rebuild and every applied change.
  uint64_t revision() const { return revision_; }

 private:
  void refresh(Record& r);
  void scanOne(Record& r);
  void unlink(Record& r);

  std::shared_ptr<vfs::Vfs> fs_;
  std::vector<Record> records_;                    // records_[id - 1]
  std::unordered_map<std::string, Id> byPath_;
  std::array<Scanner, kTypeCount> scanners_;
  std::vector<std::pair<int, Listener>> listeners_;
  int nextListener_ = 1;
  uint64_t revision_ = 0;
};

// Loaded objects of one kind, by path.
template <class T>
class Cache {
 public:
  // Makes the object from the file's bytes; returns null and sets `error` if
  // it can't. Must be safe to call from several threads at once.
  using Loader = std::function<std::shared_ptr<T>(const Record& asset, const std::vector<uint8_t>& bytes,
                                                  std::string& error)>;

  Cache(Registry& registry, Loader loader) : reg_(registry), loader_(std::move(loader)) {}

  // The current object for a path, loading it if need be. Null if the file is
  // missing or has never loaded; `error` says why.
  std::shared_ptr<const T> get(std::string_view path, std::string* error = nullptr) {
    const Id id = reg_.ensure(path);
    Slot& s = slots_[id];
    const Record* r = reg_.get(id);
    if (s.version != r->version || (!s.value && s.error.empty())) load(*r, s);
    if (error) *error = s.error;
    return s.value;
  }

  // Loads many at once, each on whichever core is free. Results are stored in
  // the order given, so the outcome does not depend on the thread count.
  size_t preload(const std::vector<std::string>& paths) {
    struct Out {
      std::shared_ptr<T> value;
      std::string error;
      bool tried = false;
    };
    std::vector<Id> ids;
    for (const std::string& p : paths) ids.push_back(reg_.ensure(p));
    std::vector<Out> out(ids.size());
    const std::shared_ptr<vfs::Vfs> fs = reg_.fileSystem();
    const Registry& reg = reg_;
    jobs::system().parallelFor(
        ids.size(), 1,
        [&](size_t b, size_t e, size_t, int) {
          for (size_t i = b; i < e; ++i) {
            const Record* r = reg.get(ids[i]);
            auto have = slots_.find(ids[i]);
            if (have != slots_.end() && have->second.version == r->version && have->second.value) continue;
            out[i].tried = true;
            std::vector<uint8_t> bytes;
            if (!fs || !r->present || !fs->read(r->path, bytes, &out[i].error)) {
              if (out[i].error.empty()) out[i].error = r->path + " does not exist";
              continue;
            }
            out[i].value = loader_(*r, bytes, out[i].error);
          }
        },
        "Load asset");
    size_t loaded = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
      if (!out[i].tried) continue;
      Slot& s = slots_[ids[i]];
      s.version = reg_.get(ids[i])->version;
      s.error = out[i].error;
      if (out[i].value) {
        s.value = std::move(out[i].value);
        ++loaded;
      }
    }
    return loaded;
  }

  // Reloads what changed on disk. Returns how many objects were replaced.
  size_t reloadChanged() {
    size_t n = 0;
    for (auto& [id, s] : slots_) {
      const Record* r = reg_.get(id);
      if (!r || s.version == r->version) continue;
      const std::shared_ptr<T> before = s.value;
      load(*r, s);
      if (s.value != before) ++n;
    }
    return n;
  }

  // Why the last load of a path failed ("" if it did not).
  std::string errorOf(std::string_view path) const {
    auto it = slots_.find(reg_.find(path));
    return it == slots_.end() ? std::string() : it->second.error;
  }
  size_t size() const { return slots_.size(); }
  void clear() { slots_.clear(); }

 private:
  struct Slot {
    std::shared_ptr<T> value;
    uint32_t version = 0;
    std::string error;
  };
  void load(const Record& r, Slot& s) {
    s.version = r.version;
    s.error.clear();
    std::vector<uint8_t> bytes;
    const std::shared_ptr<vfs::Vfs>& fs = reg_.fileSystem();
    if (!fs || !r.present || !fs->read(r.path, bytes, &s.error)) {
      if (s.error.empty()) s.error = r.path + " does not exist";
      return;  // the last good object, if any, stays
    }
    std::shared_ptr<T> fresh = loader_(r, bytes, s.error);
    if (fresh) s.value = std::move(fresh);
    else if (s.error.empty()) s.error = r.path + " could not be loaded";
  }

  Registry& reg_;
  Loader loader_;
  std::unordered_map<Id, Slot> slots_;
};

}  // namespace pioneer::assets

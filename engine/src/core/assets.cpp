// Pioneer Engine - the asset registry (see assets.hpp).
#include "pioneer/core/assets.hpp"

#include <algorithm>
#include <cctype>
#include <set>

#include <nlohmann/json.hpp>

#include "pioneer/core/profiler.hpp"

namespace pioneer::assets {

namespace {

const char* const kTypeNames[kTypeCount] = {"unknown", "rules",  "map",    "campaign", "briefing",
                                            "sound",   "music",  "texture", "model",   "shader",
                                            "script",  "font",   "pack",   "data",     "text"};

std::string lowered(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool hasExt(const std::string& p, std::initializer_list<const char*> exts) {
  for (const char* e : exts) {
    const size_t n = std::char_traits<char>::length(e);
    if (p.size() >= n && p.compare(p.size() - n, n, e) == 0) return true;
  }
  return false;
}

std::string folderOf(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

}  // namespace

const char* typeName(Type t) {
  const int i = static_cast<int>(t);
  return i >= 0 && i < kTypeCount ? kTypeNames[i] : "unknown";
}

bool typeFromName(std::string_view name, Type& out) {
  const std::string n = lowered(name);
  for (int i = 0; i < kTypeCount; ++i)
    if (n == kTypeNames[i]) {
      out = static_cast<Type>(i);
      return true;
    }
  return false;
}

Type typeOf(std::string_view pathIn) {
  const std::string p = lowered(pathIn);
  const size_t slash = p.rfind('/');
  const std::string base = slash == std::string::npos ? p : p.substr(slash + 1);
  if (base == "pack.json" || base == "mod.json") return Type::Pack;
  if (base == "campaign.json") return Type::Campaign;
  if (hasExt(p, {".lmap"})) return Type::Map;
  if (hasExt(p, {".ogg", ".wav", ".flac", ".mp3"}))
    return p.compare(0, 6, "music/") == 0 || p.find("/music/") != std::string::npos ? Type::Music : Type::Sound;
  if (hasExt(p, {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".ktx"})) return Type::Texture;
  if (hasExt(p, {".vox", ".gltf", ".glb", ".obj"})) return Type::Model;
  if (hasExt(p, {".glsl", ".vert", ".frag", ".vs", ".fs"})) return Type::Shader;
  if (hasExt(p, {".lua"})) return Type::Script;
  if (hasExt(p, {".ttf", ".otf"})) return Type::Font;
  if (hasExt(p, {".json"})) {
    if (p.compare(0, 6, "rules/") == 0 || p.find("/rules/") != std::string::npos) return Type::Rules;
    return Type::Data;
  }
  if (hasExt(p, {".md"}) && (p.compare(0, 10, "campaigns/") == 0 || p.find("/campaigns/") != std::string::npos))
    return Type::Briefing;
  if (hasExt(p, {".md", ".txt", ".cfg", ".ini"})) return Type::Text;
  return Type::Unknown;
}

Registry::Registry() {
  scanners_[static_cast<size_t>(Type::Campaign)] = jsonReferences;
  scanners_[static_cast<size_t>(Type::Data)] = jsonReferences;
}

void Registry::attach(std::shared_ptr<vfs::Vfs> fs) { fs_ = std::move(fs); }

Id Registry::find(std::string_view path) const {
  std::string p;
  if (!vfs::normalise(path, p)) return kNone;
  auto it = byPath_.find(p);
  return it == byPath_.end() ? kNone : it->second;
}

Id Registry::ensure(std::string_view path) {
  std::string p;
  if (!vfs::normalise(path, p)) p.assign(path);
  auto it = byPath_.find(p);
  if (it != byPath_.end()) return it->second;
  Record r;
  r.id = static_cast<Id>(records_.size() + 1);
  r.path = p;
  r.type = typeOf(p);
  records_.push_back(std::move(r));
  byPath_[p] = records_.back().id;
  return records_.back().id;
}

const Record* Registry::get(Id id) const {
  return id == kNone || id > records_.size() ? nullptr : &records_[id - 1];
}

// Brings one record up to date with the file system. A change of the winning
// copy (or its appearing or vanishing) is a new version.
void Registry::refresh(Record& r) {
  vfs::FileInfo fi;
  const bool present = fs_ && fs_->stat(r.path, fi);
  const bool changed = present != r.present || (present && (fi.size != r.size || fi.mtime != r.mtime || fi.mount != r.mount));
  r.present = present;
  if (present) {
    r.size = fi.size;
    r.mtime = fi.mtime;
    r.mount = fi.mount;
    vfs::MountInfo mi;
    r.source = fs_->mountInfo(fi.mount, mi) ? mi.name : std::string();
    r.layers = static_cast<uint32_t>(fs_->layers(r.path).size());
  } else {
    r.size = 0;
    r.mount = -1;
    r.layers = 0;
    r.source.clear();
  }
  if (r.version == 0) r.version = present ? 1 : 0;
  else if (changed) ++r.version;
}

void Registry::rebuild() {
  PNR_PROFILE("Assets: rebuild the registry");
  ++revision_;
  if (!fs_) {
    for (Record& r : records_) r.present = false;
    return;
  }
  std::set<Id> seen;
  for (const vfs::FileInfo& fi : fs_->list("", true)) {
    const Id id = ensure(fi.path);
    seen.insert(id);
    refresh(records_[id - 1]);
  }
  for (Record& r : records_)
    if (!seen.count(r.id) && r.present) refresh(r);
}

std::vector<Id> Registry::all() const {
  std::vector<Id> out;
  for (const Record& r : records_)
    if (r.present) out.push_back(r.id);
  std::sort(out.begin(), out.end(), [&](Id a, Id b) { return records_[a - 1].path < records_[b - 1].path; });
  return out;
}

std::vector<Id> Registry::ofType(Type t) const {
  std::vector<Id> out;
  for (const Record& r : records_)
    if (r.present && r.type == t) out.push_back(r.id);
  std::sort(out.begin(), out.end(), [&](Id a, Id b) { return records_[a - 1].path < records_[b - 1].path; });
  return out;
}

std::array<TypeStats, kTypeCount> Registry::stats() const {
  std::array<TypeStats, kTypeCount> out{};
  for (const Record& r : records_)
    if (r.present) {
      TypeStats& s = out[static_cast<size_t>(r.type)];
      ++s.files;
      s.bytes += r.size;
    }
  return out;
}

// ---------------------------------------------------------------------------
// Dependencies
// ---------------------------------------------------------------------------
void Registry::unlink(Record& r) {
  for (Id d : r.uses) {
    std::vector<Id>& back = records_[d - 1].usedBy;
    back.erase(std::remove(back.begin(), back.end(), r.id), back.end());
  }
  r.uses.clear();
}

void Registry::addDependency(Id user, Id dependency) {
  if (user == kNone || dependency == kNone || user == dependency) return;
  if (user > records_.size() || dependency > records_.size()) return;
  Record& u = records_[user - 1];
  if (std::find(u.uses.begin(), u.uses.end(), dependency) != u.uses.end()) return;
  u.uses.push_back(dependency);
  records_[dependency - 1].usedBy.push_back(user);
}

void Registry::setDependencies(Id user, const std::vector<std::string>& paths) {
  if (user == kNone || user > records_.size()) return;
  std::vector<Id> ids;
  for (const std::string& p : paths) ids.push_back(ensure(p));  // may grow records_: no reference held across it
  unlink(records_[user - 1]);
  for (Id d : ids) addDependency(user, d);
}

static std::vector<Id> walk(const std::vector<Record>& records, Id start, bool recursive, bool users) {
  std::vector<Id> out;
  if (start == kNone || start > records.size()) return out;
  std::set<Id> seen{start};
  std::vector<Id> frontier{start};
  while (!frontier.empty()) {
    std::vector<Id> next;
    for (Id id : frontier) {
      const Record& r = records[id - 1];
      for (Id n : users ? r.usedBy : r.uses)
        if (seen.insert(n).second) {
          out.push_back(n);
          next.push_back(n);
        }
    }
    if (!recursive) break;
    frontier.swap(next);
  }
  return out;
}

std::vector<Id> Registry::usersOf(Id id, bool recursive) const { return walk(records_, id, recursive, true); }
std::vector<Id> Registry::dependenciesOf(Id id, bool recursive) const { return walk(records_, id, recursive, false); }

void Registry::setScanner(Type t, Scanner s) { scanners_[static_cast<size_t>(t)] = std::move(s); }

void Registry::scanOne(Record& r) {
  const Scanner& scan = scanners_[static_cast<size_t>(r.type)];
  if (!scan || !fs_) return;
  if (!r.present) {
    unlink(r);
    return;
  }
  std::vector<uint8_t> bytes;
  if (!fs_->read(r.path, bytes)) return;
  const Id id = r.id;
  const std::vector<std::string> paths = scan(r, bytes, *fs_);
  setDependencies(id, paths);  // `r` may dangle after this: records_ can grow
}

size_t Registry::scanDependencies() {
  PNR_PROFILE("Assets: scan dependencies");
  if (!fs_) return 0;
  std::vector<Id> todo;
  for (const Record& r : records_)
    if (r.present && scanners_[static_cast<size_t>(r.type)]) todo.push_back(r.id);
  std::sort(todo.begin(), todo.end(), [&](Id a, Id b) { return records_[a - 1].path < records_[b - 1].path; });
  const vfs::Vfs& fs = *fs_;
  const std::vector<std::vector<std::string>> found =
      jobs::parallelMap<std::vector<std::string>>(jobs::system(), todo.size(), 1, [&](size_t i) {
        const Record& r = records_[todo[i] - 1];
        std::vector<uint8_t> bytes;
        if (!fs.read(r.path, bytes)) return std::vector<std::string>();
        return scanners_[static_cast<size_t>(r.type)](r, bytes, fs);
      });
  size_t links = 0;
  for (size_t i = 0; i < todo.size(); ++i) {
    setDependencies(todo[i], found[i]);
    links += records_[todo[i] - 1].uses.size();
  }
  return links;
}

std::vector<std::string> Registry::jsonReferences(const Record& asset, const std::vector<uint8_t>& bytes,
                                                  const vfs::Vfs& fs) {
  std::vector<std::string> out;
  const nlohmann::json doc = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false, true);
  if (doc.is_discarded()) return out;
  static const char* const kExts[] = {"", ".lmap", ".md", ".ogg", ".json", ".png"};
  const std::string folder = folderOf(asset.path);
  std::set<std::string> seen;
  std::function<void(const nlohmann::json&)> visit = [&](const nlohmann::json& j) {
    if (j.is_string()) {
      const std::string& s = j.get_ref<const std::string&>();
      // File names are short, on one line, and not sentences.
      if (s.empty() || s.size() > 160 || s.find('\n') != std::string::npos) return;
      if (std::count(s.begin(), s.end(), ' ') > 3) return;
      for (const char* ext : kExts)
        for (const std::string& base : {folder + s, s}) {
          std::string p;
          if (!vfs::normalise(base + ext, p) || p.empty() || p == asset.path) continue;
          if (fs.exists(p)) {
            if (seen.insert(p).second) out.push_back(p);
            return;
          }
        }
    } else if (j.is_array() || j.is_object()) {
      for (const auto& child : j) visit(child);
    }
  };
  visit(doc);
  return out;
}

uint64_t Registry::contentHash(Id id) {
  if (id == kNone || id > records_.size() || !fs_) return 0;
  Record& r = records_[id - 1];
  if (r.hashedVersion == r.version && r.hashedVersion != 0) return r.hash;
  std::vector<uint8_t> bytes;
  if (!r.present || !fs_->read(r.path, bytes)) return 0;
  r.hash = vfs::hash64(bytes.data(), bytes.size());
  r.hashedVersion = r.version;
  return r.hash;
}

// ---------------------------------------------------------------------------
// Changes
// ---------------------------------------------------------------------------
std::vector<Id> Registry::apply(const std::vector<vfs::Change>& changes) {
  std::vector<Id> changed;
  if (changes.empty()) return changed;
  ++revision_;
  std::set<Id> seen;
  for (const vfs::Change& c : changes) {
    const Id id = ensure(c.path);
    if (!seen.insert(id).second) continue;
    Record& r = records_[id - 1];
    const uint32_t before = r.version;
    refresh(r);
    // A change in a lower layer matters too (merged rules): always a new version.
    if (r.version == before) ++r.version;
    changed.push_back(id);
  }
  for (Id id : changed) scanOne(records_[id - 1]);
  std::vector<Id> out = changed;
  for (Id id : changed)
    for (Id user : usersOf(id, true))
      if (seen.insert(user).second) {
        ++records_[user - 1].version;
        out.push_back(user);
      }
  for (Id id : out)
    for (const auto& [handle, fn] : listeners_)
      if (fn) fn(records_[id - 1]);
  return out;
}

int Registry::listen(Listener fn) {
  listeners_.emplace_back(nextListener_, std::move(fn));
  return nextListener_++;
}

void Registry::unlisten(int handle) {
  listeners_.erase(std::remove_if(listeners_.begin(), listeners_.end(),
                                  [&](const auto& l) { return l.first == handle; }),
                   listeners_.end());
}

}  // namespace pioneer::assets

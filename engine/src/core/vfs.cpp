// Pioneer Engine - the virtual file system (see vfs.hpp).
#include "pioneer/core/vfs.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

#include "pioneer/core/log.hpp"
#include "pioneer/core/profiler.hpp"
#include "zip_file.hpp"

namespace pioneer::vfs {

namespace fs = std::filesystem;

namespace {

std::string withSlash(std::string d) {
  for (char& c : d)
    if (c == '\\') c = '/';
  if (!d.empty() && d.back() != '/') d += '/';
  return d;
}

std::string lowered(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

bool endsWithNoCase(std::string_view s, std::string_view suffix) {
  if (suffix.size() > s.size()) return false;
  for (size_t i = 0; i < suffix.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i])))
      return false;
  return true;
}

int64_t stampOf(const fs::file_time_type& t) { return static_cast<int64_t>(t.time_since_epoch().count()); }

// Files the walk ignores: hidden ones, editors' backups and half-written temporaries.
bool ignoredName(const std::string& name) {
  if (name.empty() || name[0] == '.') return true;
  if (name.back() == '~') return true;
  return endsWithNoCase(name, ".tmp") || endsWithNoCase(name, ".swp") || endsWithNoCase(name, ".bak");
}

// Calls fn(relative path, size, mtime) for every file below root (which ends in '/').
template <class Fn>
void walkDir(const std::string& root, bool recursive, Fn&& fn) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return;
  const size_t skip = root.size();
  auto visit = [&](const fs::directory_entry& e) {
    std::error_code ec2;
    if (!e.is_regular_file(ec2) || ec2) return;
    const std::string full = e.path().generic_string();
    if (full.size() <= skip) return;
    const uint64_t size = e.file_size(ec2);
    if (ec2) return;
    const auto when = e.last_write_time(ec2);
    if (ec2) return;
    fn(full.substr(skip), size, stampOf(when));
  };
  if (!recursive) {
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
      if (ignoredName(it->path().filename().string())) continue;
      visit(*it);
    }
    return;
  }
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (ignoredName(it->path().filename().string())) {
      std::error_code ec2;
      if (it->is_directory(ec2)) it.disable_recursion_pending();
      continue;
    }
    visit(*it);
  }
}

bool writeWhole(const std::string& target, const void* data, size_t size, std::string* error) {
  std::error_code ec;
  const fs::path p(target);
  if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
  const std::string tmp = target + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error) *error = "can't write " + target;
      return false;
    }
    if (size) out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) {
      if (error) *error = "can't write " + target + " (disk full?)";
      return false;
    }
  }
  fs::rename(tmp, target, ec);
  if (ec) {
    // Some systems refuse to rename over an open file: fall back to replacing it.
    fs::remove(target, ec);
    fs::rename(tmp, target, ec);
    if (ec) {
      if (error) *error = "can't replace " + target;
      return false;
    }
  }
  return true;
}

std::string safeName(const std::string& s) {
  std::string out;
  for (char c : s)
    out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') ? c : '_';
  return out.empty() ? std::string("pack") : out;
}

}  // namespace

bool normalise(std::string_view in, std::string& out) {
  out.clear();
  std::vector<std::string_view> parts;
  size_t i = 0;
  while (i <= in.size()) {
    size_t j = i;
    while (j < in.size() && in[j] != '/' && in[j] != '\\') ++j;
    const std::string_view seg = in.substr(i, j - i);
    if (seg == "..") {
      if (parts.empty()) return false;
      parts.pop_back();
    } else if (!seg.empty() && seg != ".") {
      if (seg.find(':') != std::string_view::npos) return false;
      parts.push_back(seg);
    }
    i = j + 1;
  }
  for (size_t k = 0; k < parts.size(); ++k) {
    if (k) out += '/';
    out += parts[k];
  }
  return true;
}

uint64_t hash64(const void* data, size_t size, uint64_t seed) {
  const unsigned char* p = static_cast<const unsigned char*>(data);
  uint64_t h = seed;
  for (size_t i = 0; i < size; ++i) {
    h ^= p[i];
    h *= 0x100000001b3ull;
  }
  return h;
}

std::string hex64(uint64_t v) {
  char buf[20];
  std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
  return buf;
}

uint32_t crc32(const void* data, size_t size) { return zip::crc32(data, size); }

const char* changeName(ChangeKind k) {
  switch (k) {
    case ChangeKind::Added: return "added";
    case ChangeKind::Modified: return "modified";
    case ChangeKind::Removed: return "removed";
  }
  return "?";
}

// ---------------------------------------------------------------------------
// Mounts
// ---------------------------------------------------------------------------
struct Vfs::Mount {
  enum Kind { Dir, Zip, Memory };
  struct Entry {
    size_t index = 0;
    uint64_t size = 0;
    uint32_t crc = 0;
    int64_t mtime = 0;
  };
  int id = -1;
  uint64_t seq = 0;
  std::string name, source, point;
  int priority = 0;
  Kind kind = Dir;

  // Archives and memory mounts: what is inside.
  std::map<std::string, Entry> entries;
  std::map<std::string, std::string> lower;  // lower-case name -> the name as stored
  std::map<std::string, std::vector<uint8_t>> mem;
  std::unique_ptr<zip::Reader> zip;
  mutable std::mutex zipMu;
  std::string strip;  // top folder of the archive that is skipped
  std::string fingerprint;
  Stamp archiveStamp;

  // Polling.
  std::map<std::string, Stamp> seen;
  bool scanned = false;
  mutable bool cacheCleaned = false;

  void refreshFingerprint() {
    uint64_t h = 0xcbf29ce484222325ull;
    uint64_t total = 0;
    for (const auto& [n, e] : entries) {
      h = hash64(n.data(), n.size(), h);
      h = hash64(&e.size, sizeof(e.size), h);
      h = hash64(&e.crc, sizeof(e.crc), h);
      total += e.size;
    }
    (void)total;
    fingerprint = hex64(h);
    lower.clear();
    for (const auto& [n, e] : entries) lower[lowered(n)] = n;
  }
};

Vfs::Vfs() {
  std::error_code ec;
  cacheDir_ = withSlash((fs::temp_directory_path(ec) / "pioneer-cache").generic_string());
}
Vfs::~Vfs() = default;

void Vfs::sortMounts() {
  std::stable_sort(mounts_.begin(), mounts_.end(), [](const auto& a, const auto& b) {
    return a->priority != b->priority ? a->priority < b->priority : a->seq < b->seq;
  });
}

int Vfs::mountDir(const std::string& dir, const std::string& name, int priority, const std::string& point) {
  std::unique_lock lock(mu_);
  auto m = std::make_unique<Mount>();
  m->id = nextId_++;
  m->seq = nextSeq_++;
  m->kind = Mount::Dir;
  m->source = withSlash(dir);
  m->name = name.empty() ? m->source : name;
  m->priority = priority;
  std::string p;
  normalise(point, p);
  m->point = p.empty() ? std::string() : p + "/";
  const int id = m->id;
  mounts_.push_back(std::move(m));
  sortMounts();
  ++revision_;
  return id;
}

int Vfs::mountZip(const std::string& file, const std::string& name, int priority, const std::string& point,
                  std::string& error) {
  auto m = std::make_unique<Mount>();
  m->kind = Mount::Zip;
  m->source = file;
  m->name = name.empty() ? fs::path(file).stem().string() : name;
  m->priority = priority;
  std::string p;
  normalise(point, p);
  m->point = p.empty() ? std::string() : p + "/";
  m->zip = std::make_unique<zip::Reader>();
  if (!m->zip->open(file, error)) return -1;
  zip::Index idx;
  zip::index(*m->zip, idx);
  m->strip = idx.strip;
  for (const auto& [n, f] : idx.files) m->entries[n] = Mount::Entry{f.first, f.second.size, f.second.crc, f.second.mtime};
  m->refreshFingerprint();
  std::error_code ec;
  m->archiveStamp.size = fs::file_size(file, ec);
  m->archiveStamp.mtime = stampOf(fs::last_write_time(file, ec));

  std::unique_lock lock(mu_);
  m->id = nextId_++;
  m->seq = nextSeq_++;
  const int id = m->id;
  mounts_.push_back(std::move(m));
  sortMounts();
  ++revision_;
  return id;
}

int Vfs::mountMemory(const std::string& name, int priority, const std::string& point) {
  std::unique_lock lock(mu_);
  auto m = std::make_unique<Mount>();
  m->id = nextId_++;
  m->seq = nextSeq_++;
  m->kind = Mount::Memory;
  m->name = name.empty() ? "memory" : name;
  m->source = "(memory)";
  m->priority = priority;
  std::string p;
  normalise(point, p);
  m->point = p.empty() ? std::string() : p + "/";
  m->refreshFingerprint();
  const int id = m->id;
  mounts_.push_back(std::move(m));
  sortMounts();
  ++revision_;
  return id;
}

bool Vfs::addMemoryFile(int mount, const std::string& path, std::vector<uint8_t> bytes) {
  std::string rel;
  if (!normalise(path, rel) || rel.empty()) return false;
  std::unique_lock lock(mu_);
  Mount* m = mountById(mount);
  if (!m || m->kind != Mount::Memory) return false;
  Mount::Entry e;
  e.size = bytes.size();
  e.crc = crc32(bytes.data(), bytes.size());
  e.mtime = static_cast<int64_t>(++nextSeq_);
  m->entries[rel] = e;
  m->mem[rel] = std::move(bytes);
  m->refreshFingerprint();
  ++revision_;
  return true;
}

bool Vfs::addMemoryFile(int mount, const std::string& path, std::string_view text) {
  return addMemoryFile(mount, path, std::vector<uint8_t>(text.begin(), text.end()));
}

bool Vfs::unmount(int id) {
  std::unique_lock lock(mu_);
  for (size_t i = 0; i < mounts_.size(); ++i)
    if (mounts_[i]->id == id) {
      mounts_.erase(mounts_.begin() + static_cast<std::ptrdiff_t>(i));
      if (writeMount_ == id) writeMount_ = -1;
      ++revision_;
      return true;
    }
  return false;
}

void Vfs::clear() {
  std::unique_lock lock(mu_);
  mounts_.clear();
  writeMount_ = -1;
  ++revision_;
}

std::vector<MountInfo> Vfs::mounts() const {
  std::shared_lock lock(mu_);
  std::vector<MountInfo> out;
  for (const auto& m : mounts_) {
    MountInfo i;
    fill(*m, i);
    out.push_back(std::move(i));
  }
  return out;
}

bool Vfs::mountInfo(int id, MountInfo& i) const {
  std::shared_lock lock(mu_);
  const Mount* m = mountById(id);
  if (!m) return false;
  fill(*m, i);
  return true;
}

void Vfs::fill(const Mount& mount, MountInfo& i) {
  const Mount* m = &mount;
  i.id = m->id;
  i.name = m->name;
  i.source = m->source;
  i.point = m->point;
  i.priority = m->priority;
  i.packed = m->kind == Mount::Zip;
  i.memory = m->kind == Mount::Memory;
  i.fingerprint = m->fingerprint;
  i.files = 0;
  i.bytes = 0;
  if (m->kind == Mount::Dir) {
    i.files = m->seen.size();
    for (const auto& [n, s] : m->seen) i.bytes += s.size;
  } else {
    i.files = m->entries.size();
    for (const auto& [n, e] : m->entries) i.bytes += e.size;
  }
}

int Vfs::findMount(const std::string& name) const {
  std::shared_lock lock(mu_);
  for (const auto& m : mounts_)
    if (m->name == name) return m->id;
  return -1;
}

const Vfs::Mount* Vfs::mountById(int id) const {
  for (const auto& m : mounts_)
    if (m->id == id) return m.get();
  return nullptr;
}
Vfs::Mount* Vfs::mountById(int id) {
  for (const auto& m : mounts_)
    if (m->id == id) return m.get();
  return nullptr;
}

// ---------------------------------------------------------------------------
// Look-ups
// ---------------------------------------------------------------------------
// `path` is normalised. True if it lies in the mount; rel = the path inside it.
bool Vfs::inside(const Mount& m, std::string_view path, std::string& rel) const {
  if (m.point.empty()) {
    rel.assign(path);
    return true;
  }
  if (path.size() + 1 == m.point.size() && m.point.compare(0, path.size(), path) == 0) {
    rel.clear();  // the mount point itself
    return true;
  }
  if (path.size() < m.point.size() || path.compare(0, m.point.size(), m.point) != 0) return false;
  rel.assign(path.substr(m.point.size()));
  return true;
}

bool Vfs::lookup(const Mount& m, const std::string& rel, FileInfo& out) const {
  if (rel.empty()) return false;
  out.mount = m.id;
  out.path = m.point + rel;
  if (m.kind == Mount::Dir) {
    std::error_code ec;
    const std::string p = m.source + rel;
    if (!fs::is_regular_file(p, ec) || ec) return false;
    out.size = fs::file_size(p, ec);
    out.mtime = stampOf(fs::last_write_time(p, ec));
    out.crc = 0;
    out.packed = false;
    return true;
  }
  auto it = m.entries.find(rel);
  if (it == m.entries.end()) {
    auto lo = m.lower.find(lowered(rel));
    if (lo == m.lower.end()) return false;
    it = m.entries.find(lo->second);
    if (it == m.entries.end()) return false;
    out.path = m.point + it->first;
  }
  out.size = it->second.size;
  out.mtime = it->second.mtime;
  out.crc = it->second.crc;
  out.packed = true;
  return true;
}

bool Vfs::hasDir(const Mount& m, const std::string& rel) const {
  if (m.kind == Mount::Dir) {
    std::error_code ec;
    return fs::is_directory(m.source + rel, ec) && !ec;
  }
  if (rel.empty()) return !m.entries.empty();
  const std::string prefix = rel + "/";
  auto it = m.entries.lower_bound(prefix);
  if (it != m.entries.end() && it->first.compare(0, prefix.size(), prefix) == 0) return true;
  const std::string lp = lowered(prefix);
  auto lo = m.lower.lower_bound(lp);
  return lo != m.lower.end() && lo->first.compare(0, lp.size(), lp) == 0;
}

bool Vfs::readFrom(const Mount& m, const std::string& rel, std::vector<uint8_t>& out, std::string* error) const {
  if (m.kind == Mount::Dir) {
    const std::string p = m.source + rel;
    std::ifstream in(p, std::ios::binary);
    if (!in) {
      if (error) *error = "can't open " + p;
      return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    out.resize(size > 0 ? static_cast<size_t>(size) : 0);
    if (!out.empty()) in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!in && !out.empty()) {
      if (error) *error = "can't read " + p;
      return false;
    }
    return true;
  }
  std::string name = rel;
  auto it = m.entries.find(name);
  if (it == m.entries.end()) {
    auto lo = m.lower.find(lowered(rel));
    if (lo != m.lower.end()) {
      name = lo->second;
      it = m.entries.find(name);
    }
  }
  if (it == m.entries.end()) {
    if (error) *error = describeMount(m, rel) + " does not exist";
    return false;
  }
  if (m.kind == Mount::Memory) {
    out = m.mem.at(name);
    return true;
  }
  std::string err;
  std::lock_guard zl(m.zipMu);
  if (!m.zip || !m.zip->read(it->second.index, out, err)) {
    if (error) *error = describeMount(m, rel) + ": " + err;
    return false;
  }
  return true;
}

bool Vfs::stat(std::string_view path, FileInfo& out) const {
  std::string p, rel;
  if (!normalise(path, p)) return false;
  std::shared_lock lock(mu_);
  for (size_t i = mounts_.size(); i-- > 0;)
    if (inside(*mounts_[i], p, rel) && lookup(*mounts_[i], rel, out)) return true;
  return false;
}

bool Vfs::exists(std::string_view path) const {
  FileInfo fi;
  return stat(path, fi);
}

bool Vfs::read(std::string_view path, std::vector<uint8_t>& out, std::string* error) const {
  std::string p, rel;
  if (!normalise(path, p)) {
    if (error) *error = "'" + std::string(path) + "' is not a path inside the game's files";
    return false;
  }
  std::shared_lock lock(mu_);
  FileInfo fi;
  for (size_t i = mounts_.size(); i-- > 0;)
    if (inside(*mounts_[i], p, rel) && lookup(*mounts_[i], rel, fi)) return readFrom(*mounts_[i], rel, out, error);
  if (error) *error = p + " does not exist";
  return false;
}

bool Vfs::readText(std::string_view path, std::string& out, std::string* error) const {
  std::vector<uint8_t> bytes;
  if (!read(path, bytes, error)) return false;
  out.assign(bytes.begin(), bytes.end());
  return true;
}

std::vector<FileInfo> Vfs::layers(std::string_view path) const {
  std::vector<FileInfo> out;
  std::string p, rel;
  if (!normalise(path, p)) return out;
  std::shared_lock lock(mu_);
  for (const auto& m : mounts_) {
    FileInfo fi;
    if (inside(*m, p, rel) && lookup(*m, rel, fi)) out.push_back(std::move(fi));
  }
  return out;
}

std::vector<FileInfo> Vfs::list(std::string_view dir, bool recursive, std::string_view ext) const {
  std::vector<FileInfo> out;
  std::string d;
  if (!normalise(dir, d)) return out;
  const std::string prefix = d.empty() ? std::string() : d + "/";
  std::map<std::string, FileInfo> merged;
  auto keep = [&](const std::string& vpath, uint64_t size, int64_t mtime, uint32_t crc, const Mount& m) {
    if (!ext.empty() && !endsWithNoCase(vpath, ext)) return;
    FileInfo fi;
    fi.path = vpath;
    fi.size = size;
    fi.mtime = mtime;
    fi.crc = crc;
    fi.mount = m.id;
    fi.packed = m.kind != Mount::Dir;
    merged[vpath] = std::move(fi);
  };
  std::shared_lock lock(mu_);
  for (const auto& mp : mounts_) {
    const Mount& m = *mp;
    if (m.kind == Mount::Dir) {
      std::string rel;
      if (inside(m, d, rel)) {
        const std::string root = m.source + (rel.empty() ? std::string() : rel + "/");
        const std::string vroot = m.point + (rel.empty() ? std::string() : rel + "/");
        walkDir(root, recursive, [&](const std::string& r, uint64_t size, int64_t mtime) {
          keep(vroot + r, size, mtime, 0, m);
        });
      } else if (recursive && m.point.compare(0, prefix.size(), prefix) == 0) {
        walkDir(m.source, true, [&](const std::string& r, uint64_t size, int64_t mtime) {
          keep(m.point + r, size, mtime, 0, m);
        });
      }
      continue;
    }
    for (const auto& [name, e] : m.entries) {
      const std::string vpath = m.point + name;
      if (vpath.compare(0, prefix.size(), prefix) != 0) continue;
      if (!recursive && vpath.find('/', prefix.size()) != std::string::npos) continue;
      keep(vpath, e.size, e.mtime, e.crc, m);
    }
  }
  out.reserve(merged.size());
  for (auto& [p, fi] : merged) out.push_back(std::move(fi));
  return out;
}

std::vector<std::string> Vfs::listDirs(std::string_view dir) const {
  std::string d;
  if (!normalise(dir, d)) return {};
  const std::string prefix = d.empty() ? std::string() : d + "/";
  std::set<std::string> names;
  auto below = [&](const std::string& vpath) {
    if (vpath.size() <= prefix.size() || vpath.compare(0, prefix.size(), prefix) != 0) return;
    const size_t slash = vpath.find('/', prefix.size());
    if (slash != std::string::npos) names.insert(vpath.substr(prefix.size(), slash - prefix.size()));
  };
  std::shared_lock lock(mu_);
  for (const auto& mp : mounts_) {
    const Mount& m = *mp;
    if (!m.point.empty()) below(m.point);
    if (m.kind == Mount::Dir) {
      std::string rel;
      if (!inside(m, d, rel)) continue;
      std::error_code ec;
      const std::string root = m.source + (rel.empty() ? std::string() : rel + "/");
      if (!fs::is_directory(root, ec)) continue;
      for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        const std::string n = it->path().filename().string();
        if (!ignoredName(n) && it->is_directory(ec2)) names.insert(n);
      }
      continue;
    }
    for (const auto& [name, e] : m.entries) below(m.point + name);
  }
  return std::vector<std::string>(names.begin(), names.end());
}

bool Vfs::existsIn(int mount, std::string_view path) const {
  std::string p, rel;
  if (!normalise(path, p)) return false;
  std::shared_lock lock(mu_);
  const Mount* m = mountById(mount);
  if (!m || !inside(*m, p, rel)) return false;
  FileInfo fi;
  return lookup(*m, rel, fi) || hasDir(*m, rel);
}

bool Vfs::statIn(int mount, std::string_view path, FileInfo& out) const {
  std::string p, rel;
  if (!normalise(path, p)) return false;
  std::shared_lock lock(mu_);
  const Mount* m = mountById(mount);
  return m && inside(*m, p, rel) && lookup(*m, rel, out);
}

bool Vfs::readIn(int mount, std::string_view path, std::vector<uint8_t>& out, std::string* error) const {
  std::string p, rel;
  if (!normalise(path, p)) {
    if (error) *error = "'" + std::string(path) + "' is not a path inside the game's files";
    return false;
  }
  std::shared_lock lock(mu_);
  const Mount* m = mountById(mount);
  FileInfo fi;
  if (!m || !inside(*m, p, rel) || !lookup(*m, rel, fi)) {
    if (error) *error = (m && inside(*m, p, rel) ? describeMount(*m, rel) : p) + " does not exist";
    return false;
  }
  return readFrom(*m, rel, out, error);
}

std::string Vfs::describe(int mount, std::string_view path) const {
  std::string p, rel;
  normalise(path, p);
  std::shared_lock lock(mu_);
  const Mount* m = mountById(mount);
  if (!m) return p;
  if (!inside(*m, p, rel)) rel = p;
  return describeMount(*m, rel);
}

std::string Vfs::describeMount(const Mount& m, const std::string& rel) const {
  if (m.kind == Mount::Dir) return m.source + rel;
  if (m.kind == Mount::Zip) return fs::path(m.source).filename().string() + ":" + rel;
  return m.name + ":" + rel;
}

// ---------------------------------------------------------------------------
// Real files
// ---------------------------------------------------------------------------
void Vfs::setCacheDir(const std::string& dir) { cacheDir_ = withSlash(dir); }

std::string Vfs::cacheRoot(const Mount& m) const {
  return cacheDir_ + safeName(m.name) + "-" + m.fingerprint + "/";
}

// Unpacks one file, or everything below a folder, into the cache; returns the
// cache's root for this mount, or "" if something could not be written.
std::string Vfs::unpack(const Mount& m, const std::string& rel, bool folder) const {
  PNR_PROFILE("VFS: unpack to cache");
  std::lock_guard lock(cacheMu_);
  const std::string root = cacheRoot(m);
  std::error_code ec;
  if (!m.cacheCleaned) {
    // Older unpacked copies of this mount (other contents) are of no more use.
    m.cacheCleaned = true;
    const std::string mine = safeName(m.name) + "-";
    const std::string keepName = safeName(m.name) + "-" + m.fingerprint;
    if (fs::is_directory(cacheDir_, ec))
      for (fs::directory_iterator it(cacheDir_, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        // "<name>-<16 hex digits>": only folders this mount made itself.
        if (n.size() == mine.size() + 16 && n.compare(0, mine.size(), mine) == 0 && n != keepName) {
          std::error_code ec2;
          fs::remove_all(it->path(), ec2);
        }
      }
  }
  const std::string prefix = rel.empty() ? std::string() : rel + "/";
  for (const auto& [name, e] : m.entries) {
    if (folder) {
      if (name.compare(0, prefix.size(), prefix) != 0) continue;
    } else if (name != rel) {
      continue;
    }
    const std::string target = root + name;
    if (fs::is_regular_file(target, ec) && fs::file_size(target, ec) == e.size) continue;
    std::vector<uint8_t> bytes;
    std::string err;
    if (!readFrom(m, name, bytes, &err) || !writeWhole(target, bytes.data(), bytes.size(), &err)) {
      PNR_WARN_CH(Content, "Can't unpack %s: %s", describeMount(m, name).c_str(), err.c_str());
      return {};
    }
  }
  return root;
}

std::string Vfs::diskPathIn(int mount, std::string_view path) const {
  const bool folder = path.empty() || path.back() == '/' || path.back() == '\\';
  std::string p, rel;
  if (!normalise(path, p)) return {};
  std::shared_lock lock(mu_);
  const Mount* m = mountById(mount);
  if (!m || !inside(*m, p, rel)) return {};
  FileInfo fi;
  if (m->kind == Mount::Dir) {
    std::error_code ec;
    if (folder) {
      const std::string d = m->source + (rel.empty() ? std::string() : rel + "/");
      return fs::is_directory(d, ec) ? d : std::string();
    }
    return lookup(*m, rel, fi) ? m->source + rel : std::string();
  }
  if (folder) {
    if (!hasDir(*m, rel)) return {};
    const std::string root = unpack(*m, rel, true);
    return root.empty() ? std::string() : root + (rel.empty() ? std::string() : rel + "/");
  }
  if (!lookup(*m, rel, fi)) return {};
  const std::string stored = fi.path.substr(m->point.size());  // the name as the archive spells it
  const std::string root = unpack(*m, stored, false);
  return root.empty() ? std::string() : root + stored;
}

std::string Vfs::diskPath(std::string_view path) const {
  FileInfo fi;
  if (!stat(path, fi)) return {};
  return diskPathIn(fi.mount, fi.path);
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------
bool Vfs::write(std::string_view path, const void* data, size_t size, std::string* error) {
  std::string p, rel;
  if (!normalise(path, p) || p.empty()) {
    if (error) *error = "'" + std::string(path) + "' is not a path inside the game's files";
    return false;
  }
  std::string target;
  {
    std::shared_lock lock(mu_);
    const Mount* m = mountById(writeMount_);
    if (!m || m->kind != Mount::Dir) {
      if (error) *error = "there is no folder to write to";
      return false;
    }
    if (!inside(*m, p, rel) || rel.empty()) {
      if (error) *error = p + " is outside the folder that may be written (" + m->point + ")";
      return false;
    }
    target = m->source + rel;
  }
  if (!writeWhole(target, data, size, error)) return false;
  ++revision_;
  return true;
}

bool Vfs::writeText(std::string_view path, std::string_view text, std::string* error) {
  return write(path, text.data(), text.size(), error);
}

// ---------------------------------------------------------------------------
// Changes on disk
// ---------------------------------------------------------------------------
Snapshot Vfs::scan() const {
  PNR_PROFILE("VFS: scan for changes");
  const auto t0 = std::chrono::steady_clock::now();
  struct Todo {
    int id;
    bool archive;
    std::string source;
  };
  std::vector<Todo> todo;
  {
    std::shared_lock lock(mu_);
    for (const auto& m : mounts_)
      if (m->kind != Mount::Memory) todo.push_back({m->id, m->kind == Mount::Zip, m->source});
  }
  Snapshot snap;
  for (const Todo& t : todo) {
    Snapshot::MountScan ms;
    ms.id = t.id;
    ms.archive = t.archive;
    if (t.archive) {
      std::error_code ec;
      ms.archiveStamp.size = fs::file_size(t.source, ec);
      if (ec) ms.archiveStamp.size = 0;
      const auto when = fs::last_write_time(t.source, ec);
      ms.archiveStamp.mtime = ec ? 0 : stampOf(when);
    } else {
      walkDir(t.source, true, [&](const std::string& rel, uint64_t size, int64_t mtime) {
        ms.files[rel] = Stamp{size, mtime};
      });
      snap.fileCount += ms.files.size();
    }
    snap.mounts.push_back(std::move(ms));
  }
  snap.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return snap;
}

std::vector<Change> Vfs::apply(const Snapshot& snap) {
  std::vector<Change> changes;
  std::unique_lock lock(mu_);
  for (const Snapshot::MountScan& ms : snap.mounts) {
    Mount* m = mountById(ms.id);
    if (!m) continue;  // unmounted since the scan
    if (!ms.archive) {
      if (!m->scanned) {
        m->seen = ms.files;
        m->scanned = true;
        continue;
      }
      auto a = m->seen.begin();
      auto b = ms.files.begin();
      while (a != m->seen.end() || b != ms.files.end()) {
        if (b == ms.files.end() || (a != m->seen.end() && a->first < b->first)) {
          changes.push_back({m->point + a->first, m->id, ChangeKind::Removed});
          ++a;
        } else if (a == m->seen.end() || b->first < a->first) {
          changes.push_back({m->point + b->first, m->id, ChangeKind::Added});
          ++b;
        } else {
          if (a->second != b->second) changes.push_back({m->point + a->first, m->id, ChangeKind::Modified});
          ++a;
          ++b;
        }
      }
      m->seen = ms.files;
      continue;
    }
    if (!m->scanned) {
      m->scanned = true;
      m->archiveStamp = ms.archiveStamp;
      continue;
    }
    if (ms.archiveStamp == m->archiveStamp || ms.archiveStamp.size == 0) continue;
    // The archive was replaced: open the new one and compare what is inside.
    auto fresh = std::make_unique<zip::Reader>();
    std::string err;
    if (!fresh->open(m->source, err)) continue;  // still being written: try again next time
    zip::Index idx;
    zip::index(*fresh, idx);
    const std::string strip = idx.strip;
    std::map<std::string, Mount::Entry> next;
    for (const auto& [n, f] : idx.files) next[n] = Mount::Entry{f.first, f.second.size, f.second.crc, f.second.mtime};
    for (const auto& [n, e] : m->entries) {
      auto it = next.find(n);
      if (it == next.end()) changes.push_back({m->point + n, m->id, ChangeKind::Removed});
      else if (it->second.size != e.size || it->second.crc != e.crc)
        changes.push_back({m->point + n, m->id, ChangeKind::Modified});
    }
    for (const auto& [n, e] : next)
      if (!m->entries.count(n)) changes.push_back({m->point + n, m->id, ChangeKind::Added});
    {
      std::lock_guard zl(m->zipMu);
      m->zip = std::move(fresh);
      m->entries = std::move(next);
      m->strip = strip;
      m->refreshFingerprint();
      m->cacheCleaned = false;
    }
    m->archiveStamp = ms.archiveStamp;
  }
  if (!changes.empty()) ++revision_;
  return changes;
}

}  // namespace pioneer::vfs

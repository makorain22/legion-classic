// Pioneer Engine - cooking content and the asset index. See cook.hpp.
#include "pioneer/tools/cook.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

#include "pioneer/core/jobs.hpp"
#include "pioneer/core/version.hpp"

namespace pioneer::tools {

namespace fs = std::filesystem;
using nlohmann::json;
using ojson = nlohmann::ordered_json;

namespace {

// Zip's earliest date (1 Jan 1980): what every entry of a cooked pack is dated.
constexpr int64_t kPackTime = 315532800;
constexpr int kStateVersion = 1;

bool endsWith(const std::string& s, const char* tail) {
  const size_t n = std::char_traits<char>::length(tail);
  return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

std::string lowerExt(const std::string& path) {
  const size_t dot = path.rfind('.');
  const size_t slash = path.rfind('/');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return {};
  std::string e = path.substr(dot);
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return e;
}

// Editors' leftovers, which never ship (the same rule as packs).
bool leftover(const std::string& path) {
  size_t at = 0;
  while (at < path.size()) {
    const size_t slash = path.find('/', at);
    const std::string part = path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
    if (part.empty() || part[0] == '.' || part.back() == '~') return true;
    if (slash == std::string::npos) break;
    at = slash + 1;
  }
  return endsWith(path, ".tmp") || endsWith(path, ".bak");
}

bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

// Through a temporary file, so a reader never sees half a file.
bool writeFile(const fs::path& p, const void* data, size_t size, std::string& error) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  const fs::path tmp = p.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "can't write " + p.generic_string();
      return false;
    }
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!out) {
      error = "can't write " + p.generic_string();
      return false;
    }
  }
  fs::remove(p, ec);
  fs::rename(tmp, p, ec);
  if (ec) {
    error = "can't write " + p.generic_string() + ": " + ec.message();
    return false;
  }
  return true;
}

std::string hashHex(uint64_t h) { return vfs::hex64(h); }

bool parseHex(const std::string& s, uint64_t& out) {
  if (s.empty() || s.size() > 16) return false;
  out = 0;
  for (char c : s) {
    out <<= 4;
    if (c >= '0' && c <= '9') out |= static_cast<uint64_t>(c - '0');
    else if (c >= 'a' && c <= 'f') out |= static_cast<uint64_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') out |= static_cast<uint64_t>(c - 'A' + 10);
    else return false;
  }
  return true;
}

// Is `inner` the same folder as `outer`, or inside it?
bool within(const fs::path& inner, const fs::path& outer) {
  std::error_code ec;
  const fs::path a = fs::weakly_canonical(inner, ec);
  const fs::path b = fs::weakly_canonical(outer, ec);
  auto ai = a.begin(), bi = b.begin();
  for (; bi != b.end(); ++ai, ++bi) {
    if (bi->empty()) continue;  // a trailing separator
    if (ai == a.end() || *ai != *bi) return false;
  }
  return true;
}

void removeEmptyDirs(const fs::path& root) {
  std::error_code ec;
  std::vector<fs::path> dirs;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_directory(ec)) dirs.push_back(it->path());
  std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return a.string().size() > b.string().size(); });
  for (const fs::path& d : dirs)
    if (fs::is_empty(d, ec)) fs::remove(d, ec);
}

}  // namespace

std::vector<uint8_t> minifiedJson(const std::vector<uint8_t>& bytes, bool* changed) {
  if (changed) *changed = false;
  // ordered_json keeps the keys in the order they were written.
  const ojson doc = ojson::parse(bytes.begin(), bytes.end(), nullptr, false, true);
  if (doc.is_discarded()) return bytes;
  const std::string s = doc.dump(-1, ' ', false, ojson::error_handler_t::replace);
  std::vector<uint8_t> out(s.begin(), s.end());
  if (changed) *changed = out != bytes;
  return out;
}

const AssetIndex::Entry* AssetIndex::find(const std::string& path) const {
  auto it = std::lower_bound(entries.begin(), entries.end(), path, [](const Entry& e, const std::string& p) { return e.path < p; });
  return it != entries.end() && it->path == path ? &*it : nullptr;
}

std::string contentHashOf(const AssetIndex& index) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (const AssetIndex::Entry& e : index.entries) {
    h = vfs::hash64(e.path.data(), e.path.size(), h);
    h = vfs::hash64(&e.hash, sizeof e.hash, h);
  }
  return hashHex(h);
}

AssetIndex indexOf(assets::Registry& reg) {
  AssetIndex idx;
  idx.engine = kEngineVersion;
  for (assets::Id id : reg.all()) {
    const assets::Record* r = reg.get(id);
    if (!r || r->path == kIndexFile || leftover(r->path)) continue;
    AssetIndex::Entry e;
    e.path = r->path;
    e.type = assets::typeName(r->type);
    e.size = r->size;
    e.hash = reg.contentHash(id);
    for (assets::Id u : r->uses)
      if (const assets::Record* d = reg.get(u)) e.uses.push_back(d->path);
    std::sort(e.uses.begin(), e.uses.end());
    idx.entries.push_back(std::move(e));
  }
  std::sort(idx.entries.begin(), idx.entries.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
  idx.contentHash = contentHashOf(idx);
  return idx;
}

json toJson(const AssetIndex& index) {
  json j;
  j["format"] = 1;
  j["engine"] = index.engine;
  j["contentHash"] = index.contentHash;
  j["files"] = index.entries.size();
  json list = json::array();
  for (const AssetIndex::Entry& e : index.entries) {
    json o;
    o["path"] = e.path;
    o["type"] = e.type;
    o["size"] = e.size;
    o["hash"] = hashHex(e.hash);
    if (!e.uses.empty()) o["uses"] = e.uses;
    list.push_back(std::move(o));
  }
  j["assets"] = std::move(list);
  return j;
}

bool fromJson(const json& j, AssetIndex& out, std::string& error) {
  out = {};
  if (!j.is_object() || !j.contains("assets") || !j["assets"].is_array()) {
    error = "not an asset index (no \"assets\" list)";
    return false;
  }
  out.engine = j.value("engine", std::string());
  out.contentHash = j.value("contentHash", std::string());
  try {
    for (const json& o : j["assets"]) {
      AssetIndex::Entry e;
      e.path = o.at("path").get<std::string>();
      e.type = o.value("type", std::string("unknown"));
      e.size = o.value("size", uint64_t{0});
      if (!parseHex(o.value("hash", std::string()), e.hash)) {
        error = e.path + ": the hash is not 16 hex digits";
        return false;
      }
      for (const json& u : o.value("uses", json::array())) e.uses.push_back(u.get<std::string>());
      out.entries.push_back(std::move(e));
    }
  } catch (const std::exception& ex) {
    error = ex.what();
    return false;
  }
  std::sort(out.entries.begin(), out.entries.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
  return true;
}

bool readIndex(const std::string& file, AssetIndex& out, std::string& error) {
  std::vector<uint8_t> bytes;
  if (!readFile(file, bytes)) {
    error = "can't read " + file;
    return false;
  }
  const json j = json::parse(bytes.begin(), bytes.end(), nullptr, false);
  if (j.is_discarded()) {
    error = file + " is not valid JSON";
    return false;
  }
  return fromJson(j, out, error);
}

IndexDiff diff(const AssetIndex& from, const AssetIndex& to) {
  IndexDiff d;
  size_t i = 0, k = 0;
  while (i < from.entries.size() || k < to.entries.size()) {
    if (k == to.entries.size() || (i < from.entries.size() && from.entries[i].path < to.entries[k].path)) {
      d.removed.push_back(from.entries[i++].path);
    } else if (i == from.entries.size() || to.entries[k].path < from.entries[i].path) {
      d.added.push_back(to.entries[k++].path);
    } else {
      if (from.entries[i].hash != to.entries[k].hash || from.entries[i].size != to.entries[k].size)
        d.changed.push_back(to.entries[k].path);
      ++i;
      ++k;
    }
  }
  return d;
}

bool cook(const vfs::Vfs& vfs, assets::Registry& reg, const std::string& outDirIn, const CookOptions& opt, CookResult& res) {
  const auto t0 = std::chrono::steady_clock::now();
  res = {};
  std::error_code ec;
  if (outDirIn.empty()) {
    res.errors.push_back("no output folder given");
    return false;
  }
  const fs::path outDir = fs::path(outDirIn);
  // Never into the content itself.
  for (const vfs::MountInfo& m : vfs.mounts()) {
    if (m.packed || m.memory || m.source.empty()) continue;
    if (within(outDir, m.source) || within(m.source, outDir)) {
      res.errors.push_back("the output folder " + outDir.generic_string() + " overlaps the content folder " + m.source);
      return false;
    }
  }
  const fs::path statePath = outDir / kCookStateFile;
  json state = json::object();
  if (fs::exists(outDir, ec) && !fs::is_empty(outDir, ec)) {
    std::vector<uint8_t> bytes;
    if (!readFile(statePath, bytes)) {
      res.errors.push_back(outDir.generic_string() + " holds files that were not cooked there: cook into an empty folder");
      return false;
    }
    state = json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (state.is_discarded() || !state.is_object()) state = json::object();
  }
  const std::string optionsKey = opt.minifyJson ? "minify" : "copy";
  json previous = state.value("files", json::object());
  if (state.value("version", 0) != kStateVersion || state.value("options", std::string()) != optionsKey || !previous.is_object())
    previous = json::object();
  if (opt.clean) {
    for (const auto& [path, v] : previous.items()) fs::remove(outDir / path, ec);
    fs::remove(outDir / kIndexFile, ec);
    previous = json::object();
    res.removed = 0;
  }
  fs::create_directories(outDir, ec);
  if (ec) {
    res.errors.push_back("can't make " + outDir.generic_string() + ": " + ec.message());
    return false;
  }

  // What uses what, for the index.
  reg.scanDependencies();

  // What goes in.
  struct Item {
    assets::Id id = assets::kNone;
    std::string path;
    std::vector<uint8_t> out;
    uint64_t srcHash = 0, outHash = 0, srcSize = 0;
    bool minified = false;
    std::string error;
  };
  std::vector<Item> items;
  for (assets::Id id : reg.all()) {
    const assets::Record* r = reg.get(id);
    if (!r || leftover(r->path) || r->path == kIndexFile) continue;
    bool skip = false;
    for (const std::string& x : opt.exclude)
      if (!x.empty() && r->path.find(x) != std::string::npos) skip = true;
    if (skip) continue;
    Item it;
    it.id = id;
    it.path = r->path;
    items.push_back(std::move(it));
  }
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.path < b.path; });

  // Read, cook and hash on every core; write in path order.
  jobs::system().parallelFor(
      items.size(), 1,
      [&](size_t b, size_t e, size_t, int) {
        for (size_t i = b; i < e; ++i) {
          Item& it = items[i];
          std::vector<uint8_t> src;
          if (!vfs.read(it.path, src, &it.error)) {
            if (it.error.empty()) it.error = it.path + ": can't be read";
            continue;
          }
          it.srcSize = src.size();
          it.srcHash = vfs::hash64(src.data(), src.size());
          if (opt.minifyJson && lowerExt(it.path) == ".json") it.out = minifiedJson(src, &it.minified);
          else it.out = std::move(src);
          it.outHash = vfs::hash64(it.out.data(), it.out.size());
        }
      },
      "Cook");

  json files = json::object();
  AssetIndex index;
  index.engine = kEngineVersion;
  for (Item& it : items) {
    if (!it.error.empty()) {
      res.errors.push_back(it.error);
      continue;
    }
    ++res.files;
    res.bytesIn += it.srcSize;
    res.bytesOut += it.out.size();
    if (it.minified) ++res.minified;
    const fs::path dst = outDir / it.path;
    bool reuse = false;
    if (opt.incremental && previous.contains(it.path)) {
      const json& p = previous[it.path];
      uint64_t src = 0;
      reuse = p.is_object() && parseHex(p.value("src", std::string()), src) && src == it.srcHash &&
              fs::file_size(dst, ec) == it.out.size() && !ec;
    }
    if (reuse) {
      ++res.reused;
    } else {
      std::string err;
      if (!writeFile(dst, it.out.data(), it.out.size(), err)) {
        res.errors.push_back(err);
        continue;
      }
      ++res.written;
    }
    files[it.path] = {{"src", hashHex(it.srcHash)}, {"out", hashHex(it.outHash)}, {"size", it.out.size()}};
    AssetIndex::Entry e;
    e.path = it.path;
    const assets::Record* r = reg.get(it.id);
    e.type = assets::typeName(r->type);
    e.size = it.out.size();
    e.hash = it.outHash;
    for (assets::Id u : r->uses)
      if (const assets::Record* d = reg.get(u)) e.uses.push_back(d->path);
    std::sort(e.uses.begin(), e.uses.end());
    index.entries.push_back(std::move(e));
  }
  // What was cooked last time and is gone now.
  for (const auto& [path, v] : previous.items())
    if (!files.contains(path)) {
      fs::remove(outDir / path, ec);
      ++res.removed;
    }
  removeEmptyDirs(outDir);

  index.contentHash = contentHashOf(index);
  res.contentHash = index.contentHash;
  {
    const std::string text = toJson(index).dump(1) + "\n";
    std::vector<uint8_t> have;
    if (!readFile(outDir / kIndexFile, have) || std::string(have.begin(), have.end()) != text) {
      std::string err;
      if (!writeFile(outDir / kIndexFile, text.data(), text.size(), err)) res.errors.push_back(err);
    }
  }
  {
    json s;
    s["version"] = kStateVersion;
    s["options"] = optionsKey;
    s["files"] = std::move(files);
    const std::string text = s.dump(1) + "\n";
    std::string err;
    if (!writeFile(statePath, text.data(), text.size(), err)) res.errors.push_back(err);
  }
  if (res.errors.empty() && !opt.packFile.empty()) {
    pack::Manifest m = opt.manifest;
    if (m.id.empty()) m.id = "content";
    if (m.engine.empty()) m.engine = std::string(">=") + kEngineVersion;
    pack::BuildOptions bo;
    bo.level = opt.packLevel;
    bo.fixedTime = kPackTime;
    std::string err;
    std::string dir = outDir.generic_string();
    if (dir.back() != '/') dir += '/';
    if (!pack::build(dir, opt.packFile, m, err, bo)) res.errors.push_back("the pack: " + err);
  }
  res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return res.errors.empty();
}

}  // namespace pioneer::tools

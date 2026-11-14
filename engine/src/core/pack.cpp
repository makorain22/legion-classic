// Pioneer Engine - packs (see pack.hpp).
#include "pioneer/core/pack.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

#include "pioneer/core/version.hpp"
#include "pioneer/core/vfs.hpp"
#include "zip_file.hpp"

namespace pioneer::pack {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string trimmed(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return std::string(s.substr(a, b - a));
}

std::string withSlash(std::string d) {
  for (char& c : d)
    if (c == '\\') c = '/';
  if (!d.empty() && d.back() != '/') d += '/';
  return d;
}

bool endsWith(const std::string& s, const char* suffix) {
  const size_t n = std::char_traits<char>::length(suffix);
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i)
    if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) != suffix[i]) return false;
  return true;
}

// Already squeezed: deflating again only costs time.
bool storedAsIs(const std::string& path) {
  for (const char* e : {".ogg", ".png", ".jpg", ".jpeg", ".mp3", ".flac", ".zip", ".pnpack", ".lmap", ".glb", ".sav"})
    if (endsWith(path, e)) return true;
  return false;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  const std::streamoff size = in.tellg();
  in.seekg(0, std::ios::beg);
  out.resize(size > 0 ? static_cast<size_t>(size) : 0);
  if (!out.empty()) in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
  return static_cast<bool>(in) || out.empty();
}

bool parseJson(const std::vector<uint8_t>& bytes, json& out) {
  out = json::parse(bytes.begin(), bytes.end(), nullptr, false, true);
  return !out.is_discarded();
}

std::string hex32(uint32_t v) {
  char buf[12];
  std::snprintf(buf, sizeof(buf), "%08x", v);
  return buf;
}

void fillFromModJson(const json& j, Manifest& m, bool overwrite) {
  if (!j.is_object()) return;
  auto take = [&](std::string& field, const char* key) {
    if ((overwrite || field.empty()) && j.contains(key) && j[key].is_string()) field = j[key].get<std::string>();
  };
  take(m.name, "name");
  take(m.version, "version");
  take(m.author, "author");
  take(m.description, "description");
  take(m.engine, "engine");
  if ((overwrite || m.requirements.empty()) && j.contains("requires") && j["requires"].is_array()) {
    m.requirements.clear();
    for (const auto& r : j["requires"]) {
      Requirement q;
      if (r.is_string() && Requirement::parse(r.get<std::string>(), q)) m.requirements.push_back(std::move(q));
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Versions and requirements
// ---------------------------------------------------------------------------
bool Version::parse(std::string_view text, Version& out) {
  out = Version{};
  size_t i = 0;
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
  if (i < text.size() && (text[i] == 'v' || text[i] == 'V')) ++i;
  int* parts[3] = {&out.major, &out.minor, &out.patch};
  int got = 0;
  for (; got < 3; ++got) {
    if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) break;
    long v = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
      v = v * 10 + (text[i] - '0');
      if (v > 1000000) v = 1000000;
      ++i;
    }
    *parts[got] = static_cast<int>(v);
    if (i < text.size() && text[i] == '.') ++i;
    else {
      ++got;
      break;
    }
  }
  return got > 0;
}

std::string Version::str() const {
  return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

int Version::compare(const Version& o) const {
  if (major != o.major) return major < o.major ? -1 : 1;
  if (minor != o.minor) return minor < o.minor ? -1 : 1;
  if (patch != o.patch) return patch < o.patch ? -1 : 1;
  return 0;
}

bool Requirement::parseRange(std::string_view text, Requirement& out) {
  const std::string t = trimmed(text);
  out.op.clear();
  out.version = Version{};
  if (t.empty() || t == "*") return true;
  size_t i = 0;
  while (i < t.size() && (t[i] == '<' || t[i] == '>' || t[i] == '=' || t[i] == '^' || t[i] == '~')) ++i;
  std::string op = t.substr(0, i);
  if (op == "==") op = "=";
  if (op.empty()) op = ">=";  // "veterans 1.2" means at least 1.2
  if (op != ">=" && op != ">" && op != "<=" && op != "<" && op != "=" && op != "^" && op != "~") return false;
  if (!Version::parse(std::string_view(t).substr(i), out.version)) return false;
  out.op = op;
  return true;
}

bool Requirement::parse(std::string_view text, Requirement& out) {
  const std::string t = trimmed(text);
  size_t i = 0;
  while (i < t.size() && !std::isspace(static_cast<unsigned char>(t[i])) && t[i] != '<' && t[i] != '>' &&
         t[i] != '=' && t[i] != '^' && t[i] != '~')
    ++i;
  out.id = t.substr(0, i);
  if (out.id.empty()) return false;
  return parseRange(std::string_view(t).substr(i), out);
}

bool Requirement::allows(const Version& have) const {
  const int c = have.compare(version);
  if (op.empty()) return true;
  if (op == ">=") return c >= 0;
  if (op == ">") return c > 0;
  if (op == "<=") return c <= 0;
  if (op == "<") return c < 0;
  if (op == "=") return c == 0;
  if (op == "^") return c >= 0 && have.major == version.major;
  if (op == "~") return c >= 0 && have.major == version.major && have.minor == version.minor;
  return false;
}

std::string Requirement::str() const {
  if (op.empty()) return id;
  return (id.empty() ? std::string() : id + " ") + op + (op == "^" || op == "~" ? "" : " ") + version.str();
}

std::string Requirement::wording() const {
  const std::string v = version.str();
  if (op.empty()) return "any version";
  if (op == ">=") return v + " or newer";
  if (op == ">") return "newer than " + v;
  if (op == "<=") return v + " or older";
  if (op == "<") return "older than " + v;
  if (op == "=") return "exactly " + v;
  if (op == "^") return v + " or newer, below " + std::to_string(version.major + 1) + ".0.0";
  return v + " or newer, below " + std::to_string(version.major) + "." + std::to_string(version.minor + 1) + ".0";
}

// ---------------------------------------------------------------------------
// The manifest
// ---------------------------------------------------------------------------
uint64_t Manifest::totalBytes() const {
  uint64_t n = 0;
  for (const FileEntry& f : files) n += f.size;
  return n;
}

std::string checksumOf(const std::vector<FileEntry>& files) {
  std::vector<const FileEntry*> sorted;
  for (const FileEntry& f : files) sorted.push_back(&f);
  std::sort(sorted.begin(), sorted.end(), [](const FileEntry* a, const FileEntry* b) { return a->path < b->path; });
  uint64_t h = 0xcbf29ce484222325ull;
  for (const FileEntry* f : sorted) {
    h = vfs::hash64(f->path.data(), f->path.size() + 1, h);  // with the terminator, so "ab","c" != "a","bc"
    h = vfs::hash64(&f->size, sizeof(f->size), h);
    h = vfs::hash64(&f->crc, sizeof(f->crc), h);
  }
  return vfs::hex64(h);
}

std::vector<std::string> kindsOf(const std::vector<FileEntry>& files) {
  static const char* const kKnown[] = {"rules",  "campaigns", "maps",    "scenarios", "audio",
                                       "art",    "models",    "scripts", "shaders",   "fonts"};
  std::set<std::string> have;
  for (const FileEntry& f : files) {
    const size_t slash = f.path.find('/');
    if (slash == std::string::npos) continue;
    const std::string top = f.path.substr(0, slash);
    for (const char* k : kKnown)
      if (top == k) have.insert(top);
  }
  std::vector<std::string> out;
  for (const char* k : kKnown)
    if (have.count(k)) out.push_back(k);
  return out;
}

json toJson(const Manifest& m) {
  json j = json::object();
  j["format"] = m.format;
  j["id"] = m.id;
  j["name"] = m.name.empty() ? m.id : m.name;
  j["version"] = m.version.empty() ? std::string("1.0") : m.version;
  if (!m.author.empty()) j["author"] = m.author;
  if (!m.description.empty()) j["description"] = m.description;
  j["kinds"] = m.kinds;
  if (!m.engine.empty()) j["engine"] = m.engine;
  json req = json::array();
  for (const Requirement& r : m.requirements) req.push_back(r.str());
  j["requires"] = req;
  json files = json::array();
  for (const FileEntry& f : m.files) files.push_back({{"path", f.path}, {"size", f.size}, {"crc", hex32(f.crc)}});
  j["files"] = files;
  j["checksum"] = m.checksum;
  return j;
}

bool fromJson(const json& j, Manifest& m, std::string& error) {
  if (!j.is_object()) {
    error = "pack.json is not an object";
    return false;
  }
  m = Manifest{};
  m.format = j.value("format", kFormat);
  if (m.format > kFormat) {
    error = "made for a newer version of the game (pack format " + std::to_string(m.format) + ")";
    return false;
  }
  auto text = [&](const char* key) { return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string(); };
  m.id = text("id");
  m.name = text("name");
  m.version = text("version");
  m.author = text("author");
  m.description = text("description");
  m.engine = text("engine");
  m.checksum = text("checksum");
  if (m.version.empty()) m.version = "1.0";
  if (j.contains("kinds") && j["kinds"].is_array())
    for (const auto& k : j["kinds"])
      if (k.is_string()) m.kinds.push_back(k.get<std::string>());
  if (j.contains("requires") && j["requires"].is_array())
    for (const auto& r : j["requires"]) {
      Requirement q;
      if (!r.is_string() || !Requirement::parse(r.get<std::string>(), q)) {
        error = "pack.json: can't read the requirement " + r.dump();
        return false;
      }
      m.requirements.push_back(std::move(q));
    }
  if (j.contains("files") && j["files"].is_array())
    for (const auto& f : j["files"]) {
      FileEntry e;
      if (!f.is_object() || !f.contains("path") || !f["path"].is_string()) {
        error = "pack.json: a file entry has no path";
        return false;
      }
      std::string clean;
      if (!vfs::normalise(f["path"].get<std::string>(), clean) || clean.empty()) {
        error = "pack.json: the path " + f["path"].dump() + " leaves the pack";
        return false;
      }
      e.path = clean;
      e.size = f.value("size", static_cast<uint64_t>(0));
      if (f.contains("crc") && f["crc"].is_string())
        e.crc = static_cast<uint32_t>(std::strtoul(f["crc"].get<std::string>().c_str(), nullptr, 16));
      else if (f.contains("crc") && f["crc"].is_number_unsigned())
        e.crc = f["crc"].get<uint32_t>();
      m.files.push_back(std::move(e));
    }
  std::sort(m.files.begin(), m.files.end(), [](const FileEntry& a, const FileEntry& b) { return a.path < b.path; });
  return true;
}

bool engineAllows(const Manifest& m, std::string* why) {
  if (m.engine.empty()) return true;
  Requirement r;
  Version have;
  Version::parse(kEngineVersion, have);
  if (!Requirement::parseRange(m.engine, r)) {
    if (why) *why = "its engine version \"" + m.engine + "\" can't be read";
    return false;
  }
  if (r.allows(have)) return true;
  if (why) *why = std::string("needs ") + kEngineName + " " + r.wording() + " (this is " + kEngineVersion + ")";
  return false;
}

// ---------------------------------------------------------------------------
// Building, reading, checking, unpacking
// ---------------------------------------------------------------------------
bool build(const std::string& dirIn, const std::string& outFile, Manifest& m, std::string& error,
           const BuildOptions& options) {
  const std::string dir = withSlash(dirIn);
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    error = dirIn + " is not a folder";
    return false;
  }
  std::vector<uint8_t> bytes;
  json modJson;
  if (readFile(dir + "mod.json", bytes) && parseJson(bytes, modJson)) fillFromModJson(modJson, m, false);
  if (m.id.empty()) m.id = fs::path(dir.substr(0, dir.size() - 1)).filename().string();
  if (m.id.empty() || m.id.find_first_of("/\\: ") != std::string::npos) {
    error = "'" + m.id + "' can't be a pack's id (no spaces, slashes or colons)";
    return false;
  }
  if (m.name.empty()) m.name = m.id;
  if (m.version.empty()) m.version = "1.0";
  m.format = kFormat;

  // What goes in, in path order.
  std::vector<std::string> paths;
  const std::string outAbs = fs::absolute(outFile, ec).lexically_normal().generic_string();
  for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    const std::string name = it->path().filename().string();
    std::error_code ec2;
    if (name.empty() || name[0] == '.' || name.back() == '~' || endsWith(name, ".tmp") || endsWith(name, ".bak")) {
      if (it->is_directory(ec2)) it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file(ec2)) continue;
    const std::string full = it->path().generic_string();
    if (fs::absolute(it->path(), ec2).lexically_normal().generic_string() == outAbs) continue;  // the pack itself
    std::string rel = full.substr(dir.size());
    if (rel == kManifestName) continue;  // written afresh
    bool skip = false;
    for (const std::string& x : options.exclude)
      if (!x.empty() && rel.find(x) != std::string::npos) skip = true;
    if (!skip) paths.push_back(std::move(rel));
  }
  std::sort(paths.begin(), paths.end());
  if (paths.empty()) {
    error = dirIn + " holds no files to pack";
    return false;
  }

  struct Loaded {
    std::vector<uint8_t> bytes;
    int64_t mtime = 0;
  };
  std::vector<Loaded> loaded(paths.size());
  m.files.clear();
  for (size_t i = 0; i < paths.size(); ++i) {
    if (!readFile(dir + paths[i], loaded[i].bytes)) {
      error = "can't read " + dir + paths[i];
      return false;
    }
    const auto when = fs::last_write_time(dir + paths[i], ec);
    if (!ec) {
      // file_time_type -> seconds since 1970, portably enough for an archive's date stamp.
      const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
          when - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
      loaded[i].mtime = static_cast<int64_t>(std::chrono::system_clock::to_time_t(sys));
    }
    FileEntry e;
    e.path = paths[i];
    e.size = loaded[i].bytes.size();
    e.crc = vfs::crc32(loaded[i].bytes.data(), loaded[i].bytes.size());
    m.files.push_back(std::move(e));
  }
  if (m.kinds.empty()) m.kinds = kindsOf(m.files);
  m.checksum = checksumOf(m.files);
  m.fromModJson = false;

  const std::string tmp = outFile + ".tmp";
  const fs::path outPath(outFile);
  if (outPath.has_parent_path()) fs::create_directories(outPath.parent_path(), ec);
  {
    zip::Writer w;
    if (!w.open(tmp, error)) return false;
    const std::string manifest = toJson(m).dump(1) + "\n";
    if (!w.add(kManifestName, manifest.data(), manifest.size(), options.level, 0, error)) return false;
    for (size_t i = 0; i < paths.size(); ++i)
      if (!w.add(paths[i], loaded[i].bytes.data(), loaded[i].bytes.size(),
                 storedAsIs(paths[i]) ? 0 : options.level, loaded[i].mtime, error)) {
        fs::remove(tmp, ec);
        return false;
      }
    if (!w.finish(error)) {
      fs::remove(tmp, ec);
      return false;
    }
  }
  fs::remove(outFile, ec);
  fs::rename(tmp, outFile, ec);
  if (ec) {
    error = "can't write " + outFile;
    return false;
  }
  return true;
}

namespace {
bool manifestOf(const zip::Reader& z, const zip::Index& idx, const std::string& file, Manifest& m,
                std::string& error) {
  std::vector<uint8_t> bytes;
  json j;
  std::vector<FileEntry> inside;
  for (const auto& [name, f] : idx.files)
    if (name != kManifestName) inside.push_back(FileEntry{name, f.second.size, f.second.crc});
  auto found = idx.files.find(kManifestName);
  if (found != idx.files.end()) {
    if (!z.read(found->second.first, bytes, error)) return false;
    if (!parseJson(bytes, j)) {
      error = "pack.json is not valid JSON";
      return false;
    }
    if (!fromJson(j, m, error)) return false;
    if (m.id.empty()) m.id = fs::path(file).stem().string();
    if (m.files.empty()) {  // written by hand: trust the archive
      m.files = inside;
      m.checksum = checksumOf(m.files);
    }
    if (m.kinds.empty()) m.kinds = kindsOf(m.files);
    return true;
  }
  found = idx.files.find("mod.json");
  if (found == idx.files.end()) {
    error = "there is no pack.json or mod.json inside";
    return false;
  }
  if (!z.read(found->second.first, bytes, error)) return false;
  if (!parseJson(bytes, j) || !j.is_object()) {
    error = "mod.json is not valid JSON";
    return false;
  }
  m = Manifest{};
  m.id = fs::path(file).stem().string();
  fillFromModJson(j, m, true);
  if (m.name.empty()) m.name = m.id;
  if (m.version.empty()) m.version = "1.0";
  m.files = inside;
  m.kinds = kindsOf(m.files);
  m.checksum = checksumOf(m.files);
  m.fromModJson = true;
  return true;
}
}  // namespace

bool readManifest(const std::string& file, Manifest& m, std::string& error) {
  zip::Reader z;
  if (!z.open(file, error)) return false;
  zip::Index idx;
  zip::index(z, idx);
  return manifestOf(z, idx, file, m, error);
}

Report verify(const std::string& file) {
  Report r;
  zip::Reader z;
  std::string error;
  if (!z.open(file, error)) {
    r.problems.push_back(error);
    return r;
  }
  zip::Index idx;
  zip::index(z, idx);
  Manifest m;
  if (!manifestOf(z, idx, file, m, error)) {
    r.problems.push_back(error);
    return r;
  }
  std::set<std::string> listed;
  for (const FileEntry& f : m.files) {
    listed.insert(f.path);
    auto it = idx.files.find(f.path);
    if (it == idx.files.end()) {
      r.problems.push_back(f.path + " is listed but missing");
      continue;
    }
    std::vector<uint8_t> bytes;
    if (!z.read(it->second.first, bytes, error)) {
      r.problems.push_back(f.path + " is damaged (" + error + ")");
      continue;
    }
    if (bytes.size() != f.size || vfs::crc32(bytes.data(), bytes.size()) != f.crc) {
      r.problems.push_back(f.path + " is not the file the pack was made with");
      continue;
    }
    ++r.files;
    r.bytes += bytes.size();
  }
  for (const auto& [name, f] : idx.files)
    if (name != kManifestName && !listed.count(name)) r.problems.push_back(name + " is not in the pack's list");
  if (!m.checksum.empty() && checksumOf(m.files) != m.checksum)
    r.problems.push_back("the checksum does not match the list of files");
  r.ok = r.problems.empty();
  return r;
}

bool extract(const std::string& file, const std::string& dirIn, std::string& error) {
  zip::Reader z;
  if (!z.open(file, error)) return false;
  zip::Index idx;
  zip::index(z, idx);
  const std::string dir = withSlash(dirIn);
  std::error_code ec;
  fs::create_directories(dir, ec);
  for (const auto& [name, f] : idx.files) {
    std::vector<uint8_t> bytes;
    if (!z.read(f.first, bytes, error)) {
      error = name + ": " + error;
      return false;
    }
    const fs::path target(dir + name);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
      error = "can't write " + target.generic_string();
      return false;
    }
  }
  return true;
}

}  // namespace pioneer::pack

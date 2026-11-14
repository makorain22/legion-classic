#include "pioneer/core/content.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>

namespace pioneer {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {
std::string withSlash(std::string d) {
  if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
  return d;
}

bool readJsonFile(const std::string& path, nlohmann::json& out, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "can't open " + path;
    return false;
  }
  out = nlohmann::json::parse(in, nullptr, false, true);  // comments allowed in mod files
  if (out.is_discarded()) {
    error = path + ": invalid JSON";
    return false;
  }
  return true;
}

// An array whose elements are objects carrying an "id": merged entry by entry.
bool isIdArray(const nlohmann::json& a) {
  if (!a.is_array() || a.empty()) return false;
  for (const auto& e : a)
    if (!e.is_object() || !e.contains("id") || !e["id"].is_string()) return false;
  return true;
}

bool removes(const nlohmann::json& e) { return e.is_object() && e.value("_remove", false); }
}  // namespace

// ---------------------------------------------------------------------------
// Mods on disk
// ---------------------------------------------------------------------------
namespace {
void readRequirements(const nlohmann::json& j, ModInfo& m) {
  if (j.contains("requires") && j["requires"].is_array())
    for (const auto& r : j["requires"]) {
      if (!r.is_string()) continue;
      pack::Requirement q;
      if (!pack::Requirement::parse(r.get<std::string>(), q)) {
        m.error = "can't read the requirement \"" + r.get<std::string>() + "\"";
        continue;
      }
      m.dependencies.push_back(q.id);
      m.requirements.push_back(std::move(q));
    }
  if (j.contains("engine") && j["engine"].is_string()) {
    pack::Manifest probe;
    probe.engine = j["engine"].get<std::string>();
    std::string why;
    if (!pack::engineAllows(probe, &why)) m.error = why;
  }
}

bool isPackFile(const fs::path& p) {
  std::string ext = p.extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == pack::kExtension || ext == ".zip";
}
}  // namespace

std::vector<ModInfo> scanMods(const std::vector<std::string>& modDirs, size_t firstUserDir) {
  std::vector<ModInfo> mods;
  for (size_t d = 0; d < modDirs.size(); ++d) {
    std::error_code ec;
    if (!fs::is_directory(modDirs[d], ec)) continue;
    // Packs first, folders second: of the two with one id, the folder (the copy
    // that can be edited) is the one that counts.
    std::vector<fs::directory_entry> packs, folders;
    for (const auto& entry : fs::directory_iterator(modDirs[d], ec)) {
      if (entry.is_directory(ec)) folders.push_back(entry);
      else if (entry.is_regular_file(ec) && isPackFile(entry.path())) packs.push_back(entry);
    }
    auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) { return a.path() < b.path(); };
    std::sort(packs.begin(), packs.end(), byName);
    std::sort(folders.begin(), folders.end(), byName);
    auto put = [&](ModInfo&& m) {
      auto same = std::find_if(mods.begin(), mods.end(), [&](const ModInfo& o) { return o.id == m.id; });
      if (same != mods.end()) *same = std::move(m);  // the player's own copy wins
      else mods.push_back(std::move(m));
    };
    for (const auto& entry : packs) {
      ModInfo m;
      m.packed = true;
      m.file = entry.path().generic_string();
      m.id = entry.path().stem().string();
      m.name = m.id;
      m.user = d >= firstUserDir;
      pack::Manifest pm;
      std::string err;
      if (!pack::readManifest(m.file, pm, err)) {
        m.error = err;
      } else {
        if (!pm.id.empty()) m.id = pm.id;
        m.name = pm.name.empty() ? m.id : pm.name;
        m.version = pm.version.empty() ? m.version : pm.version;
        m.author = pm.author;
        m.description = pm.description;
        m.checksum = pm.checksum;
        m.kinds = pm.kinds;
        for (const pack::Requirement& q : pm.requirements) {
          m.dependencies.push_back(q.id);
          m.requirements.push_back(q);
        }
        std::string why;
        if (!pack::engineAllows(pm, &why)) m.error = why;
      }
      put(std::move(m));
    }
    for (const auto& entry : folders) {
      ModInfo m;
      m.id = entry.path().filename().string();
      m.dir = withSlash(entry.path().generic_string());
      m.name = m.id;
      m.user = d >= firstUserDir;
      nlohmann::json j;
      std::string err;
      if (!fs::exists(m.dir + "mod.json", ec)) {
        m.error = "no mod.json";
      } else if (!readJsonFile(m.dir + "mod.json", j, err) || !j.is_object()) {
        m.error = err.empty() ? "mod.json is not an object" : err;
      } else {
        m.name = j.value("name", m.id);
        m.version = j.value("version", m.version);
        m.author = j.value("author", std::string());
        m.description = j.value("description", std::string());
        readRequirements(j, m);
      }
      put(std::move(m));
    }
  }
  std::sort(mods.begin(), mods.end(), [](const ModInfo& a, const ModInfo& b) { return a.name < b.name; });
  return mods;
}

const ModInfo* findMod(const std::vector<ModInfo>& mods, const std::string& id) {
  for (const ModInfo& m : mods)
    if (m.id == id) return &m;
  return nullptr;
}

bool writeModInfo(const ModInfo& m, std::string& error) {
  nlohmann::json j;
  j["name"] = m.name;
  j["version"] = m.version;
  j["author"] = m.author;
  j["description"] = m.description;
  j["requires"] = m.dependencies;
  std::ofstream out(m.dir + "mod.json");
  if (!out) {
    error = "can't write " + m.dir + "mod.json";
    return false;
  }
  out << j.dump(2) << "\n";
  return true;
}

std::string modIdFromName(const std::string& name) {
  std::string id;
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) id += static_cast<char>(std::tolower(u));
    else if (!id.empty() && id.back() != '_') id += '_';
  }
  while (!id.empty() && id.back() == '_') id.pop_back();
  if (id.size() > 40) id.resize(40);
  return id.empty() ? "my_mod" : id;
}

bool createMod(const std::string& modsDir, const std::string& id, const std::string& name, ModInfo& out,
               std::string& error) {
  if (id.empty() || id.find_first_of("/\\.:") != std::string::npos) {
    error = "a mod name needs letters or digits";
    return false;
  }
  out = ModInfo{};
  out.id = id;
  out.dir = withSlash(modsDir) + id + "/";
  out.name = name.empty() ? id : name;
  out.user = true;
  std::error_code ec;
  if (fs::exists(out.dir + "mod.json", ec)) {
    error = "a mod called '" + id + "' already exists";
    return false;
  }
  for (const char* sub : {"rules", "art", "scenarios"}) fs::create_directories(out.dir + sub, ec);
  if (ec) {
    error = "can't create " + out.dir + ": " + ec.message();
    return false;
  }
  return writeModInfo(out, error);
}

std::vector<std::string> resolveLoadOrder(const std::vector<std::string>& enabled, const std::vector<ModInfo>& mods,
                                          std::string& warnings) {
  std::vector<std::string> wanted;
  for (const std::string& id : enabled) {
    const ModInfo* m = findMod(mods, id);
    if (!m) {
      warnings += "Mod '" + id + "' is not installed.\n";
      continue;
    }
    if (!m->error.empty()) {
      warnings += "Mod '" + m->name + "' is broken: " + m->error + "\n";
      continue;
    }
    if (std::find(wanted.begin(), wanted.end(), id) == wanted.end()) wanted.push_back(id);
  }
  // Each mod after the mods it needs, otherwise in the player's order.
  std::vector<std::string> order;
  std::set<std::string> placed, visiting;
  std::function<bool(const std::string&)> place = [&](const std::string& id) -> bool {
    if (placed.count(id)) return true;
    if (visiting.count(id)) {
      warnings += "Mods '" + id + "' and its requirements need each other; load order kept as listed.\n";
      return false;
    }
    visiting.insert(id);
    const ModInfo* m = findMod(mods, id);
    for (const std::string& dep : m->dependencies) {
      if (std::find(wanted.begin(), wanted.end(), dep) == wanted.end()) {
        warnings += "Mod '" + m->name + "' needs '" + dep + "', which is not enabled.\n";
        continue;
      }
      // The right version of it?
      const ModInfo* have = findMod(mods, dep);
      for (const pack::Requirement& q : m->requirements) {
        pack::Version v;
        if (q.id != dep || q.op.empty() || !have || !pack::Version::parse(have->version, v) || q.allows(v)) continue;
        warnings += "Mod '" + m->name + "' needs '" + have->name + "' " + q.wording() + "; version " +
                    have->version + " is installed.\n";
      }
      place(dep);
    }
    visiting.erase(id);
    placed.insert(id);
    order.push_back(id);
    return true;
  };
  for (const std::string& id : wanted) place(id);
  return order;
}

// ---------------------------------------------------------------------------
// Merging rules
// ---------------------------------------------------------------------------
void mergeRules(nlohmann::json& base, const nlohmann::json& patch) {
  if (!patch.is_object() || !base.is_object()) {
    base = patch;
    return;
  }
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    const std::string& key = it.key();
    const nlohmann::json& p = it.value();
    if (key == "_comment" && base.contains(key)) continue;  // keep the base's field guide
    if (removes(p)) {
      base.erase(key);
      continue;
    }
    if (!base.contains(key)) {
      base[key] = p;
      continue;
    }
    nlohmann::json& b = base[key];
    if (b.is_array() && p.is_array() && (isIdArray(b) || b.empty()) && (isIdArray(p) || p.empty())) {
      for (const auto& entry : p) {
        const std::string id = entry["id"].get<std::string>();
        auto found = std::find_if(b.begin(), b.end(), [&](const nlohmann::json& e) { return e["id"] == id; });
        if (removes(entry)) {
          if (found != b.end()) b.erase(found);
        } else if (found != b.end()) {
          mergeRules(*found, entry);
        } else {
          b.push_back(entry);
        }
      }
    } else if (b.is_object() && p.is_object()) {
      mergeRules(b, p);
    } else {
      b = p;
    }
  }
}

nlohmann::json diffRules(const nlohmann::json& base, const nlohmann::json& target) {
  nlohmann::json patch = nlohmann::json::object();
  if (!base.is_object() || !target.is_object()) return target;
  for (auto it = target.begin(); it != target.end(); ++it) {
    const std::string& key = it.key();
    if (key == "_comment") continue;
    const nlohmann::json& t = it.value();
    if (!base.contains(key)) {
      patch[key] = t;
      continue;
    }
    const nlohmann::json& b = base[key];
    if (b == t) continue;
    if (isIdArray(b) && (isIdArray(t) || t.empty())) {
      nlohmann::json entries = nlohmann::json::array();
      for (const auto& te : t) {
        auto found = std::find_if(b.begin(), b.end(), [&](const nlohmann::json& e) { return e["id"] == te["id"]; });
        if (found == b.end()) {
          entries.push_back(te);
        } else if (*found != te) {
          nlohmann::json d = diffRules(*found, te);
          d["id"] = te["id"];
          entries.push_back(d);
        }
      }
      for (const auto& be : b) {
        const bool kept = std::any_of(t.begin(), t.end(), [&](const nlohmann::json& e) { return e["id"] == be["id"]; });
        if (!kept) entries.push_back({{"id", be["id"]}, {"_remove", true}});
      }
      if (!entries.empty()) patch[key] = entries;
    } else if (b.is_object() && t.is_object()) {
      nlohmann::json d = diffRules(b, t);
      if (!d.empty()) patch[key] = d;
    } else {
      patch[key] = t;
    }
  }
  for (auto it = base.begin(); it != base.end(); ++it)
    if (it.key() != "_comment" && !target.contains(it.key())) patch[it.key()] = {{"_remove", true}};
  return patch;
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------
ContentLayers::ContentLayers() : fs_(std::make_shared<vfs::Vfs>()) {}

void ContentLayers::clear() {
  const std::string cache = fs_->cacheDir();
  fs_ = std::make_shared<vfs::Vfs>();
  fs_->setCacheDir(cache);
  roots_.clear();
  modIds_.clear();
  mounts_.clear();
}

void ContentLayers::setCacheDir(const std::string& dir) { fs_->setCacheDir(dir); }

void ContentLayers::addRoot(const std::string& dir, const std::string& modId) {
  roots_.push_back(withSlash(dir));
  modIds_.push_back(modId);
  mounts_.push_back(fs_->mountDir(roots_.back(), modId.empty() ? "base" : modId, static_cast<int>(roots_.size())));
}

bool ContentLayers::addPack(const std::string& file, const std::string& modId, std::string& error) {
  const int mount = fs_->mountZip(file, modId, static_cast<int>(roots_.size()) + 1, std::string(), error);
  if (mount < 0) return false;
  roots_.push_back(file);
  modIds_.push_back(modId);
  mounts_.push_back(mount);
  return true;
}

bool ContentLayers::addMod(const ModInfo& mod, std::string& error) {
  if (mod.packed) return addPack(mod.file, mod.id, error);
  addRoot(mod.dir, mod.id);
  return true;
}

std::vector<std::string> ContentLayers::all(const std::string& rel) const {
  std::vector<std::string> out;
  for (int mount : mounts_) {
    std::string p = fs_->diskPathIn(mount, rel);
    if (!p.empty()) out.push_back(std::move(p));
  }
  return out;
}

bool ContentLayers::loadRules(const std::string& file, nlohmann::json& out, std::string& error) const {
  return loadRulesBelow(file, std::string(), out, error);
}

bool ContentLayers::loadRulesBelow(const std::string& file, const std::string& stopAt, nlohmann::json& out,
                                   std::string& error) const {
  out = nlohmann::json::object();
  bool any = false;
  const std::string rel = "rules/" + file;
  for (size_t i = 0; i < mounts_.size(); ++i) {
    if (!stopAt.empty() && modIds_[i] == stopAt) break;
    vfs::FileInfo fi;
    if (!fs_->statIn(mounts_[i], rel, fi)) continue;
    const std::string where = fs_->describe(mounts_[i], rel);
    std::vector<uint8_t> bytes;
    if (!fs_->readIn(mounts_[i], rel, bytes, &error)) return false;
    nlohmann::json doc = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false, true);  // comments allowed
    if (doc.is_discarded()) {
      error = where + ": invalid JSON";
      return false;
    }
    if (!doc.is_object()) {
      error = where + ": expected a JSON object";
      return false;
    }
    if (!any) out = std::move(doc);
    else mergeRules(out, doc);
    any = true;
  }
  if (!any) {
    error = "rules/" + file + " is missing";
    return false;
  }
  return true;
}

std::string rulesHash(const nlohmann::json (&docs)[kRuleFileCount]) {
  uint64_t h = 1469598103934665603ull;
  for (const nlohmann::json& d : docs) {
    nlohmann::json copy = d;
    if (copy.is_object()) copy.erase("_comment");  // comments don't change the game
    for (unsigned char c : copy.dump()) {
      h ^= c;
      h *= 1099511628211ull;
    }
  }
  char buf[17];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
  return buf;
}

// ---------------------------------------------------------------------------
// Changing rules in a running battle
// ---------------------------------------------------------------------------
namespace {
// Orders `next` like `current` (one list, possibly spread over several files).
// Returns the stabilised entries; deleted ones come back hidden.
json stabiliseList(const json& current, const json& next, std::string& notes) {
  json ordered = json::array();
  std::vector<bool> used(next.size(), false);
  for (const auto& c : current) {
    const std::string id = c["id"].get<std::string>();
    size_t at = next.size();
    for (size_t i = 0; i < next.size() && at == next.size(); ++i)
      if (!used[i] && next[i].is_object() && next[i].value("id", std::string()) == id) at = i;
    if (at == next.size()) {
      // Deleted: kept for what is already on the map, but nobody can build it.
      json kept = c;
      kept["hidden"] = true;
      kept["aiWeight"] = 0;
      if (!c.value("hidden", false)) notes += "'" + id + "' stays in this battle (deleted from the next).\n";
      ordered.push_back(std::move(kept));
      continue;
    }
    used[at] = true;
    json e = next[at];
    for (const char* fixed : {"footprint", "blocks"}) {
      const bool had = c.contains(fixed), has = e.contains(fixed);
      if (had != has || (had && c[fixed] != e[fixed])) {
        if (had) e[fixed] = c[fixed];
        else e.erase(fixed);
        notes += "'" + id + "': " + fixed + " changes take effect in the next battle.\n";
      }
    }
    ordered.push_back(std::move(e));
  }
  for (size_t i = 0; i < next.size(); ++i)
    if (!used[i]) ordered.push_back(next[i]);
  return ordered;
}

json listOf(const json& doc, const char* key) {
  return doc.is_object() && doc.contains(key) && doc[key].is_array() ? doc[key] : json::array();
}
}  // namespace

void stabiliseRules(json (&next)[kRuleFileCount], const json (&current)[kRuleFileCount], std::string& notes) {
  // Weapons: one list in one file. Warheads are named, so a deleted one is
  // simply kept for the weapons still using it.
  next[0]["weapons"] = stabiliseList(listOf(current[0], "weapons"), listOf(next[0], "weapons"), notes);
  if (current[0].contains("warheads") && current[0]["warheads"].is_object()) {
    if (!next[0].contains("warheads") || !next[0]["warheads"].is_object()) next[0]["warheads"] = json::object();
    for (auto w = current[0]["warheads"].begin(); w != current[0]["warheads"].end(); ++w)
      if (!next[0]["warheads"].contains(w.key())) next[0]["warheads"][w.key()] = w.value();
  }
  // Units and buildings are one list of types, numbered across both files:
  // the battle's types keep their files, new ones go at the very end.
  json cur = listOf(current[1], "units"), nxt = listOf(next[1], "units");
  const size_t firstFile = cur.size();
  for (const auto& e : listOf(current[2], "units")) cur.push_back(e);
  for (const auto& e : listOf(next[2], "units")) nxt.push_back(e);
  const json ordered = stabiliseList(cur, nxt, notes);
  next[1]["units"] = json(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(firstFile));
  next[2]["units"] = json(ordered.begin() + static_cast<std::ptrdiff_t>(firstFile), ordered.end());
}

}  // namespace pioneer

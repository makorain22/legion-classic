// pioneer_tool - the Pioneer Engine's content tool, from the command line.
//
//   pioneer_tool validate   check the content (the engine's checks and the game's)
//   pioneer_tool cook       make the content ready to ship: a folder and/or a pack
//   pioneer_tool checks     list the checks
//   pioneer_tool info       what one file is, where it comes from, what it uses
//   pioneer_tool ls         the merged content tree
//   pioneer_tool deps       what a file uses, or what uses it
//   pioneer_tool index      write the asset index of the content
//   pioneer_tool diff       what changed between two content folders, packs or indexes
//   pioneer_tool pack       build, verify, look into or unpack a pack
//   pioneer_tool model      look into a model, or convert it (.gltf/.glb/.vox -> .glb)
//   pioneer_tool bank       the sound events the banks give
//
// The content is given as the game layers it: --content DIR (the base),
// --mods DIR (where mods live, any number), --mod ID (enable one, any number;
// the load order follows what they require). --content can also be a pack,
// which is then the whole content. Exit code: 0 fine, 1 errors found or the job failed,
// 2 the command line was wrong.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "content_checks.hpp"
#include "pioneer/assets/gltf.hpp"
#include "pioneer/assets/vox_file.hpp"
#include "pioneer/audio/bank.hpp"
#include "pioneer/core/assets.hpp"
#include "pioneer/core/content.hpp"
#include "pioneer/core/jobs.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/core/pack.hpp"
#include "pioneer/core/version.hpp"
#include "pioneer/tools/cook.hpp"
#include "pioneer/tools/validate.hpp"

using namespace pioneer;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

void usage() {
  std::printf(
      "pioneer_tool - %s %s content tool\n"
      "\n"
      "  validate [content] [--only a,b] [--skip a,b] [--ignore text] [--notes] [--json FILE|-] [--werror] [--quiet]\n"
      "                    check the content; --all-mods checks the base alone and with each mod\n"
      "  cook [content] --out DIR [--pack FILE] [--pack-id ID] [--pack-name NAME] [--pack-version V]\n"
      "       [--no-minify] [--clean] [--full] [--exclude text] [--force]\n"
      "                    cook into a folder (and a pack); validates first, errors stop it unless --force\n"
      "  checks            list the checks\n"
      "  info PATH [content]           what a file is, where it comes from, what it uses and what uses it\n"
      "  ls [FOLDER] [--type T] [content]\n"
      "  deps PATH [--users] [--all] [content]\n"
      "  index [content] [--out FILE]  the asset index (to compare releases with diff)\n"
      "  diff A B          A and B: content folders, packs or asset_index.json files\n"
      "  pack build DIR FILE [--id ID] [--name NAME] [--version V] | verify FILE | info FILE | extract FILE DIR\n"
      "  model info FILE | model convert IN OUT.glb\n"
      "  bank list [content] | bank show EVENT [content]\n"
      "  version\n"
      "\n"
      "content: --content DIR|PACK (default: the content folder beside this program; a pack is taken alone),\n"
      "         --mods DIR (any number; default: the mods folder beside the content), --mod ID (any number)\n",
      kEngineName, kEngineVersion);
}

std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= s.size()) {
    const size_t comma = s.find(',', at);
    const std::string part = s.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (!part.empty()) out.push_back(part);
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  return out;
}

std::string withSlash(std::string d) {
  if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
  return d;
}

// The command line after the command's name: options and the words between them.
struct Args {
  std::vector<std::string> words;
  std::map<std::string, std::vector<std::string>> opts;  // "--mod" -> every value given
  std::set<std::string> flags;
  bool has(const std::string& f) const { return flags.count(f) > 0; }
  std::string opt(const std::string& name, const std::string& fallback = {}) const {
    auto it = opts.find(name);
    return it == opts.end() || it->second.empty() ? fallback : it->second.back();
  }
  std::vector<std::string> all(const std::string& name) const {
    auto it = opts.find(name);
    return it == opts.end() ? std::vector<std::string>{} : it->second;
  }
};

const std::set<std::string>& valueOptions() {
  static const std::set<std::string> v = {"--content", "--mods",   "--mod",      "--pack",       "--only",      "--skip",
                                          "--ignore",  "--json",   "--out",      "--pack-id",    "--pack-name", "--pack-version",
                                          "--exclude", "--type",   "--id",       "--name",       "--version"};
  return v;
}

bool parseArgs(int argc, char** argv, int from, Args& out) {
  for (int i = from; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.size() > 2 && a.compare(0, 2, "--") == 0) {
      if (valueOptions().count(a)) {
        if (i + 1 >= argc) {
          std::fprintf(stderr, "%s needs a value\n", a.c_str());
          return false;
        }
        out.opts[a].push_back(argv[++i]);
      } else {
        out.flags.insert(a);
      }
    } else {
      out.words.push_back(a);
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// The content, layered as the game layers it.

struct Content {
  ContentLayers layers;
  assets::Registry registry;
  std::string description;  // "content/ + veterans"
  std::shared_ptr<vfs::Vfs> fs() const { return layers.fileSystem(); }
};

std::string programDir;

std::string cacheDir() {
  std::error_code ec;
  const fs::path p = fs::temp_directory_path(ec) / "pioneer_tool_cache";
  fs::create_directories(p, ec);
  return withSlash(p.generic_string());
}

std::string defaultContentDir() {
  for (const fs::path& p : {fs::path(programDir) / "content", fs::path(programDir) / ".." / "content", fs::path("content")}) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) return withSlash(p.generic_string());
  }
  return {};
}

// Builds the layers: the base (or a pack), then the mods enabled, in load order.
bool openContent(const Args& a, const std::vector<std::string>& enabled, Content& c, std::string& error) {
  c.layers.clear();
  c.layers.setCacheDir(cacheDir());
  std::error_code fec;
  const std::string given = a.opt("--content");
  if (!given.empty() && fs::is_regular_file(given, fec)) {  // a pack as the whole content
    if (!c.layers.addPack(given, "", error)) return false;
    c.description = given;
  } else {
    std::string dir = withSlash(given);
    if (dir.empty()) dir = defaultContentDir();
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) {
      error = dir.empty() ? "the content folder wasn't found: give it with --content" : dir + " is not a folder";
      return false;
    }
    c.layers.addRoot(dir);
    c.description = dir;
    std::vector<std::string> modDirs;
    for (const std::string& m : a.all("--mods")) modDirs.push_back(withSlash(m));
    if (modDirs.empty() && !enabled.empty()) modDirs.push_back(withSlash((fs::path(dir) / ".." / "mods").lexically_normal().generic_string()));
    if (!enabled.empty()) {
      const std::vector<ModInfo> mods = scanMods(modDirs, modDirs.size());
      std::string warnings;
      for (const std::string& id : enabled)
        if (!findMod(mods, id)) {
          error = "no mod called '" + id + "' in " + (modDirs.empty() ? std::string("the mods folders") : modDirs.front());
          return false;
        }
      const std::vector<std::string> order = resolveLoadOrder(enabled, mods, warnings);
      if (!warnings.empty()) std::fprintf(stderr, "%s", warnings.c_str());
      for (const std::string& id : order) {
        if (!c.layers.addMod(*findMod(mods, id), error)) return false;
        c.description += " + " + id;
      }
    }
  }
  c.registry.attach(c.fs());
  c.registry.rebuild();
  return true;
}

bool openContent(const Args& a, Content& c, std::string& error) { return openContent(a, a.all("--mod"), c, error); }

tools::Validator validator() {
  tools::Validator v;
  tools::addEngineChecks(v);
  legion::addContentChecks(v);
  return v;
}

tools::Options checkOptions(const Args& a) {
  tools::Options o;
  for (const std::string& s : a.all("--only"))
    for (const std::string& n : splitList(s)) o.only.push_back(n);
  for (const std::string& s : a.all("--skip"))
    for (const std::string& n : splitList(s)) o.skip.push_back(n);
  o.ignore = a.all("--ignore");
  return o;
}

bool knownChecks(const tools::Validator& v, const tools::Options& o) {
  for (const auto* list : {&o.only, &o.skip})
    for (const std::string& n : *list)
      if (!v.find(n)) {
        std::fprintf(stderr, "There is no check called '%s' (pioneer_tool checks lists them)\n", n.c_str());
        return false;
      }
  return true;
}

// ---------------------------------------------------------------------------
// Commands

int cmdChecks() {
  const tools::Validator v = validator();
  for (const tools::Check& c : v.checks()) std::printf("  %-11s %s\n", c.name.c_str(), c.help.c_str());
  return 0;
}

int cmdValidate(const Args& a) {
  const tools::Validator v = validator();
  const tools::Options o = checkOptions(a);
  if (!knownChecks(v, o)) return 2;
  // Which combinations: as given, or (--all-mods) the base alone and the base with each mod.
  std::vector<std::vector<std::string>> runs = {a.all("--mod")};
  std::error_code fec;
  if (a.has("--all-mods") && !fs::is_regular_file(a.opt("--content"), fec)) {
    std::vector<std::string> modDirs;
    for (const std::string& m : a.all("--mods")) modDirs.push_back(withSlash(m));
    if (modDirs.empty()) {
      const std::string base = a.opt("--content").empty() ? defaultContentDir() : withSlash(a.opt("--content"));
      modDirs.push_back(withSlash((fs::path(base) / ".." / "mods").lexically_normal().generic_string()));
    }
    for (const ModInfo& m : scanMods(modDirs, modDirs.size())) runs.push_back({m.id});
  }
  const bool quiet = a.has("--quiet"), notes = a.has("--notes"), werror = a.has("--werror");
  json all = json::array();
  bool failed = false;
  for (const auto& enabled : runs) {
    Content c;
    std::string err;
    if (!openContent(a, enabled, c, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    const tools::Report r = v.run(*c.fs(), c.registry, o);
    if (!quiet) {
      std::printf("== %s\n", c.description.c_str());
      std::fputs(r.text(notes).c_str(), stdout);
    }
    if (!r.ok() || (werror && r.warnings() > 0)) failed = true;
    json j = r.json();
    j["content"] = c.description;
    all.push_back(std::move(j));
  }
  const std::string jsonOut = a.opt("--json");
  if (!jsonOut.empty()) {
    const std::string text = (all.size() == 1 ? all[0] : all).dump(1) + "\n";
    if (jsonOut == "-") {
      std::fputs(text.c_str(), stdout);
    } else {
      std::ofstream out(jsonOut, std::ios::binary);
      out << text;
      if (!out) {
        std::fprintf(stderr, "can't write %s\n", jsonOut.c_str());
        return 1;
      }
    }
  }
  return failed ? 1 : 0;
}

int cmdCook(const Args& a) {
  const std::string out = a.opt("--out");
  if (out.empty()) {
    std::fprintf(stderr, "cook needs --out DIR\n");
    return 2;
  }
  Content c;
  std::string err;
  if (!openContent(a, c, err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  if (!a.has("--no-validate")) {
    const tools::Validator v = validator();
    const tools::Report r = v.run(*c.fs(), c.registry, checkOptions(a));
    if (!r.ok()) {
      std::fputs(r.text(false).c_str(), stdout);
      if (!a.has("--force")) {
        std::fprintf(stderr, "Not cooked: the content has errors (--force cooks it anyway)\n");
        return 1;
      }
    } else {
      std::printf("validated: %zu warnings, %zu notes\n", r.warnings(), r.notes());
    }
  }
  tools::CookOptions o;
  o.minifyJson = !a.has("--no-minify");
  o.clean = a.has("--clean");
  o.incremental = !a.has("--full");
  o.exclude = a.all("--exclude");
  o.packFile = a.opt("--pack");
  o.manifest.id = a.opt("--pack-id");
  o.manifest.name = a.opt("--pack-name");
  o.manifest.version = a.opt("--pack-version");
  tools::CookResult r;
  const bool ok = tools::cook(*c.fs(), c.registry, out, o, r);
  for (const std::string& e : r.errors) std::fprintf(stderr, "%s\n", e.c_str());
  std::printf("cooked %s into %s: %zu files (%zu written, %zu unchanged, %zu removed, %zu JSON minified), "
              "%.1f MB -> %.1f MB, content %s, %.0f ms\n",
              c.description.c_str(), out.c_str(), r.files, r.written, r.reused, r.removed, r.minified,
              static_cast<double>(r.bytesIn) / 1048576.0, static_cast<double>(r.bytesOut) / 1048576.0, r.contentHash.c_str(), r.ms);
  if (ok && !o.packFile.empty()) {
    std::error_code ec;
    std::printf("packed %s (%.1f MB)\n", o.packFile.c_str(), static_cast<double>(fs::file_size(o.packFile, ec)) / 1048576.0);
  }
  return ok ? 0 : 1;
}

void printRecord(assets::Registry& reg, const assets::Record& r) {
  std::printf("%s\n  kind     %s\n", r.path.c_str(), assets::typeName(r.type));
  if (!r.present) {
    std::printf("  missing  named by something, but no layer has it\n");
  } else {
    vfs::FileInfo fi;
    reg.fileSystem()->stat(r.path, fi);
    std::printf("  from     %s%s\n", reg.fileSystem()->describe(fi.mount, r.path).c_str(),
                r.layers > 1 ? (" (over " + std::to_string(r.layers - 1) + " lower layer(s))").c_str() : "");
    std::printf("  size     %llu bytes\n  hash     %s\n", static_cast<unsigned long long>(r.size),
                vfs::hex64(reg.contentHash(r.id)).c_str());
  }
  std::printf("  uses     %zu\n", r.uses.size());
  for (assets::Id u : r.uses)
    if (const assets::Record* d = reg.get(u)) std::printf("    %s%s\n", d->path.c_str(), d->present ? "" : "  (missing)");
  std::printf("  used by  %zu\n", r.usedBy.size());
  for (assets::Id u : r.usedBy)
    if (const assets::Record* d = reg.get(u)) std::printf("    %s\n", d->path.c_str());
}

int cmdInfo(const Args& a) {
  if (a.words.empty()) {
    std::fprintf(stderr, "info PATH\n");
    return 2;
  }
  Content c;
  std::string err;
  if (!openContent(a, c, err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  c.registry.scanDependencies();
  const tools::Report rep = validator().run(*c.fs(), c.registry, checkOptions(a));
  int status = 0;
  for (const std::string& p : a.words) {
    const assets::Record* r = c.registry.get(p);
    if (!r) {
      std::fprintf(stderr, "%s is not in the content\n", p.c_str());
      status = 1;
      continue;
    }
    printRecord(c.registry, *r);
    for (const tools::Issue* i : rep.of(p))
      std::printf("  %-8s %s%s\n", tools::severityName(i->severity), i->where.empty() ? "" : (i->where + ": ").c_str(), i->text.c_str());
  }
  return status;
}

int cmdLs(const Args& a) {
  Content c;
  std::string err;
  if (!openContent(a, c, err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  const std::string folder = a.words.empty() ? std::string() : a.words[0];
  assets::Type want = assets::Type::Unknown;
  const std::string type = a.opt("--type");
  if (!type.empty() && !assets::typeFromName(type, want)) {
    std::fprintf(stderr, "no kind called '%s'\n", type.c_str());
    return 2;
  }
  size_t n = 0;
  uint64_t bytes = 0;
  for (const vfs::FileInfo& f : c.fs()->list(folder, true)) {
    const assets::Record* r = c.registry.get(f.path);
    if (!type.empty() && (!r || r->type != want)) continue;
    vfs::MountInfo mi;
    c.fs()->mountInfo(f.mount, mi);
    std::printf("%10llu  %-9s %-10s %s\n", static_cast<unsigned long long>(f.size), r ? assets::typeName(r->type) : "?",
                mi.name.c_str(), f.path.c_str());
    ++n;
    bytes += f.size;
  }
  std::printf("%zu files, %.1f MB\n", n, static_cast<double>(bytes) / 1048576.0);
  return 0;
}

int cmdDeps(const Args& a) {
  if (a.words.empty()) {
    std::fprintf(stderr, "deps PATH\n");
    return 2;
  }
  Content c;
  std::string err;
  if (!openContent(a, c, err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  c.registry.scanDependencies();
  const assets::Id id = c.registry.find(a.words[0]);
  if (id == assets::kNone) {
    std::fprintf(stderr, "%s is not in the content\n", a.words[0].c_str());
    return 1;
  }
  const bool users = a.has("--users"), recursive = a.has("--all");
  const auto list = users ? c.registry.usersOf(id, recursive) : c.registry.dependenciesOf(id, recursive);
  for (assets::Id d : list)
    if (const assets::Record* r = c.registry.get(d)) std::printf("%s%s\n", r->path.c_str(), r->present ? "" : "  (missing)");
  return 0;
}

// An index from a content folder, a pack or an index file.
bool indexFrom(const std::string& what, tools::AssetIndex& out, std::string& error) {
  std::error_code ec;
  if (fs::is_regular_file(what, ec) && fs::path(what).extension() == ".json") return tools::readIndex(what, out, error);
  Args a;
  if (fs::is_directory(what, ec) || fs::is_regular_file(what, ec)) a.opts["--content"].push_back(what);
  else {
    error = what + " is not there";
    return false;
  }
  Content c;
  if (!openContent(a, {}, c, error)) return false;
  c.registry.scanDependencies();
  out = tools::indexOf(c.registry);
  return true;
}

int cmdIndex(const Args& a) {
  Content c;
  std::string err;
  if (!openContent(a, c, err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  c.registry.scanDependencies();
  const std::string text = tools::toJson(tools::indexOf(c.registry)).dump(1) + "\n";
  const std::string out = a.opt("--out");
  if (out.empty()) {
    std::fputs(text.c_str(), stdout);
    return 0;
  }
  std::ofstream f(out, std::ios::binary);
  f << text;
  return f ? 0 : 1;
}

int cmdDiff(const Args& a) {
  if (a.words.size() != 2) {
    std::fprintf(stderr, "diff A B\n");
    return 2;
  }
  tools::AssetIndex x, y;
  std::string err;
  if (!indexFrom(a.words[0], x, err) || !indexFrom(a.words[1], y, err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  const tools::IndexDiff d = tools::diff(x, y);
  for (const std::string& p : d.added) std::printf("+ %s\n", p.c_str());
  for (const std::string& p : d.removed) std::printf("- %s\n", p.c_str());
  for (const std::string& p : d.changed) std::printf("~ %s\n", p.c_str());
  std::printf("%zu added, %zu removed, %zu changed (content %s -> %s)\n", d.added.size(), d.removed.size(),
              d.changed.size(), x.contentHash.c_str(), y.contentHash.c_str());
  return 0;
}

int cmdPack(const Args& a) {
  const std::string sub = a.words.empty() ? std::string() : a.words[0];
  std::string err;
  if (sub == "build" && a.words.size() == 3) {
    pack::Manifest m;
    m.id = a.opt("--id");
    m.name = a.opt("--name");
    m.version = a.opt("--version");
    if (!pack::build(withSlash(a.words[1]), a.words[2], m, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::printf("%s: %s %s, %zu files, checksum %s\n", a.words[2].c_str(), m.id.c_str(), m.version.c_str(), m.files.size(),
                m.checksum.c_str());
    return 0;
  }
  if (sub == "verify" && a.words.size() == 2) {
    const pack::Report r = pack::verify(a.words[1]);
    for (const std::string& p : r.problems) std::printf("%s\n", p.c_str());
    std::printf("%s: %s, %zu files, %.1f MB\n", a.words[1].c_str(), r.ok ? "fine" : "DAMAGED", r.files,
                static_cast<double>(r.bytes) / 1048576.0);
    return r.ok ? 0 : 1;
  }
  if (sub == "info" && a.words.size() == 2) {
    pack::Manifest m;
    if (!pack::readManifest(a.words[1], m, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::fputs((pack::toJson(m).dump(1) + "\n").c_str(), stdout);
    return 0;
  }
  if (sub == "extract" && a.words.size() == 3) {
    if (!pack::extract(a.words[1], a.words[2], err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    return 0;
  }
  std::fprintf(stderr, "pack build DIR FILE | verify FILE | info FILE | extract FILE DIR\n");
  return 2;
}

std::string lowerExt(const std::string& p) {
  std::string e = fs::path(p).extension().string();
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return e;
}

bool loadModel(const std::string& path, ModelAsset& m, std::string& err) {
  if (lowerExt(path) != ".vox") return readGltf(path, m, err);
  VoxFile vf;
  if (!readVoxFile(path, vf, err)) return false;
  if (vf.models.empty()) {
    err = path + " holds no models";
    return false;
  }
  const std::string name = fs::path(path).stem().string();
  m = voxelModelAsset(vf.models[0], vf.palette, 1.0f / 16.0f, vf.models[0].sizeX() * 0.5f, vf.models[0].sizeY() * 0.5f, name);
  return true;
}

int cmdModel(const Args& a) {
  const std::string sub = a.words.empty() ? std::string() : a.words[0];
  std::string err;
  if (sub == "info" && a.words.size() == 2) {
    ModelAsset m;
    if (!loadModel(a.words[1], m, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    float lo[3], hi[3];
    m.bounds(lo, hi);
    std::printf("%zu meshes, %zu triangles, %zu materials\n", m.meshes.size(), m.triangleCount(), m.materials.size());
    if (!m.meshes.empty())
      std::printf("size %.2f x %.2f x %.2f\n", static_cast<double>(hi[0] - lo[0]), static_cast<double>(hi[1] - lo[1]),
                  static_cast<double>(hi[2] - lo[2]));
    for (const MeshPart& p : m.meshes)
      std::printf("mesh %s: %zu vertices, %zu triangles%s\n", p.name.c_str(), p.vertices.size(), p.indices.size() / 3,
                  p.skinned ? ", skinned" : "");
    for (const Material& mat : m.materials) std::printf("material %s%s\n", mat.name.c_str(), mat.team ? " (team colour)" : "");
    if (m.skeleton.size()) std::printf("skeleton: %zu joints\n", m.skeleton.size());
    for (const AnimationClip& c : m.clips)
      std::printf("animation %s: %.2f s, %zu channels%s\n", c.name.c_str(), static_cast<double>(c.duration), c.channels.size(),
                  c.loop ? ", loops" : "");
    return 0;
  }
  if (sub == "convert" && a.words.size() == 3) {
    if (lowerExt(a.words[2]) != ".glb") {
      std::fprintf(stderr, "models are converted to .glb\n");
      return 2;
    }
    ModelAsset m;
    if (!loadModel(a.words[1], m, err) || !writeGlbFile(a.words[2], m, err)) {
      std::fprintf(stderr, "%s\n", err.c_str());
      return 1;
    }
    std::printf("wrote %s (%zu triangles)\n", a.words[2].c_str(), m.triangleCount());
    return 0;
  }
  std::fprintf(stderr, "model info FILE | model convert IN OUT.glb\n");
  return 2;
}

int cmdBank(const Args& a) {
  const std::string sub = a.words.empty() ? std::string() : a.words[0];
  if (sub != "list" && !(sub == "show" && a.words.size() == 2)) {
    std::fprintf(stderr, "bank list | bank show EVENT\n");
    return 2;
  }
  Content c;
  std::string err;
  if (!openContent(a, c, err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  audio::SoundBank bank;
  std::vector<std::string> files = {"audio/audio.json"};
  for (const vfs::FileInfo& f : c.fs()->list("audio/banks", false, ".json")) files.push_back(f.path);
  for (const vfs::MountInfo& mi : c.fs()->mounts())
    for (const std::string& p : files) {
      std::vector<uint8_t> bytes;
      if (!c.fs()->readIn(mi.id, p, bytes)) continue;
      const json j = json::parse(bytes.begin(), bytes.end(), nullptr, false, true);
      if (!j.is_discarded()) bank.add(j, c.fs()->describe(mi.id, p), err);
    }
  std::vector<const audio::SoundEvent*> events;
  for (const auto& [id, e] : bank.events()) events.push_back(&e);
  std::sort(events.begin(), events.end(), [](auto* x, auto* y) { return x->id < y->id; });
  if (sub == "list") {
    for (const audio::SoundEvent* e : events)
      std::printf("%-22s %-8s %2zu file(s)%s  vol %.2f  range %.0f  %s\n", e->id.c_str(), audio::busName(e->bus), e->files.size(),
                  e->farFiles.empty() ? "" : (" +" + std::to_string(e->farFiles.size()) + " far").c_str(),
                  static_cast<double>(e->volume), static_cast<double>(e->range), e->source.c_str());
    std::printf("%zu sound events\n", events.size());
    return 0;
  }
  const audio::SoundEvent* e = bank.find(a.words[1]);
  if (!e) {
    std::fprintf(stderr, "no sound event called '%s'\n", a.words[1].c_str());
    return 1;
  }
  std::printf("%s (from %s)\n  bus %s, volume %.2f, range %.1f, priority %d, reverb %.2f, pitch %.2f\n"
              "  at most %d a second, %d at once%s\n",
              e->id.c_str(), e->source.c_str(), audio::busName(e->bus), static_cast<double>(e->volume),
              static_cast<double>(e->range), e->priority, static_cast<double>(e->reverb), static_cast<double>(e->pitch),
              e->perSecond, e->limit, e->loop ? ", loops" : "");
  for (const std::string& f : e->files) std::printf("  file %s\n", f.c_str());
  for (const std::string& f : e->farFiles) std::printf("  far  %s (below %.2f)\n", f.c_str(), static_cast<double>(e->farBelow));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  programDir = fs::absolute(fs::path(argv[0])).parent_path().generic_string();
  log::setChannelLevel(log::Channel::Assets, log::Level::Warn);
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string cmd = argv[1];
  if (cmd == "help" || cmd == "--help" || cmd == "-h") {
    usage();
    return 0;
  }
  if (cmd == "version" || cmd == "--version") {
    std::printf("pioneer_tool %s %s\n", kEngineName, kEngineVersion);
    return 0;
  }
  Args a;
  if (!parseArgs(argc, argv, 2, a)) return 2;
  jobs::system().start(0);
  if (cmd == "validate") return cmdValidate(a);
  if (cmd == "cook") return cmdCook(a);
  if (cmd == "checks") return cmdChecks();
  if (cmd == "info") return cmdInfo(a);
  if (cmd == "ls") return cmdLs(a);
  if (cmd == "deps") return cmdDeps(a);
  if (cmd == "index") return cmdIndex(a);
  if (cmd == "diff") return cmdDiff(a);
  if (cmd == "pack") return cmdPack(a);
  if (cmd == "model") return cmdModel(a);
  if (cmd == "bank") return cmdBank(a);
  std::fprintf(stderr, "Unknown command '%s'\n\n", cmd.c_str());
  usage();
  return 2;
}

// Pioneer Engine - console commands for the data systems (see data_commands.hpp).
#include "pioneer/core/data_commands.hpp"

#include <algorithm>
#include <filesystem>

#include "pioneer/core/memory.hpp"
#include "pioneer/core/pack.hpp"

namespace pioneer {

namespace {

std::string plural(size_t n, const char* one, const char* many) {
  return std::to_string(n) + " " + (n == 1 ? one : many);
}

}  // namespace

void registerDataCommands(Console& con, DataHooks hooks) {
  auto fsOf = [hooks](Console& c) -> std::shared_ptr<vfs::Vfs> {
    std::shared_ptr<vfs::Vfs> fs = hooks.fileSystem ? hooks.fileSystem() : nullptr;
    if (!fs) c.error("No content is loaded yet.");
    return fs;
  };
  auto regOf = [hooks](Console& c) -> assets::Registry* {
    assets::Registry* r = hooks.registry ? hooks.registry() : nullptr;
    if (!r) c.error("The asset registry is not running.");
    return r;
  };
  auto resolve = [hooks](const std::string& typed, bool output) {
    return hooks.resolvePath ? hooks.resolvePath(typed, output) : typed;
  };
  // Tab completion over virtual paths.
  auto pathCompleter = [hooks](const ConsoleArgs&, const std::string& partial) {
    std::vector<std::string> out;
    std::shared_ptr<vfs::Vfs> fs = hooks.fileSystem ? hooks.fileSystem() : nullptr;
    if (!fs) return out;
    const size_t slash = partial.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string() : partial.substr(0, slash);
    const std::string prefix = folder.empty() ? std::string() : folder + "/";
    for (const std::string& d : fs->listDirs(folder))
      if ((prefix + d).compare(0, partial.size(), partial) == 0) out.push_back(prefix + d + "/");
    for (const vfs::FileInfo& f : fs->list(folder, false))
      if (f.path.compare(0, partial.size(), partial) == 0) out.push_back(f.path);
    return out;
  };

  con.addCommand("vfs.mounts", "vfs.mounts - the folders and packs the game reads from, lowest layer first.",
                 [fsOf](const ConsoleArgs&, Console& c) {
                   auto fs = fsOf(c);
                   if (!fs) return;
                   for (const vfs::MountInfo& m : fs->mounts()) {
                     std::string what = m.memory ? "memory" : m.packed ? "pack  " : "folder";
                     std::string extra;
                     if (m.packed || m.memory)
                       extra = "  " + plural(m.files, "file", "files") + ", " + mem::formatBytes(m.bytes);
                     if (!m.point.empty()) extra += "  at " + m.point;
                     c.printFmt("  %2d %s %-18s %s%s", m.priority, what.c_str(), m.name.c_str(), m.source.c_str(),
                                extra.c_str());
                   }
                   c.printFmt("Unpacked copies go to %s", fs->cacheDir().c_str());
                 });

  con.addCommand("vfs.ls", "vfs.ls [folder] [-r] - the files below a folder (-r: and below its folders).",
                 [fsOf](const ConsoleArgs& a, Console& c) {
                   auto fs = fsOf(c);
                   if (!fs) return;
                   std::string folder;
                   bool recursive = false;
                   for (size_t i = 1; i < a.size(); ++i) {
                     if (a[i] == "-r") recursive = true;
                     else folder = a[i];
                   }
                   if (!recursive)
                     for (const std::string& d : fs->listDirs(folder)) c.printFmt("  %s/", d.c_str());
                   const std::vector<vfs::FileInfo> files = fs->list(folder, recursive);
                   const size_t shown = std::min<size_t>(files.size(), 200);
                   for (size_t i = 0; i < shown; ++i) {
                     vfs::MountInfo m;
                     fs->mountInfo(files[i].mount, m);
                     c.printFmt("  %-52s %10s  %s", files[i].path.c_str(), mem::formatBytes(files[i].size).c_str(),
                                m.name.c_str());
                   }
                   if (shown < files.size()) c.printFmt("  ... and %zu more", files.size() - shown);
                   c.printFmt("%s.", plural(files.size(), "file", "files").c_str());
                 },
                 0, pathCompleter);

  con.addCommand("vfs.which", "vfs.which <path> - every layer's copy of a file; the last one is the one used.",
                 [fsOf](const ConsoleArgs& a, Console& c) {
                   auto fs = fsOf(c);
                   if (!fs) return;
                   if (a.size() < 2) return c.error("vfs.which <path>");
                   const std::vector<vfs::FileInfo> copies = fs->layers(a[1]);
                   if (copies.empty()) return c.error(a[1] + " does not exist.");
                   for (const vfs::FileInfo& f : copies)
                     c.printFmt("  %s  (%s)", fs->describe(f.mount, f.path).c_str(), mem::formatBytes(f.size).c_str());
                 },
                 0, pathCompleter);

  con.addCommand("vfs.cat", "vfs.cat <path> [lines] - the start of a text file (40 lines unless you say).",
                 [fsOf](const ConsoleArgs& a, Console& c) {
                   auto fs = fsOf(c);
                   if (!fs) return;
                   if (a.size() < 2) return c.error("vfs.cat <path> [lines]");
                   std::string text, err;
                   if (!fs->readText(a[1], text, &err)) return c.error(err);
                   const int limit = std::max(1, a.integer(2, 40));
                   int lines = 0;
                   size_t at = 0;
                   while (at < text.size() && lines < limit) {
                     size_t end = text.find('\n', at);
                     if (end == std::string::npos) end = text.size();
                     std::string line = text.substr(at, end - at);
                     if (!line.empty() && line.back() == '\r') line.pop_back();
                     if (line.size() > 300) line = line.substr(0, 300) + " ...";
                     c.print(line);
                     at = end + 1;
                     ++lines;
                   }
                   if (at < text.size()) c.printFmt("... (%s in all)", mem::formatBytes(text.size()).c_str());
                 },
                 0, pathCompleter);

  con.addCommand("assets.stats", "assets.stats - how many files of each kind the game can read, and their size.",
                 [regOf](const ConsoleArgs&, Console& c) {
                   assets::Registry* r = regOf(c);
                   if (!r) return;
                   size_t files = 0;
                   uint64_t bytes = 0;
                   const auto stats = r->stats();
                   for (int t = 0; t < assets::kTypeCount; ++t) {
                     if (!stats[static_cast<size_t>(t)].files) continue;
                     c.printFmt("  %-9s %5zu  %10s", assets::typeName(static_cast<assets::Type>(t)),
                                stats[static_cast<size_t>(t)].files,
                                mem::formatBytes(stats[static_cast<size_t>(t)].bytes).c_str());
                     files += stats[static_cast<size_t>(t)].files;
                     bytes += stats[static_cast<size_t>(t)].bytes;
                   }
                   c.printFmt("  %-9s %5zu  %10s", "in all", files, mem::formatBytes(bytes).c_str());
                 });

  con.addCommand("assets.list",
                 "assets.list [kind|text] - assets of a kind (rules, map, campaign, sound, music, texture ...) or "
                 "whose path contains the text.",
                 [regOf](const ConsoleArgs& a, Console& c) {
                   assets::Registry* r = regOf(c);
                   if (!r) return;
                   assets::Type type = assets::Type::Unknown;
                   const bool byType = a.size() > 1 && assets::typeFromName(a[1], type);
                   const std::vector<assets::Id> ids = byType ? r->ofType(type) : r->all();
                   size_t shown = 0, matched = 0;
                   for (assets::Id id : ids) {
                     const assets::Record* rec = r->get(id);
                     if (!byType && a.size() > 1 && rec->path.find(a[1]) == std::string::npos) continue;
                     ++matched;
                     if (shown >= 200) continue;
                     ++shown;
                     c.printFmt("  %-9s %-52s v%-3u %10s  %s%s", assets::typeName(rec->type), rec->path.c_str(),
                                rec->version, mem::formatBytes(rec->size).c_str(), rec->source.c_str(),
                                rec->layers > 1 ? " (overrides a lower layer)" : "");
                   }
                   if (shown < matched) c.printFmt("  ... and %zu more", matched - shown);
                   c.printFmt("%s.", plural(matched, "asset", "assets").c_str());
                 },
                 0,
                 [](const ConsoleArgs&, const std::string& partial) {
                   std::vector<std::string> out;
                   for (int t = 1; t < assets::kTypeCount; ++t) {
                     const std::string n = assets::typeName(static_cast<assets::Type>(t));
                     if (n.compare(0, partial.size(), partial) == 0) out.push_back(n);
                   }
                   return out;
                 });

  con.addCommand("assets.info",
                 "assets.info <path> - one asset: where it comes from, its version and hash, what it uses and what "
                 "uses it.",
                 [regOf](const ConsoleArgs& a, Console& c) {
                   assets::Registry* r = regOf(c);
                   if (!r) return;
                   if (a.size() < 2) return c.error("assets.info <path>");
                   const assets::Id id = r->find(a[1]);
                   const assets::Record* rec = r->get(id);
                   if (!rec) return c.error("There is no asset called " + a[1] + ".");
                   const uint64_t hash = r->contentHash(id);
                   rec = r->get(id);
                   c.printFmt("%s: %s, %s, version %u, from %s%s", rec->path.c_str(), assets::typeName(rec->type),
                              rec->present ? mem::formatBytes(rec->size).c_str() : "missing", rec->version,
                              rec->source.empty() ? "nowhere" : rec->source.c_str(),
                              rec->layers > 1 ? (", " + std::to_string(rec->layers) + " layers have it").c_str() : "");
                   if (hash) c.printFmt("  contents %s", vfs::hex64(hash).c_str());
                   for (assets::Id d : rec->uses) {
                     const assets::Record* o = r->get(d);
                     c.printFmt("  uses     %s%s", o->path.c_str(), o->present ? "" : "  (MISSING)");
                   }
                   for (assets::Id d : r->usersOf(id, true)) c.printFmt("  used by  %s", r->get(d)->path.c_str());
                 },
                 0, pathCompleter);

  con.addCommand("assets.rescan", "assets.rescan - look at everything mounted again and work out what uses what.",
                 [regOf](const ConsoleArgs&, Console& c) {
                   assets::Registry* r = regOf(c);
                   if (!r) return;
                   r->rebuild();
                   const size_t links = r->scanDependencies();
                   c.printFmt("%s, %s between them.", plural(r->all().size(), "asset", "assets").c_str(),
                              plural(links, "link", "links").c_str());
                 });

  if (hooks.hotReload) {
    HotReload* hot = hooks.hotReload;
    con.addBool("content.hotreload", true,
                "Watch the game's files while it runs and take in what changes (rules, sounds, lists of maps and "
                "campaigns).",
                kCVarArchive)
        .onChange([hot](const CVar& v) { hot->setEnabled(v.b()); });
    con.addFloat("content.interval", 1.0, 0.1, 30.0, "Seconds between looks at the disk for changed files.",
                 kCVarArchive)
        .onChange([hot](const CVar& v) { hot->setInterval(v.f()); });
    con.addCommand("content.check", "content.check - look for changed files now and take them in.",
                   [hot, fsOf, hooks](const ConsoleArgs&, Console& c) {
                     auto fs = fsOf(c);
                     if (!fs) return;
                     const size_t n = hot->checkNow(fs, hooks.registry ? hooks.registry() : nullptr);
                     if (!n) return c.print("Nothing has changed on disk.");
                     for (const vfs::Change& ch : hot->lastChanges())
                       c.printFmt("  %-8s %s", vfs::changeName(ch.kind), ch.path.c_str());
                   });
    con.addCommand("content.watches", "content.watches - which systems are told about which files, and how often they were.",
                   [hot](const ConsoleArgs&, Console& c) {
                     for (const HotReload::WatchInfo& w : hot->watches())
                       c.printFmt("  %-16s %s*%s   told %llu time%s", w.name.c_str(), w.prefix.c_str(), w.ext.c_str(),
                                  static_cast<unsigned long long>(w.calls), w.calls == 1 ? "" : "s");
                     const HotReload::Stats& s = hot->stats();
                     c.printFmt("%s. Looked %llu times (the last took %.1f ms for %zu files); %llu changes taken in, "
                                "%zu waiting to settle.",
                                hot->enabled() ? "Watching" : "Not watching", static_cast<unsigned long long>(s.scans),
                                s.lastScanMs, s.filesWatched, static_cast<unsigned long long>(s.changes), s.waiting);
                   });
  }

  con.addCommand("pack.build",
                 "pack.build <folder> [file] - packs a content folder (a mod) into one file to hand on.",
                 [resolve](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 2) return c.error("pack.build <folder> [file]");
                   const std::string dir = resolve(a[1], false);
                   pack::Manifest m;
                   std::string out = a.size() > 2 ? resolve(a[2], true) : std::string();
                   if (out.empty()) {
                     std::string stem = std::filesystem::path(dir).filename().string();
                     if (stem.empty()) stem = std::filesystem::path(dir).parent_path().filename().string();
                     out = resolve(stem + pack::kExtension, true);
                   }
                   std::string err;
                   if (!pack::build(dir, out, m, err)) return c.error("Can't pack it: " + err);
                   std::string kinds;
                   for (const std::string& k : m.kinds) kinds += (kinds.empty() ? "" : ", ") + k;
                   c.printFmt("Packed '%s' %s: %s, %s (%s) -> %s", m.name.c_str(), m.version.c_str(),
                              plural(m.files.size(), "file", "files").c_str(),
                              mem::formatBytes(m.totalBytes()).c_str(), kinds.empty() ? "no game folders" : kinds.c_str(),
                              out.c_str());
                 });

  con.addCommand("pack.info", "pack.info <file> - what a pack says about itself.",
                 [resolve](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 2) return c.error("pack.info <file>");
                   pack::Manifest m;
                   std::string err;
                   if (!pack::readManifest(resolve(a[1], false), m, err)) return c.error("Can't read it: " + err);
                   c.printFmt("%s (%s) version %s%s%s", m.name.c_str(), m.id.c_str(), m.version.c_str(),
                              m.author.empty() ? "" : " by ", m.author.c_str());
                   if (!m.description.empty()) c.print(m.description);
                   std::string kinds;
                   for (const std::string& k : m.kinds) kinds += (kinds.empty() ? "" : ", ") + k;
                   c.printFmt("  %s, %s; holds: %s", plural(m.files.size(), "file", "files").c_str(),
                              mem::formatBytes(m.totalBytes()).c_str(), kinds.empty() ? "nothing the game knows" : kinds.c_str());
                   for (const pack::Requirement& r : m.requirements)
                     c.printFmt("  needs %s (%s)", r.id.c_str(), r.wording().c_str());
                   std::string why;
                   if (!pack::engineAllows(m, &why)) c.error("  " + why);
                   if (m.fromModJson) c.print("  (a zipped mod folder without a pack.json: no checksums to verify against)");
                   else c.printFmt("  checksum %s", m.checksum.c_str());
                 });

  con.addCommand("pack.verify", "pack.verify <file> - unpacks every file of a pack and checks it against the manifest.",
                 [resolve](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 2) return c.error("pack.verify <file>");
                   const pack::Report r = pack::verify(resolve(a[1], false));
                   for (const std::string& p : r.problems) c.error("  " + p);
                   if (r.ok) c.printFmt("The pack is whole: %s, %s.", plural(r.files, "file", "files").c_str(),
                                        mem::formatBytes(r.bytes).c_str());
                   else c.error("The pack is damaged or was changed after it was made.");
                 });

  con.addCommand("pack.extract", "pack.extract <file> <folder> - unpacks a pack into a folder.",
                 [resolve](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 3) return c.error("pack.extract <file> <folder>");
                   std::string err;
                   const std::string dir = resolve(a[2], true);
                   if (!pack::extract(resolve(a[1], false), dir, err)) return c.error("Can't unpack it: " + err);
                   c.print("Unpacked into " + dir);
                 });
}

void unregisterDataCommands(Console& con) {
  for (const char* name : {"vfs.mounts", "vfs.ls", "vfs.which", "vfs.cat", "assets.stats", "assets.list",
                           "assets.info", "assets.rescan", "content.check", "content.watches", "pack.build",
                           "pack.info", "pack.verify", "pack.extract"})
    con.removeCommand(name);
}

}  // namespace pioneer

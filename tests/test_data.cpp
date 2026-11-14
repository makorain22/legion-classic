// Pioneer part 2: the virtual file system, packs, the asset registry and hot
// reload.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#include <nlohmann/json.hpp>

#include "../engine/src/core/zip_file.hpp"
#include "pioneer/core/assets.hpp"
#include "pioneer/core/content.hpp"
#include "pioneer/core/hot_reload.hpp"
#include "pioneer/core/pack.hpp"
#include "pioneer/core/version.hpp"
#include "pioneer/core/vfs.hpp"

using namespace pioneer;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// A scratch folder, removed afterwards.
struct TempDir {
  std::string path;
  explicit TempDir(const char* name) {
    path = (fs::temp_directory_path() / (std::string("pioneer_data_") + name)).generic_string() + "/";
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void put(const std::string& path, const std::string& text) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string text(const vfs::Vfs& v, const std::string& path) {
  std::string out, err;
  if (!v.readText(path, out, &err)) return "<" + err + ">";
  return out;
}

std::vector<std::string> paths(const std::vector<vfs::FileInfo>& files) {
  std::vector<std::string> out;
  for (const vfs::FileInfo& f : files) out.push_back(f.path);
  return out;
}

bool has(const std::vector<vfs::Change>& cs, const std::string& path, vfs::ChangeKind kind) {
  return std::any_of(cs.begin(), cs.end(), [&](const vfs::Change& c) { return c.path == path && c.kind == kind; });
}

// A small mod folder: one rule patch, a campaign with a map and a briefing.
void makeMod(const std::string& dir, const std::string& name = "Test mod", const std::string& version = "1.2") {
  put(dir + "mod.json", json{{"name", name}, {"version", version}, {"author", "tests"}, {"requires", json::array()}}.dump());
  put(dir + "rules/units.json", R"({"units": [{"id": "ger_rifleman", "health": 150}]})");
  put(dir + "campaigns/demo/campaign.json",
      R"({"id": "demo", "missions": [{"id": "d1", "map": "d1.lmap", "briefing": "d1.md"}]})");
  put(dir + "campaigns/demo/d1.lmap", std::string(300, 'm'));
  put(dir + "campaigns/demo/d1.md", "# Demo\nHold the bridge.\n");
}

}  // namespace

TEST_CASE("virtual paths are cleaned and may not leave the tree") {
  std::string p;
  CHECK(vfs::normalise("rules\\units.json", p));
  CHECK(p == "rules/units.json");
  CHECK(vfs::normalise("/a/./b//c/", p));
  CHECK(p == "a/b/c");
  CHECK(vfs::normalise("a/b/../c", p));
  CHECK(p == "a/c");
  CHECK(vfs::normalise("", p));
  CHECK(p.empty());
  CHECK_FALSE(vfs::normalise("../secrets.txt", p));
  CHECK_FALSE(vfs::normalise("a/../../b", p));
  CHECK_FALSE(vfs::normalise("C:/windows/system.ini", p));
}

TEST_CASE("mounts are layered: the highest that has a file wins, and every copy can be had") {
  TempDir base("layers_base"), mod("layers_mod");
  put(base.path + "rules/units.json", "base units");
  put(base.path + "rules/weapons.json", "base weapons");
  put(base.path + "audio/sfx/rifle.ogg", "bang");
  put(mod.path + "rules/units.json", "mod units");
  put(mod.path + "maps/extra.lmap", "map");

  vfs::Vfs v;
  const int b = v.mountDir(base.path, "base", 0);
  const int m = v.mountDir(mod.path, "mod", 1);
  CHECK(text(v, "rules/units.json") == "mod units");
  CHECK(text(v, "rules/weapons.json") == "base weapons");
  CHECK(text(v, "rules\\units.json") == "mod units");
  CHECK_FALSE(v.exists("rules/nothing.json"));
  CHECK_FALSE(v.exists("../" + fs::path(base.path).filename().string() + "/rules/units.json"));

  const std::vector<vfs::FileInfo> copies = v.layers("rules/units.json");
  REQUIRE(copies.size() == 2);
  CHECK(copies[0].mount == b);
  CHECK(copies[1].mount == m);
  CHECK(v.layers("rules/weapons.json").size() == 1);

  // The merged tree.
  CHECK(paths(v.list("rules")) == std::vector<std::string>{"rules/units.json", "rules/weapons.json"});
  CHECK(v.list("rules")[0].mount == m);
  CHECK(paths(v.list("", true, ".OGG")) == std::vector<std::string>{"audio/sfx/rifle.ogg"});
  CHECK(v.list("", true).size() == 4);
  CHECK(v.listDirs("") == std::vector<std::string>{"audio", "maps", "rules"});
  CHECK(v.listDirs("audio") == std::vector<std::string>{"sfx"});

  // Files in memory, on top; and a mount at a point in the tree.
  const int mem = v.mountMemory("generated", 5);
  CHECK(v.addMemoryFile(mem, "rules/units.json", "made up"));
  CHECK(text(v, "rules/units.json") == "made up");
  CHECK(v.layers("rules/units.json").size() == 3);
  const int at = v.mountMemory("extra", 0, "mods/extra");
  CHECK(v.addMemoryFile(at, "readme.txt", "hello"));
  CHECK(text(v, "mods/extra/readme.txt") == "hello");
  CHECK_FALSE(v.exists("readme.txt"));
  CHECK(v.listDirs("mods") == std::vector<std::string>{"extra"});
  CHECK(v.existsIn(at, "mods/extra"));

  // Equal priority: the one mounted last wins. Unmounting brings the other back.
  CHECK(v.unmount(mem));
  const int late = v.mountMemory("late", 1);
  v.addMemoryFile(late, "rules/units.json", "late");
  CHECK(text(v, "rules/units.json") == "late");
  CHECK(v.unmount(late));
  CHECK(text(v, "rules/units.json") == "mod units");
  CHECK(v.mounts().size() == 3);
  CHECK(v.findMount("mod") == m);
  CHECK(v.describe(m, "rules/units.json") == mod.path + "rules/units.json");
}

TEST_CASE("writing goes to the one folder chosen for it, and never half a file") {
  TempDir user("write_user"), base("write_base");
  put(base.path + "settings.json", "{}");
  vfs::Vfs v;
  v.mountDir(base.path, "base", 0);
  std::string err;
  CHECK_FALSE(v.writeText("notes/a.txt", "x", &err));  // nowhere to write yet
  const int u = v.mountDir(user.path, "user", 10);
  v.setWriteMount(u);
  CHECK(v.writeText("notes/a.txt", "first", &err));
  CHECK(slurp(user.path + "notes/a.txt") == "first");
  CHECK(v.writeText("notes/a.txt", "second", &err));
  CHECK(text(v, "notes/a.txt") == "second");
  CHECK_FALSE(fs::exists(user.path + "notes/a.txt.tmp"));
  CHECK_FALSE(v.writeText("../outside.txt", "x", &err));
  CHECK_FALSE(fs::exists(base.path + "notes/a.txt"));
}

TEST_CASE("versions and requirements are read the way people write them") {
  pack::Version v;
  CHECK(pack::Version::parse("1.2.3", v));
  CHECK(v.str() == "1.2.3");
  CHECK(pack::Version::parse("v2", v));
  CHECK(v.str() == "2.0.0");
  CHECK(pack::Version::parse("1.4-beta", v));
  CHECK(v.str() == "1.4.0");
  CHECK_FALSE(pack::Version::parse("beta", v));
  pack::Version a, b;
  pack::Version::parse("1.10", a);
  pack::Version::parse("1.9.9", b);
  CHECK(b < a);

  auto allows = [](const char* req, const char* have) {
    pack::Requirement r;
    pack::Version h;
    REQUIRE(pack::Requirement::parse(req, r));
    REQUIRE(pack::Version::parse(have, h));
    return r.allows(h);
  };
  CHECK(allows("veterans", "0.1"));
  CHECK(allows("veterans >= 1.2", "1.2.0"));
  CHECK_FALSE(allows("veterans >= 1.2", "1.1.9"));
  CHECK(allows("veterans>=1.2", "3.0"));
  CHECK(allows("veterans 1.2", "1.5"));       // a bare version means "at least"
  CHECK(allows("veterans = 1.0", "1.0.0"));
  CHECK_FALSE(allows("veterans = 1.0", "1.0.1"));
  CHECK(allows("veterans ^1.2", "1.9"));
  CHECK_FALSE(allows("veterans ^1.2", "2.0"));
  CHECK(allows("veterans ~1.2", "1.2.7"));
  CHECK_FALSE(allows("veterans ~1.2", "1.3"));
  CHECK_FALSE(allows("veterans < 2", "2.0"));
  pack::Requirement r;
  CHECK(pack::Requirement::parse("  base_units   >=  1.1 ", r));
  CHECK(r.id == "base_units");
  CHECK(r.str() == "base_units >= 1.1.0");
  CHECK(r.wording() == "1.1.0 or newer");
  CHECK_FALSE(pack::Requirement::parse(">= 1.0", r));
  CHECK_FALSE(pack::Requirement::parse("veterans >= soon", r));

  // The engine a pack was made for.
  pack::Manifest m;
  CHECK(pack::engineAllows(m));
  m.engine = std::string(">=") + kEngineVersion;
  CHECK(pack::engineAllows(m));
  m.engine = ">=99.0";
  std::string why;
  CHECK_FALSE(pack::engineAllows(m, &why));
  CHECK(why.find("99.0.0 or newer") != std::string::npos);
}

TEST_CASE("a folder becomes a pack, the pack checks out, and comes apart again unchanged") {
  TempDir work("pack");
  const std::string dir = work.path + "veterans/";
  makeMod(dir);
  put(dir + ".git/config", "not for packing");
  put(dir + "notes.txt.bak", "nor this");
  const std::string file = work.path + "out/veterans" + pack::kExtension;

  pack::Manifest m;
  m.engine = ">=2.0";
  std::string err;
  REQUIRE_MESSAGE(pack::build(dir, file, m, err), err);
  CHECK(m.id == "veterans");
  CHECK(m.name == "Test mod");
  CHECK(m.version == "1.2");
  CHECK(m.author == "tests");
  CHECK(m.files.size() == 5);
  CHECK(m.kinds == std::vector<std::string>{"rules", "campaigns"});
  CHECK(m.checksum.size() == 16);

  pack::Manifest back;
  REQUIRE_MESSAGE(pack::readManifest(file, back, err), err);
  CHECK(back.id == "veterans");
  CHECK(back.version == "1.2");
  CHECK(back.engine == ">=2.0");
  CHECK(back.files.size() == 5);
  CHECK(back.checksum == m.checksum);
  CHECK_FALSE(back.fromModJson);

  const pack::Report ok = pack::verify(file);
  CHECK(ok.ok);
  CHECK(ok.files == 5);
  CHECK(ok.problems.empty());

  REQUIRE_MESSAGE(pack::extract(file, work.path + "unpacked", err), err);
  for (const pack::FileEntry& f : m.files) CHECK(slurp(work.path + "unpacked/" + f.path) == slurp(dir + f.path));
  CHECK(fs::exists(work.path + "unpacked/pack.json"));
  CHECK_FALSE(fs::exists(work.path + "unpacked/.git/config"));

  // The same folder packs to the same checksum; a changed file to another.
  pack::Manifest again;
  REQUIRE(pack::build(dir, work.path + "again.pnpack", again, err));
  CHECK(again.checksum == m.checksum);
  put(dir + "rules/units.json", R"({"units": [{"id": "ger_rifleman", "health": 151}]})");
  pack::Manifest changed;
  REQUIRE(pack::build(dir, work.path + "changed.pnpack", changed, err));
  CHECK(changed.checksum != m.checksum);

  CHECK_FALSE(pack::build(work.path + "no_such_folder", work.path + "x.pnpack", again, err));
  CHECK_FALSE(pack::readManifest(dir + "mod.json", back, err));  // not an archive
}

TEST_CASE("a pack that was changed after it was made is caught") {
  TempDir work("tamper");
  makeMod(work.path + "mod/");
  pack::Manifest m;
  std::string err;
  const std::string good = work.path + "good.pnpack";
  REQUIRE(pack::build(work.path + "mod/", good, m, err));

  // The same manifest around other contents: one file edited, one left out, one slipped in.
  const std::string bad = work.path + "bad.pnpack";
  {
    zip::Writer w;
    REQUIRE(w.open(bad, err));
    const std::string manifest = pack::toJson(m).dump();
    REQUIRE(w.add("pack.json", manifest.data(), manifest.size(), 6, 0, err));
    for (const pack::FileEntry& f : m.files) {
      if (f.path == "campaigns/demo/d1.md") continue;
      std::string body = slurp(work.path + "mod/" + f.path);
      if (f.path == "rules/units.json") body = R"({"units": [{"id": "ger_rifleman", "health": 999}]})";
      REQUIRE(w.add(f.path, body.data(), body.size(), 6, 0, err));
    }
    const std::string extra = "surprise";
    REQUIRE(w.add("rules/extra.json", extra.data(), extra.size(), 6, 0, err));
    REQUIRE(w.finish(err));
  }
  const pack::Report r = pack::verify(bad);
  CHECK_FALSE(r.ok);
  auto mentions = [&](const char* what) {
    return std::any_of(r.problems.begin(), r.problems.end(),
                       [&](const std::string& p) { return p.find(what) != std::string::npos; });
  };
  CHECK(mentions("rules/units.json is not the file"));
  CHECK(mentions("campaigns/demo/d1.md is listed but missing"));
  CHECK(mentions("rules/extra.json is not in the pack's list"));
  CHECK(pack::verify(good).ok);
  CHECK_FALSE(pack::verify(work.path + "missing.pnpack").ok);
}

TEST_CASE("a pack is read straight out of the archive, and unpacked only for code that needs real files") {
  TempDir work("zipmount"), cache("zipmount_cache");
  makeMod(work.path + "veterans/");
  put(work.path + "veterans/Art/Tank.PNG", "pixels");
  pack::Manifest m;
  std::string err;
  const std::string file = work.path + "veterans.pnpack";
  REQUIRE(pack::build(work.path + "veterans/", file, m, err));

  TempDir base("zipmount_base");
  put(base.path + "rules/units.json", "base units");
  vfs::Vfs v;
  v.setCacheDir(cache.path);
  v.mountDir(base.path, "base", 0);
  const int z = v.mountZip(file, "veterans", 1, "", err);
  REQUIRE_MESSAGE(z >= 0, err);
  CHECK(text(v, "rules/units.json") == slurp(work.path + "veterans/rules/units.json"));
  CHECK(text(v, "campaigns/demo/d1.md") == "# Demo\nHold the bridge.\n");
  CHECK(v.layers("rules/units.json").size() == 2);
  vfs::FileInfo fi;
  REQUIRE(v.stat("campaigns/demo/d1.lmap", fi));
  CHECK(fi.size == 300);
  CHECK(fi.packed);
  CHECK(fi.crc == vfs::crc32(std::string(300, 'm').data(), 300));
  // Archives ignore case when the exact name is not there (packs made on Windows).
  CHECK(text(v, "art/tank.png") == "pixels");
  CHECK(v.existsIn(z, "campaigns/demo"));
  CHECK(v.existsIn(z, "ART"));
  CHECK_FALSE(v.existsIn(z, "campaigns/other"));
  CHECK(v.listDirs("campaigns") == std::vector<std::string>{"demo"});
  CHECK(v.describe(z, "rules/units.json") == "veterans.pnpack:rules/units.json");
  CHECK(v.mounts()[1].packed);
  CHECK(v.mounts()[1].files == 7);  // six of the mod's, and pack.json

  // A real file on request: nothing is unpacked until then, and nothing twice.
  CHECK(fs::is_empty(cache.path));
  const std::string real = v.diskPath("campaigns/demo/d1.md");
  REQUIRE_FALSE(real.empty());
  CHECK(real.compare(0, cache.path.size(), cache.path) == 0);
  CHECK(slurp(real) == "# Demo\nHold the bridge.\n");
  const auto stamp = fs::last_write_time(real);
  CHECK(v.diskPath("campaigns/demo/d1.md") == real);
  CHECK(fs::last_write_time(real) == stamp);
  const std::string folder = v.diskPathIn(z, "campaigns/");
  REQUIRE_FALSE(folder.empty());
  CHECK(folder.back() == '/');
  CHECK(fs::exists(folder + "demo/campaign.json"));
  CHECK(fs::exists(folder + "demo/d1.lmap"));
  CHECK(v.diskPathIn(z, "no_such_folder/").empty());
  // A folder mount's file is its own path.
  CHECK(v.diskPath("rules/units.json").compare(0, cache.path.size(), cache.path) == 0);  // the pack's copy wins
  CHECK(v.diskPathIn(v.findMount("base"), "rules/units.json") == base.path + "rules/units.json");
}

TEST_CASE("a folder zipped by hand works as a mod: its top folder is skipped, mod.json is enough") {
  TempDir work("handzip");
  const std::string file = work.path + "My Mod.zip";
  std::string err;
  {
    zip::Writer w;
    REQUIRE(w.open(file, err));
    auto add = [&](const std::string& name, const std::string& body) {
      REQUIRE(w.add(name, body.data(), body.size(), 6, 0, err));
    };
    add("my_mod/mod.json", R"({"name": "My Mod", "version": "0.3", "requires": ["veterans >= 1.2"]})");
    add("my_mod/rules/units.json", R"({"units": []})");
    add("my_mod/../../escape.txt", "no");
    REQUIRE(w.finish(err));
  }
  pack::Manifest m;
  REQUIRE_MESSAGE(pack::readManifest(file, m, err), err);
  CHECK(m.fromModJson);
  CHECK(m.id == "My Mod");
  CHECK(m.name == "My Mod");
  CHECK(m.version == "0.3");
  REQUIRE(m.requirements.size() == 1);
  CHECK(m.requirements[0].id == "veterans");
  CHECK(m.files.size() == 2);
  CHECK(pack::verify(file).ok);

  vfs::Vfs v;
  REQUIRE(v.mountZip(file, "my_mod", 0, "", err) >= 0);
  CHECK(v.exists("rules/units.json"));
  CHECK(v.exists("mod.json"));
  CHECK_FALSE(v.exists("my_mod/mod.json"));
  CHECK_FALSE(v.exists("escape.txt"));
  CHECK(v.list("", true).size() == 2);

  TempDir out("handzip_out");
  REQUIRE(pack::extract(file, out.path + "inner", err));
  CHECK(fs::exists(out.path + "inner/rules/units.json"));
  CHECK_FALSE(fs::exists(out.path + "escape.txt"));
  CHECK_FALSE(fs::exists(out.path + "inner/escape.txt"));
}

TEST_CASE("changes on disk are found: added, modified and removed, in folders and in replaced packs") {
  TempDir base("changes"), packs("changes_packs");
  put(base.path + "rules/units.json", "one");
  put(base.path + "audio/a.ogg", "aaaa");
  makeMod(packs.path + "mod/");
  pack::Manifest m;
  std::string err;
  const std::string file = packs.path + "mod.pnpack";
  REQUIRE(pack::build(packs.path + "mod/", file, m, err));

  vfs::Vfs v;
  v.mountDir(base.path, "base", 0);
  const int z = v.mountZip(file, "mod", 1, "", err);
  REQUIRE(z >= 0);
  const uint64_t rev = v.revision();
  CHECK(v.poll().empty());  // the first look is the starting point
  CHECK(v.poll().empty());
  CHECK(v.revision() == rev);

  put(base.path + "rules/units.json", "one, longer");
  put(base.path + "rules/new.json", "{}");
  fs::remove(base.path + "audio/a.ogg");
  put(base.path + "rules/units.json.tmp", "half written");  // editors' leftovers don't count
  put(base.path + ".hidden/x", "x");
  std::vector<vfs::Change> cs = v.poll();
  CHECK(cs.size() == 3);
  CHECK(has(cs, "rules/units.json", vfs::ChangeKind::Modified));
  CHECK(has(cs, "rules/new.json", vfs::ChangeKind::Added));
  CHECK(has(cs, "audio/a.ogg", vfs::ChangeKind::Removed));
  CHECK(v.revision() > rev);
  CHECK(v.poll().empty());

  // The pack is replaced by a new build: what differs inside is reported, by content.
  put(packs.path + "mod/rules/units.json", R"({"units": [{"id": "ger_rifleman", "health": 175}]})");
  put(packs.path + "mod/rules/weapons.json", R"({"weapons": []})");
  fs::remove(packs.path + "mod/campaigns/demo/d1.md");
  const std::string oldPrint = v.mounts()[1].fingerprint;
  REQUIRE(pack::build(packs.path + "mod/", file, m, err));
  cs = v.poll();
  CHECK(has(cs, "rules/units.json", vfs::ChangeKind::Modified));
  CHECK(has(cs, "rules/weapons.json", vfs::ChangeKind::Added));
  CHECK(has(cs, "campaigns/demo/d1.md", vfs::ChangeKind::Removed));
  CHECK(has(cs, "pack.json", vfs::ChangeKind::Modified));
  CHECK_FALSE(has(cs, "campaigns/demo/d1.lmap", vfs::ChangeKind::Modified));
  for (const vfs::Change& c : cs) CHECK(c.mount == z);
  CHECK(text(v, "rules/units.json").find("175") != std::string::npos);  // read from the new archive at once
  CHECK(v.mounts()[1].fingerprint != oldPrint);
  CHECK(v.poll().empty());
}

TEST_CASE("hot reload tells the systems concerned, once a file has settled") {
  TempDir base("hot");
  put(base.path + "rules/units.json", "v1");
  put(base.path + "audio/audio.json", "{}");
  auto v = std::make_shared<vfs::Vfs>();
  v->mountDir(base.path, "base", 0);

  HotReload hot;
  hot.setInterval(0.1);
  std::vector<std::string> rules, sounds, everything;
  hot.watch("rules", "rules/", ".json", [&](const std::vector<vfs::Change>& cs) {
    for (const vfs::Change& c : cs) rules.push_back(c.path);
  });
  const int soundWatch = hot.watch("sounds", "audio/", "", [&](const std::vector<vfs::Change>& cs) {
    for (const vfs::Change& c : cs) sounds.push_back(c.path);
  });
  hot.watch("all", "", "", [&](const std::vector<vfs::Change>& cs) {
    for (const vfs::Change& c : cs) everything.push_back(c.path);
  });

  // Tests run the job system on one thread, so a look is finished the moment
  // it is asked for. With steps of 0.06 s and a look every 0.1 s, every other
  // step asks for a look and the step after takes it in.
  auto step = [&] { return hot.update(v, 0.06); };
  CHECK(step() == 0);
  CHECK(step() == 0);  // asks for the first look
  CHECK(step() == 0);  // takes it in: the starting point
  put(base.path + "rules/units.json", "v2, changed");
  put(base.path + "rules/notes.txt", "not json");
  CHECK(step() == 0);  // looks
  CHECK(step() == 0);  // both seen once: held back
  CHECK(hot.stats().waiting == 2);
  CHECK(rules.empty());
  // Still being written: the rules change again before the next look, so they wait on.
  put(base.path + "rules/units.json", "v3, changed once more");
  CHECK(step() == 0);  // looks
  CHECK(step() == 1);  // the text file has settled; the rules have not
  CHECK(rules.empty());
  CHECK(everything == std::vector<std::string>{"rules/notes.txt"});
  CHECK(hot.stats().waiting == 1);
  CHECK(step() == 0);  // looks
  CHECK(step() == 1);  // now they have
  CHECK(rules == std::vector<std::string>{"rules/units.json"});
  CHECK(sounds.empty());
  CHECK(everything.size() == 2);
  CHECK(hot.stats().waiting == 0);
  CHECK(step() == 0);

  // On demand: at once, without waiting for anything to settle.
  put(base.path + "audio/audio.json", "{\"sounds\": {}}");
  CHECK(hot.checkNow(v) == 1);
  CHECK(sounds == std::vector<std::string>{"audio/audio.json"});
  REQUIRE(hot.lastChanges().size() == 1);
  CHECK(hot.lastChanges()[0].kind == vfs::ChangeKind::Modified);

  // Switched off, nothing is looked at; a watch taken away is not called.
  hot.setEnabled(false);
  put(base.path + "audio/audio.json", "{\"sounds\": {\"x\": {}}}");
  for (int i = 0; i < 4; ++i) CHECK(hot.update(v, 0.2) == 0);
  hot.setEnabled(true);
  hot.unwatch(soundWatch);
  CHECK(hot.checkNow(v) == 1);
  CHECK(sounds.size() == 1);
  CHECK(hot.watches().size() == 2);
  CHECK(hot.watches()[0].calls == 1);
}

TEST_CASE("the asset registry knows what every file is, where it comes from and what uses it") {
  CHECK(assets::typeOf("rules/units.json") == assets::Type::Rules);
  CHECK(assets::typeOf("campaigns/ger/campaign.json") == assets::Type::Campaign);
  CHECK(assets::typeOf("campaigns/ger/g01.md") == assets::Type::Briefing);
  CHECK(assets::typeOf("campaigns/ger/g01.lmap") == assets::Type::Map);
  CHECK(assets::typeOf("audio/sfx/Rifle.OGG") == assets::Type::Sound);
  CHECK(assets::typeOf("audio/music/legion.ogg") == assets::Type::Music);
  CHECK(assets::typeOf("art/tank.png") == assets::Type::Texture);
  CHECK(assets::typeOf("models/tiger.glb") == assets::Type::Model);
  CHECK(assets::typeOf("shaders/terrain.frag") == assets::Type::Shader);
  CHECK(assets::typeOf("scripts/mission.lua") == assets::Type::Script);
  CHECK(assets::typeOf("fonts/ui.ttf") == assets::Type::Font);
  CHECK(assets::typeOf("mod.json") == assets::Type::Pack);
  CHECK(assets::typeOf("maps/skirmish/maps.json") == assets::Type::Data);
  CHECK(assets::typeOf("audio/LICENSES.md") == assets::Type::Text);
  CHECK(assets::typeOf("thing.xyz") == assets::Type::Unknown);
  assets::Type t;
  CHECK(assets::typeFromName("Music", t));
  CHECK(t == assets::Type::Music);
  CHECK_FALSE(assets::typeFromName("sandwich", t));

  TempDir base("assets_base"), mod("assets_mod");
  put(base.path + "rules/units.json", "base");
  put(base.path + "audio/audio.json", R"({"sounds": {"rifle": {"files": ["sfx/rifle.ogg", "sfx/lost.ogg"]}}})");
  put(base.path + "audio/sfx/rifle.ogg", "bang");
  makeMod(mod.path);
  auto v = std::make_shared<vfs::Vfs>();
  v->mountDir(base.path, "base", 0);
  v->mountDir(mod.path, "veterans", 1);

  assets::Registry reg;
  reg.attach(v);
  reg.rebuild();
  CHECK(reg.all().size() == 7);
  const assets::Id units = reg.find("rules/units.json");
  REQUIRE(units != assets::kNone);
  const assets::Record* r = reg.get(units);
  CHECK(r->type == assets::Type::Rules);
  CHECK(r->present);
  CHECK(r->source == "veterans");
  CHECK(r->layers == 2);
  CHECK(r->version == 1);
  CHECK(reg.get("audio/sfx/rifle.ogg")->source == "base");
  CHECK(reg.ofType(assets::Type::Map).size() == 1);
  CHECK(reg.stats()[static_cast<size_t>(assets::Type::Rules)].files == 1);
  CHECK(reg.find("rules\\units.json") == units);
  CHECK(reg.find("nothing/here.json") == assets::kNone);

  // What uses what, found by reading the JSON.
  CHECK(reg.scanDependencies() == 3);
  const assets::Id campaign = reg.find("campaigns/demo/campaign.json");
  const assets::Id map = reg.find("campaigns/demo/d1.lmap");
  const assets::Id brief = reg.find("campaigns/demo/d1.md");
  CHECK(std::set<assets::Id>(reg.get(campaign)->uses.begin(), reg.get(campaign)->uses.end()) ==
        std::set<assets::Id>{map, brief});
  CHECK(reg.usersOf(map) == std::vector<assets::Id>{campaign});
  const assets::Id audio = reg.find("audio/audio.json");
  REQUIRE(reg.get(audio)->uses.size() == 1);  // sfx/lost.ogg does not exist: not a link
  CHECK(reg.get(reg.get(audio)->uses[0])->path == "audio/sfx/rifle.ogg");
  // Links by hand, also to something that is not there (it shows as missing).
  reg.setDependencies(audio, {"audio/sfx/rifle.ogg", "audio/sfx/lost.ogg"});
  const assets::Record* lost = reg.get("audio/sfx/lost.ogg");
  REQUIRE(lost);
  CHECK_FALSE(lost->present);
  CHECK(lost->usedBy == std::vector<assets::Id>{audio});
  CHECK(reg.dependenciesOf(audio).size() == 2);

  // Contents are hashed once per version.
  const uint64_t h1 = reg.contentHash(units);
  CHECK(h1 != 0);
  CHECK(reg.contentHash(units) == h1);

  // A change on disk: new versions for the file and for everything that uses it.
  v->poll();
  std::vector<std::string> told;
  const int handle = reg.listen([&](const assets::Record& a) { told.push_back(a.path); });
  put(mod.path + "campaigns/demo/d1.lmap", std::string(400, 'n'));
  put(mod.path + "rules/units.json", "changed in the mod");
  const std::vector<assets::Id> changed = reg.apply(v->poll());
  CHECK(changed == std::vector<assets::Id>{map, units, campaign});
  CHECK(told == std::vector<std::string>{"campaigns/demo/d1.lmap", "rules/units.json", "campaigns/demo/campaign.json"});
  CHECK(reg.get(map)->version == 2);
  CHECK(reg.get(map)->size == 400);
  CHECK(reg.get(campaign)->version == 2);
  CHECK(reg.get(brief)->version == 1);
  CHECK(reg.contentHash(units) != h1);
  reg.unlisten(handle);

  // A change in a lower layer counts too (rules are merged, not replaced).
  put(base.path + "rules/units.json", "base, edited");
  reg.apply(v->poll());
  CHECK(reg.get(units)->version == 3);
  CHECK(reg.get(units)->source == "veterans");

  // A file that goes away stays known (absent); ids never change.
  fs::remove(mod.path + "campaigns/demo/d1.md");
  reg.apply(v->poll());
  CHECK_FALSE(reg.get(brief)->present);
  CHECK(reg.find("campaigns/demo/d1.md") == brief);
  CHECK(reg.get(campaign)->uses.size() == 2);  // the campaign still wants it: it shows as missing
  CHECK(reg.get(campaign)->version == 3);
  put(mod.path + "campaigns/demo/d1.md", "back");
  reg.apply(v->poll());
  CHECK(reg.get(brief)->present);
  CHECK(reg.find("campaigns/demo/d1.md") == brief);
  CHECK(told.size() == 3);

  // Switching mods: the same registry, another file system.
  auto v2 = std::make_shared<vfs::Vfs>();
  v2->mountDir(base.path, "base", 0);
  reg.attach(v2);
  reg.rebuild();
  CHECK(reg.find("rules/units.json") == units);
  CHECK(reg.get(units)->source == "base");
  CHECK(reg.get(units)->layers == 1);
  CHECK_FALSE(reg.get(campaign)->present);
  CHECK(reg.all().size() == 3);
}

TEST_CASE("a cache loads on first use, swaps in what changed, and keeps the last good version") {
  TempDir base("cache");
  put(base.path + "data/a.json", R"({"n": 1})");
  put(base.path + "data/b.json", R"({"n": 2})");
  put(base.path + "data/c.json", R"({"n": 3})");
  auto v = std::make_shared<vfs::Vfs>();
  v->mountDir(base.path, "base", 0);
  assets::Registry reg;
  reg.attach(v);
  reg.rebuild();
  v->poll();

  std::atomic<int> loads{0};
  assets::Cache<json> cache(reg, [&](const assets::Record& a, const std::vector<uint8_t>& bytes, std::string& error) {
    ++loads;
    json j = json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (j.is_discarded()) {
      error = a.path + " is not JSON";
      return std::shared_ptr<json>();
    }
    return std::make_shared<json>(std::move(j));
  });

  std::string err;
  const std::shared_ptr<const json> a1 = cache.get("data/a.json", &err);
  REQUIRE(a1);
  CHECK((*a1)["n"] == 1);
  CHECK(cache.get("data/a.json") == a1);  // the same object, not loaded again
  CHECK(loads.load() == 1);
  CHECK(cache.preload({"data/a.json", "data/b.json", "data/c.json", "data/none.json"}) == 2);
  CHECK(loads.load() == 3);
  CHECK((*cache.get("data/c.json"))["n"] == 3);
  CHECK_FALSE(cache.get("data/none.json", &err));
  CHECK(err.find("does not exist") != std::string::npos);

  // The file changes: holders of the old object keep it, new requests get the new one.
  put(base.path + "data/a.json", R"({"n": 100})");
  reg.apply(v->poll());
  CHECK(cache.reloadChanged() == 1);
  const std::shared_ptr<const json> a2 = cache.get("data/a.json");
  CHECK(a2 != a1);
  CHECK((*a2)["n"] == 100);
  CHECK((*a1)["n"] == 1);

  // Saved half-written: the last good version stays, and the reason is known.
  put(base.path + "data/a.json", R"({"n": )");
  reg.apply(v->poll());
  CHECK(cache.reloadChanged() == 0);
  CHECK(cache.get("data/a.json", &err) == a2);
  CHECK(err == "data/a.json is not JSON");
  CHECK(cache.errorOf("data/a.json") == "data/a.json is not JSON");
  put(base.path + "data/a.json", R"({"n": 7})");
  reg.apply(v->poll());
  CHECK((*cache.get("data/a.json", &err))["n"] == 7);
  CHECK(err.empty());
}

TEST_CASE("mods can be packs: listed, ordered by what they need, and layered like folders") {
  TempDir game("mods_game"), user("mods_user"), base("mods_base"), cache("mods_cache");
  put(base.path + "rules/units.json", R"({"units": [{"id": "ger_rifleman", "health": 100, "cost": 50}]})");
  put(base.path + "rules/weapons.json", R"({"weapons": []})");

  // One mod as a folder, one as a pack that needs a newer version of it.
  makeMod(game.path + "veterans/", "Veterans", "1.0");
  TempDir work("mods_work");
  put(work.path + "elite/mod.json",
      R"({"name": "Elite", "version": "2.1", "requires": ["veterans >= 1.2"], "engine": ">=2.0"})");
  put(work.path + "elite/rules/units.json", R"({"units": [{"id": "ger_rifleman", "cost": 75}]})");
  std::string err;
  auto packUp = [&](const std::string& dir, const std::string& file) {
    pack::Manifest fresh;
    return pack::build(dir, file, fresh, err);
  };
  REQUIRE(packUp(work.path + "elite/", user.path + "elite.pnpack"));
  // And a pack made for a Pioneer that does not exist yet.
  put(work.path + "future/mod.json", R"({"name": "Future", "engine": ">=99"})");
  put(work.path + "future/rules/units.json", "{}");
  REQUIRE(packUp(work.path + "future/", user.path + "future.pnpack"));
  put(user.path + "notes.txt", "not a mod");

  std::vector<ModInfo> mods = scanMods({game.path, user.path}, 1);
  REQUIRE(mods.size() == 3);
  const ModInfo* elite = findMod(mods, "elite");
  REQUIRE(elite);
  CHECK(elite->packed);
  CHECK(elite->user);
  CHECK(elite->dir.empty());
  CHECK(elite->name == "Elite");
  CHECK(elite->version == "2.1");
  CHECK(elite->error.empty());
  CHECK(elite->dependencies == std::vector<std::string>{"veterans"});
  CHECK(elite->kinds == std::vector<std::string>{"rules"});
  CHECK(elite->checksum.size() == 16);
  CHECK_FALSE(findMod(mods, "veterans")->packed);
  const ModInfo* future = findMod(mods, "future");
  REQUIRE(future);
  CHECK(future->error.find("99.0.0 or newer") != std::string::npos);

  // Load order: what is needed goes first; a version that is too old is said so; a broken mod is left out.
  std::string warnings;
  const std::vector<std::string> order = resolveLoadOrder({"elite", "future", "veterans"}, mods, warnings);
  CHECK(order == std::vector<std::string>{"veterans", "elite"});
  CHECK(warnings.find("'Elite' needs 'Veterans' 1.2.0 or newer; version 1.0 is installed") != std::string::npos);
  CHECK(warnings.find("'Future' is broken") != std::string::npos);

  // In one folder, the folder (which can be edited) counts, not a pack of the same id.
  REQUIRE(packUp(game.path + "veterans/", game.path + "veterans.pnpack"));
  mods = scanMods({game.path, user.path}, 1);
  CHECK(mods.size() == 3);
  CHECK_FALSE(findMod(mods, "veterans")->packed);

  // Layers: base, folder, pack. The rules merge through all three.
  ContentLayers layers;
  layers.setCacheDir(cache.path);
  layers.addRoot(base.path);
  for (const std::string& id : order) REQUIRE_MESSAGE(layers.addMod(*findMod(mods, id), err), err);
  CHECK(layers.modIds() == std::vector<std::string>{"", "veterans", "elite"});
  json units;
  REQUIRE_MESSAGE(layers.loadRules("units.json", units, err), err);
  CHECK(units["units"][0]["health"] == 150);  // from the folder
  CHECK(units["units"][0]["cost"] == 75);     // from the pack
  json below;
  REQUIRE(layers.loadRulesBelow("units.json", "elite", below, err));
  CHECK(below["units"][0]["cost"] == 50);
  // Folders for code that reads real files: the pack's is an unpacked copy.
  const std::vector<std::string> dirs = layers.all("rules/");
  REQUIRE(dirs.size() == 3);
  CHECK(dirs[0] == base.path + "rules/");
  CHECK(dirs[2].compare(0, cache.path.size(), cache.path) == 0);
  CHECK(slurp(dirs[2] + "units.json").find("75") != std::string::npos);
  CHECK(layers.all("campaigns/").size() == 1);
  CHECK(layers.all("rules/units.json").size() == 3);
  CHECK(layers.all("rules/weapons.json").size() == 1);
  // The whole stack as one tree.
  CHECK(layers.fileSystem()->layers("rules/units.json").size() == 3);
  CHECK(layers.fileSystem()->exists("campaigns/demo/campaign.json"));

  // A pack with broken rules names itself in the error.
  put(work.path + "broken/mod.json", R"({"name": "Broken"})");
  put(work.path + "broken/rules/units.json", "{ not json");
  REQUIRE(packUp(work.path + "broken/", user.path + "broken.pnpack"));
  REQUIRE(layers.addPack(user.path + "broken.pnpack", "broken", err));
  CHECK_FALSE(layers.loadRules("units.json", units, err));
  CHECK(err == "broken.pnpack:rules/units.json: invalid JSON");
  CHECK_FALSE(layers.addPack(user.path + "notes.txt", "notes", err));
}

TEST_CASE("the game's own content: every file its data names is there") {
  auto v = std::make_shared<vfs::Vfs>();
  v->mountDir(LEGION_CONTENT_DIR, "base", 0);
  assets::Registry reg;
  reg.attach(v);
  reg.rebuild();
  CHECK(reg.all().size() > 150);
  CHECK(reg.ofType(assets::Type::Rules).size() == 3);
  CHECK(reg.scanDependencies() > 100);

  // Campaigns use their maps and briefings; the sound list uses its files.
  for (assets::Id id : reg.ofType(assets::Type::Campaign)) {
    const assets::Record* c = reg.get(id);
    const json doc = json::parse(std::ifstream(std::string(LEGION_CONTENT_DIR) + c->path), nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    const size_t missions = doc.value("missions", json::array()).size();
    INFO(c->path);
    CHECK(c->uses.size() == missions * 2);
  }
  const assets::Record* audio = reg.get("audio/audio.json");
  REQUIRE(audio);
  size_t sounds = 0, music = 0;
  for (assets::Id d : audio->uses) {
    const assets::Record* o = reg.get(d);
    CHECK(o->present);
    if (o->type == assets::Type::Sound) ++sounds;
    if (o->type == assets::Type::Music) ++music;
  }
  CHECK(sounds >= 77);
  CHECK(music == 9);
  // Every map file is used by a campaign or a map list, or is a stand-alone scenario.
  for (assets::Id id : reg.ofType(assets::Type::Map)) {
    const assets::Record* m = reg.get(id);
    INFO(m->path);
    CHECK((!m->usedBy.empty() || m->path.compare(0, 10, "scenarios/") == 0));
  }
}

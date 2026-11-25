// Pioneer part 9a: the content validator, cooking and the asset index.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

#include <nlohmann/json.hpp>

#include "content_checks.hpp"
#include "pioneer/assets/gltf.hpp"
#include "pioneer/assets/image.hpp"
#include "pioneer/core/assets.hpp"
#include "pioneer/core/content.hpp"
#include "pioneer/core/pack.hpp"
#include "pioneer/map/map_io.hpp"
#include "pioneer/core/vfs.hpp"
#include "pioneer/tools/cook.hpp"
#include "pioneer/tools/validate.hpp"

using namespace pioneer;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  std::string path;
  explicit TempDir(const char* name) {
    path = (fs::temp_directory_path() / (std::string("pioneer_tools_") + name)).generic_string() + "/";
    std::error_code ec;
    fs::remove_all(path, ec);
    fs::create_directories(path, ec);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void put(const std::string& path, const std::string& bytes) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream(path, std::ios::binary) << bytes;
}
void putBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
  put(path, std::string(bytes.begin(), bytes.end()));
}
std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A tenth of a second of a quiet tone, as a 16-bit mono WAV.
std::string wavBytes() {
  const uint32_t rate = 22050, frames = 2205;
  std::string s;
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF); };
  auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF); };
  s += "RIFF";
  u32(36 + frames * 2);
  s += "WAVEfmt ";
  u32(16);
  u16(1);
  u16(1);
  u32(rate);
  u32(rate * 2);
  u16(2);
  u16(16);
  s += "data";
  u32(frames * 2);
  for (uint32_t i = 0; i < frames; ++i) u16(static_cast<uint16_t>(static_cast<int16_t>((i % 50) * 40 - 1000)));
  return s;
}

std::vector<uint8_t> boxGlb() {
  VoxelModel vm(4, 4, 4);
  for (int z = 0; z < 4; ++z)
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) vm.set(x, y, z, 1);
  VoxelPalette pal;
  return writeGlb(voxelModelAsset(vm, pal, 0.25f, 2, 2, "box"));
}

// A small content folder that passes every engine check.
void cleanTree(const std::string& dir) {
  put(dir + "lang/en.json", R"({"language": "en", "name": "English", "strings": {"hud": {"selected": "{n|# unit|# units} selected", "hello": "Hello"}}})");
  put(dir + "lang/de.json", R"({"language": "de", "name": "Deutsch", "strings": {"hud": {"selected": "{n|# Einheit|# Einheiten} gewählt"}}})");
  put(dir + "audio/sfx/beep.wav", wavBytes());
  put(dir + "audio/audio.json", R"({
    "sounds": {"beep": {"files": ["sfx/beep.wav"], "bus": "ui", "volume": 0.5, "range": 0, "limit": 2}},
    "voices": {"en": {"language": "en", "cues": {"move": {"lines": [{"file": "sfx/beep.wav", "text": "Move!"}]}}}},
    "factions": {"blue": "en"}
  })");
  put(dir + "ui/panel.lua", "panel{ title = \"Test\", draw = function() ui.text(\"hi\") end }\n");
  putBytes(dir + "models/box.glb", boxGlb());
  Image img(8, 8);
  fs::create_directories(dir + "art");
  REQUIRE(img.savePng(dir + "art/swatch.png"));
  put(dir + "fonts/fake.ttf", std::string("\0\1\0\0", 4) + std::string(60, 'x'));
  put(dir + "data/list.json", R"({"swatch": "art/swatch.png", "note": "a sentence that is not a file name.png at all"})");
}

struct Loaded {
  std::shared_ptr<vfs::Vfs> fs = std::make_shared<vfs::Vfs>();
  assets::Registry reg;
  explicit Loaded(const std::string& dir) {
    fs->mountDir(dir, "base", 0);
    reg.attach(fs);
    reg.rebuild();
  }
};

tools::Validator engineValidator() {
  tools::Validator v;
  tools::addEngineChecks(v);
  return v;
}

const tools::Issue* findIssue(const tools::Report& r, const std::string& check, const std::string& path,
                              const std::string& textPart, tools::Issue::Severity s = tools::Issue::Severity::Error) {
  for (const tools::Issue& i : r.issues)
    if (i.check == check && i.path == path && i.severity == s && i.text.find(textPart) != std::string::npos) return &i;
  return nullptr;
}

}  // namespace

TEST_CASE("validator: a clean content folder passes every engine check") {
  TempDir t("clean");
  cleanTree(t.path);
  Loaded c(t.path);
  const tools::Report r = engineValidator().run(*c.fs, c.reg);
  INFO(r.text(true));
  CHECK(r.ok());
  CHECK(r.warnings() == 0);
  CHECK(r.checksRun.size() == 12);
  CHECK(r.checked.at("json") == 4);
  CHECK(r.checked.at("audio") == 1);
  CHECK(r.checked.at("models") == 1);
  // German has one text less than English: a note, not a fault.
  CHECK(findIssue(r, "strings", "lang/de.json", "not translated yet", tools::Issue::Severity::Note));
}

TEST_CASE("validator: finds what is wrong, says where, and names the check") {
  TempDir t("broken");
  cleanTree(t.path);
  const std::string d = t.path;
  put(d + "data/bad.json", "{\n  \"a\": 1,\n  \"b\": [1, 2\n}\n");
  put(d + "data/twice.json", R"({"sounds": {"x": {"volume": 1, "volume": 2}}})");
  put(d + "data/refs.json", R"({"music": "music/theme.ogg", "map": "Maps/Hill.lmap"})");
  put(d + "maps/hill.lmap", "not really a map");
  put(d + "audio/broken.wav", "RIFF....not a wave");
  put(d + "audio/banks/extra.json", R"({"sounds": {
      "boom": {"files": ["sfx/beep.wav"], "bus": "loud", "volume": "very", "rnage": 5},
      "hush": {"bus": "sfx"},
      "beep": {"files": ["sfx/gone.wav"]}}})");
  putBytes(d + "models/junk.glb", std::vector<uint8_t>(64, 7));
  put(d + "ui/broken.lua", "panel{\n  title = \"x\",\n  draw = function( end\n}\n");
  put(d + "lang/de.json", R"({"language": "de", "strings": {"hud": {"selected": "{count} gewählt", "helo": "Hallo"}}})");
  put(d + "notes/Read.txt", "a");
  put(d + "notes/read.txt", "b");
  put(d + "notes/con.txt", "c");
  put(d + "fonts/notafont.ttf", "hello, I am text pretending to be a font");
  Loaded c(d);
  const tools::Report r = engineValidator().run(*c.fs, c.reg);
  INFO(r.text(true));

  const tools::Issue* bad = findIssue(r, "json", "data/bad.json", "syntax error");
  REQUIRE(bad);
  CHECK(bad->where == "line 4, column 1");
  CHECK(bad->source.find("data/bad.json") != std::string::npos);
  const tools::Issue* twice = findIssue(r, "json", "data/twice.json", "given twice", tools::Issue::Severity::Warning);
  REQUIRE(twice);
  CHECK(twice->where == "/sounds/x: key 'volume'");
  CHECK(findIssue(r, "references", "data/refs.json", "'music/theme.ogg', which is not there"));
  const tools::Issue* cased = findIssue(r, "references", "data/refs.json", "spelt 'maps/hill.lmap'");
  REQUIRE(cased);
  CHECK(cased->where == "/map");
  CHECK(findIssue(r, "audio", "audio/broken.wav", ""));
  CHECK(findIssue(r, "sounds", "audio/banks/extra.json", "unknown field 'rnage'", tools::Issue::Severity::Warning));
  CHECK(findIssue(r, "sounds", "audio/banks/extra.json", "\"bus\" \"loud\"", tools::Issue::Severity::Warning));
  CHECK(findIssue(r, "sounds", "audio/banks/extra.json", "\"volume\" should be a number"));
  CHECK(findIssue(r, "sounds", "audio/banks/extra.json", "plays nothing", tools::Issue::Severity::Warning));
  CHECK(findIssue(r, "sounds", "audio/banks/extra.json", "'sfx/gone.wav', which is not in audio/"));
  CHECK(findIssue(r, "sounds", "audio/banks/extra.json", "replaces the sound of the same name from audio/audio.json",
                  tools::Issue::Severity::Note));
  CHECK(findIssue(r, "models", "models/junk.glb", ""));
  const tools::Issue* lua = findIssue(r, "scripts", "ui/broken.lua", "");
  REQUIRE(lua);
  CHECK(lua->where == "line 3");
  CHECK(findIssue(r, "strings", "lang/de.json", "not a key English has", tools::Issue::Severity::Warning));
  const tools::Issue* ph = findIssue(r, "strings", "lang/de.json", "other {names}", tools::Issue::Severity::Warning);
  REQUIRE(ph);
  CHECK(ph->where == "hud.selected");
  CHECK(findIssue(r, "paths", "notes/Read.txt", "differs only in upper and lower case"));
  CHECK(findIssue(r, "paths", "notes/read.txt", "differs only in upper and lower case"));
  CHECK(findIssue(r, "paths", "notes/con.txt", "Windows reserves"));
  CHECK(findIssue(r, "fonts", "fonts/notafont.ttf", "not a TrueType"));

  // Sorted by file, then place: the report reads the same every time.
  const tools::Report again = engineValidator().run(*c.fs, c.reg);
  CHECK(again.text(true).substr(0, again.text(true).rfind('\n', again.text(true).size() - 2)) ==
        r.text(true).substr(0, r.text(true).rfind('\n', r.text(true).size() - 2)));
  CHECK(std::is_sorted(r.issues.begin(), r.issues.end(), [](const tools::Issue& a, const tools::Issue& b) { return a.path < b.path; }));
  const json j = r.json();
  CHECK(j["errors"].get<size_t>() == r.errors());
  CHECK(j["issues"].size() == r.issues.size());
}

TEST_CASE("validator: only, skip and ignore choose what runs and what is looked at") {
  TempDir t("choose");
  cleanTree(t.path);
  put(t.path + "data/bad.json", "{");
  put(t.path + "wip/bad.json", "{");
  Loaded c(t.path);
  tools::Options o;
  o.only = {"json"};
  tools::Report r = engineValidator().run(*c.fs, c.reg, o);
  CHECK(r.checksRun == std::vector<std::string>{"json"});
  CHECK(r.errors() == 2);
  o.ignore = {"wip/"};
  r = engineValidator().run(*c.fs, c.reg, o);
  CHECK(r.errors() == 1);
  o = {};
  o.skip = {"json", "references"};
  r = engineValidator().run(*c.fs, c.reg, o);
  CHECK(r.ok());
  CHECK(std::find(r.checksRun.begin(), r.checksRun.end(), "json") == r.checksRun.end());
  // A game's own check joins in, and replaces one of the same name.
  tools::Validator v = engineValidator();
  v.add({"house", "says hello", [](tools::Context& ctx) { ctx.note("", "hello"); }});
  v.add({"house", "says hello twice", [](tools::Context& ctx) { ctx.note("", "hello again"); }});
  o = {};
  o.only = {"house"};
  r = v.run(*c.fs, c.reg, o);
  REQUIRE(r.issues.size() == 1);
  CHECK(r.issues[0].text == "hello again");
}

TEST_CASE("validator: names Windows refuses, and JSON errors taken apart") {
  CHECK(tools::portableNameProblem("units.json").empty());
  CHECK(tools::portableNameProblem("console.txt").empty());
  CHECK_FALSE(tools::portableNameProblem("CON").empty());
  CHECK_FALSE(tools::portableNameProblem("aux.json").empty());
  CHECK_FALSE(tools::portableNameProblem("lpt3.ogg").empty());
  CHECK_FALSE(tools::portableNameProblem("a:b").empty());
  CHECK_FALSE(tools::portableNameProblem("what?.md").empty());
  CHECK_FALSE(tools::portableNameProblem("trailing.").empty());
  std::string where, text;
  tools::splitJsonError("[json.exception.parse_error.101] parse error at line 3, column 7: syntax error while parsing value", where, text);
  CHECK(where == "line 3, column 7");
  CHECK(text == "syntax error while parsing value");
}

TEST_CASE("validator: Legion's checks catch broken rules, maps and campaigns over the real content") {
  auto fs = std::make_shared<vfs::Vfs>();
  fs->mountDir(LEGION_CONTENT_DIR, "base", 0);
  const int rules = fs->mountMemory("brokenrules", 2);
  fs->addMemoryFile(rules, "rules/units.json", R"({"units": [{"id": "ger_rifleman", "weapons": ["no_such_gun"]}]})");
  const int mod = fs->mountMemory("brokenmod", 1);
  fs->addMemoryFile(mod, "campaigns/demo/campaign.json",
                    R"({"title": "Demo", "faction": "usa", "missions": [{"id": "a", "map": "a.lmap", "briefing": "a.md"},
                                                                         {"id": "a", "map": "gone.lmap"}]})");
  fs->addMemoryFile(mod, "campaigns/demo/a.md", "# A\n");
  // A map with a unit the rules don't know, and one off the edge.
  {
    Map m(32, 32);
    std::vector<UnitPlacement> units = {{"ger_rifleman", 0, 5 * 256, 5 * 256}, {"martian_tripod", 1, 6 * 256, 6 * 256},
                                        {"ger_rifleman", 0, 1000, -128}};
    fs->addMemoryFile(mod, "campaigns/demo/a.lmap", encodeMap(m, units));
  }
  assets::Registry reg;
  reg.attach(fs);
  reg.rebuild();
  tools::Validator v;
  legion::addContentChecks(v);
  tools::Options o;
  o.ignore = {"campaigns/ger/", "campaigns/rus/", "campaigns/gbr/", "campaigns/tutorial/", "maps/", "scenarios/"};
  tools::Report r = v.run(*fs, reg, o);
  INFO(r.text(true));
  CHECK(findIssue(r, "rules", "rules/units.json", "unknown weapon 'no_such_gun'"));
  CHECK(findIssue(r, "maps", "campaigns/demo/a.lmap", "1 unit stands off the map"));
  // Unit types can be checked once the rules load.
  CHECK_FALSE(findIssue(r, "maps", "campaigns/demo/a.lmap", "martian_tripod"));
  fs->unmount(rules);
  reg.rebuild();
  r = v.run(*fs, reg, o);
  CHECK_FALSE(findIssue(r, "rules", "rules/units.json", ""));
  CHECK(findIssue(r, "maps", "campaigns/demo/a.lmap", "'martian_tripod', a unit the rules don't have"));
  CHECK(findIssue(r, "campaigns", "campaigns/demo/campaign.json", "'usa', a faction"));
  const tools::Issue* dup = findIssue(r, "campaigns", "campaigns/demo/campaign.json", "the id 'a' of an earlier mission");
  REQUIRE(dup);
  CHECK(dup->where == "mission 2");
  CHECK(findIssue(r, "campaigns", "campaigns/demo/campaign.json", "'gone.lmap', which is not"));
  CHECK(findIssue(r, "campaigns", "campaigns/demo/campaign.json", "has no briefing", tools::Issue::Severity::Warning));
}

TEST_CASE("validator: the game's own content and its mods have no errors") {
  for (bool withMod : {false, true}) {
    ContentLayers layers;
    layers.addRoot(LEGION_CONTENT_DIR);
    if (withMod) {
      const auto mods = scanMods({std::string(LEGION_CONTENT_DIR) + "../mods/"}, 1);
      REQUIRE_FALSE(mods.empty());
      std::string err;
      for (const ModInfo& m : mods) REQUIRE(layers.addMod(m, err));
    }
    assets::Registry reg;
    reg.attach(layers.fileSystem());
    reg.rebuild();
    tools::Validator v;
    tools::addEngineChecks(v);
    legion::addContentChecks(v);
    const tools::Report r = v.run(*layers.fileSystem(), reg);
    for (const tools::Issue& i : r.issues)
      if (i.severity == tools::Issue::Severity::Error) FAIL_CHECK(i.path, ": ", i.where, ": ", i.text, " [", i.check, "]");
    CHECK(r.checksRun.size() == 16);
    CHECK(r.checked.at("missions") >= 90);
    CHECK(r.checked.at("audio") >= 150);
    MESSAGE(std::string(withMod ? "with mods: " : "base: "), r.warnings(), " warnings, ", r.notes(), " notes, ", static_cast<int>(r.ms), " ms");
  }
}

TEST_CASE("cook: minified JSON keeps every value and the order of the keys") {
  const std::string in = "{\n  // a comment\n  \"zebra\": 1,\n  \"apple\": [1, 2.5, \"x\"],\n  \"mid\": {\"b\": true, \"a\": null}\n}\n";
  bool changed = false;
  const std::vector<uint8_t> out = tools::minifiedJson(std::vector<uint8_t>(in.begin(), in.end()), &changed);
  CHECK(changed);
  CHECK(std::string(out.begin(), out.end()) == R"({"zebra":1,"apple":[1,2.5,"x"],"mid":{"b":true,"a":null}})");
  const std::string broken = "{ not json";
  const std::vector<uint8_t> same = tools::minifiedJson(std::vector<uint8_t>(broken.begin(), broken.end()), &changed);
  CHECK_FALSE(changed);
  CHECK(std::string(same.begin(), same.end()) == broken);
}

TEST_CASE("cook: the same content cooks to the same bytes; cooking again redoes only what changed") {
  TempDir src("cook_src"), out("cook_out");
  cleanTree(src.path);
  put(src.path + "notes.txt~", "an editor's backup");
  put(src.path + "drafts/wip.json", "{}");
  tools::CookOptions o;
  o.exclude = {"drafts/"};
  tools::CookResult r1, r2;
  {
    Loaded c(src.path);
    c.reg.scanDependencies();
    REQUIRE(tools::cook(*c.fs, c.reg, out.path + "a", o, r1));
    REQUIRE(tools::cook(*c.fs, c.reg, out.path + "b", o, r2));
  }
  CHECK(r1.files == 9);
  CHECK(r1.written == 9);
  CHECK(r1.minified == 4);
  CHECK(r1.bytesOut < r1.bytesIn);
  CHECK(r1.contentHash == r2.contentHash);
  CHECK_FALSE(fs::exists(out.path + "a/notes.txt~"));
  CHECK_FALSE(fs::exists(out.path + "a/drafts/wip.json"));
  for (const auto& e : fs::recursive_directory_iterator(out.path + "a")) {
    if (!e.is_regular_file()) continue;
    const std::string rel = fs::relative(e.path(), out.path + "a").generic_string();
    INFO(rel);
    CHECK(slurp(e.path().string()) == slurp(out.path + "b/" + rel));
  }
  // The cooked folder is content the game reads, and checks out the same.
  {
    Loaded cooked(out.path + "a/");
    const tools::Report r = engineValidator().run(*cooked.fs, cooked.reg);
    INFO(r.text(true));
    CHECK(r.ok());
    CHECK(cooked.fs->exists("asset_index.json"));
    tools::AssetIndex idx;
    std::string err;
    REQUIRE(tools::readIndex(out.path + "a/asset_index.json", idx, err));
    CHECK(idx.entries.size() == 9);
    CHECK(idx.contentHash == r1.contentHash);
    const tools::AssetIndex::Entry* list = idx.find("data/list.json");
    REQUIRE(list);
    CHECK(list->type == "data");
    CHECK(std::find(list->uses.begin(), list->uses.end(), "art/swatch.png") != list->uses.end());
  }
  // Again into the same folder: nothing to do.
  {
    Loaded c(src.path);
    tools::CookResult r;
    REQUIRE(tools::cook(*c.fs, c.reg, out.path + "a", o, r));
    CHECK(r.written == 0);
    CHECK(r.reused == 9);
  }
  // One file changed, one gone, one new.
  put(src.path + "lang/en.json", R"({"language": "en", "strings": {"hud": {"hello": "Hi"}}})");
  fs::remove(src.path + "fonts/fake.ttf");
  put(src.path + "data/new.json", R"({"x": 1})");
  {
    Loaded c(src.path);
    tools::CookResult r;
    REQUIRE(tools::cook(*c.fs, c.reg, out.path + "a", o, r));
    CHECK(r.written == 2);
    CHECK(r.reused == 7);
    CHECK(r.removed == 1);
    CHECK(r.contentHash != r1.contentHash);
    CHECK_FALSE(fs::exists(out.path + "a/fonts"));
    CHECK(slurp(out.path + "a/lang/en.json") == R"({"language":"en","strings":{"hud":{"hello":"Hi"}}})");
    tools::AssetIndex before, after;
    std::string err;
    REQUIRE(tools::readIndex(out.path + "b/asset_index.json", before, err));
    REQUIRE(tools::readIndex(out.path + "a/asset_index.json", after, err));
    const tools::IndexDiff d = tools::diff(before, after);
    CHECK(d.added == std::vector<std::string>{"data/new.json"});
    CHECK(d.removed == std::vector<std::string>{"fonts/fake.ttf"});
    CHECK(d.changed == std::vector<std::string>{"lang/en.json"});
  }
}

TEST_CASE("cook: packs come out byte for byte the same, and the cook keeps out of other folders") {
  TempDir src("pack_src"), out("pack_out");
  cleanTree(src.path);
  Loaded c(src.path);
  tools::CookOptions o;
  o.manifest.id = "testcontent";
  o.manifest.version = "1.4";
  o.packFile = out.path + "one.pnpack";
  tools::CookResult r;
  REQUIRE(tools::cook(*c.fs, c.reg, out.path + "x", o, r));
  o.packFile = out.path + "two.pnpack";
  REQUIRE(tools::cook(*c.fs, c.reg, out.path + "y", o, r));
  CHECK(slurp(out.path + "one.pnpack") == slurp(out.path + "two.pnpack"));
  const pack::Report pr = pack::verify(out.path + "one.pnpack");
  CHECK(pr.ok);
  CHECK(pr.files == 10);  // the nine files and the index; never the cook's own notes
  pack::Manifest m;
  std::string err;
  REQUIRE(pack::readManifest(out.path + "one.pnpack", m, err));
  CHECK(m.id == "testcontent");
  CHECK(m.version == "1.4");
  CHECK(pack::engineAllows(m));
  // The pack is content too.
  {
    auto pfs = std::make_shared<vfs::Vfs>();
    REQUIRE(pfs->mountZip(out.path + "one.pnpack", "packed", 0, "", err) >= 0);
    assets::Registry preg;
    preg.attach(pfs);
    preg.rebuild();
    const tools::Report vr = engineValidator().run(*pfs, preg);
    INFO(vr.text(true));
    CHECK(vr.ok());
  }
  // A folder with someone else's files in it, or the content itself: refused.
  put(out.path + "theirs/precious.txt", "keep me");
  CHECK_FALSE(tools::cook(*c.fs, c.reg, out.path + "theirs", {}, r));
  CHECK(slurp(out.path + "theirs/precious.txt") == "keep me");
  CHECK_FALSE(tools::cook(*c.fs, c.reg, src.path + "cooked", {}, r));
  CHECK_FALSE(r.errors.empty());
  // --clean takes away only what the cook made.
  put(out.path + "x/mine.txt", "not cooked");
  tools::CookOptions clean;
  clean.clean = true;
  REQUIRE(tools::cook(*c.fs, c.reg, out.path + "x", clean, r));
  CHECK(r.written == 9);
  CHECK(fs::exists(out.path + "x/mine.txt"));
}

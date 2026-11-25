// Pioneer part 9b: the models behind the tool windows - the asset browser,
// the model viewer and the sound bank editor - tested without graphics.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "pioneer/assets/gltf.hpp"
#include "pioneer/assets/voxel_rig.hpp"
#include "pioneer/core/assets.hpp"
#include "pioneer/core/vfs.hpp"
#include "pioneer/tools/asset_browser_model.hpp"
#include "pioneer/tools/bank_document.hpp"
#include "pioneer/tools/model_view.hpp"
#include "pioneer/tools/validate.hpp"

using namespace pioneer;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct TempDir {
  std::string path;
  explicit TempDir(const char* name) {
    path = (fs::temp_directory_path() / (std::string("pioneer_toolmodels_") + name)).generic_string() + "/";
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
std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::vector<std::string> rowPaths(const tools::AssetBrowserModel& m) {
  std::vector<std::string> out;
  for (int i : m.rows()) out.push_back(m.nodes()[static_cast<size_t>(i)].path);
  return out;
}

// A block man, 6 x 4 x 18 voxels, cut onto the humanoid rig.
VoxelRig blockRig() {
  VoxelModel man(6, 4, 18);
  man.box(1, 1, 0, 5, 3, 8, 1);   // legs
  man.box(0, 0, 8, 6, 4, 15, 2);  // body and arms
  man.box(2, 1, 15, 4, 3, 18, 3); // head
  VoxelRig rig = humanoidRig(18);
  rig.origin[0] = -3;
  rig.origin[1] = -2;
  assignByRegion(rig, man);
  return rig;
}

}  // namespace

TEST_CASE("asset browser: a tree of folders with totals, filters, order and issues") {
  auto v = std::make_shared<vfs::Vfs>();
  const int base = v->mountMemory("base", 0);
  v->addMemoryFile(base, "rules/units.json", std::string(100, ' '));
  v->addMemoryFile(base, "rules/weapons.json", std::string(300, ' '));
  v->addMemoryFile(base, "audio/audio.json", R"({"sounds": {"rifle": {"files": ["sfx/rifle.ogg", "sfx/gone.ogg"]}}})");
  v->addMemoryFile(base, "audio/sfx/rifle.ogg", std::string(5000, 'o'));
  v->addMemoryFile(base, "audio/sfx/unused.ogg", std::string(10, 'o'));
  const int mod = v->mountMemory("veterans", 1);
  v->addMemoryFile(mod, "rules/units.json", std::string(50, ' '));
  assets::Registry reg;
  reg.attach(v);
  reg.rebuild();
  reg.scanDependencies();
  // A file named but not there (a scanner or a game records it).
  reg.addDependency(reg.find("audio/audio.json"), reg.ensure("audio/sfx/gone.ogg"));

  tools::AssetBrowserModel m;
  m.attach(reg);
  CHECK(m.refresh());
  CHECK_FALSE(m.refresh());  // nothing changed
  // Folders first; closed folders show only themselves.
  CHECK(rowPaths(m) == std::vector<std::string>{"audio/", "rules/"});
  const auto& root = m.nodes()[0];
  CHECK(root.files == 5);
  m.setOpen("audio/", true);
  CHECK(rowPaths(m) == std::vector<std::string>{"audio/", "audio/sfx/", "audio/audio.json", "rules/"});
  const int sfx = m.find("audio/sfx/");
  REQUIRE(sfx >= 0);
  CHECK(m.nodes()[static_cast<size_t>(sfx)].files == 2);
  CHECK(m.nodes()[static_cast<size_t>(sfx)].bytes == 5010);

  // Words and keywords.
  m.setFilter("kind:sound");
  m.refresh();
  CHECK(m.matches() == 3);  // the missing one too: it is a sound that should be there
  CHECK(rowPaths(m) == std::vector<std::string>{"audio/", "audio/sfx/", "audio/sfx/gone.ogg", "audio/sfx/rifle.ogg", "audio/sfx/unused.ogg"});
  m.setFilter("is:unused kind:sound");
  m.refresh();
  CHECK(rowPaths(m) == std::vector<std::string>{"audio/", "audio/sfx/", "audio/sfx/unused.ogg"});
  m.setFilter("is:layered");
  m.refresh();
  CHECK(rowPaths(m) == std::vector<std::string>{"rules/", "rules/units.json"});
  m.setFilter("from:veterans");
  m.refresh();
  CHECK(m.matches() == 1);
  m.setFilter("is:missing");
  m.refresh();
  CHECK(rowPaths(m) == std::vector<std::string>{"audio/", "audio/sfx/", "audio/sfx/gone.ogg"});
  CHECK(m.nodes()[static_cast<size_t>(m.find("audio/sfx/gone.ogg"))].missing);
  m.setFilter("kind:sond");
  m.refresh();
  CHECK(m.filterProblem().find("sond") != std::string::npos);

  // Biggest first.
  m.setFilter("");
  m.setSort(tools::AssetBrowserModel::Sort::Size, true);
  m.refresh();
  m.setOpen("rules/", true);
  const auto rows = rowPaths(m);
  CHECK(std::find(rows.begin(), rows.end(), "rules/weapons.json") < std::find(rows.begin(), rows.end(), "rules/units.json"));

  // The validator's issues, added up per folder; is:problem finds them.
  tools::Report r;
  tools::Issue i;
  i.path = "audio/audio.json";
  i.text = "names 'sfx/gone.ogg', which is not there";
  i.check = "references";
  r.issues.push_back(i);
  i.severity = tools::Issue::Severity::Warning;
  i.path = "audio/sfx/rifle.ogg";
  r.issues.push_back(i);
  m.setReport(r);
  m.refresh();
  CHECK(m.nodes()[static_cast<size_t>(m.find("audio/"))].errors == 1);
  CHECK(m.nodes()[static_cast<size_t>(m.find("audio/"))].warnings == 1);
  CHECK(m.nodes()[0].errors == 1);
  m.setFilter("is:error");
  m.refresh();
  CHECK(m.matches() == 1);
  CHECK(m.issuesOf("audio/audio.json").size() == 1);
}

TEST_CASE("asset browser: selection follows links and goes back and forth") {
  auto v = std::make_shared<vfs::Vfs>();
  const int base = v->mountMemory("base", 0);
  v->addMemoryFile(base, "campaigns/demo/campaign.json", R"({"missions": [{"map": "m1.lmap"}]})");
  v->addMemoryFile(base, "campaigns/demo/m1.lmap", "map");
  v->addMemoryFile(base, "lang/en.json", "{}");
  assets::Registry reg;
  reg.attach(v);
  reg.rebuild();
  tools::AssetBrowserModel m;
  m.attach(reg);
  m.refresh();
  CHECK_FALSE(m.canBack());
  CHECK(m.select("campaigns/demo/campaign.json"));
  CHECK(m.isOpen("campaigns/"));
  CHECK(m.isOpen("campaigns/demo/"));
  CHECK(m.select("campaigns/demo/m1.lmap"));
  CHECK(m.select("lang/en.json"));
  CHECK_FALSE(m.select("nowhere.json"));
  CHECK(m.back());
  CHECK(m.selected() == "campaigns/demo/m1.lmap");
  CHECK(m.back());
  CHECK(m.selected() == "campaigns/demo/campaign.json");
  CHECK_FALSE(m.back());
  CHECK(m.forward());
  // Going somewhere new from the middle drops the forward history.
  CHECK(m.select("lang/en.json"));
  CHECK_FALSE(m.canForward());
  m.closeAll();
  CHECK_FALSE(m.isOpen("campaigns/"));
  m.openAll();
  CHECK(m.isOpen("campaigns/demo/"));
}

TEST_CASE("asset browser: previews - JSON laid out, long files cut, binary refused") {
  const std::string j = R"({"b":1,"a":[1,2]})";
  bool cut = true;
  const std::string t = tools::AssetBrowserModel::previewText({j.begin(), j.end()}, "x.json", 100, &cut);
  CHECK_FALSE(cut);
  CHECK(t.find("\"b\": 1") < t.find("\"a\""));  // laid out, order kept
  std::string many;
  for (int i = 0; i < 50; ++i) many += "line " + std::to_string(i) + "\n";
  const std::string c = tools::AssetBrowserModel::previewText({many.begin(), many.end()}, "x.md", 10, &cut);
  CHECK(cut);
  CHECK(std::count(c.begin(), c.end(), '\n') == 10);
  const std::vector<uint8_t> bin = {'O', 'g', 'g', 'S', 0, 2, 0, 0};
  CHECK(tools::AssetBrowserModel::previewText(bin, "x.ogg", 10).empty());
  CHECK(tools::AssetBrowserModel::sizeText(812) == "812 B");
  CHECK(tools::AssetBrowserModel::sizeText(12595) == "12.3 KB");
  assets::Record r;
  r.path = "models/tank.glb";
  r.type = assets::typeOf(r.path);
  CHECK(tools::previewOf(r) == tools::Preview::Model);
  r.path = "audio/sfx/x.ogg";
  r.type = assets::typeOf(r.path);
  CHECK(tools::previewOf(r) == tools::Preview::Sound);
  r.path = "rules/units.json";
  r.type = assets::typeOf(r.path);
  CHECK(tools::previewOf(r) == tools::Preview::Text);
}

TEST_CASE("model view: a rigged figure walks; skinning moves the legs and keeps the head on") {
  const VoxelRig rig = blockRig();
  auto model = std::make_shared<ModelAsset>(rigModelAsset(rig, VoxelPalette{}, 1.0f / 16.0f, "blockman"));
  REQUIRE_FALSE(model->meshes.empty());
  CHECK(model->skeleton.size() == 15);
  CHECK(model->clips.size() == infantryPoses().size());
  // The figure stands on the ground, about 18 voxels tall.
  float lo[3], hi[3];
  model->bounds(lo, hi);
  CHECK(lo[2] == doctest::Approx(0.0f).epsilon(0.01));
  CHECK(hi[2] == doctest::Approx(18.0f / 16.0f).epsilon(0.01));
  // A round trip through .glb keeps it all.
  ModelAsset back;
  std::string err;
  REQUIRE(readGltfBytes(writeGlb(*model), "", back, err));
  CHECK(back.clips.size() == model->clips.size());
  CHECK(back.skeleton.size() == 16);  // with the y-up root joint glTF needs

  tools::ModelView v;
  v.setModel(model, "blockman");
  CHECK(v.name() == "blockman");
  CHECK(v.currentClip() != nullptr);
  CHECK(v.currentClip()->name == "walk");  // a figure starts walking
  CHECK(v.loop());
  const tools::ModelView::Stats s = v.stats();
  CHECK(s.joints == 15);
  CHECK(s.skinned == s.meshes);
  CHECK(s.size[2] == doctest::Approx(18.0f / 16.0f).epsilon(0.01));
  // The camera looks at the figure from outside it.
  CHECK(v.camera.distance > s.size[2]);
  CHECK(v.camera.target.z == doctest::Approx(9.0f / 16.0f).epsilon(0.05));

  // The rest pose is the figure as built.
  REQUIRE(v.setClip(-1));
  const auto rest = v.posedMeshes();
  for (size_t m = 0; m < rest.size(); ++m)
    for (size_t k = 0; k < rest[m].size(); ++k)
      for (int a = 0; a < 3; ++a) REQUIRE(rest[m][k].pos[a] == doctest::Approx(model->meshes[m].vertices[k].pos[a]).epsilon(0.001));

  // Half a stride on, the feet have moved and the head is where a head goes.
  REQUIRE(v.setClip("walk"));
  const uint64_t before = v.poseRevision();
  v.update(0.25f);
  const auto& walk = v.posedMeshes();
  CHECK(v.poseRevision() != before);
  float moved = 0, top = 0;
  for (size_t m = 0; m < walk.size(); ++m)
    for (size_t k = 0; k < walk[m].size(); ++k) {
      const float* a = walk[m][k].pos;
      const float* b = model->meshes[m].vertices[k].pos;
      if (b[2] < 4.0f / 16.0f) moved = std::max(moved, std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) + std::fabs(a[2] - b[2]));
      top = std::max(top, a[2]);
    }
  CHECK(moved > 0.5f / 16.0f);
  CHECK(top > 15.0f / 16.0f);
  // Bones: every joint but the root, as lines.
  const auto bones = v.bones();
  CHECK(bones.size() == 14);

  // Playing: loops wrap, a clip that doesn't loop holds its last frame, pausing stops time.
  const float d = v.duration();
  REQUIRE(d > 0);
  v.setTime(0);
  v.update(d * 1.5f);
  CHECK(v.time() == doctest::Approx(d * 0.5f).epsilon(0.01));
  REQUIRE(v.setClip("fall_back"));
  CHECK_FALSE(v.loop());
  v.update(100.0f);
  CHECK(v.time() == doctest::Approx(v.duration()));
  v.play(false);
  v.setTime(0);
  v.update(1.0f);
  CHECK(v.time() == 0);
  CHECK_FALSE(v.setClip("moonwalk"));
  CHECK_FALSE(v.setClip(99));
}

TEST_CASE("model view: the orbit camera turns, zooms and pans about its target") {
  tools::OrbitCamera c;
  const float lo[3] = {-1, -1, 0}, hi[3] = {1, 1, 2};
  c.frame(lo, hi);
  CHECK(c.target.z == doctest::Approx(1.0f));
  const float d0 = c.distance;
  CHECK((c.eye() - c.target).length() == doctest::Approx(d0));
  c.zoom(2);
  CHECK(c.distance < d0);
  c.zoom(-2);
  CHECK(c.distance == doctest::Approx(d0));
  c.orbit(0, 10);
  CHECK(c.pitch == doctest::Approx(1.5f));  // never over the top
  c.orbit(0, -10);
  CHECK(c.pitch == doctest::Approx(-1.5f));
  c.orbit(0, 1.5f + 0.3f);
  const Vec3f before = c.target;
  c.pan(0.1f, 0);
  CHECK((c.target - before).length() > 0);
  CHECK(std::fabs(c.target.z - before.z) < 0.5f * (c.target - before).length());  // sideways, mostly
  // The view puts the target in the middle of the screen.
  const Mat4 vp = c.projection(1.5f) * c.view();
  const Vec4f p = vp * Vec4f{c.target.x, c.target.y, c.target.z, 1};
  CHECK(p.x / p.w == doctest::Approx(0.0f).epsilon(0.001));
  CHECK(p.y / p.w == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("bank document: edits copy the base event into the target, undo, save and revert") {
  TempDir t("bank");
  const std::string base = t.path + "base/", mod = t.path + "mod/";
  put(base + "audio/audio.json", R"({"sounds": {
      "rifle": {"files": ["sfx/rifle.ogg"], "bus": "sfx", "volume": 0.4, "range": 10},
      "click": {"files": ["sfx/click.ogg"], "bus": "ui", "range": 0}}})");
  put(base + "audio/sfx/rifle.ogg", "o");
  put(base + "audio/sfx/rifle2.ogg", "o");
  put(base + "audio/sfx/click.ogg", "o");
  auto v = std::make_shared<vfs::Vfs>();
  v->mountDir(base, "base", 0);
  v->mountDir(mod, "mymod", 1);

  tools::BankDocument doc;
  doc.load(v);
  CHECK(doc.ids() == std::vector<std::string>{"click", "rifle"});
  CHECK(doc.row("rifle")->from == "base: audio/audio.json");
  std::string err;
  CHECK_FALSE(doc.set("rifle", "volume", 0.3, &err));  // no target yet
  CHECK(err.find("choose where") != std::string::npos);
  REQUIRE(doc.setTarget(mod, "audio/banks/edits.json", &err));

  REQUIRE(doc.set("rifle", "volume", 0.3, &err));
  CHECK(doc.dirty());
  const auto* r = doc.row("rifle");
  CHECK(r->inTarget);
  CHECK(r->changed);
  CHECK(r->event.volume == doctest::Approx(0.3f));
  CHECK(r->event.range == doctest::Approx(10.0f));  // the rest of the base event came with it
  CHECK(r->event.files == std::vector<std::string>{"sfx/rifle.ogg"});
  CHECK(r->below == "base: audio/audio.json");
  // Values are checked and kept within range.
  CHECK_FALSE(doc.set("rifle", "bus", "loud", &err));
  CHECK_FALSE(doc.set("rifle", "volume", "very", &err));
  CHECK_FALSE(doc.set("rifle", "colour", 1, &err));
  REQUIRE(doc.set("rifle", "limit", 1000, &err));
  CHECK(doc.row("rifle")->event.limit == 32);
  REQUIRE(doc.addFile("rifle", "sfx/rifle2.ogg", false, &err));
  CHECK_FALSE(doc.addFile("rifle", "sfx/rifle2.ogg", false, &err));  // once is enough
  REQUIRE(doc.addFile("rifle", "sfx/far.ogg", true, &err));
  CHECK(doc.row("rifle")->event.farFiles == std::vector<std::string>{"sfx/far.ogg"});
  // ... which isn't there: the check says so.
  const auto issues = doc.check("rifle");
  REQUIRE(issues.size() == 1);
  CHECK(issues[0].text.find("sfx/far.ogg") != std::string::npos);
  REQUIRE(doc.removeFile("rifle", 0, true));
  CHECK(doc.check("rifle").empty());
  REQUIRE(doc.moveFile("rifle", 1, -1));
  CHECK(doc.row("rifle")->event.files == std::vector<std::string>{"sfx/rifle2.ogg", "sfx/rifle.ogg"});

  // Undo walks it back, redo forward again.
  REQUIRE(doc.undo());
  CHECK(doc.row("rifle")->event.files == std::vector<std::string>{"sfx/rifle.ogg", "sfx/rifle2.ogg"});
  REQUIRE(doc.redo());
  CHECK(doc.row("rifle")->event.files.front() == "sfx/rifle2.ogg");

  // New sounds and copies.
  REQUIRE(doc.create("whistle", &err));
  CHECK_FALSE(doc.create("rifle", &err));
  CHECK_FALSE(doc.create("two words", &err));
  REQUIRE(doc.duplicate("click", "click_soft", &err));
  CHECK(doc.row("click_soft")->event.bus == audio::Bus::Ui);
  REQUIRE(doc.undo());
  CHECK(doc.row("click_soft") == nullptr);  // the copy was one step
  REQUIRE(doc.redo());

  // Saved: only what the mod changes, events by name, fields in order.
  REQUIRE(doc.save(err));
  CHECK_FALSE(doc.dirty());
  const json saved = json::parse(slurp(mod + "audio/banks/edits.json"));
  CHECK(saved["sounds"].size() == 3);
  CHECK(saved["sounds"].contains("rifle"));
  CHECK_FALSE(saved["sounds"].contains("click"));
  CHECK(saved["sounds"]["rifle"]["volume"] == 0.3);
  CHECK(json::parse(slurp(base + "audio/audio.json"))["sounds"]["rifle"]["volume"] == 0.4);  // the base untouched
  CHECK(doc.targetText().find("\"files\"") < doc.targetText().find("\"bus\""));

  // The game reading both layers hears the edit.
  {
    audio::SoundBank bank;
    for (const std::string& f : {base + "audio/audio.json", mod + "audio/banks/edits.json"}) REQUIRE(bank.addFile(f, err));
    CHECK(bank.find("rifle")->volume == doctest::Approx(0.3f));
    CHECK(bank.find("whistle") != nullptr);
  }

  // Loading again with the target mounted as a layer doesn't count it twice.
  tools::BankDocument again;
  again.load(v);
  REQUIRE(again.setTarget(mod, "audio/banks/edits.json", &err));
  CHECK(again.row("rifle")->inTarget);
  CHECK(again.row("rifle")->below == "base: audio/audio.json");
  CHECK_FALSE(again.dirty());
  // Revert: the base version is back; a sound only the mod had is gone.
  REQUIRE(again.revert("rifle"));
  CHECK_FALSE(again.row("rifle")->inTarget);
  CHECK(again.row("rifle")->event.volume == doctest::Approx(0.4f));
  REQUIRE(again.revert("whistle"));
  CHECK(again.row("whistle") == nullptr);
  CHECK(again.dirty());
  // A target that isn't a bank is refused and left alone.
  put(t.path + "odd/audio/banks/x.json", "[1, 2]");
  tools::BankDocument odd;
  CHECK_FALSE(odd.setTarget(t.path + "odd/", "audio/banks/x.json", &err));
  CHECK(slurp(t.path + "odd/audio/banks/x.json") == "[1, 2]");
}

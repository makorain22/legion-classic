// Legion Classic - the engine's tool windows in the game (Pioneer part 9):
// the asset browser, the model viewer and the sound bank editor, their
// console commands, and --tools-selftest.
//
//   tools.assets [path|filter]   the asset browser (F7)
//   tools.models [unit|file]     the model viewer: the game's units (infantry
//                                as figures on the rig, with every pose) and
//                                any .glb/.gltf/.vox of the content
//   tools.banks [sound]          the sound bank editor; edits go into one of
//                                the player's own mods
//   tools.validate [checks]      every content check, as pioneer_tool runs them
#include <cmath>
#include <filesystem>
#include <fstream>

#include <imgui.h>

#include "content_checks.hpp"
#include "game_audio.hpp"
#include "game_scene.hpp"
#include "pioneer/assets/gltf.hpp"
#include "pioneer/assets/voxel_rig.hpp"
#include "pioneer/core/console.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/tools/validate.hpp"
#include "settings.hpp"

using namespace pioneer;

namespace legion {

namespace {

// A unit's body (and turret) as a model, as model.export makes it.
std::shared_ptr<ModelAsset> unitModel(const SpriteCatalog& art, const UnitArt& ua, const std::string& name) {
  auto sourceOf = [&](const SpriteSheet& s) -> VoxelSourcePtr {
    const int f = s.frame(0, 0);
    return f >= 0 && f < static_cast<int>(art.frames.size()) ? art.frames[static_cast<size_t>(f)].voxel : nullptr;
  };
  const VoxelSourcePtr body = sourceOf(ua.body);
  if (!body) return nullptr;
  auto m = std::make_shared<ModelAsset>(voxelModelAsset(body->model, body->palette, body->voxelTiles, body->pivotX, body->pivotY, name));
  if (ua.hasTurret)
    if (const VoxelSourcePtr t = sourceOf(ua.turret)) {
      ModelAsset tm = voxelModelAsset(t->model, t->palette, t->voxelTiles, t->pivotX, t->pivotY, name + "_turret");
      for (MeshPart& part : tm.meshes) {
        for (MeshVertexFull& v : part.vertices) v.pos[0] += ua.turretOffset / static_cast<float>(LEPTONS_PER_TILE);
        m->meshes.push_back(std::move(part));  // the same two materials (paint, team)
      }
    }
  return m;
}

// An infantryman cut onto the standard rig, with every infantry pose.
std::shared_ptr<ModelAsset> figureModel(const SpriteCatalog& art, const UnitArt& ua, const std::string& name) {
  const int f = ua.body.frame(0, 0);
  if (f < 0 || f >= static_cast<int>(art.frames.size()) || !art.frames[static_cast<size_t>(f)].voxel) return nullptr;
  const VoxelSource& src = *art.frames[static_cast<size_t>(f)].voxel;
  VoxelRig rig = humanoidRig(static_cast<float>(std::max(12, src.model.sizeZ())));
  rig.origin[0] = -src.pivotX;
  rig.origin[1] = -src.pivotY;
  assignByRegion(rig, src.model);
  if (rig.parts.empty()) return nullptr;
  return std::make_shared<ModelAsset>(rigModelAsset(rig, src.palette, src.voxelTiles, name));
}

}  // namespace

void GameScene::setupToolWindows() {
  // What only the game can do, for the engine's windows.
  toolHooks_.exportDir = exportsDir();
  toolHooks_.playFile = [this](const std::string& path) {
    if (audio_) audio_->previewFile(content_.fileSystem()->diskPath(path));
  };
  toolHooks_.playEvent = [this](const audio::SoundEvent& e) {
    if (audio_) audio_->previewEvent(e, [this](const std::string& rel) { return bankWin_.document().diskPath(rel); });
  };
  toolHooks_.stopSounds = [this] {
    if (audio_) audio_->stopPreview();
  };
  toolHooks_.validate = [this] {
    tools::Validator v;
    tools::addEngineChecks(v);
    addContentChecks(v);
    return v.run(*content_.fileSystem(), assets_);
  };
  refreshToolSources();
}

void GameScene::refreshToolSources() {
  modelWin_.clearSources();
  for (size_t i = 0; i < types_.size() && i < unitArt_.size(); ++i) {
    const UnitTypeDef& d = types_.get(static_cast<int>(i));
    if (d.hidden) continue;
    const std::string id = d.id;
    const size_t index = i;
    // The rules can change under a source (a mod switch) before the list is made again.
    if (d.infantry())
      modelWin_.addSource(id + " (figure)", [this, index, id] {
        return index < unitArt_.size() ? figureModel(art_, unitArt_[index], id) : nullptr;
      });
    modelWin_.addSource(id, [this, index, id] { return index < unitArt_.size() ? unitModel(art_, unitArt_[index], id) : nullptr; });
  }
  // Edits to sounds go into one of the player's own mods.
  std::vector<ui::SoundBankWindow::Target> targets;
  for (const ModInfo& m : mods_) {
    if (!m.user || m.packed) continue;
    const bool on = std::find(content_.modIds().begin(), content_.modIds().end(), m.id) != content_.modIds().end();
    targets.push_back({m.name.empty() ? m.id : m.name + " (" + m.id + ")", m.dir, on});
  }
  bankWin_.setTargets(std::move(targets));
}

void GameScene::drawToolWindows() {
  if (showAssets_) assetWin_.draw(&showAssets_, assets_, toolHooks_, &modelWin_, &showModels_, &bankWin_, &showBanks_);
  if (showModels_) modelWin_.draw(&showModels_, content_.fileSystem().get(), &assets_, toolHooks_);
  if (showBanks_) bankWin_.draw(&showBanks_, content_.fileSystem(), &assets_, toolHooks_);
}

void GameScene::registerToolCommands() {
  Console& con = Console::global();
  con.addCommand("tools.assets", "tools.assets [path|filter] - the asset browser: every file of the content, where it comes from, "
                                 "what it uses and what uses it, its problems, a look inside (F7).",
                 [this](const ConsoleArgs& a, Console& c) {
                   showAssets_ = a.size() < 2 ? !showAssets_ : true;
                   if (a.size() >= 2) {
                     const std::string what = a.rest(1);
                     if (!assetWin_.model().select(what)) assetWin_.model().setFilter(what);
                   }
                   c.printFmt("asset browser %s", showAssets_ ? "open" : "closed");
                 });
  con.addCommand("tools.models", "tools.models [unit|file] - the model viewer: a unit (infantry also as a figure with every pose), "
                                 "or a .glb, .gltf or .vox of the content.",
                 [this](const ConsoleArgs& a, Console& c) {
                   showModels_ = a.size() < 2 ? !showModels_ : true;
                   if (a.size() < 2) return;
                   const std::string what = a.str(1);
                   std::string err;
                   const bool ok = modelWin_.openSource(what + " (figure)") || modelWin_.openSource(what, &err) ||
                                   modelWin_.openFile(*content_.fileSystem(), what, &err);
                   if (!ok) return c.error(err);
                   const auto s = modelWin_.view().stats();
                   c.printFmt("%s: %zu triangles, %zu joints, %zu animations", modelWin_.view().name().c_str(), s.triangles, s.joints, s.clips);
                 });
  con.addCommand("tools.banks", "tools.banks [sound] - the sound bank editor: every sound as the game layers them; edits go into "
                                "a bank file in one of your own mods.",
                 [this](const ConsoleArgs& a, Console& c) {
                   showBanks_ = a.size() < 2 ? !showBanks_ : true;
                   if (a.size() >= 2) {
                     bankWin_.reload(content_.fileSystem());
                     if (!bankWin_.select(a.str(1))) return c.error("no sound called " + a.str(1));
                   }
                 });
  con.addCommand("tools.validate", "tools.validate [check ...] - runs the content checks (as pioneer_tool validate does) over the "
                                   "base game and the enabled mods, and lists what they find.",
                 [this](const ConsoleArgs& a, Console& c) {
                   tools::Validator v;
                   tools::addEngineChecks(v);
                   addContentChecks(v);
                   tools::Options o;
                   for (size_t i = 1; i < a.size(); ++i) {
                     if (!v.find(a.str(i))) return c.error("no check called " + a.str(i));
                     o.only.push_back(a.str(i));
                   }
                   const tools::Report r = v.run(*content_.fileSystem(), assets_, o);
                   size_t shown = 0;
                   for (const tools::Issue& i : r.issues) {
                     if (i.severity == tools::Issue::Severity::Note) continue;
                     if (++shown > 40) break;
                     const std::string line = i.path + ": " + (i.where.empty() ? "" : i.where + ": ") + i.text + " [" + i.check + "]";
                     if (i.severity == tools::Issue::Severity::Error) c.error(line);
                     else c.print(line);
                   }
                   c.printFmt("%zu errors, %zu warnings, %zu notes (%.0f ms)%s", r.errors(), r.warnings(), r.notes(), r.ms,
                              shown > 40 ? " - the asset browser lists them all" : "");
                   assetWin_.model().setReport(r);
                 });
}

// --tools-selftest: the three windows through the real game.
void GameScene::runToolsSelfTest() {
  auto check = [&](bool ok, const std::string& what) {
    if (!ok) ++selfTestFailures_;
    PNR_INFO("TOOLS SELFTEST %s: %s", ok ? "ok  " : "FAIL", what.c_str());
  };
  const std::string modId = "selftest_tools";
  const std::string modDir = userModsDir() + modId + "/";
  if (frame_ == 10) {
    // The asset browser: the content, a link followed, the checks.
    showAssets_ = true;
    tools::AssetBrowserModel& m = assetWin_.model();
    m.attach(assets_);
    m.refresh();
    check(m.nodes().size() > 100 && m.nodes()[0].files > 300, "the browser shows the content: " + std::to_string(m.nodes()[0].files) + " files");
    check(m.select("campaigns/tutorial/campaign.json"), "select a campaign");
    const assets::Record* camp = assets_.get("campaigns/tutorial/campaign.json");
    check(camp && camp->uses.size() == 8, "it uses its four maps and four briefings");
    assetWin_.validate(toolHooks_);
    const tools::Report* r = m.report();
    check(r && r->checksRun.size() == 16 && r->errors() == 0,
          "every check runs from the browser, no errors: " + (r ? std::to_string(r->errors()) + " errors" : std::string("no report")));
    m.setFilter("kind:sound rifle");
    m.refresh();
    check(m.matches() >= 5, "the filter finds the rifle recordings: " + std::to_string(m.matches()));
  }
  if (frame_ == 14) {
    // The model viewer: a grenadier on the rig, walking.
    showModels_ = true;
    std::string err;
    check(modelWin_.openSource("ger_rifleman (figure)", &err), "open the grenadier as a figure: " + err);
    tools::ModelView& v = modelWin_.view();
    const auto s = v.stats();
    check(s.joints == 15 && s.clips >= 10 && s.triangles > 50, "a skeleton of 15 joints and the infantry poses: " + std::to_string(s.triangles) + " triangles");
    check(v.currentClip() && v.currentClip()->name == "walk", "he walks");
    v.options.skeleton = true;
    v.options.grid = true;
    check(modelWin_.openSource("ger_medium_tank", &err) || modelWin_.openSource("ger_heavy_tank", &err), "a tank opens too: " + err);
    check(modelWin_.openSource("ger_rifleman (figure)", &err), "and back to the grenadier");
  }
  if (frame_ == 24) {
    // A few frames of walking have been drawn.
    Image shot;
    const bool drew = modelWin_.snapshot(shot);
    size_t lit = 0;
    if (drew)
      for (const Rgba& p : shot.px) lit += (p.r > 70 || p.g > 70 || p.b > 70) ? 1 : 0;
    check(drew && shot.w > 100 && lit > 200, "the viewer drew the figure: " + std::to_string(lit) + " bright pixels");
    check(modelWin_.renderer().trianglesLastFrame() > 50, "triangles drawn: " + std::to_string(modelWin_.renderer().trianglesLastFrame()));
    check(modelWin_.view().time() > 0, "the walk moves on with the frames");
    if (drew) shot.savePng(app_.userDir() + "tools_selftest_model.png");
    // The sound bank editor: a mod of the player's own to write to.
    std::error_code ec;
    std::filesystem::remove_all(modDir, ec);
    ModInfo made;
    std::string err;
    check(createMod(userModsDir(), modId, "Tools selftest", made, err), "make a mod to hold sound edits: " + err);
    rescanMods();
    refreshToolSources();
    showBanks_ = true;
    tools::BankDocument& doc = bankWin_.document();
    bankWin_.reload(content_.fileSystem());
    check(doc.size() > 30, "the editor lists the game's sounds: " + std::to_string(doc.size()));
    check(doc.setTarget(modDir, "audio/banks/edits.json", &err), "edits go into the mod: " + err);
    check(doc.set("rifle", "volume", 0.25, &err), "turn the rifle down: " + err);
    check(doc.addFile("rifle", "rec/rifle_far_0.ogg", false, &err), "give it another recording: " + err);
    check(doc.undo() && doc.row("rifle")->event.files.size() == 5 && doc.redo(), "undo and redo");
    check(doc.check("rifle").empty(), "the edited rifle checks out");
    check(doc.save(err), "save: " + err);
    std::ifstream in(modDir + "audio/banks/edits.json");
    const nlohmann::json saved = nlohmann::json::parse(in, nullptr, false);
    check(saved.is_object() && saved["sounds"].size() == 1 && saved["sounds"]["rifle"]["volume"] == 0.25,
          "the mod holds only the rifle: " + (saved.is_object() ? saved.dump().substr(0, 80) : std::string("unreadable")));
    if (audio_) toolHooks_.playEvent(doc.row("rifle")->event);
    bankWin_.select("rifle");
    assetWin_.model().setFilter("");
    assetWin_.model().select("audio/audio.json");
    check(setMods({modId}, err), "switch the mod on: " + err);
  }
  if (frame_ == 30) {
    // The banks the game reads now, in its order (and, with a sound card, what it has loaded).
    audio::SoundBank bank;
    std::string berr;
    for (const std::string& f : GameAudio::bankFilesIn(content_.all("audio/"))) bank.addFile(f, berr);
    const audio::SoundEvent* rifle = bank.find("rifle");
    check(rifle && std::fabs(rifle->volume - 0.25f) < 0.001f && rifle->files.size() == 6,
          "the game's banks have the edited rifle once the mod is on");
    if (audio_ && audio_->deviceOpen()) {
      const audio::SoundEvent* live = audio_->bank().find("rifle");
      check(live && std::fabs(live->volume - 0.25f) < 0.001f, "and the sound it plays is the edited one");
    }
    std::string err;
    check(setMods({}, err), "switch the mod off: " + err);
    std::error_code ec;
    std::filesystem::remove_all(modDir, ec);
    rescanMods();
    refreshToolSources();
    PNR_INFO("TOOLS SELFTEST %s (%d failures)", selfTestFailures_ ? "FAILED" : "PASSED", selfTestFailures_);
    // The last frame keeps the windows on screen for --screenshot.
    quit_ = true;
  }
}

}  // namespace legion

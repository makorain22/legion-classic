// Legion Classic - content layers and mods for the game scene: loading rules
// through the enabled mods, switching mods, putting edited rules into a running
// battle and rebuilding the art.
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "editor.hpp"
#include "game_audio.hpp"
#include "game_scene.hpp"
#include "rules_editor.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/core/pack.hpp"
#include "pioneer/core/version.hpp"

using namespace pioneer;

namespace legion {

void GameScene::rescanMods() {
  std::string baseMods = contentDir_ + "../mods/";
  std::error_code ec;
  std::filesystem::create_directories(userModsDir(), ec);
  mods_ = scanMods({baseMods, userModsDir()}, 1);
}

bool GameScene::loadRules(std::string& error) {
  rescanMods();
  std::string warnings;
  const std::vector<std::string> order = resolveLoadOrder(settings_.mods, mods_, warnings);
  modWarnings_ = warnings;
  ContentLayers layers;
  layers.setCacheDir(app_.userDir() + "cache/");
  layers.addRoot(contentDir_);
  for (const std::string& id : order) {
    std::string why;
    if (!layers.addMod(*findMod(mods_, id), why)) {
      modWarnings_ += "Mod '" + id + "' can't be opened: " + why + "\n";
      PNR_WARN("Mod %s can't be opened: %s", id.c_str(), why.c_str());
    }
  }
  nlohmann::json docs[kRuleFileCount];
  for (int i = 0; i < kRuleFileCount; ++i)
    if (!layers.loadRules(kRuleFiles[i], docs[i], error)) return false;
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  if (!loadRuleSet(docs[0], docs[1], docs[2], weapons, types, error)) return false;
  // Nothing may be running on the old rules from here on (start-up, or a mod switch
  // that replaces the world straight after).
  weapons_ = std::move(weapons);
  types_ = std::move(types);
  content_ = std::move(layers);
  for (int i = 0; i < kRuleFileCount; ++i) rules_[i] = std::move(docs[i]);
  rulesHash_ = pioneer::rulesHash(rules_);
  rulesEditedLive_ = false;
  refreshAssets();
  trenchTypes_.assign(types_.size(), false);
  for (size_t i = 0; i < types_.size(); ++i) trenchTypes_[i] = types_.get(static_cast<int>(i)).model == "trench";
  bridgeTypes_.assign(types_.size(), false);
  for (size_t i = 0; i < types_.size(); ++i) bridgeTypes_[i] = types_.get(static_cast<int>(i)).bridge;
  if (!order.empty()) {
    std::string list;
    for (const std::string& id : order) list += (list.empty() ? "" : ", ") + id;
    PNR_INFO("Mods: %s (rules %s)", list.c_str(), rulesHash_.c_str());
  }
  return true;
}

// ---------------------------------------------------------------------------
// The asset registry and hot reload
// ---------------------------------------------------------------------------
void GameScene::refreshAssets() {
  assets_.attach(content_.fileSystem());
  assets_.rebuild();
  const size_t links = assets_.scanDependencies();
  PNR_DEBUG_CH(Assets, "%zu assets, %zu links between them", assets_.all().size(), links);
}

void GameScene::watchContent() {
  // Automated runs look at the disk only when told to (content.check), so
  // they behave the same on every machine.
  hotReload_.setEnabled(!app_.offscreen());
  hotReload_.watch("rules", "rules/", ".json", [this](const std::vector<vfs::Change>&) { reloadRulesFromDisk(); });
  hotReload_.watch("sounds", "audio/", "", [this](const std::vector<vfs::Change>&) { reloadSounds(); });
  hotReload_.watch("campaigns", "campaigns/", "", [this](const std::vector<vfs::Change>&) {
    loadCampaigns();
    PNR_INFO_CH(Content, "Campaign list read again: %zu campaigns", campaigns_.size());
  });
  hotReload_.watch("sprites", "art/", ".png", [this](const std::vector<vfs::Change>&) {
    std::string err;
    if (!reloadArt(err)) PNR_WARN_CH(Content, "Sprites not reloaded: %s", err.c_str());
    else PNR_INFO_CH(Content, "Sprites reloaded from disk");
  });
}

void GameScene::reloadRulesFromDisk() {
  nlohmann::json docs[kRuleFileCount];
  std::string error;
  for (int i = 0; i < kRuleFileCount; ++i)
    if (!content_.loadRules(kRuleFiles[i], docs[i], error)) {
      // Half-saved or mistyped: keep playing on the rules we have.
      PNR_WARN_CH(Content, "Rules not reloaded: %s", error.c_str());
      showStatus("Rules on disk can't be read: " + error, 6.0f);
      return;
    }
  if (pioneer::rulesHash(docs) == rulesHash_) return;  // saved again without a change
  std::string notes;
  if (!applyRules(docs, notes, error)) {
    PNR_WARN_CH(Content, "Rules not reloaded: %s", error.c_str());
    showStatus("Rules on disk were not taken in: " + error, 6.0f);
    return;
  }
  if (rulesEditor_) rulesEditor_->reset();
  showStatus("Rules reloaded from disk.", 4.0f);
  PNR_INFO_CH(Content, "Rules reloaded from disk (%s)%s%s", rulesHash_.c_str(), notes.empty() ? "" : ": ",
              notes.c_str());
}

void GameScene::reloadSounds() {
  if (opt_.noSound || (app_.offscreen() && !opt_.forceSound)) return;
  // The mixer's sounds are loaded once per GameAudio: start a fresh one.
  audio_.reset();
  audio_ = std::make_unique<GameAudio>();
  std::string err;
  if (!audio_->init(content_.all("audio/"), err)) {
    PNR_WARN_CH(Content, "Sound off: %s", err.c_str());
    return;
  }
  applySettings();  // the volumes
  audio_->setBiome(map_.biome);
  if (mode_ != Mode::Menu) audio_->newBattle();
  PNR_INFO_CH(Content, "Sounds reloaded from disk");
}

bool GameScene::packMod(const std::string& modId, std::string& file, std::string& error) {
  rescanMods();
  const ModInfo* m = findMod(mods_, modId);
  if (!m) {
    error = "there is no mod called '" + modId + "'";
    return false;
  }
  if (m->packed) {
    error = "'" + m->name + "' is a pack already (" + m->file + ")";
    return false;
  }
  pack::Manifest pm;
  pm.id = m->id;
  pm.engine = std::string(">=") + kEngineVersion;
  file = exportsDir() + m->id + "-" + m->version + pack::kExtension;
  return pack::build(m->dir, file, pm, error);
}

ArtOptions GameScene::artOptions() const {
  ArtOptions o;
  o.overrideDirs = content_.all("art/");  // the base's art folder, then each mod's
  for (std::string& d : o.overrideDirs) d.pop_back();
  o.exportDir = opt_.exportSprites;
  std::error_code ec;
  const std::string pack = contentDir_ + "art/blitzkrieg";
  if (!opt_.noArtPack && settings_.artPack && std::filesystem::exists(pack + "/sprites.json", ec))
    o.packDirs.push_back(pack);
  return o;
}

bool GameScene::setMods(const std::vector<std::string>& enabled, std::string& error) {
  const std::vector<std::string> before = settings_.mods;
  settings_.mods = enabled;
  if (!loadRules(error)) {
    settings_.mods = before;
    std::string again;
    if (!loadRules(again)) PNR_ERROR("Can't go back to the previous mods: %s", again.c_str());
    return false;
  }
  saveSettings();
  if (!reloadArt(error)) return false;
  rulesEditor_->reset();
  toMainMenu();  // a fresh battle on the new rules behind the menu
  return true;
}

bool GameScene::reloadArt(std::string& error) {
  SpriteCatalog fresh;
  const ArtOptions o = artOptions();
  if (!buildPlaceholderArt(fresh, error, o)) return false;
  art_ = std::move(fresh);
  artPack_ = !o.packDirs.empty();
  renderer_.setAtlas(art_.atlas, art_.frames);
  frameMesh_.assign(art_.frames.size(), -2);
  renderer_.setWhiteFrame(art_.white);
  std::string err;
  if (!pickUnitArt(err)) PNR_WARN("%s", err.c_str());
  return true;
}

// Each unit type's sprites: the art for this map's biome when there is any
// (winter paint on snow), else the faction's, else the German model.
bool GameScene::artFor(const UnitTypeDef& t, UnitArt& out) const {
  const std::string biome = std::string("~") + biomeId(map_.biome);
  const std::string key = t.model + "@" + t.faction;
  for (const std::string& k : {key + biome, key, t.model + "@ger" + biome, t.model + "@ger"}) {
    auto it = art_.units.find(k);
    if (it != art_.units.end()) {
      out = it->second;
      return true;
    }
  }
  return false;
}

bool GameScene::pickUnitArt(std::string& error) {
  unitArt_.clear();
  for (const UnitTypeDef& t : types_.all()) {
    UnitArt a;
    if (!artFor(t, a)) {
      // A mod naming a model that doesn't exist: draw a rifleman rather than refuse to run.
      error = "unit '" + t.id + "' uses unknown model '" + t.model + "'";
      PNR_WARN("%s", error.c_str());
      auto it = art_.units.find("rifleman@ger");
      if (it == art_.units.end()) return false;
      a = it->second;
    }
    unitArt_.push_back(a);
  }
  // Infantry dug in: the same men in a trench or behind sandbags, where the
  // art has them.
  postArt_.assign(types_.size(), 0);
  dugArt_.assign(types_.size(), UnitArt{});
  bagArt_.assign(types_.size(), UnitArt{});
  for (size_t i = 0; i < types_.size(); ++i) {
    const UnitTypeDef& t = types_.get(static_cast<int>(i));
    if (!t.infantry()) continue;
    UnitTypeDef v = t;
    v.model = t.model + "_dug";
    if (artFor(v, dugArt_[i])) postArt_[i] |= 1;
    v.model = t.model + "_bags";
    if (artFor(v, bagArt_[i])) postArt_[i] |= 2;
  }
  return true;
}

bool GameScene::applyRules(const nlohmann::json (&docs)[kRuleFileCount], std::string& notes, std::string& error) {
  nlohmann::json next[kRuleFileCount] = {docs[0], docs[1], docs[2]};
  notes.clear();
  stabiliseRules(next, rules_, notes);
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  if (!loadRuleSet(next[0], next[1], next[2], weapons, types, error)) return false;
  for (const UnitTypeDef& t : types.all()) {
    UnitArt a;
    if (!artFor(t, a)) {
      error = "'" + t.id + "' uses model '" + t.model + "', which has no art";
      return false;
    }
  }
  // The battle holds references to these two registries: it sees the new rules at once.
  weapons_ = std::move(weapons);
  types_ = std::move(types);
  for (int i = 0; i < kRuleFileCount; ++i) rules_[i] = std::move(next[i]);
  rulesHash_ = pioneer::rulesHash(rules_);
  rulesEditedLive_ = true;
  trenchTypes_.assign(types_.size(), false);
  for (size_t i = 0; i < types_.size(); ++i) trenchTypes_[i] = types_.get(static_cast<int>(i)).model == "trench";
  bridgeTypes_.assign(types_.size(), false);
  for (size_t i = 0; i < types_.size(); ++i) bridgeTypes_[i] = types_.get(static_cast<int>(i)).bridge;
  std::string err;
  pickUnitArt(err);
  if (sim_) sim_->rulesChanged();
  pruneSelection();
  replayValid_ = false;  // a replay can't follow rules changed half-way
  return true;
}

bool GameScene::creationToolOpen() const {
  return rulesEditor_ && rulesEditor_->isOpen();
}

}  // namespace legion

namespace legion {

// --mods-selftest: the creation tools through the real game. Edits rules live,
// saves them as a mod, switches the mod on and off, checks saves refuse the
// wrong mods.
void GameScene::runModsSelfTest() {
  auto check = [&](bool ok, const std::string& what) {
    if (!ok) ++selfTestFailures_;
    PNR_INFO("MODS SELFTEST %s: %s", ok ? "ok  " : "FAIL", what.c_str());
  };
  const std::string modId = "selftest_mod";
  std::string msg, err;
  if (frame_ == 10) {
    std::error_code ec;
    std::filesystem::remove_all(userModsDir() + modId, ec);
    settings_.mods.clear();
    RulesEditor& re = *rulesEditor_;
    re.open();
    nlohmann::json* rifle = re.entry(RulesEditor::Units, "ger_rifleman");
    check(rifle != nullptr, "the rules editor finds the grenadier");
    if (!rifle) return;
    (*rifle)["health"] = 321;
    if (nlohmann::json* w = re.entry(RulesEditor::Weapons, "rifle")) (*w)["damage"] = 77;
    re.edited();
    check(re.problem().empty(), "the edited rules check out: " + re.problem());
    check(re.apply(msg), "apply to the running battle: " + msg);
    const int t = types_.find("ger_rifleman");
    check(t >= 0 && types_.get(t).maxHealth == 321, "the battle uses the new health");
    check(weapons_.get(weapons_.find("rifle")).damage == 77, "the battle uses the new rifle");
    // A unit deleted mid-battle stays for what is on the map, but nobody builds it.
    const int sniper = types_.find("rus_sniper");
    const nlohmann::json keep = *re.entry(RulesEditor::Units, "rus_sniper");
    nlohmann::json& list = re.working(1)["units"];
    list.erase(std::remove_if(list.begin(), list.end(), [](const nlohmann::json& e) { return e["id"] == "rus_sniper"; }),
               list.end());
    re.edited();
    check(re.apply(msg), "apply a deletion: " + msg);
    check(types_.find("rus_sniper") == sniper && types_.get(sniper).hidden, "a deleted type stays, hidden, at its number");
    check(!sim_->canProduce(1, static_cast<uint16_t>(sniper)), "nobody can build the deleted type");
    list.push_back(keep);
    re.edited();
    check(re.apply(msg) && types_.find("rus_sniper") == sniper && !types_.get(sniper).hidden, "and it comes back in its place");
    // A broken edit is caught before it reaches the battle.
    (*re.entry(RulesEditor::Units, "ger_rifleman"))["weapons"] = nlohmann::json::array({"no_such_gun"});
    re.edited();
    check(!re.problem().empty() && !re.apply(msg), "an unknown weapon is refused: " + re.problem());
    (*re.entry(RulesEditor::Units, "ger_rifleman"))["weapons"] = nlohmann::json::array({"rifle"});
    re.edited();
    check(re.problem().empty(), "fixed again");
  }
  if (frame_ == 12) {
    ModInfo made;
    check(createMod(userModsDir(), modId, "Self-test mod", made, err), "make a mod: " + err);
    rescanMods();
    check(rulesEditor_->saveToMod(modId, msg), "save the rules into it: " + msg);
    std::ifstream in(made.dir + "rules/units.json");
    const nlohmann::json patch = nlohmann::json::parse(in, nullptr, false);
    check(patch.is_object() && patch["units"].size() == 1 && patch["units"][0]["health"] == 321,
          "the mod holds only the change: " + (patch.is_object() ? patch.dump() : std::string("unreadable")));
    check(std::filesystem::exists(made.dir + "rules/weapons.json") && !std::filesystem::exists(made.dir + "rules/buildings.json"),
          "one file per changed rule file");
    mode_ = Mode::Game;
    check(saveGame(savesDir_ + "selftest_mods.lsav", err), "save a game made before the mod is on: " + err);
  }
  if (frame_ == 14) {
    check(setMods({modId}, err), "switch the mod on: " + err);
    const int t = types_.find("ger_rifleman");
    check(t >= 0 && types_.get(t).maxHealth == 321, "the mod's rules are loaded");
    check(content_.modIds().size() == 2 && content_.modIds()[1] == modId, "the mod is in the load order");
    check(!loadGame(savesDir_ + "selftest_mods.lsav", err) && err.find("made with") != std::string::npos,
          "a save made without the mod is refused: " + err);
  }
  if (frame_ == 18) {
    check(setMods({}, err), "switch the mod off: " + err);
    check(types_.get(types_.find("ger_rifleman")).maxHealth == 100, "back to the game's own rules");
    check(loadGame(savesDir_ + "selftest_mods.lsav", err), "the save loads again without the mod: " + err);
  }
  if (frame_ >= 20) runDataSelfTest();
}

// The second half of --mods-selftest (Pioneer part 2): hot reload, the asset
// registry and packs, through the real game. (A function of its own: one
// very long function is slow to compile.)
void GameScene::runDataSelfTest() {
  auto check = [&](bool ok, const std::string& what) {
    if (!ok) ++selfTestFailures_;
    PNR_INFO("MODS SELFTEST %s: %s", ok ? "ok  " : "FAIL", what.c_str());
  };
  const std::string modId = "selftest_mod";
  std::string err;
  const std::string modDir = userModsDir() + modId + "/";
  const std::string packFile = userModsDir() + modId + pack::kExtension;
  if (frame_ == 20) {
    check(setMods({modId}, err), "the mod on again: " + err);
    hotReload_.checkNow(content_.fileSystem(), &assets_);  // what is on disk now is the starting point
    const assets::Record* rec = assets_.get("rules/units.json");
    check(rec && rec->present && rec->layers == 2 && rec->source == modId,
          "the asset registry knows the mod's copy of rules/units.json wins");
    const assets::Record* tut = assets_.get("campaigns/tutorial/campaign.json");
    check(tut && !tut->uses.empty(), "it knows a campaign uses its missions and briefings");
  }
  if (frame_ == 22) {
    // Somebody edits the mod's rules in a text editor while the game runs.
    const uint32_t before = assets_.get("rules/units.json")->version;
    std::ofstream(modDir + "rules/units.json") << R"({"units": [{"id": "ger_rifleman", "health": 4321}]})";
    const size_t n = hotReload_.checkNow(content_.fileSystem(), &assets_);
    check(n == 1, "one changed file is noticed on disk (" + std::to_string(n) + ")");
    check(types_.get(types_.find("ger_rifleman")).maxHealth == 4321, "the running game has the edited rules");
    check(assets_.get("rules/units.json")->version == before + 1, "the asset has a new version");
    // A half-typed file must not hurt the game.
    std::ofstream(modDir + "rules/units.json") << R"({"units": [{"id": "ger_rifleman", "health": )";
    hotReload_.checkNow(content_.fileSystem(), &assets_);
    check(types_.get(types_.find("ger_rifleman")).maxHealth == 4321, "a file that can't be read changes nothing");
    std::ofstream(modDir + "rules/units.json") << R"({"units": [{"id": "ger_rifleman", "health": 4321}]})";
    hotReload_.checkNow(content_.fileSystem(), &assets_);
  }
  if (frame_ == 24) {
    // The mod as one file: packed, checked, the folder thrown away.
    std::string made;
    check(packMod(modId, made, err), "pack the mod: " + err);
    const pack::Report rep = pack::verify(made);
    check(rep.ok && rep.files >= 3, "the pack checks out (" + std::to_string(rep.files) + " files)");
    check(setMods({}, err), "mods off: " + err);
    std::error_code ec;
    std::filesystem::remove_all(modDir, ec);
    std::filesystem::copy_file(made, packFile, std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(made, ec);
    rescanMods();
    const ModInfo* m = findMod(mods_, modId);
    check(m && m->packed && m->error.empty() && m->name == "Self-test mod",
          "the pack is listed as a mod" + (m && !m->error.empty() ? ": " + m->error : std::string()));
    check(setMods({modId}, err), "switch the packed mod on: " + err);
    check(types_.get(types_.find("ger_rifleman")).maxHealth == 4321, "its rules are read straight out of the pack");
    const std::vector<std::string> ruleDirs = content_.all("rules/");
    check(ruleDirs.size() == 2 && std::filesystem::exists(ruleDirs[1] + "units.json"),
          "code that needs real files gets an unpacked copy");
  }
  if (frame_ == 26) {
    check(setMods({}, err), "switch the packed mod off: " + err);
    check(types_.get(types_.find("ger_rifleman")).maxHealth == 100, "back to the game's own rules");
    std::error_code ec;
    std::filesystem::remove(packFile, ec);
    std::filesystem::remove_all(modDir, ec);
    std::filesystem::remove(savesDir_ + "selftest_mods.lsav", ec);
    rescanMods();
    PNR_INFO("MODS SELFTEST %s (%d failures)", selfTestFailures_ ? "FAILED" : "PASSED", selfTestFailures_);
    quit_ = true;
  }
}

}  // namespace legion

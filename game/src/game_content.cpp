// Legion Classic - content layers and mods for the game scene: loading rules
// through the enabled mods, switching mods, putting edited rules into a running
// battle and rebuilding the art.
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "editor.hpp"
#include "game_scene.hpp"
#include "rules_editor.hpp"
#include "slave/core/log.hpp"
#include "sprite_studio.hpp"

using namespace slave;

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
  layers.addRoot(contentDir_);
  for (const std::string& id : order) layers.addRoot(findMod(mods_, id)->dir, id);
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
  rulesHash_ = slave::rulesHash(rules_);
  rulesEditedLive_ = false;
  trenchTypes_.assign(types_.size(), false);
  for (size_t i = 0; i < types_.size(); ++i) trenchTypes_[i] = types_.get(static_cast<int>(i)).model == "trench";
  if (!order.empty()) {
    std::string list;
    for (const std::string& id : order) list += (list.empty() ? "" : ", ") + id;
    SLV_INFO("Mods: %s (rules %s)", list.c_str(), rulesHash_.c_str());
  }
  return true;
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
    if (!loadRules(again)) SLV_ERROR("Can't go back to the previous mods: %s", again.c_str());
    return false;
  }
  saveSettings();
  if (!reloadArt(error)) return false;
  rulesEditor_->reset();
  spriteStudio_->reset();
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
  renderer_.setWhiteFrame(art_.white);
  std::string err;
  if (!pickUnitArt(err)) SLV_WARN("%s", err.c_str());
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
      SLV_WARN("%s", error.c_str());
      auto it = art_.units.find("rifleman@ger");
      if (it == art_.units.end()) return false;
      a = it->second;
    }
    unitArt_.push_back(a);
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
  rulesHash_ = slave::rulesHash(rules_);
  rulesEditedLive_ = true;
  trenchTypes_.assign(types_.size(), false);
  for (size_t i = 0; i < types_.size(); ++i) trenchTypes_[i] = types_.get(static_cast<int>(i)).model == "trench";
  std::string err;
  pickUnitArt(err);
  if (sim_) sim_->rulesChanged();
  pruneSelection();
  replayValid_ = false;  // a replay can't follow rules changed half-way
  return true;
}

bool GameScene::creationToolOpen() const {
  return (rulesEditor_ && rulesEditor_->isOpen()) || (spriteStudio_ && spriteStudio_->isOpen());
}

}  // namespace legion

namespace legion {

// --mods-selftest: the creation tools through the real game. Edits rules live,
// saves them as a mod, switches the mod on and off, checks saves refuse the
// wrong mods, and saves a recolour from the sprite studio.
void GameScene::runModsSelfTest() {
  auto check = [&](bool ok, const std::string& what) {
    if (!ok) ++selfTestFailures_;
    SLV_INFO("MODS SELFTEST %s: %s", ok ? "ok  " : "FAIL", what.c_str());
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
  if (frame_ == 16) {
    SpriteStudio& ss = *spriteStudio_;
    ss.open();
    ss.select("units/tank@ger");
    ss.setRecolour(0, 0.25f, 1.35f);
    check(ss.saveRecolour(modId, msg), "save a recolour: " + msg);
    check(std::filesystem::exists(userModsDir() + modId + "/art/units/tank@ger/body_f00_a0.png"), "the recoloured frames are in the mod");
    UnitArt a;
    check(artFor(types_.get(types_.find("ger_medium_tank")), a), "the art is rebuilt with the override");
    check(ss.exportSet(modId, msg), "export a set for painting: " + msg);
  }
  if (frame_ == 18) {
    check(setMods({}, err), "switch the mod off: " + err);
    check(types_.get(types_.find("ger_rifleman")).maxHealth == 100, "back to the game's own rules");
    check(loadGame(savesDir_ + "selftest_mods.lsav", err), "the save loads again without the mod: " + err);
    std::error_code ec;
    std::filesystem::remove_all(userModsDir() + modId, ec);
    std::filesystem::remove(savesDir_ + "selftest_mods.lsav", ec);
    rescanMods();
    SLV_INFO("MODS SELFTEST %s (%d failures)", selfTestFailures_ ? "FAILED" : "PASSED", selfTestFailures_);
    quit_ = true;
  }
}

}  // namespace legion

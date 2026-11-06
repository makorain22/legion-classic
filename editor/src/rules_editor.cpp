#include "rules_editor.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

#include "game_scene.hpp"
#include "slave/core/log.hpp"
#include "slave/sim/unit_types.hpp"

using namespace slave;
using nlohmann::json;

namespace legion {

namespace {
const ImVec4 kGold(0.93f, 0.78f, 0.38f, 1);
const ImVec4 kBad(0.95f, 0.45f, 0.35f, 1);
const ImVec4 kGood(0.55f, 0.85f, 0.45f, 1);

enum class F { Int, Float, Text, Long, Enum, Bool, Model, WeaponList, TypeList, TypeRef, WarheadRef, WeaponRef, Footprint, Blocks, Turret, Facing };

struct Field {
  const char* key;
  F type;
  const char* label;
  const char* help;
  double def = 0;          // shown while the field is absent
  double lo = 0, hi = 0;   // range for numbers (hi 0: none)
  std::vector<const char*> options = {};
  const char* section = nullptr;  // starts a new section
};

const std::vector<Field>& fieldsFor(RulesEditor::Kind k) {
  static const std::vector<Field> units = {
      {"id", F::Text, "Id", "The name the rules, maps and saves use. Faction prefix by custom: ger_..., rus_..., gbr_...", 0, 0, 0, {}, "Identity"},
      {"name", F::Text, "Name", "What players see."},
      {"faction", F::Enum, "Faction", "Which side builds it.", 0, 0, 0, {"ger", "rus", "gbr", "neutral"}},
      {"group", F::Text, "Sidebar group", "Heading it is listed under in the sidebar and encyclopedia."},
      {"description", F::Long, "Description", "A line or two for the encyclopedia."},
      {"model", F::Model, "Model", "The art it is drawn with (any built-in or art-pack model)."},
      {"nato", F::Enum, "NATO symbol", "Symbol on unit cards and fog chips.", 0, 0, 0,
       {"infantry", "mg", "antitank", "engineer", "sniper", "recon", "mech", "armour", "antitank_armour", "artillery", "hq"}},
      {"category", F::Enum, "Category", "Where it is built. Left out: worked out from the locomotor.", 0, 0, 0,
       {"infantry", "vehicle", "building", "defense", "fortification"}},
      {"locomotor", F::Enum, "Locomotor", "How it moves: foot (infantry), wheeled or tracked.", 0, 0, 0, {"foot", "wheeled", "tracked"}, "Movement"},
      {"speed", F::Int, "Speed", "Leptons per tick (256 = one tile; 15 ticks a second).", 20, 1, 256},
      {"turnTicks", F::Int, "Turn time", "Ticks per 1/16 turn; 0 turns at once.", 2, 0, 30},
      {"radius", F::Int, "Radius", "Size for selection and spacing, in leptons.", 96, 8, 512},
      {"animTicks", F::Int, "Animation", "Ticks per walk / drive frame.", 2, 1, 30},
      {"health", F::Int, "Health", "Hit points.", 100, 1, 100000, {}, "Combat"},
      {"armor", F::Enum, "Armour", "Which column of the warhead table hits it.", 0, 0, 0, {"infantry", "light", "medium", "heavy", "structure"}},
      {"sight", F::Float, "Sight", "In tiles.", 7, 0, 40},
      {"weapons", F::WeaponList, "Weapons", "Up to two."},
      {"turret", F::Turret, "Turret", "Weapons on a turret that turns on its own (ticks per 1/16 turn)."},
      {"cost", F::Int, "Cost", "Supplies.", 0, 0, 100000, {}, "Production"},
      {"buildTime", F::Float, "Build time", "Seconds at full power.", 10, 0, 600},
      {"builtAt", F::Enum, "Built at", "The kind of structure that makes it.", 0, 0, 0, {"", "barracks", "factory", "yard"}},
      {"requires", F::TypeList, "Requires", "The side needs one of each of these first."},
      {"aiWeight", F::Int, "Computer weight", "How often the computer picks it (relative); 0 = never.", 1, 0, 100},
      {"capacity", F::Int, "Supply load", "Supply trucks: supplies carried per trip.", 0, 0, 10000},
      {"deploysTo", F::TypeRef, "Deploys to", "Construction trucks: the structure it unfolds into."},
      {"repair", F::Int, "Repair", "Engineers: hit points restored per tick.", 0, 0, 100},
  };
  static const std::vector<Field> buildings = {
      {"id", F::Text, "Id", "The name the rules, maps and saves use.", 0, 0, 0, {}, "Identity"},
      {"name", F::Text, "Name", "What players see."},
      {"faction", F::Enum, "Faction", "Which side builds it; neutral = civilian (nobody builds it).", 0, 0, 0, {"ger", "rus", "gbr", "neutral"}},
      {"group", F::Text, "Sidebar group", "Heading it is listed under."},
      {"description", F::Long, "Description", "A line or two for the encyclopedia."},
      {"model", F::Model, "Model", "The art it is drawn with."},
      {"category", F::Enum, "Category", "building, defence (guns, bunkers) or fortification (bought and placed).", 0, 0, 0,
       {"building", "defense", "fortification"}},
      {"nato", F::Enum, "NATO symbol", "Symbol on cards and chips (defences).", 0, 0, 0,
       {"", "infantry", "mg", "antitank", "artillery", "hq"}},
      {"footprint", F::Footprint, "Footprint", "Tiles wide and deep (1..8).", 0, 0, 0, {}, "Structure"},
      {"role", F::Enum, "Role", "What it does for its side.", 0, 0, 0, {"", "yard", "barracks", "factory", "depot", "power"}},
      {"power", F::Int, "Power", "+ produced, - used.", 0, -1000, 1000},
      {"buildRadius", F::Int, "Build radius", "Tiles around it where its side may build.", 0, 0, 40},
      {"income", F::Int, "Income", "Supplies per minute it earns.", 0, 0, 10000},
      {"garrison", F::Int, "Garrison", "Infantry it holds (they take half damage inside).", 0, 0, 40},
      {"cover", F::Int, "Cover in it", "% less damage for infantry standing in it (trenches).", 0, 0, 90},
      {"coverAround", F::Int, "Cover beside it", "% less damage for infantry next to it (sandbags).", 0, 0, 90},
      {"blocks", F::Blocks, "Blocks", "Which movement types it stops (default: all)."},
      {"autoTarget", F::Bool, "Attacked unasked", "Enemies attack it of their own accord.", 1},
      {"targetable", F::Bool, "Can be attacked", "Off for things like wire that only blocks.", 1},
      {"health", F::Int, "Health", "Hit points.", 100, 1, 100000, {}, "Combat"},
      {"armor", F::Enum, "Armour", "Which column of the warhead table hits it.", 0, 0, 0, {"structure", "heavy", "medium", "light", "infantry"}},
      {"sight", F::Float, "Sight", "In tiles.", 4, 0, 40},
      {"weapons", F::WeaponList, "Weapons", "Defences: up to two."},
      {"turret", F::Turret, "Turret", "A gun that turns on its own."},
      {"cost", F::Int, "Cost", "Supplies.", 0, 0, 100000, {}, "Production"},
      {"buildTime", F::Float, "Build time", "Seconds at full power.", 10, 0, 600},
      {"builtAt", F::Enum, "Built at", "yard for buildings and defences; empty: placed directly.", 0, 0, 0, {"", "yard"}},
      {"requires", F::TypeList, "Requires", "The side needs one of each of these first."},
      {"aiWeight", F::Int, "Computer weight", "How often the computer picks it (relative).", 1, 0, 100},
      {"freeUnit", F::TypeRef, "Comes with", "A unit delivered with it (a depot's supply truck)."},
  };
  static const std::vector<Field> weapons = {
      {"id", F::Text, "Id", "The name units refer to.", 0, 0, 0, {}, "Identity"},
      {"name", F::Text, "Name", "What players see."},
      {"warhead", F::WarheadRef, "Warhead", "Damage against each kind of armour (Warheads list).", 0, 0, 0, {}, "Damage"},
      {"damage", F::Int, "Damage", "Per hit, before armour.", 10, 0, 100000},
      {"accuracy", F::Int, "Accuracy", "% chance an instant-hit weapon hits.", 100, 1, 100},
      {"vsInfantry", F::WeaponRef, "Against infantry", "The round a gun switches to against soldiers (HE)."},
      {"range", F::Float, "Range", "In tiles.", 5, 0.1, 60, {}, "Fire"},
      {"cooldown", F::Int, "Cooldown", "Ticks between shots (15 a second).", 15, 1, 3000},
      {"projectile", F::Enum, "Projectile", "instant: hits at once; shell: flies.", 0, 0, 0, {"instant", "shell"}},
      {"speed", F::Int, "Shell speed", "Leptons per tick.", 160, 1, 2000},
      {"splash", F::Float, "Splash", "Blast radius in tiles.", 0, 0, 10},
      {"burst", F::Int, "Burst", "Shells per shot (rocket salvoes).", 1, 1, 48},
      {"burstSpread", F::Float, "Burst spread", "Extra scatter per shell, in tiles.", 0, 0, 10},
      {"muzzle", F::Int, "Muzzle flash", "Size 0..2 (looks only).", 1, 0, 2},
      {"sound", F::Text, "Firing sound", "A sound from content/audio/audio.json: rifle, sniper, mg, lmg, hmg, at_launcher, at_rifle, cannon_small, cannon_medium, cannon_large, howitzer, rocket. Empty: chosen by the kind of weapon.", 0, 0, 0, {}, "Sound"},
      {"impact", F::Text, "Impact sound", "Where it lands: explosion_small, explosion_medium, explosion_large, armour_hit... Empty: chosen by the warhead."},
  };
  static const std::vector<Field> warheads = {
      {"infantry", F::Int, "vs infantry", "% damage.", 100, 0, 1000, {}, "Damage by armour"},
      {"light", F::Int, "vs light", "% damage (scout cars, half-tracks).", 100, 0, 1000},
      {"medium", F::Int, "vs medium", "% damage (medium tanks).", 100, 0, 1000},
      {"heavy", F::Int, "vs heavy", "% damage (heavy tanks).", 100, 0, 1000},
      {"structure", F::Int, "vs structures", "% damage.", 100, 0, 1000},
      {"facing", F::Facing, "By side struck", "% damage to vehicles by the side hit: front / side / rear.", 0, 0, 0, {}, "Flanking"},
  };
  switch (k) {
    case RulesEditor::Units: return units;
    case RulesEditor::Buildings: return buildings;
    case RulesEditor::Weapons: return weapons;
    default: return warheads;
  }
}

const char* kKindNames[] = {"Units", "Buildings", "Weapons", "Warheads"};
const char* kArmour[] = {"infantry", "light", "medium", "heavy", "structure"};

int fileOf(RulesEditor::Kind k) { return k == RulesEditor::Units ? 1 : k == RulesEditor::Buildings ? 2 : 0; }

double num(const json& e, const char* key, double def) {
  if (!e.is_object() || !e.contains(key) || !e[key].is_number()) return def;
  return e[key].get<double>();
}

std::string str(const json& e, const char* key) {
  if (!e.is_object() || !e.contains(key) || !e[key].is_string()) return {};
  return e[key].get<std::string>();
}

// A combo over a list of strings, writing the choice into `value`.
bool comboOf(const char* label, std::string& value, const std::vector<std::string>& options, bool allowNone) {
  bool changed = false;
  if (ImGui::BeginCombo(label, value.empty() ? "(none)" : value.c_str(), ImGuiComboFlags_HeightLarge)) {
    if (allowNone && ImGui::Selectable("(none)", value.empty())) {
      value.clear();
      changed = true;
    }
    for (const std::string& o : options) {
      if (ImGui::Selectable(o.c_str(), o == value)) {
        value = o;
        changed = true;
      }
      if (o == value) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  return changed;
}

bool textField(const char* label, json& e, const char* key, bool multiline) {
  static char buf[2048];
  const std::string v = str(e, key);
  std::snprintf(buf, sizeof buf, "%s", v.c_str());
  const bool changed = multiline ? ImGui::InputTextMultiline(label, buf, sizeof buf, ImVec2(-1, 60))
                                 : ImGui::InputText(label, buf, sizeof buf);
  if (changed) e[key] = std::string(buf);
  return changed;
}
}  // namespace

RulesEditor::RulesEditor(GameScene& scene) : scene_(scene) {}

void RulesEditor::reset() {
  loaded_ = false;
  status_.clear();
  notes_.clear();
  if (open_) open();
}

void RulesEditor::open() {
  open_ = true;
  if (loaded_) return;
  for (int i = 0; i < kRuleFileCount; ++i) {
    work_[i] = scene_.rulesDoc(i);
    // Types deleted earlier in this battle are kept there, hidden: not part of what is edited.
    for (const char* key : {"units", "weapons"}) {
      if (!work_[i].contains(key)) continue;
      json& list = work_[i][key];
      list.erase(std::remove_if(list.begin(), list.end(), [](const json& e) { return e.value("hidden", false); }),
                 list.end());
    }
    orig_[i] = work_[i];
  }
  std::set<std::string> f{"ger", "rus", "gbr"};
  for (const auto& e : work_[1]["units"]) f.insert(e.value("faction", std::string()));
  for (const auto& e : work_[2]["units"]) f.insert(e.value("faction", std::string()));
  f.erase("");
  factions_.assign(f.begin(), f.end());
  loaded_ = true;
  changedSinceSave_ = false;
  if (selected_.empty() && !listOf(kind_).empty()) select(kind_, "ger_medium_tank");
  validate();
}

json& RulesEditor::listOf(Kind k) {
  if (k == Warheads) return work_[0]["warheads"];
  return work_[fileOf(k)][k == Weapons ? "weapons" : "units"];
}

const json* RulesEditor::originalOf(Kind k, const std::string& id) const {
  const json& doc = orig_[fileOf(k)];
  if (k == Warheads) return doc.contains("warheads") && doc["warheads"].contains(id) ? &doc["warheads"][id] : nullptr;
  const char* key = k == Weapons ? "weapons" : "units";
  if (!doc.contains(key)) return nullptr;
  for (const auto& e : doc[key])
    if (e.value("id", std::string()) == id) return &e;
  return nullptr;
}

json* RulesEditor::entry(Kind k, const std::string& id) {
  json& list = listOf(k);
  if (k == Warheads) return list.contains(id) ? &list[id] : nullptr;
  for (auto& e : list)
    if (e.value("id", std::string()) == id) return &e;
  return nullptr;
}

void RulesEditor::select(Kind k, const std::string& id) {
  if (k != kind_) syncTab_ = true;
  kind_ = k;
  selected_ = id;
  if (!entry(k, id)) {
    json& list = listOf(k);
    selected_.clear();
    if (k == Warheads && !list.empty()) selected_ = list.begin().key();
    else if (k != Warheads && !list.empty()) selected_ = list[0].value("id", std::string());
  }
}

std::vector<std::string> RulesEditor::idsOf(Kind k) const {
  std::vector<std::string> ids;
  const json& doc = work_[fileOf(k)];
  if (k == Warheads) {
    if (doc.contains("warheads"))
      for (auto it = doc["warheads"].begin(); it != doc["warheads"].end(); ++it) ids.push_back(it.key());
    return ids;
  }
  const char* key = k == Weapons ? "weapons" : "units";
  if (doc.contains(key))
    for (const auto& e : doc[key]) ids.push_back(e.value("id", std::string()));
  return ids;
}

std::vector<std::string> RulesEditor::models() const {
  std::set<std::string> m;
  for (const auto& [key, art] : scene_.art().units) m.insert(key.substr(0, key.find('@')));
  return {m.begin(), m.end()};
}

void RulesEditor::edited() {
  changedSinceSave_ = true;
  validate();
  if (live_ && problem_.empty()) {
    pendingApply_ = true;
    applyTimer_ = 0.35f;  // typing a number: apply once it settles
  }
}

void RulesEditor::validate() {
  WeaponRegistry w;
  UnitTypeRegistry t;
  problem_.clear();
  if (!loadRuleSet(work_[0], work_[1], work_[2], w, t, problem_)) return;
  for (const UnitTypeDef& d : t.all()) {
    UnitArt a;
    if (!scene_.artFor(d, a)) {
      problem_ = "'" + d.id + "' uses model '" + d.model + "', which has no art";
      return;
    }
  }
}

bool RulesEditor::apply(std::string& message) {
  validate();
  if (!problem_.empty()) {
    message = "Not applied: " + problem_;
    return false;
  }
  std::string error;
  if (!scene_.applyRules(work_, notes_, error)) {
    message = "Not applied: " + error;
    return false;
  }
  message = "In the battle now.";
  return true;
}

bool RulesEditor::saveToMod(const std::string& modId, std::string& message) {
  validate();
  if (!problem_.empty()) {
    message = "Not saved: " + problem_;
    return false;
  }
  scene_.rescanMods();
  const ModInfo* mod = findMod(scene_.installedMods(), modId);
  if (!mod || !mod->user) {
    message = "Not saved: pick one of your own mods (or make one)";
    return false;
  }
  // What is below the mod: everything enabled before it (or everything, if it isn't enabled yet).
  const auto& ids = scene_.content().modIds();
  const bool enabled = std::find(ids.begin(), ids.end(), modId) != ids.end();
  int written = 0;
  for (int i = 0; i < kRuleFileCount; ++i) {
    json below;
    std::string error;
    const bool ok = enabled ? scene_.content().loadRulesBelow(kRuleFiles[i], modId, below, error)
                            : scene_.content().loadRules(kRuleFiles[i], below, error);
    if (!ok) {
      message = "Not saved: " + error;
      return false;
    }
    const json patch = diffRules(below, work_[i]);
    const std::string path = mod->dir + "rules/" + kRuleFiles[i];
    std::error_code ec;
    if (patch.empty()) {
      std::filesystem::remove(path, ec);
      continue;
    }
    std::filesystem::create_directories(mod->dir + "rules", ec);
    std::ofstream out(path);
    if (!out) {
      message = "Not saved: can't write " + path;
      return false;
    }
    out << patch.dump(2) << "\n";
    ++written;
  }
  changedSinceSave_ = false;
  scene_.settings().toolMod = modId;
  if (!enabled) {
    // Switched on for the next battle; this one already runs on these rules.
    auto& want = scene_.settings().mods;
    if (std::find(want.begin(), want.end(), modId) == want.end()) want.push_back(modId);
  }
  scene_.saveSettings();
  message = written ? "Saved to '" + mod->name + "' (" + std::to_string(written) + " rule file" + (written > 1 ? "s" : "") + ")."
                    : "Nothing differs from the game's rules: '" + mod->name + "' has no rule files now.";
  if (!enabled) message += " Enabled from the next start or mod change.";
  return true;
}

void RulesEditor::newEntry(bool copy) {
  json* from = entry(kind_, selected_);
  json fresh;
  std::string base = copy && from ? selected_ : kind_ == Weapons ? "new_weapon" : kind_ == Warheads ? "new_warhead" : "new_unit";
  std::string id = base + (copy ? "_copy" : "");
  for (int n = 2; entry(kind_, id); ++n) id = base + (copy ? "_copy" : "") + std::to_string(n);
  if (copy && from) {
    fresh = *from;
  } else if (kind_ == Units) {
    fresh = {{"faction", "ger"}, {"model", "rifleman"}, {"locomotor", "foot"}, {"speed", 10}, {"health", 100},
             {"armor", "infantry"}, {"weapons", json::array({"rifle"})}, {"cost", 10}, {"buildTime", 2},
             {"builtAt", "barracks"}, {"nato", "infantry"}, {"group", "Infantry"}};
  } else if (kind_ == Buildings) {
    fresh = {{"faction", "ger"}, {"model", "barracks"}, {"footprint", json::array({2, 2})}, {"health", 800},
             {"armor", "structure"}, {"category", "building"}, {"cost", 500}, {"buildTime", 10}, {"builtAt", "yard"},
             {"group", "Base"}};
  } else if (kind_ == Weapons) {
    fresh = {{"warhead", "bullet"}, {"damage", 10}, {"range", 5.0}, {"cooldown", 15}};
  } else {
    fresh = {{"infantry", 100}, {"light", 100}, {"medium", 100}, {"heavy", 100}, {"structure", 100}};
  }
  if (kind_ == Warheads) {
    listOf(kind_)[id] = fresh;
  } else {
    fresh["id"] = id;
    if (fresh.contains("name") && copy) fresh["name"] = fresh["name"].get<std::string>() + " (copy)";
    else if (!copy) fresh["name"] = "New " + std::string(kind_ == Weapons ? "weapon" : kind_ == Buildings ? "building" : "unit");
    listOf(kind_).push_back(fresh);
  }
  selected_ = id;
  edited();
}

void RulesEditor::deleteEntry() {
  json& list = listOf(kind_);
  if (kind_ == Warheads) {
    list.erase(selected_);
  } else {
    list.erase(std::remove_if(list.begin(), list.end(), [&](const json& e) { return e.value("id", std::string()) == selected_; }),
               list.end());
  }
  select(kind_, "");
  edited();
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void RulesEditor::draw() {
  if (!open_) return;
  if (!loaded_) open();
  if (pendingApply_) {
    applyTimer_ -= ImGui::GetIO().DeltaTime;
    if (applyTimer_ <= 0 && !ImGui::IsAnyItemActive()) {
      pendingApply_ = false;
      if (problem_.empty()) apply(status_);
    }
  }
  const ImGuiIO& io = ImGui::GetIO();
  const ImVec2 size(std::min(1120.0f, io.DisplaySize.x - 40), std::min(740.0f, io.DisplaySize.y - 60));
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
  ImGui::SetNextWindowBgAlpha(0.96f);
  if (!ImGui::Begin("Rules editor", &open_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginTabBar("##kinds")) {
    const bool sync = syncTab_;
    syncTab_ = false;
    for (int k = 0; k < KindCount; ++k) {
      const ImGuiTabItemFlags flags = sync && k == kind_ ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem(kKindNames[k], nullptr, flags)) {
        if (!sync && kind_ != k) select(static_cast<Kind>(k), "");
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  const float bottom = ImGui::GetFrameHeightWithSpacing() * 3.2f;
  ImGui::BeginChild("##list", ImVec2(270, -bottom), ImGuiChildFlags_Borders);
  drawList();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##form", ImVec2(0, -bottom), ImGuiChildFlags_Borders);
  drawForm();
  ImGui::EndChild();
  drawBottomBar();
  ImGui::End();
}

void RulesEditor::drawList() {
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##filter", "search", filter_, sizeof filter_);
  if (kind_ == Units || kind_ == Buildings) {
    std::string f = faction_ > 0 && faction_ <= static_cast<int>(factions_.size()) ? factions_[faction_ - 1] : std::string();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##faction", f.empty() ? "every side" : f.c_str())) {
      if (ImGui::Selectable("every side", faction_ == 0)) faction_ = 0;
      for (size_t i = 0; i < factions_.size(); ++i)
        if (ImGui::Selectable(factions_[i].c_str(), faction_ == static_cast<int>(i) + 1)) faction_ = static_cast<int>(i) + 1;
      ImGui::EndCombo();
    }
  }
  if (ImGui::Button("New")) newEntry(false);
  ImGui::SameLine();
  ImGui::BeginDisabled(selected_.empty());
  if (ImGui::Button("Copy")) newEntry(true);
  ImGui::SameLine();
  if (ImGui::Button("Delete")) deleteEntry();
  ImGui::EndDisabled();
  ImGui::Separator();

  std::string needle = filter_;
  std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  const std::string fac = faction_ > 0 && faction_ <= static_cast<int>(factions_.size()) ? factions_[faction_ - 1] : std::string();
  ImGui::BeginChild("##entries");
  for (const std::string& id : idsOf(kind_)) {
    const json* e = entry(kind_, id);
    if (!e) continue;
    const std::string name = kind_ == Warheads ? id : e->value("name", id);
    if (!fac.empty() && e->value("faction", std::string()) != fac) continue;
    std::string hay = name + " " + id;
    std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!needle.empty() && hay.find(needle) == std::string::npos) continue;
    const json* o = originalOf(kind_, id);
    const char* mark = !o ? "+ " : *o != *e ? "* " : "  ";
    if (o && *o == *e) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    else ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    const std::string label = std::string(mark) + name + (kind_ == Warheads ? "" : "##" + id);
    if (ImGui::Selectable(label.c_str(), id == selected_)) selected_ = id;
    ImGui::PopStyleColor();
    if (kind_ == Units || kind_ == Buildings) {
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
      ImGui::TextDisabled("%s", e->value("faction", std::string()).c_str());
    }
    if (kind_ != Warheads && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", id.c_str());
  }
  ImGui::EndChild();
}

void RulesEditor::drawForm() {
  json* ep = entry(kind_, selected_);
  if (!ep) {
    ImGui::TextDisabled("Pick something on the left, or make a new one.");
    return;
  }
  json& e = *ep;
  const json* orig = originalOf(kind_, selected_);
  bool changed = false;
  const float labelW = 150;

  ImGui::PushStyleColor(ImGuiCol_Text, kGold);
  ImGui::SetWindowFontScale(1.25f);
  ImGui::TextUnformatted(kind_ == Warheads ? selected_.c_str() : e.value("name", selected_).c_str());
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();
  if (!orig) ImGui::TextDisabled("New in these rules");
  else if (*orig != e) ImGui::TextDisabled("Changed (gold fields); hover one to see what it was");

  if (ImGui::BeginTable("##split", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("form", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("facts", ImGuiTableColumnFlags_WidthStretch, 2.4f);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    for (const Field& f : fieldsFor(kind_)) {
      if (f.section) ImGui::SeparatorText(f.section);
      ImGui::PushID(f.key);
      const bool present = e.contains(f.key);
      const bool differs = orig ? (orig->contains(f.key) != present || (present && (*orig)[f.key] != e[f.key])) : false;
      ImGui::AlignTextToFramePadding();
      if (differs) ImGui::TextColored(kGold, "%s", f.label);
      else if (present) ImGui::TextUnformatted(f.label);
      else ImGui::TextDisabled("%s", f.label);
      if (ImGui::IsItemHovered()) {
        std::string tip = f.help;
        if (differs) tip += "\nWas: " + (orig->contains(f.key) ? (*orig)[f.key].dump() : std::string("(not set)"));
        if (!present) tip += "\nNot set: the default applies.";
        ImGui::SetTooltip("%s", tip.c_str());
      }
      ImGui::SameLine(labelW);
      const float resetW = present && f.type != F::Text && std::strcmp(f.key, "faction") != 0 ? 24.0f : 0.0f;
      ImGui::SetNextItemWidth(-1 - resetW);
      bool c = false;
      switch (f.type) {
        case F::Int: {
          int v = static_cast<int>(num(e, f.key, f.def));
          if (ImGui::DragInt("##v", &v, std::max(1.0f, static_cast<float>(f.hi - f.lo) / 400.0f), static_cast<int>(f.lo),
                             static_cast<int>(f.hi), "%d", ImGuiSliderFlags_AlwaysClamp)) {
            e[f.key] = v;
            c = true;
          }
          break;
        }
        case F::Float: {
          float v = static_cast<float>(num(e, f.key, f.def));
          if (ImGui::DragFloat("##v", &v, 0.05f, static_cast<float>(f.lo), static_cast<float>(f.hi), "%.2f",
                               ImGuiSliderFlags_AlwaysClamp)) {
            e[f.key] = std::round(v * 100.0) / 100.0;
            c = true;
          }
          break;
        }
        case F::Text:
          if (std::strcmp(f.key, "id") == 0 && kind_ == Warheads) {
            ImGui::TextUnformatted(selected_.c_str());
          } else if (std::strcmp(f.key, "id") == 0) {
            // Renaming: references elsewhere must follow by hand (the check below says where).
            static char buf[96];
            std::snprintf(buf, sizeof buf, "%s", selected_.c_str());
            if (ImGui::InputText("##v", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue) && buf[0] &&
                !entry(kind_, buf)) {
              e["id"] = std::string(buf);
              selected_ = buf;
              c = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Press Enter to rename. Things that refer to it must be changed too.");
          } else {
            c = textField("##v", e, f.key, false);
          }
          break;
        case F::Long: c = textField("##v", e, f.key, true); break;
        case F::Bool: {
          bool v = present ? e[f.key].get<bool>() : f.def != 0;
          if (ImGui::Checkbox("##v", &v)) {
            e[f.key] = v;
            c = true;
          }
          break;
        }
        case F::Enum: {
          std::string v = str(e, f.key);
          std::vector<std::string> opts(f.options.begin(), f.options.end());
          if (std::find(opts.begin(), opts.end(), v) == opts.end() && !v.empty()) opts.push_back(v);
          for (const std::string& fac : factions_)
            if (std::strcmp(f.key, "faction") == 0 && std::find(opts.begin(), opts.end(), fac) == opts.end()) opts.push_back(fac);
          opts.erase(std::remove(opts.begin(), opts.end(), std::string()), opts.end());
          if (comboOf("##v", v, opts, true)) {
            if (v.empty()) e.erase(f.key);
            else e[f.key] = v;
            c = true;
          }
          if (std::strcmp(f.key, "faction") == 0) {
            // A new side: type it in.
            static char buf[16];
            ImGui::Indent(labelW);
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputTextWithHint("##newfac", "new side id", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue) && buf[0]) {
              e[f.key] = std::string(buf);
              factions_.push_back(buf);
              buf[0] = 0;
              c = true;
            }
            ImGui::Unindent(labelW);
          }
          break;
        }
        case F::Model: {
          std::string v = str(e, f.key);
          if (comboOf("##v", v, models(), false)) {
            e[f.key] = v;
            c = true;
          }
          break;
        }
        case F::WarheadRef:
        case F::WeaponRef:
        case F::TypeRef: {
          std::string v = str(e, f.key);
          std::vector<std::string> opts = f.type == F::WarheadRef ? idsOf(Warheads) : f.type == F::WeaponRef ? idsOf(Weapons) : idsOf(Units);
          if (f.type == F::TypeRef)
            for (const std::string& b : idsOf(Buildings)) opts.push_back(b);
          if (comboOf("##v", v, opts, f.type != F::WarheadRef)) {
            if (v.empty()) e.erase(f.key);
            else e[f.key] = v;
            c = true;
          }
          break;
        }
        case F::WeaponList:
        case F::TypeList: {
          json list = present && e[f.key].is_array() ? e[f.key] : json::array();
          std::vector<std::string> opts = f.type == F::WeaponList ? idsOf(Weapons) : idsOf(Buildings);
          if (f.type == F::TypeList)
            for (const std::string& u : idsOf(Units)) opts.push_back(u);
          ImGui::BeginGroup();
          int drop = -1;
          for (size_t i = 0; i < list.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            std::string v = list[i].get<std::string>();
            ImGui::SetNextItemWidth(-30 - resetW);
            if (comboOf("##item", v, opts, false)) {
              list[i] = v;
              c = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) drop = static_cast<int>(i);
            ImGui::PopID();
          }
          if (drop >= 0) {
            list.erase(list.begin() + drop);
            c = true;
          }
          const size_t cap = f.type == F::WeaponList ? 2 : 8;
          if (list.size() < cap && !opts.empty()) {
            std::string add;
            ImGui::SetNextItemWidth(-1 - resetW);
            if (comboOf("##add", add, opts, false) && !add.empty()) {
              list.push_back(add);
              c = true;
            }
          }
          ImGui::EndGroup();
          if (c) e[f.key] = list;
          break;
        }
        case F::Footprint: {
          int v[2] = {2, 2};
          if (present && e[f.key].is_array() && e[f.key].size() == 2) {
            v[0] = e[f.key][0].get<int>();
            v[1] = e[f.key][1].get<int>();
          }
          if (ImGui::DragInt2("##v", v, 0.05f, 1, 8, "%d", ImGuiSliderFlags_AlwaysClamp)) {
            e[f.key] = json::array({v[0], v[1]});
            c = true;
          }
          break;
        }
        case F::Blocks: {
          const char* locos[] = {"foot", "wheeled", "tracked"};
          json list = present && e[f.key].is_array() ? e[f.key] : json::array({"foot", "wheeled", "tracked"});
          for (int i = 0; i < 3; ++i) {
            bool on = std::find(list.begin(), list.end(), locos[i]) != list.end();
            if (i) ImGui::SameLine();
            if (ImGui::Checkbox(locos[i], &on)) {
              if (on) list.push_back(locos[i]);
              else list.erase(std::remove(list.begin(), list.end(), locos[i]), list.end());
              e[f.key] = list;
              c = true;
            }
          }
          break;
        }
        case F::Turret: {
          bool on = present;
          if (ImGui::Checkbox("##on", &on)) {
            if (on) e[f.key] = {{"turnTicks", 1}};
            else e.erase(f.key);
            c = true;
          }
          if (on && e.contains(f.key)) {
            ImGui::SameLine();
            int t = e[f.key].value("turnTicks", 1);
            ImGui::SetNextItemWidth(-1 - resetW);
            if (ImGui::DragInt("##turn", &t, 0.1f, 1, 30, "%d ticks per turn step", ImGuiSliderFlags_AlwaysClamp)) {
              e[f.key]["turnTicks"] = t;
              c = true;
            }
          }
          break;
        }
        case F::Facing: {
          int v[3] = {100, 100, 100};
          if (present) {
            v[0] = e[f.key].value("front", 100);
            v[1] = e[f.key].value("side", 100);
            v[2] = e[f.key].value("rear", 100);
          }
          if (ImGui::DragInt3("##v", v, 1, 0, 1000, "%d%%", ImGuiSliderFlags_AlwaysClamp)) {
            e[f.key] = {{"front", v[0]}, {"side", v[1]}, {"rear", v[2]}};
            c = true;
          }
          break;
        }
      }
      if (resetW > 0 && present && std::strcmp(f.key, "id") != 0 && std::strcmp(f.key, "faction") != 0 && f.type != F::Turret) {
        ImGui::SameLine();
        if (ImGui::SmallButton("-")) {
          e.erase(f.key);
          c = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear: use the default");
      }
      changed = changed || c;
      ImGui::PopID();
    }

    ImGui::TableNextColumn();
    drawFacts(e);
    ImGui::EndTable();
  }
  if (changed) edited();
}

// Numbers worked out from the rules: what a change really does.
void RulesEditor::drawFacts(const json& e) {
  ImGui::SeparatorText("What it adds up to");
  const json& weapons = work_[0]["weapons"];
  const json& warheads = work_[0]["warheads"];
  auto weapon = [&](const std::string& id) -> const json* {
    for (const auto& w : weapons)
      if (w.value("id", std::string()) == id) return &w;
    return nullptr;
  };
  // Damage per second of one weapon against each armour class.
  auto dpsRow = [&](const json& w) {
    const json wh = warheads.contains(w.value("warhead", std::string())) ? warheads[w.value("warhead", std::string())] : json::object();
    const double perSecond = 15.0 / std::max(1.0, num(w, "cooldown", 15)) * std::max(1.0, num(w, "burst", 1)) *
                             (w.value("projectile", std::string("instant")) == "instant" ? num(w, "accuracy", 100) / 100.0 : 1.0);
    for (int a = 0; a < 5; ++a) {
      ImGui::TableNextColumn();
      ImGui::Text("%.1f", num(w, "damage", 0) * num(wh, kArmour[a], 100) / 100.0 * perSecond);
    }
  };
  auto dpsTable = [&](const std::vector<const json*>& list) {
    if (list.empty()) return;
    if (ImGui::BeginTable("##dps", 6, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Damage / s", ImGuiTableColumnFlags_WidthStretch, 2.6f);
      for (const char* a : {"Inf", "Lt", "Med", "Hvy", "Bldg"}) ImGui::TableSetupColumn(a, ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableHeadersRow();
      for (const json* w : list) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(w->value("name", w->value("id", std::string())).c_str());
        dpsRow(*w);
        if (w->contains("vsInfantry")) {
          if (const json* he = weapon((*w)["vsInfantry"].get<std::string>())) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("  HE: %s", he->value("name", std::string()).c_str());
            dpsRow(*he);
          }
        }
      }
      ImGui::EndTable();
    }
  };

  if (kind_ == Units || kind_ == Buildings) {
    std::vector<const json*> ws;
    if (e.contains("weapons") && e["weapons"].is_array())
      for (const auto& id : e["weapons"])
        if (const json* w = weapon(id.get<std::string>())) ws.push_back(w);
    dpsTable(ws);
    for (const json* w : ws) ImGui::TextDisabled("%s: range %.1f tiles", w->value("name", std::string()).c_str(), num(*w, "range", 0));
    if (kind_ == Units) {
      ImGui::Text("Speed: %.2f tiles / s", num(e, "speed", 20) * 15.0 / 256.0);
    } else if (e.contains("footprint")) {
      ImGui::Text("Footprint: %d x %d tiles", e["footprint"][0].get<int>(), e["footprint"][1].get<int>());
    }
    const double cost = num(e, "cost", 0), hp = num(e, "health", 100);
    ImGui::Text("Health per 100 supplies: %.0f", cost > 0 ? hp * 100.0 / cost : 0.0);
    ImGui::Text("Build time: %.1f s", num(e, "buildTime", 10));
    // Who else refers to it.
    std::vector<std::string> need;
    for (int file : {1, 2})
      for (const auto& o : work_[file]["units"])
        if (o.contains("requires") && std::find(o["requires"].begin(), o["requires"].end(), e.value("id", std::string())) != o["requires"].end())
          need.push_back(o.value("name", o.value("id", std::string())));
    if (!need.empty()) {
      std::string list;
      for (const std::string& n : need) list += (list.empty() ? "" : ", ") + n;
      ImGui::TextWrapped("Needed for: %s", list.c_str());
    }
  } else if (kind_ == Weapons) {
    dpsTable({&e});
    std::string users;
    for (int file : {1, 2})
      for (const auto& o : work_[file]["units"])
        if (o.contains("weapons") && std::find(o["weapons"].begin(), o["weapons"].end(), e.value("id", std::string())) != o["weapons"].end())
          users += (users.empty() ? "" : ", ") + o.value("name", o.value("id", std::string()));
    ImGui::TextWrapped("Carried by: %s", users.empty() ? "nobody" : users.c_str());
  } else {
    std::string users;
    for (const auto& w : weapons)
      if (w.value("warhead", std::string()) == selected_) users += (users.empty() ? "" : ", ") + w.value("name", w.value("id", std::string()));
    ImGui::TextWrapped("Used by: %s", users.empty() ? "no weapon" : users.c_str());
    if (e.contains("facing"))
      ImGui::TextWrapped("A hit in the rear does %.1f times the damage of one in the front.",
                         num(e["facing"], "rear", 100) / std::max(1.0, num(e["facing"], "front", 100)));
  }
}

void RulesEditor::drawBottomBar() {
  if (!problem_.empty()) ImGui::TextColored(kBad, "Problem: %s", problem_.c_str());
  else if (!status_.empty()) ImGui::TextColored(status_.rfind("Not", 0) == 0 ? kBad : kGood, "%s", status_.c_str());
  else ImGui::TextDisabled("The rules check out.");
  if (!notes_.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", notes_.c_str());
  if (!notes_.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("(some changes wait for the next battle: hover)");
  }

  ImGui::Checkbox("Live", &live_);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put every change into the running battle as you make it.");
  ImGui::SameLine();
  ImGui::BeginDisabled(!problem_.empty());
  if (ImGui::Button("Apply to battle")) apply(status_);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Undo all changes")) {
    for (int i = 0; i < kRuleFileCount; ++i) work_[i] = orig_[i];
    select(kind_, selected_);
    edited();
    changedSinceSave_ = false;
  }

  // Where it is saved.
  ImGui::SameLine(0, 30);
  ImGui::TextUnformatted("Mod:");
  ImGui::SameLine();
  std::vector<std::string> mine;
  for (const ModInfo& m : scene_.installedMods())
    if (m.user && m.error.empty()) mine.push_back(m.id);
  std::string& target = scene_.settings().toolMod;
  if (std::find(mine.begin(), mine.end(), target) == mine.end()) target = mine.empty() ? std::string() : mine.front();
  ImGui::SetNextItemWidth(170);
  comboOf("##mod", target, mine, false);
  ImGui::SameLine();
  ImGui::BeginDisabled(target.empty() || !problem_.empty());
  if (ImGui::Button(changedSinceSave_ ? "Save to mod *" : "Save to mod")) saveToMod(target, status_);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  ImGui::InputTextWithHint("##newmod", "new mod name", newModName_, sizeof newModName_);
  ImGui::SameLine();
  ImGui::BeginDisabled(newModName_[0] == 0);
  if (ImGui::Button("Make")) {
    ModInfo made;
    std::string error;
    if (createMod(scene_.userModsDir(), modIdFromName(newModName_), newModName_, made, error)) {
      scene_.rescanMods();
      target = made.id;
      newModName_[0] = 0;
      status_ = "Made mod '" + made.name + "'.";
    } else {
      status_ = "Not made: " + error;
    }
  }
  ImGui::EndDisabled();
}

}  // namespace legion

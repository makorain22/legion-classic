// Milestone 8: content layers, mods, rules merging and changing rules in a
// running battle.
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "slave/core/content.hpp"
#include "slave/sim/simulation.hpp"

using namespace slave;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
constexpr int32_t T = LEPTONS_PER_TILE;

void writeJson(const std::string& path, const json& j) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream(path) << j.dump(1);
}

// A scratch folder, removed afterwards.
struct TempDir {
  std::string path;
  explicit TempDir(const char* name) {
    path = (fs::temp_directory_path() / (std::string("legion_test_") + name)).generic_string() + "/";
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

struct Loaded {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
};

bool loadAll(const json (&docs)[kRuleFileCount], Loaded& out, std::string& err) {
  out = Loaded{};
  return out.weapons.loadJson(docs[0], err) && out.types.loadJson(docs[1], err) && out.types.loadJson(docs[2], err) &&
         out.types.resolveWeapons(out.weapons, err) && out.types.resolveLinks(err);
}

bool loadBase(json (&docs)[kRuleFileCount]) {
  ContentLayers layers;
  layers.addRoot(std::string(LEGION_RULES_DIR) + "../");
  std::string err;
  for (int i = 0; i < kRuleFileCount; ++i)
    if (!layers.loadRules(kRuleFiles[i], docs[i], err)) return false;
  return true;
}

const json* entry(const json& doc, const std::string& key, const std::string& id) {
  for (const auto& e : doc[key])
    if (e["id"] == id) return &e;
  return nullptr;
}
}  // namespace

TEST_CASE("mod rules change only the fields they name, add entries and delete them") {
  json base = {{"_comment", "guide"},
               {"units", json::array({{{"id", "a"}, {"cost", 10}, {"health", 100}, {"weapons", {"rifle"}}},
                                      {{"id", "b"}, {"cost", 20}}})},
               {"warheads", {{"ap", {{"light", 90}, {"facing", {{"rear", 400}, {"side", 150}}}}}, {"he", {{"light", 50}}}}}};
  const json patch = {{"_comment", "mine"},
                      {"units", json::array({{{"id", "a"}, {"cost", 15}, {"weapons", {"mg"}}},
                                             {{"id", "b"}, {"_remove", true}},
                                             {{"id", "c"}, {"cost", 5}}})},
                      {"warheads", {{"ap", {{"facing", {{"rear", 300}}}}}, {"he", {{"_remove", true}}}}}};
  mergeRules(base, patch);
  CHECK(base["_comment"] == "guide");  // the base's field guide stays
  REQUIRE(base["units"].size() == 2);
  CHECK(base["units"][0]["cost"] == 15);
  CHECK(base["units"][0]["health"] == 100);       // untouched field kept
  CHECK(base["units"][0]["weapons"] == json{"mg"});  // lists are replaced, not appended
  CHECK(base["units"][1]["id"] == "c");
  CHECK(base["warheads"]["ap"]["light"] == 90);
  CHECK(base["warheads"]["ap"]["facing"]["rear"] == 300);
  CHECK(base["warheads"]["ap"]["facing"]["side"] == 150);
  CHECK_FALSE(base["warheads"].contains("he"));
}

TEST_CASE("a diff of edited rules, merged back onto the base, gives the edited rules") {
  json docs[kRuleFileCount];
  REQUIRE(loadBase(docs));
  json edited = docs[1];
  edited["units"][0]["cost"] = 999;
  edited["units"][0].erase("description");
  edited["units"].erase(edited["units"].begin() + 3);
  json extra = edited["units"][1];
  extra["id"] = "ger_super_rifleman";
  extra["health"] = 250;
  edited["units"].push_back(extra);

  const json patch = diffRules(docs[1], edited);
  CHECK(patch["units"].size() == 3);  // the changed entry, the removed one, the new one
  json rebuilt = docs[1];
  mergeRules(rebuilt, patch);
  CHECK(rebuilt == edited);
  CHECK(diffRules(docs[1], docs[1]).empty());  // nothing changed: an empty mod file
}

TEST_CASE("mods load in order: each after the mods it needs, missing and broken ones left out") {
  TempDir tmp("modorder");
  writeJson(tmp.path + "base_mods/tanks/mod.json", {{"name", "Tanks"}, {"requires", {"core_fix"}}});
  writeJson(tmp.path + "base_mods/core_fix/mod.json", {{"name", "Core fix"}});
  fs::create_directories(tmp.path + "base_mods/broken");
  writeJson(tmp.path + "user_mods/tanks/mod.json", {{"name", "Tanks (mine)"}, {"requires", {"core_fix"}}});

  const auto mods = scanMods({tmp.path + "base_mods", tmp.path + "user_mods"});
  REQUIRE(mods.size() == 3);
  const ModInfo* tanks = findMod(mods, "tanks");
  REQUIRE(tanks);
  CHECK(tanks->name == "Tanks (mine)");  // the player's copy wins
  CHECK(tanks->user);
  CHECK_FALSE(findMod(mods, "broken")->error.empty());

  std::string warnings;
  auto order = resolveLoadOrder({"tanks", "broken", "gone", "core_fix"}, mods, warnings);
  CHECK(order == std::vector<std::string>{"core_fix", "tanks"});
  CHECK(warnings.find("gone") != std::string::npos);
  CHECK(warnings.find("broken") != std::string::npos);

  warnings.clear();
  order = resolveLoadOrder({"tanks"}, mods, warnings);  // its requirement isn't enabled
  CHECK(order == std::vector<std::string>{"tanks"});
  CHECK(warnings.find("core_fix") != std::string::npos);
}

TEST_CASE("content layers: a mod's rules sit on the base, and the rules editor sees what is below its mod") {
  TempDir tmp("layers");
  ModInfo mod;
  std::string err;
  REQUIRE(createMod(tmp.path, modIdFromName("Heavy Armour!"), "Heavy Armour!", mod, err));
  CHECK(mod.id == "heavy_armour");
  CHECK_FALSE(createMod(tmp.path, "heavy_armour", "again", mod, err));
  writeJson(mod.dir + "rules/units.json", {{"units", json::array({{{"id", "ger_heavy_tank"}, {"health", 4000}}})}});

  ContentLayers layers;
  layers.addRoot(std::string(LEGION_RULES_DIR) + "../");
  layers.addRoot(mod.dir, mod.id);
  json docs[kRuleFileCount], below;
  for (int i = 0; i < kRuleFileCount; ++i) REQUIRE(layers.loadRules(kRuleFiles[i], docs[i], err));
  REQUIRE(layers.loadRulesBelow("units.json", mod.id, below, err));
  CHECK((*entry(docs[1], "units", "ger_heavy_tank"))["health"] == 4000);
  CHECK((*entry(below, "units", "ger_heavy_tank"))["health"] != 4000);
  CHECK(layers.all("rules/units.json").size() == 2);
  CHECK(layers.all("rules/weapons.json").size() == 1);

  Loaded rules;
  REQUIRE_MESSAGE(loadAll(docs, rules, err), err);
  CHECK(rules.types.get(rules.types.find("ger_heavy_tank")).maxHealth == 4000);

  json plain[kRuleFileCount];
  REQUIRE(loadBase(plain));
  CHECK(rulesHash(docs) != rulesHash(plain));
  json commented[kRuleFileCount] = {plain[0], plain[1], plain[2]};
  commented[1]["_comment"] = "another comment";
  CHECK(rulesHash(commented) == rulesHash(plain));  // comments don't change the game

  std::ofstream(mod.dir + "rules/weapons.json") << "{ \"weapons\": [ oops ] }";
  CHECK_FALSE(layers.loadRules("weapons.json", docs[0], err));
  CHECK(err.find("weapons.json") != std::string::npos);
}

TEST_CASE("rules changed in a running battle keep every type number and change the units already fighting") {
  json docs[kRuleFileCount];
  REQUIRE(loadBase(docs));
  Loaded live;
  std::string err;
  REQUIRE(loadAll(docs, live, err));

  Map map;
  map.resize(48, 48);
  Simulation sim(map, live.types, live.weapons, 7);
  const auto spawnOne = [&](const char* id, uint8_t owner, Vec2i at) {
    Command c;
    c.type = CommandType::SpawnList;
    c.spawns = {{static_cast<uint16_t>(live.types.find(id)), owner, at, 0, -1}};
    sim.queue(c);
    sim.tick();
    REQUIRE(sim.spawnLog().size() == 1);
    return sim.spawnLog()[0].entity;
  };
  const Entity tank = spawnOne("ger_medium_tank", 0, {10 * T, 10 * T});
  const Entity rifle = spawnOne("rus_rifleman", 1, {30 * T, 30 * T});
  const Entity truck = spawnOne("ger_mcv", 0, {14 * T, 10 * T});
  const int rifleType = live.types.find("rus_rifleman");
  const int tankType = live.types.find("ger_medium_tank");
  const int yardType = live.types.find("ger_yard");

  // The edit: the tank loses its gun, riflemen are deleted, the yard is resized,
  // weapons are reordered, and a new unit comes in.
  json next[kRuleFileCount] = {docs[0], docs[1], docs[2]};
  for (auto& u : next[1]["units"])
    if (u["id"] == "ger_medium_tank") {
      u["weapons"] = json::array();
      u.erase("turret");
      u["health"] = 50;
    }
  auto& units = next[1]["units"];
  units.erase(std::remove_if(units.begin(), units.end(), [](const json& u) { return u["id"] == "rus_rifleman"; }),
              units.end());
  json added = *entry(docs[1], "units", "ger_sniper");
  added["id"] = "ger_marksman";
  units.insert(units.begin(), added);
  std::reverse(next[0]["weapons"].begin(), next[0]["weapons"].end());
  for (auto& b : next[2]["units"])
    if (b["id"] == "ger_yard") b["footprint"] = json::array({2, 2});

  std::string notes;
  stabiliseRules(next, docs, notes);
  CHECK(notes.find("rus_rifleman") != std::string::npos);
  CHECK(notes.find("ger_yard") != std::string::npos);

  Loaded fresh;
  REQUIRE_MESSAGE(loadAll(next, fresh, err), err);
  CHECK(fresh.types.find("rus_rifleman") == rifleType);
  CHECK(fresh.types.find("ger_medium_tank") == tankType);
  CHECK(fresh.types.get(rifleType).hidden);
  CHECK(fresh.types.get(yardType).footW == live.types.get(yardType).footW);
  CHECK(fresh.types.find("ger_marksman") == static_cast<int>(live.types.size()));  // new types go last
  for (size_t w = 0; w < live.weapons.size(); ++w)
    CHECK(fresh.weapons.get(static_cast<int>(w)).id == live.weapons.get(static_cast<int>(w)).id);

  // A second change in the same battle, made from the editor's copy (where the
  // new unit sits in units.json): numbers still hold.
  json again[kRuleFileCount] = {docs[0], docs[1], docs[2]};
  again[1]["units"].insert(again[1]["units"].begin(), added);
  json applied[kRuleFileCount] = {next[0], next[1], next[2]};
  notes.clear();
  stabiliseRules(again, applied, notes);
  Loaded second;
  REQUIRE_MESSAGE(loadAll(again, second, err), err);
  CHECK(second.types.find("ger_marksman") == fresh.types.find("ger_marksman"));
  CHECK(second.types.find("rus_rifleman") == rifleType);
  CHECK_FALSE(second.types.get(rifleType).hidden);  // back again

  live = std::move(fresh);  // what the game does: the battle keeps its references
  sim.rulesChanged();
  Registry& reg = sim.registry();
  CHECK(reg.get<CUnit>(tank).health == 50);
  CHECK_FALSE(reg.has<CCombat>(tank));
  CHECK_FALSE(reg.has<CTurret>(tank));
  CHECK(reg.has<CCombat>(rifle));  // a deleted type still fights with what it had
  CHECK(reg.alive(truck));
  CHECK_FALSE(sim.canProduce(1, rifleType));
  for (int i = 0; i < 60; ++i) sim.tick();  // and the battle runs on
  CHECK(reg.alive(tank));
}

TEST_CASE("the example mod that ships with the game loads cleanly") {
  const std::string mods = std::string(LEGION_RULES_DIR) + "../../mods/";
  const auto installed = scanMods({mods});
  const ModInfo* m = findMod(installed, "example_veterans");
  REQUIRE(m);
  CHECK(m->error.empty());
  ContentLayers layers;
  layers.addRoot(std::string(LEGION_RULES_DIR) + "../");
  layers.addRoot(m->dir, m->id);
  json docs[kRuleFileCount];
  std::string err;
  for (int i = 0; i < kRuleFileCount; ++i) REQUIRE(layers.loadRules(kRuleFiles[i], docs[i], err));
  Loaded rules;
  REQUIRE_MESSAGE(loadAll(docs, rules, err), err);
  const int vet = rules.types.find("ger_veteran");
  REQUIRE(vet >= 0);
  CHECK(rules.types.get(vet).maxHealth == 160);
  CHECK(rules.types.get(rules.types.find("ger_rifleman")).cost == 12);
  CHECK(rules.types.get(rules.types.find("ger_rifleman")).maxHealth == 100);
  CHECK(rules.weapons.get(rules.weapons.find("rifle")).accuracy == 75);
}

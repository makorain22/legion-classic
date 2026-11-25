// Legion Classic - the game's content checks. See content_checks.hpp.
#include "content_checks.hpp"

#include <algorithm>
#include <memory>
#include <set>

#include <nlohmann/json.hpp>

#include "pioneer/core/content.hpp"
#include "pioneer/map/map_io.hpp"
#include "pioneer/sim/factions.hpp"
#include "pioneer/sim/script.hpp"
#include "pioneer/sim/script_registry.hpp"
#include "pioneer/sim/unit_types.hpp"
#include "pioneer/sim/weapons.hpp"
#include "pioneer/tools/validate.hpp"

using namespace pioneer;
using pioneer::tools::Context;
using nlohmann::json;

namespace legion {

namespace {

struct Rules {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  bool ok = false;
};

// The rules as the game merges them: every layer's copy, lowest first.
std::shared_ptr<Rules> mergedRules(Context& c, bool report) {
  auto out = std::make_shared<Rules>();
  json docs[kRuleFileCount];
  for (int i = 0; i < kRuleFileCount; ++i) {
    const std::string rel = std::string("rules/") + kRuleFiles[i];
    const auto layers = c.fs().layers(rel);
    if (layers.empty()) {
      if (report) c.error(rel, "is missing: the game can't start without it");
      return out;
    }
    bool any = false;
    for (const vfs::FileInfo& f : layers) {
      std::vector<uint8_t> bytes;
      if (!c.fs().readIn(f.mount, rel, bytes)) return out;
      json doc = json::parse(bytes.begin(), bytes.end(), nullptr, false, true);
      if (doc.is_discarded()) return out;  // the json check says where
      if (!doc.is_object()) {
        if (report) c.error(rel, "a rule file is a JSON object", c.fs().describe(f.mount, rel));
        return out;
      }
      if (!any) docs[i] = std::move(doc);
      else mergeRules(docs[i], doc);
      any = true;
    }
    if (report) c.counted(layers.size());
  }
  std::string err;
  if (!loadRuleSet(docs[0], docs[1], docs[2], out->weapons, out->types, err)) {
    if (report) {
      // "units.json: ..." -> the file it names.
      std::string path = "rules/units.json";
      for (const char* f : kRuleFiles)
        if (err.find(f) != std::string::npos) {
          path = std::string("rules/") + f;
          break;
        }
      c.error(path, err);
    }
    return out;
  }
  out->ok = true;
  return out;
}

void checkRules(Context& c) {
  auto rules = mergedRules(c, true);
  if (!rules->ok) return;
  // Every playable faction can field something.
  for (const auto& f : kPlayableFactions) {
    bool any = false;
    for (size_t i = 0; i < rules->types.size(); ++i)
      if (rules->types.get(static_cast<int>(i)).faction == f.id) any = true;
    if (!any) c.warning("rules/units.json", std::string("the faction '") + f.id + "' has no units or buildings");
  }
}

// Sound events the banks name (any layer), for the script checker.
std::vector<std::string> soundIds(Context& c) {
  std::set<std::string> ids;
  std::vector<std::string> files = {"audio/audio.json"};
  for (const vfs::FileInfo& f : c.fs().list("audio/banks", false, ".json")) files.push_back(f.path);
  for (const std::string& p : files)
    for (const vfs::FileInfo& f : c.fs().layers(p)) {
      std::vector<uint8_t> bytes;
      if (!c.fs().readIn(f.mount, p, bytes)) continue;
      const json j = json::parse(bytes.begin(), bytes.end(), nullptr, false, true);
      if (j.is_object() && j.contains("sounds") && j["sounds"].is_object())
        for (const auto& [k, v] : j["sounds"].items()) ids.insert(k);
    }
  return {ids.begin(), ids.end()};
}

void checkMaps(Context& c, bool scripts) {
  auto rules = mergedRules(c, false);
  const std::vector<std::string> sounds = scripts ? soundIds(c) : std::vector<std::string>{};
  for (const std::string& p : c.files("", {".lmap"})) {
    c.counted();
    std::vector<uint8_t> bytes;
    if (!c.read(p, bytes)) continue;
    MapFile mf;
    std::string err;
    if (!decodeMap(bytes, mf, err)) {
      if (!scripts) c.error(p, err.empty() ? "is not a map that can be opened" : err);
      continue;
    }
    if (!scripts) {
      if (mf.map.width() <= 0 || mf.map.height() <= 0) c.error(p, "has no tiles");
      std::set<std::string> unknown;
      size_t outside = 0;
      for (const UnitPlacement& u : mf.units) {
        if (rules->ok && rules->types.find(u.type) < 0) unknown.insert(u.type);
        const int64_t tx = u.x / LEPTONS_PER_TILE, ty = u.y / LEPTONS_PER_TILE;
        if (u.x < 0 || u.y < 0 || tx >= mf.map.width() || ty >= mf.map.height()) ++outside;
      }
      for (const std::string& t : unknown) c.error(p, "places '" + t + "', a unit the rules don't have");
      if (outside) c.error(p, std::to_string(outside) + (outside == 1 ? " unit stands" : " units stand") + " off the map");
      for (size_t i = 0; i < mf.players.size(); ++i) {
        const PlayerSetup& ps = mf.players[i];
        if (!ps.active) continue;
        bool known = false;
        for (const auto& f : kPlayableFactions) known = known || ps.faction == f.id;
        if (!known) c.error(p, "side " + std::to_string(i + 1) + " plays '" + ps.faction + "', a faction the game doesn't have");
      }
      continue;
    }
    if (mf.scriptJson.empty() || !rules->ok) continue;
    const json sj = json::parse(mf.scriptJson, nullptr, false);
    if (sj.is_discarded()) {
      c.error(p, "the mission script is damaged");
      continue;
    }
    const ScenarioScript s = scriptFromJson(sj, &rules->types);
    script::CheckContext ctx;
    ctx.types = &rules->types;
    ctx.map = &mf.map;
    ctx.sounds = sounds;
    for (const UnitPlacement& u : mf.units)
      if (!u.name.empty()) ctx.placedNames.push_back(u.name);
    for (const script::Issue& i : script::check(s, ctx)) {
      std::string where;
      if (i.trigger >= 0) {
        where = "trigger " + std::to_string(i.trigger + 1);
        if (i.trigger < static_cast<int>(s.triggers.size()) && !s.triggers[static_cast<size_t>(i.trigger)].name.empty())
          where += " '" + s.triggers[static_cast<size_t>(i.trigger)].name + "'";
        if (i.item >= 0) where += (i.action ? ", action " : ", condition ") + std::to_string(i.item + 1);
      }
      const auto sev = i.severity == script::Issue::Error     ? tools::Issue::Severity::Error
                       : i.severity == script::Issue::Warning ? tools::Issue::Severity::Warning
                                                              : tools::Issue::Severity::Note;
      c.report(sev, p, i.text, where);
    }
  }
}

void checkCampaigns(Context& c) {
  for (const std::string& p : c.files("campaigns/", {".json"})) {
    if (p.size() < 14 || p.compare(p.size() - 13, 13, "campaign.json") != 0) continue;
    c.counted();
    const json* doc = c.json(p);
    if (!doc) continue;
    if (!doc->is_object()) {
      c.error(p, "a campaign is a JSON object");
      continue;
    }
    const std::string folder = p.substr(0, p.rfind('/') + 1);
    if (doc->value("title", std::string()).empty()) c.warning(p, "has no \"title\"");
    const std::string faction = doc->value("faction", std::string());
    bool known = faction.empty();
    for (const auto& f : kPlayableFactions) known = known || faction == f.id;
    if (!known) c.error(p, "is played as '" + faction + "', a faction the game doesn't have");
    const auto missions = doc->find("missions");
    if (missions == doc->end() || !missions->is_array() || missions->empty()) {
      c.error(p, "has no missions");
      continue;
    }
    std::set<std::string> ids;
    size_t n = 0;
    for (const json& m : *missions) {
      ++n;
      const std::string at = "mission " + std::to_string(n);
      if (!m.is_object()) {
        c.error(p, "is not an object", at);
        continue;
      }
      const std::string id = m.value("id", std::string());
      if (id.empty()) c.error(p, "has no \"id\": the game skips it", at);
      else if (!ids.insert(id).second) c.error(p, "has the id '" + id + "' of an earlier mission: progress would mix them up", at);
      const std::string map = m.value("map", std::string());
      if (map.empty()) c.error(p, "has no \"map\": the game skips it", at);
      else if (!c.fs().exists(folder + map)) c.error(p, "plays '" + map + "', which is not in the campaign's folder", at);
      const std::string brief = m.value("briefing", std::string());
      if (brief.empty()) c.warning(p, "has no briefing", at);
      else if (!c.fs().exists(folder + brief)) c.error(p, "is briefed by '" + brief + "', which is not in the campaign's folder", at);
    }
  }
}

}  // namespace

void addContentChecks(tools::Validator& v) {
  v.add({"rules", "The rule files, merged over every layer as the game merges them, make a sound rule set.", checkRules});
  v.add({"maps", "Every map opens; placed units are known types on the map; sides play real factions.",
         [](Context& c) { checkMaps(c, false); }});
  v.add({"missions", "Every mission script passes the script checker against its map, the rules and the sounds.",
         [](Context& c) { checkMaps(c, true); }});
  v.add({"campaigns", "Campaigns list missions with ids, maps and briefings that exist.", checkCampaigns});
}

}  // namespace legion

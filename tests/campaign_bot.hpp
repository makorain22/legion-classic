// The campaign test bot and the checks every campaign gets (9.10, shared by
// the Germany, Soviet and British campaign tests in 9.11-9.12).
//
// The bot is a script tester, not a player: it makes the player's units
// invulnerable, clears the enemy out of each open objective's area and moves
// men into it, and waits out the clocks. What it proves is that every
// mission's story can be followed from the opening to the victory.
#pragma once
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "campaign.hpp"
#include "mission_battle.hpp"

namespace campaignkit {
using namespace pioneer;
using namespace legion;
using testkit::Battle;
using testkit::kContent;
using testkit::Rules;

inline std::vector<std::string> missionFiles(const std::string& dir) {
  std::vector<std::string> out;
  for (const auto& e : std::filesystem::directory_iterator(dir))
    if (e.path().extension() == ".lmap") out.push_back(e.path().filename().string());
  std::sort(out.begin(), out.end());
  return out;
}

inline bool hostile(const Simulation& sim, uint8_t owner) { return owner < MAX_PLAYERS && !sim.allied(owner, 0); }

struct Bot {
  Battle& b;
  int steps = 0;
  void step() {
    Simulation& sim = *b.sim;
    Registry& reg = sim.registry();
    ++steps;
    // The player's side can't be beaten: this tests the story, not the balance.
    {
      Command tag;
      tag.type = CommandType::TagUnits;
      tag.player = NEUTRAL_OWNER;
      tag.units = b.units(0);
      tag.count = kScriptInvulnerable;
      tag.amount = kScriptInvulnerable;
      if (!tag.units.empty()) sim.queue(tag);
    }
    const ScenarioScript& sc = sim.script();
    std::set<uint32_t> moved;
    for (const Objective& o : sim.objectives()) {
      if (o.state != 0 || o.optional || !(o.player == 0 || o.player == ANY_PLAYER)) continue;
      if (o.area < 0 || o.area >= static_cast<int>(sc.areas.size())) {
        // No place to go: whatever named enemy the story is about.
        Command kill;
        kill.type = CommandType::Kill;
        for (size_t n = 1; n < sim.names().size(); ++n)
          for (Entity e : sim.named(sim.names()[n]))
            if (hostile(sim, reg.get<CUnit>(e).owner)) kill.units.push_back(e);
        if (!kill.units.empty()) sim.queue(kill);
        continue;
      }
      const ScriptArea& a = sc.areas[static_cast<size_t>(o.area)];
      auto inside = [&](Entity e) {
        const CPosition& p = reg.get<CPosition>(e);
        return a.contains(p.x >> 8, p.y >> 8);
      };
      // Clear the enemy out of it.
      Command kill;
      kill.type = CommandType::Kill;
      for (uint32_t e : reg.pool<CUnit>().entities()) {
        const CUnit& u = reg.pool<CUnit>().get(e);
        if (u.health > 0 && hostile(sim, u.owner) && inside(reg.handle(e))) kill.units.push_back(reg.handle(e));
      }
      if (!kill.units.empty()) sim.queue(kill);
      // Move men in: named units first, then one of every kind, then more, up to a dozen.
      std::vector<Entity> pick;
      std::set<int> kinds;
      std::vector<Entity> mine = b.units(0, [](const UnitTypeDef& d, Entity) { return !d.isStructure(); });
      std::stable_sort(mine.begin(), mine.end(), [&](Entity x, Entity y) {
        return !sim.unitName(x).empty() && sim.unitName(y).empty();
      });
      for (int pass = 0; pass < 2; ++pass)
        for (Entity e : mine) {
          if (pick.size() >= 12 || moved.count(e.index) || inside(e) || reg.has<CGarrisoned>(e)) continue;
          const int type = reg.get<CUnit>(e).type;
          if (pass == 0 && !sim.unitName(e).empty()) {
            pick.push_back(e);
          } else if (pass == 0 && !kinds.count(type)) {
            kinds.insert(type);
            pick.push_back(e);
          } else if (pass == 1 && std::find(pick.begin(), pick.end(), e) == pick.end()) {
            pick.push_back(e);
          }
        }
      Command mv;
      mv.type = CommandType::Edit;
      const int w = std::max(1, a.x1 - a.x0 - 1), h = std::max(1, a.y1 - a.y0 - 1);
      for (size_t i = 0; i < pick.size(); ++i) {
        UnitEdit ed;
        ed.unit = pick[i];
        ed.move = true;
        const int k = static_cast<int>(i) * 7 + steps;
        ed.pos = {(a.x0 + 1 + (k * 3) % w) * 256 + 128, (a.y0 + 1 + (k * 5) % h) * 256 + 128};
        mv.edits.push_back(ed);
        moved.insert(pick[i].index);
      }
      if (!mv.edits.empty()) sim.queue(mv);
    }
  }
};

inline std::string describe(const Simulation& sim) {
  std::string s;
  for (size_t i = 0; i < sim.objectives().size(); ++i) {
    const Objective& o = sim.objectives()[i];
    s += "  [" + std::string(o.state == 1 ? "x" : o.state == 2 ? "!" : o.state == 3 ? "-" : " ") + "] " + o.text + "\n";
  }
  for (const MissionCountdown& d : sim.countdowns()) s += "  clock " + d.label + " " + std::to_string(sim.countdownLeft(d.label)) + "\n";
  return s;
}

// campaign.json: the title, `missions` missions in five acts, every map and briefing present.
inline void checkCampaignFiles(const std::string& id, const std::string& title, size_t missions) {
  std::string warnings;
  const auto campaigns = scanCampaigns({kContent + "campaigns/"}, warnings);
  const CampaignInfo* c = nullptr;
  for (const CampaignInfo& x : campaigns)
    if (x.id == id) c = &x;
  REQUIRE(c);
  CHECK(c->title == title);
  CHECK(c->missions.size() == missions);
  std::set<std::string> acts;
  for (size_t i = 0; i < c->missions.size(); ++i) {
    const MissionInfo& m = c->missions[i];
    INFO(m.id);
    CHECK(std::filesystem::exists(c->mapPath(i)));
    CHECK(std::filesystem::exists(c->briefingPath(i)));
    CHECK_FALSE(m.act.empty());
    CHECK_FALSE(m.formation.empty());
    acts.insert(m.act);
  }
  CHECK(acts.size() == 5);
}

// Every mission loads, every reference points at something, it can be won and
// lost, and its opening (a cinematic that ends) plays without loss.
inline void checkMissionsOpen(const std::string& dir) {
  Rules R;
  const auto files = missionFiles(dir);
  REQUIRE(files.size() >= 4);
  for (const std::string& f : files) {
    INFO(f);
    Battle b(R);
    REQUIRE(b.load(dir + f));
    bool victory = false, defeat = false;
    for (const Trigger& t : b.script.triggers) {
      for (const Condition& c : t.conditions) {
        CHECK(c.area < static_cast<int>(b.script.areas.size()));
        CHECK(c.trigger < static_cast<int>(b.script.triggers.size()));
      }
      for (const Action& a : t.actions) {
        CHECK(a.area < static_cast<int>(b.script.areas.size()));
        CHECK(a.area2 < static_cast<int>(b.script.areas.size()));
        CHECK(a.trigger < static_cast<int>(b.script.triggers.size()));
        victory = victory || a.type == ActionType::Victory;
        defeat = defeat || a.type == ActionType::Defeat;
        if (a.type == ActionType::SpawnUnits || a.type == ActionType::SpawnNamed || a.type == ActionType::SpawnFromFlag)
          CHECK(a.unitType >= 0);
      }
    }
    CHECK(victory);
    CHECK(defeat);
    REQUIRE(b.runUntil([&] { return !b.sim->cinematic(0) && b.sim->tickCount() > 30; }, 40));
    CHECK_FALSE(b.sim->objectives().empty());
    b.runUntil([&] { return b.sim->playerState(0).defeated; }, 90);
    CHECK_FALSE(b.sim->playerState(0).defeated);
    CHECK_FALSE(b.won());
  }
}

// The bot plays every mission (LEGION_PLAYTHROUGH: "all" by default, a list of
// mission prefixes such as "g01,r05", or "none").
inline void playThrough(const std::string& dir) {
  const char* env = std::getenv("LEGION_PLAYTHROUGH");
  const std::string want = env ? env : "all";
  if (want == "none") return;
  Rules R;
  for (const std::string& f : missionFiles(dir)) {
    if (want != "all" && want.find(f.substr(0, 3)) == std::string::npos) continue;
    INFO(f);
    Battle b(R);
    REQUIRE(b.load(dir + f));
    Bot bot{b};
    const bool won = b.runUntil([&] { return b.won() || b.sim->playerState(0).defeated; }, 150 * 60, [&] {
      if (b.sim->tickCount() % (10 * TICKS_PER_SECOND) < 30) bot.step();
    });
    MESSAGE(f << ": " << std::string(b.won() ? "won" : "NOT won") << " after " << b.sim->tickCount() / TICKS_PER_SECOND / 60 << " min\n"
              << describe(*b.sim));
    CHECK(won);
    CHECK(b.won());
  }
}

}  // namespace campaignkit

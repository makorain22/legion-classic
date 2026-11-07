#pragma once
// Legion Classic - campaigns and the tutorial.
//
// A campaign is a folder under content/campaigns/ (or a mod's campaigns/)
// holding campaign.json, the mission maps (.lmap, scripted in the editor or
// by tools/make_scenarios.cpp) and a briefing per mission in Markdown:
//
//   {"id": "gbr", "title": "...", "faction": "gbr", "order": 3, "tutorial": false,
//    "description": "...",
//    "missions": [{"id": "b1", "title": "...", "map": "b1.lmap", "briefing": "b1.md",
//                  "place": "...", "date": "..."}]}
//
// Missions open one after another. Winning one records it in the player's
// progress (progress.json in the user folder) with the campaign flags its
// script set (Set campaign flag), which the next missions can test
// (Campaign flag at least): a tank that survived comes back, for instance.

#include <map>
#include <set>
#include <string>
#include <vector>

namespace legion {

struct MissionInfo {
  std::string id, title, map, briefing, place, date;
};

struct CampaignInfo {
  std::string id, dir, title, faction, description;
  int order = 0;
  bool tutorial = false;
  std::vector<MissionInfo> missions;
  std::string mapPath(size_t m) const { return dir + missions[m].map; }
  std::string briefingPath(size_t m) const { return dir + missions[m].briefing; }
};

// Every campaign in the given folders (each holds one folder per campaign), sorted by order.
// A later folder's campaign with the same id replaces an earlier one (mods).
std::vector<CampaignInfo> scanCampaigns(const std::vector<std::string>& dirs, std::string& warnings);

struct CampaignProgress {
  std::map<std::string, std::set<std::string>> done;                    // campaign -> missions won
  std::map<std::string, std::map<std::string, int32_t>> flags;         // campaign -> flags
  bool load(const std::string& path);
  bool save(const std::string& path) const;
  bool won(const std::string& campaign, const std::string& mission) const;
  // The first mission not yet won can be played, and every one before it.
  bool open(const CampaignInfo& c, size_t mission) const;
  int wonCount(const CampaignInfo& c) const;
  void recordWin(const std::string& campaign, const std::string& mission, const std::map<std::string, int32_t>& f);
};

// A small Markdown reader for briefings: "# title", "## heading", "- bullet",
// "> quote", blank lines between paragraphs, *italic* and **bold** markers dropped.
struct BriefingLine {
  enum Kind { Title, Heading, Para, Bullet, Quote, Gap } kind = Para;
  std::string text;
};
std::vector<BriefingLine> parseBriefing(const std::string& markdown);

}  // namespace legion

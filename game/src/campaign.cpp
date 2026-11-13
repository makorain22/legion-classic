#include "campaign.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>

namespace legion {

std::vector<CampaignInfo> scanCampaigns(const std::vector<std::string>& dirs, std::string& warnings) {
  std::vector<CampaignInfo> out;
  namespace fs = std::filesystem;
  for (const std::string& root : dirs) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) continue;
    std::vector<fs::path> folders;
    for (const auto& e : fs::directory_iterator(root, ec))
      if (e.is_directory()) folders.push_back(e.path());
    std::sort(folders.begin(), folders.end());
    for (const fs::path& f : folders) {
      std::ifstream in(f / "campaign.json");
      if (!in) continue;
      const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
      if (j.is_discarded()) {
        warnings += (f / "campaign.json").string() + " is damaged\n";
        continue;
      }
      CampaignInfo c;
      c.id = j.value("id", f.filename().string());
      c.dir = f.string() + "/";
      c.title = j.value("title", c.id);
      c.faction = j.value("faction", std::string());
      c.description = j.value("description", std::string());
      c.order = j.value("order", 100);
      c.tutorial = j.value("tutorial", false);
      for (const auto& m : j.value("missions", nlohmann::json::array())) {
        MissionInfo mi;
        mi.id = m.value("id", std::string());
        mi.title = m.value("title", mi.id);
        mi.map = m.value("map", std::string());
        mi.briefing = m.value("briefing", std::string());
        mi.place = m.value("place", std::string());
        mi.date = m.value("date", std::string());
        mi.act = m.value("act", std::string());
        mi.formation = m.value("formation", std::string());
        if (mi.id.empty() || mi.map.empty()) {
          warnings += c.id + ": a mission without an id or map was skipped\n";
          continue;
        }
        c.missions.push_back(std::move(mi));
      }
      if (c.missions.empty()) continue;
      auto same = std::find_if(out.begin(), out.end(), [&](const CampaignInfo& o) { return o.id == c.id; });
      if (same != out.end()) *same = std::move(c);
      else out.push_back(std::move(c));
    }
  }
  std::stable_sort(out.begin(), out.end(), [](const CampaignInfo& a, const CampaignInfo& b) { return a.order < b.order; });
  return out;
}

bool CampaignProgress::load(const std::string& path) {
  std::ifstream in(path);
  if (!in) return false;
  const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (!j.is_object()) return false;
  done.clear();
  flags.clear();
  const nlohmann::json campaigns = j.value("campaigns", nlohmann::json::object());
  // Missions renumbered when their campaign grew (9.10-9.12): an old win counts
  // for the mission it became.
  static const std::map<std::string, std::map<std::string, std::string>> kRenamed = {
      {"ger", {{"g1", "g09"}, {"g2", "g10"}, {"g3", "g12"}}},
      {"rus", {{"r1", "r01"}, {"r2", "r02"}, {"r3", "r10"}}},
      {"gbr", {{"b1", "b06"}, {"b2", "b07"}, {"b3", "b18"}}},
  };
  for (const auto& [id, c] : campaigns.items()) {
    const auto renames = kRenamed.find(id);
    for (const auto& m : c.value("won", nlohmann::json::array()))
      if (m.is_string()) {
        std::string mid = m.get<std::string>();
        if (renames != kRenamed.end()) {
          const auto r = renames->second.find(mid);
          if (r != renames->second.end()) mid = r->second;
        }
        done[id].insert(mid);
      }
    const nlohmann::json f = c.value("flags", nlohmann::json::object());
    for (const auto& [k, v] : f.items())
      if (v.is_number_integer()) flags[id][k] = v.get<int32_t>();
  }
  return true;
}

bool CampaignProgress::save(const std::string& path) const {
  nlohmann::json j;
  auto& cs = j["campaigns"] = nlohmann::json::object();
  for (const auto& [id, missions] : done) cs[id]["won"] = std::vector<std::string>(missions.begin(), missions.end());
  for (const auto& [id, f] : flags) cs[id]["flags"] = f;
  std::ofstream out(path);
  if (!out) return false;
  out << j.dump(2);
  return static_cast<bool>(out);
}

bool CampaignProgress::won(const std::string& campaign, const std::string& mission) const {
  auto it = done.find(campaign);
  return it != done.end() && it->second.count(mission) > 0;
}

bool CampaignProgress::open(const CampaignInfo& c, size_t mission) const {
  for (size_t i = 0; i < mission && i < c.missions.size(); ++i)
    if (!won(c.id, c.missions[i].id)) return false;
  return mission < c.missions.size();
}

int CampaignProgress::wonCount(const CampaignInfo& c) const {
  int n = 0;
  for (const MissionInfo& m : c.missions) n += won(c.id, m.id);
  return n;
}

void CampaignProgress::recordWin(const std::string& campaign, const std::string& mission,
                                 const std::map<std::string, int32_t>& f) {
  done[campaign].insert(mission);
  for (const auto& [k, v] : f) flags[campaign][k] = v;
}

std::vector<BriefingLine> parseBriefing(const std::string& md) {
  std::vector<BriefingLine> out;
  std::istringstream in(md);
  std::string line, para;
  auto clean = [](std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '*'), s.end());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
    return s;
  };
  auto flush = [&]() {
    if (!para.empty()) out.push_back({BriefingLine::Para, clean(para)});
    para.clear();
  };
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t s = 0;
    while (s < line.size() && line[s] == ' ') ++s;
    const std::string t = line.substr(s);
    if (t.empty()) {
      flush();
      if (!out.empty() && out.back().kind != BriefingLine::Gap) out.push_back({BriefingLine::Gap, {}});
    } else if (t.rfind("## ", 0) == 0) {
      flush();
      out.push_back({BriefingLine::Heading, clean(t.substr(3))});
    } else if (t.rfind("# ", 0) == 0) {
      flush();
      out.push_back({BriefingLine::Title, clean(t.substr(2))});
    } else if (t.rfind("- ", 0) == 0 || t.rfind("* ", 0) == 0) {
      flush();
      out.push_back({BriefingLine::Bullet, clean(t.substr(2))});
    } else if (t.rfind("> ", 0) == 0) {
      flush();
      out.push_back({BriefingLine::Quote, clean(t.substr(2))});
    } else {
      para += (para.empty() ? "" : " ") + t;
    }
  }
  flush();
  while (!out.empty() && out.back().kind == BriefingLine::Gap) out.pop_back();
  return out;
}

}  // namespace legion

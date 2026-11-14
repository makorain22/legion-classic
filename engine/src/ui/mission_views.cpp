// A mission's data as graph and timeline models (no graphics: built into the
// tests too).
#include "pioneer/ui/mission_views.hpp"

#include <algorithm>

#include "pioneer/sim/script_registry.hpp"

namespace pioneer::ui {

namespace {
std::string plural(size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

void addGoto(Graph& g, int from, int to, const std::string& label) {
  if (to < 0 || to >= static_cast<int>(g.nodes.size())) return;
  if (from < 0) {  // from anywhere: said on the box, an arrow from every phase would be a thicket
    g.nodes[static_cast<size_t>(to)].lines.push_back("also by " + label);
    return;
  }
  for (GraphLink& l : g.links)
    if (l.from == from && l.to == to && l.dashed) {
      if (l.label.find(label) == std::string::npos) l.label += ", " + label;
      return;
    }
  GraphLink l;
  l.from = from;
  l.to = to;
  l.label = label;
  l.dashed = true;
  g.links.push_back(std::move(l));
}
}  // namespace

Graph phaseGraph(const ScenarioScript& s, int current) {
  Graph g;
  const int n = static_cast<int>(s.phases.size());
  for (int i = 0; i < n; ++i) {
    const MissionPhase& p = s.phases[static_cast<size_t>(i)];
    GraphNode node;
    node.id = i;
    node.title = std::to_string(i) + ": " + p.name;
    if (i == 0) node.lines.push_back("the mission starts here");
    else if (p.enter.empty()) node.lines.push_back("begins by a Go to phase action");
    else node.lines.push_back("begins when " + plural(p.enter.size(), "condition holds", p.any ? "conditions: any holds" : "conditions hold"));
    if (i > 0 && !p.enter.empty() && p.from == -2) node.lines.push_back("from any phase");
    if (!p.onEnter.empty()) node.lines.push_back("on entry: " + plural(p.onEnter.size(), "action", "actions"));
    if (!p.music.empty()) node.lines.push_back("music: " + p.music);
    size_t goals = 0, triggers = 0;
    for (const MissionGoal& goal : s.goals) goals += goal.phase == i ? 1 : 0;
    for (const Trigger& t : s.triggers) triggers += t.phase == i ? 1 : 0;
    if (goals) node.lines.push_back(plural(goals, "goal", "goals"));
    if (triggers) node.lines.push_back(plural(triggers, "trigger", "triggers"));
    if (i == current) node.colour = 0xFF3C8C3Cu;  // green: where the mission is
    g.nodes.push_back(std::move(node));
  }
  // By itself, when its conditions hold.
  for (int i = 1; i < n; ++i) {
    const MissionPhase& p = s.phases[static_cast<size_t>(i)];
    if (p.enter.empty() || p.from == -2) continue;
    const int from = p.from == -1 ? i - 1 : p.from;
    if (from < 0 || from >= n) continue;
    GraphLink l;
    l.from = from;
    l.to = i;
    l.label = plural(p.enter.size(), "condition", "conditions");
    g.links.push_back(std::move(l));
  }
  // By Go to phase actions.
  auto scan = [&](const std::vector<Action>& actions, int from, const std::string& label) {
    for (const Action& a : actions)
      if (a.type == ActionType::GotoPhase) addGoto(g, from, a.value, label);
  };
  for (const Trigger& t : s.triggers) scan(t.actions, t.phase, "trigger '" + t.name + "'");
  for (int i = 0; i < n; ++i) scan(s.phases[static_cast<size_t>(i)].onEnter, i, "on entry");
  for (const MissionGoal& goal : s.goals) {
    scan(goal.onDone, goal.phase, "goal done: " + goal.text);
    scan(goal.onFail, goal.phase, "goal failed: " + goal.text);
  }
  return g;
}

void sequenceTimeline(const MissionSequence& seq, std::vector<TimelineTrack>& tracks, std::vector<TimelineItem>& items) {
  tracks.clear();
  items.clear();
  for (const script::SequenceTrack& t : script::sequenceTracks()) tracks.push_back({t.id, 0, false});
  for (size_t i = 0; i < seq.events.size(); ++i) {
    const MissionSequence::Event& e = seq.events[i];
    int track = -1;
    for (size_t t = 0; t < tracks.size(); ++t)
      if (tracks[t].name == e.track) track = static_cast<int>(t);
    if (track < 0) {
      track = static_cast<int>(tracks.size());
      tracks.push_back({e.track, 0, false});
    }
    TimelineItem it;
    it.id = static_cast<int>(i);
    it.track = track;
    it.start = std::max(0, e.at);
    it.label = e.kind;
    // How long it lasts, where the event says ("seconds" or "duration", in seconds).
    for (const auto& [key, value] : e.params) {
      if (key != "seconds" && key != "duration") continue;
      double secs = 0.0;
      try {
        secs = std::stod(value);
      } catch (...) {
        secs = 0.0;
      }
      if (secs > 0.0 && secs < 36000.0) it.length = std::max(1, static_cast<int>(secs * 10.0 + 0.5));
    }
    for (const auto& [key, value] : e.params)
      if (key == "text" || key == "who" || key == "unit" || key == "to") {
        it.label += ": " + value;
        break;
      }
    items.push_back(std::move(it));
  }
}

bool applyTimeline(MissionSequence& seq, const std::vector<TimelineTrack>& tracks, const std::vector<TimelineItem>& items) {
  if (items.size() != seq.events.size()) return false;
  std::vector<uint8_t> seen(seq.events.size(), 0);
  for (const TimelineItem& it : items) {
    if (it.id < 0 || it.id >= static_cast<int>(seq.events.size()) || seen[static_cast<size_t>(it.id)]) return false;
    if (it.track < 0 || it.track >= static_cast<int>(tracks.size())) return false;
    seen[static_cast<size_t>(it.id)] = 1;
  }
  for (const TimelineItem& it : items) {
    MissionSequence::Event& e = seq.events[static_cast<size_t>(it.id)];
    e.at = std::max(0, it.start);
    e.track = tracks[static_cast<size_t>(it.track)].name;
    if (it.length > 0)
      for (auto& [key, value] : e.params)
        if (key == "seconds" || key == "duration") {
          value = std::to_string(it.length / 10);
          if (it.length % 10) value += "." + std::to_string(it.length % 10);
        }
  }
  std::stable_sort(seq.events.begin(), seq.events.end(),
                   [](const MissionSequence::Event& a, const MissionSequence::Event& b) { return a.at < b.at; });
  return true;
}

void waveTimeline(const ScenarioScript& s, std::vector<TimelineTrack>& tracks, std::vector<TimelineItem>& items) {
  tracks.clear();
  items.clear();
  for (size_t w = 0; w < s.waves.size(); ++w) {
    const MissionWave& wave = s.waves[w];
    tracks.push_back({wave.name, 0, true});
    const int times = std::clamp(wave.times, 1, 60);
    for (int k = 0; k < times; ++k) {
      TimelineItem it;
      it.id = static_cast<int>(items.size());
      it.track = static_cast<int>(w);
      it.start = std::max(0, wave.delay + k * std::max(0, wave.interval));
      size_t units = 0;
      for (const MissionWave::Entry& e : wave.units) units += static_cast<size_t>(std::max(0, e.count[1]));
      it.label = times > 1 ? std::to_string(k + 1) + "/" + std::to_string(times) : plural(units, "unit", "units");
      items.push_back(std::move(it));
    }
  }
}

}  // namespace pioneer::ui

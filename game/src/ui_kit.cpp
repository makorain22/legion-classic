// The interface kit: a window that shows what the engine's UI framework
// offers (fonts, icons, script field panels, the timeline, the graph, string
// tables) on this game's own data. Opened with `ui.kit` in the console.
// Also the console's commands for languages.
#include <imgui.h>

#include <algorithm>

#include <nlohmann/json.hpp>

#include "game_scene.hpp"
#include "pioneer/core/console.hpp"
#include "pioneer/core/strings.hpp"
#include "pioneer/sim/script_registry.hpp"
#include "pioneer/ui/fonts.hpp"
#include "pioneer/ui/icons.hpp"
#include "pioneer/ui/mission_views.hpp"
#include "pioneer/ui/node_graph.hpp"
#include "pioneer/ui/script_fields.hpp"
#include "pioneer/ui/timeline.hpp"

using namespace pioneer;

namespace legion {

namespace {
using loc::tr;

struct Kit {
  // Script fields.
  bool action = true;
  int entry = 0;
  Condition cond;
  Action act;
  // Timeline.
  bool timelineReady = false;
  MissionSequence sequence;
  std::vector<pioneer::ui::TimelineTrack> tracks;
  std::vector<pioneer::ui::TimelineItem> items;
  pioneer::ui::TimelineView view;
  // Graph.
  pioneer::ui::Graph graph;
  pioneer::ui::GraphView graphView;
  const void* graphOf = nullptr;
  size_t graphPhases = static_cast<size_t>(-1);
  int graphCurrent = -2;
  char filter[64] = "";
};
Kit& kit() {
  static Kit k;
  return k;
}

// A sequence to show: a patrol is ambushed at a bridge.
MissionSequence sampleSequence() {
  MissionSequence q;
  q.name = "Ambush at the bridge";
  auto ev = [&](int at, const char* track, const char* kind, std::vector<std::pair<std::string, std::string>> params) {
    MissionSequence::Event e;
    e.at = at;
    e.track = track;
    e.kind = kind;
    e.params = std::move(params);
    q.events.push_back(std::move(e));
  };
  ev(0, "screen", "letterbox", {{"seconds", "24"}});
  ev(0, "screen", "fade", {{"to", "in"}, {"seconds", "2"}});
  ev(0, "camera", "cut", {{"to", "bridge"}});
  ev(10, "camera", "pan", {{"to", "road"}, {"seconds", "6"}});
  ev(15, "actor", "move", {{"unit", "patrol"}, {"to", "bridge"}, {"seconds", "9"}});
  ev(20, "dialogue", "radio", {{"who", "Patrol leader"}, {"text", "Bridge in sight. All quiet."}, {"seconds", "4"}});
  ev(75, "camera", "orbit", {{"to", "bridge"}, {"seconds", "7"}});
  ev(105, "effects", "explosion", {{"to", "bridge"}});
  ev(105, "camera", "shake", {{"seconds", "1.5"}});
  ev(110, "effects", "smoke", {{"to", "bridge"}, {"seconds", "10"}});
  ev(115, "dialogue", "radio", {{"who", "Patrol leader"}, {"text", "Ambush! Off the road!"}, {"seconds", "3"}});
  ev(120, "actor", "prone", {{"unit", "patrol"}});
  ev(150, "dialogue", "music", {{"to", "battle"}});
  ev(200, "screen", "objective", {{"text", "Clear the far bank"}});
  ev(220, "screen", "fade", {{"to", "out"}, {"seconds", "2"}});
  ev(240, "flow", "end", {});
  return q;
}

// Phases to show when the mission has none.
ScenarioScript sampleMission() {
  ScenarioScript s;
  auto phase = [&](const char* name, int from, int conditions) {
    MissionPhase p;
    p.name = name;
    p.from = from;
    p.enter.resize(static_cast<size_t>(conditions));
    s.phases.push_back(std::move(p));
  };
  phase("Approach", -1, 0);
  phase("Take the bridge", -1, 1);
  phase("Hold against the counter-attack", -1, 2);
  phase("Relief arrives", -1, 1);
  phase("Fall back", 2, 1);
  phase("Debrief", -1, 0);
  Trigger won;
  won.name = "Relief is across";
  won.phase = 3;
  Action go;
  go.type = ActionType::GotoPhase;
  go.value = 5;
  won.actions.push_back(go);
  s.triggers.push_back(won);
  Trigger lost = won;
  lost.name = "The rearguard is out";
  lost.phase = 4;
  s.triggers.push_back(lost);
  Trigger again;
  again.name = "The bridge is lost";
  again.phase = 2;
  go.value = 1;
  again.actions = {go};
  s.triggers.push_back(again);
  MissionGoal g;
  g.type = "hold";
  g.text = "Hold the bridge";
  g.phase = 2;
  s.goals.push_back(g);
  return s;
}
}  // namespace

void GameScene::drawInterfaceKit() {
  if (!showKit_) return;
  Kit& k = kit();
  const float s = pioneer::ui::scale();
  ImGui::SetNextWindowSize(ImVec2(860 * s, 560 * s), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(60, 60), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin((std::string(ICON_LAYERS " ") + tr("kit.title") + "###kit").c_str(), &showKit_)) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginTabBar("##kit")) {
    const auto tab = [&](int n, const char* key) {
      return ImGui::BeginTabItem((std::string(tr(key)) + "###t" + std::to_string(n)).c_str(), nullptr,
                                 kitTab_ == n ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
    };
    // --- Fonts and icons -----------------------------------------------------
    if (tab(0, "kit.fonts")) {
      const pioneer::ui::FontSet& f = pioneer::ui::fonts();
      ImGui::TextUnformatted(tr("kit.regular"));
      {
        pioneer::ui::Bold b;
        ImGui::TextUnformatted(tr("kit.bold"));
      }
      {
        pioneer::ui::Mono m;
        ImGui::TextUnformatted(tr("kit.mono"));
      }
      for (const float times : {0.8f, 1.3f, 1.8f}) {
        pioneer::ui::Sized z(times);
        ImGui::Text("%d%%  Pioneer %s %s %s", static_cast<int>(times * 100.0f + 0.5f), ICON_SAVE, ICON_PLAY, ICON_SETTINGS);
      }
      ImGui::Separator();
      size_t count = 0;
      const pioneer::ui::IconName* icons = pioneer::ui::iconList(count);
      ImGui::TextDisabled("%s", loc::strings().format("kit.icons", {{"n", count}}).c_str());
      if (!f.icons) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "fonts/icons.ttf was not found");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(180 * s);
      ImGui::InputTextWithHint("##iconfilter", "filter", k.filter, sizeof k.filter);
      ImGui::BeginChild("##icons", ImVec2(0, 0));
      const float cell = 44.0f * s;
      const float pitch = cell - 6.0f + ImGui::GetStyle().ItemSpacing.x;
      const int perRow = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + ImGui::GetStyle().ItemSpacing.x) / pitch));
      int shown = 0;
      for (size_t i = 0; i < count; ++i) {
        std::string name = icons[i].name + 5;  // without ICON_
        std::string low = name, want = k.filter;
        for (char& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : want) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!want.empty() && low.find(want) == std::string::npos) continue;
        if (shown++ % perRow != 0) ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        {
          pioneer::ui::Sized z(1.5f);
          if (ImGui::Button(icons[i].glyph, ImVec2(cell - 6.0f, cell - 6.0f))) ImGui::SetClipboardText(icons[i].name);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n(click to copy the name)", icons[i].name);
        ImGui::PopID();
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    // --- Script fields -------------------------------------------------------
    if (tab(1, "kit.fields")) {
      ImGui::TextDisabled("%s", tr("kit.fields_help"));
      bool pick = false;
      if (ImGui::RadioButton("Actions", k.action)) {
        k.action = true;
        k.entry = 0;
        pick = true;
      }
      ImGui::SameLine();
      if (ImGui::RadioButton("Conditions", !k.action)) {
        k.action = false;
        k.entry = 0;
        pick = true;
      }
      const auto& list = k.action ? script::actions() : script::conditions();
      k.entry = std::clamp(k.entry, 0, static_cast<int>(list.size()) - 1);
      ImGui::BeginChild("##list", ImVec2(230 * s, 0), ImGuiChildFlags_Borders);
      // By group, the groups in the order they first appear.
      std::vector<int> order;
      std::vector<const char*> groups;
      for (const script::Entry& e : list)
        if (std::none_of(groups.begin(), groups.end(), [&](const char* g) { return std::string(g) == e.group; })) groups.push_back(e.group);
      for (const char* g : groups)
        for (int i = 0; i < static_cast<int>(list.size()); ++i)
          if (std::string(list[static_cast<size_t>(i)].group) == g) order.push_back(i);
      const char* group = "";
      for (const int i : order) {
        const script::Entry& e = list[static_cast<size_t>(i)];
        if (std::string(group) != e.group) {
          group = e.group;
          ImGui::SeparatorText(group);
        }
        if (ImGui::Selectable((std::string(e.name) + "##" + e.id).c_str(), k.entry == i)) {
          k.entry = i;
          pick = true;
        }
      }
      ImGui::EndChild();
      const script::Entry& e = list[static_cast<size_t>(k.entry)];
      if (pick || (k.action ? k.act.type != static_cast<ActionType>(e.type) : k.cond.type != static_cast<CondType>(e.type))) {
        if (k.action) k.act = script::makeAction(static_cast<ActionType>(e.type));
        else k.cond = script::makeCondition(static_cast<CondType>(e.type));
      }
      ImGui::SameLine();
      ImGui::BeginChild("##edit", ImVec2(0, 0));
      {
        pioneer::ui::Bold b;
        ImGui::TextUnformatted(e.name);
      }
      ImGui::SameLine();
      {
        pioneer::ui::Mono m;
        ImGui::TextDisabled("%s", e.id);
      }
      ImGui::TextWrapped("%s", e.summary);
      ImGui::Separator();
      static const ScenarioScript none;
      pioneer::ui::ScriptFieldContext ctx{sim_ ? &scenario() : &none, &types()};
      if (e.fields.empty()) ImGui::TextDisabled("(nothing to fill in)");
      else if (k.action) pioneer::ui::editFields(e, k.act, ctx);
      else pioneer::ui::editFields(e, k.cond, ctx);
      ImGui::Separator();
      ImGui::TextUnformatted((k.action ? script::describe(k.act, &(sim_ ? scenario() : none)) : script::describe(k.cond, &(sim_ ? scenario() : none))).c_str());
      ImGui::TextDisabled("As it is saved:");
      {
        pioneer::ui::Mono m;
        const nlohmann::json j = k.action ? actionJson(k.act, &types()) : conditionJson(k.cond, &types());
        ImGui::TextWrapped("%s", j.dump(2).c_str());
      }
      ImGui::EndChild();
      ImGui::EndTabItem();
    }
    // --- Timeline ------------------------------------------------------------
    if (tab(2, "kit.timeline")) {
      if (!k.timelineReady) {
        k.sequence = sampleSequence();
        pioneer::ui::sequenceTimeline(k.sequence, k.tracks, k.items);
        k.view.length = 300;
        k.view.zoom = 3.0f;
        k.view.cursor = 60;
        k.timelineReady = true;
      }
      ImGui::TextDisabled("%s", tr("kit.timeline_help"));
      {
        pioneer::ui::Bold b;
        ImGui::TextUnformatted(k.sequence.name.c_str());
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s   %zu events", pioneer::ui::timeLabel(k.view.cursor, k.view.unitsPerSecond).c_str(), k.items.size());
      const pioneer::ui::TimelineResult r = pioneer::ui::timeline("##seq", k.tracks, k.items, k.view);
      if (r.addTrack >= 0) {
        pioneer::ui::TimelineItem it;
        it.id = pioneer::ui::nextItemId(k.items);
        it.track = r.addTrack;
        it.start = r.addAt;
        it.label = "new";
        k.items.push_back(it);
        k.view.selected = it.id;
      }
      if (r.removeSelected) {
        std::erase_if(k.items, [&](const pioneer::ui::TimelineItem& it) { return it.id == k.view.selected; });
        k.view.selected = -1;
      }
      if (const pioneer::ui::TimelineItem* it = pioneer::ui::findItem(k.items, k.view.selected)) {
        ImGui::Text("%s  on %s  at %s", it->label.c_str(), k.tracks[static_cast<size_t>(it->track)].name.c_str(),
                    pioneer::ui::timeLabel(it->start, k.view.unitsPerSecond).c_str());
        if (it->length > 0) {
          ImGui::SameLine();
          ImGui::Text(" for %s", pioneer::ui::timeLabel(it->length, k.view.unitsPerSecond).c_str());
        }
      } else {
        ImGui::TextDisabled("Nothing selected. Double-click an empty place to add an event; Delete removes the selected one.");
      }
      if (sim_ && !scenario().waves.empty()) {
        ImGui::SeparatorText("This mission's waves (seconds after they are sent)");
        static std::vector<pioneer::ui::TimelineTrack> wt;
        static std::vector<pioneer::ui::TimelineItem> wi;
        static pioneer::ui::TimelineView wv;
        pioneer::ui::waveTimeline(scenario(), wt, wi);
        wv.unitsPerSecond = 1;
        wv.snap = 1;
        wv.zoom = 3.0f;
        pioneer::ui::timeline("##waves", wt, wi, wv);
      }
      ImGui::EndTabItem();
    }
    // --- Graph ---------------------------------------------------------------
    if (tab(3, "kit.graph")) {
      static const ScenarioScript sample = sampleMission();
      const bool own = sim_ && !scenario().phases.empty();
      const ScenarioScript& sc = own ? scenario() : sample;
      const int current = own ? sim_->missionPhase() : 2;
      if (k.graphOf != &sc || k.graphPhases != sc.phases.size()) {
        k.graph = pioneer::ui::phaseGraph(sc, current);
        k.graphView = {};
        k.graphOf = &sc;
        k.graphPhases = sc.phases.size();
        k.graphCurrent = current;
      } else if (k.graphCurrent != current) {  // the mission moved on: tint, keep the layout
        const pioneer::ui::Graph fresh = pioneer::ui::phaseGraph(sc, current);
        for (size_t i = 0; i < k.graph.nodes.size() && i < fresh.nodes.size(); ++i) k.graph.nodes[i].colour = fresh.nodes[i].colour;
        k.graphCurrent = current;
      }
      ImGui::TextDisabled("%s  %s", own ? loc::strings().format("kit.phases", {{"n", sc.phases.size()}}).c_str() : tr("kit.no_phases"),
                          tr("kit.graph_help"));
      if (ImGui::SmallButton(ICON_REFRESH_CW " Lay out again")) k.graphView.laidOut = false;
      pioneer::ui::nodeGraph("##phases", k.graph, k.graphView);
      ImGui::EndTabItem();
    }
    // --- Strings -------------------------------------------------------------
    if (tab(4, "kit.strings")) {
      loc::Strings& st = loc::strings();
      ImGui::TextUnformatted(st.format("kit.strings_loaded", {{"n", st.size()}, {"language", st.language()}}).c_str());
      bool pseudo = st.pseudo();
      if (ImGui::Checkbox("Pseudo-translation (finds text that is not in the table)", &pseudo)) st.setPseudo(pseudo);
      const std::vector<std::string> missing = st.missing();
      if (!missing.empty()) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.4f, 1), "%s", st.format("kit.strings_missing", {{"n", missing.size()}}).c_str());
        for (const std::string& m : missing) ImGui::BulletText("%s", m.c_str());
      }
      ImGui::SeparatorText("Numbers");
      for (const int n : {0, 1, 2, 5, 21})
        ImGui::BulletText("%s", loc::format("{n|# unit is|# units are} selected", {{"n", n}}, st.rule()).c_str());
      ImGui::EndTabItem();
    }
    // --- Interface scripts ---------------------------------------------------
    if (uiLua_ && tab(5, "kit.scripts")) {
      ImGui::TextDisabled("%s", tr("kit.scripts_help"));
      for (const InterfaceLua::Panel& p : uiLua_->panels()) {
        bool open = p.open;
        if (ImGui::Checkbox((p.title + "##p").c_str(), &open)) uiLua_->setOpen(p.title, open);
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s", p.script.c_str(), p.broken ? "   (stopped after errors; tick it to try again)" : "");
      }
      if (uiLua_->panels().empty()) ImGui::TextDisabled("(no script has declared a panel)");
      if (ImGui::Button(ICON_REFRESH_CW " Read the scripts again")) loadInterfaceScripts();
      ImGui::SameLine();
      ImGui::TextDisabled("%zu scripts, %zu KB", uiLua_->scripts(), uiLua_->memory() / 1024);
      if (!uiLua_->errors().empty()) {
        ImGui::SeparatorText("Errors");
        pioneer::ui::Mono m;
        for (const std::string& e : uiLua_->errors()) ImGui::TextWrapped("%s", e.c_str());
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    kitTab_ = -1;
  }
  ImGui::End();
}

void GameScene::registerKitCommands() {
  Console& con = Console::global();
  con.addCommand("ui.kit", "ui.kit [0|1] [tab] - the interface kit: fonts and icons, script field panels, the timeline, "
                           "the graph, the string tables and the interface scripts, shown on this game's data (tab: 0-5).",
                 [this](const ConsoleArgs& a, Console&) {
                   showKit_ = a.size() > 1 ? a.flag(1) : !showKit_;
                   if (a.size() > 2) kitTab_ = std::atoi(a.str(2).c_str());
                 });
  con.addCommand("ui.scale", "ui.scale [percent] - the interface size for now (the setting is in Settings > Display).",
                 [](const ConsoleArgs& a, Console& c) {
                   if (a.size() > 1) pioneer::ui::setScale(static_cast<float>(std::atof(a.str(1).c_str())) / 100.0f);
                   c.printFmt("interface size: %d%%", static_cast<int>(pioneer::ui::scale() * 100.0f + 0.5f));
                 });
  con.addCommand("lang.list", "lang.list - the languages there are string tables for.", [this](const ConsoleArgs&, Console& c) {
    for (const loc::Strings::Language& l : loc::Strings::available(*content_.fileSystem()))
      c.printFmt("%-6s %-20s %zu strings%s", l.id.c_str(), l.name.c_str(), l.strings, l.id == loc::strings().language() ? "   (in use)" : "");
  });
  con.addCommand("lang.set", "lang.set <id> - switches the interface's language (and keeps it in the settings).",
                 [this](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 2) return c.print("in use: " + loc::strings().language());
                   std::string error;
                   if (!loc::strings().load(*content_.fileSystem(), a.str(1), &error)) return c.error(error.empty() ? "no such language" : error);
                   settings_.language = a.str(1);
                   saveSettings();
                   c.printFmt("%zu strings; %zu not translated yet", loc::strings().size(), loc::strings().untranslated().size());
                 });
  con.addCommand("lang.missing", "lang.missing - keys the interface asked for that no table has, what the language in use "
                                 "hasn't translated, and what it has that English hasn't.",
                 [](const ConsoleArgs&, Console& c) {
                   const loc::Strings& s = loc::strings();
                   for (const std::string& k : s.missing()) c.error("missing: " + k);
                   for (const std::string& k : s.untranslated()) c.print("not translated: " + k);
                   for (const std::string& k : s.orphans()) c.print("not in English: " + k);
                   c.printFmt("%zu missing, %zu not translated, %zu not in English", s.missing().size(), s.untranslated().size(), s.orphans().size());
                 });
  con.addCommand("lang.pseudo", "lang.pseudo [0|1] - pseudo-translation: table text comes out accented and longer, so "
                                "text that isn't from the table stands out.",
                 [](const ConsoleArgs& a, Console& c) {
                   loc::strings().setPseudo(a.size() > 1 ? a.flag(1) : !loc::strings().pseudo());
                   c.print(loc::strings().pseudo() ? "pseudo-translation on" : "pseudo-translation off");
                 });
}

}  // namespace legion

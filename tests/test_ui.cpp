// The interface kit's models (no window needed): the timeline's arithmetic,
// the graph's layout, a mission as graph and timeline, string tables, and
// the fonts the interface ships with.
#include <doctest/doctest.h>

#include <fstream>
#include <set>
#include <sstream>

#include "pioneer/core/strings.hpp"
#include "pioneer/core/vfs.hpp"
#include "pioneer/sim/script_registry.hpp"
#include "pioneer/ui/icons.hpp"
#include "pioneer/ui/mission_views.hpp"
#include "pioneer/ui/node_graph.hpp"
#include "pioneer/ui/timeline.hpp"

using namespace pioneer;
using namespace pioneer::ui;

namespace {
std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
}  // namespace

TEST_CASE("timeline: snapping, labels and ruler steps") {
  CHECK(snapTo(7, 5) == 5);
  CHECK(snapTo(8, 5) == 10);
  CHECK(snapTo(-3, 5) == -5);
  CHECK(snapTo(13, 1) == 13);
  CHECK(snapTo(13, 0) == 13);

  CHECK(timeLabel(0, 10) == "0:00");
  CHECK(timeLabel(70, 10) == "0:07");
  CHECK(timeLabel(650, 10) == "1:05");
  CHECK(timeLabel(25, 10) == "0:02.5");
  CHECK(timeLabel(-25, 10) == "-0:02.5");
  CHECK(timeLabel(15 * 61, 15) == "1:01");
  CHECK(timeLabel(3, 4) == "0:00.75");

  // Labels never closer than asked, whatever the zoom; small lines divide the big ones.
  for (const float ppu : {0.02f, 0.3f, 1.0f, 6.0f, 40.0f, 200.0f})
    for (const int ups : {1, 10, 15}) {
      const RulerSteps r = rulerSteps(ppu, ups, 64.0f);
      CAPTURE(ppu);
      CAPTURE(ups);
      CHECK(r.major >= 1);
      CHECK(r.minor >= 1);
      CHECK(r.major % r.minor == 0);
      if (ppu >= 0.3f) CHECK(static_cast<float>(r.major) * ppu >= 64.0f);
    }
  CHECK(rulerSteps(6.0f, 10).major == 20);   // 2 s at the default zoom
  CHECK(rulerSteps(100.0f, 10).major == 1);  // tenths when zoomed right in
}

TEST_CASE("timeline: items are found, moved and stretched within bounds") {
  std::vector<TimelineItem> items;
  items.push_back({0, 0, 10, 20, "pan", 0});
  items.push_back({1, 0, 50, 0, "cut", 0});
  items.push_back({2, 1, 15, 30, "say", 0});
  CHECK(timelineEnd(items) == 50);
  CHECK(nextItemId(items) == 3);

  CHECK(itemAt(items, 0, 10) == 0);
  CHECK(itemAt(items, 0, 29) == 0);
  CHECK(itemAt(items, 0, 30) == -1);   // an item ends before its last unit + 1
  CHECK(itemAt(items, 0, 52, 2) == 1); // a moment has slack either side
  CHECK(itemAt(items, 0, 53, 2) == -1);
  CHECK(itemAt(items, 1, 20) == 2);
  CHECK(itemAt(items, 2, 20) == -1);

  CHECK(moveItem(items, 0, 23, 1, 3, 5));
  CHECK(findItem(items, 0)->start == 25);
  CHECK(findItem(items, 0)->track == 1);
  CHECK_FALSE(moveItem(items, 0, 24, 1, 3, 5));  // lands where it is
  CHECK(moveItem(items, 0, -40, 9, 3, 5));       // kept on the axis and on a real track
  CHECK(findItem(items, 0)->start == 0);
  CHECK(findItem(items, 0)->track == 2);
  CHECK_FALSE(moveItem(items, 77, 0, 0, 3, 5));

  CHECK(resizeItem(items, 2, 42, 5));  // ends at 57 -> snaps to 55
  CHECK(findItem(items, 2)->length == 40);
  CHECK(resizeItem(items, 2, -100, 5));  // never shorter than a step
  CHECK(findItem(items, 2)->length == 5);
  CHECK_FALSE(resizeItem(items, 1, 30, 5));  // a moment stays a moment

  sortItems(items);
  CHECK(items[0].id == 0);
  CHECK(items[1].id == 2);
  CHECK(items[2].id == 1);
}

TEST_CASE("graph: columns follow the links, loops don't break the layout") {
  Graph g;
  for (int i = 0; i < 6; ++i) {
    GraphNode n;
    n.id = i * 10;  // ids are not positions
    n.title = "n" + std::to_string(i);
    if (i % 2) n.lines = {"a", "b"};
    g.nodes.push_back(n);
  }
  auto link = [&](int a, int b) { g.links.push_back({a * 10, b * 10, "", 0, false}); };
  link(0, 1);
  link(1, 2);
  link(0, 2);  // the long way decides: 2 comes after 1
  link(2, 3);
  link(3, 1);  // a loop back
  link(4, 4);  // to itself
  link(2, 99); // to nowhere
  const std::vector<int> layers = graphLayers(g);
  CHECK(layers == std::vector<int>{0, 1, 2, 3, 0, 0});
  const std::vector<bool> back = graphBackLinks(g);
  CHECK(back == std::vector<bool>{false, false, false, false, true, true, true});

  layoutGraph(g);
  // Every node has a size, columns go left to right, and no two boxes overlap.
  for (const GraphNode& n : g.nodes) {
    CHECK(n.w > 0.0f);
    CHECK(n.h > 0.0f);
  }
  CHECK(g.node(10)->x > g.node(0)->x);
  CHECK(g.node(20)->x > g.node(10)->x);
  CHECK(g.node(30)->x > g.node(20)->x);
  CHECK(g.node(10)->h > g.node(0)->h);  // lines make a box taller
  for (size_t i = 0; i < g.nodes.size(); ++i)
    for (size_t j = i + 1; j < g.nodes.size(); ++j) {
      const GraphNode& a = g.nodes[i];
      const GraphNode& b = g.nodes[j];
      const bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y;
      CHECK(apart);
    }
  const GraphBounds b = graphBounds(g);
  CHECK(b.x0 == 0.0f);
  CHECK(b.x1 > b.x0);
  CHECK(b.y1 > b.y0);

  // The same data lays out the same way (tools show it identically each time).
  Graph again = g;
  for (GraphNode& n : again.nodes) n.x = n.y = 0.0f;
  layoutGraph(again);
  for (size_t i = 0; i < g.nodes.size(); ++i) {
    CHECK(again.nodes[i].x == g.nodes[i].x);
    CHECK(again.nodes[i].y == g.nodes[i].y);
  }
}

TEST_CASE("graph: the layout untangles crossing links") {
  // Two columns of four, linked in reverse order: laid out as listed, every
  // pair of links crosses (6); ordered well, none does.
  Graph g;
  for (int i = 0; i < 9; ++i) {
    GraphNode n;
    n.id = i;
    g.nodes.push_back(n);
  }
  for (int i = 1; i <= 4; ++i) {
    g.links.push_back({0, i, "", 0, false});
    g.links.push_back({i, 9 - i, "", 0, false});
  }
  layoutGraph(g);
  CHECK(g.node(1)->layer == 1);
  CHECK(g.node(8)->layer == 2);
  CHECK(graphCrossings(g) == 0);
}

TEST_CASE("mission views: phases become a graph") {
  ScenarioScript s;
  auto phase = [&](const char* name, int from, size_t conditions) {
    MissionPhase p;
    p.name = name;
    p.from = from;
    p.enter.resize(conditions);
    s.phases.push_back(p);
  };
  phase("Approach", -1, 0);
  phase("Assault", -1, 2);    // after Approach, by itself
  phase("Hold", -1, 1);       // after Assault
  phase("Retreat", 1, 1);     // from Assault
  phase("Debrief", -1, 0);    // only by an action
  phase("Alarm", -2, 1);      // from anywhere
  Action go;
  go.type = ActionType::GotoPhase;
  go.value = 4;
  Trigger t;
  t.name = "Relieved";
  t.phase = 2;
  t.actions = {go};
  s.triggers.push_back(t);
  t.name = "Everyone out";
  t.phase = -1;  // in any phase
  s.triggers.push_back(t);
  MissionGoal goal;
  goal.text = "Hold the bridge";
  goal.phase = 2;
  go.value = 3;
  goal.onFail = {go};
  s.goals.push_back(goal);

  const Graph g = phaseGraph(s, 2);
  REQUIRE(g.nodes.size() == 6);
  CHECK(g.nodes[1].title == "1: Assault");
  CHECK(g.nodes[2].colour != 0);  // where the mission is
  CHECK(g.nodes[0].colour == 0);
  auto has = [&](int from, int to, bool dashed) {
    for (const GraphLink& l : g.links)
      if (l.from == from && l.to == to && l.dashed == dashed) return true;
    return false;
  };
  CHECK(has(0, 1, false));
  CHECK(has(1, 2, false));
  CHECK(has(1, 3, false));
  CHECK_FALSE(has(3, 4, false));  // Debrief has no conditions: nothing leads there by itself
  CHECK(has(2, 4, true));         // the trigger of phase 2
  CHECK(has(2, 3, true));         // the goal failing
  CHECK(g.links.size() == 5);     // nothing for "from anywhere": that is said on the boxes
  auto says = [&](int node, const char* what) {
    for (const std::string& l : g.nodes[static_cast<size_t>(node)].lines)
      if (l.find(what) != std::string::npos) return true;
    return false;
  };
  CHECK(says(4, "Everyone out"));
  CHECK(says(5, "from any phase"));
  CHECK(says(2, "1 goal"));
  CHECK(says(2, "1 trigger"));
  CHECK(says(0, "starts here"));

  // And it lays out: the phases in order left to right.
  Graph laid = g;
  layoutGraph(laid);
  CHECK(laid.nodes[1].x > laid.nodes[0].x);
  CHECK(laid.nodes[2].x > laid.nodes[1].x);
  CHECK(laid.nodes[4].x > laid.nodes[2].x);
}

TEST_CASE("mission views: a sequence goes to a timeline and back") {
  MissionSequence q;
  auto ev = [&](int at, const char* track, const char* kind, std::vector<std::pair<std::string, std::string>> params) {
    MissionSequence::Event e;
    e.at = at;
    e.track = track;
    e.kind = kind;
    e.params = std::move(params);
    q.events.push_back(e);
  };
  ev(0, "camera", "cut", {{"to", "bridge"}});
  ev(10, "camera", "pan", {{"to", "road"}, {"seconds", "6"}});
  ev(20, "dialogue", "radio", {{"who", "Leader"}, {"text", "All quiet."}, {"seconds", "2.5"}});
  ev(30, "weather", "rain", {});  // a track the registry doesn't know: kept, after the others

  std::vector<TimelineTrack> tracks;
  std::vector<TimelineItem> items;
  sequenceTimeline(q, tracks, items);
  REQUIRE(tracks.size() == script::sequenceTracks().size() + 1);
  CHECK(tracks.back().name == "weather");
  REQUIRE(items.size() == 4);
  CHECK(items[0].length == 0);
  CHECK(items[1].length == 60);
  CHECK(items[2].length == 25);
  CHECK(items[1].label == "pan: road");
  CHECK(items[2].label == "radio: Leader");
  CHECK(tracks[static_cast<size_t>(items[2].track)].name == "dialogue");

  // The radio call is dragged to the start and shortened; the pan moves to another track.
  CHECK(moveItem(items, 2, 0, items[2].track, static_cast<int>(tracks.size()), 5));
  CHECK(resizeItem(items, 2, 40, 5));
  CHECK(moveItem(items, 1, 15, items[1].track + 1, static_cast<int>(tracks.size()), 5));
  REQUIRE(applyTimeline(q, tracks, items));
  REQUIRE(q.events.size() == 4);
  CHECK(q.events[0].kind == "cut");    // equal times keep their order
  CHECK(q.events[1].kind == "radio");
  CHECK(q.events[1].at == 0);
  CHECK(q.events[1].params[2].second == "4");
  CHECK(q.events[2].kind == "pan");
  CHECK(q.events[2].at == 15);
  CHECK(q.events[2].track == tracks[static_cast<size_t>(items[1].track)].name);
  CHECK(q.events[3].track == "weather");

  // Items that don't match the sequence change nothing.
  items.pop_back();
  const MissionSequence before = q;
  CHECK_FALSE(applyTimeline(q, tracks, items));
  CHECK(q.events.size() == before.events.size());

  // Waves: one track each, an item per arrival.
  ScenarioScript s;
  MissionWave w;
  w.name = "Raiders";
  w.delay = 30;
  w.interval = 45;
  w.times = 3;
  s.waves.push_back(w);
  w.name = "Tanks";
  w.delay = 120;
  w.times = 1;
  w.units.push_back({});
  s.waves.push_back(w);
  waveTimeline(s, tracks, items);
  REQUIRE(tracks.size() == 2);
  REQUIRE(items.size() == 4);
  CHECK(items[0].start == 30);
  CHECK(items[2].start == 120);
  CHECK(items[2].label == "3/3");
  CHECK(items[3].start == 120);
  CHECK(tracks[0].locked);
}

TEST_CASE("strings: tables, fallback, patterns and plurals") {
  using namespace pioneer::loc;
  StringTable en;
  std::string error;
  REQUIRE(en.parse(R"({"language": "en", "name": "English", "strings": {
      "menu": {"play": "Play", "quit": "Quit", "deep": {"er": "Deeper"}},
      "hud.selected": "{n|# unit|# units} selected",
      "hud.hello": "Hello, {name}! {{braces}} and {unknown}",
      "odd": 7 }})", &error));
  CHECK(en.size() == 5);
  CHECK(error.find("1 entries") != std::string::npos);  // the number was left out, and said so
  CHECK(*en.find("menu.deep.er") == "Deeper");
  CHECK(en.find("odd") == nullptr);
  CHECK(en.rule == PluralRule::OneOther);
  CHECK_FALSE(StringTable().parse("[1,2]"));
  CHECK_FALSE(StringTable().parse(R"({"language": "en"})"));
  CHECK_FALSE(StringTable().parse("not json"));

  // A later file wins key by key (a mod over the game).
  REQUIRE(en.parse(R"({"strings": {"menu": {"play": "Fight"}, "mod.thing": "A thing"}})"));
  CHECK(*en.find("menu.play") == "Fight");
  CHECK(*en.find("menu.quit") == "Quit");
  CHECK(en.language == "en");
  CHECK(en.size() == 6);

  StringTable ru;
  REQUIRE(ru.parse(R"({"language": "ru", "name": "Русский", "strings": {
      "menu.play": "Играть", "hud.selected": "Выбрано: {n|# отряд|# отряда|# отрядов}", "old.key": "?"}})"));
  CHECK(ru.rule == PluralRule::Slavic);

  Strings s;
  s.use(ru, en);
  CHECK(s.language() == "ru");
  CHECK(s.get("menu.play") == "Играть");
  CHECK(s.get("menu.quit") == "Quit");          // not translated: English
  CHECK(s.get("no.such.key") == "no.such.key");  // nowhere: the key, and remembered
  CHECK(s.has("menu.quit"));
  CHECK_FALSE(s.has("no.such.key"));
  CHECK(s.missing() == std::vector<std::string>{"no.such.key"});
  CHECK(s.orphans() == std::vector<std::string>{"old.key"});
  const std::vector<std::string> todo = s.untranslated();
  CHECK(todo.size() == 4);
  CHECK(s.format("hud.selected", {{"n", 1}}) == "Выбрано: 1 отряд");
  CHECK(s.format("hud.selected", {{"n", 3}}) == "Выбрано: 3 отряда");
  CHECK(s.format("hud.selected", {{"n", 11}}) == "Выбрано: 11 отрядов");
  CHECK(s.format("hud.selected", {{"n", 21}}) == "Выбрано: 21 отряд");
  CHECK(s.format("hud.selected", {{"n", 25}}) == "Выбрано: 25 отрядов");
  // A string that fell back to English is filled in by English's rule.
  CHECK(s.format("hud.hello", {{"name", "Ivan"}}) == "Hello, Ivan! {braces} and {unknown}");

  Strings e;
  e.use(en, en);
  CHECK(e.format("hud.selected", {{"n", 1}}) == "1 unit selected");
  CHECK(e.format("hud.selected", {{"n", 0}}) == "0 units selected");
  CHECK(e.resolve("@menu.quit") == "Quit");
  CHECK(e.resolve("@not.there") == "@not.there");
  CHECK(e.resolve("Plain words") == "Plain words");
  CHECK(e.resolve("@") == "@");
  const uint32_t rev = e.revision();
  e.setPseudo(true);
  CHECK(e.revision() != rev);
  const std::string p = e.get("menu.quit");
  CHECK(p.front() == '[');
  CHECK(p.back() == ']');
  CHECK(p.size() > 6);
  CHECK(p.find("Quit") == std::string::npos);
  CHECK(e.format("hud.selected", {{"n", 2}}).find("2") != std::string::npos);  // patterns survive it

  // The rules by language, and the forms they pick.
  CHECK(pluralRuleFor("ms") == PluralRule::None);
  CHECK(pluralRuleFor("ja-JP") == PluralRule::None);
  CHECK(pluralRuleFor("de") == PluralRule::OneOther);
  CHECK(pluralRuleFor("fr") == PluralRule::French);
  CHECK(pluralRuleFor("pt-BR") == PluralRule::French);
  CHECK(pluralRuleFor("pt") == PluralRule::OneOther);
  CHECK(pluralRuleFor("uk") == PluralRule::Slavic);
  CHECK(pluralRuleFor("pl") == PluralRule::Polish);
  CHECK(pluralRuleFor("cs") == PluralRule::Czech);
  CHECK(pluralRuleFor("xx") == PluralRule::OneOther);
  CHECK(pluralForm(PluralRule::French, 0) == 0);
  CHECK(pluralForm(PluralRule::French, 2) == 1);
  CHECK(pluralForm(PluralRule::Polish, 21) == 2);
  CHECK(pluralForm(PluralRule::Polish, 22) == 1);
  CHECK(pluralForm(PluralRule::Czech, 22) == 2);
  CHECK(pluralForm(PluralRule::None, 5) == 0);
  CHECK(format("{n|# tank}", {{"n", 5}}, PluralRule::Slavic) == "5 tank");  // fewer forms than the rule: the last stands in
  CHECK(format("{n|satu|banyak}", {{"n", 9}}, PluralRule::None) == "satu");
  CHECK(format("open { brace", {}) == "open { brace");
  CHECK(format("{a}{b}", {{"a", "x"}, {"b", 2}}) == "x2");
}

TEST_CASE("strings: the game's table loads through the file system, mods add to it") {
  using namespace pioneer::loc;
  vfs::Vfs fs;
  std::string error;
  const int base = fs.mountDir(LEGION_CONTENT_DIR, "base", 0);
  REQUIRE(base >= 0);
  Strings s;
  REQUIRE(s.load(fs, "en", &error));
  CHECK(s.language() == "en");
  CHECK(s.size() >= 20);
  CHECK(s.get("settings.display.interface_size") == "Interface size");
  CHECK(s.untranslated().empty());

  const std::vector<Strings::Language> langs = Strings::available(fs);
  REQUIRE(!langs.empty());
  CHECK(langs[0].id == "en");
  CHECK(langs[0].name == "English");

  // A mod brings a language and a few words of its own in English.
  const int mod = fs.mountMemory("mod", 10);
  REQUIRE(mod >= 0);
  REQUIRE(fs.addMemoryFile(mod, "lang/ms.json", std::string_view(R"({"language": "ms", "name": "Bahasa Melayu", "strings": {
      "settings": {"display": {"interface_size": "Saiz antara muka"}}, "mod.hello": "{n|# kereta kebal} sedia"}})")));
  REQUIRE(fs.addMemoryFile(mod, "lang/en.json", std::string_view(R"({"strings": {"mod.hello": "{n|# tank|# tanks} ready"}})")));
  REQUIRE(s.load(fs, "ms", &error));
  CHECK(s.language() == "ms");
  CHECK(s.rule() == PluralRule::None);
  CHECK(s.get("settings.display.interface_size") == "Saiz antara muka");
  CHECK(s.get("settings.display.language") == "Language");  // the game's English underneath
  CHECK(s.format("mod.hello", {{"n", 4}}) == "4 kereta kebal sedia");
  CHECK(Strings::available(fs).size() == langs.size() + 1);
  REQUIRE(s.load(fs, "en", &error));
  CHECK(s.format("mod.hello", {{"n", 4}}) == "4 tanks ready");  // the mod's words joined the game's
  CHECK(s.get("settings.display.interface_size") == "Interface size");

  // A language there is no file for: English, and the caller is told.
  error.clear();
  CHECK_FALSE(s.load(fs, "tlh", &error));
  CHECK(!error.empty());
  CHECK(s.get("settings.display.interface_size") == "Interface size");
}

TEST_CASE("interface fonts are in the content and every named icon is listed once") {
  for (const char* name : {"ui-regular.ttf", "ui-bold.ttf", "mono.ttf", "icons.ttf"}) {
    const std::string bytes = slurp(std::string(LEGION_CONTENT_DIR) + "fonts/" + name);
    CAPTURE(name);
    REQUIRE(bytes.size() > 20000);
    // TrueType (00 01 00 00) or OpenType ("OTTO") outlines.
    const bool ttf = bytes.compare(0, 4, std::string("\x00\x01\x00\x00", 4)) == 0 || bytes.compare(0, 4, "OTTO") == 0;
    CHECK(ttf);
  }
  CHECK(slurp(std::string(LEGION_CONTENT_DIR) + "fonts/LICENSES.md").size() > 100);

  // The list in icon_list.cpp against the header's defines.
  std::string header = slurp(std::string(LEGION_CONTENT_DIR) + "../engine/include/pioneer/ui/icons.hpp");
  size_t defines = 0;
  for (size_t at = header.find("#define ICON_"); at != std::string::npos; at = header.find("#define ICON_", at + 1)) ++defines;
  size_t count = 0;
  const IconName* icons = iconList(count);
  CHECK(count == defines);
  std::set<std::string> names, glyphs;
  for (size_t i = 0; i < count; ++i) {
    names.insert(icons[i].name);
    glyphs.insert(icons[i].glyph);
    // Three bytes of UTF-8 in the private use area, where the icon font keeps its glyphs.
    const std::string g = icons[i].glyph;
    REQUIRE(g.size() == 3);
    CHECK(static_cast<unsigned char>(g[0]) == 0xEE);
  }
  CHECK(names.size() == count);
  CHECK(glyphs.size() == count);  // no two names for one picture
  CHECK(names.count("ICON_SAVE") == 1);
}

// Pioneer Engine core (9.13 part 1): the job system, the profiler, memory
// tracking, logging channels and the developer console.
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "pioneer/core/assert.hpp"
#include "pioneer/core/console.hpp"
#include "pioneer/core/jobs.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/core/memory.hpp"
#include "pioneer/core/profiler.hpp"
#include "pioneer/core/rng.hpp"
#include "pioneer/core/version.hpp"
#include "pioneer/map/map.hpp"
#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

// --- Jobs -----------------------------------------------------------------------

TEST_CASE("parallelFor visits every item once, in chunks that don't depend on the thread count") {
  for (int threads : {1, 2, 4, 8}) {
    jobs::JobSystem js;
    js.start(threads);
    CHECK(js.threadCount() == threads);
    const size_t count = 10007, grain = 64;
    std::vector<std::atomic<int>> seen(count);
    std::vector<std::pair<size_t, size_t>> borders(jobs::chunkCount(count, grain));
    js.parallelFor(count, grain, [&](size_t b, size_t e, size_t chunk, int worker) {
      CHECK(worker >= 0);
      CHECK(worker < threads);
      borders[chunk] = {b, e};
      for (size_t i = b; i < e; ++i) seen[i].fetch_add(1);
    });
    for (size_t i = 0; i < count; ++i) REQUIRE(seen[i].load() == 1);
    for (size_t c = 0; c < borders.size(); ++c) {
      CHECK(borders[c].first == c * grain);
      CHECK(borders[c].second == std::min(count, c * grain + grain));
    }
  }
}

TEST_CASE("results merged in chunk order are the same on one thread and on many") {
  auto run = [](int threads) {
    jobs::JobSystem js;
    js.start(threads);
    // Each chunk hashes its own items; the chunks are folded front to back.
    const std::vector<uint64_t> parts =
        jobs::parallelChunks<uint64_t>(js, 50000, 97, [](size_t b, size_t e, uint64_t& out) {
          uint64_t h = 1469598103934665603ull;
          for (size_t i = b; i < e; ++i) h = (h ^ hash32(static_cast<uint32_t>(i))) * 1099511628211ull;
          out = h;
        });
    uint64_t all = 0;
    for (uint64_t p : parts) all = all * 31 + p;
    const std::vector<int> squares = jobs::parallelMap<int>(js, 1000, 10, [](size_t i) { return static_cast<int>(i * i); });
    for (size_t i = 0; i < squares.size(); ++i) REQUIRE(squares[i] == static_cast<int>(i * i));
    return all;
  };
  const uint64_t one = run(1);
  CHECK(run(2) == one);
  CHECK(run(5) == one);
  CHECK(run(8) == one);
}

TEST_CASE("a job starts only after the jobs it depends on") {
  for (int threads : {1, 4}) {
    jobs::JobSystem js;
    js.start(threads);
    std::atomic<int> step{0};
    std::vector<int> order(60, -1);
    std::vector<jobs::JobHandle> chain;
    for (int i = 0; i < 60; ++i) {
      std::vector<jobs::JobHandle> after;
      if (!chain.empty()) after.push_back(chain.back());
      chain.push_back(js.submit([&order, &step, i](int) { order[static_cast<size_t>(i)] = step.fetch_add(1); }, after));
    }
    js.wait(chain);
    for (int i = 0; i < 60; ++i) CHECK(order[static_cast<size_t>(i)] == i);
    // A job with two parents waits for both.
    std::atomic<int> parents{0};
    int seenByChild = -1;
    jobs::JobHandle a = js.submit([&](int) { parents.fetch_add(1); });
    jobs::JobHandle b = js.submit([&](int) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      parents.fetch_add(1);
    });
    jobs::JobHandle c = js.submit([&](int) { seenByChild = parents.load(); }, {a, b});
    js.wait(c);
    CHECK(seenByChild == 2);
    CHECK(c.done());
  }
}

TEST_CASE("jobs may start jobs and wait for them without deadlock") {
  jobs::JobSystem js;
  js.start(4);
  std::atomic<int> total{0};
  js.parallelFor(16, 1, [&](size_t, size_t, size_t, int) {
    js.parallelFor(100, 7, [&](size_t b, size_t e, size_t, int) { total.fetch_add(static_cast<int>(e - b)); });
    jobs::JobHandle h = js.submit([&](int) { total.fetch_add(1); });
    js.wait(h);
  });
  CHECK(total.load() == 16 * 101);
  const jobs::JobStats s = js.stats();
  CHECK(s.threads == 4);
  CHECK(s.executed >= 16);
  CHECK(s.queued == 0);
  CHECK(std::accumulate(s.perThread.begin(), s.perThread.end(), uint64_t{0}) == s.executed);
}

TEST_CASE("what a job throws comes out of wait and parallelFor") {
  for (int threads : {1, 3}) {
    jobs::JobSystem js;
    js.start(threads);
    jobs::JobHandle h = js.submit([](int) { throw std::runtime_error("boom"); });
    CHECK_THROWS_AS(js.wait(h), std::runtime_error);
    std::atomic<int> ran{0};
    CHECK_THROWS_AS(js.parallelFor(40, 1,
                                   [&](size_t b, size_t, size_t, int) {
                                     ran.fetch_add(1);
                                     if (b == 17) throw std::logic_error("chunk 17");
                                   }),
                    std::logic_error);
    CHECK(ran.load() == 40);  // the other chunks still ran
  }
}

TEST_CASE("with one thread nothing is queued: jobs run at once on the caller") {
  jobs::JobSystem js;
  js.start(1);
  bool ran = false;
  jobs::JobHandle h = js.submit([&](int worker) {
    ran = true;
    CHECK(worker == 0);
  });
  CHECK(ran);  // before wait
  CHECK(h.done());
  CHECK(js.stats().inlined == 1);
  CHECK(js.currentWorker() == 0);
  // A system that was never started behaves the same way.
  jobs::JobSystem idle;
  int sum = 0;
  idle.parallelFor(10, 3, [&](size_t b, size_t e, size_t, int) { sum += static_cast<int>(e - b); });
  CHECK(sum == 10);
  CHECK(idle.threadCount() == 1);
}

TEST_CASE("a task graph runs tasks after their dependencies and refuses circles") {
  jobs::JobSystem js;
  js.start(4);
  jobs::TaskGraph g;
  std::mutex m;
  std::vector<std::string> ran;
  auto note = [&](const char* name) {
    return [&, name](int) {
      std::lock_guard<std::mutex> lk(m);
      ran.push_back(name);
    };
  };
  const int move = g.add("movement", note("movement"));
  const int audio = g.add("audio", note("audio"));
  const int vision = g.add("vision", note("vision"), {move});
  const int target = g.add("targeting", note("targeting"), {vision});
  g.add("render-prep", note("render-prep"), {move, target});
  std::string error;
  REQUIRE(g.run(js, &error));
  REQUIRE(ran.size() == 5);
  auto at = [&](const char* name) { return std::find(ran.begin(), ran.end(), name) - ran.begin(); };
  CHECK(at("movement") < at("vision"));
  CHECK(at("vision") < at("targeting"));
  CHECK(at("targeting") < at("render-prep"));
  CHECK(g.find("audio") == audio);
  std::vector<int> order;
  REQUIRE(g.order(order));
  CHECK(order.front() == move);

  jobs::TaskGraph circle;
  const int a = circle.add("a", [](int) {}, {1});
  circle.add("b", [](int) {}, {a});
  CHECK_FALSE(circle.run(js, &error));
  CHECK(error.find("circle") != std::string::npos);
  jobs::TaskGraph missing;
  missing.add("lonely", [](int) {}, {7});
  CHECK_FALSE(missing.run(js, &error));
  CHECK(error.find("lonely") != std::string::npos);
}

// --- The sim on the job system ------------------------------------------------------

namespace {
constexpr int T = 256;
struct Rules {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  Rules() {
    std::string err;
    const std::string dir = LEGION_RULES_DIR;
    REQUIRE(weapons.loadFile(dir + "weapons.json", err));
    REQUIRE(types.loadFile(dir + "units.json", err));
    REQUIRE(types.loadFile(dir + "buildings.json", err));
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
  }
  uint16_t t(const char* id) const {
    const int i = types.find(id);
    REQUIRE(i >= 0);
    return static_cast<uint16_t>(i);
  }
};

struct BattleResult {
  uint64_t hash = 0;
  size_t units = 0;
  std::vector<uint8_t> vision0, vision1;
};

// Two armies meet in the middle and fight it out for a minute and a half.
BattleResult battle(const Rules& R, int threads) {
  jobs::system().start(threads);
  Map map(96, 96);
  map.generateCountryside(1946);
  Simulation sim(map, R.types, R.weapons, 11);
  Command team;
  team.type = CommandType::SetTeam;
  team.player = 1;
  team.count = 1;
  sim.queue(team);
  static const char* kinds[2][4] = {{"ger_rifleman", "ger_mg_team", "ger_medium_tank", "ger_scout_car"},
                                    {"rus_rifleman", "rus_mg_team", "rus_medium_tank", "rus_scout_car"}};
  for (uint8_t p = 0; p < 2; ++p) {
    for (int k = 0; k < 4; ++k) {
      Command s;
      s.type = CommandType::Spawn;
      s.player = p;
      s.unitType = R.t(kinds[p][k]);
      s.count = k == 0 ? 40 : 6;
      s.target = {(p == 0 ? 30 : 66) * T, (40 + k * 5) * T};
      sim.queue(s);
    }
  }
  sim.tick();
  for (uint8_t p = 0; p < 2; ++p) {
    Command go;
    go.type = CommandType::AttackMove;
    go.player = p;
    go.target = {48 * T, 48 * T};
    sim.registry().each<CUnit>([&](uint32_t i, CUnit& u) {
      if (u.owner == p) go.units.push_back(sim.registry().handle(i));
    });
    sim.queue(go);
  }
  for (int i = 0; i < 15 * 90; ++i) sim.tick();
  BattleResult r;
  r.hash = sim.stateHash();
  r.units = sim.unitCount();
  r.vision0 = sim.vision().grid(0);
  r.vision1 = sim.vision().grid(1);
  jobs::system().stop();
  return r;
}
}  // namespace

TEST_CASE("a battle ends in exactly the same state on one thread and on four") {
  Rules R;
  const BattleResult one = battle(R, 1);
  const BattleResult four = battle(R, 4);
  const BattleResult three = battle(R, 3);
  CHECK(one.units > 0);
  CHECK(one.units < 116);  // they did fight
  MESSAGE("battle: ", one.units, " of 116 units left, state ", one.hash);
  CHECK(four.hash == one.hash);
  CHECK(three.hash == one.hash);
  CHECK(four.units == one.units);
  CHECK(four.vision0 == one.vision0);
  CHECK(four.vision1 == one.vision1);
  CHECK(std::count(one.vision0.begin(), one.vision0.end(), uint8_t{3}) > 0);  // somebody sees something
}

// --- Profiler ---------------------------------------------------------------------

namespace {
void spin(int ms) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
  while (std::chrono::steady_clock::now() < end) {
  }
}
const prof::ScopeStat* scope(const prof::FrameStats& f, const std::string& name) {
  for (const prof::ScopeStat& s : f.scopes)
    if (name == s.name) return &s;
  return nullptr;
}
}  // namespace

TEST_CASE("the profiler adds up scopes per frame, with own time and nesting") {
  prof::reset();
  prof::setEnabled(false);
  {
    PNR_PROFILE("never recorded");
  }
  prof::beginFrame();
  CHECK(prof::lastFrame().scopes.empty());

  prof::setEnabled(true);
  prof::beginFrame();  // opens the first frame
  for (int i = 0; i < 3; ++i) {
    PNR_PROFILE("outer");
    spin(2);
    {
      PNR_PROFILE("inner");
      spin(3);
    }
  }
  PNR_COUNTER("units", 1234);
  prof::beginFrame();  // closes it
  const prof::FrameStats& f = prof::lastFrame();
  const prof::ScopeStat* outer = scope(f, "outer");
  const prof::ScopeStat* inner = scope(f, "inner");
  REQUIRE(outer);
  REQUIRE(inner);
  CHECK(scope(f, "never recorded") == nullptr);
  CHECK(outer->calls == 3);
  CHECK(inner->calls == 3);
  CHECK(outer->depth == 0);
  CHECK(inner->depth == 1);
  CHECK(outer->ms >= 14.0);
  CHECK(inner->ms >= 8.5);
  CHECK(outer->ms > inner->ms);
  CHECK(outer->selfMs == doctest::Approx(outer->ms - inner->ms).epsilon(0.01));
  CHECK(inner->selfMs == doctest::Approx(inner->ms));
  CHECK(outer->maxMs >= 4.5);
  CHECK(f.ms >= outer->ms);
  CHECK(f.scopes.front().name == std::string("outer"));  // longest first
  REQUIRE(f.counters.size() == 1);
  CHECK(f.counters[0].value == 1234);
  CHECK(prof::scopeMs("inner") == doctest::Approx(inner->ms));
  CHECK(f.events.size() == 6);
  prof::setEnabled(false);
  prof::reset();
}

TEST_CASE("a capture records worker threads too and is valid Chrome trace JSON") {
  prof::reset();
  jobs::JobSystem js;
  js.start(3);
  CHECK_FALSE(prof::enabled());
  prof::startCapture(2);
  CHECK(prof::capturing());
  prof::beginFrame();
  for (int frame = 0; frame < 2; ++frame) {
    {
      PNR_PROFILE("frame \"work\"");
      js.parallelFor(6, 1, [](size_t, size_t, size_t, int) {
        PNR_PROFILE("chunk");
        spin(2);
      });
    }
    prof::beginFrame();
  }
  CHECK_FALSE(prof::capturing());
  REQUIRE(prof::captureReady());
  CHECK_FALSE(prof::enabled());  // back to how it was
  const nlohmann::json j = nlohmann::json::parse(prof::traceJson(), nullptr, false);
  REQUIRE(j.is_object());
  REQUIRE(j["traceEvents"].is_array());
  int chunks = 0, frames = 0, threadNames = 0;
  std::vector<int> tids;
  for (const auto& e : j["traceEvents"]) {
    if (e["ph"] == "M" && e["name"] == "thread_name") ++threadNames;
    if (e["ph"] != "X") continue;
    CHECK(e["dur"].get<double>() >= 0);
    if (e["name"] == "chunk") {
      ++chunks;
      tids.push_back(e["tid"].get<int>());
    }
    if (e["name"] == "frame \"work\"") ++frames;  // quotes survive the escaping
  }
  CHECK(chunks == 12);
  CHECK(frames == 2);
  CHECK(threadNames >= 3);
  std::sort(tids.begin(), tids.end());
  CHECK(std::unique(tids.begin(), tids.end()) - tids.begin() >= 2);  // more than one thread did chunks
  prof::discardCapture();
  CHECK_FALSE(prof::captureReady());
  prof::reset();
}

// --- Memory -----------------------------------------------------------------------

TEST_CASE("memory is counted per system: tagged allocations, reporters and the process") {
  const mem::TagStats before = mem::stats(mem::Tag::Tools);
  {
    mem::Vector<uint64_t, mem::Tag::Tools> v;
    v.reserve(1000);
    const mem::TagStats during = mem::stats(mem::Tag::Tools);
    CHECK(during.bytes - before.bytes == 8000);
    CHECK(during.peak >= during.bytes);
    CHECK(during.allocs == before.allocs + 1);
  }
  const mem::TagStats after = mem::stats(mem::Tag::Tools);
  CHECK(after.bytes == before.bytes);
  CHECK(after.frees == before.frees + 1);
  CHECK(after.peak >= before.bytes + 8000);

  std::vector<int> owned(5000);
  {
    mem::ScopedReporter rep(mem::Tag::Path, "test nodes", [&] { return mem::bytesOf(owned); });
    bool found = false;
    for (const mem::ReportLine& l : mem::report())
      if (l.name == "test nodes") {
        found = true;
        CHECK(l.tag == mem::Tag::Path);
        CHECK(l.bytes == owned.capacity() * sizeof(int));
      }
    CHECK(found);
    CHECK(mem::totals()[static_cast<size_t>(mem::Tag::Path)] >= owned.capacity() * sizeof(int));
  }
  for (const mem::ReportLine& l : mem::report()) CHECK(l.name != "test nodes");

  CHECK(mem::formatBytes(88) == "88 B");
  CHECK(mem::formatBytes(2048) == "2 KB");
  CHECK(mem::formatBytes(3u << 20) == "3.0 MB");
  CHECK(std::string(mem::tagName(mem::Tag::Path)) == "Pathfinding");
#if defined(__linux__) || defined(_WIN32)
  CHECK(mem::process().resident > 1000000);
  CHECK(mem::process().peakResident >= mem::process().resident / 2);
#endif
}

// --- Logging ------------------------------------------------------------------------

TEST_CASE("log channels have their own thresholds; lines reach the ring buffer and sinks") {
  log::setConsoleOutput(false);
  const uint64_t start = log::lastSeq();
  std::vector<std::string> sunk;
  const int sink = log::addSink([&](const log::Entry& e) { sunk.push_back(e.text); });

  log::setChannelLevel(log::Channel::Path, log::Level::Debug);
  log::setChannelLevel(log::Channel::Net, log::Level::Error);
  PNR_LOG(Path, Debug, "search %d", 7);
  PNR_LOG(Path, Trace, "too quiet");
  PNR_LOG(Net, Warn, "below this channel's threshold");
  PNR_LOG(Net, Error, "peer %s lost", "B");
  PNR_INFO("plain %d", 1);
  PNR_DEBUG("core debug is off by default");

  std::vector<log::Entry> lines;
  const uint64_t end = log::recent(lines, start);
  REQUIRE(lines.size() == 3);
  CHECK(end == start + 3);
  CHECK(lines[0].text == "search 7");
  CHECK(lines[0].channel == log::Channel::Path);
  CHECK(lines[0].level == log::Level::Debug);
  CHECK(lines[1].text == "peer B lost");
  CHECK(lines[1].level == log::Level::Error);
  CHECK(lines[2].channel == log::Channel::Core);
  CHECK(lines[0].seq + 2 == lines[2].seq);
  CHECK(sunk == std::vector<std::string>{"search 7", "peer B lost", "plain 1"});

  log::removeSink(sink);
  PNR_INFO("after the sink left");
  CHECK(sunk.size() == 3);

  log::Channel c;
  log::Level l;
  CHECK(log::channelFromName("Audio", c));
  CHECK(c == log::Channel::Audio);
  CHECK_FALSE(log::channelFromName("nonsense", c));
  CHECK(log::levelFromName("WARNING", l));
  CHECK(l == log::Level::Warn);
  for (int i = 0; i < log::kChannelCount; ++i) {
    REQUIRE(log::channelFromName(log::channelName(static_cast<log::Channel>(i)), c));
    CHECK(static_cast<int>(c) == i);
  }
  log::setMinLevel(log::Level::Info);
  CHECK(log::channelLevel(log::Channel::Net) == log::Level::Info);
  log::setConsoleOutput(true);
}

// --- Console ------------------------------------------------------------------------

namespace {
struct TestConsole {
  Console con;
  std::vector<std::string> out, errors;
  TestConsole() {
    con.setLogOutput(false);
    con.addOutput([this](std::string_view line, bool error) { (error ? errors : out).push_back(std::string(line)); });
  }
  std::string last() const { return out.empty() ? std::string() : out.back(); }
};
}  // namespace

TEST_CASE("console text is split into commands and words like a small shell") {
  using V = std::vector<std::string>;
  CHECK(Console::tokenize("  move  12 -3.5\t") == V{"move", "12", "-3.5"});
  CHECK(Console::tokenize("say \"two words\" \"a \\\"quoted\\\" one\" \"\"") ==
        V{"say", "two words", "a \"quoted\" one", ""});
  CHECK(Console::splitCommands("a 1; b 2\nc \"x; y\" // comment ; not a command\n# whole line\n  \n d") ==
        V{"a 1", "b 2", "c \"x; y\"", "d"});
  CHECK(Console::splitCommands("open http://example").size() == 1);  // '//' inside a word is not a comment
  CHECK(Console::quote("plain") == "plain");
  CHECK(Console::quote("two words") == "\"two words\"");
  CHECK(Console::tokenize(Console::quote("a \"b\" \\ c; d")) == V{"a \"b\" \\ c; d"});
}

TEST_CASE("console variables are typed, clamped, guarded by their flags and tell listeners") {
  TestConsole t;
  Console& con = t.con;
  CVar& speed = con.addFloat("sim.speed", 1.0, 0.25, 16.0, "Game speed.");
  CVar& shadows = con.addBool("r.shadows", true, "Sun shadows.", kCVarArchive);
  CVar& units = con.addInt("bench.units", 1500, 0, 5000, "Units in the bench.");
  CVar& name = con.addString("net.name", "Player", "Your name in multiplayer.", kCVarArchive);
  CVar& fps = con.addInt("r.fps", 60, "Frames per second (measured).", kCVarReadOnly);
  CVar& god = con.addBool("god", false, "Your units take no damage.", kCVarCheat);
  CHECK(&con.addFloat("SIM.Speed", 3.0, "again") == &speed);  // same variable, value kept
  CHECK(speed.f() == 1.0f);

  int changes = 0;
  speed.onChange([&](const CVar&) { ++changes; });
  CHECK(con.execute("sim.speed 4") == 1);
  CHECK(speed.f() == 4.0f);
  CHECK(t.last() == "sim.speed = 4");
  con.execute("sim.speed 400");
  CHECK(speed.f() == 16.0f);  // clamped
  con.execute("sim.speed 16");
  CHECK(changes == 2);        // no change, no callback
  con.execute("sim.speed fast");
  CHECK(speed.f() == 16.0f);
  REQUIRE(t.errors.size() == 1);
  CHECK(t.errors[0].find("number") != std::string::npos);

  con.execute("r.shadows off; bench.units 2000.4; net.name Max Sonnenberg");
  CHECK_FALSE(shadows.b());
  CHECK(units.i() == 2000);
  CHECK(name.s() == "Max Sonnenberg");
  con.execute("r.shadows maybe");
  CHECK(t.errors.size() == 2);

  con.execute("r.fps 30");
  CHECK(fps.i() == 60);
  CHECK(t.errors.back().find("read-only") != std::string::npos);
  fps.setInt(144);  // the engine itself may set it
  CHECK(fps.i() == 144);

  con.execute("god 1");
  CHECK_FALSE(god.b());
  CHECK(t.errors.back().find("cheat") != std::string::npos);
  con.execute("cheats 1; god 1");
  CHECK(god.b());
  CHECK(con.cheats());

  t.out.clear();
  con.execute("sim.speed");
  REQUIRE(t.out.size() == 2);
  CHECK(t.out[0].find("sim.speed = 16") == 0);
  CHECK(t.out[0].find("default 1") != std::string::npos);
  CHECK(t.out[0].find("0.25 .. 16") != std::string::npos);
  CHECK(t.out[1].find("Game speed.") != std::string::npos);

  con.execute("toggle r.shadows");
  CHECK(shadows.b());
  con.execute("toggle sim.speed 1 2 4; toggle sim.speed 1 2 4");
  CHECK(speed.f() == 2.0f);
  con.execute("inc bench.units -500; inc bench.units");
  CHECK(units.i() == 1501);
  con.execute("reset bench.units");
  CHECK(units.i() == 1500);
  CHECK(units.isDefault());
  con.execute("reset all");
  CHECK(speed.f() == 1.0f);
  CHECK(name.s() == "Player");
}

TEST_CASE("console commands, aliases, key binds and typing help") {
  TestConsole t;
  Console& con = t.con;
  std::vector<std::string> spawned;
  con.addCommand("spawn", "spawn <type> [count] - creates units at the cursor.",
                 [&](const ConsoleArgs& a, Console& c) {
                   if (a.size() < 2) return c.error("spawn <type> [count]");
                   for (int i = 0; i < a.integer(2, 1); ++i) spawned.push_back(a[1]);
                 },
                 kCmdCheat,
                 [](const ConsoleArgs& a, const std::string&) {
                   return a.size() == 1 ? std::vector<std::string>{"ger_rifleman", "ger_mg", "rus_rifleman"}
                                        : std::vector<std::string>{};
                 });
  con.addInt("sim.speed", 1, 1, 16, "Game speed.");
  con.addInt("sim.seed", 7, "Map seed.");

  con.execute("spawn ger_mg 3");
  CHECK(spawned.empty());  // a cheat
  con.setCheats(true);
  con.execute("SPAWN ger_mg 3; spawn");
  CHECK(spawned == std::vector<std::string>{"ger_mg", "ger_mg", "ger_mg"});
  CHECK(t.errors.back() == "spawn <type> [count]");
  con.execute("nonsense 1 2");
  CHECK(t.errors.back().find("Unknown command 'nonsense'") != std::string::npos);

  // Aliases take arguments along, and can't loop for ever.
  con.execute("alias mg \"spawn ger_mg\"; mg 2");
  CHECK(spawned.size() == 5);
  con.execute("alias a b; alias b a; a");
  CHECK(t.errors.back().find("circles") != std::string::npos);
  con.execute("alias spawn \"echo no\"");
  CHECK(t.errors.back().find("already") != std::string::npos);

  // Keys.
  con.execute("bind F6 \"sim.speed 8\"");
  CHECK(con.binding("f6") == "sim.speed 8");
  CHECK(con.keyPressed("F6"));
  CHECK(con.find("sim.speed")->i() == 8);
  CHECK_FALSE(con.keyPressed("f7"));
  con.execute("unbind f6");
  CHECK_FALSE(con.keyPressed("f6"));

  // Completion: names first, then what the command or variable suggests.
  using V = std::vector<std::string>;
  CHECK(con.complete("sim.") == V{"sim.seed", "sim.speed"});
  CHECK(con.completeLine("sim.") == "sim.s");
  CHECK(con.completeLine("sim.sp") == "sim.speed ");
  CHECK(con.complete("spawn ger") == V{"ger_mg", "ger_rifleman"});
  CHECK(con.completeLine("spawn ger") == "spawn ger_");
  CHECK(con.completeLine("spawn rus") == "spawn rus_rifleman ");
  CHECK(con.complete("spawn ger_mg ").empty());
  CHECK(con.complete("help sp") == V{"spawn"});
  CHECK(con.completeLine("zzz") == "zzz");
  const V first = con.complete("");
  CHECK(std::find(first.begin(), first.end(), "help") != first.end());
  CHECK(std::find(first.begin(), first.end(), "mg") != first.end());  // aliases too

  t.out.clear();
  con.execute("find seed");
  REQUIRE(t.out.size() == 1);
  CHECK(t.out[0].find("sim.seed") != std::string::npos);
  con.execute("help spawn");
  CHECK(t.last().find("creates units") != std::string::npos);

  con.addHistory("one");
  con.addHistory("two");
  con.addHistory("two");
  CHECK(con.history() == V{"one", "two"});
}

TEST_CASE("the console config brings back saved variables, binds and aliases, even for late systems") {
  std::string saved;
  {
    TestConsole t;
    t.con.addInt("r.quality", 2, 0, 4, "Graphics preset.", kCVarArchive);
    t.con.addBool("r.bloom", true, "Bloom.", kCVarArchive);
    t.con.addString("net.name", "Player", "Name.", kCVarArchive);
    t.con.addInt("sim.speed", 1, "Not saved.");
    t.con.execute("r.quality 4; net.name \"Tom Kerr; Maj.\"; sim.speed 8; bind ctrl+p \"toggle r.bloom\"; "
                  "alias quiet \"snd.master 0\"; snd.late 0.25");
    CHECK(t.errors.size() == 1);  // snd.late doesn't exist in a typed line
    saved = t.con.writeConfig();
    CHECK(saved.find("r.quality 4") != std::string::npos);
    CHECK(saved.find("\nr.bloom ") == std::string::npos);    // still its default
    CHECK(saved.find("\nsim.speed ") == std::string::npos);  // not archived
  }
  TestConsole t;
  CVar& quality = t.con.addInt("r.quality", 2, 0, 4, "Graphics preset.", kCVarArchive);
  CVar& name = t.con.addString("net.name", "Player", "Name.", kCVarArchive);
  t.con.bind("f1", "help");  // a default the saved config replaces
  t.con.executeConfig(saved + "\nsnd.master 0.5\n");
  CHECK(t.errors.empty());
  CHECK(t.out.empty());  // loading a config is silent
  CHECK(quality.i() == 4);
  CHECK(name.s() == "Tom Kerr; Maj.");
  CHECK(t.con.binding("ctrl+p") == "toggle r.bloom");
  CHECK(t.con.binding("f1").empty());
  CHECK(t.con.aliases().at("quiet") == "snd.master 0");
  // The sound system registers its variable later and still gets the saved value...
  CVar& master = t.con.addFloat("snd.master", 0.85, 0.0, 1.0, "Master volume.", kCVarArchive);
  CHECK(master.f() == 0.5f);
  // ...and a value nobody has claimed yet is not lost by the next save.
  t.con.executeConfig("mod.future 3");
  CHECK(t.con.writeConfig().find("mod.future 3") != std::string::npos);
}

TEST_CASE("console: wait holds the rest back, other threads queue lines, exec reads files") {
  TestConsole t;
  Console& con = t.con;
  con.execute("echo now; wait 2; echo later; wait; echo last");
  CHECK(t.out == std::vector<std::string>{"now"});
  con.update();
  CHECK(t.out.size() == 1);
  con.update();
  CHECK(t.out == std::vector<std::string>{"now", "later"});
  con.update();
  CHECK(t.out == std::vector<std::string>{"now", "later", "last"});

  std::thread other([&] { con.queue("echo from a thread"); });
  other.join();
  CHECK(t.out.size() == 3);  // nothing runs until the main thread says so
  con.update();
  CHECK(t.last() == "from a thread");

  con.setFileReader([](const std::string& path, std::string& text) {
    if (path == "a.cfg") text = "echo a\nexec b.cfg\n";
    else if (path == "b.cfg") text = "echo b // inner\n";
    else if (path == "loop.cfg") text = "exec loop.cfg\n";
    else return false;
    return true;
  });
  con.execute("exec a.cfg");
  CHECK(t.out[t.out.size() - 2] == "a");
  CHECK(t.last() == "b");
  con.execute("exec missing.cfg");
  CHECK(t.errors.back().find("can't read") != std::string::npos);
  con.execute("exec loop.cfg");
  CHECK(t.errors.back().find("too many") != std::string::npos);
  con.execute("version");
  CHECK(t.last() == std::string(PIONEER_ENGINE_NAME) + " " + PIONEER_VERSION_STRING);
}

// --- Assertions -----------------------------------------------------------------------

namespace {
std::vector<std::string> g_failedChecks;
AssertAction collect(const char* expression, const char* file, int line, const char* message) {
  g_failedChecks.push_back(std::string(expression) + "|" + message + "|" + (file && line > 0 ? "placed" : "nowhere"));
  return AssertAction::Continue;
}
}  // namespace

TEST_CASE("a failed check is logged with its place, counted, and handed to the handler") {
  log::setConsoleOutput(false);
  const uint64_t seq = log::lastSeq();
  const uint64_t before = assertFailures();
  const AssertHandler old = setAssertHandler(collect);
  g_failedChecks.clear();
  const int owner = 9;
  PNR_ASSERT(owner < 8);
  PNR_ASSERT(owner == 9);  // holds: nothing happens
  PNR_ASSERT_MSG(owner < 8, "unit %d has owner %d", 41, owner);
  setAssertHandler(old);
  log::setConsoleOutput(true);
  CHECK(assertFailures() == before + 2);
  REQUIRE(g_failedChecks.size() == 2);
  CHECK(g_failedChecks[0] == "owner < 8||placed");
  CHECK(g_failedChecks[1] == "owner < 8|unit 41 has owner 9|placed");
  std::vector<log::Entry> lines;
  log::recent(lines, seq);
  REQUIRE(lines.size() == 2);
  CHECK(lines[0].level == log::Level::Error);
  CHECK(lines[0].text.find("Check failed: owner < 8") == 0);
  CHECK(lines[0].text.find("test_core.cpp") != std::string::npos);
  CHECK(lines[1].text.find("unit 41 has owner 9") != std::string::npos);
}

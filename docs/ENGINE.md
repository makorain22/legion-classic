# The Pioneer Engine (2.0)

Legion Classic runs on its own engine. Up to build 0.9.1 it was called Slave
Engine 1.0; Milestone 9.13 renames it **Pioneer** and rebuilds its
foundations so the Mission Maker, cutscenes, set pieces, big scenarios and
(later) multiplayer have something solid to stand on.

This document describes how the engine is put together and how to use each
system. It grows part by part as 9.13 is built; the table says where it
stands.

| Part | What | State |
| --- | --- | --- |
| 1 | **Core:** the rename, job system, profiler, memory tracking, logging channels, developer console | **done** |
| 2 | **Data:** virtual file system, asset registry, hot reload, pack format | next |
| 3 | **Simulation:** tagged-chunk saves, determinism checker, hierarchical pathfinding and flow fields, vision 2.0, suppression/morale fields | planned |
| 4 | **Scripting:** condition/action registry, mission data model, event bus, sandboxed Lua | planned |
| 5 | **UI framework:** docking, real fonts, UI scale, timeline and node widgets, property grids, key rebinding | planned |
| 6 | **Renderer:** backend layer, HDR post-processing, time of day, weather, graphics presets with auto-detect | planned |
| 7 | **Assets and audio:** glTF in and out, .vox, materials, skeletal animation, poses; sound banks, voice lines, reverb zones, occlusion | planned |
| 8 | **Networking groundwork:** lockstep, hosting, LAN discovery, dedicated server, reconnect | planned |
| 9 | **Tools:** `pioneer_tool`, asset browser, model viewer, sound bank editor | planned |

## Layout

```
engine/include/pioneer/   public headers, one folder per system
engine/src/core/          log, jobs, profiler, memory, console, content layers, fixed-point maths
engine/src/{ecs,sim,map}  the deterministic simulation
engine/src/{platform,render,assets,audio,ui}   window, OpenGL 3.3 renderer, art, mixer, developer windows
game/src/                 Legion Classic itself
editor/src/               the Live Scenario Editor and the rules editor
tests/                    doctest cases (pioneer_tests)
```

Two libraries:

- **`pioneer_core`** – everything with no graphics and no sound card: the
  core systems, the ECS, the simulation, maps. Tests, the content tools and
  the future dedicated server link only this.
- **`pioneer`** – window, renderer, art, audio and the ImGui developer
  windows, on top of `pioneer_core`.

Namespace `pioneer`; macros `PNR_*` (short, used everywhere) and
`PIONEER_*` (constants). The game's code lives in namespace `legion`.

### Rules that hold everywhere

1. **The simulation is deterministic.** Integer and fixed-point maths, a
   seeded generator, 15 ticks a second. The same commands give the same
   state hash on every machine and with any number of threads.
2. **Presentation never feeds back.** The renderer, the mixer, the profiler,
   the log, memory counters and the console read the simulation; nothing in
   the simulation reads them.
3. **One way to change the world:** a `Command` queued for the next tick.
4. **Old hardware counts.** OpenGL 3.3 core, every costly feature optional,
   a single-thread fallback for everything threaded.

---

## Part 1 – Core

### Job system (`pioneer/core/jobs.hpp`)

Worker threads with one queue each. A thread takes its own newest job first
and steals the oldest from another thread when it has nothing to do. The
thread that started the system is worker 0 and works too whenever it waits,
so waiting inside a job cannot deadlock.

```cpp
jobs::init(0);                       // one thread per core; 1 = no threads at all
jobs::JobSystem& js = jobs::system();

js.parallelFor(count, 64, [&](size_t begin, size_t end, size_t chunk, int worker) {
  for (size_t i = begin; i < end; ++i) out[i] = work(i);   // writes its own slots only
});

jobs::JobHandle a = js.submit([](int) { loadSounds(); });
jobs::JobHandle b = js.submit([](int) { buildIndex(); }, {a});   // starts after a
js.wait(b);                                                      // helps with other jobs meanwhile

jobs::TaskGraph frame;                                           // named tasks with dependencies
int move = frame.add("movement", moveFn);
frame.add("vision", visionFn, {move});
frame.run(js);
```

**How the simulation stays deterministic on many threads.** Jobs decide when
something is computed, never what:

- `parallelFor` cuts `[0, count)` into chunks whose borders depend on `count`
  and `grain` only, not on the number of threads.
- `parallelMap` and `parallelChunks` return results indexed by item or by
  chunk; the caller merges them front to back.
- A job writes only its own output. Shared state is read-only while jobs run.

The test *"a battle ends in exactly the same state on one thread and on
four"* plays a 90-second battle on 1, 3 and 4 threads and compares the state
hash and both sides' vision grids. Any new parallel step in the simulation
must keep that test green.

**What runs on it today:** each side's vision grid (one job per side), sprite
baking and art-pack loading. Pathfinding, meshing, asset loading and audio
decoding move onto it in parts 2, 3 and 7.

**One thread.** `--threads 1`, `sys.threads 1` or `PIONEER_THREADS=1` starts
no worker: every job runs at once on the caller. A system that was never
started behaves the same way, so tools and tests need no set-up.

A chunk or job that throws does not stop the others; the first exception
comes out of `wait` / `parallelFor` once all are done.

### Profiler (`pioneer/core/profiler.hpp`)

```cpp
void Simulation::stepMovement() {
  PNR_PROFILE("Sim: movement");        // a string literal; prof::intern() for names made at run time
  ...
}
PNR_COUNTER("Units", unitCount);
```

Every thread records its scopes; `prof::beginFrame()` (called by `App::run`
each frame, and by `--bench` each tick) closes the frame and adds up, per
scope: calls, total time, own time (minus nested scopes) and the longest
call. 240 frames of history are kept. Off by default; an inactive scope costs
one branch.

- **In game:** F8 opens the profiler window – frame-time history, the scope
  table, a per-thread timeline of the last frame, memory by system and the
  job system's counters.
- **Captures:** `prof.capture 300` (or the window's button, or
  `--trace out.json`) records every scope of the next frames as a Chrome
  trace. Open it in `chrome://tracing`, <https://ui.perfetto.dev> or
  <https://speedscope.app>.
- **Headless:** `--bench N` ends with *where the ticks went*: mean and worst
  time per simulation step, the share of each, and how many ticks went over
  the 66.7 ms budget.

### Memory (`pioneer/core/memory.hpp`)

Three sources, shown together by `mem.report` and the profiler's Memory tab:

- **Tagged allocations:** `mem::Vector<Node, mem::Tag::Path>` (a
  `std::vector` with a counting allocator) or `mem::add(tag, bytes)` for
  memory obtained some other way.
- **Reporters:** a system that keeps its data in ordinary containers
  registers a function returning its size; it is asked only when a report is
  made. `mem::ScopedReporter` as a member removes it automatically.
- **The process:** resident and peak memory from the operating system.

Tags: General, Simulation, ECS, Pathfinding, Vision, Map, Script, AI, Render,
Meshes, Textures, Audio, Assets, Content, UI, Jobs, Network, Tools.

### Logging (`pioneer/core/log.hpp`)

```cpp
PNR_INFO("loaded %d maps", n);                 // channel core
PNR_LOG(Net, Warn, "peer %d timed out", id);   // any channel, any level
PNR_DEBUG_CH(Path, "search took %d expansions", n);
```

Levels: trace, debug, info, warn, error. Channels: core, sim, ai, path,
script, map, content, assets, render, audio, ui, input, net, game, editor,
tools, console, jobs. Each channel has its own threshold (`log.level path
debug`), so one system can be opened up without drowning in the rest.

Lines go to the terminal, to `legion.log`, to a ring buffer of the newest
2000 lines (what the console window shows) and to any sink added with
`log::addSink`. Thread-safe; a line below its channel's threshold costs one
comparison.

### Assertions (`pioneer/core/assert.hpp`)

`PNR_ASSERT(cond)`, `PNR_ASSERT_MSG(cond, "unit %u has owner %d", id, owner)`,
`PNR_DEBUG_ASSERT(cond)` (compiled out of release builds, for hot paths),
`PNR_FATAL(...)` and `PNR_UNREACHABLE()`. A failed check is logged with its
file and line and counted. A debug build then stops, so the crash reporter
writes `legion-crash.txt` with the call stack; a release build carries on.
`setAssertHandler` lets a program decide otherwise.

### Developer console (`pioneer/core/console.hpp`)

The console has no window of its own: it is a registry of **commands** and
**variables** plus a small command language. The game draws it (**`** or
**F9**); tools, tests and the future server use the same console headless.

```
con.height 0.6              set a variable          con.height    show it, its default and range
bind f6 "profiler"          run a command on a key  toggle r.showfps
alias quiet "log.level all warn"                    exec my.cfg
echo one; echo two          ';' separates commands  // and # start a comment
wait 30; screenshot         'wait N' holds the rest back for N frames
```

Tab completes names, unit ids and values; Up/Down recall earlier lines;
`help`, `find <text>`, `cmdlist`, `cvarlist` explain what there is.

Adding to it from code:

```cpp
Console& con = Console::global();
CVar& bloom = con.addBool("r.bloom", true, "Bloom on bright things.", kCVarArchive);
bloom.onChange([](const CVar& v) { post.setBloom(v.b()); });

con.addCommand("spawn", "spawn <unit id> [count] - creates units under the mouse.",
               [](const ConsoleArgs& a, Console& c) { ... },
               kCmdCheat,                       // needs 'cheats 1'
               completer);                      // optional: suggestions for Tab
```

Variable flags: `kCVarArchive` (saved), `kCVarCheat`, `kCVarReadOnly`,
`kCVarDev` (hidden from listings), `kCVarRestart`.

**Config files** in the user folder: `console.cfg` is written when the game
closes (saved variables that differ from their defaults, key binds, aliases);
`autoexec.cfg`, if you make one, runs after it and is never overwritten. A
saved value for a variable that is registered later (by a system that starts
late, or a mod) is kept and applied when the variable appears.

**Built in everywhere** (`registerCoreCommands`): `help`, `find`, `cmdlist`,
`cvarlist`, `echo`, `exec`, `set`, `toggle`, `inc`, `reset`, `alias`,
`unalias`, `bind`, `unbind`, `unbindall`, `bindlist`, `history`, `version`,
`cheats`, `log.level`, `log.channels`, `prof.measure`, `prof.capture`,
`prof.report`, `mem.report`, `jobs.info`, `jobs.threads`.

**Added by the game** (`game/src/dev_tools.cpp`): `quit`, `clear`,
`console`, `profiler`, `screenshot`, `speed`, `pause`, `hash`, `status`, `map`, `goto`,
`restart`, `editor`, and the cheats `reveal`, `credits`, `spawn`, `kill`,
`side`; variables `r.showfps`, `con.height`, `sys.threads`.

From other threads use `Console::queue()`; the main thread runs queued lines
in `update()`.

### Command line (new in 2.0)

| Option | What it does |
| --- | --- |
| `--threads N` | Threads for the job system: 0 = one per core (default), 1 = none |
| `--exec "cmd; cmd"` | Console commands run once the game is up |
| `--profile` | Measure from the first frame and open the profiler |
| `--trace out.json` | Capture a Chrome trace of the first frames (with `--bench`: of the ticks in the middle of the battle) |
| `--trace-frames N` | How many frames or ticks the trace covers (default 300) |

### Tests

`tests/test_core.cpp` (18 cases): chunk borders and coverage on 1–8 threads,
ordered merging, dependencies, nested jobs, exceptions, the single-thread
path, task graphs and cycles; the battle determinism check; profiler sums,
own time, worker threads and trace JSON; tagged allocations and reporters;
log channels, ring buffer and sinks; console parsing, variables and their
flags, commands, aliases, binds, completion, config round trip, `wait`,
queued lines and `exec`; failed checks.

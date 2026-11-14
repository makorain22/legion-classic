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
| 2 | **Data:** virtual file system, asset registry, hot reload, pack format | **done** |
| 3 | **Simulation:** tagged-chunk saves, determinism checker, hierarchical pathfinding and flow fields, vision 2.0, suppression/morale fields | next |
| 4 | **Scripting:** condition/action registry, mission data model, event bus, sandboxed Lua | planned |
| 5 | **UI framework:** docking, real fonts, UI scale, timeline and node widgets, property grids, key rebinding | planned |
| 6 | **Renderer:** backend layer, HDR post-processing, time of day, weather, graphics presets with auto-detect | planned |
| 7 | **Assets and audio:** glTF in and out, .vox, materials, skeletal animation, poses; sound banks, voice lines, reverb zones, occlusion | planned |
| 8 | **Networking groundwork:** lockstep, hosting, LAN discovery, dedicated server, reconnect | planned |
| 9 | **Tools:** `pioneer_tool`, asset browser, model viewer, sound bank editor | planned |

## Layout

```
engine/include/pioneer/   public headers, one folder per system
engine/src/core/          log, jobs, profiler, memory, console, virtual file system, packs, assets,
                          hot reload, content layers, fixed-point maths
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

---

## Part 2 – Data

Everything the game reads goes through four pieces that build on each other:
the **virtual file system** says where a file is, **packs** put a whole mod
into one file, the **asset registry** knows what every file is and what uses
it, and **hot reload** brings changes on disk into the running game.

### Virtual file system (`pioneer/core/vfs.hpp`)

One tree of virtual paths (`rules/units.json`, `audio/sfx/rifle.ogg`) over
any number of **mounts**:

```cpp
auto fs = std::make_shared<vfs::Vfs>();
fs->mountDir("content/", "base", 0);                       // a folder
fs->mountZip("mods/veterans.pnpack", "veterans", 1, "", err);   // a pack, read straight out of the archive
int gen = fs->mountMemory("generated", 2);                 // files held in memory (tools, tests)
fs->addMemoryFile(gen, "rules/extra.json", text);

fs->read("rules/units.json", bytes);        // the copy of the highest mount that has it
fs->layers("rules/units.json");             // every copy, lowest first (for data that is merged)
fs->list("campaigns", true, ".lmap");       // the merged tree
fs->listDirs("campaigns");
```

- Higher priority wins; of equal priorities, the mount made last. A mount can
  sit at a point in the tree (`mountZip(file, name, prio, "mods/veterans/")`).
- Paths use `/`, are relative and cannot leave the tree: `../x` and `C:/x` are
  refused everywhere, also inside archives.
- Archives fall back to ignoring case, so a pack made on Windows behaves the
  same on every system.
- **Writing** goes to one folder mount chosen with `setWriteMount()` (the
  player's folder), through a temporary file that is renamed into place, so no
  reader ever sees half a file.
- **Old code that wants a real path** asks `diskPath(path)`: the file itself
  for a folder mount, a copy unpacked into the cache folder
  (`<user folder>/cache/<mount>-<fingerprint>/`) for an archive. The cache is
  keyed by the archive's contents: nothing is unpacked twice, and copies of
  older versions of the same pack are removed.
- An archive under 96 MB is held in memory, so its file is not kept open and
  can be replaced while the game runs (Windows will not overwrite an open
  file).
- Thread-safe for reading from any thread.

`ContentLayers` (`pioneer/core/content.hpp`), the stack "base, then each
enabled mod" the game has always had, is now a thin layer over a `Vfs`:
`addRoot(dir)`, `addPack(file)`, `addMod(mod)`; `fileSystem()` gives the tree.
`all("audio/")` still returns real folders, base first.

### Packs (`pioneer/core/pack.hpp`)

A pack is a zip (`.pnpack`, or plain `.zip`) laid out like a content folder,
with a manifest `pack.json`: id, name, version, author, description, what it
holds (`kinds`), the engine versions it runs on, the packs it needs, and every
file's size and CRC plus one checksum over all of them.

```cpp
pack::Manifest m;                       // empty fields are filled from the folder's mod.json
pack::build("mods/veterans/", "veterans-1.2.pnpack", m, err);
pack::readManifest(file, m, err);
pack::Report r = pack::verify(file);    // unpacks every file and compares it with the manifest
pack::extract(file, "somewhere/", err);
```

- **Requirements with versions:** `"requires": ["base_units >= 1.1", "maps"]`
  and `"engine": ">=2.0"`. Operators `>= > <= < = ^ ~`; a bare version means
  "at least". A mod whose requirement is installed but too old gets a warning;
  a pack made for a newer Pioneer is listed as broken and not loaded.
- A folder zipped by hand works too: one top folder is skipped, and a
  `mod.json` is enough (the file list is then taken from the archive).
- Hidden files, editors' backups and `*.tmp` are left out; files that are
  already compressed (`.ogg`, `.png`, `.lmap` ...) are stored as they are.
- **Mods as packs:** drop a pack into a mods folder and it is listed like any
  other mod (`scanMods`). In one folder, a folder wins over a pack with the
  same id, because the folder can be edited.
- In the game: *Mods > Pack into one file*, or `mod.pack <id>` in the console,
  writes `<user folder>/exports/<id>-<version>.pnpack`. The Mission Maker's
  exporter (9.14) and asset packs use the same format.

### Asset registry (`pioneer/core/assets.hpp`)

One record per virtual path: its kind (rules, map, campaign, briefing, sound,
music, texture, model, shader, script, font, pack, data, text), the mount it
comes from, how many layers have it, size, a **version** that goes up with
every change, a content hash on request, **what it uses** and **what uses
it**. Ids never change while the registry lives, also across mod switches.

```cpp
assets::Registry reg;
reg.attach(fs);
reg.rebuild();                    // look at everything mounted
reg.scanDependencies();           // read the JSON, on all cores: campaign -> maps and briefings,
                                  // audio.json -> its sound files, maps.json -> its maps
reg.get("campaigns/ger/campaign.json")->uses;
reg.usersOf(reg.find("campaigns/ger/g01_walkure.lmap"));
reg.setScanner(assets::Type::Model, myScanner);   // other kinds declare their own
```

`reg.apply(changes)` takes what the file system found changed, gives the
changed assets **and everything that uses them** a new version, and tells
listeners in that order. An asset that is named but not there stays in the
registry as *missing*, which is how a campaign with a lost map shows up
(`assets.info`).

`assets::Cache<T>` holds loaded objects by path: it loads on first use (or
many at once on every core with `preload`), hands out shared pointers, and
`reloadChanged()` swaps in a fresh object when the file's version moved.
Whoever still holds the old object keeps it; a file that fails to load keeps
its **last good version** and remembers why. Parts 6 and 7 put shaders,
models and sound banks in such caches.

### Hot reload (`pioneer/core/hot_reload.hpp`)

```cpp
hot.watch("rules", "rules/", ".json", [&](const std::vector<vfs::Change>&) { reloadRules(); });
hot.update(fs, dt, &registry);        // once a frame
```

Every second (`content.interval`) the mounted folders are looked at **on a
worker thread**; the frame never waits for the disk. A changed file is held
back until one more look finds it unchanged, because editors and exporters
write in several steps. A replaced pack is compared entry by entry, so only
what really differs inside it is reported.

What the game takes in while it runs:

| Files | What happens |
| --- | --- |
| `rules/*.json` in the game or any enabled mod | Merged again and put into the running battle, exactly like *Apply* in the rules editor (numbers of types and weapons stay valid; such a battle can no longer be replayed). A file that can't be read changes nothing and says so. |
| `audio/**` | The sound list and files are loaded again. |
| `campaigns/**` | The campaign list is read again. |
| `art/*.png` | The sprite atlas is rebuilt. |
| `maps/skirmish/**` | Nothing to do: the list is read whenever the page opens. |

Shaders, models and Lua scripts join when their parts are built (6, 7, 4).
Automated runs (`--offscreen`) only look when told to (`content.check`).

### Console

`vfs.mounts`, `vfs.ls [folder] [-r]`, `vfs.which <path>`, `vfs.cat <path>`,
`assets.stats`, `assets.list [kind|text]`, `assets.info <path>`,
`assets.rescan`, `content.check`, `content.watches`, `pack.build <folder>
[file]`, `pack.info <file>`, `pack.verify <file>`, `pack.extract <file>
<folder>`, `mod.list`, `mod.pack <id>`; variables `content.hotreload` and
`content.interval`. Paths complete with Tab; a mod's id stands for its folder.

### Tests

`tests/test_data.cpp` (13 cases): path cleaning; layering, mount points and
memory mounts; writing; versions and requirements; building, verifying,
unpacking and tampering with packs; reading from archives and the unpack
cache; hand-zipped mods; change detection in folders and replaced packs; hot
reload's settling; the registry's kinds, links, versions and mod switches;
caches and their last good version; packs as mods through `ContentLayers`;
and one over the game's real content: every file its data names exists.
`--mods-selftest` edits a mod's rules on disk under a running battle, packs
the mod, deletes the folder and plays on from the pack.

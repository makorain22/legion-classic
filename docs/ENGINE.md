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
| 3 | **Simulation:** tagged-chunk saves, determinism checker, sector pathfinding and flow fields, vision 2.0, suppression/morale fields | **done** |
| 4 | **Scripting:** condition/action registry, mission data model, event bus, sandboxed Lua | **done** |
| 5 | **UI framework:** docking, real fonts, UI scale, timeline and node widgets, property grids, key rebinding | next |
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

---

## Part 3 – Simulation

Four things under the simulation that the Mission Maker, big scenarios and
multiplayer need: saves that survive new systems, a way to prove two runs
are the same, routes that stay cheap on big maps, and a vision model that
can hide things.

### Saved state in sections (`pioneer/core/chunks.hpp`, `sim/serialize.cpp`)

A saved state is `SIM9` followed by tagged chunks:

```
u32 "SIM9"
u32 count
count x { u32 tag, u16 version, u16 flags, u32 size, bytes }
```

One section per system, in `Simulation::stateSections()`:

| Tag | What |
| --- | --- |
| `HEAD` | map size, tick, random numbers |
| `CMDS` | commands waiting for the next tick |
| `ECS ` | entities, and every plain component pool under its own tag and item size |
| `GRPS` `MOVE` `PATH` | bound units and garrisons, routes, route requests |
| `SIDE` `TILE` `VISN` | sides and production, structures and cover on the ground, what each side sees |
| `SCRP` `AI  ` | the scenario script and objectives, the computer players |
| `TUTR` `FORM` `LOGI` `MISN` | hints and flags, formations, logistics points, the mission engine |
| `VIS2` | vision rules and smoke (new) |

Rules:

- **A new system adds a section**, optional, with a reset for states that
  don't have it (`Simulation::resetSection`). Nothing else changes.
- **A new plain component** goes on the `PNR_POD_POOLS` list with a tag. A
  state without that pool loads with the pool empty; a pool this version
  doesn't know is stepped over.
- **A component may grow at its end.** The pool records the size one item
  had when saved; shorter items keep the defaults in the new fields.
- A reader refuses a section it doesn't know only if the section is flagged
  `chunk::kRequired`, and a known section with a higher version.
  `Simulation::loadNotes()` lists what was left out.
- States saved by 0.7 to 0.9.1 (`SIM2`..`SIM8`, one stream without a table)
  load through the same section code. `tests/data/sim8_three_sides.lsav`
  is such a save, kept as a test.

`ChunkWriter` / `ChunkReader` are general: packs and network snapshots can
use them.

### Canonical bytes (`pioneer/core/reflect.hpp`)

Plain structs are written as their bytes. The compiler leaves gaps between
fields (padding) that hold whatever was in memory, so two machines in the
same state could write different bytes. `ByteWriter::pod` and `podVector`
blank them: `reflect::podMask<T>()` finds the padding of any aggregate at
start-up with a small structured-binding reflection (`forEachField`, up to
24 fields; nested structs and `std::array` are followed). Types without
padding are written as before, at full speed.

So: **equal states give equal bytes.** Save, load, save again gives the same
file, and a checksum of saved bytes is a checksum of the state.

### The determinism checker (`pioneer/sim/determinism.hpp`)

- `Simulation::stateHash()` – the cheap checksum of units, projectiles and
  mission state, for every tick.
- `Simulation::digest()` – a checksum of **each section** (of the bytes it
  would save). `digestDifference(a, b)` names the sections two states
  differ in: "ECS" (a unit), "AI", "PATH"...
- `HashTrace` – the state checksum at regular ticks. While a game is
  recorded the simulation keeps one sample a second
  (`Simulation::hashLog()`); **replays carry it** (save file version 3) and
  check themselves on playback. A replay made with other rules or another
  version says from which minute it no longer matches, instead of showing a
  battle that never happened. Lockstep multiplayer (part 8) compares the
  same samples between machines.
- `twinCheck(sim, ticks)` – saves the battle, loads it into a second
  simulation on a copy of the map and runs both side by side. A piece of
  state that isn't saved, or isn't deterministic, shows as a difference at
  the tick it first matters, with the sections named.

Console: `hash`, `sim.digest`, `sim.twincheck [seconds]`, `replay.check`.

### Pathfinding (`pioneer/sim/pathfinder.hpp`)

- **Sectors.** The map is cut into 16 x 16 tile sectors. Each knows its
  connected parts per movement type (foot, wheels, tracks) and which parts
  of its neighbours they touch. Regions ("can this tile reach that one")
  come from that graph. When a building goes up or a bridge is blown only
  the sectors whose ground changed are worked out again; before, every
  change flood-filled the whole map three times.
- **Guided search.** For a route of more than 12 tiles the distance to the
  goal's part from every other part (one pass over the sector graph, kept
  for the next units going there) steers the tile A*: where the graph knows
  the way round is longer than the straight line, the search heads for the
  ford or the gap at once instead of filling the ground in front of the
  river. On a test map with one ford and a walled town: 3.4 times fewer
  nodes, routes the same length. On open ground it changes nothing.
- **Route cache.** The same trip (start tile, goal, movement type) asked
  again is answered from memory: units leaving one door for one rally
  point, supply trucks on their round. Emptied when the ground changes.
  **The cache never changes an answer and reports the work the first search
  took**, so the per-tick search budget is spent identically warm or cold
  and a loaded game plays out exactly like the one that went on.
- **Flow fields.** `flowField(goal, loco[, rect])` gives every tile its
  step towards one goal (a Dijkstra pass outwards from it), `flowPath`
  reads one unit's smoothed way off it. One field serves any number of
  units: this is the mover for crowds (civilians, 9.16) and hordes.
- `Pathfinder::stats()`, console `path.stats`.

Routes can differ from 0.9.1's, so replays recorded before part 3 stop
matching (and say so).

### Vision 2.0 (`pioneer/sim/vision.hpp`, `Simulation::VisionRules`)

Each side's grid gains a third bit, and the vision a ground layer:

- `kScouted` – the tile is under a **scout's** eye now.
- Ground: `kWood` (woodland: a tile with a tree and trees on at least three
  tiles round it), `kWoodEdge` (its outskirts), `kSmoke`.
- `stampRays` – sight along rays from the viewer instead of a filled
  circle: a ray ends in smoke and, with line of sight on, where the ground
  rises to hide what lies beyond (a ridge hides the valley behind it; the
  hill beyond the valley is seen; so is the flat top of a hill from below).

What the layers mean is set per battle by `VisionRules`, changed with a
`SetVisionRules` command (so replays and multiplayer stay in step), all
**off by default: the game sees as it did**:

| Rule | Effect |
| --- | --- |
| `kWoodland` | infantry inside woodland is seen (and shot at, and drawn, and shown as a chip) only where a scout looks; on the outskirts, and vehicles anywhere, as usual |
| `kLineOfSight` | rising ground hides what is behind it |
| `sightPercent` | every sight radius scaled (weather, night): 10..200 |

**Smoke** is a `Smoke` command (place, radius, seconds): nobody sees into
or through a cloud until it blows away. Smoke shells (9.18) and scripts
will lay it; `sim.smoke` does from the console.

`Simulation::unitVisibleTo(player, unit)` is the one question to ask ("does
this side see that unit"); targeting, scouts, the computer player and the
game's drawing ask it. `concealed(unit)` says whether the rules hide a unit
where it stands.

### Unit condition (`CCondition`)

Suppression (0..1000), morale, posture (standing, crouched, prone, pinned,
hands up, cowering), flags (hidden, ambush, routing, surrendered),
experience and the tick a unit last gave itself away. Only units something
has touched carry it; it is saved (pool `CCND`) and counts in the state
checksum. Nothing changes it yet: 9.18's suppression, prone, ambush and
veterancy write to it.

### Budget

`tests/test_vision.cpp` runs 1,500 soldiers and tanks fighting in thirty
blocks on a 256 x 256 map for a minute and prints the time per tick
(real time is 66.7 ms a tick). On the development container (two cores):
under 1 ms a tick on average, 2 ms at worst. Two formations of 750 marching
as one body each are the dear case (about 12 ms a tick): formations that
large are for 9.19 to look at.

### Console

`sim.digest`, `sim.twincheck`, `replay.check`, `path.stats`,
`sim.vision [woodland 0|1] [los 0|1] [sight percent]`,
`sim.smoke <x> <y> [radius] [seconds]`.

### Tests

`tests/test_state.cpp` (8 cases: reflection and padding, chunks, the old
save, canonical bytes, newer sections and components, twin check, hash
traces), `tests/test_path.cpp` (+6: regions against a flood fill, sector
rebuilds, guided routes, the cache, a battle warm and cold, flow fields),
`tests/test_vision.cpp` (8: rays against circles, scouted sight, line of
sight, smoke, woodland, weather, condition, the budget).

---

## Part 4 – Scripting

What the Mission Maker (9.14) and the cutscene tools (9.15) stand on: one
description of everything a script can do, missions as structured data
instead of a pile of triggers, and a real programming language for what
triggers can't say.

### The script registry (`pioneer/sim/script_registry.hpp`)

Every condition (38) and action (70) is described once: id, name, group, the
fields it reads (which member of `Condition` / `Action`, what kind of value,
its range and default, its key in files and in Lua) and help: one sentence,
an example, the usual mistakes. Four things read the table:

- **The editor**: hover help in the "+ condition" / "+ action" menus, a
  searchable *Script reference* window, and *Check the script*. (The Mission
  Maker's property grids will be generated from the same fields.)
- **`docs/SCRIPT_REFERENCE.md`** is written from it
  (`script::referenceMarkdown()`); a test fails when the file is out of
  date. `LEGION_WRITE_DOCS=1 ./build/pioneer_tests` rewrites it.
- **Checking** (`script::check(script, context)`): areas, triggers, phases,
  waves, paths and goals that don't exist; unit types not chosen; names
  nothing carries; countdowns never started; objectives never added;
  triggers nothing switches on; waves nothing sends; phases nothing leads
  to; a trigger that fires itself; the clock-hiding trap; Lua that doesn't
  compile; and, with the map, **places units can't get to from where they
  are sent** (the cliff-walled fords of playtest round 1) and path points
  with no way between them.
- **Lua**: every action and condition is callable by its id with a table of
  its fields.

To add a condition or action: the enum, its name and id in `script.cpp`, its
case in `test()` / `perform()`, and its entry in `script_registry.cpp`. A
test fails if the entry or its help is missing. Text in the help is checked
against nothing but your care: say only what the code does.

### The mission model (`pioneer/sim/script.hpp`, `sim/mission.cpp`)

Beside areas and triggers a `ScenarioScript` now holds:

| | What | Runs |
| --- | --- | --- |
| **Phases** | Stages of the mission; exactly one is current. A phase begins when the phase it follows is current and its conditions hold, or by *Go to phase*; phases following the same one branch. On entry: its actions, its music. A trigger with a `phase` is checked only while that phase is current | yes |
| **Goals** | Objectives the simulation keeps score of: destroy, reach, clear, hold, survive, escort, protect; each begins with its phase and runs actions when done or failed | yes |
| **Waves** | Units that arrive together: types with counts for easy / normal / hard, where, after what delay, how often, and their orders (attack or move to an area, follow a path), name, set-piece flags, stance, binding | yes |
| **Paths** | Named routes, point by point, optionally looping; *Follow path* and waves walk them | yes |
| **Sequences** | Timelines of events on tracks (camera, actor, effects, dialogue, screen, flow) for cutscenes | **data only**: carried, saved in maps and checked; played by the sequencer of 9.15 |

New conditions `phase_is`, `phase_elapsed`, `goal_done`, `goal_failed`,
`script_says`; new actions `goto_phase`, `send_wave`, `follow_path`,
`call_script`; events `PhaseChanged`, `WaveArrived`. All of it is in the map
file's script JSON (`phases`, `goals`, `waves`, `paths`, `sequences`, `lua`),
written only when present, so older maps are byte for byte what they were.
The run-time state (current phase, waves on their way, routes, goal states)
is saved in section `MIS2`.

What 9.14 adds on top is the editing: the phase columns, the wave designer,
drawing paths on the map, checkpoints and "test from any phase".

### Lua for missions (`pioneer/sim/mission_lua.hpp`)

A mission may carry a Lua 5.4 script. It runs **inside the simulation**, so
it has to be as repeatable as the rest: on every machine, in a replay, after
a load. Lua is built from source as one unit (`engine/src/lua/pioneer_lua.cpp`)
with the settings that matter in one place:

- The seed of Lua's string hashes is fixed (not the clock and addresses), so
  tables are laid out the same everywhere.
- `^` is worked out by multiplication for whole powers; other powers are
  `nan` rather than the C library's guess, which differs in the last bit
  between platforms.
- Built as C++, so a Lua error unwinds the engine's frames as an exception.

And the sandbox (`mission_lua.cpp`):

- Only a vetted base, `string`, `table` and a small `math`. No `io`, `os`,
  `debug`, `package`, `load`, `require`, `coroutine`, `collectgarbage`,
  `next`, `string.dump`, no trigonometry.
- `pairs` walks keys in a fixed order (numbers rising, then texts) and
  refuses tables keyed by tables or functions; `tostring` never shows an
  address; `setmetatable` refuses `__gc` and `__mode` (both depend on when
  the collector runs).
- `random` and `math.random` are the simulation's dice.
- A call from the engine may run 5 million VM steps and the script hold
  32 MB. A runaway script is stopped with an error; the mission goes on.
- **Saving**: only the global table `state` (plain values and tables,
  written in `pairs` order) and the timers set with `after()` are saved, in
  section `LUA`. On loading, the script is run again to define its
  functions (it may not touch the world while it loads) and `state` is put
  back. The same happens when the script is edited while the battle runs.

The bridge is the registry: `spawn_units{player = 1, area = "North road",
unit = "rus_rifleman", value = 8}`, `is.units_in_area{...}`. Names are
resolved (areas, triggers, phases, waves, paths by name; goals by text; unit
types by id; choices by word) and a misspelt field or name is an error that
lists the choices. Handlers: `on_start`, `on_second`, `on_unit_died`,
`on_phase`, `on_countdown`, `on_wave`, `on_defeated`, `on_objectives`;
events reach them at the end of the tick they happened in. Triggers call
Lua with *Call script* and ask it with *Script says*. The full list is the
"Lua" chapter of `docs/SCRIPT_REFERENCE.md`.

Not in this part: Lua for the interface (menus, HUD). That is presentation
code with other rules (it need not be deterministic) and comes with the UI
framework.

### The event bus (`pioneer/core/events.hpp`)

`EventBus`: typed publish / subscribe. `subscribe<E>(fn, priority)` returns
a guard that ends the subscription when it goes out of scope; `publish(e)`
tells subscribers now, in priority then subscription order; `post(e)` keeps
the event for `dispatch()`. Handlers may subscribe, unsubscribe, publish and
post while being told. The game publishes every `SimEvent` of a tick on
`GameScene::events()`; the existing presentation code (effects, sound,
announcements) is unchanged and new listeners (the Mission Maker's panels,
interface scripts) subscribe instead of being wired in by hand.

### Console

`script.help [word]`, `script.check`, `lua.run <code>`, `lua.errors`.

### Tests

`tests/test_script_registry.cpp` (4: every entry described, the checker,
all 81 shipped missions without errors, the reference up to date),
`tests/test_mission_model.cpp` (4: files, a mission played through its
phases with a wave on a path and a save in mid-flight, the branch, the
checker), `tests/test_lua.cpp` (6: the event bus, a script driving a
mission, mistakes told not fatal, the sandbox, `state` and timers through a
save, triggers calling and asking Lua).

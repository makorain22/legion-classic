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
| 5 | **UI framework:** real fonts and icons, interface size, docking, property grids from the script registry, timeline and graph widgets, string tables, Lua panels | **done** |
| 6 | **Renderer:** device layer, graphics presets with auto-detect, HDR post-processing, time of day, weather, rain and snow, debug drawing | **done** |
| 7 | **Assets and audio:** glTF in and out, .vox, materials, skeletal animation, poses; sound banks, voice lines, reverb zones, occlusion | **done** |
| 8 | **Networking groundwork:** lockstep, hosting, LAN discovery, dedicated server, reconnect | **done** |
| 9 | **Tools:** `pioneer_tool`, asset browser, model viewer, sound bank editor | planned |

## Layout

```
engine/include/pioneer/   public headers, one folder per system
engine/src/core/          log, jobs, profiler, memory, console, virtual file system, packs, assets,
                          hot reload, content layers, fixed-point maths
engine/src/{ecs,sim,map}  the deterministic simulation
engine/src/{platform,render,assets,audio,ui}   window, OpenGL 3.3 renderer, art, mixer, the interface kit and developer windows
engine/src/net/           sockets, transport, sessions, LAN discovery, lockstep (no graphics: in pioneer_core)
game/src/                 Legion Classic itself
server/                   LegionServer, the dedicated server (pioneer_core + game/src/net_world.cpp)
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
| `ENV ` | the time of day, the clock and the weather (part 6) |

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

Lua for the interface is a different thing with other rules (it only looks
at the battle and need not be exact): see *Interface scripts* in part 5.

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

## Part 5 – UI framework

What the Mission Maker, the sequencer and every later tool are built from.
Nothing in the battle interface was redesigned (that is a milestone of its
own, after the Mission Maker); what changed for players is the text, the
*Interface size* setting, and tool windows that dock and stay where they
were put.

```
engine/include/pioneer/ui/fonts.hpp              fonts, interface size
engine/include/pioneer/ui/icons.hpp              ICON_* names, iconList()
engine/include/pioneer/ui/script_fields.hpp      property grids from the script registry
engine/include/pioneer/ui/timeline.hpp           timeline: model + widget
engine/include/pioneer/ui/node_graph.hpp         graph: model, layout + widget
engine/include/pioneer/ui/mission_views.hpp      a mission as graph and timelines
engine/include/pioneer/ui/interface_widgets.hpp  ImGui widgets for interface scripts
engine/include/pioneer/core/strings.hpp          string tables
engine/include/pioneer/core/interface_lua.hpp    Lua panels
```

The rule for this part: **what can be worked out without a window is kept
apart from what draws it.** The timeline's arithmetic, the graph's layout,
the mission views, string tables and the interface scripts' runtime have no
graphics in them and are built into `pioneer_tests`; only the thin drawing
halves need Dear ImGui.

### Fonts, icons and interface size (`pioneer/ui/fonts.hpp`, `icons.hpp`)

- `loadFonts(vfs)` reads `fonts/ui-regular.ttf`, `ui-bold.ttf`, `mono.ttf`
  and `icons.ttf` through the virtual file system (so a mod may bring its
  own) and merges the icons into each text font. Shipped: IBM Plex Sans and
  Mono (SIL OFL 1.1) and Lucide icons (ISC); licences beside them in
  `content/fonts/`. Dear ImGui 1.92+ rasterises glyphs on demand, so any
  size and any character the font has is available without baking ranges.
  Without the files the built-in bitmap font is used and the game still runs.
- `setScale(s)` (0.75 to 2.5) scales fonts and every style metric;
  `px(v)` turns a designed size into pixels. Layouts written before this
  part use fixed pixels: the editor's panels and the menus follow the
  scale, the battle sidebar is drawn inside `ui::Unscaled` and keeps its
  designed size until the in-game interface is redone.
- RAII guards: `Bold`, `Mono`, `Sized(1.5f)`, `Unscaled`.
- Icons are text: `ImGui::Button(ICON_SAVE " Save")`. 109 are named;
  `iconList()` gives them all (a test keeps list and header in step).
- Setting: Settings → Display → *Interface size* (90% to 200%, `uiScale` in
  settings.json); `--ui-scale 1.5` for one run; `ui.scale 130` in the console.
- Docking is on (`ImGuiConfigFlags_DockingEnable`, hold Shift while dragging
  a window to dock it) and window places are kept in `<user>/layout.ini`.

### Property grids (`pioneer/ui/script_fields.hpp`)

`editFields(entry, condition|action, ctx)` draws the fields of a script
line from the registry of part 4: a widget per field by its `Kind` (the
mission's areas, triggers, phases, waves, paths and goals by name; unit
types with a search box; sides; choices; percentages as sliders; numbers
clamped to their range; text), labelled, with the field's help on hover.
The editor's Script tab uses it for every line that has no hand-written
panel, which is how the nine mission-model lines of part 4 (*Go to phase*,
*Send wave*, *Follow path*, *Call script*; *Phase is*, *Phase elapsed*,
*Goal done*, *Goal failed*, *Script says*) became editable, under a new
group in the "+" menus. A line added to the registry later needs no editor
code. `script::describe(line, &script)` gives any line as a sentence from
the registry alone, for lists.

### Timeline (`pioneer/ui/timeline.hpp`)

Model: `TimelineTrack`, `TimelineItem` (start, length; length 0 is a
moment), `TimelineView` (zoom, scroll, playhead, selection, snap);
`snapTo`, `timeLabel`, `rulerSteps` (label spacing that reads well at any
zoom: tenths, seconds, 5 s, 15 s, minutes...), `itemAt`, `moveItem`,
`resizeItem`, `sortItems`. Widget: `timeline(id, tracks, items, view)`
draws tracks, ruler, items and playhead; drag to move (Alt: off the grid),
drag the right edge to stretch, drag the ruler for the playhead, Ctrl+wheel
zooms about the mouse, wheel scrolls, double-click on empty space asks for
a new item, Delete asks to remove the selected one. It changes `items` and
reports what happened in a `TimelineResult`; undo and what an item *means*
stay with the caller.

### Graph (`pioneer/ui/node_graph.hpp`)

Model: `Graph` of `GraphNode` (title, small print, colour) and `GraphLink`
(label, dashed). `layoutGraph` puts nodes in columns by longest path from
the roots (links that close a loop are found by a depth-first walk in list
order and don't count), orders each column by its neighbours' average
place over several sweeps keeping the best ordering seen, and centres the
columns. Same data, same picture. `graphCrossings` counts crossings (a test
lays out a tangle and expects none). Widget: `nodeGraph(id, graph, view)`
pans (drag the background, or right/middle button), zooms about the mouse,
drags boxes, selects, reports double-clicks.

### Mission views (`pioneer/ui/mission_views.hpp`)

`phaseGraph(script, current)`: phases as boxes, solid arrows where a phase
begins by itself, dashed ones for *Go to phase* actions of triggers,
on-entry actions and goals; a jump from "any phase" is written on the box
instead of drawn from everywhere. `sequenceTimeline` / `applyTimeline`: a
`MissionSequence` to tracks and items and back after editing.
`waveTimeline`: when each wave arrives. These are what 9.14 and 9.15 put
on screen.

### String tables (`pioneer/core/strings.hpp`)

`lang/<id>.json`: `{"language", "name", "strings": {nested keys}}`. Every
layer's copy of a file is read, lowest first, so a mod adds or changes
words without replacing the game's file. `loc::tr("settings.display.language")`
returns the chosen language's string, else English, else the key (which is
then listed by `lang.missing`). Patterns: `{name}`, and
`{n|# unit|# units}` for numbers, with plural rules for one-form languages
(Malay, Japanese, Chinese...), one/other, French, Slavic, Polish and Czech,
chosen by language id or the file's `"plural"`. `Strings::resolve("@key")`
is for text that comes from data. Pseudo-translation (`lang.pseudo`) turns
table text into accented, longer text so anything not from the table, or
not fitting, shows at once.

The framework is in; the interface's existing English is **not** moved into
the table yet (only the new settings and the interface kit are). That is
done screen by screen as screens are rebuilt, so nothing is translated
twice. The setting appears in Settings → Display once a second language
file exists. The shipped fonts cover Latin (with Vietnamese), Cyrillic and
Greek; a language in another script needs a font from its mod.

### Interface scripts (`pioneer/core/interface_lua.hpp`)

`ui/*.lua` in the game or a mod: Lua that draws panels.

```lua
local lost = 0
on("unit_died", function(e) if e.owner == game.player then lost = lost + 1 end end)
panel{ title = "Losses", draw = function()
  ui.heading(lost .. " lost")
  if ui.button("Reset") then lost = 0 end
end }
```

- **Apart from the simulation.** A second Lua state, not the mission's. It
  reads values the game provides (`game.seconds`, `game.supplies`,
  `game.selected`, `game.phase`...), hears events by name, and draws. It
  cannot give orders, is not saved with a battle and is not part of
  replays, so it may use `math.random` and the clock. `console("...")`
  runs a console line and returns what it printed (cheat commands still
  need `cheats 1`).
- **Sandboxed and limited** like mission scripts: no `load`, `require`,
  `io`, `os`, `debug`; text chunks only; 16 MB; two million VM instructions
  per call. A panel or handler that fails three times in a row is switched
  off and said so; opening the panel again retries it.
- **Widgets behind an interface** (`InterfaceWidgets`): the runtime is in
  `pioneer_core` and tested with widgets that only record; the game passes
  `ui::makeInterfaceWidgets()`.
- **Kept between runs:** the scripts' `saved` table and which panels were
  open (`<user>/ui_scripts.json`). Scripts reload when a file under `ui/`
  changes.
- Panels are closed until opened: `ui.panel "Battle report"`, or the
  interface kit's Scripts tab. Shipped example: `content/ui/battle_report.lua`.

### The interface kit and the console

`ui.kit [0|1] [tab]` opens a window showing all of the above on the game's
own data: fonts and icons (click an icon to copy its name), every script
line in its property grid with the JSON it saves as, the timeline with a
sample sequence and the mission's waves, the mission's phases as a graph
(a sample when it has none), string tables, interface scripts.

`ui.scale [percent]`, `ui.scripts`, `ui.panel <title> [0|1]`, `ui.reload`,
`lang.list`, `lang.set <id>`, `lang.missing`, `lang.pseudo [0|1]`.

### Tests

`tests/test_ui.cpp` (9: timeline arithmetic and editing, graph columns with
loops, untangling, phases as a graph, a sequence to a timeline and back,
string tables with plurals and fallback, tables through the file system
with a mod's language, fonts present and icons listed once),
`tests/test_interface_lua.cpp` (4: a panel reading, hearing and drawing;
mistakes contained; the sandbox, limits and `saved`; the shipped script),
and one more in `tests/test_script_registry.cpp` (every line put into words).

## Part 6 – Renderer

What the battlefield looks like at any hour and in any weather, on any
machine. The user's brief for this part was immersion, not effects for their
own sake: time of day, weather, light that stays readable, and presets that
run on old machines. **Noon under a clear sky is exactly the picture the
engine drew before**: checked pixel for pixel against the build before this
part at Low, Medium and High and in the classic view.

```
engine/include/pioneer/render/graphics.hpp       options, presets, auto-detect, GLES shaders, colour filters (no GL)
engine/include/pioneer/render/device.hpp         what the card can do, render targets, GPU timer
engine/include/pioneer/render/post.hpp           the post-processing pipeline
engine/include/pioneer/render/environment.hpp    the hour and the weather as light (no GL)
engine/include/pioneer/render/precipitation.hpp  rain and snow in the air
engine/include/pioneer/core/weather.hpp          names of the weather, times of day (shared with the simulation)
engine/include/pioneer/core/debug_draw.hpp       debug shapes in the world (no GL)
engine/include/pioneer/ui/debug_view.hpp         draws them over a view
game/src/graphics.cpp                            the settings page, auto-detect, lighting each frame, console
```

As in part 5, **what can be worked out without a window is kept apart from
what draws it**: `graphics_settings.cpp`, `environment.cpp`, `weather.cpp`
and `debug_draw.cpp` are built into `pioneer_tests`.

### Graphics quality (`pioneer/render/graphics.hpp`)

`gfx::Options` holds every choice; four presets fill it:

| Preset | What it draws |
| --- | --- |
| Low | Straight to the screen as before 2.0: no shadows, no pipeline, thin rain and snow. For old and slow machines, and software renderers. |
| Medium | Shadows (1024), the pipeline: mist, clouds' shadows; edges smoothed the fast way (FXAA); no glow. |
| High | Shadows 2048, glow round fire and flashes, 4x multisampling, full rain and snow. |
| Ultra | As High with 4096 shadows. |

Anything else is *Custom*. The colour filter is not part of a preset and is
kept when the preset changes. `sanitized` keeps options in range;
`effective(options, postAvailable, hdrAvailable)` cuts what the card can't
do (no float targets: no glow; no pipeline: no mist, clouds or glow, and the
window's own smoothing).

**Auto-detect** (`autoDetect(Machine)`), on the first run and from *Choose
for this machine*: a software renderer (llvmpipe, SwiftShader, Microsoft
Basic Render...), no float targets or textures under 4096 → Low. Otherwise
the battlefield is drawn at Medium for 120 frames and timed: over 14 ms →
Low, over 6.5 → Medium, over 2.5 → High, else Ultra. Without a measurement,
by the card's name (old integrated graphics → Medium).

Where: Settings → Display → *Graphics quality* (shown while the 3D
battlefield is on). `--gfx low|medium|high|ultra` for one run; offscreen
runs use Medium so the pipeline is exercised. Console: `gfx.info`,
`gfx.preset [low|medium|high|ultra|auto]`, `gfx.set <option> <value>`.

**OpenGL ES 3.0:** every shader goes through `shaderSource(src, api)`, which
swaps the `#version 330 core` line for `#version 300 es` and the precision
lines it needs. The engine still runs on OpenGL 3.3; this is the seam for a
second API.

**Colour filters** for colour-blind players (red-weak, green-weak,
blue-weak): daltonisation, built from the full-severity simulation matrices
of Machado, Oliveira and Fernandes (2009), applied in the pipeline's last
pass. White stays white; a test recomputes the matrices.

### The device layer (`pioneer/render/device.hpp`)

`gfx::queryCaps()` / `caps()`: vendor, renderer, version, the largest
texture, whether half-float targets can be drawn into (found by trying),
timer queries, software rendering. `RenderTarget` (RGBA8 or RGBA16F, with or
without a depth texture, or sharing one), `bindScreen`, `drawFullscreen`
(one triangle from `gl_VertexID`, no vertex buffer), `GpuTimer`.

### The pipeline (`pioneer/render/post.hpp`)

```
beginScene()   the world is drawn into a half-float target at the render scale
               (4x multisampled renderbuffers when edges are smoothed the best way)
light(look)    the drawn world lit for the hour and the weather: the light's colour,
               saturation, clouds' shadows, haze and ground mist (world positions
               rebuilt from depth). Skipped when the look is the identity.
               Fire, smoke, flashes, rain and snow are drawn after it and keep their brightness.
present()      glow (a chain down and up over what is brighter than white), exposure,
               a soft shoulder, the colour filter, grain in dark skies, FXAA;
               then the interface, untouched
```

Haze begins at `fogNear` (70%) of the eye's distance to what the camera
looks at, so the fighting in view stays clear and the distance fades; ground
mist starts nearer and lies in the map's low ground (`fogBase` is the lowest
ground, thinning over a third of the map's height range).

### Time of day and weather (`pioneer/render/environment.hpp`)

`lightingFor(Environment{hour, weather, previous, change, seconds})` gives a
`Lighting`:

- `look` – the `SceneLook` for the pipeline;
- `sun` – towards the light that casts shadows. The sun rises at 6, sets at
  18 and swings 160° across the sky (never drawn lower than 14°, or shadows
  would run off the map); the moon lights the night from the other side;
  they hand over at dusk and before dawn while the light is too weak for
  sharp shadows. At 12:00 it is exactly the classic sun;
- `direct` – the share of light straight from it. Models lose contrast to
  an even sky light as it drops, the ground's slopes flatten, shadows fade;
  below 0.2 the shadow pass is skipped;
- `wet`, `snowCover` – the ground darkens in rain; snow lies where the ground
  is flat enough, in ragged patches, never quite hiding roads and fields;
- `rain`, `snow`, `wind`.

The day is a table of light keys eased into one another: moonlit night
(blue, dim), first light, sunrise (warm, low, mist in the valleys), morning,
day (9:30 to 14:30: the picture as drawn), late afternoon, golden hour,
sunset, dusk. **The battle stays readable:** however dark the hour and the
weather together, the exposure rises to keep the world at least at a set
brightness.

| Weather (`id`) | Light | Shadows | Mist | In the air | Ground |
| --- | --- | --- | --- | --- | --- |
| Clear (`clear`) | as drawn | full | – | – | – |
| Fair with clouds (`fair`) | as drawn | full; clouds' shadows drift over the ground | – | – | – |
| Overcast (`overcast`) | greyer, paler | faint | light haze | – | – |
| Rain (`rain`) | grey | almost none | haze | rain | wet |
| Storm (`storm`) | dark | none | thick haze | heavy rain, slanting | wet |
| Snow (`snow`) | cold, pale | faint | haze | snow | snow lies |
| Blizzard (`blizzard`) | white-grey | none | very thick | snow driven sideways | snow lies |
| Mist (`mist`) | soft | faint | thick, deep in low ground | – | damp |

Weather turns from one kind to the next over the seconds asked: every value
is blended, and rain and snow can fall together while it does.

**Renderers.** `TerrainRenderer::setSun(dir, direct)`, `setGroundCover(wet,
snow)`; `MeshRenderer::setSun(dir, direct)`; `ShadowMap::fit(..., light)`.
The terrain's slope light, baked per vertex from the classic sun, is worked
out in the 3D vertex shader from the vertex's slope when the sun is anywhere
else (the same arithmetic, so it matches at noon). **Without the pipeline**
(Low, and the classic 2D view) the light goes into the colours as they are
drawn: the terrain's tint and the sprites' and models' colour grade; haze
and mist need the pipeline.

**Only the look.** The simulation keeps the clock and the weather (section
`ENV`: the time of day when it was last set and the tick, how fast the day
runs, the weather, what it is turning from, since when and over how long) so
that a save or a replay shows the same sky, but nothing in the rules reads
them. A mission that wants night to shorten sight changes the vision rules
of part 3 as well.

| Where | How |
| --- | --- |
| The scenario | Editor → Script → Quick start → *Time and weather*: begins at (quarter hours), the clock (stands still, real time, 10, 30, 60 or 120 times as fast), the weather. In the map file: `"environment": {"time": "05:30", "clock": 60, "weather": "mist"}`, written only when not noon, clear and still. |
| Script actions | `set_time_of_day` (`text` "hh:mm", `value` the clock), `set_weather` (`text` a weather id, `value` seconds to turn), in the editor's *The battlefield* menu, checked by *Check the script*. |
| Lua | `set_time_of_day{text = "21:15", value = 0}`, `set_weather{text = "snow", value = 60}`; a time or weather that doesn't exist is an error. |
| Console | `env.time [hh:mm] [clock]`, `env.weather [id] [seconds]` (through `CommandType::SetEnvironment`, so they are recorded like any command). |

### Rain and snow (`pioneer/render/precipitation.hpp`)

6,000 drops made once (a fixed seed) are drawn instanced. The vertex shader
moves each with the fall and the wind and wraps it into a box between the
eye and what it looks at, as large as the view is far: the box follows the
camera, the drops stay put in the world, and nothing is sent to the card per
frame but a few numbers. Rain is streaks along the fall (slanting in a
storm), snow is swaying flakes (driven sideways in a blizzard). They are
depth-tested against the world, faded near the eye and the box's edges,
and seen in the scene's light. How many: *Rain and snow in the air* – none,
thin (a third), full. 3D view only; the classic view gets the light and the
ground but nothing in the air.

**Wind on smoke:** smoke and dust have always leaned in a light breeze; the
weather's wind is added on top (`BattleFx::setWind`), so under a clear sky
nothing changes.

### Debug drawing and photos

`debug::DebugDraw::global()`: `line`, `box`, `circle`, `cross`, `arrow`,
`text` in world units (tiles), each with seconds to last (0: the next frame
only, for code that runs every frame) and a channel. `endFrame(dt)` ages
them; `clear(channel)`, `showChannel`, `setEnabled`; at most 20,000 kept.
`ui::drawDebugShapes(shapes, drawList, project)` draws them with ImGui over
the view (on top of everything, so they show through hills and walls); the
game projects through the 3D camera, or onto the ground in the classic view.
Console: `debug.draw [0|1]`, `debug.clear [channel]`, `debug.channel <name>
[0|1]`, `debug.sun [seconds]` (live: an arrow towards the sun or moon, the
wind, the time, the weather and how much of the light is direct),
`debug.areas [seconds]` (the mission's areas as boxes with their names).

**Photos:** `App::requestScreenshot(path, false)` saves the frame before the
interface is drawn over it; scenes ask `App::takingPhoto()` and leave out
their own selection rings, health bars, markers and debug shapes. Console
`photo [name]`; `--photo out.png --frames N` as `--screenshot`.

### Not done on purpose

- Haze and mist don't change what units can see (see *Only the look*).
- Render-only objects moving along splines (aircraft overhead, trains) were
  listed as optional for this part; they are left to 9.16, which needs them.
- Lightning, puddles and splashes: effects for their own sake.

### Tests

`tests/test_graphics.cpp` (9: presets recognised as themselves and only
adding as they go up, options kept in range, what a weaker card gets,
auto-detect for software renderers, old cards, measured times and names,
GLES shaders, colour filters keeping greys and matching a recomputation from
Machado et al.), `tests/test_environment.cpp` (9: names and times of day;
noon and clear is the identity and the classic sun; the sun's path, no jumps
minute to minute, midnight round to the start; night dark but readable, at
every weather; each weather's effect; turning from one to the next; the
battle's clock from the scenario, the editor and past midnight; script
actions, saved and loaded, a change under way, the console command and the
checker; Lua), `tests/test_debug_draw.cpp` (3: lifetimes, channels, the
lines each shape is made of).

## Part 7 – Assets and audio

What the battle sounds like, and models in and out of the engine. The
user's brief, as for part 6, was immersion: **sound that serves it** -
authentic recordings, men who answer and call out, the land shaping what
is heard - and model formats a modder (or a later game on this engine) can
work in.

```
engine/include/pioneer/audio/acoustics.hpp   spaces (reverb zones) and occlusion over the ground (no device)
engine/include/pioneer/audio/bank.hpp        sound banks: named events from JSON, the player that keeps their limits
engine/include/pioneer/audio/voice_lines.hpp voice sets, cues and lines; the director (one voice at a time) and subtitles
engine/include/pioneer/audio/audio.hpp       the mixer: per-space reverbs, occlusion, the field-radio filter
engine/include/pioneer/assets/vox_file.hpp   MagicaVoxel .vox in and out
engine/include/pioneer/assets/gltf.hpp       glTF 2.0 in (.gltf, .glb) and out (.glb); materials; voxel models as meshes
engine/include/pioneer/assets/skeleton.hpp   skeletons, clips, sampling, blending, skinning
engine/include/pioneer/assets/voxel_rig.hpp  voxel figures posed by a skeleton; the humanoid and its infantry poses
game/src/game_audio.cpp                      banks, voices and acoustics in the game
game/src/audio_commands.cpp, asset_commands.cpp   console
tools/audio/recordings.py                    field recordings cut and levelled into the game's sounds
```

All of it runs without a window or a sound card: `acoustics.cpp`,
`bank.cpp`, `voice_lines.cpp`, `vox_file.cpp`, `skeleton.cpp`, `gltf.cpp`
and `voxel_rig.cpp` are built into `pioneer_tests`. Nothing here reaches the
simulation: sound and models are presentation.

### Sound banks (`pioneer/audio/bank.hpp`)

The game's sounds were named in one file, `audio.json`, read by the game.
That reader is now the engine's: a `SoundBank` holds named `SoundEvent`s
(the files to pick from, bus, volume, range, priority, reverb send, random
pitch, starts a second, voices at once, loop) and an `EventPlayer` plays
them on a mixer, keeping to the limits (thinned at high game speed, as
before). New:

- **Several bank files.** Each content folder's `audio.json`, then its
  `banks/*.json` in name order, base game first and mods after. A later
  file's event replaces an earlier one of the same name, so a mod redoes
  one sound without copying the rest. `audio.info` lists the files read.
- **Far recordings.** `"far": [...]` with `"farBelow"` (default 0.3): when
  the sound would reach the ear at less than that, a far recording is
  played instead of a quieter near one. A rifle a long way off is a flat
  crack, not a small copy of the one beside you.

### Voice lines and subtitles (`pioneer/audio/voice_lines.hpp`)

Bank files may carry `"voices"`: sets (any name: a language, a squad, a
radio operator), each with a speaker's title, an optional `fallback` set,
and cues ("move", "incoming"), each cue with lines - a file, what is said,
and for a foreign language what it means:

```json
"voices": {
  "de": { "language": "de", "speaker": "Unteroffizier", "fallback": "battle",
          "cues": { "move": { "cooldown": 7, "priority": 1, "chance": 0.7, "radio": true,
                              "lines": [ { "file": "rec/voice/vo_de_los.ogg", "text": "Los!", "meaning": "Go!" } ] } } } }
```

`VoiceDirector` speaks them: **one voice at a time** (a line waits up to
2.5 s for the one before, or cuts in when its cue's priority is two or more
above), a **cooldown per cue** so a busy battle doesn't chatter, `chance`
to leave some orders unanswered, and a **subtitle** for every line spoken,
held 1.6 s after it ends or long enough to read, whichever is longer. Text
may be a string-table key (`@voice.de.move.1`), resolved when drawn.
`radio` lines go through the mixer's field-radio filter.

`audio.json` maps factions to sets (`"factions": {"ger": "de", ...,
"default": "en"}`); the player's faction picks who speaks for them. The
cues the game uses: `move` and `attack` (an order given), `ready` (a unit
comes off the production line), `incoming` (enemy shells landing in view),
`man_down` (one of yours falls in view), `vehicle_lost`, `captured` (a
supply point taken). Settings → Sound: **Voice lines** and **Subtitles**,
both on by default.

### Spaces and occlusion (`pioneer/audio/acoustics.hpp`)

`Acoustics` is a coarse grid over the ground (cells of 4 tiles) holding
each cell's ground height, how much of it is buildings and how much trees,
and its **space**:

| Space | How it rings |
| --- | --- |
| open | the big soft hall the mixer always had (unchanged numbers) |
| forest | short, dense and dark: trunks scatter, leaves eat the top |
| town | hard and bright, with slapback off the house fronts (55, 83, 131 ms) |
| valley | a long tail and echoes rolling back off the slopes (0.3–1.1 s) |

`classify()` picks each cell's space: a street from enough buildings, a
wood from enough trees, a valley where ground rises 1.25 tiles or more on
both ends of a line through the cell (across, down or either diagonal - one
hill beside you is not a valley). `ReverbZone`s (circles with a tag) laid
on top override the cells: for a mission or a set piece that wants a cave,
a cathedral or a river gorge to sound so.

**Occlusion** walks the line from a sound to the ear in half-cell steps
(leaving out the cells they stand in): ground rising above the line blocks
it by how far (a crest leaks over the top, so never more than 0.85),
buildings soak it up (0.35 a cell), trees dull it a little (0.08). In the
mixer, `Emitter::occlusion` makes a sound quieter (up to 55%), much duller
(the low-pass falls towards 300 Hz) and wetter (more of it arrives by
reflection). Voices are re-checked eight times a second and glide, so
panning the camera over a ridge opens a muffled fight up smoothly.

The mixer runs **one reverb per space**; each voice rings in the space of
the place it comes from. Open ground always runs (exactly as the single
hall did); the others only while something has been sent to them and until
their tail has died. **Without acoustics, or on flat open land, the mix is
sample for sample what it was** (a test compares the two). The game builds
the grid from the map (`GameAudio::setMap`) and again when buildings go up
or come down (the map's revision, checked every two seconds).
`audio.acoustics 0` turns it off to compare.

### The field radio

`PlayDesc::radio`: mono, a high-pass near 450 Hz, a two-stage low-pass at
3 kHz and a little overdrive. Order acknowledgements use it; shouts on the
field don't.

### The recordings

The game's sounds were synthesised (`tools/audio/sfx.py`). Part 7 replaces
the battle's with **field recordings**: gunfire, guns, engines, the land
and men's voices, many of them of the very weapons in the game (a
Lee-Enfield, a Mauser 98k, an MG42, a Bren, a 37 mm and a 6-pounder anti-tank
gun, a Churchill's engine) and US Army recordings of artillery, mortars and
machine guns. All from Freesound, **CC0 or CC BY** only (both may be shipped
with the game; CC BY ones are credited). `content/audio/RECORDINGS.md` lists
every one with its author, link and licence.

`tools/audio/recordings.py` turns them into the game's sounds (99 sounds,
27 distant variants, 6 ambience beds and 17 voice lines from 101
recordings):

- takes with several shots are cut into one sound per shot at their
  onsets; every gun, blast and impact then **starts on its bang** (the first
  moment at 40% of its loudest on a 10 ms envelope, so a click before it
  doesn't count), so the sound lands with the muzzle flash; a take that
  gives one piece keeps its whole burst; slivers are dropped;
- engines, feet, fire and the land become **seamless loops** (an
  equal-power crossfade of the end into the start; layers of different
  lengths are repeated to the longest and the mix is joined once more);
- everything is resampled to 48 kHz, cleaned below 30 Hz, and **levelled to
  the loudness of the synthesised sound it replaces**, so the balance of the
  mix stays as it was tuned. Those target levels were measured once and are
  kept in `tools/audio/levels.json`, so the tool can be run again;
- the output is bit-exact: the same recordings give the same files.

Where a weapon in the rules has its own recording it gets its own event:
the Lee-Enfield (`rifle_smle`) and the 6-pounder (`cannon_6pdr`, also its HE
round). The interface sounds (`sfx/ui_*`) and the music are unchanged; the
synthesised battle sounds they replaced are gone from `content/`
(`tools/audio/sfx.py` still makes them). Three long takes didn't download
(a roaring fire, a forest bed, a winter forest bed): the fire is the
crackling take alone, temperate ground has no forest layer, and winter is
the cold wind alone.

### MagicaVoxel (`pioneer/assets/vox_file.hpp`)

`readVox` takes every model in a .vox (SIZE and XYZI pairs) and its
palette; `writeVox` writes one. Axes match (z up). **Team colour travels as
magenta**: red and blue equal, no green, red = brightness × 192 - so a
modder paints team colour in magenta and it comes back as team colour. A
plain colour that happens to look like that is nudged off it on the way
out. Console `model.export <unit> vox` writes a unit (and its turret).

### glTF (`pioneer/assets/gltf.hpp`)

A `ModelAsset`: meshes (positions, normals, colours, texture coordinates,
four joint weights), `Material`s (metallic-roughness: base colour, metal,
roughness, emission, a base-colour image's file, alpha mode, double-sided,
and a team-colour flag kept in the material's `extras`), a skeleton and
clips.

- **Reading** .gltf (buffers as base64 or in .bin files beside it) and
  .glb. Static meshes are placed by their nodes into one space (a mirroring
  node's triangles are turned back the right way out); skinned meshes stay
  in bind space. glTF is y-up and the engine z-up: everything is turned
  upright on the way in (a root joint `__yup` above the skeleton does it for
  skinned models, so animating the real root doesn't undo it) and laid
  down again on the way out. Cubic-spline keys keep their values. Not read:
  morph targets, cameras, lights, sparse accessors, extensions.
- **Writing** a .glb with everything above; a file this engine wrote reads
  back with the same skeleton (the upright joint is used again, not
  doubled).
- `voxelModelAsset()` turns a voxel model into a mesh (shared corners,
  crowded corners a little darker as the game shades them, team-colour
  faces as their own part and material), 1 unit = 1 tile.
  `model.export <unit> [glb]` writes a unit to the exports folder, ready
  for Blender; `model.info <file>` says what a glTF file holds as the
  engine reads it.

### Skeletons and animation (`pioneer/assets/skeleton.hpp`)

`Skeleton` (joints with a parent, a rest placement and an inverse bind
matrix; `bindRest`, `sortParentsFirst` for files in any order), `Pose`
(each joint's local placement), `AnimationClip` (channels moving a joint's
translation, rotation or scale through step or linear keys; rotations
slerped the short way), `sample` (looped or held at the end; joints a clip
doesn't move keep the base pose), `blend` with an optional per-joint mask
(`maskBelow`: the upper body over the legs), `globalMatrices`,
`skinningMatrices` and `skinVertex` (four weights). Matrices are the
renderer's `Mat4`.

### Infantry poses (`pioneer/assets/voxel_rig.hpp`)

A voxel figure is cut into rigid parts, each fixed to a joint
(`assignByRegion` gives every voxel to the bone passing closest to it);
`poseVoxels` lays the parts down again where the pose has put their joints
- working backwards from each cell of the posed grid into the part at
rest, so a turned part leaves no holes. `humanoidRig()` is the standard
figure: 15 joints sized for the engine's 18-voxel soldier and the infantry
poses as clips - **stand, walk** (four steps a second), **run, kneel,
kneel_aim, prone, prone_aim, throw, fall_back, fall_forward**. Standing
gives back the figure exactly as built.

### Console

`audio.info`, `audio.play <event> [here]`, `audio.say <cue> [set]`,
`audio.acoustics [0|1]`, `audio.space [seconds]` (the space under the
camera, and the cells round it drawn in colour with the occlusion from each
to the ear), `model.export <unit> [glb|vox]`, `model.info <file>`.

### Not done on purpose

- **The game's soldiers are not yet drawn from the rig.** The rig, its
  poses and the tests are here; the soldiers keep their hand-built frames
  until the in-game visuals are redone (their kit differs per weapon, and
  switching changes how every battle looks - it is a decision for that
  milestone, not a side effect of this one).
- glTF models are read and written but not drawn in the battle: the 3D view
  draws voxel models. The model viewer of part 9 is their first user.
- No voice lines were recorded for this part: they come from freely
  licensed recordings, so the English set is fullest, the German and
  Russian ones are short (and fall back to wordless sounds, never to another
  language). A set is a few lines of JSON: recorded lines can be added
  without code.
- Reverb zones are not yet placed from the editor (the engine and the
  console have them; the mission editor's turn comes with 9.14).

### Tests

`tests/test_audio_part7.cpp` (12: a hill between a sound and the ear
blocks it and only then, the same both ways, a crest leaking; houses and
woods; spaces from cells, valleys needing both sides, zones and tags; the
open space is the old hall; flat open land sounds sample for sample as no
acoustics; a fight behind a hill quieter and duller at the top; occlusion
following the ear; the street's slapback; the radio band; banks replacing
by name, limits, far recordings, missing files; voices one at a time,
cooldowns, cutting in, waiting, subtitles and their meaning, fallback sets;
every bank file, sound and voice line in the game exists, decodes and has
its words), `tests/test_assets_part7.cpp` (12: .vox round trip with team
colour, several models, damaged files; skeleton rest, bending, skinning;
decompose, invert, slerp the short way; sampling, looping, step keys,
masked blends; sorting joints; glTF round trips of a voxel model and of a
skinned animated model - twice - and a hand-written .gltf with a node
transform; the humanoid's joints and poses; standing exact; kneeling,
prone, walking, falling, nothing lost or smeared).

---

## Part 8 – Networking groundwork

Multiplayer for Legion, built in layers so another game on the engine can
take the lower ones as they are. Everything under `engine/net/` knows
nothing about Legion; the game plugs in through two small points (a session
config and a lockstep adapter). All of it is in `pioneer_core` - the
dedicated server links nothing else.

```
LegionClassic / LegionServer      game/src/net_game.cpp (scene, lobby, overlay), net_world.cpp (orders, NetWorld)
Lockstep                          turns, delay, waiting, pause, checks, snapshots     net/lockstep.hpp
Session  + DiscoveryAnnouncer/Browser   members, lobby, chat, rejoin / LAN beacons    net/session.hpp, discovery.hpp
Transport                         connections, reliable + unreliable messages         net/transport.hpp
DatagramSocket: UdpSocket | LoopbackNetwork (tests)                                   net/socket.hpp, loopback.hpp
```

Nothing blocks and nothing makes threads: every layer has `update(nowMs)`
and is given the clock, so tests run whole games over a simulated network
in a fraction of a second, the same way every time.

### Sockets (`pioneer/net/socket.hpp`, `loopback.hpp`)

- `Address` (IPv4 and port), `parseAddress("host:port", defaultPort, ...)`
  (numbers, or a name looked up).
- `DatagramSocket`: send to an address, take the next datagram (never
  waits). `UdpSocket` is the real one (Winsock or POSIX; non-blocking,
  broadcast on request, a shared port for LAN browsing, 1 MB buffers, the
  Windows "connection reset" quirk turned off).
- `LoopbackNetwork`: sockets in memory with latency, jitter (datagrams
  overtake each other), loss, doubled datagrams and computers cut off, on
  the test's clock with seeded dice.
- Datagrams stay under 1200 bytes (`kMaxDatagram`).

### Transport (`pioneer/net/transport.hpp`)

One socket, any number of connections, each with a **reliable** channel
(every message once, in order, any size: big ones go in 1100-byte pieces)
and an **unreliable** one (may be lost, never repeated).

- Every datagram has a sequence number and acknowledges the last 33 the
  other side sent; a reliable piece stays queued until a datagram carrying
  it is acknowledged and goes again after 1.5 round trips (50 ms - 1 s).
  A burst of more than 24 datagrams is acknowledged part-way, so the start
  of a big message isn't sent twice.
- The round trip comes from the acknowledgements; sending is limited per
  connection (`sendRate`, 512 KB/s; a burst of 1/8 s), so a battle's state
  for a player who rejoins doesn't flood a slow line.
- Handshake Connect / Accept with a random number from each side that every
  later datagram must carry; the Connect is padded so no answer is bigger
  than its question. `protocolId` keeps other games and versions out.
  Disconnect, or a timeout (8 s of silence); `NoAnswer` and `Refused` for
  connecting. A computer that connects again from the same address
  replaces its old connection.
- `stats()`: round trip, datagrams sent, received and lost, pieces resent,
  bytes queued, time since anything arrived.

### Sessions (`pioneer/net/session.hpp`)

A host and its members. The host is a player's game (member 0, a *listen
server*) or a **dedicated server** (no member of its own; the connected
member with the lowest id is the **leader**, who chooses the settings and
starts).

- Joining is checked: the same `gameId`, `gameVersion` and `contentHash`
  (Legion: the merged rules' hash - mods included), the password; a refusal
  says why ("The host runs version ...", "Your rules or mods are not the
  same as the host's", "Wrong password", "The game is full", "The game has
  already started"). The same name twice gets a number.
- Members: id, name, ready, connected, ping (every two seconds), and a text
  the game owns (Legion: side and army as JSON). Settings: one text the
  leader owns (Legion: the skirmish setup). Both reach everyone when they
  change.
- Chat with a log; names and lines cleaned (no control characters, cut on
  whole UTF-8 characters); at most five lines in three seconds per member;
  joins, leaves and losses are chat lines too.
- **Rejoining.** In a game (`setInGame(true)`), a member whose connection
  is lost keeps its place for `rejoinSeconds` (120); its session sees the
  host go quiet, says `Reconnecting` and keeps trying with the token the
  host gave it, and gets its old id back (`Rejoined` / `MemberBack`). In the
  lobby a lost member simply leaves.
- `kick`, `leave` (members hear why).
- Messages for the systems on top: types from 32 up, to the host, to one
  member or to all, by registered handler; listeners for the events, and a
  queue for the interface.

### LAN discovery (`pioneer/net/discovery.hpp`)

A host broadcasts a beacon once a second to port 47801 (and to the same
port on this computer, for a second copy of the game running here): game
id, version, name, description, port, players, in game, password,
dedicated. A browser listens on that port (shared: two browsers on one
computer both hear) and keeps the games heard within 3.5 s, one entry per
host even when heard twice. Hosts listen for nothing.

### Lockstep (`pioneer/net/lockstep.hpp`)

Every machine runs the same simulation; only orders travel. The host is
relay and referee.

- **Turns.** Players send orders to the host. The host closes one turn per
  tick on its own clock - the orders that reached it, in the order they
  came - and sends it to everyone; every machine runs tick *t* once it has
  turn *t*, applying that turn's orders first. Orders are bytes here; the
  game says what they mean (`LockstepAdapter::apply`).
- **Delay.** The host runs its own battle `delay` ticks behind the turns it
  has closed, so turns arrive before they are needed. The delay follows
  the slowest round trip (half of it, plus the turn players keep in hand):
  one tick on a LAN, up at once when a line gets slower, down one step
  every four seconds. Players keep a turn in hand and catch up (up to 8
  extra ticks a step) when they fall behind.
- **Waiting.** A player more than two seconds behind the host's battle
  whose line is alive (a slow computer) holds everyone: the host stops
  closing turns and everyone sees *Waiting for ...*. A player whose line
  has gone silent doesn't hold anyone: it catches up from the turns when
  its line is back, or rejoins.
- **Pause** for everyone, from anyone (`anyonePauses`): the host stops the
  turns.
- **Checks.** Players send the state hash every second; the host compares
  it with its own battle's (keeping players' hashes that are ahead of it
  until it gets there). On a difference - a **desync** - the host asks the
  player for its per-system digest at a tick neither has run yet, takes its
  own there, names the systems that differ (log, `onDesync`, a notice to
  everyone) and sends the player its own battle (**resync**).
- **Snapshots** - the adapter's saved battle plus the turns after it - start
  the game for everyone, bring back a player who rejoins and repair a
  desync. Reports from before a snapshot are told apart by an epoch number.
- **A dedicated server** builds no battle: the leader's game builds it and
  sends it up (`start()` on the leader), the server loads it and sends it
  to everyone - the leader too, so every machine starts from the very same
  bytes.
- `setFilter`: the host decides which orders it takes.
- `phase()`: idle, loading, running, waiting, paused, ended; `stats()`.

### Legion on the lockstep (`game/src/net_world.hpp`, `net_game.hpp`)

- **Orders** are `Command`s in their save-game bytes. Every machine prepares
  an order the same way before the battle gets it (`prepareOrder`): it is
  given its sender's side (the **seats** in the snapshot's description),
  only player orders pass (`orderAllowed`: moving, fighting, building,
  producing, organising - no spawning, editing, credits, teams, weather),
  and only the sender's own units take it. The host filters too. Nobody can
  order another side's units or conjure an army, whatever their game sends.
- **Snapshots** are save games in memory (`SaveFile`: description with the
  seats, map, state - about 110 KB for a fresh 128 x 128 skirmish), the
  format of the game's own saves; the game, the server and the tests read
  each other's.
- `NetWorld`: the battle without graphics (dedicated server, tests).
- The game's adapter runs the scene: `GameScene::simTick()` (the tick and
  what follows it: events, effects, sounds) is what the lockstep steps;
  every order a player gives goes through `GameScene::issue()`.
- `NetGame`: hosting (port 47800 by default), joining by address or from
  the LAN list, the lobby (the skirmish setup in the session's settings,
  each member's side and army in its data), starting (the leader's game
  builds the battle with `startSkirmish`, names the sides after their
  players), the in-battle overlay (waiting, pause, reconnecting, chat lines,
  ping) and Enter for chat.
- In a multiplayer battle: no saving, loading or restarting; no game speed;
  no editor; console commands that would change the battle say no;
  replays record as always.
- `LegionServer` (`server/main.cpp`): `--port`, `--name`, `--password`,
  `--max`, `--mods`, `--content`, `--no-lan`, `--quit-after`, `--verbose`.
  Loads the rules with the mods like the game (`loadNetRules`), hosts,
  announces itself, referees, and goes back to the lobby when everyone has
  left.

### Console and command line

`net.status`, `net.host [port]`, `net.join <address>`, `net.leave`,
`net.say <text>`; `--host [port]`, `--join address`, `--name NAME`,
`--net-selftest [dedicated]`.

### Not done on purpose

- No NAT punch-through or relay service: over the internet the host
  forwards a port (or players use a VPN such as Tailscale); LAN and direct
  IP work as they are.
- No encryption or accounts: it is for LAN parties and friends' servers.
- IPv4 only.
- No joining a game in progress as a new player (rejoining your own place
  works); no spectators beyond players who choose "watching" in the lobby.
- A player who leaves for good leaves its army standing idle (handing it to
  the computer is a gameplay decision for later).
- Skirmish only: campaign missions and scenarios with scripts are not
  offered in the lobby yet (the lockstep would carry them: the snapshot is
  a full save with the script).
- A desync is repaired by sending the host's battle, not prevented: the
  digest names the system to look at.

### Tests

`tests/test_net.cpp` (10: addresses; reliable messages once and in order
through a line losing a fifth of its datagrams, with a 150 KB message both
ways; unreliable ones lost but never repeated; refused, no answer, the
cable out, closed politely, another game's datagrams ignored; real UDP on
this computer; LAN beacons found and forgotten, another game's ignored; a
lobby with names, settings, chat, version and mod refusals, leaving and
kicking; password, full, started; a lost player back in its own place, and
gone when away too long; a dedicated server's leader),
`tests/test_lockstep.cpp` (7: three machines the same through a bad line;
one tick of delay on a LAN; a slow player holds everyone, a lost one
doesn't and comes back; a desync found, named and repaired; pause; a
dedicated server taking the leader's battle; **a Legion battle between two
players over a lossy line**, cheating orders dropped on every machine, a
broken machine's desync named (`ECS`) and repaired). The game:
`--net-selftest` (the scene hosts, a headless second player joins over real
UDP, chat, sides, start, orders from both, a cheat dropped, a pause,
battles compared tick for tick) and `--net-selftest dedicated` (the same
through a dedicated server in the process, the scene leading). ctest
`server_smoke` starts and stops `LegionServer`.

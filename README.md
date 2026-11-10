# Legion Classic

**A 2.5D isometric real-time strategy game set in a war that never ended: Europe, 1946.**

Legion Classic plays like the RTS games of the late 1990s and looks like
the classics of the era: build a base, run supply trucks, train fireteams and tank platoons,
and dig in behind trenches, wire and tank traps. It runs on its own engine,
**Slave Engine**, and has a scenario editor you can open in the middle of a
battle and change anything, the rules included.

![A German and a Soviet force meet on a river crossing](docs/images/battle.jpg)

> **Status: in development (0.9.1).** Milestones 1–9 and 9.1 are done: three armies,
> skirmish against the computer, a tutorial and three short campaigns, sound
> and an orchestral score, the Live Scenario Editor, the tools for making
> mods, and a real 3D battlefield with a free camera. Next: terrain 2.0 (9.2), then intel, armour,
> economy, AI, towns, campaigns and big scenarios in small milestones; tuning
> and debugging always come last. The game is built together
> with Claude (Anthropic's AI); see [How it is made](#how-it-is-made).

## Features

- **Three armies, Germany, the Soviet Union and Britain,** each with a full
  roster: riflemen, MG and anti-tank teams, snipers, engineers, scout cars,
  half-tracks and carriers, light, medium and heavy tanks, tank destroyers,
  howitzers, rocket trucks and self-propelled guns.
- **A tutorial and three campaigns.** Four lessons to learn the game, then
  three missions each for Germany, the Soviet Union and Britain, with
  briefings, and what you achieve carrying over from mission to mission.
- **Sound that follows the camera, and an orchestral score.** A fight on
  screen is loud and sharp; one across the map is a distant rumble. Nine
  pieces of music, written for the game, follow the fighting near you.
- **Real formations.** Men fight as fireteams, squads, platoons and companies;
  tanks and infantry bound together become battle groups. Each unit carries a
  NATO card with its strength.
- **Cover that matters.** Garrison houses and bunkers, put three men in each
  piece of trench, and hit tanks in the side or rear.
- **Intel instead of fog.** The whole map is always in view; enemy forces
  out of sight show as chips sized by their strength, buildings as counters
  until scouted. Scouts see twice as far and keep their distance.
- **A computer player with a doctrine per side.** The Germans fortify in depth
  and attack with combined arms; the Soviets mass infantry and artillery and
  stream reinforcements in; the British dig in, bring up the guns and attack
  in set pieces. All of them raid, regroup and expand.
- **Twelve landscapes** in five climates (temperate, winter, desert, tropical,
  urban), with river valleys, lakes, ridges, marshes and towns; a ready-made
  skirmish map for each, or a new random map from any seed.
- **Live Scenario Editor (F2).** Sculpt terrain, place units for any side,
  call artillery, and write triggers and objectives while the battle runs.
- **Creation tools and mods.** A rules editor changes any unit, building or
  weapon and the battle follows as you type. Save it as a mod and switch it on
  from the main menu.
- **Saves and replays.** The simulation is deterministic, so a replay is just
  the list of orders given.

| | |
| --- | --- |
| ![A German base in winter](docs/images/winter-base.jpg) | ![The Live Scenario Editor](docs/images/live-editor.jpg) |
| ![The rules editor over a running battle](docs/images/rules-editor.jpg) | |

## Play it

Windows builds are made automatically from every change:

1. Open [**Actions → Build**](../../actions/workflows/build.yml) and click the
   latest green run.
2. Download **LegionClassic-Windows** under *Artifacts* (you need to be signed
   in to GitHub), unzip it and run `LegionClassic.exe`. Nothing to install.

Windows may warn that the game isn't signed: choose **More info → Run anyway**.
How to play, the controls and the editor are in
[docs/PLAYING.md](docs/PLAYING.md).

## Make mods

Mods are folders of rule changes, sprites and scenarios that sit on top of the
game without changing it. Everything about them, and the two creation tools,
is in [docs/MODDING.md](docs/MODDING.md). Replacing art by hand is in
[docs/ART_PIPELINE.md](docs/ART_PIPELINE.md).

## Build it yourself

You need CMake 3.24+ and Visual Studio 2022 (Windows) or GCC 13 / Clang 16
(Linux; plus the X11 / Wayland development packages SDL needs). Libraries are
downloaded automatically at pinned versions.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release      # unit tests
build/bin/LegionClassic                # run the game
```

Headless self-tests drive the real game: `--flow-selftest` (skirmish, save,
load, replay), `--editor-selftest` and `--mods-selftest`.

## How it is built

| Folder | What it holds |
| --- | --- |
| `engine/` | Slave Engine: the deterministic simulation (`sim/`, `core/`, `ecs/`, `map/`) and presentation (`render/`, `assets/`, `platform/`) |
| `game/` | The game itself: battle scene, sidebar, unit cards, menus, encyclopedia |
| `editor/` | The Live Scenario Editor and the rules editor |
| `content/` | All game data: unit, building and weapon rules, scenarios, art overrides |
| `mods/` | Mods that ship with the game (an example) |
| `tests/` | Unit tests (doctest), including determinism, combat, maps and mods |
| `tools/` | Build-time tools (`make_scenarios.cpp` writes the bundled scenario) |
| `docs/` | Player guide, modding guide, art pipeline and the design document |

- **Deterministic simulation:** integer and fixed-point maths only, seeded
  random numbers, 15 ticks a second. The same orders always give the same
  battle (tested), which makes replays possible and multiplayer later.
- **Everything is data:** units, weapons and armour are defined in
  `content/rules/*.json`, not in code.
- **One way to change the world:** players, the editor and the computer
  players all send commands to the simulation.

The plan, the milestones and the decisions behind them are in
[docs/design/DESIGN.md](docs/design/DESIGN.md); what changed in each build is
in [CHANGELOG.md](CHANGELOG.md).

## How it is made

Legion Classic is designed and play-tested by
[makorain22](https://github.com/makorain22) and written with Claude, Anthropic's
AI model, working in Claude Code. Each milestone is planned together, built,
tested headless in the cloud, then played on Windows; the playtest notes become
the next round of work. Commits made this way are co-authored by Claude.

## Licence

Copyright © 2026 makorain22. All rights reserved for now: you're welcome to
read the code, build it and play, but please ask before reusing it. An open
licence may follow once the game reaches 1.0. The third-party libraries below
keep their own licences.

## Third-party code

SDL 3 (zlib), Dear ImGui (MIT), nlohmann/json (MIT), stb and stb_vorbis
(public domain / MIT), miniz (MIT), doctest (MIT), and a glad-generated OpenGL
loader (public domain / CC0). The music is played by the FluidR3 GM soundfont
(MIT); the sound effects are synthesised for the game (CC0). See
[content/audio/LICENSES.md](content/audio/LICENSES.md).

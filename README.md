# Legion Classic

A 2.5D isometric real-time strategy game in the spirit of the 1990s classics,
set in an alternate-history 1940s–1950s war, built on its own engine,
**Slave Engine 1.0**, with a Live Scenario Editor built in.

**Status:** Milestones 6 and 7 of 10 (computer player, scenario scripting,
saves and replays; the full German and Soviet rosters in detailed voxel art,
five biomes, and a PNG art pipeline). Next: Milestone 8, creation tools. See
[`docs/design/DESIGN.md`](docs/design/DESIGN.md) for the full plan,
[`docs/PLAYING.md`](docs/PLAYING.md) for how to play and
[`docs/ART_PIPELINE.md`](docs/ART_PIPELINE.md) for replacing art.

## Getting a playable build

Every push to `main` builds a Windows version automatically:

1. Open the repository's **Actions** tab on GitHub.
2. Click the latest green **Build** run.
3. Download **LegionClassic-Windows** under *Artifacts*, unzip it, and run
   `LegionClassic.exe`.

## Building it yourself

Requirements: CMake 3.24+, and Visual Studio 2022 (Windows) or GCC 13 / Clang 16
(Linux). Third-party libraries are downloaded automatically at pinned versions.

```bash
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release      # unit tests
build/bin/LegionClassic                # run the game
```

## Project layout

| Folder | What it holds |
| --- | --- |
| `engine/` | Slave Engine: deterministic simulation (`sim/`, `core/`, `ecs/`, `map/`) and presentation (`render/`, `assets/`, `platform/`) |
| `game/` | Legion Classic itself: scenes, HUD, game rules glue |
| `editor/` | The built-in map editor (F2): tools, panels, undo history |
| `content/` | All game data: unit and weapon rules, bundled scenarios, sprite overrides. Mods copy this layout |
| `tests/` | Unit tests (doctest), including the determinism test |
| `tools/` | Build-time scripts and tools (`make_scenarios.cpp` writes the bundled scenarios) |
| `external/` | Checked-in generated code (OpenGL loader) |
| `docs/` | Design doc, player notes, engine notes |

## Engine principles

- **Deterministic simulation:** integer and fixed-point maths only, seeded
  random numbers, 15 ticks per second. The same commands always give the same
  result (tested), which enables replays and multiplayer later.
- **Everything is data:** units are defined in `content/rules/*.json`, not in code.
- **One path for changes:** players, the editor and the AI all change the world
  only by sending commands to the simulation.

## Third-party code

SDL 3 (zlib), Dear ImGui (MIT), nlohmann/json (MIT), stb (public domain / MIT), miniz (MIT),
doctest (MIT), glad-generated OpenGL loader (public domain / CC0).

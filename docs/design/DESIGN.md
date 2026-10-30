# Legion Classic – design summary

The full, living design doc is kept as a Claude doc ("Legion Classic – Design
Doc"). This file is the in-repo summary so every coding session has the
essentials. Keep both in sync when decisions change.

## Locked decisions (30 Oct 2026)

| Topic | Decision |
| --- | --- |
| Engine | Custom C++20 engine, SDL3 + OpenGL 3.3 core, Dear ImGui for editor UI |
| Target | Windows 10/11 64-bit PC; Linux possible later |
| Setting | Alternate-history 1940s–1950s |
| Factions | Germany and Russia first, United Kingdom third |
| Art | Gritty, realistic, Blitzkrieg-inspired; voxel-baked sprites (supersampled), replaceable by hand-drawn PNGs |
| Facings | 16 visible facings, animations at 8–10 fps (heavy retro feel) |
| Sidebar | Classic right sidebar, minimap on top |
| Language | English |
| Names | Game: Legion Classic. Engine: Slave Engine 1.0 |
| Code | Private GitHub repo `makorain22/legion-classic`; Windows builds via GitHub Actions |

## Top priority: the Live Scenario Editor

Like Stronghold's custom scenario editor: the battle keeps running while you
edit it. Place / delete / move anything for any player mid-battle, spawn
brushes (up to 10,000 units), possess any player, live inspector, god tools,
snapshot to scenario, and turn any sandbox into a mission with triggers.

## Art direction

Gritty and realistic, in the spirit of Blitzkrieg: painted, blended terrain
with no visible grid; muted olive, brown and grey palette; soft shadows; fine,
weathered units with camouflage and grime and no outlines; lasting battle
damage (craters, scorch marks, wrecks, smoke); summer, autumn and winter
biomes. Terrain follows this from Milestone 2; units and buildings move to it
in the art milestone.

## Core technical rules

1. The simulation is deterministic: integers and fixed point only, seeded RNG,
   fixed 15 ticks per second. `Simulation::stateHash()` must match across runs.
2. 1 tile = 256 leptons. Angles are 0–4095; 16 facings of 256 each.
3. Tiles are 64 × 32 diamonds. Heights are stored at tile corners, 64 units per tile of height. Ground layers are a splat map at 2 × 2 texels per tile.
4. All world changes go through `Command`s queued to the simulation.
5. Game data lives in `content/` as JSON; no unit names in engine code.
6. Rendering only reads the simulation; it never changes it.

## Milestones

1. **Foundations** – done: project, CI, loop, camera, voxel-baked sprites,
   selection, group moves, 10,000-unit stress test.
2. **Terrain and maps** – done: height mesh, blended ground layers, water,
   random countryside, .lmap files, F2 editor with terrain tools and undo.
3. Units and combat: pathfinding on huge maps, weapons, fog of war, mass performance.
4. **Live Scenario Editor** (first main deliverable).
5. Economy and base building.
6. AI and scenario scripting (triggers, objectives), saves and replays.
7. Art pipeline and content: Germany and Russia, 5 biomes.
8. Creation tools: rules editor, sprite studio, mods.
9. UK faction, tutorial, campaigns (built with the editor), sound and music.
10. Release 1.0: balance, performance, documentation, packaging.

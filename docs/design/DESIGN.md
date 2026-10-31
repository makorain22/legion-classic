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

## Economy (decided in Milestone 5)

- One resource, **supplies**, from **oil** fields. Supply trucks carry 100 oil
  (worth $500) from field to Oil Depot. Fields hold up to 255 oil per tile.
- **Power** from Power Stations; a deficit halves production speed (WW2
  defences don't need electricity, so they keep firing).
- Construction Yard builds structures (placed within build range of the
  side's own structures); Barracks trains infantry; Tank Works builds
  vehicles, supply trucks and construction trucks.
- Fortifications are bought tile by tile and placed at once.

## Balance direction (from playtesting, 30 Oct 2026)

Blitzkrieg-like: slow, deadly, combined arms.
- Infantry are inexperienced: rifles hit 70 % of the time. They can't hurt
  tanks; dedicated anti-tank units come later.
- Tanks outrange everything, kill infantry with HE and vehicles with AP, and
  will be expensive (costs arrive with the economy in Milestone 5).
- Armour has front / side / rear values; a rear hit can kill a tank outright.
- Units see less far than tanks can shoot; scouts spot for the guns.
- Unit scale: true to life (soldier a third of a tile tall, medium tank a
  little over a tile long) so battles read as big.
- Milestone 7 roster: AT teams hunt tanks from cover; engineers repair;
  snipers out-range rifles; tank destroyers out-range tanks but have no
  turret; heavy tanks need an oil depot and a watchtower; artillery needs a
  watchtower (spotters) and out-ranges everything.

## Art direction

Gritty and realistic, in the spirit of Blitzkrieg: painted, blended terrain
with no visible grid; muted olive, brown and grey palette; soft shadows; fine,
weathered units with camouflage and grime and no outlines; lasting battle
damage (craters, scorch marks, wrecks, smoke). Five biomes: temperate,
winter, desert, tropical, urban. Since Milestone 7, units and buildings are
detailed voxel models baked with ambient occlusion, camouflage and grime (see
`docs/ART_PIPELINE.md`); any sprite can be replaced by a PNG. Germany uses
only the plain cross, Russia the red star.

## Core technical rules

1. The simulation is deterministic: integers and fixed point only, seeded RNG,
   fixed 15 ticks per second. `Simulation::stateHash()` must match across runs.
2. 1 tile = 256 leptons. Angles are 0–4095; 16 facings of 256 each.
3. Tiles are 64 × 32 diamonds. Heights are stored at tile corners, 64 units per tile of height. Ground layers are a splat map at 2 × 2 texels per tile.
4. All world changes go through `Command`s queued to the simulation.
5. Game data lives in `content/` as JSON; no unit names in engine code.
6. Rendering only reads the simulation; it never changes it. The simulation
   reports shots, impacts and deaths as events; effects are drawn from those
   and may use their own random numbers.
7. Each tick runs: commands, path requests (budgeted), orders, movement,
   spatial hash, separation, vision (every 4 ticks), targeting (staggered),
   weapons, projectiles, removal of the dead.

## Milestones

1. **Foundations** – done: project, CI, loop, camera, voxel-baked sprites,
   selection, group moves, 10,000-unit stress test.
2. **Terrain and maps** – done: height mesh, blended ground layers, water,
   random countryside, .lmap files, F2 editor with terrain tools and undo.
3. **Units and combat** – done: region-checked A* with shared group routes and
   a per-tick search budget, collision via spatial hash, data-driven weapons /
   warheads / armour, turrets, shells with splash, attack and attack-move,
   per-side fog of war, battle effects, 10,000-unit battles at ~13 ms a tick.
4. **Live Scenario Editor** – done: unit palette (one / brush / block up to
   10,000), select and edit any side's units, inspector, sides and teams,
   possession, artillery / smite / heal, snapshots, shared terrain + unit undo.
   Triggers and objectives come with scenario scripting in Milestone 6.
5. **Economy and base building** – done: oil and supply trucks, power,
   structures on the grid, four production lines, sidebar with minimap,
   fortifications (trenches with cover, sandbags, wire, tank traps), stances,
   formations with right-drag fronts, unit scale pass.
6. **AI, scripting, saves and replays** – done: main menu and skirmish setup,
   a computer player inside the simulation tick (easy / normal / hard),
   triggers with areas, conditions, actions and objectives (Script tab),
   full-state saves, command-log replays.
7. **Art pipeline and content** – done: full German and Soviet rosters (MG and
   AT teams, engineers that repair, snipers, half-tracks, light / medium /
   heavy tanks, tank destroyers, howitzer and salvo rockets), detailed voxel
   models at true scale with camouflage, grime and ambient occlusion, five
   biomes with their own ground, trees, light and water, neutral villages and
   towns, PNG export / override pipeline, a bundled scenario.
   **7.5 Playtest rework** – done: rebindable keys with WASD, screen lock, FPS
   limiter, minimap zoom; supply points and depots instead of oil; cheap
   infantry; fireteams to companies (and armoured platoons to battalions)
   with NATO cards; garrisons in buildings, bunkers and trench lines; small
   arms that chip armour; fog chips.
8. Creation tools: rules editor, sprite studio, mods.
9. UK faction, tutorial, campaigns (built with the editor), sound and music.
10. Release 1.0: balance, performance, documentation, packaging.

## Playtest backlog (31 Oct 2026, after Milestones 6–7)

Taken on in 0.7.5 (hard to undo): key layout and rebinding, supply points,
unit organisation, garrisons, infantry against armour, fog chips, editor
palette headings, placement grid, the quick fixes.

Taken on in 0.7.6: the Blitzkrieg art pack (personal use), the sidebar
redesign, the encyclopedia, the Script tab, the art pass (palette, colour
grading and vignette, long tracers, trenches over slopes, lasting corpses and
wrecks) and more content (MG nests, howitzer emplacements, six village
buildings, five kinds of map decoration). Buildings per climate come with the
art pack's winter and desert variants.

Still open: building designs per climate for the built-in voxel art.

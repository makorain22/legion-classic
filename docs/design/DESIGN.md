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
| Factions | Germany, Russia and the United Kingdom (`gbr`, since 0.9.0) |
| Art | Gritty, realistic, Blitzkrieg-inspired; voxel-baked sprites (supersampled), replaceable by hand-drawn PNGs |
| Facings | 16 visible facings, animations at 8–10 fps (heavy retro feel) |
| Sidebar | Classic right sidebar, minimap on top |
| Language | English |
| Names | Game: Legion Classic. Engine: Pioneer Engine 2.0 |
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
- Armour has front / side / rear values (since 9.4: side 1.5x, rear 2.5x;
  a medium tank has half a heavy's health, a light tank a quarter, and only
  heavy guns pierce a heavy tank's front in full).
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
damage (craters, scorch marks, wrecks, smoke). Since 0.9.1 the battlefield is
real 3D: the same voxel models drawn as meshes under a sun with shadows,
seen from any angle (the classic baked-sprite view is still a setting). Five biomes: temperate,
winter, desert, tropical, urban. Since Milestone 7, units and buildings are
detailed voxel models baked with ambient occlusion, camouflage and grime (see
`docs/ART_PIPELINE.md`); any sprite can be replaced by a PNG. Germany uses
only the plain cross, Russia the red star, Britain the white-red-white
recognition flash on vehicles and a roundel in the interface.

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
8. **Creation tools** – done (0.8.0): content layers with mods (rules merged
   by id, art overrides, scenarios; load order with requirements; a mod picker
   on the main menu), a rules editor with live changes into the running battle
   (type and weapon numbers kept stable, deleted types kept hidden until the
   next battle), a sprite studio (removed in 0.9.1 with the move to 3D),
   saves and replays that record their
   mods and a rules fingerprint. See docs/MODDING.md.
9. **Britain, sound, tutorial, campaigns** – done (0.9.0):
   - Britain (`gbr`): 14 units, the base under British names, 11 new weapons,
     a methodical artillery doctrine for the computer, the UI theme and
     roundel, Brodie helmets and the recognition flash on the placeholder art
     (German hull shapes until British voxel models are made); saves
     renumbered by unit id.
   - Sound and music, built for immersion and size: a software mixer in the
     engine where sound follows the camera (fade, muffle and echo with
     distance; the far-off front rumbles when the fighting is out of view), a
     large-hall reverb, synthesised effects and ambience, nine orchestral
     pieces that follow the fighting near the camera.
   - Tutorial: four lessons (orders, cover and armour, digging in, base and
     supply) with hints, highlighted buttons and locked build options.
   - Campaigns: a campaign format with Markdown briefings and flags carried
     between missions; three missions each for Germany, the Soviet Union and
     Britain; progress kept per player.
   - Twelve ready-made skirmish maps, one per landscape.
   **Roadmap after Milestone 9** (8 Nov 2026, small milestones by choice;
   tuning and debugging is always the last, new milestones go in before it):
   - **9.1 3D renderer and free camera** (done, 0.9.1) – the voxel models
     drawn as real 3D meshes with depth and shadows; a camera that turns to
     any angle, tilts and zooms; everything ported from the isometric view.
     How it works: the sprite baker remembers the voxel model behind every
     sprite (`AtlasFrame::voxel`), so the 3D view draws the model wherever the
     classic view would draw the sprite. Models are greedy-meshed on first use
     (`voxel_mesh.cpp`) into one vertex buffer and drawn instanced
     (`MeshRenderer`) with the baker's shading worked out in the shader. The
     sun's shadow map is fitted to the ground in view each frame. In 3D the
     camera's "screen-world" plane is the screen itself, so the 2D overlays
     (bars, cards, brushes) are placed at projected points unchanged. Effects
     live on the flat classic plane plus a height, so the same particles are
     drawn in either view. The classic 2D view stays behind a setting.
   - **9.2 Terrain 2.0** (done, 0.9.1): per-tile speeds (`TileInfo::speed`,
     `Map::speedPercent`) in movement and path costs; the drawn ground is a
     clamped Catmull-Rom surface (`Map::heightAt`; the generator keeps
     `heightAtLinear`); rock faces (`rock_faces.cpp`) on tiles of slope ≥ 41;
     ground blending in the model shader; `Simulation::levelPlot`; graded
     roads, cut banks, escarpments and straight bridges in the generator.
     Originally: – finer ground, cliffs and overhangs, ground that
     blends round buildings, roads and wrecks; buildings level their plot and
     blend into the slope; graded roads and proper banks; bridges only between
     high banks, straight and seamless; fords crossable at half speed;
     movement that feels slopes, fords and rough ground.
   - **9.3 Intel instead of fog** (done, 0.9.1): terrain fog off;
     `GameScene::intelAt` (vision only), `structureKnown` / `spotted_` (enemy
     buildings remembered once seen, rebuilt from explored ground on load or
     side change); chips in `intel.cpp` (8-tile buckets for units, 16 for
     buildings, identified within 2x sight); card stacks in `unit_cards.cpp`;
     `UnitTypeDef::scout` (NATO "recon") and `Simulation::stepScouts`
     (`scouts.cpp`, every 6 ticks, idle scouts only). Originally: – the map is always visible, but enemy
     units and buildings show only as cards until spotted (as in R.U.S.E.);
     nearby cards merge into one stack; scouts see twice as far and fall back
     on their own when the enemy gets close.
   - **9.4 Armour and artillery** – done (10 Nov): the health scale (heavy
     640–720, medium half, light a quarter; table in the changelog); a fourth
     vehicle armour class (`soft` / `light` / `medium` / `heavy`,
     `ArmorClass` in `weapons.hpp`); three AP warheads (`ap_light`, `ap`,
     `ap_heavy`) so only heavy guns pierce heavy armour in full; shell blast
     measured from a vehicle's hull (`impact()` in `combat.cpp`), which
     ended most "aimed" shells doing nothing; side 1.5x / rear 2.5x;
     artillery 24–38 tiles; reverse gear (`reverseReach`, `wouldReverse`,
     stateless test in `stepMovement`: waypoint more than 112° off the bow
     and the path no longer than 5 tiles, 12 in contact).
   - **9.5 Formations** – done (11 Nov; the user's answer to "what feels
     wrong": shape breaks on the move, spacing and shapes, facing, controls).
     A group move creates a `CFormation` (`engine/src/sim/formation.cpp`): a
     frame that travels the route of the group's centre at the pace of its
     slowest joined member, wheeling at most 40/4096 of a turn a tick; each
     member (`CFormationMember`: offset, final goal, shape, heading) is sent
     to its slot every tick (straight line when clear, else a path). Lag
     slows the frame (full pace under 1 tile, halt at 3); stragglers beyond 7
     tiles are dropped until within 2. Slots on impassable ground are pulled
     in (offset halved twice). A Column is not a stiff frame: slots lie on
     the route itself (`routePoint`), so it snakes. Attack-move: the frame
     halts while any member has a target in range and pursues up to 8 tiles
     off its route. `COrder::faceTo` survives arrival as the post facing.
     Spacing: men 150 leptons; vehicles 3 radii abreast, 4 nose to tail.
     Saves are SIM5 (SIM4 and older still load).
   - **9.6 Economy** – done (11 Nov), `engine/src/sim/logistics.cpp`. The
     map's `supplyPoints` are logistics points (generator: `(W+H)/80` of
     them, 2..12, at least 30% of the map's short side from every home and
     22% apart, mirrored pairs, own RNG stream). `stepPoints` (every 3
     ticks): armed non-structure units within 3.5 tiles hold a point for
     their side when no enemy is there; 12 s (`kCaptureTicks`) raises the
     faction's `role: office` structure (anyone standing in its footprint is
     moved aside first), which spawns its `freeUnit` x `convoy` trucks.
     `stepConvoys` runs `CHarvester` (Seek, ToOffice, Loading 12 s, ToYard,
     Unloading 2 s; pays the truck's `capacity`). Rules: `perYard`
     (`roomFor`, checked in `canProduce` and on placing), `aura {radius,
     fireRate}` (`fireRateBonus`, applied to weapon cooldowns with a
     dithered rounding), builtAt `post` / `warehouse`. Events
     `PointCaptured`, `ConvoyArrived`. Saves are SIM6 (point states; the
     AI's capture party). The design as the user gave it: rare logistics points far from the bases: capture one
     and a logistics office sends three-truck convoys of money to your HQ
     (office and trucks can be destroyed); Post Office (small income, cheap
     levy infantry: Recruits, Volkssturm, Conscripts), Logistics Warehouse
     (middling income, supply trucks, an aura that speeds friendly fire),
     Strategic Fuel Depot (the most income, one per construction yard).
   - **9.7 AI** – done (11 Nov), `engine/src/sim/ai.cpp`. `AiPlayer::
     mainEnemy` is the nearest hostile side still standing (kept until it has
     nothing left; `toward` and the main force's targets follow it; the
     building it is going for stays the target until it is down);
     `raidEnemy` rotates through the other sides, one per raiding party
     (`raids`). Raiders (5-6, bound; mixed vehicle kinds now bind into one
     platoon) score offices, then income buildings, and attack supply trucks
     in sight. Binding: `bindGroups` splits a force into battle groups of
     about sixty (men and vehicles dealt out from flank to flank),
     `keepBound` rebinds when 15-30% stand outside a proper unit,
     `orderUnits` gives each bound unit its own formation on a front across
     the way to the target, `frontOf` is the middle of the half nearest the
     enemy. One scout (`scout`, `scoutPost`) moves between four watch points
     every 75 s. Production: `Simulation::lineSpeed` - every further
     building of the kind that builds a thing speeds its line by half (3x
     with five); the AI adds works and barracks when it has over 5000 to
     spare. Saves are SIM7. `--bench N` (with `--ai-battle`, `--sides`,
     `--ffa`, `--factions`) plays a battle without drawing and prints what
     each side did: the tool for 9.13's balance runs. The design as the user
     gave it: in free-for-all it presses the nearest enemy and raids the
     others; it binds most of its army into battle groups; it plays the new
     economy.
   - **9.8 Towns and villages** (done 12 Nov) – compact settlements: one-tile
     row houses (`neutral_row*`, lot kinds `Map::kRowLot`..+4, joined
     models by neighbour mask; every row style joins every other) in rows of
     two to five along the street with alleys between, lanes off the street
     to the church (on a green) and the inn, blocks round a yard with one way
     in, a well and a farm; the market town round the real crossing of the
     two roads (square, church and town hall facing, inn and market, school,
     station, warehouse, blocks packed from the middle out). Timber country
     (winter without a town, tropical) builds apart. Laid with its own random
     stream after the terrain, so the rest of the map is unchanged; each
     group of touching buildings is levelled to its mean height, neighbours
     within 8 of each other, the ground eased in (9 per corner); lanes and
     fronts paved. `Map::lotFootprint` is the shared footprint table.
     `generate(..., settlements)`: missions placed by hand start from open
     country. Urban maps keep their grid.
   - **9.9 The mission engine** (done 13 Nov) – inserted before the
     campaigns at the user's request ("engine work first"), with more than
     the missions strictly need, all of it in the scenario editor for modders.
     Sim: `CScripted` (name index + ScriptFlag bits: invulnerable, kept from
     the computer, untargetable, locked); per-player play area (`TileRect`,
     empty = all; human orders clamped, building refused outside, camera
     bounds, ground dimmed through the old fog shader); countdowns, markers,
     counters, kills/losses, income rate, cinematic flag, mission difficulty
     (spawn scale 70/100/135), patrols; 21 new conditions (33) and 40 new
     actions (62), `negate` per condition, `any` per trigger, `scaled` per
     spawn; `Command::text` + `TagUnits` (map placements tagged in the same
     tick as they spawn, by spawn tag and index). Computer player: ignores
     script-held units, `order` attack/defend a point (attack launches at
     once with what it has), behaviour bits (build, train, attack, raid,
     capture, fortify, scout, repair), wave scale; a side with no yard keeps
     no home guard. Saves SIM8 (mission block at the end), save files v2
     (commands carry text). UnitPlacement name/flags/stance. Maps of any
     width and height (editor *New...*, `Builder::startRect`, `findSeedRect`).
     Presentation (game/src/mission_ui.cpp): radio panel and log (L),
     countdown clocks, letterbox, camera glides, ground markers with edge
     arrows, minimap rings and dimming, script music (`GameAudio::scriptMusic`).
     Editor: all conditions and actions in grouped menus, NOT / ANY, units by
     name, inspector Mission box, five story templates, mission debugger.
     Campaigns: difficulty on the briefing page (enemy computers one step
     easier/harder), `act` and `formation` per mission. Showcase: *The
     Crossing at Dawn* (96x224). Tests: test_mission.cpp + the showcase played
     through phase by phase.
   - **9.10 Campaign: Germany** (done 13 Nov) – *The Long Road Home*, 25
     missions in five acts (Walküre, The Desert, The Don, Deep Territory, The
     Regency Falls), the main canon, four rotating formations. Built from code:
     `tools/mission_kit.hpp` (Builder for terrain, sides, set pieces; Script
     helpers for conditions, actions, objectives numbered in add order) and
     one `tools/campaign_ger_act*.cpp` per act. Carry-over flags: tiger_survived,
     brenner_men, africa_veterans, mutineers, refinery_intact, defected,
     elbe_bridges (plus oster_escaped, bir_sidra_dumps, airfield_intact,
     ice_road_stores). Eight landmark buildings (landmark_models.cpp). Tests:
     test_campaign_ger.cpp (campaign.json, every mission's script and opening,
     a bot plays all 25 to victory; tests/mission_battle.hpp shared with
     test_campaign.cpp). Deferred: coalition markings on the models (team
     colour tells the sides apart for now).
   - **9.11 Campaign: Soviet Union** (done 13 Nov) – *The Road West*, 25
     missions in five acts (The Guards, The Ring on the Don, The Dnieper,
     Through Poland, Berlin), a what-if that splits from the canon at the
     Morozovsk pocket (Feb 1947): the ring holds, Adler is taken, the Red Army
     reaches Berlin first and meets the West on the Elbe. Formations: 5th Guards
     Rifle Division (Gromov), 17th Guards Tank Brigade (Zharkov), partisan
     brigade "Za Rodinu" (Orlova), 61st Assault Engineer Brigade (Nazarov).
     Engine: river ice (`Map::setIce`, saved in .lmap; foot 70%, wheels 60%,
     tracks barred; shells with splash >= 1 tile break it and drown what
     stands there; actions freeze_water, break_ice); the brick fort landmark.
     tools/campaign_rus.hpp + campaign_rus_act1..5.cpp. Flags: guards_tanks,
     bryansk_partisans, chir_crossing, ice_road_cut, adler_captured,
     relief_stopped, kirovsk_guns, mius_breach, dam_saved, pripet_paths,
     vistula_held, rising_saved, narew_tanks, posen_guns, oder_bridgehead,
     free_germans. Old r1-r3 rebuilt as r01, r02, r10 (progress carries over).
   - **9.12 Campaign: Britain** (done 13 Nov) – *Hold Fast*, 25 missions in five
     acts (Holding the Cities, The Desert, The North, Back to the Low
     Countries, Over the Border), canon, defensive; links to g05, g06, g08,
     g16, g19, g22, g24. Formations: 2nd Bn Wessex Rifles (Kerr), 7th Armoured
     Brigade (Ashworth), No. 15 Commando (Munro), SOE (Holloway). Engine:
     flooding (`Map::flood/drain`, depth 11, old heights kept in .lmap;
     actions flood_area, drain_area); coasts (`Builder::sea`, sand beaches);
     landmarks: landing craft, harbour crane, U-boat pen, coastal casemate,
     pumping station, lighthouse. tools/campaign_gbr.hpp +
     campaign_gbr_act1..5.cpp. Flags: antwerp_docks, nijmegen_bridge,
     walcheren_guns, ridge_guns, lantern, sonnenberg_friends, narvik_ore,
     danish_rising, arnhem_bridge, rotterdam_port. Old b1-b3 rebuilt as b06,
     b07, b18. An American campaign later.
   - Also 13 Nov: range circles for every unit (T), scouts see 44-48 tiles.
   - **Roadmap change (13 Nov, after playtest round 1):** the three campaigns
     are shelved (the user builds the campaigns himself with the Mission
     Maker and sends packs for finalisation), and the engine is upgraded
     first.
   - **9.13 Engine upgrade: the Pioneer Engine 2.0** (in progress) – the
     engine renamed from Slave Engine and rebuilt in nine parts, deliberately
     over-engineered ("whatever you plan to add, add more"): core, data,
     simulation, scripting, UI framework, renderer, assets and audio,
     networking groundwork, tools. Decisions: Lua yes; must run on old
     hardware (OpenGL 3.3 kept, ES 3.0 possible later, graphics presets and
     auto-detect); old replays may break; multiplayer groundwork with
     hosting, LAN, dedicated server and reconnect; glTF import and export;
     multithreading with a single-thread fallback. See docs/ENGINE.md.
     - **Part 1, core (done 13 Nov):** rename (namespace `pioneer`, targets
       `pioneer_core` / `pioneer` / `pioneer_tests`, macros `PNR_*`); job
       system (`core/jobs.hpp`: work-stealing workers, `parallelFor` with
       thread-independent chunks, dependencies, `TaskGraph`, inline mode for
       one thread); profiler (`core/profiler.hpp`: `PNR_PROFILE`, per-frame
       stats, Chrome trace capture); memory tracking (`core/memory.hpp`:
       tags, counting allocator, reporters); logging channels and levels,
       ring buffer and sinks (`core/log.hpp`); developer console
       (`core/console.hpp`: commands, typed variables with flags, aliases,
       binds, completion, config files, `wait`, queued lines) with the
       engine's own commands in `core/console_core.cpp`; ImGui console and
       profiler windows (`ui/dev_ui.hpp`); game commands and hooks in
       `game/src/dev_tools.cpp`. In the sim: `Simulation::tick` is marked
       step by step and each side's vision grid is a job
       (`Simulation::stepVision`, `Vision::prepare`). `--bench` reports
       where the ticks went. Keys: ` / F9 console, F8 profiler. Tests:
       tests/test_core.cpp (18 cases, including the same battle on 1, 3 and
       4 threads). Also `core/assert.hpp` (`PNR_ASSERT`, logged and counted;
       stops debug builds only) and `core/version.hpp`.
     - **Part 2, data (done 14 Nov):** virtual file system
       (`core/vfs.hpp`: folder, zip and memory mounts layered by priority,
       mount points, case-insensitive fallback in archives, safe paths, atomic
       writes to one write mount, `diskPath()` unpacking archives into a
       content-keyed cache for path-based code, `scan()` on a worker +
       `apply()` for changes); packs (`core/pack.hpp`: zip + `pack.json`
       manifest with per-file size/CRC and one checksum, `requires` with
       version ranges and `engine`, build / readManifest / verify / extract;
       hand-zipped mod folders accepted); `ContentLayers` rebuilt on the Vfs
       (`addPack`, `addMod`, `fileSystem()`), `scanMods` lists packs,
       `resolveLoadOrder` checks versions; asset registry (`core/assets.hpp`:
       stable ids, kind, source mount, layers, version, content hash,
       uses / usedBy from scanners run on the job system, `apply()` cascading
       to users, `Cache<T>` with preload on all cores and last-good-version
       reloads); hot reload (`core/hot_reload.hpp`: watches by prefix and
       extension, background scan, settle-before-dispatch); game hooks for
       rules (via `applyRules`), sounds, campaigns and sprite overrides;
       console commands `vfs.*`, `assets.*`, `content.*`, `pack.*`, `mod.*`;
       Mods page shows packs and packs a folder mod into
       `<user>/exports/`. Zip reading is in `engine/src/core/zip_file.*`
       (miniz). Tests: `tests/test_data.cpp`; `--mods-selftest` covers hot
       reload and a packed mod end to end.
     - **Part 3, simulation (done 14 Nov):** state saved as tagged chunks
       (`core/chunks.hpp`; `SIM9`, one section per system via
       `Simulation::saveSection` / `loadSection` / `resetSection`, plain
       pools self-describing with tag and item size on `PNR_POD_POOLS`;
       SIM2..SIM8 read by the same code); canonical bytes
       (`core/reflect.hpp`: `podMask`, `forEachField`; `ByteWriter` blanks
       padding); `Simulation::digest()` per section; determinism checker
       (`sim/determinism.hpp`: `HashTrace`, `twinCheck`; replays carry a
       checksum a second, save file version 3, checked in
       `GameScene::onTick`); pathfinder rebuilt on 16-tile sectors
       (`Pathfinder::buildSector` / `linkSector` / `buildRegions`,
       incremental by sector hash), guided A* (`guideTo`: distance to the
       goal's part over the part graph, slack 8 tiles, from 12 tiles),
       route cache keyed by start tile, goal and locomotor that charges the
       first search's work, flow fields (`flowField`, `flowPath`); vision
       2.0 (`Vision::kScouted`, ground layer `kWood` / `kWoodEdge` /
       `kSmoke`, `stampRays`; `Simulation::VisionRules` set by
       `CommandType::SetVisionRules`, `CommandType::Smoke`,
       `unitVisibleTo`, `concealed`, section `VIS2`); `CCondition`
       (suppression, morale, posture, flags, experience; pool `CCND`).
       Console `sim.digest`, `sim.twincheck`, `replay.check`, `path.stats`,
       `sim.vision`, `sim.smoke`. Tests: test_state.cpp, test_path.cpp,
       test_vision.cpp; fixture tests/data/sim8_three_sides.lsav.
   - **9.14 Mission Maker foundation**, **9.15 Cutscenes and event
     sequences**, **9.16 Set pieces**, **9.17 Map, assets and terrain**,
     **9.18 Combat and controls**, (campaign finalisation when the user's
     packs arrive), **9.19 Big scenarios** – large stand-alone battles
     inspired by great battles and long operations, with many units.
   - **9.20 Tuning and debugging** – always last: playtest, balance the armies
     and missions, performance, bugs, documentation.
   - Parked: air power (air strikes, an air force). Later: packaging and 1.0.

## Playtest backlog (31 Oct 2026, after Milestones 6–7)

Taken on in 0.7.5 (hard to undo): key layout and rebinding, supply points,
unit organisation, garrisons, infantry against armour, fog chips, editor
palette headings, placement grid, the quick fixes.

Taken on in 0.7.6 (the Blitzkrieg art pack was dropped from the plans on 7 Nov 2026; the importer stays in the code but nothing more is done with it): the Blitzkrieg art pack (personal use), the sidebar
redesign, the encyclopedia, the Script tab, the art pass (palette, colour
grading and vignette, long tracers, trenches over slopes, lasting corpses and
wrecks) and more content (MG nests, howitzer emplacements, six village
buildings, five kinds of map decoration). Buildings per climate come with the
art pack's winter and desert variants.

## Playtest backlog (1 Nov 2026, after 0.7.6)

Taken on in 0.7.7: trench garrisons (three men a piece, held until
evacuated); every unit selectable on its own, the whole unit through its
card; combined tank-and-infantry units and vehicle units named by role;
bigger cards with enemy cards (toggle); starting positions on opposite sides;
grid painting and civilian buildings as props in the editor; sandbag, wire
and fence lines that join up, a smooth pillbox and hedgehog tank traps;
fenced supply compounds; three more civilian buildings; bases laid out as
military posts; computer doctrines per side with raids, regrouping,
expansion and field works; faction colours for the interface.

Still open: building designs per climate for the built-in voxel art; balance
between the doctrines (in AI-against-AI games on hard the Soviets now edge
it); the computer fortifying its expansions as well as its main base.

## Sound and music (decided 7 Nov 2026)

- Immersion and grandness first. Sound follows the camera: what is in view is
  heard in full; further away it fades, loses its high end and gets wetter,
  so a fight across the map is a distant rumble rather than a quiet crackle.
  Zooming out lifts the ear. Big guns carry further than rifles (each sound's
  `range` in `content/audio/audio.json`).
- When the fighting is away from the camera, a far-off front (guns over the
  hills) fades in under the ambience in proportion to how much is going on.
- Music follows the fighting near the camera: calm pieces with silences
  between them, battle music when it flares up, back to calm after a lull of
  about twenty seconds; the menu theme, a victory fanfare and a lament.
- All sound is presentation only, never in the simulation, so it can't break
  determinism. Effects are synthesised (CC0); the music is composed in
  `tools/audio/music.py` and played by the FluidR3 GM soundfont (MIT).

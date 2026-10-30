# Changelog

## 0.3.0 – Milestone 3: Pathfinding, combat and fog of war (30 Oct 2026)

- Pathfinding for huge maps: A* over the tile grid per movement type (foot,
  wheeled, tracked) with region labels, so impossible trips are rejected at
  once and clicks on water or cliffs go to the nearest reachable spot. Paths
  are smoothed into straight runs.
- Group orders: one shared route per group, formation slots at the
  destination, and a per-tick search budget so ordering 5,000 units never
  freezes the game. Units that get stuck re-plan; failed searches back off.
- Collision: units keep their spacing (vehicles nearly their full width,
  infantry closer) using a spatial hash; pushes never shove units into water
  or cliffs.
- Combat: weapons, warheads and armour classes from `content/rules/weapons.json`.
  Rifles, machine guns and 75/76 mm tank guns; instant-hit weapons roll for
  accuracy, shells fly, scatter and burst with splash damage.
- Turrets turn independently of the hull, one facing step at a time; tanks
  fire on the move, infantry stop to shoot.
- Orders: attack (chases a moving target), attack-move (stops to fight anything
  on the way, then carries on), move, stop. Right-click an enemy to attack;
  A or Ctrl+right-click to attack-move.
- Fog of war per side: unexplored ground is black, explored ground out of view
  is dimmed, edges fade smoothly. Enemies are hidden and can't be targeted
  unless seen. Tab switches side and view; the editor reveals everything.
- Battle effects: muzzle flashes, tracers, shell bursts with thrown earth and
  sparks, craters and scorch marks, fallen infantry, and burning wrecks
  pouring black smoke before leaving blackened hulks.
- Tanks and scout cars are now baked as separate hull and turret sprites.
- Debug panel: per-step simulation timings, path statistics, reveal map,
  health bars on every damaged unit.
- Performance: a 10,000-unit battle (7,000 infantry, 3,000 tanks) on a
  512 × 512 map averages about 13 ms per simulation tick (66 ms budget).
- 34 unit tests (adds pathfinding, group movement, combat, vision and
  determinism of whole battles).


## 0.2.0 – Milestone 2: Terrain, maps and the editor shell (30 Oct 2026)

- New art direction: gritty and realistic in the spirit of Blitzkrieg.
- Terrain is now a lit height mesh with eight blended ground layers (grass,
  dry grass, dirt, mud, sand, rock, snow, dirt road): procedural seamless
  textures, height-based blending and large-scale tone variation, so no tile
  grid shows.
- Animated water with depth-based colour, shoreline fade and glints; a depth
  buffer lets hills hide what is behind them.
- Random countryside: rolling hills, a winding river in a gentle valley with
  muddy banks, two dirt roads with fords, woods, scrub and rocks.
- Trees, bushes and rocks rebuilt at double detail with soft, supersampled
  edges and no outlines.
- Units and props follow the terrain; mouse picking works on hills; units
  stop at cliffs and deep water (per movement type).
- Map files (.lmap): compressed, versioned, safe against damaged files; store
  terrain, water, props, units and map info.
- Map editor (F2) over the running world: raise, lower, smooth, flatten,
  roughen, paint ground, place and erase props, water level, brush size and
  strength, 100-step undo/redo, new / open / save.
- Maps from 64 × 64 to 1024 × 1024; a 1024 map with 100,000 trees starts in
  about 2 seconds.
- 24 unit tests (map maths, painting, passability, file round-trips and
  corruption, undo/redo across chunk borders).


## 0.1.0 – Milestone 1: Foundations (30 Oct 2026)

- Slave Engine core: deterministic fixed-point maths, integer CORDIC atan2,
  seeded PCG random numbers, entity/component storage with generation-checked
  handles, logging.
- Simulation at a fixed 15 ticks per second: spawn / move / stop / remove
  commands, 16-step visible turning, group moves in formation, state hash.
- Renderer: SDL3 window, OpenGL 3.3 core, instanced sprite batching, team-colour
  shader, chunked terrain, pixel-snapped camera with 0.5× / 1× / 2× zoom.
- Voxel baker: renders voxel models into isometric sprites with three-tone
  shading, lit edges, outline and drop shadow, in 16 facings.
- Placeholder art for Germany and Russia (medium tank, scout car, rifleman),
  trees, pines, bushes, rocks and procedural grass, meadow and dirt tiles.
- Random countryside maps up to 1024 × 1024 tiles.
- Controls: click and box select, double-click select-by-type, right-click move,
  control groups, side switching, pause, game speed, fullscreen, screenshots.
- Debug panel with spawn tools (up to 10,000 units at once).
- 13 unit tests, including a determinism test with 1,000 moving units.
- GitHub Actions: Windows build and tests on every push.

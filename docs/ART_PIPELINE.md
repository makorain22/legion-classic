# Art pipeline

Every sprite in Legion Classic is generated when the game starts: units,
buildings and trees are built as voxel models in code and baked into
pixel sprites from the isometric camera, in 16 facings, on all CPU cores.
Effects (explosions, smoke, craters) and ground decals (trenches, oil, rubble)
are painted procedurally. Nothing is downloaded and there are no image files
to lose.

Any sprite can be replaced by a PNG file without touching code. This is how
hand-painted or externally rendered art comes into the game.

## Scale

| What | One voxel is | Example |
| --- | --- | --- |
| Vehicles | 1/36 tile (about 14 cm) | A medium tank is about 40 voxels long: 1.1 tiles |
| Soldiers | 1/56 tile | A man stands 18 voxels tall: a third of a tile |
| Buildings | 1/12 tile | A 2 × 2 cottage is a 24 × 24 voxel grid |
| Trees, rocks | 1/32 tile | An oak is about 1.2 tiles across |

A tile is a 64 × 32 pixel diamond at 1× zoom. These scales keep everything
true to life next to each other, so a battle with thousands of units reads as
an army on a wide battlefield rather than toys on a board.

## The look

- **Baking.** Each model is rendered at twice the final size and averaged
  down (no outlines, soft edges), lit from the upper left, with ambient
  occlusion in crevices (under fenders, between road wheels, where walls meet
  roofs) and a little per-voxel grain, and a soft shadow on the ground.
- **Vehicles** get camouflage (German three-tone dunkelgelb / olive / red-brown
  stripes; Soviet plain green with faded and touched-up patches), mud thrown
  up the running gear, dust on top faces, chipped edges, and the national
  marking: a plain cross or a red star. No other insignia are used.
- **Team colour** is a thin band (turret ring, hull top edge, a pack and
  blanket roll on soldiers). The game recolours it for each player.
- **Buildings** get weathered plaster and brick, rain streaks and soot under
  the windows, shell damage and rubble.

The modelling toolkit lives in `engine/src/assets/art_common.*` (sloped armour
plates, tracks with road wheels and turning sprockets, wheels, barrels,
domes, camouflage, grime, markings, team bands, soldiers with their kit), the
models in `unit_models.cpp` (soldiers and vehicles), `world_models.cpp` (biome
props and civilian buildings) and `placeholder_art.cpp` (military buildings,
effects, decals).

## Replacing sprites with PNG files

1. **Export** the current sprites:

   ```
   LegionClassic.exe --export-sprites exported
   ```

   This writes every sprite as a PNG into `exported/`, plus `sprites.json`
   with each sprite's anchor (the pixel that stands on the unit's position).

2. **Repaint** the ones you want. Keep the file names. Sizes may change.

3. **Drop them into** `content/art/`, in the same folder layout, next to the
   exe, or better, into a mod's `art/` folder, so they can be switched off
   again (see [MODDING.md](MODDING.md); the sprite studio exports straight
   into a mod). Copy the matching entries of `sprites.json` too if you changed a
   sprite's size or want to move its anchor. The next start uses your files;
   the log says how many were replaced. Anything you don't supply keeps the
   generated art.

### Names

| File | Sprite |
| --- | --- |
| `units/<model>@<faction>/body_f<00-15>_a<frame>.png` | A unit's hull or body: 16 facings (00 faces east-south-east, then clockwise in steps of 22.5°), animation frames from 0 |
| `units/<model>@<faction>/turret_f<00-15>.png` | A turret, drawn on top of the hull and turned on its own |
| `units/<model>@<faction>/corpse_f<00-15>.png` | Fallen infantry (`corpse_f<nn>_a<frame>` when the fall is animated; the last frame stays) |
| `icons/<model>@<faction>.png` | Optional picture for the sidebar and encyclopedia |
| `units/<model>@<faction>/body.png` | A building (one view) |
| `props/<kind>_<variant>.png` | Trees, rocks and other props: `tree`, `pine`, `bush`, `rock`, `snow_pine`, `dead_tree`, `palm`, `scrub`, `jungle_tree`, `debris`, `sandstone`, `stump`, `logs`, `haystack`, `crates`, `fence` |
| `effects/...`, `decals/...` | Explosions, fire, muzzle flashes, smoke, dust, craters; trench pieces (by which neighbours they join), oil stains, rubble |

`<model>` is the `model` field of the unit in `content/rules/*.json`
(`tank`, `heavy_tank`, `rifleman`, `cottage`, ...); `<faction>` is `ger` or
`rus`. Civilian buildings are exported under both.

### Team colour in PNG files

Paint team-coloured parts in **pure magenta**: red and blue equal, green zero.
The brightness is kept: `#FF00FF` is the bright team colour, `#800080` a dark
shade of it. Everything else is drawn as painted. Avoid magenta-ish colours
elsewhere in the sprite.

### Anchors

`sprites.json` maps sprite names to `[x, y]` pixel anchors. Without an entry, a
replaced sprite of the same size keeps the generated anchor; one of a new size
stands on its bottom centre (three quarters of the way down).

## The Blitzkrieg art pack

Nival released the source and data of **Blitzkrieg** (2003) for
non-commercial use (<https://github.com/nival/Blitzkrieg>). Legion Classic can
read that data and render it into an **art pack** for your own use:

```
LegionClassic.exe --import-blitzkrieg <path to Blitzkrieg>/Versions/Current/Data
```

It takes a few seconds and writes `content/art/blitzkrieg/` (about 5,000
sprites, 25 MB). The next start uses it; Settings → Display can switch back to
the built-in art (or start with `--no-art-pack`).

**Licence.** The pack is Nival's art. Their licence allows non-commercial use
only and can be revoked, in which case you must stop using it. Keep it to
yourself: don't sell it, publish it or bundle it with anything sold. For that
reason it is never committed to this repository or included in the automatic
builds; only the importer and the mapping are.

**What it reads** (formats worked out from Nival's source: nested chunks as
written by `StructureSaver2`):

- `.mod` vehicle and gun models: meshes on a skeleton, textured with `.dds`.
  They are rendered from our camera in 16 facings, the turret (the
  `Turret` node and everything on it) separately, with lighting, a cast
  shadow and a small team-coloured **pennant** so the owner reads at a glance.
- `.san` infantry sprite animations: 8 directions; the still pose, the walk
  cycle, the shot and the fall (which ends as the corpse). Two-man teams are
  composed from two soldiers (the gunner and a rifleman).
- `.san` building sprite packs with their shadows, fitted to our footprints
  (Blitzkrieg's houses are bigger than ours), and small objects (sandbags,
  hedgehogs, wire, trees, haystacks) placed on a tile.
- `icon.tga` pictures for the sidebar and the encyclopedia.

Summer paint is the default; winter (`w`) and Africa (`a`) paint become
`model@faction~winter` and `~desert` variants, picked by the map's biome.

**The mapping** – which Blitzkrieg unit plays which of ours – is
`content/art/blitzkrieg.json`. Each entry names a folder inside the Data
folder and a kind (`vehicle`, `infantry`, `building`, `objects` or `prop`),
with options: `scale`, `brightness`, `pennant`, `turret` (vehicles), `second`
(infantry teams), `state` (buildings: 1 intact, 2 damaged, 3 destroyed),
`parts` and `turret` (objects: emplacements are sandbags with a gun model
turning inside). Change it and import again to try other units: there are
dozens of tanks, guns and buildings to choose from.

**Lines that join up.** Sandbags, barbed wire and fences are drawn per
piece from the built-in voxel models, which come in 16 shapes for each:
`SpriteCatalog::joined[model][mask]`, where the mask says which neighbours
the piece has (+X 1, +Y 2, −X 4, −Y 8). A lone piece is a short stack or a
post; a run becomes one continuous low wall or wire fence; corners and
T-junctions meet cleanly. Fortifications and small structures are baked at a
finer voxel size (1/24 of a tile) so the pillbox reads as a smooth concrete
dome rather than blocks. Supply points are drawn as a fenced compound with
crates in the corners, from the same fence pieces.

**Scale.** Blitzkrieg's map cells are 32×16 pixels, half our tile each way.
Vehicles render at 72 Blitzkrieg units a tile (Blitzkrieg's own is 64);
infantry and building sprites keep their pixel size.

No swastikas: only the vehicle crosses are used; flags, aircraft and statues
are not imported.

## Adding a new unit

1. Add it to `content/rules/units.json` (or `buildings.json`) with a `model`
   name.
2. Either build a voxel model in `unit_models.cpp` and register it in
   `addUnitModels()` under that name, or reuse an existing model name and
   replace its sprites with PNGs as above.
3. `LegionClassic --gallery` lines up every unit and building for a look.

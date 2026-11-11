# Changelog

## 0.9.1 – in progress (edits after Milestone 9; Milestone 9.1: terrain and camera overhaul)

- **Fixed a crash** in skirmish (seen in a 1v1v1 at high game speed, and
  probably the old "2v2 crash after five minutes"): a man killed inside a
  building stayed on its occupant list until the next tick, and the
  renderer read his owner from a deleted unit. A dead man now leaves the
  list at once, and the renderer checks anyway. Regression test added.
- `--ai-battle --sides 3`: every side for itself.
- **Men dug in look dug in.** Every kind of infantry of every army has two
  new models: down in a trench, waist deep behind its own lip of earth, and
  kneeling behind bags, each keeping low or up and firing. Men manning a
  trench now stand along its channel instead of on the decal.
- **Sandbags can be manned** like trenches: right-click a wall (or right
  beside it) with infantry. Three men a piece, behind the bags, half damage,
  they stay until evacuated. Vehicles can't cross sandbags; infantry climb
  over. Tutorial 3 now teaches it with the Bren teams (and its second wave
  no longer brings an MG 42 that out-ranged the trench).
- **Pioneers repair what they hold:** an engineer in a bunker, tower, trench
  or sandbag wall mends it and the field works right round it.
- **British vehicles have their own models:** Daimler Dingo, Universal
  Carrier, Crusader, Cromwell, Sherman Firefly (its own model, with the
  countershaded 17-pounder), Achilles, Sexton, Churchill, and Bedford lorries
  for the supply and construction trucks.
- **Fifteen more civilian buildings:** half-timbered house, stone cottage,
  corner shop, apartment block, factory, inn, terraced row, manor, town hall,
  onion-domed church, grain silo, windmill, market stalls, courtyard villa,
  stilt house. Villages pick them by climate; the jungle has villages now.
- **Bridges** where roads cross rivers, stone or timber, laid level on arches
  or trestles. Anything crosses on the deck; destroy a span and whatever is on
  it goes into the river. Bridge pieces are in the editor with the civilian
  buildings. Skirmish maps regenerated with the new villages and bridges.
- Demos for screenshots: `--works-demo`, `--town-demo`, `--bridge-demo`,
  `--gallery --factions gbr`.

### Milestone 9.1: the 3D renderer and free camera

- **The battlefield is real 3D.** Every voxel model the game used to bake into
  sprites (units in each frame of their animations, turrets, the fallen,
  dug-in men, buildings, field works, bridges, trees and props) is now drawn
  as a mesh: greedy-meshed on first use, lit by the sun with the same shading
  the sprites had (darker near the ground, crowded corners darker, a grain
  per voxel), team colours and markings straight from the palette, drawn
  with GPU instancing. The sprite baker remembers which model every sprite
  came from, so nothing in the art had to change.
- **Shadows:** the sun casts real shadows from models and hills onto the
  ground and onto each other (Settings > Game > Shadows).
- **Free camera:** turn all the way round and tilt from low across the field
  to almost straight down (middle mouse drag, or Q / R and Page Up / Page
  Down); zoom smoothly towards the mouse; Shift + middle drag pans;
  Backspace resets the view. Hills hide what is behind them.
- Units turn smoothly instead of in 16 steps; vehicles lean with the slopes
  they drive over; turrets turn on their hulls.
- Smoke, fire, flashes and explosions are camera-facing sprites placed in the
  world; tracers are streaks between muzzle and target; craters, scorch
  marks, oil stains, trenches, selection rings and move markers lie on the
  ground. Health bars, unit cards, chips and the editor's brushes stay flat
  on the screen.
- Picking works by casting a ray from the mouse to the ground; the minimap
  shows the ground the camera sees.
- The ground carries on past the edge of the map, so a low view never looks
  into a void. Far-off models use coarser meshes (a quarter and a sixteenth
  of the triangles), ground textures are filtered for low angles, and edges
  are smoothed with 4x multisampling where the driver offers it.
- Placement ghosts (sidebar and Live Scenario Editor) are see-through models.
- Saves keep where the camera looks and how it is turned (older saves still
  load). The menu's battle slowly circles the field.
- **The classic 2D view stays** behind a switch: Settings > Game > 3D
  battlefield, or `--classic`. `--yaw` and `--pitch` (degrees) start the 3D
  view at an angle, for screenshots.
- Replays: reveal the map with **V** (R now turns the view).
### Milestone 9.2: terrain 2.0

- **Ground sets the pace.** Every tile has a speed for foot, wheels and
  tracks: slopes slow everything (down to 65% at the steepest climb), mud
  bogs wheels down (60%) and slows tracks and men, sand and snow slow
  vehicles, and **fords and wading are half speed** while staying open to
  everything. Bridges and roads are full speed. Paths weigh it: a truck goes
  round a mud field by the road when that is quicker, and shortcuts never cut
  back across slower ground.
- **Smooth ground:** the 3D ground is a smooth surface through the height
  corners, drawn at twice the old resolution, with soft normals; units,
  decals and picking all stand on the same surface.
- **Cliffs and overhangs:** ground too steep to climb is covered in voxel
  rock, banded stone with a turf, snow, sand or moss cap on the crest and
  ledges jutting out over the face below. Ridge and Hill country maps get
  escarpments: hillsides stepping up in terraces with rock faces between
  (never across a road).
- **Ground blends round things:** earth, grass, snow or sand creeps up the
  foot of buildings, wrecks, trees and rocks in a ragged line.
- **Building plots are levelled:** a new building flattens its plot and the
  ground round it eases into the slope (at most 14 height units per tile).
  Buildings may now go on slightly steeper ground (30).
- **Graded roads:** road beds are smoothed along their length, shoulders
  eased in.
- **River banks:** rivers sit in a cut, a short steep bank out of the water
  and the valley above it; the bank is cut away where a road comes down to a
  ford.
- **Bridges (playtest edit 1):** only where both banks stand well above the
  water, laid straight across square to the map beside the ford, the deck at
  the higher bank's height and both ends set level into the banks with a
  stretch of road on. Bridges near a base are no longer cut in half.
- All bundled maps (skirmish, campaigns, tutorial, Kalach) are rebuilt with
  the new ground. Replays from before 9.2 no longer play back the same.
- `--cliff-demo` puts the camera on the most rock on the map.

### Milestone 9.3: intel instead of fog

- **No more fog of war.** The whole map is always in view: ground, woods,
  villages, the lie of the land. What you don't see is the enemy.
- **Intel chips (playtest edit 6, as in R.U.S.E.):** enemy forces out of
  sight of all your units show as counters standing roughly where they are,
  sized by roughly how many (few, some, many, a host). Within twice a unit's
  sight a chip also shows the kind of force (its NATO symbol); further off it
  is just a "?". Enemy buildings show as square counters until one of your
  units has had them in sight; after that they stay on the map, dimmed while
  out of sight, and on the minimap. Far chips are drawn smaller in 3D.
- **Card stacks (playtest edit 3):** unit cards close together on screen
  merge into one stack card ("5 units") with the cards behind it peeking out.
  Hover or select a stack and it opens into a list of the cards in it;
  clicking the stack selects all of them.
- **Scouts (playtest edit 11):** scout cars see about twice as far as other
  units (22 to 24 tiles). A scout standing watch that sees an armed enemy come
  within 6 tiles falls back 5 tiles straight away from it and keeps watching
  from there: no shooting match, and it doesn't leave the area. Orders you
  give it are never overridden. A status note says when one falls back.
- The editor shows everything; *Show what the player's side would know* on
  the Battle tab shows the intel view. Saves rebuild what each side has seen
  from its explored ground. `--intel` watches an `--ai-battle` with the
  first side's intel instead of seeing everything.

- **The sprite studio is removed** (for now): play no longer uses the baked
  sprites. Sprite overrides in mods still load for the classic view and the
  sidebar pictures; `--export-sprites` still works.

### Milestone 9.4: armour and artillery

- **Armour is lighter, on one scale.** In every army a medium tank has half
  its heavy tank's health and a light tank a quarter, and the heavies came
  down too: German Heavy Tank 640 (was 950), Breakthrough Tank 680 (1000),
  Churchill 720 (1150); mediums 320 / 340 / 360 (Firefly 360); lights 160 /
  170 / 180. Tank destroyers 300 (Achilles 240), half-tracks 130 (Universal
  Carrier 110), scout cars 80, self-propelled artillery 120, construction
  trucks 400.
- **Four kinds of vehicle armour** instead of three: *soft* (cars, carriers,
  lorries, self-propelled guns: bullets hurt them), *light*, *medium* and
  *heavy* (heavy tanks and the casemate tank destroyers). Units now carry the
  class their name says.
- **Three weights of armour-piercing shot.** Light guns (37 and 45 mm,
  anti-tank rifles) do a quarter of their damage to a heavy tank's front and
  60% to a medium; medium guns (75 and 76 mm, the 6-pounder, Panzerschreck
  and PIAT) do 55% to a heavy; only the heavy guns (88 and 122 mm, the long
  75, the 85 mm, the 17-pounder and the anti-tank gun pits) go through at
  full weight. A heavy tank beats two medium tanks head on and falls to
  four, to a pair of tank destroyers, or to anything that gets behind it.
- **Shells hit what they land on.** A shell's blast is now measured from a
  vehicle's hull instead of its centre point, so a round that lands on a
  tank hits it. Before, about two shells in three aimed at a tank eight
  tiles off did nothing, which is most of why armour felt so hard to kill.
  Long shots still scatter and lose force; closing the range pays.
- Side hits now count 1.5 times and rear hits 2.5 times (were 1.6 and 4.5):
  with the lighter armour a flank shot still decides a duel without ending
  it in one round. Panzerschreck 100 damage (was 130), PIAT 95 (was 120);
  the German 75 mm fires as fast as the Soviet 76.
- **Artillery reaches half as far again:** 105 mm howitzer 28 tiles (was
  18), rocket truck 24 (16), Sexton 26 (17); the emplaced 150 mm 36 (22),
  122 mm 34 (21) and 5.5-inch 38 (24). It still needs someone to see the
  target, and scouts now see far enough to do it.
- **Reverse gear.** A vehicle sent a short way to its rear (up to 5 tiles, or
  up to 12 while it is shooting or being shot at) backs up at 60% speed
  instead of turning round, so its front armour and its gun stay on the
  enemy. Further than that it turns and drives.

### Milestone 9.5: formations

- **A group marches as one body.** Sending several units somewhere used to
  give each its own path and its own speed, so the shape only appeared at
  the far end, if at all. Now the group forms up and moves as a formation at
  the pace of its slowest member, turning with its route and holding its
  shape the whole way. Tanks and their infantry arrive together.
- **Cohesion:** the body slows and then waits for members that fall behind;
  one that starts far off is not waited for and catches up on the way. Where
  the ground narrows the flanks draw in and open out again beyond.
- **Columns follow the road:** in Column every rank takes the route the head
  took, bending round corners and filing over bridges and through gaps.
- **Attack-move as a body:** the formation halts to fight when anyone has a
  target in range, closes with an enemy near its route together, and carries
  on afterwards.
- **Spacing:** men a couple of paces apart; vehicles a hull's width apart
  side by side and more nose to tail. Line is one rank for a handful, two for
  a platoon, three beyond; a small Wedge is a true chevron. A formation sent
  to the map's edge stands inside it instead of piling up on the border.
- **Facing:** units keep the front they were given: they turn to it on
  arrival and turn back to it a few seconds after a fight. A group sent a
  short way to its rear reverses as a body.
- **Controls:** the formation buttons now show the shape the selected units
  are in and **re-form them at once** when clicked (standing or on the
  march); the shape belongs to the units and is used for every later move.
  **Z** steps to the next shape. Dragging a front with the right button
  shows a mark where every unit will stand and which way the front faces.
- Saved games are format SIM5 (marches in progress are saved); older saves
  still load. `--formation-demo` for screenshots.

## 0.9.0 – Milestone 9: Britain, sound, tutorial, campaigns (7 Nov 2026)

- **Sound and music**, built for immersion and size. A new audio module in
  the engine mixes up to 72 voices through a large-hall reverb and a limiter.
  Sound follows the camera: fighting in view is loud and sharp; further away
  it fades, dulls and echoes, and zooming out lifts the ear. When the battle
  is away from the camera, a far-off front rumbles in under the ambience.
  Every weapon has its firing and impact sound (rifles, Brens and MG 42s,
  anti-tank weapons, small, medium and large guns, howitzers, rockets),
  plus wrecks burning, buildings collapsing, construction, engines,
  marching men, whistles and the radio when you give orders, and the
  interface. Wind and birds, snow, desert, jungle or ruins by biome.
- **Nine orchestral pieces**, composed for the game: the main theme
  *Legion*, three calm pieces, three battle pieces, a victory fanfare and a
  lament. The music follows the fighting near the camera, with long
  crossfades. Settings has a Sound tab with six volumes. `--no-sound`.
- **Tutorial: Basic Training.** Four short lessons on a training ground,
  first on the main menu: giving orders, cover and armour, digging in, and
  building a base. Hints appear at the top of the screen, the button you
  need glows, and the build options open up as you go.
- **Campaigns.** Germany (*Kampfgruppe Adler*: Kalach Crossing, The Ice
  Road, Breakthrough on the Donets), the Soviet Union (*Guards Rifle Corps*:
  Sokolovo, The Bridgehead, Iron Storm) and Britain (*The Desert Rats*:
  Point 64, Operation Lantern, The Gap at Hellenthal). Each mission has a
  briefing; missions open one after another; progress is kept in
  `progress.json` in the user folder. What you achieve carries over: the
  Tiger that survives Kalach comes back, the six-pounders saved at Point 64
  turn up as Sextons later. A debriefing offers the next mission or another
  try.
- **Twelve skirmish maps**, one for every landscape, with villages and
  hamlets placed, chosen from a new *Map* list in Skirmish (or a new random
  map as before).
- **Scripting for missions** (Script tab and `.lmap`): new conditions *Unit
  count at least*, *Men in cover*, *Order given*, *Campaign flag at least*,
  and *Trigger fired* can wait some seconds; new actions *Show hint*,
  *Highlight interface*, *Lock building*, *Unlock building*, *Set campaign
  flag*. A mission whose script declares victory is no longer won just by
  wiping out an enemy wave. Saves are now SIM4 (SIM3 and SIM2 still load).
- `tools/make_content.cpp` (was `make_scenarios.cpp`) builds the tutorial,
  the campaign missions, the bundled scenario and the skirmish maps;
  `tools/audio/` makes the sound and music. `--campaign id[:mission]` and
  `--mission id:N` open a campaign or start a mission.

- **A third army: Britain** (`gbr`). Fourteen units, mirroring the German and
  Soviet roster with British kit and its own guns: Rifleman (rapid-fire
  Lee-Enfield), Bren Team, PIAT Team, Royal Engineer, Sniper, Dingo Scout
  Car, Universal Carrier, Crusader (6-pounder), Cromwell (75 mm), Sherman
  Firefly and Achilles (17-pounder), Sexton (25-pounder), Churchill (the
  toughest tank in the game) and Construction Lorry; the base under British
  names, with a Pillbox, a 6-pdr Gun Pit, a Vickers Nest and a 5.5-inch
  Medium Gun Emplacement. Eleven new weapons in `weapons.json`.
- **British doctrine** for the computer: an early observation post to bring
  on the guns, Sextons in every attack and an extra gun pit, a trench line
  and wire, big set-piece attacks, falling back sooner. Every side's computer
  now puts up a watchtower by the eighth minute, so heavy tanks, snipers and
  artillery come into its games.
- **Skirmish, the editor's Sides tab, the rules editor and the encyclopedia**
  offer Britain. The sidebar wears service brown and stone with an RAF-style
  roundel. Placeholder art: British soldiers in Brodie helmets and
  battledress; vehicles in plain service brown with the white-red-white
  recognition flash on the German hull shapes (British models to come).
- **Saves carry their unit list** (meta `types`) and are renumbered by id on
  loading, so adding units (a new army, a mod) no longer breaks older saves;
  0.8.0 saves load with the British taken into account.
- `--factions gbr,rus[,ger,gbr]` chooses the armies of an `--ai-battle`.

## 0.8.0 – Milestone 8: creation tools and mods (6 Nov 2026)

- **Mods.** A mod is a folder of rule changes, sprites and scenarios that sits
  on top of the game without changing it. *Main menu → Mods and creation
  tools* lists them: tick, order and **Apply**, and the game reloads its rules
  and art. Mod rule files name only what they change (matched by id; a new id
  adds a unit; `"_remove": true` deletes one); mods can require other mods and
  load after them. A broken mod never stops the game starting. Your own mods
  live in `%APPDATA%\Makorain\LegionClassic\mods\`; the game ships with an
  example, *Veteran Grenadiers*. See docs/MODDING.md.
- **Rules editor.** Every unit, building, weapon and warhead as a form, every
  field explained, changed fields in gold with what they were. It works out
  damage per second against each armour, speed, health per supply, and what
  needs or carries each thing. Changes are checked as you type and go straight
  into the running battle (the one behind the menu, or yours, from the editor's
  Battle tab): units already fighting take their new health, guns and turrets.
  **Save to mod** writes only the differences.
- **Sprite studio.** Every sprite set: units turned through 16 facings and
  animated, in any side's colours on any ground; whole sheets frame by frame;
  hue / saturation / brightness recolouring (winter whitewash, desert sand);
  anchor nudging; export for painting and PNG import, straight into a mod.
- **Saves and replays remember their mods** and a fingerprint of the rules:
  a game made with other mods says which to enable; a replay needs the very
  same rules.
- `--tool rules|sprites|mods` opens a creation tool at start;
  `--mods-selftest` runs the tools through the real game.

## 0.7.8 – Map variety, crash reports (6 Nov 2026)

- **Twelve landscapes for random maps:** river valley, front line river (a
  river between the sides with three fords), confluence, lakeland,
  crossroads (a market town where two roads cross), ridge (high ground
  crossed at passes), marshes (shallow pools men can wade, causeway roads),
  great lake, open steppe (dry gullies), deep forest, hill country and twin
  rivers. Choose one in Skirmish and the editor's New map (*Landscape*), or
  leave it on *Random* and the seed picks one; every seed varies the rest
  (where rivers wind, how many lakes, roads, woods and villages). Rivers bend
  round the starting areas, which stay dry and level; roads ford every river.
- **Crash reports:** a crash writes `legion-crash.txt` and
  `legion-crash.dmp` beside `legion.log`; Windows builds ship
  `LegionClassic.pdb` so reports name the exact place.
- The computer digs its trench line, wire and tank traps even when short of
  supplies, and only where it may build.
- Trench lines join only pieces of the same side.
- **Showcase videos:** `--showcase out.mp4` records a computer battle with a
  camera that follows the fighting (`--showcase-warm`, `--showcase-seconds`).

## 0.7.7 – Playtest rework: trenches, unit cards, bases, computer doctrines, field works (1 Nov 2026)

From the 0.7.6 playtest.

- **Trenches hold men.** Each piece of trench takes up to three men, who drop
  into it and stay there (60 % less damage); move orders don't pull them out.
  **Evacuate** (E or Ctrl+E, or the sidebar button) brings them out. Men in
  trenches show, can be picked and box-selected.
- **Every unit is its own.** Clicking or box-selecting picks just those men or
  vehicles, whatever unit they belong to; **click a unit card** to select the
  whole unit, double-click it to centre on it.
- **Combined units.** Tanks and infantry bound together (Ctrl+B) become a
  mechanised platoon, battle group or armoured regiment, with a bigger fire
  bonus; vehicles alone are named by role (armoured, reconnaissance, tank
  destroyer, artillery battery/battalion/regiment, mechanised).
- **Bigger unit cards** with symbol, name, strength, a health bar and a team
  stripe, stacked neatly; enemy cards show too (Settings → Display toggle).
- **Starting positions** for skirmish: opposite sides (default; teams face
  each other across the map), fixed, or shuffled.
- **Bases laid out as military posts:** a compound with roads and a parade
  ground, barracks and power behind, the depot on the nearest supply point,
  a sandbag line with a gate and MG nests across the front, watchtowers,
  wire and tank traps beyond.
- **Computer player rework.** Each side fights to its own doctrine: the
  Germans build fewer, better units, bind them into battle groups and fortify
  in depth; the Soviets mass riflemen and T-34s, lean on artillery and stream
  reinforcements into an attack. The main force attacks (often by a flank),
  regroups and digs in when hurt instead of running home, then goes again.
  Fast vehicles raid the weakest enemy depots. A home guard mans the base
  trenches and bunkers; damaged vehicles go home for repairs. It expands to
  distant supply points (with MG nests beside them), builds a second yard,
  and digs trench lines, wire and tank traps across its approach. It binds
  its forces into units, so its cards mean something.
- **Fortification art:** sandbags as a low line that joins up into walls;
  barbed wire as a post-and-wire fence; hedgehog tank traps; a smooth
  concrete pillbox instead of the blocky bunker (finer voxels for all field
  works). Supply points are fenced compounds with crates where a depot goes,
  not circles.
- **More buildings:** a railway station, a farmhouse and a school, in towns
  and villages and in the art pack.
- **Live Scenario Editor:** grid painting (G) for exact terrain edges;
  civilian buildings are placed from the Props panel and belong to nobody.
- **Faction colours:** the sidebar and encyclopedia wear field grey for the
  Germans and khaki for the Soviets, with painted stripes and the side's
  emblem (a plain cross; a red star). The menus stay olive.
- Saves are version SIM3; 0.7.6 saves still load. `--side N` watches an
  `--ai-battle` from another side.

## 0.7.6 – Blitzkrieg art pack, new interface, encyclopedia, more content (31 Oct 2026)

- **Blitzkrieg art pack (personal use).** `--import-blitzkrieg <Data folder>`
  reads Blitzkrieg's own models, sprites and textures (released by Nival for
  non-commercial use) and renders an art pack into `content/art/blitzkrieg`:
  26 units, 18 base and civilian buildings, emplacements, fortifications,
  trees and decorations, with winter and desert paint for those maps. Vehicles
  in 16 facings with turrets and a team pennant; infantry with walk, firing
  and falling animations; two-man teams composed. Not committed or shipped
  with the builds (licence); the importer and mapping are. Switch it off in
  Settings → Display.
- **Sidebar redesign** in the period style: steel plates and rivets, a header
  for supplies and power, picture tabs, bigger build buttons grouped under
  headings (Base, Production, Emplacements, Field works, Infantry,
  Specialists, Recon & transport, Tanks, Support…), plate buttons for orders.
- **Encyclopedia** (K / F4, sidebar, main menu): every unit and building with
  picture, numbers and damage a minute against each armour.
- **Script tab:** a quick-start page with ready-made triggers (briefing,
  capture an area, victory, reinforcements, counter-attack, defeat); triggers
  read as WHEN … THEN … sentences, each line opened to edit in place; an
  objectives page with their numbers.
- **New content:** MG nests and howitzer emplacements for both sides; barns,
  sheds, log houses, warehouses, wells and water towers in villages and towns;
  tree stumps, log piles, haystacks, crates and fences on the map. Unit
  descriptions.
- **Art pass:** colour grading per biome and a vignette (Settings →
  Atmosphere); German voxel paint darker, Soviet uniforms khaki; long tracer
  streaks; trenches follow slopes; corpses stay ten minutes (and fall when hit),
  burnt-out wrecks eight.
- Infantry sprites walk on time with each man out of step, aim at their
  target and play the shot when they fire.

## 0.7.5 – Playtest rework: controls, economy, unit organisation, garrisons, fog chips (31 Oct 2026)

From playtest feedback, the changes that are hard to undo come first.

- **Controls:** every key can be rebound (Settings → Controls), saved with
  your settings. WASD scrolls alongside the arrows; commands moved to Ctrl:
  attack-move Ctrl+A (or F), stop Ctrl+S (or X), deploy Ctrl+D (or G), select
  all Ctrl+Shift+A. Ctrl+right-click opens a formation picker (move or
  attack-move in box, line, column, wedge or loose).
- **Fixes:** edge scrolling works over the sidebar and panels; the mouse is
  kept inside the window while playing (screen lock, can be turned off); the
  main menu is centred; a frame rate limiter (default 144, or unlimited) and
  vsync setting; minimap zoom (1×–8×, buttons and wheel); the editor started
  from the main menu no longer leaves the menu open and swallowing input;
  the editor's map size list is fixed; the game info panel is hidden by
  default (I toggles it) and the controls panel shows only on first start.
- **Economy:** oil and supply trucks are gone. Supply points on every random
  map (two by each start, more in the middle); a Supply Depot on one earns
  $600 a minute, anywhere on the map; the yard trickles $120. The computer
  claims the nearest free points. Income shows on the sidebar; points show on
  the ground and minimap. Editor tool to place them.
- **Cheaper infantry and fortifications:** Grenadiers $10 (Riflemen $8), MG
  teams $40, AT teams $50, engineers $30, snipers $80; train one man or a
  fireteam of five. Sandbags and trenches $5, wire $3, tank traps $10.
- **Unit organisation:** fireteams of five form automatically; bind by hand
  (Ctrl+B) into squads (10), platoons (20) and companies (100), or armoured
  platoons (5), companies (15) and battalions (50); Ctrl+U disbands.
  Firepower bonus by size; casualties demote (a company becomes a platoon at
  20); members stay members however far they go. Selecting one selects the
  unit. NATO-style cards with branch and size marks and automatic names;
  hover readout with type, unit and strength.
- **Garrisons:** infantry occupy civilian buildings (6–30 men), bunkers (4) and
  watchtowers (1), take half damage inside and fire out; sandbags go up in
  three stages as more men move in. Men are thrown out, hurt, if the building
  falls. Right-clicking a trench mans the whole trench line.
- **Small arms against armour:** damage is counted in hundredths of a hit
  point, so rifles chip at tanks: about thirty men wear a heavy tank down.
- **Fog chips (R.U.S.E style):** enemy forces detected beyond sight (twice
  each unit's sight) show as stacks of chips: the size gives a rough count,
  the symbol the most valuable unit.
- **Editor:** the unit palette is grouped under headings (infantry, vehicles,
  base buildings, defences, fortifications, civilian buildings); a placement
  grid around the cursor when placing buildings (also from the sidebar).
- Saves from 0.7.0 don't load (the saved state now holds units, garrisons and
  income).
- 71 unit tests (adds fireteams, binding and demotion, garrisons, small arms
  against armour, supply depots).

## 0.7.0 – Milestones 6 and 7: computer player, scripting, saves and replays; art and content (31 Oct 2026)

**Milestone 6: the computer player, scenario scripting, saves and replays**

- Main menu over a live battle between two computer players; skirmish setup
  (map size, biome, seed, supplies, up to four sides: human or computer at
  easy / normal / hard, faction, team); scenario list; load game; replays;
  in-game menu (Esc / F10: resume, save, load, save replay, restart, main
  menu, quit).
- Computer player that plays by the rules, inside the simulation tick (so its
  games replay exactly): power first, then depots, barracks, works and extra
  production; defences; supply trucks per depot; a mixed army at a staging
  point; base defence; growing attack waves that attack buildings directly
  with units that can hurt them; falls back when worn down. Easy, normal and
  hard differ in thinking speed, wave size and fortification.
- Scenario scripting: named areas, triggers (all conditions, then actions,
  once or repeating), 8 conditions and 17 actions (messages, objectives,
  victory / defeat, spawns, supplies, reveal, attack-move / move, change
  owner, kill, switch triggers, hand a side to the computer, camera).
  Objectives shown on screen. Script tab in the editor, editable mid-battle.
- Saves: the full simulation state (every component, paths, vision, script
  progress, the computer players) in compressed `.lsav` files; a loaded game
  continues bit for bit.
- Replays: the starting state plus every command with its tick, in `.lrep`
  files; the last game is kept automatically. Play / pause, 1×–8×, watch any
  side, reveal the map.
- Scenario files v2 store the triggers.

**Milestone 7: art pipeline and content**

- Full roster for Germany and Russia: riflemen, MG teams, AT teams
  (Panzerschreck / anti-tank rifle), engineers, snipers; scout cars,
  half-tracks, light, medium and heavy tanks, tank destroyers, artillery
  (105 mm howitzer / rocket truck), supply and construction trucks. New
  weapons including 37 / 45 / 75 L70 / 85 / 88 / 122 mm guns, heavy MGs,
  scoped rifles and salvo rockets (`burst`: 8 rockets rippled a tick apart).
- Engineers repair friendly vehicles and buildings (right-click), and look
  for damaged ones nearby when idle. The computer keeps engineers at home.
- New detailed voxel art at true scale (vehicles 1/36 tile per voxel,
  soldiers 1/56): sloped or slab armour, tracks with road wheels and turning
  sprockets, return rollers, side skirts, mantlets and muzzle brakes,
  commanders in hatches, fuel drums, tools, spare track links; German
  three-tone camouflage and plain Soviet green; mud, dust, chipped paint;
  plain cross and red star markings; soldiers with helmets, packs, webbing and
  their weapons (MG and AT teams are two men). The baker adds ambient
  occlusion and grain, and bakes on all CPU cores; sprites are trimmed.
- Five biomes for random maps: temperate, winter (snow, snowy pines, frozen
  river), desert (sand, rock, palms, narrow green rivers), tropical (jungle,
  broad muddy rivers) and urban (a street grid of town houses, churches,
  cottages and ruins). Each has its own ground, trees, light and water.
- Civilian buildings (cottage, bombed cottage, town house, church, ruin,
  mud-brick house): neutral, blocking, give infantry beside them 30 % cover,
  attacked only on orders, grey on the minimap. Villages and towns are placed
  on random maps; the editor places them as neutral.
- PNG art pipeline: `--export-sprites` writes every sprite as a PNG with its
  anchor; PNGs in `content/art/` replace generated sprites by name (team
  colour painted in magenta). See `docs/ART_PIPELINE.md`.
- Editor: new maps in any biome with or without villages; new props in the
  palette; civilian buildings always listed.
- A bundled scenario, *Kalach Crossing* (winter): take a village, hold it
  against a counter-attack, destroy the Soviet base.
- `--gallery` lines up every unit and building; `--biome` picks the biome.
- 66 unit tests (adds saves, replays, triggers, the computer player, the
  roster, salvo fire, repairs, biomes, neutral buildings, the bundled scenario).

## 0.5.0 – Milestone 5: Economy, base building and fortifications (31 Oct 2026)

- Economy: supplies from oil. Supply trucks pump oil fields and drive it to an
  Oil Depot; fields run dry. Random maps get mirrored home and contested
  fields. Power stations power the base; low power halves build speed.
- Structures on the tile grid (they block movement and paths go round them):
  Construction Yard, Power Station, Oil Depot, Barracks, Tank Works,
  Watchtower, MG Bunker, Anti-Tank Gun Pit, for Germany and Russia.
- Fortifications: trenches (60 % cover for infantry, join up into lines,
  stop wheeled vehicles), sandbags (40 % cover beside them), barbed wire (stops
  infantry), tank traps (stop vehicles). Dragged out as lines.
- Production: four lines per side (buildings, defences, infantry, vehicles),
  paid as work progresses, with prerequisites, build range, rally points and
  a primary factory. Construction trucks unfold into yards. Selling refunds
  half. A side with nothing left is defeated; victory / defeat banners.
- Classic right sidebar: supplies, power bar, minimap (fog, units, camera,
  click to look, right-click to send), build tabs with progress, READY and
  queue counts, placement ghosts with green / red footprints, sell mode.
- Unit behaviour (playtest feedback): stances (aggressive, hold, hold fire);
  units shot on the march stop to fight back, idle ones go after attackers out
  of reach; Alt+right-click forced march.
- Formations: box, line, column, wedge, loose; right-drag to set the front
  line's position, width and facing; armour in front of infantry in mixed
  groups; units turn to face the front on arrival.
- Pathfinding keeps off riverbanks, cliff edges and building corners.
- Unit scale (playtest feedback): vehicles about 10 % smaller, infantry about
  half their old size, true to life next to tanks, trees and buildings; soft,
  outline-free sprites.
- Structures: build-up fade, dust, smoke and fire when damaged, rubble and a
  burning ruin when destroyed. Enemy structures stay on the map once seen.
- Editor: buildings, defences and fortifications in the palette (with a kind
  filter), oil-field brush (undoable), starting supplies per side.
- Scenario files v2: oil layer and supplies (v1 files still open).
- 61 unit tests (adds structures, production, power, oil, trucks, deploy, sell,
  trenches and cover, defeat, formations, stances, return fire).


## 0.4.0 – Milestone 4: Live Scenario Editor (30 Oct 2026)

- The editor (F2) is now the Live Scenario Editor, in four tabs: Terrain,
  Units, Sides and Battle. Everything works while the battle runs.
- Unit placement for any side from a palette with unit icons: one at a time,
  a brush that paints units over an area, or a formed block of up to 10,000.
  A see-through preview shows facing and where units can't stand.
- Select units of any side; drag to move them; delete, duplicate, kill, stop,
  turn, give to another side, set health. Live inspector shows a unit's
  health, order, route, target and weapon reload, or a breakdown of a group.
- One undo history for terrain and units (100 steps). Unit undo works mid-
  battle and follows units through delete / restore cycles.
- Sides: up to 8, with name, faction, colour, team (allies share vision and
  don't fight), human / computer flag and camera start. Take command of any
  side; Tab cycles through them.
- Battle tools: 105 mm artillery barrage, smite, heal brush; pause and speed;
  snapshot and restore of the whole battle; fog-of-war preview; clear wrecks.
- Scenario files now store sides and the briefing; older maps still open.
- Simulation commands for the editor (exact spawns with a spawn log, edits,
  kill, strike, teams) keep everything deterministic.
- Balance, from playtest feedback: riflemen miss 30 % of shots; tank guns
  reach 9.5–10 tiles, hit harder and switch to high-explosive shells against
  infantry; armour has front / side / rear values (a rear hit knocks out a
  medium tank in one shot); scout cars see farther; allies share vision.
- `--editor-selftest` drives the real editor through place, undo, redo,
  delete, move, give and snapshot, and checks the battlefield after each step.
- 41 unit tests (adds editor commands, teams, artillery, rear armour, HE
  switching, side round-trips, undo actions).


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

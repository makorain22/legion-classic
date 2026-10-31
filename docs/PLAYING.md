# Playing Legion Classic (Milestones 6–7 build)

Double-click `LegionClassic.exe`. No installation is needed; keep the `content`
folder next to the exe.

Windows may show a SmartScreen warning the first time because the game isn't
code-signed yet: choose **More info → Run anyway**.

## What's in this build

The game opens on the **main menu**, over a live battle between two computer
players on a random map.

- **Skirmish.** Pick a map size, a biome (temperate, winter, desert, tropical
  or urban), a seed, the starting supplies and up to four sides. Each side is
  you or the computer (easy, normal or hard), Germany or Russia, on a team of
  your choice. Every side starts with a base and a small force; villages and
  towns stand between them.
- **Play a scenario.** Missions that come with the game (*Kalach Crossing*:
  take a village in the snow, hold it, then destroy the Soviet base) and any
  map you saved in the editor.
- **Load game**, **Replays**, **Scenario editor** (opens the editor on a fresh
  map) and **Quit**.

In a game, **Esc** (or F10) opens the menu: resume, save, load, save a replay
of this game, restart, back to the main menu, or quit. **Tab** switches the
side you command, and **F2** opens the Live Scenario Editor while the battle
keeps running.

### The computer player

The computer plays by the same rules you do: it pays for everything, keeps its
power up, builds an economy (depots, supply trucks), barracks and works,
defences (bunkers, anti-tank guns, watchtowers), keeps an engineer or two at
home for repairs, trains a mixed army at a staging point in front of its base,
sends it to defend when the base is attacked, and launches attack waves that
grow over time. **Easy** thinks slowly and attacks with small waves; **hard**
thinks fast, fortifies more and attacks sooner and harder. You can hand any
side to the computer in the editor's Sides tab, even mid-battle.

### Saving, loading and replays

- **Save** from the in-game menu at any moment; **Load game** lists your saves
  with their date and game time. A loaded game carries on exactly as it was.
- **Replays.** Every game is recorded. *Save replay* in the in-game menu keeps
  it; your last game is always kept as `last_game`. A replay plays the battle
  back exactly as it happened: Play / Pause, speed 1× to 8×, **Tab** to watch
  another side, **R** to reveal the whole map. (A game you changed with the
  editor while playing can't be replayed, because the editor's changes aren't
  part of the record.)
- Saves (`.lsav`), replays (`.lrep`) and your maps (`.lmap`) live in
  `%APPDATA%\Makorain\LegionClassic\` (`saves`, `replays`, `maps`).

### The armies

Both sides field the same kinds of unit, built their own way: German armour is
boxy and camouflaged in three colours, Soviet armour sloped and plain green.

| Unit | Built at | Cost | Notes |
| --- | --- | --- | --- |
| Rifleman / Grenadier | Barracks | 80 / 100 | Cheap, hit 7 shots in 10. Useless against armour. |
| MG Team | Barracks | 250 | A heavy machine gun: shreds infantry at 6.5 tiles. |
| AT Team | Barracks | 300 | Panzerschreck (Germany: big hit, short range) or anti-tank rifle (Russia: longer range, smaller hits). Hunts tanks from cover. |
| Engineer (Pioneer / Sapper) | Barracks | 200 | Unarmed. Repairs friendly vehicles and buildings: right-click them; idle engineers repair anything damaged nearby. |
| Sniper | Barracks (needs a Watchtower) | 400 | Sees 11 tiles, kills a man with almost every shot at 9.5 tiles. |
| Scout Car | Tank Works | 400 | Fast, sees far: spots for the guns. |
| Half-track | Tank Works | 450 | Armoured, quick, machine gun on a pivot. |
| Light Tank | Tank Works | 550 | 37 / 45 mm gun. Fast and cheap; loses to anything heavier. |
| Medium Tank | Tank Works (needs an Oil Depot) | 850 / 900 | The backbone: 75 / 76 mm gun, HE against infantry. |
| Tank Destroyer | Tank Works (needs an Oil Depot) | 1100 | No turret, heavy frontal armour, a long gun that out-ranges tanks. Must turn to fire. |
| Artillery | Tank Works (needs a Watchtower) | 1200 | Germany: 105 mm howitzer (18 tiles). Russia: rocket truck firing salvoes of 8 rockets (16 tiles) that blanket an area. Needs spotters. |
| Heavy Tank | Tank Works (needs an Oil Depot and a Watchtower) | 2000 | 88 / 122 mm gun at 10.5–11.5 tiles, armour that shrugs off light guns. |
| Supply Truck, Construction Truck | Tank Works | 1200, 3000 | Economy (see below). |

### Biomes, villages and towns

Random maps come in five biomes, each with its own ground, trees and light:

- **Temperate**: green fields, oak and pine woods, a river, hamlets of cottages.
- **Winter**: deep snow, snowy pines and dead trees, a frozen river.
- **Desert**: sand and gravel, rock outcrops, palms along a narrow green river,
  mud-brick houses.
- **Tropical**: dense jungle, palms, broad muddy rivers. No villages.
- **Urban**: a grid of streets filled with town houses, churches, cottages and
  ruins, debris everywhere.

Houses, churches and ruins belong to nobody. They block movement and line of
advance like any building, **infantry beside them take 30 % less damage**, and
nobody shoots them unless ordered to (right-click one to shell it). They show
grey on the minimap.

### The economy

- **Supplies** ($, top of the sidebar) pay for everything. They come from
  **oil**: dark stains on the ground. **Supply trucks** pump it out and drive it
  to an **Oil Depot**, about $500 a trip. Each depot comes with one truck; build
  more at the Tank Works. Right-click oil with a truck selected to send it
  there. Fields run dry, so expand to new ones.
- **Power.** Most structures use power; Power Stations make it (the bar under
  your supplies). Short of power, everything takes twice as long to build.
- **Building.** On the sidebar's **Build** tab, click a structure. It is paid
  for as it goes up; when it says READY, click it again and put it down near
  your other structures (a green footprint means it fits). Structures need
  others first: the Barracks needs a Power Station, the Tank Works an Oil Depot,
  and so on (hover a greyed-out button to see what's missing).
- **Training.** The **Train** and **Vehicles** tabs queue infantry at the Barracks
  and vehicles at the Tank Works (Shift-click for 5, right-click to cancel).
  Select a barracks or works and right-click the ground to set where new units
  go; that building becomes the one they come out of.
- **Construction Truck.** Built at the Tank Works; press **D** (or right-click it)
  to unfold it into a new Construction Yard wherever there is room.
- **Selling.** The **Sell** button, then click your structures, returns half their
  cost.
- **Defeat.** A side with no units and no structures left (trenches and walls
  don't count) is defeated. The last side standing wins.

### Fortifications and defences (Defend tab)

| | What it does |
| --- | --- |
| Trench | Dig a line (click, drag, release). Infantry in a trench take 60 % less damage. Wheeled vehicles can't cross it. |
| Sandbags | A low wall. Infantry right beside it take 40 % less damage. Blocks everything. |
| Barbed wire | Stops infantry; vehicles drive through. |
| Tank traps | Stop vehicles; infantry walk through. |
| MG Bunker | A machine-gun pillbox that shreds infantry. |
| Anti-Tank Gun Pit | A dug-in gun with a long reach against tanks. |
| Watchtower | Sees 13 tiles: spots for your guns. |

Fortifications are bought and placed on the spot, a tile at a time; bunkers,
guns and towers are built and placed like other structures.

### Moving and fighting

- **Formations.** Pick one under *Orders* on the sidebar (Box, Line, Column,
  Wedge, Loose) and it applies to every move. **Hold the right button and drag**
  to lay out the front line yourself: the units spread along it and face away
  from where they came from. In mixed groups, armour forms up in front, other
  vehicles behind it and infantry at the back. Units turn to face the front
  when they arrive.
- **Stances** (sidebar *Orders*):
  - **Aggressive** (the default): fight back when shot, even on the march,
    then carry on; go after attackers they can't reach.
  - **Hold**: return fire from where they stand, never move on their own.
  - **Hold fire**: don't shoot unless ordered to attack (ambushes).
- **Forced march: Alt + right-click.** Units ignore enemy fire until they
  arrive. Use it to retreat.

### How combat works

- **Weapons and armour.** Rifles and machine guns cut infantry down but can't
  hurt tanks; AT teams, anti-tank guns and tanks can. Riflemen are green troops: they miss about 3 shots in 10, so
  firefights take time. Tanks outrange everything (about 10 tiles) and hit
  hard: armour-piercing shells against vehicles, and high-explosive shells with
  a wide blast against infantry, so infantry without armour support gets
  crushed. Shells fly, scatter at long range and can miss.
- **Front, side and rear armour.** A shell into a tank's flank does about 1.6×
  damage; one into its rear does 4.5×, enough to knock out a medium tank in a
  single hit. Flanking pays.
- **Spotting.** Tanks see about 8 tiles, less than their gun range; scout cars
  see 11–12. Every unit shoots at whatever its side (and its allies) can see,
  so put scouts forward to let the tanks fire at full range.
- **Orders.** Right-click an enemy to attack it: your units drive into range,
  chase it if it moves, and stop when it dies. **Attack-move** (press **A**,
  then left-click the ground, or Ctrl+right-click) advances to a point and
  stops to fight anything met on the way. A plain move ignores the enemy
  (tanks still shoot on the move with their turrets; infantry don't).
- **Fog of war.** Black ground has never been seen; dimmed ground was seen
  before but isn't watched now. Enemy units are only shown where your units
  can see them, and they can only be shot when seen. **Tab** switches the side
  you command and the side whose view you see.
- **Aftermath.** Destroyed vehicles burn and smoke for about 20 seconds and
  stay on the field as blackened wrecks; fallen soldiers stay for a while;
  shells leave craters and scorch marks.

## Controls

| Input | Action |
| --- | --- |
| Left click | Select a unit (Shift: add or remove) |
| Left drag | Box-select your units |
| Double-click | Select all units of that type on screen |
| Right click | Move the selected units; on a visible enemy: attack it; on a civilian building: shell it; with trucks on oil: pump it; with engineers on a damaged friend: repair it; with a barracks or works selected: rally point |
| Right drag | Move into formation along the dragged front line |
| Alt+right click | Forced march (don't stop to fight) |
| D | Deploy a construction truck |
| A, then left click | Attack-move to that point (Esc or right click cancels) |
| Ctrl+right click | Attack-move |
| Ctrl+1…9 / 1…9 | Set / recall a control group |
| Ctrl+A | Select all your units |
| S | Stop |
| Tab | Switch which side you command (and see) |
| Sidebar minimap | Click/drag to look; right-click to send the selection |
| Arrow keys, screen edges | Scroll |
| Middle mouse drag | Pan |
| Mouse wheel | Zoom 0.5× / 1× / 2× |
| Home | Centre the view on the selection |
| Space | Pause |
| + / − | Game speed (0.25× to 8×) |
| Delete | Remove the selected units |
| Esc or F10 | Menu: save, load, replays, restart, quit |
| F1 | Show / hide the controls panel |
| F2 | Open / close the map editor |
| F3 | Debug panel: spawn tools (up to 10,000 units at once), reveal map, all health bars, simulation timings |
| F11 or Alt+Enter | Fullscreen |
| F12 | Screenshot |

Screenshots and the log file (`legion.log`) are saved in
`%APPDATA%\Makorain\LegionClassic\`.

## Live Scenario Editor (F2)

The editor opens as a panel on the left while the battle keeps running. Build
a scenario, drop in armies, set them fighting, and change anything while they
fight. Press Space (or untick *Paused* on the Battle tab) to freeze time. The
editor shows the whole map; tick *Show the fog of war while editing* on the
Battle tab to see what the side you command sees.

Every change to terrain and units goes into one undo history (100 steps):
**Ctrl+Z** undoes, **Ctrl+Y** redoes, even mid-battle. Undoing a placement
removes those units again; undoing a delete brings them back where they were.

### Terrain tab

| Tool | What it does |
| --- | --- |
| Oil fields | Lay oil for supply trucks (Shift: drain it) |
| Raise / Lower | Build hills or dig valleys and riverbeds |
| Smooth | Soften bumps and sharp edges |
| Flatten | Level the ground to the height where the stroke started |
| Roughen | Add natural unevenness |
| Paint ground | Grass, dry grass, dirt, mud, sand, rock, snow, dirt road; edges blend naturally |
| Place props | Click for one tree, bush, rock, palm, snowy pine, dead tree, jungle tree, scrub, debris or sandstone; drag to scatter |
| Erase props | Remove props inside the brush |
| Water level | Water fills every part of the map below this height |

### Units tab

- **Place units.** Choose the side, then a unit, building, defence or
  fortification from the palette (it lists that side's faction and the
  civilian buildings, which always belong to nobody; tick *Every faction* to
  mix, or filter by kind). Structures go down exactly on the grid,
  free and anywhere there's room. Pick how:
  - **One**: one unit per click.
  - **Brush**: drag to paint units across an area (size and density sliders).
  - **Block**: one click places a formed block of 2 to 10,000 units.
  
  Set the facing with the compass or **,** and **.**. A see-through preview
  follows the mouse; red means the unit can't stand there (water, cliffs).
- **Select & edit.** Click or box-select units of *any* side. Drag selected
  units to move them. Then:
  - **Delete** (or the Delete key), **Duplicate** (Ctrl+D), **Kill** (destroyed
    as in battle, with wrecks; can't be undone), **Stop**.
  - **Turn left / right** (or , and .).
  - **Give to** a side: units change sides on the spot.
  - **Set health** to any percentage.
- **Inspector.** With one unit selected: its type, side, health, position,
  facing, current order, route, target and weapon reload. With many: a count
  per type and side with their average health.

Right-click still gives orders while you edit. If the selection mixes sides,
each side's units get the order as their own.

### Sides tab

Up to 8 sides. For each: whether it's in the scenario, name, faction
(Germany or Russia), colour, team, starting supplies (*Give now* sets them in
the running battle too), and whether a human or the computer (easy, normal, hard) plays
it; the change takes effect at once. Sides on the same **team**
are allies: they share vision and never shoot each other. **Start view here**
sets where that side's camera starts. **Command this side** hands you control
of it (Tab also cycles through the sides).

### Battle tab

- **Scenario**: name, author and a briefing / description saved with the map.
- **Time**: pause and game speed.
- **Snapshot**: *Take snapshot* remembers the whole battle (terrain, every
  unit's position and health, the sides). *Restore* rewinds to it, so you can
  play a fight out, rewind, and try it again.
- **Battle tools**:
  - **Artillery**: click to call a 105 mm barrage onto the brush area
    (choose the number of rounds). It hurts every side.
  - **Smite**: destroys everything under the brush.
  - **Heal**: restores units under the brush to full health (undoable).
- **Clear wrecks, bodies and craters**, **Select all units**, **Heal all**.

### Script tab: triggers and objectives

Scenarios come alive with **triggers**: *when these conditions are all true, do
these actions*. Everything runs inside the simulation, so scripted scenarios
save, load and replay exactly.

- **Areas.** Draw named rectangles on the map (*Draw area*, then drag). Triggers
  refer to them by name. They're outlined on the map while the Script tab is
  open.
- **Conditions**: always (at the start); a timer (seconds since the start);
  a side has at least N units (of a type, or any) in an area; a side has no
  units in an area; a side has at most N units on the map; a side has been
  defeated; a side has at least N supplies; another trigger has fired.
- **Actions**: show a message; add an objective (main or optional), complete
  or fail it; victory or defeat for a side; spawn units in an area; give or
  take supplies; reveal an area; send a side's units in one area to attack-move
  or move to another; hand units in an area to another side; destroy units in
  an area; switch a trigger on or off; hand a side to the computer or a human;
  move a side's camera to an area.
- A trigger fires once, or **repeats** (at most once a second) while its
  conditions hold. Triggers can start switched off and be switched on by
  others, which chains a mission into stages.
- Objectives appear at the top left of the screen for the side they belong to,
  ticked (green) when done and marked [!] (red) when failed.

Edit triggers during play: the battle keeps what has already fired.

### Keys in the editor

| Input | Action |
| --- | --- |
| Left click / drag | Use the current tool |
| [ / ] | Smaller / larger brush |
| , / . | Turn the units being placed or the selection |
| Delete | Delete the selected units |
| Ctrl+D | Duplicate the selected units |
| Esc | While placing: back to Select |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+S | Save |

### Files

**New…** makes a flat or random map from 64 × 64 up to 1024 × 1024 tiles, in
any of the five biomes, with or without villages and towns.
**Save as…** and **Open…** use your maps folder,
`%APPDATA%\Makorain\LegionClassic\maps\`. Scenario files (`.lmap`) store the
terrain, water, props, every unit (position, facing, health), the sides and
the scenario details and its triggers, so you can share them.

## Command-line options

`--fullscreen`, `--windowed 1920x1080`, `--map 256` (map size in tiles, up to
1024), `--seed 1234` (different random map), `--biome winter` (temperate,
winter, desert, tropical, urban), `--zoom 2`, `--spawn 5000` (extra units at
start), `--demo` (the two armies attack each other), `--no-bases` (armies only,
no bases), `--debug` (open the debug panel), `--editor` (start with the editor
open), `--open path\to\map.lmap` (start on a saved map), `--menu` (the main
menu; the default when started with no options), `--ai-battle` (watch two
computer players), `--gallery` (every unit and building lined up, for looking
at the art), `--export-sprites folder` (write every sprite as a PNG for
repainting; see [ART_PIPELINE.md](ART_PIPELINE.md)).

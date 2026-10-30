# Playing Legion Classic (Milestone 4 build)

Double-click `LegionClassic.exe`. No installation is needed; keep the `content`
folder next to the exe.

Windows may show a SmartScreen warning the first time because the game isn't
code-signed yet: choose **More info → Run anyway**.

## What's in this build

A sandbox on a random 128 × 128 countryside map with rolling hills, a river,
dirt roads and woods: a German force (red) in the west and a Russian force
(blue) in the east. Both armies can fight now. Tanks carry a main gun and a
machine gun on a turret that turns on its own; riflemen and scout cars shoot
anything in range. Units find their way around rivers, woods' edges and
cliffs, keep their spacing, and only see what is near them: the rest of the
map lies under the fog of war. Press **F2** to open the Live Scenario Editor
while the battle keeps running: place and edit armies for any side, set up
sides and teams, call in artillery, and save it all as a scenario.

### How combat works

- **Weapons and armour.** Rifles and machine guns cut infantry down but can't
  hurt tanks. Riflemen are green troops: they miss about 3 shots in 10, so
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
| Right click | Move the selected units; on a visible enemy: attack it |
| A, then left click | Attack-move to that point (Esc or right click cancels) |
| Ctrl+right click | Attack-move |
| Ctrl+1…9 / 1…9 | Set / recall a control group |
| Ctrl+A | Select all your units |
| S | Stop |
| Tab | Switch which side you command (and see) |
| Arrow keys, screen edges | Scroll |
| Middle mouse drag | Pan |
| Mouse wheel | Zoom 0.5× / 1× / 2× |
| Home | Centre the view on the selection |
| Space | Pause |
| + / − | Game speed (0.25× to 8×) |
| Delete | Remove the selected units |
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
| Raise / Lower | Build hills or dig valleys and riverbeds |
| Smooth | Soften bumps and sharp edges |
| Flatten | Level the ground to the height where the stroke started |
| Roughen | Add natural unevenness |
| Paint ground | Grass, dry grass, dirt, mud, sand, rock, snow, dirt road; edges blend naturally |
| Place props | Click for one tree, bush or rock; drag to scatter |
| Erase props | Remove props inside the brush |
| Water level | Water fills every part of the map below this height |

### Units tab

- **Place units.** Choose the side, then a unit from the palette (it lists
  that side's faction; tick *Show every faction's units* to mix). Pick how:
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
(Germany or Russia), colour, team, and whether a human or the computer plays
it (the computer player arrives in Milestone 6). Sides on the same **team**
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

**New…** makes a flat or random map from 64 × 64 up to 1024 × 1024 tiles.
**Save as…** and **Open…** use your maps folder,
`%APPDATA%\Makorain\LegionClassic\maps\`. Scenario files (`.lmap`) store the
terrain, water, props, every unit (position, facing, health), the sides and
the scenario details, so you can share them.

## Command-line options

`--fullscreen`, `--windowed 1920x1080`, `--map 256` (map size in tiles, up to
1024), `--seed 1234` (different random map), `--zoom 2`, `--spawn 5000`
(extra units at start), `--demo` (the two armies attack each other),
`--debug` (open the debug panel), `--editor` (start
with the editor open), `--open path\to\map.lmap` (start on a saved map).

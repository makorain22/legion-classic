# Playing Legion Classic (Milestone 2 build)

Double-click `LegionClassic.exe`. No installation is needed; keep the `content`
folder next to the exe.

Windows may show a SmartScreen warning the first time because the game isn't
code-signed yet: choose **More info → Run anyway**.

## What's in this build

A sandbox on a random 128 × 128 countryside map with rolling hills, a river,
dirt roads and woods: a German force (red) in the west and a Russian force
(blue) in the east. You can select and move either army, and press **F2** to
open the map editor while the battle keeps running. Units stop at cliffs and
deep water; pathfinding around obstacles and combat come in Milestone 3.

## Controls

| Input | Action |
| --- | --- |
| Left click | Select a unit (Shift: add or remove) |
| Left drag | Box-select your units |
| Double-click | Select all units of that type on screen |
| Right click | Move the selected units |
| Ctrl+1…9 / 1…9 | Set / recall a control group |
| Ctrl+A | Select all your units |
| S | Stop |
| Tab | Switch which side you command |
| Arrow keys, screen edges | Scroll |
| Middle mouse drag | Pan |
| Mouse wheel | Zoom 0.5× / 1× / 2× |
| Home | Centre the view on the selection |
| Space | Pause |
| + / − | Game speed (0.25× to 8×) |
| Delete | Remove the selected units |
| F1 | Show / hide the controls panel |
| F2 | Open / close the map editor |
| F3 | Debug panel and spawn tools (up to 10,000 units at once) |
| F11 or Alt+Enter | Fullscreen |
| F12 | Screenshot |

Screenshots and the log file (`legion.log`) are saved in
`%APPDATA%\Makorain\LegionClassic\`.

## Map editor (F2)

The editor opens as a panel on the left; the world keeps running (tick
"Pause the battle" or press Space to freeze it). Right-click still orders
your selected units while you edit.

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

| Input | Action |
| --- | --- |
| Left drag | Use the current tool |
| [ / ] | Smaller / larger brush |
| Ctrl+Z / Ctrl+Y | Undo / redo (100 steps) |
| Ctrl+S | Save |

**New…** makes a flat or random map from 64 × 64 up to 1024 × 1024 tiles.
**Save as…** and **Open…** use your maps folder,
`%APPDATA%\Makorain\LegionClassic\maps\`. Map files (`.lmap`) store the
terrain, water, props and every unit on the map, so you can share them.

## Command-line options

`--fullscreen`, `--windowed 1920x1080`, `--map 256` (map size in tiles, up to
1024), `--seed 1234` (different random map), `--zoom 2`, `--spawn 5000`
(extra units at start), `--debug` (open the debug panel), `--editor` (start
with the editor open), `--open path\to\map.lmap` (start on a saved map).

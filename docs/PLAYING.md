# Playing Legion Classic (Milestone 1 build)

Double-click `LegionClassic.exe`. No installation is needed; keep the `content`
folder next to the exe.

Windows may show a SmartScreen warning the first time because the game isn't
code-signed yet: choose **More info → Run anyway**.

## What's in this build

A sandbox on a random 128 × 128 countryside map: a German force (red) in the
west and a Russian force (blue) in the east. You can select and move either
army. Combat, terrain heights and the Live Scenario Editor come in later
milestones.

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
| F3 | Debug panel and spawn tools (up to 10,000 units at once) |
| F11 or Alt+Enter | Fullscreen |
| F12 | Screenshot |

Screenshots and the log file (`legion.log`) are saved in
`%APPDATA%\Makorain\LegionClassic\`.

## Command-line options

`--fullscreen`, `--windowed 1920x1080`, `--map 256` (map size in tiles, up to
1024), `--seed 1234` (different random map), `--zoom 2`, `--spawn 5000`
(extra units at start), `--debug` (open the debug panel).

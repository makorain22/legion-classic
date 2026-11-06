# Modding Legion Classic

Everything a battle is made of comes from data: unit, building and weapon
rules, sprites and scenarios. A **mod** is a folder that changes some of that
data. The game itself is never edited; mods sit on top of it and can be
switched on and off from the main menu.

You can make mods with the two **creation tools** in the game (the rules
editor and the sprite studio) or by hand with any text editor and paint
program. Both make the same files.

## Where mods live

| Folder | What goes there |
| --- | --- |
| `mods\` next to `LegionClassic.exe` | Mods that come with the game (the example mod). |
| `%APPDATA%\Makorain\LegionClassic\mods\` | Your own mods. The creation tools save here. A mod here with the same folder name as one beside the exe replaces it. |

**Main menu → Mods and creation tools** lists every mod. Tick the ones you
want, put them in order with the arrows, and press **Apply**: the game reloads
its rules and art and starts a fresh battle behind the menu. Your choice is
remembered. **Open mods folder** opens your own mods folder; **Create mod**
makes an empty one.

## What a mod folder holds

```
my_mod\
  mod.json            name, version, author, description, requires
  rules\
    units.json        changes to units           (all three are optional)
    buildings.json    changes to buildings
    weapons.json      changes to weapons and warheads
  art\                PNG sprites that replace the game's (same names as --export-sprites)
    sprites.json      anchors for those PNGs (optional)
  scenarios\          .lmap missions added to "Play a scenario"
```

`mod.json`:

```json
{
  "name": "Heavy Armour",
  "version": "1.0",
  "author": "you",
  "description": "Tanks take longer to kill.",
  "requires": []
}
```

`requires` lists the folder names of mods that must load first. The game puts
each mod after the ones it needs and says so if one is missing. A mod whose
`mod.json` can't be read is listed as broken and can't be enabled.

## How rule files merge

A mod's rule file says **only what it changes**. Entries are matched by `id`
(units, buildings, weapons) or by name (warheads):

```json
{
  "units": [
    { "id": "ger_heavy_tank", "health": 4000 },
    { "id": "rus_sniper", "_remove": true },
    { "id": "ger_veteran", "name": "Veteran Grenadier", "model": "rifleman", ... }
  ]
}
```

* An entry with an existing id changes just the fields it names. Everything
  else stays as the game (or an earlier mod) had it.
* Lists inside an entry (`weapons`, `requires`, `blocks`) are replaced whole.
* Objects inside an entry (`turret`, a warhead's `facing`) merge field by field.
* `"_remove": true` deletes the entry. On a field, `{"_remove": true}` clears
  that field back to its default.
* A new id adds a new unit, building or weapon.
* Mods load in the order shown in the Mods page; where two change the same
  field, the later one wins.

Every field is explained in the `_comment` at the top of the game's own
`content\rules\*.json`, and in the rules editor (hover a field's name).

The game checks the merged rules when it loads them: an unknown weapon, a
`requires` naming a building that doesn't exist, a model with no art, a
duplicate id. A broken mod never stops the game starting: the mods are switched
off for that run and the Mods page says why.

## The rules editor

Open it from **Mods and creation tools**, or from the Live Scenario Editor
(F2 → **Battle** tab → **Rules editor**) while you play.

* Four tabs: **Units**, **Buildings**, **Weapons**, **Warheads**. Search and
  filter by side on the left; **New**, **Copy** and **Delete** below the search.
* The form shows every field with what it means (hover the name). Fields not
  set show the default in grey; the **–** button beside a field clears it back
  to the default.
* Fields you changed turn gold; hover one to see what it was. In the list, `*`
  marks a changed entry and `+` a new one.
* **What it adds up to** works out the numbers that matter: damage per second
  against each kind of armour (with the HE round a gun switches to against
  infantry), range, speed in tiles per second, health per 100 supplies, and
  what needs or carries it.
* **Live** (on by default) puts every change into the running battle as soon
  as the rules check out: change a tank's armour and the next shell that hits
  it uses the new number. **Apply to battle** does it by hand.
  * Units already on the map get the new health cap, guns, turrets and supply
    loads at once.
  * A unit or weapon deleted mid-battle stays for what is already on the map,
    but nobody can build it any more. It goes in the next battle.
  * A building's footprint and what it blocks change in the next battle, not
    under buildings already standing (the bar at the bottom says so).
  * A battle whose rules were changed can't be saved as a replay.
* **Save to mod** writes only the differences from the rules below that mod
  into its `rules\` folder. Pick one of your own mods in the **Mod** box, or
  type a name and press **Make**. If the mod isn't enabled yet, it is switched
  on from the next start or mod change.
* **Undo all changes** goes back to the rules as they were when you opened the
  editor.

## The sprite studio

Open it from **Mods and creation tools** or the editor's Battle tab.

* **Browse** every sprite set on the left: each unit and building per side
  (and per climate, where the art has winter or desert paint), then props,
  effects, ground decals and sidebar icons.
* **Unit view**: turn it through its 16 facings (**Turn** spins it), step or
  **Play** its animation, see a soldier **Fallen**, in any side's colour, on
  grass, snow, sand, dark or a checkerboard, at 1× to 6×. The yellow cross is
  the anchor: the point that stands on the unit's position.
* **Whole sheet** shows every frame of the set; hover a frame for its file
  name, size and anchor, click it to look at it closely.
* **Recolour**: hue, saturation and brightness for the whole set, with
  *Winter whitewash* and *Desert sand* to start from. Side colours are never
  changed.
* **Ground anchor**: nudge the set up, down and sideways, if it floats or sinks.
* **Save recolour and anchor** writes the set, recoloured and moved, into the
  chosen mod's `art\` folder and rebuilds the art.
* **Export for painting** writes the set as PNG files into the mod's `art\`
  folder (with `sprites.json`). Paint over them in any program, keeping the
  names, then press **Reload art**.
* **Import a PNG for this frame…** puts a picture of your own in place of the
  frame on show.

Art only shows in the game while its mod is enabled. Team colour in PNG files
and anchors work as described in [ART_PIPELINE.md](ART_PIPELINE.md).

Sprites from the Blitzkrieg art pack are licensed for personal, non-commercial
use: keep mods made from them to yourself.

## Saves, replays and mods

Saved games and replays record the mods they were made with and a fingerprint
of the rules.

* Loading a **saved game** needs the same mods enabled; the game says which.
  If a mod was edited since, the game still loads and units take the new rules.
* A **replay** needs exactly the same rules, or it would play out differently;
  it is refused otherwise.

## A worked example

`mods\example_veterans` comes with the game. It makes grenadiers cost 12
instead of 10, makes rifles a little more accurate, and adds a **Veteran
Grenadier** that the barracks trains once you have a watchtower. Its whole
rules folder is two short files. Enable it in the Mods page to try it.

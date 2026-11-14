# Modding Legion Classic

Everything a battle is made of comes from data: unit, building and weapon
rules, sprites and scenarios. A **mod** is a folder that changes some of that
data. The game itself is never edited; mods sit on top of it and can be
switched on and off from the main menu.

You can make mods with the two **creation tools** in the game (the rules
editor) or by hand with any text editor and paint
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

## Editing while the game runs (new in 0.9.2)

Leave the game open beside your text editor. When you save a `rules\*.json`
file of the game or of a mod that is switched on, the battle on screen takes
the change in within a couple of seconds ("Rules reloaded from disk"), the
same way *Apply* in the rules editor does. A file that can't be read (a
missing bracket, a half-finished save) is reported and nothing changes.
Sounds (`audio\`), campaigns (`campaigns\`) and sprite overrides (`art\`)
reload too. `content.hotreload 0` in the console switches this off;
`content.check` looks at once instead of waiting.

## A mod as one file: packs (new in 0.9.2)

To hand a mod to somebody, pack it: **Mods → pick the mod → Pack into one
file**, or `mod.pack my_mod` in the console. That writes
`%APPDATA%\Makorain\LegionClassic\exports\my_mod-1.0.pnpack`. Whoever gets
it drops the file into their mods folder and ticks it on the Mods page like
any other mod; nothing needs unpacking.

- A pack is a zip with a `pack.json` listing every file with its size and
  checksum. `pack.verify <file>` in the console tells whether a pack is whole;
  `pack.info <file>` shows what it says about itself; `pack.extract <file>
  <folder>` turns it back into a folder.
- A plain `.zip` of a mod folder works as well (with the folder itself inside
  the zip or without).
- If your mods folder holds both `my_mod\` and `my_mod.pnpack`, the folder is
  used: it is the copy you can edit.
- A pack cannot be changed by the creation tools. Unpack it into a folder to
  work on it.

**Saying what a mod needs** (in `mod.json`; packing copies it into the pack):

```json
{"name": "Elite Infantry", "version": "2.1",
 "requires": ["example_veterans >= 1.2", "my_maps"],
 "engine": ">=2.0"}
```

`requires` lists other mods by id, each optionally with a version: `>= 1.2`
(at least), `= 1.0` (exactly), `^1.2` (1.2 or newer, but not 2.0), `~1.2`
(1.2.x). A required mod that is not switched on, or too old, is named on the
Mods page; `engine` keeps a mod made for a newer version of the game from
loading on an older one.

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
  campaigns\<id>\     a campaign: campaign.json, mission .lmap files, briefings (.md)
  maps\skirmish\      ready-made skirmish maps and their maps.json
  audio\              sounds and music that replace the game's (same paths as content\audio)
                      and an audio.json to change or add sounds
```

A campaign folder holds `campaign.json`:

```json
{"id": "my_war", "title": "My War", "faction": "gbr", "order": 10,
 "description": "Shown on the Campaigns page.",
 "missions": [{"id": "m1", "title": "First Day", "map": "m1.lmap", "briefing": "m1.md",
               "place": "Normandy", "date": "June 1946",
               "act": "Act I - The Landings", "formation": "2nd Battalion"}]}
```

Missions are ordinary scenarios made in the editor (Script tab). A mission is
won with the *Victory* action; *Set campaign flag* stores a number that later
missions can test with *Campaign flag at least*. Briefings are Markdown: `#`
title, `##` headings, `-` bullets, `>` quotes. A mod's campaign with the same
id as the game's replaces it.

A mission can also carry `"act"` (the part of the story it belongs to: the
Missions page lists missions under their act) and `"formation"` (who the
player commands in it), both shown on the list and the briefing. The player
picks a difficulty on the briefing page (not for a campaign marked
`"tutorial": true`): enemy computer sides play a step easier or harder than
the map says, and *Spawn units* actions ticked "more on hard, fewer on easy"
bring 70% / 100% / 135% of their number.

## Making missions: the trigger language (Milestone 9.9)

A mission's script is a list of **triggers**: *when* its conditions hold
(all of them, or any one if the trigger is marked "any one"), *then* its
actions run in order. A trigger fires once, or repeats (at most once a
second). Any condition can be negated ("not"). Everything runs inside the
simulation, so missions save, load and replay exactly. The script is stored in
the map as JSON (ids below); the editor's Script tab edits all of it, with
every line written out as a sentence.

**Named units and set pieces.** Select units in the editor and use the
inspector's *Mission* box: give them a name (several can share one), and tick
*Invulnerable* (takes no damage until a script lets it), *Not for the
computer* (the computer player never commands it), *Not a target* (enemies
never pick it) or *Locked from orders* (its own player can't command it), and
a stance. These are saved with the map (`"name"`, `"flags"`, `"stance"` on a
unit) and are in place from the first tick. Scripts can do the same at any
time. This is how a mission sets a scene: tanks that can't die shelling a
village while the story is told, released to fight (and die) when the
counter-attack comes.

Actions marked *units* pick their units **by name** (text field) or **by
side, area and unit type**.

**The full reference is [SCRIPT_REFERENCE.md](SCRIPT_REFERENCE.md)**: every
condition and action with its fields, an example and the usual mistakes,
written by the game from its own registry (the tables below are the short
form). The same help is in the editor: hover a line in the "+" menus, or
open *Script reference* in the Script tab. *Check the script* there looks
for mistakes, including places units can't reach.

**Beyond triggers (new in 0.9.2).** A script can also hold **phases**
(stages of the mission, with branches; triggers can belong to one),
**goals** (objectives the game keeps score of: destroy, reach, clear, hold,
survive, escort, protect), **waves** (reinforcements with counts for each
difficulty and orders on arrival) and **paths** (routes units follow), and a
**Lua script** for whatever triggers can't say. They are described in the
"mission model" and "Lua" chapters of the reference. Until the Mission Maker
(Milestone 9.14) brings screens for them, they are written in the map file's
script JSON (`phases`, `goals`, `waves`, `paths`, `lua`).

### Conditions

| Id | Holds when |
| --- | --- |
| `always` | from the start |
| `timer` | `value` seconds have passed (since the trigger last fired, if it repeats) |
| `trigger_fired` | trigger `trigger` has fired, at least `value` seconds ago |
| `countdown_done` | the countdown `text` has run out |
| `countdown_below` | the countdown `text` is running with at most `value` seconds left |
| `counter_at_least` / `counter_at_most` | mission counter `text` is at least / at most `value` |
| `flag_at_least` | campaign flag `text` is at least `value` |
| `objective_done` / `objective_failed` | objective number `value` (from 0) is done / failed |
| `difficulty_at_least` | the mission is played at `value` (0 easy, 1 normal, 2 hard) or harder |
| `chance` | `value` percent of the times it is checked (deterministic: replays the same) |
| `units_in_area` | `player` has at least `value` units (of `unit`, or any) in `area` |
| `no_units_in_area` | `player` has none |
| `units_in_area_at_most` | `player` has at most `value` |
| `area_controlled` | `player` and its allies have at least `value` units in `area` and no enemy has any |
| `unit_count_at_least` / `unit_count_at_most` | on the whole map |
| `structures_at_least` / `structures_at_most` | structures (of `unit`, or any but field works) in `area` (-1: the map) |
| `health_below` | `player`'s units in `area` have less than `value` percent of their health in total |
| `in_cover` | `player` has at least `value` men in buildings, bunkers or trenches |
| `named_alive` / `named_dead` | a unit named `text` is alive / none is |
| `named_in_area` | at least `value` units named `text` are in `area` |
| `named_health_below` | a unit named `text` has less than `value` percent of its health |
| `player_defeated` | `player` is defeated |
| `supplies` / `supplies_at_most` | `player` has at least / at most `value` supplies |
| `kills_at_least` / `losses_at_least` | `player` has destroyed / lost at least `value` units and structures |
| `allied_with` | `player` is allied with player `value` |
| `order_given` | a human `player` has given `value` orders of kind `order` (tutorials) |

### Actions

| Id | Does |
| --- | --- |
| `message` | shows `text` to `player` |
| `transmission` | a radio message: `text` = `"Speaker: words"`, shown `value` seconds (0: long enough to read), typed out with a radio sound; kept in the radio log (L) |
| `add_objective` | objective `text` (optional if `value` 1), marked on the map at `area` while open |
| `complete_objective` / `fail_objective` / `hide_objective` | objective number `value` |
| `victory` / `defeat` | `player` (and its allies) win / `player` loses |
| `hint` / `highlight` | the tutorial panel / a glowing interface element |
| `cinematic` | letterbox and no orders for `player` (`value` 1), or back to play (0) |
| `camera_to` / `camera_pan` | cut / glide `player`'s view to `area` (over `value` seconds) |
| `mark_area` / `clear_marker` | a marker named `text` on `area` and the minimap (`value` 0 goal, 1 danger, 2 note); clear one (empty: all) |
| `play_sound` | a sound from audio.json, at `area` or everywhere |
| `play_music` | `calm`, `battle`, `victory`, `defeat`, `menu` or a piece by title; held until a `play_music` with empty text |
| `spawn_units` | `value` of `unit` for `player` in `area` (`"scaled": true`: by difficulty) |
| `spawn_named` | the same, and they are called `text` |
| `spawn_from_flag` | as many `unit` as campaign flag `text` says (at most `value`): survivors carried over |
| `move_units` / `attack_move` | *units* go to `area2` |
| `patrol` | *units* walk between `area` and `area2` until given other orders |
| `garrison` / `evacuate` | infantry (*units*) go into the buildings and trenches in `area2` / men in `area` come out |
| `bind_units` | *units* bind into a fighting unit |
| `set_stance` | *units* take stance `value` (0 aggressive, 1 hold ground, 2 hold fire) |
| `change_owner` | *units* go over to `player2` |
| `kill_units` / `damage_units` / `set_health` | destroy / take `value`% of maximum health / set health to `value`% (these hurt invulnerable units too) |
| `name_units` | `player`'s units in `area` (of `unit`) are called `text` (at most `value`; 0: all) |
| `set_invulnerable` / `set_ai_control` / `set_targetable` / `set_locked` | the set-piece switches on *units* (`value` 1: invulnerable / the computer may command them / a target / locked) |
| `ai_attack` / `ai_defend` / `ai_free` | the computer playing `player` sends its main force at `area` / holds and defends `area` / chooses for itself again |
| `ai_behaviour` | the computer may (`value` 1) or may not (0) `build`, `train`, `attack`, `raid`, `capture`, `fortify`, `scout` or `repair` |
| `ai_wave_size` | it attacks with `value` percent of its usual force (0: usual) |
| `set_computer` | `player` is played by the computer (`value` difficulty) or a human (-1) |
| `set_alliance` | `player` becomes an ally (`value` 1) or an enemy (0) of `player2`: betrayals, defections |
| `set_play_area` | the battle is confined to `area` (-1: the whole map) for `player`: orders, building and the camera stay inside, the rest of the ground is dimmed. Grow it as the operation develops |
| `reveal_area` | `player` sees `area` for good |
| `barrage` | `value` shells of weapon `text` (default `howitzer_105`) on `area`, fired by `player` |
| `freeze_water` / `break_ice` | the water in `area` freezes (men and wheels cross it, tracks can't) / the ice in `area` breaks and whatever stands on it goes under |
| `flood_area` / `drain_area` | the land in `area` floods, waist deep (men wade it, vehicles can't cross it) / is pumped dry again to its old height |
| `add_logistics_point` | a new logistics point in the middle of `area` (an airdrop, a depot found) |
| `give_supplies` / `set_income_rate` | supplies to `player` / its income at `value` percent |
| `lock_build` / `unlock_build` | `player` can't / can build `unit` (any: everything) |
| `countdown` / `stop_countdown` / `pause_countdown` | a clock on screen labelled `text`, `value` seconds; take it away; pause (`value` 1) or run on |
| `set_counter` / `add_counter` | mission counter `text` |
| `set_flag` / `add_flag` | campaign flag `text` (kept when the mission is won) |
| `enable_trigger` / `disable_trigger` | switch trigger `trigger` on or off |
| `fire_trigger` / `reset_trigger` | run trigger `trigger`'s actions now / let it fire again |

The Script tab's *Mission debugger* shows every trigger with when it fired,
the clocks, counters and flags, what each computer player has been told and
the named units, while the battle runs. *The Crossing at Dawn* (in the
scenarios folder) uses most of the above; open it in the editor to see how.

Maps needn't be square: *New...* in the editor takes a width and a height
of your own (steps of 32 tiles), for missions along a road, a coast or a
valley.

**Landmarks for stories.** Besides the civilian houses, the editor has
buildings made for missions: `neutral_radio_mast`, `neutral_hangar`,
`neutral_transport_plane`, `neutral_steelworks`, `neutral_ministry` (a
garrison of forty), `neutral_derrick`, `neutral_refinery`, and the Reich's
`ger_flak_tower` (heavy guns, a garrison of twenty, 9000 health; it has no
`builtAt`, so nobody can build it). Give any of them to a side and name it,
and a script can make it a target, a prize or something to protect. The
Germany campaign's 25 missions are worked examples: open any of them from
`campaigns/ger/` in the editor.

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

## Armour classes (changed in 0.9.1, Milestone 9.4)

A unit's `armor` is one of `infantry`, `soft` (cars, carriers, lorries,
self-propelled guns), `light`, `medium`, `heavy` (tanks by weight; casemate
tank destroyers are `heavy`) or `structure`. Each warhead gives a percentage
against each. `soft` is new: before 9.4 cars were `light`, light tanks
`medium` and medium tanks `heavy`. A mod written for the old classes still
loads (a warhead without a `soft` column uses its `light` one for soft
vehicles), but its units keep the class they name, so check them against the
new meanings. Armour-piercing guns now use one of three warheads: `ap_light`,
`ap` or `ap_heavy`.

## Income, logistics and levies (new in 0.9.1, Milestone 9.6)

These keys in `buildings.json` and `units.json` drive the economy:

| Key | On | Meaning |
| --- | --- | --- |
| `income` | a structure | Supplies a minute for as long as it stands. |
| `perYard` | a structure | How many of it a side may have for each Construction Yard it owns (0 or absent: no limit). |
| `aura` | a structure | `{ "radius": tiles, "fireRate": percent }`: friendly units within the radius fire that much faster. The best aura in reach counts; they don't add up. |
| `role` | a structure | `post`, `warehouse`, `fuel` are the income buildings (the computer builds them by role); `office` is the building that goes up on a captured logistics point (one per army, `cost` 0, never built from the sidebar); `yard` is where convoys pay in. |
| `convoy` | an `office` | How many trucks an office keeps running. |
| `freeUnit` | a structure | The unit that arrives with it (an office gets `convoy` of them). |
| `capacity` | a unit | A supply truck: the supplies it pays in each trip. A unit with a `capacity` runs convoys. |
| `builtAt` | a unit | Now also `post` (the Post Office: levy infantry) and `warehouse` (the Logistics Warehouse: supply trucks), besides `barracks`, `factory` and `yard`. |

A mod that still has a building with the old role `depot` loads, but nothing
earns on a supply point any more: give it an `income` and a `perYard`
instead. Maps keep their `supplyPoints` list; those are the logistics points
now.

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

## Sprites (the classic view)

The sprite studio was removed in 0.9.1: the battle is now drawn in 3D from the
voxel models, so a repainted sprite only shows in the classic 2D view and on
the sidebar's pictures. Sprite overrides in a mod's `art/` folder still load
(see [ART_PIPELINE.md](ART_PIPELINE.md)), and `--export-sprites` still writes
the sprites out for painting.

## The console, for testing a mod

Press **`** or **F9** in the game. For mod work the useful lines are:

| Type | To |
| --- | --- |
| `cheats 1` | Allow the testing commands below |
| `spawn my_new_tank 4` | Put units under the mouse (Tab lists every unit and building id, your mod's included) |
| `credits 20000`, `side 1`, `reveal` | Give a side supplies, command another side, see everything |
| `speed 8`, `pause`, `restart` | Run the battle fast, stop it, start again |
| `log.level content debug` | Make the content loader say more in the log |
| `vfs.which rules/units.json` | Show every copy of a file, the game's and each mod's; the last one listed is the one in use |
| `assets.info campaigns/my_war/campaign.json` | Show what a file uses and what uses it; a map or briefing the campaign names but that is not there is marked MISSING |
| `mod.list`, `mod.pack my_mod` | List the mods; pack one into a single file |
| `bind f6 "spawn my_new_tank 4"` | Put a line you type often on a key |

Lines you want at every start (binds, `cheats 1`) go in `autoexec.cfg` in the
user folder. The full list of commands is in [ENGINE.md](ENGINE.md).

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

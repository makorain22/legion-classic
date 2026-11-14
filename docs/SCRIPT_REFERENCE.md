# Script reference

Every condition and action a Legion Classic mission can use. This file is written by the game from its own script registry (`engine/src/sim/script_registry.cpp`): don't edit it by hand, run the tests with `LEGION_WRITE_DOCS=1` instead.

A trigger fires when all its conditions hold (or any one, if set so) and then runs its actions in order, once, or again and again if it repeats. Any condition can be turned round with *not*. The names in `code` are the ids used in map files and in Lua.

## Conditions

### Time and chance

#### Always (`always`)

True from the first moment of the mission.

*Example:* A trigger with only this condition runs its actions at the start: the opening radio message, the first objectives.

*Watch out:* On a repeating trigger it fires once a second for ever.

#### Timer (`timer`)

True once this many seconds have passed since the mission began; on a repeating trigger, since it last fired.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Seconds | `value` | seconds | How long to wait. |

*Example:* Timer 300: the counter-attack comes five minutes in. Repeating with Timer 90: a patrol every minute and a half.

*Watch out:* It counts from the start of the mission, not from when the trigger was switched on. To count from an event, add 'Trigger fired' with a delay, or use a countdown.

#### Chance (`chance`)

True this share of the times it is checked. The dice are the simulation's, so a replay rolls the same.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Percent | `value` | percent | How often it is true. |

*Example:* Timer 120, repeating, Chance 50: every two minutes, a raid half the time.

*Watch out:* On a trigger that doesn't repeat it is rolled three times a second until it comes up, which is soon: pair it with a repeating timer.

### Units

#### Units in area (`units_in_area`)

True while the side has at least this many units in the area.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it counts, or every side together. |
| Area | `area` | area | The area it looks at. |
| At least | `value` | number | How many units. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |

*Example:* Side 0 has 5 units in 'Bridge': the bridge is reached.

*Watch out:* Buildings in the area count too (field works don't) unless a unit type is chosen. Allies of the side are not counted: add a condition for each, with 'Any one'.

#### No units in area (`no_units_in_area`)

True while the side has no unit (of the type) in the area.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it counts, or every side together. |
| Area | `area` | area | The area it looks at. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |

*Example:* The enemy has no units in 'Village': the village is cleared.

*Watch out:* True at once if the side never had units there. Buildings count as units unless a unit type is chosen: a side with a house in the area still has something there.

#### Unit count at most (`unit_count_at_most`)

True while the side has at most this many units on the whole map.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it counts, or every side together. |
| At most | `value` | number | How many units. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |

*Example:* Side 1 has at most 0 units: the enemy force is destroyed. At most 3 tanks: time to send the reserve.

*Watch out:* Buildings count too (field works don't) unless a unit type is chosen, so 'at most 0' waits for the last building as well. A side that has not arrived yet has 0: it is true before the first wave lands.

#### Unit count at least (`unit_count_at_least`)

True while the side has at least this many units on the whole map.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it counts, or every side together. |
| At least | `value` | number | How many units. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |

*Example:* Side 0 has 40 'ger_rifleman': the build-up is complete.

*Watch out:* Buildings count too (field works don't) unless a unit type is chosen.

#### Units in area at most (`units_in_area_at_most`)

True while the side has at most this many units in the area.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it counts, or every side together. |
| Area | `area` | area or none | An area, or none for the whole map. |
| At most | `value` | number | How many units. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |

*Example:* The enemy has at most 2 units in 'Ridge': the ridge is as good as taken.

*Watch out:* Buildings count too (field works don't) unless a unit type is chosen.

#### Area controlled (`area_controlled`)

True while the side and its allies have at least this many units in the area and no enemy has any.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| Area | `area` | area | The area it looks at. |
| At least | `value` | number | How many units. |

*Example:* Side 0 controls 'Crossroads' with 4 units: hold it for the clock to run.

*Watch out:* A single enemy straggler hiding in the area keeps it false.

#### Health below (`health_below`)

True while the side's units (of the type) in the area have, together, less than this share of their full health.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it counts, or every side together. |
| Area | `area` | area | The area it looks at. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| Below | `value` | percent | Percent of their full health in total. |

*Example:* The garrison in 'Fort' is below 40%: it surrenders.

*Watch out:* Also true when there are no such units in the area (none left).

### Sides

#### Player defeated (`player_defeated`)

True once the side has been defeated (nothing left, or a script said so).

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |

*Example:* Side 2 is defeated: the allied garrison has fallen, fail the objective.


#### Supplies at least (`supplies`)

True while the side has at least this many supplies.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| At least | `value` | number | Supplies. |

*Example:* Side 0 has 3000 supplies: enough is stockpiled for the offensive.


#### Kills at least (`kills_at_least`)

True once the side has destroyed at least this many enemy units and structures.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| At least | `value` | number | Kills. |

*Example:* Side 0 has 30 kills: the enemy calls in armour.

*Watch out:* Field works are not counted.

#### Losses at least (`losses_at_least`)

True once the side has lost at least this many units and structures.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| At least | `value` | number | Losses. |

*Example:* Side 0 has lost 40: the attack is called off.

*Watch out:* Field works are not counted.

#### Supplies at most (`supplies_at_most`)

True while the side has at most this many supplies.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| At most | `value` | number | Supplies. |

*Example:* Side 0 has at most 200: send the supply convoy.


#### Allied with (`allied_with`)

True while the side is allied with another.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| With side | `value` | side 0-7 | The other side. |

*Example:* Side 2 is allied with side 0: the mutineers have come over.


### Mission state

#### Trigger fired (`trigger_fired`)

True once another trigger has fired, at least so many seconds ago.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Trigger | `trigger` | trigger | The trigger that must have fired. |
| Seconds ago | `value` | seconds | How long ago, at least (0: as soon as it has). |

*Example:* 'Bridge taken' fired 20 s ago: the enemy answers twenty seconds after the bridge falls.

*Watch out:* It stays true after the other trigger has fired (for one that repeats, the delay counts from its latest firing). Triggers are referred to by number: deleting or reordering triggers by hand in a map file moves the numbers.

#### Countdown ran out (`countdown_done`)

True once the countdown of this name has run out.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Countdown | `text` | text | The countdown's name. |

*Example:* 'Relief arrives' has run out: victory.

*Watch out:* A trigger that stops or restarts the countdown hides this from every trigger after it in the list. Put the trigger that reacts to the clock running out before any that stop it.

#### Countdown below (`countdown_below`)

True while the countdown is running with at most this many seconds left.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Countdown | `text` | text | The countdown's name. |
| Seconds left | `value` | seconds | At most this many. |

*Example:* 'Evacuation' below 60 s: the last train whistles.

*Watch out:* False once it has run out: use 'Countdown done' for that.

#### Counter at least (`counter_at_least`)

True while a mission counter is at least this value.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Counter | `text` | text | The counter's name. |
| At least | `value` | number | Its value. |

*Example:* 'trucks_through' at least 5: enough supplies arrived.

*Watch out:* A counter nobody has set is 0.

#### Counter at most (`counter_at_most`)

True while a mission counter is at most this value.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Counter | `text` | text | The counter's name. |
| At most | `value` | number | Its value. |

*Example:* 'lives' at most 0: the mission is lost.

*Watch out:* A counter nobody has set is 0, so 'at most 0' is true from the start.

#### Objective done (`objective_done`)

True once the objective is done.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Objective | `value` | objective number | Its number: the first objective a script adds is 0, the next 1... |

*Example:* Objective 0 is done: add objective 1.


#### Objective failed (`objective_failed`)

True once the objective has failed.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Objective | `value` | objective number | Its number: the first objective a script adds is 0, the next 1... |

*Example:* Objective 2 failed: the bridge is lost, change the plan.


#### Difficulty at least (`difficulty_at_least`)

True if the mission is played at this difficulty or harder.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Difficulty | `value` | easy/normal/hard | The campaign setting. |

*Example:* At least hard: an extra wave. With 'not': a helping hand on easy.


### Campaign and tutorials

#### Men in cover (`in_cover`)

True while the side has at least this many men inside buildings, bunkers or trenches.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| At least | `value` | number | How many men. |

*Example:* Tutorial: six men in cover, go on to the next lesson.


#### Order given (`order_given`)

True once the (human) side has given at least this many orders of a kind.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| At least | `value` | number | How many orders. |
| Order | `order` | order or any | The kind of order, or any. |

*Example:* Tutorial: the player has given one attack-move order.

*Watch out:* Orders by the computer and by scripts are not counted.

#### Campaign flag at least (`flag_at_least`)

True if a campaign flag is at least this value. Flags are kept from mission to mission.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Flag | `text` | text | The flag's name. |
| At least | `value` | number | Its value. |

*Example:* Flag 'tiger_survived' at least 1: the Tiger from the last mission joins this one.

*Watch out:* A flag set in this mission counts at once; flags only reach later missions if this one is won.

### Named units

#### Named unit alive (`named_alive`)

True while a unit with this name is alive.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text | The name the units were given (Name units, Spawn named, or in the editor's inspector). |

*Example:* 'Adler' is alive (with 'not': Adler is lost).

*Watch out:* With no unit of that name it is false, so 'not Named alive' fires at once if the name is misspelt or the unit has not arrived yet.

#### Named unit dead (`named_dead`)

True when no unit with this name is alive.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text | The name the units were given (Name units, Spawn named, or in the editor's inspector). |

*Example:* 'Flak tower' is dead: the way is open.

*Watch out:* Also true before units of that name exist. Guard it with 'Trigger fired' (the trigger that spawns them).

#### Named unit in area (`named_in_area`)

True while at least this many units with the name are in the area.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text | The name the units were given (Name units, Spawn named, or in the editor's inspector). |
| Area | `area` | area | The area it looks at. |
| At least | `value` | number | How many. |

*Example:* 'Convoy' has 3 units in 'Depot': the convoy got through.


#### Named unit hurt (`named_health_below`)

True while a unit with the name has less than this share of its health.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text | The name the units were given (Name units, Spawn named, or in the editor's inspector). |
| Below | `value` | percent | Percent of full health. |

*Example:* 'Command tank' below 30%: it pulls out.


### Structures

#### Structures at most (`structures_at_most`)

True while the side has at most this many structures (of the type) in the area or on the map.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side, or every side. |
| At most | `value` | number | How many structures. |
| Unit type | `unit` | unit type or any | Only this type of structure, or any but field works. |
| Area | `area` | area or none | An area, or none for the whole map. |

*Example:* Side 1 has at most 0 'ger_flak_tower' in 'Airfield': the guns are silenced.

*Watch out:* With no type, field works (wire, trenches, sandbags) are not counted.

#### Structures at least (`structures_at_least`)

True while the side has at least this many structures (of the type) in the area or on the map.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side, or every side. |
| At least | `value` | number | How many structures. |
| Unit type | `unit` | unit type or any | Only this type of structure, or any but field works. |
| Area | `area` | area or none | An area, or none for the whole map. |

*Example:* Side 0 has 2 barracks: the base is established.

*Watch out:* With no type, field works are not counted.

## Actions

### Messages and story

#### Message (`message`)

Shows a line of text to a side, or to everyone.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Text | `text` | text | The words. |

*Example:* "Enemy armour sighted to the north."

*Watch out:* For a speaker's voice with the radio panel use Radio message instead.

#### Radio message (`transmission`)

A radio message: the speaker's name and words appear in the radio panel, typed out, with the radio sound.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Text | `text` | text | Speaker: words. |
| Seconds shown | `value` | seconds | How long it stays (0: by its length). |

*Example:* "Oster: The bridge must hold until nightfall."

*Watch out:* Write 'Speaker: words'.

### Objectives

#### Add objective (`add_objective`)

Adds an objective to a side's list. With an area it is marked on the map while open.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Text | `text` | text | What the player must do. |
| Optional | `value` | on/off | A secondary objective. |
| At area | `area` | area or none | Where it is, to mark on the map (or none). |

*Example:* "Take the bridge" at 'Bridge'. Objectives are numbered in the order they are added, from 0.

*Watch out:* The number is the order of adding at run time, not the order of triggers in the list: add objectives in a fixed order, or the wrong one gets completed.

#### Complete objective (`complete_objective`)

Marks an objective as done.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Objective | `value` | objective number | Its number: the first objective a script adds is 0, the next 1... |

*Example:* Complete objective 0.


#### Fail objective (`fail_objective`)

Marks an objective as failed.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Objective | `value` | objective number | Its number: the first objective a script adds is 0, the next 1... |

*Example:* Fail objective 1: the convoy was lost.


#### Hide objective (`hide_objective`)

Takes an objective off the list.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Objective | `value` | objective number | Its number: the first objective a script adds is 0, the next 1... |

*Example:* Hide objective 1 when the plan changes.


### Winning and losing

#### Victory (`victory`)

The side and its allies win the mission.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | Who wins. |

*Example:* After the last objective: Victory for side 0.

*Watch out:* A script with a Victory action switches off the usual win (all enemies destroyed): make sure it can be reached. Campaign flags set earlier in the mission are kept only if this is reached.

#### Defeat (`defeat`)

The side loses the mission.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | Who loses. |

*Example:* 'Adler' is dead: Defeat for side 0.


### Units

#### Spawn units (`spawn_units`)

Creates units for a side in an area.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | Whose they are. |
| Area | `area` | area | Where they appear. |
| Unit type | `unit` | unit type | What to create. |
| How many | `value` | number | Units to create. |
| Scale with difficulty | `scaled` | on/off | 70% as many on easy, 135% on hard. |

*Example:* 8 'rus_rifleman' for side 1 in 'North road': a wave arrives.

*Watch out:* They appear standing in the area with no orders: follow with Attack-move.

#### Change owner (`change_owner`)

The units go over to another side.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| New side | `player2` | side 0-7 | Their new side. |

*Example:* Side 3's units in 'Barracks' go over to side 0: the garrison defects.


#### Destroy units (`kill_units`)

Destroys the units as if killed in battle (wrecks and bodies remain).

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |

*Example:* Kill 'Bridge charge': the bridge goes up.

*Watch out:* It kills invulnerable units too. To remove units without a trace there is no action yet: move them off first.

### Sides and supplies

#### Give supplies (`give_supplies`)

Adds supplies to a side (a negative number takes them away).

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| Supplies | `value` | number | How many (negative: lose). |

*Example:* Side 0 gains 2000: the supply drop landed.


#### Set alliance (`set_alliance`)

Makes two sides allies, or enemies.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| With side | `player2` | side 0-7 | The side it joins or leaves. |
| Allied | `value` | on/off | On: allies. Off: enemies. |

*Example:* Side 3 becomes an ally of side 0: the garrison changes sides.


#### Income rate (`set_income_rate`)

The side's income runs at this share of normal.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| Percent | `value` | percent | Of normal income. |

*Example:* Side 0 at 50%: the supply line is cut.


### Camera and sound

#### Reveal area (`reveal_area`)

The side sees the area from now on.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Area | `area` | area | The area it looks at. |

*Example:* Reveal 'Enemy HQ' to side 0 when the spy reports.

*Watch out:* It can't be hidden again.

#### Move camera (`camera_to`)

Jumps a side's view to an area.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Area | `area` | area | The area it looks at. |

*Example:* Camera to 'Bridge' when it is blown.

*Watch out:* For a glide use Camera pan.

#### Cinematic (`cinematic`)

Letterbox bars and no orders for a side, or back to play.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Cinematic | `value` | on/off | On or off. |

*Example:* On for the opening pan, off when it ends.

*Watch out:* Always pair an 'on' with an 'off' that is certain to fire: the player can do nothing meanwhile.

#### Pan camera (`camera_pan`)

A side's view glides to an area over some seconds.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Area | `area` | area | The area it looks at. |
| Seconds | `value` | seconds | How long the glide takes. |

*Example:* Pan to 'Enemy column' over 6 s.


#### Mark area (`mark_area`)

Puts a named marker on an area and the minimap.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Area | `area` | area | The area it looks at. |
| Name | `text` | text | The marker's name, shown on the map. |
| Kind | `value` | goal/danger/note | How it is drawn. |

*Example:* Mark 'Bridge' as a goal, 'Minefield' as danger.


#### Clear marker (`clear_marker`)

Removes a marker (empty name: all of them).

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Name | `text` | text (may be empty) | The marker's name. |

*Example:* Clear 'Bridge' once it is taken.


#### Play sound (`play_sound`)

Plays a sound for a side, at an area or everywhere.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Sound | `text` | text | A sound of audio.json. |
| At area | `area` | area or none | Where it sounds from (or everywhere). |

*Example:* 'explosion_large' at 'Bridge'.


#### Play music (`play_music`)

Plays a piece or a mood, held until released.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Music | `text` | text (may be empty) | A track, or calm, battle, victory, defeat, menu (empty: automatic). |

*Example:* 'battle' for the assault; empty to let the music follow the fighting again.


### Orders

#### Attack-move units (`attack_move`)

The units attack-move to an area: they march there and fight what they meet.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| To area | `area2` | area | Where they go. |

*Example:* Side 1's units in 'North road' attack-move to 'Village'.

*Watch out:* Units held by 'Set locked' still obey scripts. Nothing happens if no unit matches: check the area covers where they were spawned.

#### Move units (`move_units`)

The units move to an area without stopping to fight.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| To area | `area2` | area | Where they go. |

*Example:* 'Convoy' moves to 'Depot'.

*Watch out:* For a route with several legs chain triggers with 'Named in area', or use a path (mission model).

#### Set stance (`set_stance`)

The units take a stance.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| Stance | `value` | aggressive/hold ground/hold fire | How they behave on their own. |

*Example:* 'Ambush party' holds fire.


#### Patrol (`patrol`)

The units walk between two areas until given other orders.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| To area | `area2` | area | The other end of the beat. |

*Example:* 'Sentries' patrol between 'Gate' and 'Depot'.


#### Garrison (`garrison`)

The infantry go into buildings, bunkers or trenches in an area (their side's or empty).

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| Buildings in | `area2` | area | Where the buildings are. |

*Example:* Side 1's men in 'Village' garrison 'Village'.


#### Evacuate (`evacuate`)

Men inside buildings in the area come out.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | Whose men. |
| Area | `area` | area | Buildings in this area. |

*Example:* Evacuate 'Farm' when it catches fire.


#### Bind units (`bind_units`)

The units are bound into one fighting unit (squad, platoon...).

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |

*Example:* Bind the wave so it marches as one body.


### Triggers

#### Enable trigger (`enable_trigger`)

Switches a trigger on.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Trigger | `trigger` | trigger | The trigger to switch on. |

*Example:* Enable 'Counter-attack' once the village is taken.


#### Disable trigger (`disable_trigger`)

Switches a trigger off: it is not checked until switched on again.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Trigger | `trigger` | trigger | The trigger to switch off. |

*Example:* Disable 'Patrols' when the alarm is raised.


#### Fire trigger (`fire_trigger`)

Runs another trigger's actions now; it counts as fired.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Trigger | `trigger` | trigger | The trigger to run. |

*Example:* Fire 'Alarm' from several triggers.

*Watch out:* Its conditions are not checked.

#### Reset trigger (`reset_trigger`)

A trigger can fire again as if it never had, and is switched on.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Trigger | `trigger` | trigger | The trigger to reset. |

*Example:* Reset 'Wave' to send it once more.


### Computer player

#### Set computer player (`set_computer`)

Hands a side to the computer at a difficulty, or back to a human.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| Played by | `value` | human/easy/normal/hard | Who plays it. |

*Example:* Side 2 is played by the computer, normal.

*Watch out:* On hard, the campaign already raises hostile computers one level.

#### Computer: attack area (`ai_attack`)

The computer playing a side sends its main force against an area, at once, with what it has.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| Area | `area` | area | The target. |

*Example:* Side 1's computer attacks 'Bridgehead'.

*Watch out:* The side must be played by the computer (Set computer).

#### Computer: defend area (`ai_defend`)

The computer playing a side keeps its main force in an area and defends it.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| Area | `area` | area | What to hold. |

*Example:* Side 2's computer defends 'Town'.


#### Computer: free hand (`ai_free`)

The computer playing a side chooses its own targets again.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |

*Example:* After the scripted assault: AI free.


#### Computer: behaviour (`ai_behaviour`)

Allows or forbids one thing the computer does: build, train, attack, raid, capture, fortify, scout, repair.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| Behaviour | `text` | text | build, train, attack, raid, capture, fortify, scout or repair. |
| Allowed | `value` | on/off | On: it may. |

*Example:* A garrison's computer may not build or train: it fights with what it has.


#### Computer: attack size (`ai_wave_size`)

The computer playing a side attacks with this share of its usual force.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| Percent | `value` | percent | Of its usual attack size (0: usual). |

*Example:* 50%: probing attacks early on.


### Interface (tutorials)

#### Show hint (`hint`)

Shows text in a side's hint panel until the next hint. The first line is the title; empty text closes the panel.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Text | `text` | text (may be empty) | Title on the first line, then the hint. |

*Example:* "Digging in\nSelect your riflemen and right-click the trench."


#### Highlight interface (`highlight`)

Makes an interface element glow for a side (empty: none).

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Element | `text` | text (may be empty) | Which element. |

*Example:* 'tab.train', 'build:gbr_power', 'order.evacuate', 'minimap'.


#### Lock building (`lock_build`)

The side can't build a type (or anything).

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| Unit type | `unit` | unit type or any | The type to forbid, or everything. |

*Example:* Lock everything, then unlock the barracks: build this first.


#### Unlock building (`unlock_build`)

The side can build a type (or everything) again.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | The side it is about. |
| Unit type | `unit` | unit type or any | The type to allow, or everything. |

*Example:* Unlock 'gbr_barracks'.


### Campaign

#### Set campaign flag (`set_flag`)

Sets a campaign flag. The campaign keeps it for later missions when this one is won.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Flag | `text` | text | The flag's name. |
| Value | `value` | number | What to set it to. |

*Example:* 'tiger_survived' = 1 if 'Tiger' is alive at the end.

*Watch out:* Set it before the Victory action in the same trigger, or in an earlier one.

#### Add to campaign flag (`add_flag`)

Adds to a campaign flag.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Flag | `text` | text | Its name. |
| Add | `value` | number | How much. |

*Example:* 'defected' + 4 for each battalion that comes over.


#### Spawn from campaign flag (`spawn_from_flag`)

Creates as many units as a campaign flag says.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | Whose they are. |
| Area | `area` | area | Where they appear. |
| Unit type | `unit` | unit type | What to create. |
| Flag | `text` | text | The flag that says how many. |
| At most | `value` | number | The most to create (0: 50). |
| Scale with difficulty | `scaled` | on/off | 70% as many on easy, 135% on hard. |

*Example:* As many 'ger_rifleman' as 'brenner_men' (at most 30) in 'Camp'.


### Clocks and counters

#### Start countdown (`countdown`)

Starts (or restarts) a clock on screen that counts down.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Countdown | `text` | text | Its name, shown beside the clock. |
| Seconds | `value` | seconds | How long it runs. |
| Side | `player` | side 0-7 or everyone | Who sees it. |

*Example:* 'Relief arrives' at 600 s, shown to everyone.

*Watch out:* Restarting a clock that has run out hides 'Countdown done' from later triggers.

#### Stop countdown (`stop_countdown`)

Removes a clock.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Countdown | `text` | text | Its name. |

*Example:* Stop 'Bridge demolition' when the charges are found.

*Watch out:* Later triggers no longer see it as done.

#### Pause countdown (`pause_countdown`)

Pauses a clock, or lets it run on.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Countdown | `text` | text | Its name. |
| Paused | `value` | on/off | Paused, or running. |

*Example:* Pause 'Evacuation' during a cutscene.


#### Set counter (`set_counter`)

Sets a mission counter.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Counter | `text` | text | Its name. |
| Value | `value` | number | What to set it to. |

*Example:* 'lives' = 3.


#### Add to counter (`add_counter`)

Adds to a mission counter (a negative number takes away).

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Counter | `text` | text | Its name. |
| Add | `value` | number | How much. |

*Example:* 'trucks_through' + 1 each time a truck reaches the depot.


### Map and terrain

#### Set play area (`set_play_area`)

Confines the battle to an area for a side (or everyone): orders, building and the camera stay inside. No area: the whole map.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 or everyone | The side it is for, or everyone. |
| Area | `area` | area or none | The play area, or none for the whole map. |

*Example:* Start in 'Valley'; widen to 'Valley and town' when the bridge is taken.

*Watch out:* Units already outside stay where they are.

#### Add logistics point (`add_logistics_point`)

A new logistics point appears in the middle of an area.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Area | `area` | area | The area it looks at. |

*Example:* A point at 'Rail yard' when the line is repaired.


#### Freeze water (`freeze_water`)

The water in an area freezes: men and wheels can cross it, tracks can't. Shells break it.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Area | `area` | area | The area it looks at. |

*Example:* Freeze 'River' when winter comes.


#### Break the ice (`break_ice`)

The ice in an area breaks up; whatever stands on it goes into the water.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Area | `area` | area | The area it looks at. |

*Example:* Break 'Ford' when the thaw sets in.


#### Flood the land (`flood_area`)

The land in an area floods waist deep: men wade, vehicles can't cross.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Area | `area` | area | The area it looks at. |

*Example:* Flood 'Polder' when the dyke is blown.

*Watch out:* Units standing there that can't wade are lost.

#### Drain the land (`drain_area`)

Flooded land in an area is pumped dry again.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Area | `area` | area | The area it looks at. |

*Example:* Drain 'Polder' when the pumping station is taken.


### Set pieces

#### Name units (`name_units`)

Gives a name to a side's units in an area (of a type). Named units can be picked by name in conditions and actions.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | Whose units. |
| Area | `area` | area | Units in this area. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| Name | `text` | text | The name to give. |
| At most | `value` | number | How many to name (0: all). |

*Example:* Side 0's 'ger_heavy_tank' in 'Start' are called 'Tiger'.


#### Invulnerable (`set_invulnerable`)

The units take no damage, or can be hurt again. For set pieces that must survive until the story lets them die.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| Invulnerable | `value` | on/off | On or off. |

*Example:* 'Column' is invulnerable until the ambush trigger.

*Watch out:* Kill units and Damage units still hurt them.

#### Computer control (`set_ai_control`)

The computer may command the units, or leaves them to the script.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| Computer commands them | `value` | on/off | On: the computer may use them. |

*Example:* 'Reserve' is held back from the computer until phase two.


#### Targetable (`set_targetable`)

Enemies pick the units as targets on their own, or ignore them.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| Targetable | `value` | on/off | On: enemies shoot at them. |

*Example:* 'Staff car' can't be targeted during the opening.

*Watch out:* A player can still order an attack on them.

#### Locked from orders (`set_locked`)

The owning player can't give the units orders, or can again.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| Locked | `value` | on/off | On: the player can't command them. |

*Example:* 'Escort' is locked while it follows the script.


#### Set health (`set_health`)

Sets the units' health to a share of their maximum.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| Health | `value` | percent | Percent of maximum. |

*Example:* 'Wrecked column' at 20%.


#### Damage units (`damage_units`)

The units lose a share of their maximum health; they can die of it.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Unit name | `text` | text (may be empty) | Act on the units with this name. Leave empty to pick them by side, area and type instead. |
| Side | `player` | side 0-7 or everyone | Whose units, or every side's (when not picked by name). |
| In area | `area` | area or none | Units in this area, or anywhere on the map. |
| Unit type | `unit` | unit type or any | Only this type of unit, or any. |
| Damage | `value` | percent | Percent of maximum health lost. |

*Example:* Damage 'Bunker' by 50% when the bombers pass.


#### Artillery barrage (`barrage`)

Shells fall on an area.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Area | `area` | area | Where they fall. |
| Shells | `value` | number | How many. |
| Fired by | `player` | side 0-7 or everyone | The side that fires (for kills and return fire), or nobody's guns. |
| Weapon | `text` | text (may be empty) | Which gun's shell (empty: a field gun). |

*Example:* 20 shells on 'Trench line', fired by side 1.


#### Spawn named units (`spawn_named`)

Creates units and gives them a name in one go.

| Field | In files | Kind | What |
| --- | --- | --- | --- |
| Side | `player` | side 0-7 | Whose they are. |
| Area | `area` | area | Where they appear. |
| Unit type | `unit` | unit type | What to create. |
| How many | `value` | number | Units to create. |
| Name | `text` | text | The name they get. |
| Scale with difficulty | `scaled` | on/off | 70% as many on easy, 135% on hard. |

*Example:* 3 'ger_heavy_tank' in 'Hill' called 'Tigers'.


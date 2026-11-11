# Playing Legion Classic (build 0.9.0)

Double-click `LegionClassic.exe`. No installation is needed; keep the `content`
and `mods` folders next to the exe.

Windows may show a SmartScreen warning the first time because the game isn't
code-signed yet: choose **More info → Run anyway**.

## The encyclopedia

**K** (or F4, or the button on the sidebar, or the main menu) opens the
encyclopedia: every unit, building and defence of each side with its picture,
cost, armour, speed, sight, and damage a minute against each kind of armour.
With the mouse over a build button, K opens that unit's page.

## What's in this build

The game opens on the **main menu**, over a live battle between two computer
players on a random map.

- **Tutorial: Basic Training.** New to the game? Start here: four short
  lessons (giving orders, cover and armour, digging in, building a base).
  Hints appear at the top of the screen and the button you need glows.
- **Campaigns.** Three missions each for Germany (*Kampfgruppe Adler*), the
  Soviet Union (*Guards Rifle Corps*) and Britain (*The Desert Rats*). Read
  the briefing, then Begin. Missions open one after another, and what you
  achieve in one can help in the next (a tank that survives comes back).
  Progress is kept in `progress.json` in the user folder; *Start again* on a
  campaign's page forgets it.

- **Skirmish.** Pick one of twelve ready-made **maps** (one for every
  landscape, villages placed), or a new random map: then pick a map size, a biome (temperate, winter, desert, tropical
  or urban), a seed, the starting supplies and up to four sides. Each side is
  you or the computer (easy, normal or hard), Germany, Russia or Britain, on a team of
  your choice. **Landscape**: river valley, front line river, confluence,
  lakeland, crossroads, ridge, marshes, great lake, open steppe, deep forest,
  hill country or twin rivers, or *Random* to let the seed pick (the menu
  shows which). **Starting positions**: *Opposite sides* (the default: teams
  face each other across the map, as in Company of Heroes), *Fixed* or
  *Shuffled*. Every side starts with a base laid out like a military post (a
  compound with a parade ground, barracks, power, a depot on the nearest
  supply point, sandbags and MG nests across the front, wire and tank traps
  beyond) and a small force on its parade ground; villages and towns stand
  between them.
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

The computer plays by the same rules you do: it pays for everything and only
gives ordinary orders. Each side fights to **its own doctrine**:

- **Germany** builds fewer, better units, binds them into battle groups of
  tanks and infantry, fortifies in depth (a trench line, wire and tank traps
  across the approach, more guns) and sends reinforcements forward as a bound
  second echelon.
- **Russia** masses riflemen and T-34s, leans on artillery, attacks with bigger
  forces and feeds new troops into an attack as they arrive.
- **Britain** is methodical: an observation post goes up early to bring on the
  Sextons and an extra 5.5-inch gun pit, the front is dug in and wired, and it
  waits to attack with bigger set-piece forces than the others, falling back
  sooner to save lives (it regroups at 40% of its strength, the others at 30%).
  Bren teams and carriers are its favourite light kit.

Its forces move all the time. The **main force** gathers in front of the base,
binds into a unit and attacks, often round a flank; when it is badly hurt it
falls back a few hundred metres, takes cover in buildings and trenches, and
goes again, rather than running home. **Raiders** (scout cars, half-tracks and
light tanks) hunt the weakest-defended enemy depots and stragglers. A **home
guard** mans the base's trenches and bunkers, and damaged vehicles go home for
repairs. It keeps **expanding**: depots on ever more distant supply points
with an MG nest beside the far ones, and later a second Construction Yard on
a quieter part of the map. **Easy** thinks slowly and attacks with small
forces; **hard** thinks fast, fortifies more and attacks sooner and harder.
You can hand any side to the computer in the editor's Sides tab, even
mid-battle.

### Saving, loading and replays

- **Save** from the in-game menu at any moment; **Load game** lists your saves
  with their date and game time. A loaded game carries on exactly as it was.
- **Replays.** Every game is recorded. *Save replay* in the in-game menu keeps
  it; your last game is always kept as `last_game`. A replay plays the battle
  back exactly as it happened: Play / Pause, speed 1× to 8×, **Tab** to watch
  another side, **V** to reveal the whole map. (A game you changed with the
  editor while playing can't be replayed, because the editor's changes aren't
  part of the record.)
- Saves (`.lsav`), replays (`.lrep`) and your maps (`.lmap`) live in
  `%APPDATA%\Makorain\LegionClassic\` (`saves`, `replays`, `maps`).

### The armies

Every army fields the same kinds of unit, built its own way: German armour is
boxy and camouflaged in three colours, Soviet armour sloped and plain green,
British armour plain service brown with the white-red-white recognition flash
(on German-style hulls until the British models are made; British soldiers wear
the flat Brodie helmet). The table gives German / Soviet figures; the British
differences follow it.

| Unit | Built at | Cost | Notes |
| --- | --- | --- | --- |
| Rifleman / Grenadier | Barracks | 8 / 10 | Cheap, hit 7 shots in 10. Only scratch armour: it takes about thirty of them to wear down a heavy tank. |
| MG Team | Barracks | 40 | A heavy machine gun: shreds infantry at 6.5 tiles. |
| AT Team | Barracks | 50 | Panzerschreck (Germany: big hit, short range) or anti-tank rifle (Russia: longer range, small hits that only bite on light armour and flanks). Hunts tanks from cover. |
| Engineer (Pioneer / Sapper) | Barracks | 30 | Unarmed. Repairs friendly vehicles and buildings: right-click them; idle engineers repair anything damaged nearby. In a bunker, tower, trench or wall of sandbags they keep it, and the field works right round it, in repair. |
| Sniper | Barracks (needs a Watchtower) | 80 | Sees 11 tiles, kills a man with almost every shot at 9.5 tiles. |
| Scout Car | Tank Works | 400 | Fast, sees far: spots for the guns. Thin-skinned (80 hit points). |
| Half-track | Tank Works | 450 | Quick, machine gun on a pivot; proof against rifles for a while, not against any gun. |
| Light Tank | Tank Works | 550 | 37 / 45 mm gun. Fast and cheap, a quarter of a heavy tank's health; its gun does little to a medium tank's front and next to nothing to a heavy's. |
| Medium Tank | Tank Works (needs a Supply Depot) | 850 / 900 | The backbone: 75 / 76 mm gun, HE against infantry. Half a heavy tank's health; needs numbers or a flank against a heavy. |
| Tank Destroyer | Tank Works (needs a Supply Depot) | 1100 | No turret, heavy armour, a long gun that goes through a heavy tank's front. Must turn to fire. Two of them beat a heavy tank. |
| Artillery | Tank Works (needs a Watchtower) | 1200 | Germany: 105 mm howitzer (28 tiles). Russia: rocket truck firing salvoes of 8 rockets (24 tiles) that blanket an area. Needs spotters; dies to anything that reaches it. |
| Heavy Tank | Tank Works (needs a Supply Depot and a Watchtower) | 2000 | 88 / 122 mm gun at 10.5–11.5 tiles, armour that shrugs off light guns and blunts medium ones: it falls to numbers, to tank destroyers, or to a shot from behind. |
| Construction Truck | Tank Works | 3000 | Unfolds into a new Construction Yard. |

**The British** build the same base under their own names (Drill Hall, Tank
Workshop, Supply Dump, Observation Post, Pillbox, 6-pdr Gun Pit, Vickers Nest,
Slit Trench, Dannert Wire, Anti-Tank Cubes) and field:

| Unit | Slot | Cost | Notes |
| --- | --- | --- | --- |
| Rifleman | Rifleman | 10 | The Lee-Enfield fires faster than other rifles (1.3 s against 1.5 s). |
| Bren Team | MG Team | 40 | Lighter than an MG 42 or a Maxim (6 tiles) but more accurate and quicker on its feet. |
| PIAT Team | AT Team | 50 | Short (4 tiles), slow to reload, hits hard enough for side and rear shots. |
| Royal Engineer | Engineer | 30 | As the others. |
| Dingo Scout Car | Scout Car | 400 | The fastest thing on wheels. |
| Universal Carrier | Half-track | 400 | Cheaper and quicker than a half-track, thinner skinned. |
| Crusader | Light Tank | 600 | A 6-pounder: the best gun on a light tank. |
| Cromwell | Medium Tank | 850 | Fast, with a dual-purpose 75 mm: good HE, weaker AP than the German 75. |
| Sherman Firefly | Tanks (needs a Supply Dump and an Observation Post) | 1150 | A Sherman with the 17-pounder: the British answer to heavy tanks. |
| Achilles | Tank Destroyer | 1050 | The 17-pounder in an open turret on a fast, thin hull. |
| Sexton | Artillery | 1100 | A 25-pounder: 26 tiles, fires every 4 s (the German 105 every 6.3 s). |
| Churchill | Heavy Tank | 1800 | The toughest thing on tracks (720 hit points), very slow, only a 75 mm gun. |
| Construction Lorry | Construction Truck | 3000 | Unfolds into a Construction Yard. |

The British *Medium Gun Emplacement* mounts a 5.5-inch gun: 38 tiles, the
longest reach on the field.

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

- **Supplies** ($, top of the sidebar, with your income a minute beside it) pay
  for everything. Your Construction Yard brings in a trickle ($120 a minute).
  The real money comes from **supply points**: marked SUPPLY on the ground and
  as gold squares on the minimap. Build a **Supply Depot** on one ($800) and it
  earns $600 a minute for as long as it stands. Depots can go on any free
  supply point on the map, however far from your base, so the points out in
  the middle are worth fighting over. Destroy an enemy depot and its point is
  free again.
- **Power.** Most structures use power; Power Stations make it (the bar under
  your supplies). Short of power, everything takes twice as long to build.
- **Building.** On the sidebar's **Build** tab, click a structure. It is paid
  for as it goes up; when it says READY, click it again and put it down near
  your other structures (a green footprint means it fits; a grid shows the
  tiles around the cursor, red where the ground is too steep or wet).
  Structures need others first: the Barracks needs a Power Station, the Tank
  Works a Supply Depot, and so on (hover a greyed-out button to see what's
  missing).
- **Training.** The **Train** and **Vehicles** tabs queue infantry at the Barracks
  and vehicles at the Tank Works. Infantry are cheap: a Grenadier costs $10.
  Click for one man, **Shift-click for a fireteam of five**; right-click
  cancels. Select a barracks or works and right-click the ground to set where
  new units go; that building becomes the one they come out of.
- **Construction Truck.** Built at the Tank Works; press **Ctrl+D** (or G, or
  right-click it) to unfold it into a new Construction Yard wherever there is
  room.
- **Selling.** The **Sell** button, then click your structures, returns half their
  cost.
- **Defeat.** A side with no units and no structures left (trenches and walls
  don't count) is defeated. The last side standing wins.

### Fireteams, platoons and companies

Soldiers fight as units, as real armies do:

- **Fireteams form by themselves.** Five men of the same kind standing close
  together, not yet in a unit, become a fireteam (a fireteam of five trained
  together forms at the rally point).
- **Bigger units are bound by hand.** Select the men or fireteams and press
  **Ctrl+B**: 10 or more make a squad, 20 a platoon, 100 a company. Vehicles
  bind the same way: 5 make an armoured platoon, 15 a company, 50 a battalion,
  named for what they are (reconnaissance, tank destroyer, artillery battery,
  mechanised...). **Tanks and infantry bound together** make a combined unit:
  a mechanised platoon, a battle group or an armoured regiment (a vehicle
  counts as four men towards its size), and it fights harder still.
  **Ctrl+U** disbands the selected unit.
- **Bigger units hit harder.** Members of a fireteam fire 5 % harder, a squad
  10 %, a platoon 15 %, a company 30 %. Armour gains less (3 %, 6 %, 10 %).
- **Every man and vehicle is still its own.** Clicking a soldier or box-selecting
  picks just those, whatever unit they belong to. To take the **whole unit**,
  click its **card**; double-click the card to centre the view on it. Men who
  wander far stay members.
  Casualties wear a unit down: a company that falls to 20 men becomes a
  platoon, a platoon at 10 a squad, a squad at 5 a fireteam; a fireteam down to
  one man breaks up.
- **Unit cards.** Each unit carries a card above it in NATO style: the frame in
  your colour (a diamond for the enemy), the branch inside (a cross for
  infantry, an oval for armour, a slash for recon, a dot for artillery...),
  the size above (Ø fireteam, • squad, ••• platoon, | company, || battalion), and
  a name such as *Fireteam Bravo* or *2nd Platoon*, its strength and a health
  bar. Cards overlap neatly when units stand together. Enemy cards show too
  (turn them off in Settings → Display, *Show enemy unit cards*). Hovering a
  soldier or vehicle shows its type, its unit and its strength.

### Holding buildings, bunkers, trenches and sandbags

- **Right-click a building or bunker** with infantry selected to send them in
  (houses, churches and ruins hold 6 to 30 men; a bunker 4; a watchtower 1).
  Men inside take **half damage** and shoot out at full strength. A badge
  shows how many are inside.
- **The more men, the more fortified it looks:** sandbags go up along the
  front, then a side, then all round, and the men take a little less damage
  still (45 %, then 40 %).
- **To come out,** give the men inside any move order: click the building to
  select them, then right-click where they should go.
- If the building is destroyed, the men inside are thrown out and hurt.
- **Right-click a trench** with infantry selected to man it: each piece of
  trench holds **up to three men**. They spread along the whole trench line,
  drop into it and **stay there**: move orders don't pull them out. Press
  **Evacuate** (E, or the sidebar button) with them selected to climb out.
  Men in a trench take 60 % less damage. They are drawn down in it, waist
  deep behind its lip of earth, and come up to fire.
- **Sandbags are manned the same way:** right-click a wall of sandbags (or
  just beside it) with infantry. Three men to a piece, spread along the
  wall, kneeling behind it; they take half damage and stay until evacuated.
- **Pioneers (engineers) in a work keep it in repair:** a bunker, tower,
  trench or sandbag wall they hold, and the field works right round it.

### Villages, towns and bridges

Skirmish maps grow villages that fit their country: half-timbered and stone
cottages, inns, manors, windmills and town halls in the west; log houses,
grain silos and onion-domed churches in the snow; mud-brick houses, market
stalls and courtyard villas in the desert; terraces, corner shops, apartment
blocks and factories in towns; stilt houses in the jungle. Every one can be
held by infantry. Where a road crosses a river there is a **bridge** (stone,
or timber in the snow and the jungle) beside the old ford. Anything can cross
a bridge; it is never shot at unless you order it (force-fire), and when a
span falls whatever is on it goes into the river.

### What you can't see: chips

Black ground has never been seen and dimmed ground isn't watched now. But
your troops hear and glimpse movement beyond their sight (about twice as
far). Enemy forces there show as **chips**, like counters on a map table: the
taller the stack and the more question marks, the more of them (a handful,
about a platoon, about a company, more), and the symbol on top is the most
valuable thing among them. Chips can't be shot; send someone to look.

### Fortifications and defences (Defend tab)

| | What it does |
| --- | --- |
| Trench | Dig a line (click, drag, release). Each piece holds three men, who take 60 % less damage and stay until evacuated (E). Wheeled vehicles can't cross it. |
| Sandbags | A low line of sandbags; pieces join up into a wall. Infantry right beside it take 40 % less damage; right-click it with infantry to man it (three men a piece, half damage, they stay until evacuated). Vehicles can't cross it; infantry climb over. |
| Barbed wire | Stops infantry; vehicles drive through. |
| Tank traps | Stop vehicles; infantry walk through. |
| MG Bunker | A machine-gun pillbox that shreds infantry. |
| Anti-Tank Gun Pit | A dug-in gun with a long reach against tanks. |
| Watchtower | Sees 13 tiles: spots for your guns. |

Fortifications are bought and placed on the spot, a tile at a time; bunkers,
guns and towers are built and placed like other structures.

### Moving and fighting

- **Formations.** A group you send somewhere **marches as one body**: it
  forms up, moves at the pace of its slowest member and keeps its shape all
  the way, so tanks no longer arrive a minute ahead of their infantry. Armour
  goes in front, other vehicles behind it, infantry at the back. Men stand a
  couple of paces apart; vehicles keep a hull's width between them and more
  nose to tail.
  - **Shapes** (sidebar *Orders*, or **Z** for the next one): Box, Line (one
    rank for a handful, two for a platoon, three beyond), Column (two abreast),
    Wedge and Loose. The button that is lit is the shape the selected units
    are in. **Picking a shape re-forms them at once**: where they stand,
    keeping their front, or on the way if they are marching. The shape stays
    with the units, not with the sidebar: every later move uses it.
  - **A column follows the road.** Every rank takes the route the head took,
    so a column bends round corners and files across bridges and through
    gaps. The other shapes wheel as a body, and draw their flanks in where
    the ground is too narrow, opening out again beyond.
  - **Laying out a front:** hold the right button and drag. Marks show where
    every unit will stand and a short line shows which way the front will
    face (away from where the units are now); the length of the drag sets the
    width. The first rank stands on the line.
  - **Keeping together:** the body waits for members that fall behind, and
    sets off without one that starts far away (it catches up). On an
    **attack-move** the whole formation halts to fight when anyone has a
    target in range, closes with an enemy near its route together, then goes
    on. A member given another order, halted or sent indoors simply leaves
    the march.
  - **Fronts are kept.** Units turn to their front when they arrive, and turn
    back to it a few seconds after a fight. Vehicles sent a short way back
    reverse as a body, guns to the enemy.
  - A formation sent to the edge of the map stands just inside it.
- **Stances** (sidebar *Orders*):
  - **Aggressive** (the default): fight back when shot, even on the march,
    then carry on; go after attackers they can't reach.
  - **Hold**: return fire from where they stand, never move on their own.
  - **Hold fire**: don't shoot unless ordered to attack (ambushes).
- **Forced march: Alt + right-click.** Units ignore enemy fire until they
  arrive. Use it to retreat.

### How combat works

- **Weapons and armour.** Rifles and machine guns cut infantry down but barely
  scratch tanks (a heavy tank shrugs off a squad; thirty men firing for half a
  minute wear it down); AT teams, anti-tank guns and tanks kill them. Riflemen are green troops: they miss about 3 shots in 10, so
  firefights take time. Tanks outrange everything (about 10 tiles) and hit
  hard: armour-piercing shells against vehicles, and high-explosive shells with
  a wide blast against infantry, so infantry without armour support gets
  crushed. Shells fly and scatter with distance: one that lands on a
  vehicle's hull hits it in full, a near miss does less, a long shot may do
  nothing. Closing the range pays.
- **Armour on one scale.** A medium tank has half its army's heavy tank's
  health, a light tank a quarter. Guns come in three weights: light guns (37
  and 45 mm, anti-tank rifles) do a quarter of their damage to a heavy tank's
  front; medium guns (75 and 76 mm, the 6-pounder, rocket teams) about half;
  heavy guns (88 and 122 mm, tank destroyers, the 17-pounder, anti-tank gun
  pits) go through in full. A heavy tank beats two mediums head on: bring
  four, bring tank destroyers, or get behind it.
- **Front, side and rear armour.** A shell into a tank's flank does 1.5×
  damage; one into its rear does 2.5×. Flanking pays.
- **Reverse gear.** Order a vehicle a short way to its rear (up to 5 tiles;
  up to 12 while it is in a fight) and it backs up, slower than it drives,
  keeping its front armour and its gun towards the enemy. Order it further
  and it turns round.
- **Artillery** reaches 24–28 tiles (emplaced guns 34–38), far beyond its
  own sight: it fires at whatever your side can see.
- **Spotting.** Tanks see about 8 tiles, less than their gun range; scout cars
  see 22–24. Every unit shoots at whatever its side (and its allies) can see,
  so put scouts forward to let the tanks fire at full range.
- **Orders.** Right-click an enemy to attack it: your units drive into range,
  chase it if it moves, and stop when it dies. **Attack-move** (press **Ctrl+A** or **F**,
  then left-click the ground, or pick it in the Ctrl+right-click menu) advances to a point and
  stops to fight anything met on the way. A plain move ignores the enemy
  (tanks still shoot on the move with their turrets; infantry don't).
- **Intel.** The whole map is always in view; the enemy isn't. Enemy units
  are shown only where one of your units can see them, and can only be shot
  when seen. Out of sight, their forces show as **chips**: counters standing
  roughly where they are, bigger the more there are (few, some, many, a host).
  A chip near your units (within twice their sight) shows what kind of force
  it is; further off it is just a "?". Enemy buildings are square counters
  until one of your units has seen them; then they stay on the map, dimmed
  while nobody is watching. **Tab** switches the side you command and the
  side whose view you see.
- **Card stacks.** Unit cards close together merge into one stack card
  ("5 units"). Hover over it or select it to open the list of cards inside;
  click it to select them all.
- **Scouts** (scout cars) see about twice as far as anything else. Left
  standing, a scout falls back a few tiles by itself when an armed enemy comes
  within 6 tiles, and keeps watching from there. Give it orders and it goes
  where it is told.
- **Aftermath.** Destroyed vehicles burn and smoke for about 20 seconds and
  stay on the field as blackened wrecks; fallen soldiers stay for a while;
  shells leave craters and scorch marks.

## The 3D battlefield

Since 0.9.1 the battle is drawn in real 3D. Every unit, building, tree and
field work is its voxel model drawn as a mesh, lit by the sun and casting
shadows on the ground and on each other; hills hide what is behind them.
The camera turns all the way round and tilts from a low view across the field
to almost straight down; the wheel zooms smoothly towards the mouse. Vehicles
lean with the slopes they drive over. Smoke, fire, flashes and tracers hang
in the air and face the camera; craters, scorch marks, oil and trenches lie
on the ground. Unit cards, health bars and the editor's brushes stay flat on
the screen.

The ground matters too (Milestone 9.2): slopes slow everything,
mud bogs down wheels, sand and snow slow vehicles, and fords and wading are
half speed (bridges and roads are full speed). Ground too steep to climb is
rock: cliffs, some with overhanging ledges. New buildings level their plot.
Bridges stand only where both banks are high, straight across beside the
ford.

The old 2D view is still there: **Settings > Game > 3D battlefield** off (or
`--classic`). Shadows can be switched off on the same page.

## Sound and music

Sound follows your view. Fighting on screen is loud and sharp; a battle
further off is quieter, duller and echoes; scroll toward it to hear it
properly. When the fighting is out of view altogether you hear the front in
the distance, guns over the hills, louder the more there is going on.
Zooming out lifts you up: everything is a little more distant.

The music follows the fighting near you: quiet pieces (with silences
between them) while all is calm, the battle music when a fight flares up
near the camera, back to calm after a lull. Victory and defeat have their
own. **Settings > Sound** has volumes for the battle, orders (radio and
whistles), music, ambience and the interface, and shows what is playing.

## Controls

Every key can be changed under **Settings → Controls** (main menu or Esc).
These are the defaults:

| Input | Action |
| --- | --- |
| Left click | Select a soldier, vehicle or building (Shift: add or remove) |
| Left click on a unit card | Select the whole unit (double-click: centre on it) |
| Left drag | Box-select your units |
| Double-click | Select all units of that type on screen |
| Left click on a building your men hold | Select the men inside |
| Right click | Move; on a visible enemy: attack it; on a building or bunker with infantry: go in; on (or beside) a trench or sandbags: man it; with engineers on a damaged friend: repair it; with a barracks or works selected: rally point |
| Right drag | Lay out the front: where it is, how wide, facing which way (marks show every unit's place) |
| Z | Next formation for the selected units (they re-form at once) |
| Ctrl+right click | Choose a formation (and move or attack-move) for this order |
| Alt+right click | Forced march (don't stop to fight) |
| W A S D, arrow keys, screen edges | Scroll |
| Ctrl+A, then left click (or F) | Attack-move to that point (Esc or right click cancels) |
| Ctrl+S (or X) | Stop |
| E (or Ctrl+E) | Evacuate: the selected men climb out of their trench or from behind their sandbags |
| Ctrl+D (or G) | Deploy a construction truck |
| Ctrl+B | Bind the selection into a squad, platoon or company (vehicles: platoon, company, battalion) |
| Ctrl+U | Disband the selected unit |
| Ctrl+Shift+A | Select all your units |
| Ctrl+1…9 / 1…9 | Set / recall a control group |
| Tab | Switch which side you command (and see) |
| Home or C | Centre the view on the selection |
| Sidebar minimap | Click/drag to look; right-click to send the selection; wheel or + / − to zoom (1× to 8×) |
| Middle mouse drag | Turn the view (sideways) and tilt it (up and down); classic view: pan |
| Shift + middle mouse drag | Pan |
| Q / R | Turn the view left / right |
| Page Up / Page Down | Tilt the view up / down |
| Backspace | Reset the view (the classic angle, zoom 1×) |
| Mouse wheel | Zoom (towards the mouse); classic view: 0.5× / 1× / 2× |
| Space | Pause |
| + / − | Game speed (0.25× to 8×) |
| I | Show / hide the game info panel |
| Esc or F10 | Menu: save, load, replays, settings, restart, quit |
| F1 | Show / hide the controls panel |
| F2 | Open / close the map editor (its own keys, Ctrl+S save and Ctrl+D duplicate, win while it's open) |
| F3 | Debug panel: spawn tools (up to 10,000 units at once), reveal map, all health bars, simulation timings |
| F11 or Alt+Enter | Fullscreen |
| F12 | Screenshot |

**Settings → Display** sets the frame rate limit (30 to 240, or unlimited;
144 by default), vsync, whether the mouse is kept inside the window while you
play (on by default, so edge scrolling always works), edge scrolling, scroll
speed, the info panel, unit cards (and whether enemy cards show) and intel
chips. The sidebar wears your side's colours: field grey for the Germans,
khaki for the Soviets, service brown and stone (with the roundel) for the British.

Screenshots, settings and the log file (`legion.log`) are saved in
`%APPDATA%\Makorain\LegionClassic\`.

## Mods and creation tools

**Main menu → Mods and creation tools** switches mods on and off and opens
the two creation tools:

- **Rules editor**: every unit, building, weapon and warhead as a form, with
  each field explained and the numbers that matter worked out (damage per
  second against each armour, speed, health per supply). Changes go into the
  battle behind the menu, or the one you are playing, as you type. **Save to
  mod** keeps them as a mod.
The rules editor is also on the editor's **Battle** tab (F2). A mod is a folder of rule
changes, sprites and scenarios; the game ships with a small example,
*Veteran Grenadiers*. Saved games remember their mods. Everything about making
mods is in [MODDING.md](MODDING.md).

## Live Scenario Editor (F2)

The editor opens as a panel on the left while the battle keeps running. Build
a scenario, drop in armies, set them fighting, and change anything while they
fight. Press Space (or untick *Paused* on the Battle tab) to freeze time. The
editor shows everything; tick *Show what the player's side would know
(intel) while editing* on the Battle tab to see what the side you command
knows.

Every change to terrain and units goes into one undo history (100 steps):
**Ctrl+Z** undoes, **Ctrl+Y** redoes, even mid-battle. Undoing a placement
removes those units again; undoing a delete brings them back where they were.

### Terrain tab

| Tool | What it does |
| --- | --- |
| Supply points | Click to add a supply point (Supply Depots earn on them); Shift-click removes the nearest |
| Raise / Lower | Build hills or dig valleys and riverbeds |
| Smooth | Soften bumps and sharp edges |
| Flatten | Level the ground to the height where the stroke started |
| Roughen | Add natural unevenness |
| Paint ground | Grass, dry grass, dirt, mud, sand, rock, snow, dirt road; edges blend naturally. Tick **Grid** (or press G) to paint whole tiles, a square the brush size wide, for exact edges |
| Place props | Click for one tree, bush, rock, palm, snowy pine, dead tree, jungle tree, scrub, debris, sandstone, stump, log pile, haystack, crates or fence; drag to scatter. **Civilian buildings** (houses, churches, barns, the railway station, farmhouse, school, inn, manor, town hall, windmill, factory, market, stilt house...) and **bridge pieces** (stone or timber; lay them across a river tile by tile and they join up) are here too, placed one at a time; they belong to nobody |
| Erase props | Remove props inside the brush |
| Water level | Water fills every part of the map below this height |

### Units tab

- **Place units.** Choose the side, then a unit from the palette. It is
  listed under headings: infantry, vehicles, base buildings, defences and
  fortifications (civilian buildings are on the Terrain tab, with the props).
  Pick one heading from the list to see only that kind; tick *Every faction*
  to mix factions. While placing a building, a grid shows the tiles around the
  cursor. Structures go down exactly on the grid,
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
(Germany, Russia or Britain), colour, team, starting supplies (*Give now* sets them in
the running battle too), and whether a human or the computer (easy, normal, hard) plays
it; the change takes effect at once. Sides on the same **team**
are allies: they share vision and never shoot each other. **Start view here**
sets where that side's camera starts. **Command this side** hands you control
of it (Tab also cycles through the sides).

### Battle tab

- **Creation tools**: the rules editor (see
  [MODDING.md](MODDING.md)); rule changes take effect in this battle at once.
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
winter, desert, tropical, urban), `--zoom 2`, `--classic` (the old 2D sprite
view), `--yaw 200 --pitch 35` (start with the view turned and tilted, in
degrees), `--spawn 5000` (extra units at
start), `--demo` (the two armies attack each other), `--no-bases` (armies only,
no bases), `--debug` (open the debug panel), `--editor` (start with the editor
open), `--open path\to\map.lmap` (start on a saved map), `--menu` (the main
menu; the default when started with no options), `--ai-battle` (watch two
computer players; add `--side 1` to watch from the Soviet side, `--sides 3` for every side for itself, `--sides 4`
for two against two, and `--factions gbr,rus` (or `ger,gbr,rus,gbr` with four
sides) to choose who plays each side), `--gallery` (every unit and building lined up, for looking
at the art; with `--factions gbr` one army only), `--works-demo` (trenches and sandbag walls
manned by every kind of infantry), `--town-demo` (every civilian building), `--bridge-demo`
(with `--ai-battle --layout 0`: a column crossing a bridge), `--org-demo` (a platoon, an armoured platoon, a held building and
intel chips), `--intel` (with `--ai-battle`: watch with the first side's intel), `--export-sprites folder` (write every sprite as a PNG for
repainting; see [ART_PIPELINE.md](ART_PIPELINE.md)), `--tool rules:ger_medium_tank`,
or `--tool mods` (open a creation tool at start),
`--mods-selftest` (checks the rules editor and mods through the
real game), `--no-sound`, `--campaign gbr` or `--campaign gbr:2` (open a
campaign, or a mission's briefing), `--mission tutorial:1` (play a mission
straight away).

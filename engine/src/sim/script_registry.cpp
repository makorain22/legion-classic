// Pioneer Engine - the script registry: every condition and action, described once.
#include "pioneer/sim/script_registry.hpp"

#include <algorithm>
#include <set>

#include "pioneer/map/map.hpp"
#include "pioneer/sim/mission_lua.hpp"
#include "pioneer/sim/pathfinder.hpp"
#include "pioneer/sim/unit_types.hpp"

namespace pioneer::script {

namespace {
// --- Field makers ---------------------------------------------------------------
Field side(const char* help = "The side it is about.", const char* label = "Side") {
  return {Slot::Player, Kind::Player, "player", label, help, 0, 0, 7, "", false};
}
Field sideOrAll(const char* help = "The side it is for, or everyone.", const char* label = "Side") {
  return {Slot::Player, Kind::PlayerOrAny, "player", label, help, ANY_PLAYER, 0, 255, "", false};
}
Field anySide(const char* help = "The side it counts, or every side together.") {
  return {Slot::Player, Kind::PlayerOrAny, "player", "Side", help, 0, 0, 255, "", false};
}
Field side2(const char* help, const char* label = "Other side") {
  return {Slot::Player2, Kind::Player, "player2", label, help, 0, 0, 7, "", false};
}
Field area(const char* help = "The area it looks at.", const char* label = "Area") {
  return {Slot::Area, Kind::Area, "area", label, help, -1, 0, INT32_MAX, "", false};
}
Field areaOrMap(const char* help = "An area, or none for the whole map.", const char* label = "Area") {
  return {Slot::Area, Kind::AreaOrMap, "area", label, help, -1, -1, INT32_MAX, "", true};
}
Field area2(const char* help, const char* label = "To area") {
  return {Slot::Area2, Kind::Area, "area2", label, help, -1, 0, INT32_MAX, "", false};
}
Field number(const char* label, const char* help, int32_t def = 1, int32_t min = 0, int32_t max = 1000000) {
  return {Slot::Value, Kind::Int, "value", label, help, def, min, max, "", false};
}
Field seconds(const char* label, const char* help, int32_t def = 60, int32_t max = 86400) {
  return {Slot::Value, Kind::Seconds, "value", label, help, def, 0, max, "", false};
}
Field percent(const char* label, const char* help, int32_t def = 50, int32_t max = 100) {
  return {Slot::Value, Kind::Percent, "value", label, help, def, 0, max, "", false};
}
Field onOff(const char* label, const char* help, int32_t def = 1) {
  return {Slot::Value, Kind::Bool, "value", label, help, def, 0, 1, "", false};
}
Field choice(const char* label, const char* help, const char* choices, int32_t def = 0) {
  int n = 1;
  for (const char* p = choices; *p; ++p) n += *p == '|';
  return {Slot::Value, Kind::Choice, "value", label, help, def, 0, n - 1, choices, false};
}
Field objective(const char* help = "Its number: the first objective a script adds is 0, the next 1...") {
  return {Slot::Value, Kind::Objective, "value", "Objective", help, 0, 0, 999, "", false};
}
Field unitType(const char* help = "Only this type of unit, or any.", bool optional = true, const char* label = "Unit type") {
  return {Slot::UnitType, Kind::UnitType, "unit", label, help, -1, -1, INT32_MAX, "", optional};
}
Field trigger(const char* help, const char* label = "Trigger") {
  return {Slot::Trigger, Kind::Trigger, "trigger", label, help, -1, 0, INT32_MAX, "", false};
}
Field text(Kind kind, const char* label, const char* help, bool optional = false) {
  return {Slot::Text, kind, "text", label, help, 0, 0, 0, "", optional};
}
Field unitName(const char* help = "The name the units were given (Name units, Spawn named, or in the editor's inspector).") {
  return text(Kind::UnitName, "Unit name", help);
}
Field model(Kind kind, const char* label, const char* help) {
  return {Slot::Value, kind, "value", label, help, 0, 0, INT32_MAX, "", false};
}
Field scaled() {
  return {Slot::Scaled, Kind::Bool, "scaled", "Scale with difficulty", "70% as many on easy, 135% on hard.", 0, 0, 1, "", true};
}

std::vector<Entry> buildConditions() {
  std::vector<Entry> v(static_cast<size_t>(CondType::Count));
  auto C = [&](CondType t, const char* group, const char* summary, const char* example, const char* pitfalls,
               std::vector<Field> fields) {
    Entry& e = v[static_cast<size_t>(t)];
    e.action = false;
    e.type = static_cast<int>(t);
    e.id = condId(t);
    e.name = condName(t);
    e.group = group;
    e.summary = summary;
    e.example = example;
    e.pitfalls = pitfalls;
    e.fields = std::move(fields);
  };
  const char* kTime = "Time and chance";
  const char* kUnits = "Units";
  const char* kNamed = "Named units";
  const char* kBuild = "Structures";
  const char* kSides = "Sides";
  const char* kState = "Mission state";
  const char* kCamp = "Campaign and tutorials";

  C(CondType::Always, kTime, "True from the first moment of the mission.",
    "A trigger with only this condition runs its actions at the start: the opening radio message, the first objectives.",
    "On a repeating trigger it fires once a second for ever.", {});
  C(CondType::Timer, kTime, "True once this many seconds have passed since the mission began; on a repeating trigger, since it last fired.",
    "Timer 300: the counter-attack comes five minutes in. Repeating with Timer 90: a patrol every minute and a half.",
    "It counts from the start of the mission, not from when the trigger was switched on. To count from an event, add "
    "'Trigger fired' with a delay, or use a countdown.",
    {seconds("Seconds", "How long to wait.")});
  C(CondType::UnitsInArea, kUnits, "True while the side has at least this many units in the area.",
    "Side 0 has 5 units in 'Bridge': the bridge is reached.",
    "Buildings in the area count too (field works don't) unless a unit type is chosen. Allies of the side are not "
    "counted: add a condition for each, with 'Any one'.",
    {anySide(), area(), number("At least", "How many units.", 1, 1), unitType()});
  C(CondType::NoUnitsInArea, kUnits, "True while the side has no unit (of the type) in the area.",
    "The enemy has no units in 'Village': the village is cleared.",
    "True at once if the side never had units there. Buildings count as units unless a unit type is chosen: a side "
    "with a house in the area still has something there.",
    {anySide(), area(), unitType()});
  C(CondType::UnitCount, kUnits, "True while the side has at most this many units on the whole map.",
    "Side 1 has at most 0 units: the enemy force is destroyed. At most 3 tanks: time to send the reserve.",
    "Buildings count too (field works don't) unless a unit type is chosen, so 'at most 0' waits for the last "
    "building as well. A side that has not arrived yet has 0: it is true before the first wave lands.",
    {anySide(), number("At most", "How many units.", 0), unitType()});
  C(CondType::PlayerDefeated, kSides, "True once the side has been defeated (nothing left, or a script said so).",
    "Side 2 is defeated: the allied garrison has fallen, fail the objective.", "", {side()});
  C(CondType::Supplies, kSides, "True while the side has at least this many supplies.",
    "Side 0 has 3000 supplies: enough is stockpiled for the offensive.", "", {side(), number("At least", "Supplies.", 1000)});
  C(CondType::TriggerFired, kState, "True once another trigger has fired, at least so many seconds ago.",
    "'Bridge taken' fired 20 s ago: the enemy answers twenty seconds after the bridge falls.",
    "It stays true after the other trigger has fired (for one that repeats, the delay counts from its latest firing). "
    "Triggers are referred to by number: deleting or reordering triggers by hand in a map file moves the numbers.",
    {trigger("The trigger that must have fired."), seconds("Seconds ago", "How long ago, at least (0: as soon as it has).", 0)});
  C(CondType::UnitsAtLeast, kUnits, "True while the side has at least this many units on the whole map.",
    "Side 0 has 40 'ger_rifleman': the build-up is complete.", "Buildings count too (field works don't) unless a unit type is chosen.",
    {anySide(), number("At least", "How many units.", 1, 1), unitType()});
  C(CondType::InCover, kCamp, "True while the side has at least this many men inside buildings, bunkers or trenches.",
    "Tutorial: six men in cover, go on to the next lesson.", "", {side(), number("At least", "How many men.", 1, 1)});
  C(CondType::OrderGiven, kCamp, "True once the (human) side has given at least this many orders of a kind.",
    "Tutorial: the player has given one attack-move order.",
    "Orders by the computer and by scripts are not counted.",
    {side(), number("At least", "How many orders.", 1, 1),
     {Slot::Order, Kind::Order, "order", "Order", "The kind of order, or any.", -1, -1, 31, "", true}});
  C(CondType::FlagAtLeast, kCamp, "True if a campaign flag is at least this value. Flags are kept from mission to mission.",
    "Flag 'tiger_survived' at least 1: the Tiger from the last mission joins this one.",
    "A flag set in this mission counts at once; flags only reach later missions if this one is won.",
    {text(Kind::Flag, "Flag", "The flag's name."), number("At least", "Its value.", 1, -1000000)});
  C(CondType::NamedAlive, kNamed, "True while a unit with this name is alive.", "'Adler' is alive (with 'not': Adler is lost).",
    "With no unit of that name it is false, so 'not Named alive' fires at once if the name is misspelt or the unit has "
    "not arrived yet.",
    {unitName()});
  C(CondType::NamedDead, kNamed, "True when no unit with this name is alive.", "'Flak tower' is dead: the way is open.",
    "Also true before units of that name exist. Guard it with 'Trigger fired' (the trigger that spawns them).", {unitName()});
  C(CondType::NamedInArea, kNamed, "True while at least this many units with the name are in the area.",
    "'Convoy' has 3 units in 'Depot': the convoy got through.", "", {unitName(), area(), number("At least", "How many.", 1, 1)});
  C(CondType::NamedHealthBelow, kNamed, "True while a unit with the name has less than this share of its health.",
    "'Command tank' below 30%: it pulls out.", "", {unitName(), percent("Below", "Percent of full health.", 50)});
  C(CondType::StructuresAtMost, kBuild, "True while the side has at most this many structures (of the type) in the area or on the map.",
    "Side 1 has at most 0 'ger_flak_tower' in 'Airfield': the guns are silenced.",
    "With no type, field works (wire, trenches, sandbags) are not counted.",
    {sideOrAll("The side, or every side."), number("At most", "How many structures.", 0), unitType("Only this type of structure, or any but field works."), areaOrMap()});
  C(CondType::StructuresAtLeast, kBuild, "True while the side has at least this many structures (of the type) in the area or on the map.",
    "Side 0 has 2 barracks: the base is established.", "With no type, field works are not counted.",
    {sideOrAll("The side, or every side."), number("At least", "How many structures.", 1, 1), unitType("Only this type of structure, or any but field works."), areaOrMap()});
  C(CondType::UnitsInAreaAtMost, kUnits, "True while the side has at most this many units in the area.",
    "The enemy has at most 2 units in 'Ridge': the ridge is as good as taken.",
    "Buildings count too (field works don't) unless a unit type is chosen.", {anySide(), areaOrMap(), number("At most", "How many units.", 0), unitType()});
  C(CondType::AreaControlled, kUnits, "True while the side and its allies have at least this many units in the area and no enemy has any.",
    "Side 0 controls 'Crossroads' with 4 units: hold it for the clock to run.",
    "A single enemy straggler hiding in the area keeps it false.", {side(), area(), number("At least", "How many units.", 1, 1)});
  C(CondType::CountdownDone, kState, "True once the countdown of this name has run out.", "'Relief arrives' has run out: victory.",
    "A trigger that stops or restarts the countdown hides this from every trigger after it in the list. Put the trigger "
    "that reacts to the clock running out before any that stop it.",
    {text(Kind::Countdown, "Countdown", "The countdown's name.")});
  C(CondType::CountdownBelow, kState, "True while the countdown is running with at most this many seconds left.",
    "'Evacuation' below 60 s: the last train whistles.", "False once it has run out: use 'Countdown done' for that.",
    {text(Kind::Countdown, "Countdown", "The countdown's name."), seconds("Seconds left", "At most this many.", 60)});
  C(CondType::CounterAtLeast, kState, "True while a mission counter is at least this value.",
    "'trucks_through' at least 5: enough supplies arrived.", "A counter nobody has set is 0.",
    {text(Kind::Counter, "Counter", "The counter's name."), number("At least", "Its value.", 1, -1000000)});
  C(CondType::CounterAtMost, kState, "True while a mission counter is at most this value.", "'lives' at most 0: the mission is lost.",
    "A counter nobody has set is 0, so 'at most 0' is true from the start.",
    {text(Kind::Counter, "Counter", "The counter's name."), number("At most", "Its value.", 0, -1000000)});
  C(CondType::KillsAtLeast, kSides, "True once the side has destroyed at least this many enemy units and structures.",
    "Side 0 has 30 kills: the enemy calls in armour.", "Field works are not counted.", {side(), number("At least", "Kills.", 10, 1)});
  C(CondType::LossesAtLeast, kSides, "True once the side has lost at least this many units and structures.",
    "Side 0 has lost 40: the attack is called off.", "Field works are not counted.", {side(), number("At least", "Losses.", 10, 1)});
  C(CondType::ObjectiveDone, kState, "True once the objective is done.", "Objective 0 is done: add objective 1.", "", {objective()});
  C(CondType::ObjectiveFailed, kState, "True once the objective has failed.", "Objective 2 failed: the bridge is lost, change the plan.", "", {objective()});
  C(CondType::DifficultyAtLeast, kState, "True if the mission is played at this difficulty or harder.",
    "At least hard: an extra wave. With 'not': a helping hand on easy.", "", {choice("Difficulty", "The campaign setting.", "easy|normal|hard", 2)});
  C(CondType::Chance, kTime, "True this share of the times it is checked. The dice are the simulation's, so a replay rolls the same.",
    "Timer 120, repeating, Chance 50: every two minutes, a raid half the time.",
    "On a trigger that doesn't repeat it is rolled three times a second until it comes up, which is soon: pair it with a "
    "repeating timer.",
    {percent("Percent", "How often it is true.", 50)});
  C(CondType::SuppliesAtMost, kSides, "True while the side has at most this many supplies.", "Side 0 has at most 200: send the supply convoy.", "",
    {side(), number("At most", "Supplies.", 0)});
  C(CondType::HealthBelow, kUnits, "True while the side's units (of the type) in the area have, together, less than this share of their full health.",
    "The garrison in 'Fort' is below 40%: it surrenders.", "Also true when there are no such units in the area (none left).",
    {anySide(), area(), unitType(), percent("Below", "Percent of their full health in total.", 50)});
  C(CondType::AlliedWith, kSides, "True while the side is allied with another.", "Side 2 is allied with side 0: the mutineers have come over.", "",
    {side(), {Slot::Value, Kind::Player, "value", "With side", "The other side.", 0, 0, 7, "", false}});
  const char* kModel = "Mission model";
  C(CondType::PhaseIs, kModel, "True while the mission is in this phase.", "Phase is 2 (with 'not': any phase but the second).",
    "A trigger can simply belong to a phase instead: then it is only checked while that phase is current.",
    {model(Kind::Phase, "Phase", "The phase's number: the first phase is 0.")});
  C(CondType::PhaseElapsed, kModel, "True once the current phase has been running this many seconds.",
    "Phase running for 120 s: two minutes into the defence, the tanks arrive.",
    "It is about whichever phase is current. Put the trigger in the phase you mean.",
    {seconds("Seconds", "Since the phase began.", 60)});
  C(CondType::GoalDone, kModel, "True once a goal of the mission model is done.", "Goal 0 done: the bridge is taken, go to the next phase.",
    "Goals can run actions of their own when done; this condition is for everything else that should wait on one.",
    {model(Kind::Goal, "Goal", "The goal's number: the first goal is 0.")});
  C(CondType::GoalFailed, kModel, "True once a goal of the mission model has failed.", "Goal 1 failed: the convoy is lost.", "",
    {model(Kind::Goal, "Goal", "The goal's number: the first goal is 0.")});
  C(CondType::ScriptSays, kModel, "True when a function of the mission's Lua script returns true.",
    "Script says 'flank_is_open': a test too involved for conditions, written in Lua.",
    "The function is called three times a second while the trigger waits: keep it cheap, and don't change the world from it.",
    {text(Kind::Text, "Function", "The name of a global function of the mission's Lua script.")});
  return v;
}

std::vector<Entry> buildActions() {
  std::vector<Entry> v(static_cast<size_t>(ActionType::Count));
  auto A = [&](ActionType t, const char* group, const char* summary, const char* example, const char* pitfalls,
               std::vector<Field> fields) {
    Entry& e = v[static_cast<size_t>(t)];
    e.action = true;
    e.type = static_cast<int>(t);
    e.id = actionId(t);
    e.name = actionName(t);
    e.group = group;
    e.summary = summary;
    e.example = example;
    e.pitfalls = pitfalls;
    e.takesUnits = actionTakesUnits(t);
    auto has = [&](Slot s) {
      for (const Field& f : fields)
        if (f.slot == s) return true;
      return false;
    };
    if (e.takesUnits) {
      // The units it acts on: by name, or by side, area and type.
      std::vector<Field> pick;
      if (!actionUsesText(t) && !has(Slot::Text)) {
        Field n = unitName("Act on the units with this name. Leave empty to pick them by side, area and type instead.");
        n.optional = true;
        pick.push_back(n);
      }
      if (!has(Slot::Player)) {
        Field f = sideOrAll("Whose units, or every side's (when not picked by name).");
        f.def = 0;
        pick.push_back(f);
      }
      if (!has(Slot::Area)) pick.push_back(areaOrMap("Units in this area, or anywhere on the map.", "In area"));
      if (!has(Slot::UnitType)) pick.push_back(unitType());
      fields.insert(fields.begin(), pick.begin(), pick.end());
    }
    e.fields = std::move(fields);
  };
  const char* kStory = "Messages and story";
  const char* kGoals = "Objectives";
  const char* kEnd = "Winning and losing";
  const char* kUnits = "Units";
  const char* kSet = "Set pieces";
  const char* kOrders = "Orders";
  const char* kSides = "Sides and supplies";
  const char* kAi = "Computer player";
  const char* kTrig = "Triggers";
  const char* kClock = "Clocks and counters";
  const char* kView = "Camera and sound";
  const char* kMap = "Map and terrain";
  const char* kUi = "Interface (tutorials)";
  const char* kCamp = "Campaign";

  A(ActionType::Message, kStory, "Shows a line of text to a side, or to everyone.", "\"Enemy armour sighted to the north.\"",
    "For a speaker's voice with the radio panel use Radio message instead.", {sideOrAll(), text(Kind::Text, "Text", "The words.")});
  A(ActionType::AddObjective, kGoals, "Adds an objective to a side's list. With an area it is marked on the map while open.",
    "\"Take the bridge\" at 'Bridge'. Objectives are numbered in the order they are added, from 0.",
    "The number is the order of adding at run time, not the order of triggers in the list: add objectives in a fixed "
    "order, or the wrong one gets completed.",
    {sideOrAll(), text(Kind::Text, "Text", "What the player must do."), onOff("Optional", "A secondary objective.", 0),
     areaOrMap("Where it is, to mark on the map (or none).", "At area")});
  A(ActionType::CompleteObjective, kGoals, "Marks an objective as done.", "Complete objective 0.", "", {objective()});
  A(ActionType::FailObjective, kGoals, "Marks an objective as failed.", "Fail objective 1: the convoy was lost.", "", {objective()});
  A(ActionType::Victory, kEnd, "The side and its allies win the mission.", "After the last objective: Victory for side 0.",
    "A script with a Victory action switches off the usual win (all enemies destroyed): make sure it can be reached. "
    "Campaign flags set earlier in the mission are kept only if this is reached.",
    {side("Who wins.")});
  A(ActionType::Defeat, kEnd, "The side loses the mission.", "'Adler' is dead: Defeat for side 0.", "", {side("Who loses.")});
  A(ActionType::SpawnUnits, kUnits, "Creates units for a side in an area.", "8 'rus_rifleman' for side 1 in 'North road': a wave arrives.",
    "They appear standing in the area with no orders: follow with Attack-move.",
    {side("Whose they are."), area("Where they appear."), unitType("What to create.", false), number("How many", "Units to create.", 5, 1, 500),
     scaled()});
  A(ActionType::GiveSupplies, kSides, "Adds supplies to a side (a negative number takes them away).", "Side 0 gains 2000: the supply drop landed.", "",
    {side(), number("Supplies", "How many (negative: lose).", 1000, -1000000)});
  A(ActionType::RevealArea, kView, "The side sees the area from now on.", "Reveal 'Enemy HQ' to side 0 when the spy reports.", "It can't be hidden again.",
    {sideOrAll(), area()});
  A(ActionType::AttackMove, kOrders, "The units attack-move to an area: they march there and fight what they meet.",
    "Side 1's units in 'North road' attack-move to 'Village'.",
    "Units held by 'Set locked' still obey scripts. Nothing happens if no unit matches: check the area covers where they were spawned.",
    {area2("Where they go.")});
  A(ActionType::MoveUnits, kOrders, "The units move to an area without stopping to fight.", "'Convoy' moves to 'Depot'.",
    "For a route with several legs chain triggers with 'Named in area', or use a path (mission model).", {area2("Where they go.")});
  A(ActionType::ChangeOwner, kUnits, "The units go over to another side.", "Side 3's units in 'Barracks' go over to side 0: the garrison defects.", "",
    {side2("Their new side.", "New side")});
  A(ActionType::KillUnits, kUnits, "Destroys the units as if killed in battle (wrecks and bodies remain).", "Kill 'Bridge charge': the bridge goes up.",
    "It kills invulnerable units too. To remove units without a trace there is no action yet: move them off first.", {});
  A(ActionType::EnableTrigger, kTrig, "Switches a trigger on.", "Enable 'Counter-attack' once the village is taken.", "", {trigger("The trigger to switch on.")});
  A(ActionType::DisableTrigger, kTrig, "Switches a trigger off: it is not checked until switched on again.", "Disable 'Patrols' when the alarm is raised.", "",
    {trigger("The trigger to switch off.")});
  A(ActionType::SetComputer, kAi, "Hands a side to the computer at a difficulty, or back to a human.", "Side 2 is played by the computer, normal.",
    "On hard, the campaign already raises hostile computers one level.",
    {side(), {Slot::Value, Kind::Choice, "value", "Played by", "Who plays it.", 1, -1, 2, "human|easy|normal|hard", false}});
  A(ActionType::CameraTo, kView, "Jumps a side's view to an area.", "Camera to 'Bridge' when it is blown.", "For a glide use Camera pan.", {sideOrAll(), area()});
  A(ActionType::Hint, kUi, "Shows text in a side's hint panel until the next hint. The first line is the title; empty text closes the panel.",
    "\"Digging in\\nSelect your riflemen and right-click the trench.\"", "", {sideOrAll(), text(Kind::Text, "Text", "Title on the first line, then the hint.", true)});
  A(ActionType::Highlight, kUi, "Makes an interface element glow for a side (empty: none).",
    "'tab.train', 'build:gbr_power', 'order.evacuate', 'minimap'.", "", {sideOrAll(), text(Kind::Interface, "Element", "Which element.", true)});
  A(ActionType::LockBuild, kUi, "The side can't build a type (or anything).", "Lock everything, then unlock the barracks: build this first.", "",
    {side(), unitType("The type to forbid, or everything.")});
  A(ActionType::UnlockBuild, kUi, "The side can build a type (or everything) again.", "Unlock 'gbr_barracks'.", "", {side(), unitType("The type to allow, or everything.")});
  A(ActionType::SetFlag, kCamp, "Sets a campaign flag. The campaign keeps it for later missions when this one is won.",
    "'tiger_survived' = 1 if 'Tiger' is alive at the end.", "Set it before the Victory action in the same trigger, or in an earlier one.",
    {text(Kind::Flag, "Flag", "The flag's name."), number("Value", "What to set it to.", 1, -1000000)});
  A(ActionType::Countdown, kClock, "Starts (or restarts) a clock on screen that counts down.", "'Relief arrives' at 600 s, shown to everyone.",
    "Restarting a clock that has run out hides 'Countdown done' from later triggers.",
    {text(Kind::Countdown, "Countdown", "Its name, shown beside the clock."), seconds("Seconds", "How long it runs.", 300), sideOrAll("Who sees it.")});
  A(ActionType::StopCountdown, kClock, "Removes a clock.", "Stop 'Bridge demolition' when the charges are found.",
    "Later triggers no longer see it as done.", {text(Kind::Countdown, "Countdown", "Its name.")});
  A(ActionType::PauseCountdown, kClock, "Pauses a clock, or lets it run on.", "Pause 'Evacuation' during a cutscene.", "",
    {text(Kind::Countdown, "Countdown", "Its name."), onOff("Paused", "Paused, or running.")});
  A(ActionType::SetPlayArea, kMap, "Confines the battle to an area for a side (or everyone): orders, building and the camera stay inside. No area: the whole map.",
    "Start in 'Valley'; widen to 'Valley and town' when the bridge is taken.", "Units already outside stay where they are.",
    {sideOrAll(), areaOrMap("The play area, or none for the whole map.")});
  A(ActionType::NameUnits, kSet, "Gives a name to a side's units in an area (of a type). Named units can be picked by name in conditions and actions.",
    "Side 0's 'ger_heavy_tank' in 'Start' are called 'Tiger'.", "",
    {side("Whose units."), area("Units in this area."), unitType(), text(Kind::UnitName, "Name", "The name to give."),
     number("At most", "How many to name (0: all).", 0)});
  A(ActionType::SetInvulnerable, kSet, "The units take no damage, or can be hurt again. For set pieces that must survive until the story lets them die.",
    "'Column' is invulnerable until the ambush trigger.", "Kill units and Damage units still hurt them.", {onOff("Invulnerable", "On or off.")});
  A(ActionType::SetAiControl, kSet, "The computer may command the units, or leaves them to the script.", "'Reserve' is held back from the computer until phase two.", "",
    {onOff("Computer commands them", "On: the computer may use them.")});
  A(ActionType::SetTargetable, kSet, "Enemies pick the units as targets on their own, or ignore them.", "'Staff car' can't be targeted during the opening.",
    "A player can still order an attack on them.", {onOff("Targetable", "On: enemies shoot at them.")});
  A(ActionType::SetLocked, kSet, "The owning player can't give the units orders, or can again.", "'Escort' is locked while it follows the script.", "",
    {onOff("Locked", "On: the player can't command them.")});
  A(ActionType::SetHealth, kSet, "Sets the units' health to a share of their maximum.", "'Wrecked column' at 20%.", "", {percent("Health", "Percent of maximum.", 100)});
  A(ActionType::DamageUnits, kSet, "The units lose a share of their maximum health; they can die of it.", "Damage 'Bunker' by 50% when the bombers pass.", "",
    {percent("Damage", "Percent of maximum health lost.", 25)});
  A(ActionType::SetStance, kOrders, "The units take a stance.", "'Ambush party' holds fire.", "",
    {choice("Stance", "How they behave on their own.", "aggressive|hold ground|hold fire")});
  A(ActionType::Patrol, kOrders, "The units walk between two areas until given other orders.", "'Sentries' patrol between 'Gate' and 'Depot'.", "",
    {area2("The other end of the beat.")});
  A(ActionType::Garrison, kOrders, "The infantry go into buildings, bunkers or trenches in an area (their side's or empty).", "Side 1's men in 'Village' garrison 'Village'.", "",
    {area2("Where the buildings are.", "Buildings in")});
  A(ActionType::Evacuate, kOrders, "Men inside buildings in the area come out.", "Evacuate 'Farm' when it catches fire.", "", {side("Whose men."), area("Buildings in this area.")});
  A(ActionType::BindUnits, kOrders, "The units are bound into one fighting unit (squad, platoon...).", "Bind the wave so it marches as one body.", "", {});
  A(ActionType::Barrage, kSet, "Shells fall on an area.", "20 shells on 'Trench line', fired by side 1.", "",
    {area("Where they fall."), number("Shells", "How many.", 12, 1, 500), sideOrAll("The side that fires (for kills and return fire), or nobody's guns.", "Fired by"),
     text(Kind::Weapon, "Weapon", "Which gun's shell (empty: a field gun).", true)});
  A(ActionType::SetAlliance, kSides, "Makes two sides allies, or enemies.", "Side 3 becomes an ally of side 0: the garrison changes sides.", "",
    {side(), side2("The side it joins or leaves.", "With side"), onOff("Allied", "On: allies. Off: enemies.")});
  A(ActionType::AiAttack, kAi, "The computer playing a side sends its main force against an area, at once, with what it has.", "Side 1's computer attacks 'Bridgehead'.",
    "The side must be played by the computer (Set computer).", {side(), area("The target.")});
  A(ActionType::AiDefend, kAi, "The computer playing a side keeps its main force in an area and defends it.", "Side 2's computer defends 'Town'.", "", {side(), area("What to hold.")});
  A(ActionType::AiFree, kAi, "The computer playing a side chooses its own targets again.", "After the scripted assault: AI free.", "", {side()});
  A(ActionType::AiBehaviour, kAi, "Allows or forbids one thing the computer does: build, train, attack, raid, capture, fortify, scout, repair.",
    "A garrison's computer may not build or train: it fights with what it has.", "",
    {side(), text(Kind::AiBehaviour, "Behaviour", "build, train, attack, raid, capture, fortify, scout or repair."), onOff("Allowed", "On: it may.")});
  A(ActionType::AiWaveSize, kAi, "The computer playing a side attacks with this share of its usual force.", "50%: probing attacks early on.", "",
    {side(), percent("Percent", "Of its usual attack size (0: usual).", 100, 400)});
  A(ActionType::SetCounter, kClock, "Sets a mission counter.", "'lives' = 3.", "", {text(Kind::Counter, "Counter", "Its name."), number("Value", "What to set it to.", 0, -1000000)});
  A(ActionType::AddCounter, kClock, "Adds to a mission counter (a negative number takes away).", "'trucks_through' + 1 each time a truck reaches the depot.", "",
    {text(Kind::Counter, "Counter", "Its name."), number("Add", "How much.", 1, -1000000)});
  A(ActionType::AddFlag, kCamp, "Adds to a campaign flag.", "'defected' + 4 for each battalion that comes over.", "",
    {text(Kind::Flag, "Flag", "Its name."), number("Add", "How much.", 1, -1000000)});
  A(ActionType::SpawnFromFlag, kCamp, "Creates as many units as a campaign flag says.", "As many 'ger_rifleman' as 'brenner_men' (at most 30) in 'Camp'.", "",
    {side("Whose they are."), area("Where they appear."), unitType("What to create.", false), text(Kind::Flag, "Flag", "The flag that says how many."),
     number("At most", "The most to create (0: 50).", 0, 0, 500), scaled()});
  A(ActionType::Transmission, kStory, "A radio message: the speaker's name and words appear in the radio panel, typed out, with the radio sound.",
    "\"Oster: The bridge must hold until nightfall.\"", "Write 'Speaker: words'.",
    {sideOrAll(), text(Kind::Text, "Text", "Speaker: words."), seconds("Seconds shown", "How long it stays (0: by its length).", 0, 120)});
  A(ActionType::Cinematic, kView, "Letterbox bars and no orders for a side, or back to play.", "On for the opening pan, off when it ends.",
    "Always pair an 'on' with an 'off' that is certain to fire: the player can do nothing meanwhile.", {sideOrAll(), onOff("Cinematic", "On or off.")});
  A(ActionType::CameraPan, kView, "A side's view glides to an area over some seconds.", "Pan to 'Enemy column' over 6 s.", "", {sideOrAll(), area(), seconds("Seconds", "How long the glide takes.", 5, 120)});
  A(ActionType::MarkArea, kView, "Puts a named marker on an area and the minimap.", "Mark 'Bridge' as a goal, 'Minefield' as danger.", "",
    {sideOrAll(), area(), text(Kind::Marker, "Name", "The marker's name, shown on the map."), choice("Kind", "How it is drawn.", "goal|danger|note")});
  A(ActionType::ClearMarker, kView, "Removes a marker (empty name: all of them).", "Clear 'Bridge' once it is taken.", "", {text(Kind::Marker, "Name", "The marker's name.", true)});
  A(ActionType::PlaySound, kView, "Plays a sound for a side, at an area or everywhere.", "'explosion_large' at 'Bridge'.", "", {sideOrAll(), text(Kind::Sound, "Sound", "A sound of audio.json."), areaOrMap("Where it sounds from (or everywhere).", "At area")});
  A(ActionType::PlayMusic, kView, "Plays a piece or a mood, held until released.", "'battle' for the assault; empty to let the music follow the fighting again.", "",
    {sideOrAll(), text(Kind::Music, "Music", "A track, or calm, battle, victory, defeat, menu (empty: automatic).", true)});
  A(ActionType::SetIncomeRate, kSides, "The side's income runs at this share of normal.", "Side 0 at 50%: the supply line is cut.", "", {side(), percent("Percent", "Of normal income.", 100, 1000)});
  A(ActionType::AddLogisticsPoint, kMap, "A new logistics point appears in the middle of an area.", "A point at 'Rail yard' when the line is repaired.", "", {area()});
  A(ActionType::FireTrigger, kTrig, "Runs another trigger's actions now; it counts as fired.", "Fire 'Alarm' from several triggers.", "Its conditions are not checked.",
    {trigger("The trigger to run.")});
  A(ActionType::ResetTrigger, kTrig, "A trigger can fire again as if it never had, and is switched on.", "Reset 'Wave' to send it once more.", "", {trigger("The trigger to reset.")});
  A(ActionType::HideObjective, kGoals, "Takes an objective off the list.", "Hide objective 1 when the plan changes.", "", {objective()});
  A(ActionType::SpawnNamed, kSet, "Creates units and gives them a name in one go.", "3 'ger_heavy_tank' in 'Hill' called 'Tigers'.", "",
    {side("Whose they are."), area("Where they appear."), unitType("What to create.", false), number("How many", "Units to create.", 1, 1, 500),
     text(Kind::UnitName, "Name", "The name they get."), scaled()});
  A(ActionType::FreezeWater, kMap, "The water in an area freezes: men and wheels can cross it, tracks can't. Shells break it.", "Freeze 'River' when winter comes.", "", {area()});
  A(ActionType::BreakIce, kMap, "The ice in an area breaks up; whatever stands on it goes into the water.", "Break 'Ford' when the thaw sets in.", "", {area()});
  A(ActionType::FloodArea, kMap, "The land in an area floods waist deep: men wade, vehicles can't cross.", "Flood 'Polder' when the dyke is blown.",
    "Units standing there that can't wade are lost.", {area()});
  A(ActionType::DrainArea, kMap, "Flooded land in an area is pumped dry again.", "Drain 'Polder' when the pumping station is taken.", "", {area()});
  const char* kModel = "Mission model";
  A(ActionType::GotoPhase, kModel, "The mission moves to a phase: its on-entry actions run, its triggers and goals begin.",
    "When the bridge is blown: go to phase 'Ford crossing' instead of 'Over the bridge'.",
    "Triggers that belong to the phase left stop being checked at once, even later ones in the same check.",
    {model(Kind::Phase, "Phase", "The phase's number: the first phase is 0.")});
  A(ActionType::SendWave, kModel, "Sends a wave of the mission model: after its delay its units arrive where it says, as often as it says, and carry out its orders.",
    "Send wave 'Northern attack' when the village falls.",
    "Sending a wave that is still coming sends it a second time alongside the first.",
    {model(Kind::Wave, "Wave", "The wave's number: the first wave is 0.")});
  A(ActionType::FollowPath, kModel, "The units follow a path of the mission model point by point, to its end (or round and round, if the path loops).",
    "'Convoy' follows path 'Supply road'.",
    "They go on to the next point when all of them have arrived or stopped. Any other order takes units off the path for good.",
    {model(Kind::Path, "Path", "The path's number: the first path is 0."),
     {Slot::Player2, Kind::Choice, "player2", "On the way", "March, or fight what they meet.", 0, 0, 1, "march|fight", false}});
  A(ActionType::CallScript, kModel, "Calls a function of the mission's Lua script, passing it a number.",
    "Call script 'reinforce' with 3: Lua decides what arrives.",
    "An error in the function is reported (console: lua.errors) and the mission goes on.",
    {text(Kind::Text, "Function", "The name of a global function of the mission's Lua script."),
     number("Number", "Passed to the function.", 0, -1000000)});
  return v;
}
}  // namespace

const Field* Entry::field(Slot s) const {
  for (const Field& f : fields)
    if (f.slot == s) return &f;
  return nullptr;
}
const Field* Entry::fieldByKey(const std::string& key) const {
  for (const Field& f : fields)
    if (key == f.key) return &f;
  return nullptr;
}

const std::vector<Entry>& conditions() {
  static const std::vector<Entry> v = buildConditions();
  return v;
}
const std::vector<Entry>& actions() {
  static const std::vector<Entry> v = buildActions();
  return v;
}
const Entry& entry(CondType t) { return conditions()[static_cast<size_t>(t) % conditions().size()]; }
const Entry& entry(ActionType t) { return actions()[static_cast<size_t>(t) % actions().size()]; }
const Entry* findCondition(const std::string& id) {
  for (const Entry& e : conditions())
    if (id == e.id) return &e;
  return nullptr;
}
const Entry* findAction(const std::string& id) {
  for (const Entry& e : actions())
    if (id == e.id) return &e;
  return nullptr;
}

int32_t get(const Condition& c, Slot s) {
  switch (s) {
    case Slot::Player: return c.player;
    case Slot::Area: return c.area;
    case Slot::Value: return c.value;
    case Slot::UnitType: return c.unitType;
    case Slot::Trigger: return c.trigger;
    case Slot::Order: return c.order;
    default: return 0;
  }
}
void set(Condition& c, Slot s, int32_t v) {
  switch (s) {
    case Slot::Player: c.player = static_cast<uint8_t>(v); break;
    case Slot::Area: c.area = v; break;
    case Slot::Value: c.value = v; break;
    case Slot::UnitType: c.unitType = v; break;
    case Slot::Trigger: c.trigger = v; break;
    case Slot::Order: c.order = v; break;
    default: break;
  }
}
int32_t get(const Action& a, Slot s) {
  switch (s) {
    case Slot::Player: return a.player;
    case Slot::Player2: return a.player2;
    case Slot::Area: return a.area;
    case Slot::Area2: return a.area2;
    case Slot::Value: return a.value;
    case Slot::UnitType: return a.unitType;
    case Slot::Trigger: return a.trigger;
    case Slot::Scaled: return a.scaled ? 1 : 0;
    default: return 0;
  }
}
void set(Action& a, Slot s, int32_t v) {
  switch (s) {
    case Slot::Player: a.player = static_cast<uint8_t>(v); break;
    case Slot::Player2: a.player2 = static_cast<uint8_t>(v); break;
    case Slot::Area: a.area = v; break;
    case Slot::Area2: a.area2 = v; break;
    case Slot::Value: a.value = v; break;
    case Slot::UnitType: a.unitType = v; break;
    case Slot::Trigger: a.trigger = v; break;
    case Slot::Scaled: a.scaled = v != 0; break;
    default: break;
  }
}

namespace {
template <class T>
std::string describeItem(const Entry& e, const T& item, const ScenarioScript* s) {
  std::string out = e.name;
  bool first = true;
  auto named = [](const auto& list, int32_t v, const char* none) -> std::string {
    if (v < 0) return none;
    if (static_cast<size_t>(v) < list.size()) return list[static_cast<size_t>(v)].name;
    return "#" + std::to_string(v);
  };
  for (const Field& f : e.fields) {
    std::string value;
    if (f.slot == Slot::Text) {
      if (item.text.empty()) continue;
      value = item.text.size() > 40 ? item.text.substr(0, 37) + "..." : item.text;
      if (f.kind == Kind::Text) value = "\"" + value + "\"";
    } else {
      const int32_t v = get(item, f.slot);
      switch (f.kind) {
        case Kind::Player:
        case Kind::PlayerOrAny: value = v == ANY_PLAYER ? "everyone" : "side " + std::to_string(v + 1); break;
        case Kind::Area:
        case Kind::AreaOrMap: value = s ? named(s->areas, v, "the whole map") : std::to_string(v); break;
        case Kind::Trigger: value = s ? named(s->triggers, v, "none") : std::to_string(v); break;
        case Kind::Phase: value = s ? named(s->phases, v, "none") : std::to_string(v); break;
        case Kind::Wave: value = s ? named(s->waves, v, "none") : std::to_string(v); break;
        case Kind::Path: value = s ? named(s->paths, v, "none") : std::to_string(v); break;
        case Kind::Goal: value = s && v >= 0 && static_cast<size_t>(v) < s->goals.size() ? s->goals[static_cast<size_t>(v)].text : std::to_string(v); break;
        case Kind::Bool:
          if (v == 0) continue;  // said only when it is on
          value = "yes";
          break;
        case Kind::Percent: value = std::to_string(v) + "%"; break;
        case Kind::Seconds: value = std::to_string(v) + " s"; break;
        case Kind::UnitType:
          if (v < 0) continue;
          value = "type " + std::to_string(v);
          break;
        case Kind::Choice: {
          int at = v - f.min;
          const char* p = f.choices ? f.choices : "";
          while (at > 0 && *p) {
            if (*p == '|') --at;
            ++p;
          }
          const char* end = p;
          while (*end && *end != '|') ++end;
          value = at == 0 && end > p ? std::string(p, end) : std::to_string(v);
          break;
        }
        default: value = std::to_string(v); break;
      }
    }
    std::string label = f.label;
    if (!label.empty()) label[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(label[0])));
    out += first ? ": " : ", ";
    out += label + " " + value;
    first = false;
  }
  return out;
}
}  // namespace

std::string describe(const Condition& c, const ScenarioScript* s) {
  if (c.type >= CondType::Count) return "unknown condition";
  return describeItem(entry(c.type), c, s);
}
std::string describe(const Action& a, const ScenarioScript* s) {
  if (a.type >= ActionType::Count) return "unknown action";
  return describeItem(entry(a.type), a, s);
}

Condition makeCondition(CondType t) {
  Condition c;
  c.type = t;
  for (const Field& f : entry(t).fields)
    if (f.slot != Slot::Text) set(c, f.slot, f.def);
  return c;
}
Action makeAction(ActionType t) {
  Action a;
  a.type = t;
  for (const Field& f : entry(t).fields)
    if (f.slot != Slot::Text) set(a, f.slot, f.def);
  return a;
}

// ---------------------------------------------------------------------------
// Checking
// ---------------------------------------------------------------------------
std::vector<Issue> check(const ScenarioScript& s, const CheckContext& ctx) {
  std::vector<Issue> out;
  auto add = [&](Issue::Severity sev, int trig, int item, bool action, std::string text) {
    out.push_back({sev, trig, item, action, std::move(text)});
  };
  const int nAreas = static_cast<int>(s.areas.size()), nTriggers = static_cast<int>(s.triggers.size());
  std::vector<int> areaUses(s.areas.size(), 0);
  std::set<std::string> namesGiven(ctx.placedNames.begin(), ctx.placedNames.end()), countdowns, counters;
  int objectivesAdded = 0;
  bool victory = false;
  std::vector<char> enabledByScript(s.triggers.size(), 0);
  for (const Trigger& t : s.triggers)
    for (const Action& a : t.actions) {
      if (a.type == ActionType::NameUnits || a.type == ActionType::SpawnNamed) namesGiven.insert(a.text);
      if (a.type == ActionType::Countdown) countdowns.insert(a.text);
      if (a.type == ActionType::SetCounter || a.type == ActionType::AddCounter) counters.insert(a.text);
      if (a.type == ActionType::AddObjective) ++objectivesAdded;
      if (a.type == ActionType::Victory) victory = true;
      if ((a.type == ActionType::EnableTrigger || a.type == ActionType::FireTrigger || a.type == ActionType::ResetTrigger) &&
          a.trigger >= 0 && a.trigger < nTriggers)
        enabledByScript[static_cast<size_t>(a.trigger)] = 1;
    }

  // One field of one condition or action.
  auto field = [&](const Entry& e, const Field& f, int32_t v, const std::string& txt, int ti, int item) {
    const std::string where = std::string(e.name) + ", " + f.label + ": ";
    switch (f.kind) {
      case Kind::Area:
      case Kind::AreaOrMap:
        if (v >= nAreas) add(Issue::Error, ti, item, e.action, where + "area " + std::to_string(v) + " does not exist");
        else if (v < 0 && f.kind == Kind::Area) add(Issue::Error, ti, item, e.action, where + "no area chosen");
        else if (v >= 0) ++areaUses[static_cast<size_t>(v)];
        break;
      case Kind::Trigger:
        if (v < 0 || v >= nTriggers) add(Issue::Error, ti, item, e.action, where + "no such trigger");
        else if (v == ti && e.action && e.type == static_cast<int>(ActionType::FireTrigger))
          add(Issue::Error, ti, item, true, where + "a trigger that fires itself never stops");
        break;
      case Kind::Player:
        if (v < 0 || v > 7) add(Issue::Error, ti, item, e.action, where + "a side from 0 to 7 is needed");
        break;
      case Kind::PlayerOrAny:
        if (v > 7 && v != ANY_PLAYER) add(Issue::Error, ti, item, e.action, where + "a side from 0 to 7, or everyone");
        break;
      case Kind::UnitType:
        if (v < 0 && !f.optional) add(Issue::Error, ti, item, e.action, where + "no unit type chosen");
        else if (ctx.types && v >= static_cast<int>(ctx.types->size())) add(Issue::Error, ti, item, e.action, where + "unknown unit type");
        break;
      case Kind::Objective:
        if (v < 0) add(Issue::Error, ti, item, e.action, where + "objective numbers start at 0");
        else if (v >= objectivesAdded)
          add(Issue::Warning, ti, item, e.action,
              where + "objective " + std::to_string(v) + ", but the script only adds " + std::to_string(objectivesAdded));
        break;
      case Kind::UnitName:
        if (txt.empty()) {
          if (!f.optional) add(Issue::Error, ti, item, e.action, where + "no name given");
        } else if (!(e.action && (e.type == static_cast<int>(ActionType::NameUnits) || e.type == static_cast<int>(ActionType::SpawnNamed))) &&
                   !namesGiven.count(txt)) {
          add(Issue::Warning, ti, item, e.action, where + "nothing is ever named '" + txt + "'");
        }
        break;
      case Kind::Countdown:
        if (txt.empty()) add(Issue::Error, ti, item, e.action, where + "no countdown named");
        else if (!countdowns.count(txt)) add(Issue::Warning, ti, item, e.action, where + "no countdown '" + txt + "' is ever started");
        break;
      case Kind::Counter:
        if (txt.empty()) add(Issue::Error, ti, item, e.action, where + "no counter named");
        else if (!e.action && !counters.count(txt)) add(Issue::Warning, ti, item, false, where + "counter '" + txt + "' is never set");
        break;
      case Kind::Flag:
      case Kind::Marker:
      case Kind::Text:
      case Kind::Interface:
      case Kind::Music:
        if (txt.empty() && !f.optional) add(Issue::Error, ti, item, e.action, where + "left empty");
        break;
      case Kind::AiBehaviour:
        if (aiBehaviourFromId(txt) < 0) add(Issue::Error, ti, item, e.action, where + "'" + txt + "' is not a behaviour of the computer");
        break;
      case Kind::Sound:
        if (txt.empty()) add(Issue::Error, ti, item, e.action, where + "no sound named");
        else if (!ctx.sounds.empty() && std::find(ctx.sounds.begin(), ctx.sounds.end(), txt) == ctx.sounds.end())
          add(Issue::Warning, ti, item, e.action, where + "there is no sound '" + txt + "'");
        break;
      case Kind::Weapon:
        if (!txt.empty() && !ctx.weapons.empty() && std::find(ctx.weapons.begin(), ctx.weapons.end(), txt) == ctx.weapons.end())
          add(Issue::Warning, ti, item, e.action, where + "there is no weapon '" + txt + "'");
        break;
      case Kind::Int:
      case Kind::Seconds:
      case Kind::Percent:
      case Kind::Bool:
      case Kind::Choice:
        if (v < f.min || v > f.max)
          add(Issue::Warning, ti, item, e.action,
              where + std::to_string(v) + " is outside " + std::to_string(f.min) + " to " + std::to_string(f.max));
        break;
      case Kind::Order:
        break;
      case Kind::Phase:
        if (v < 0 || v >= static_cast<int>(s.phases.size())) add(Issue::Error, ti, item, e.action, where + "the mission has no phase " + std::to_string(v));
        break;
      case Kind::Wave:
        if (v < 0 || v >= static_cast<int>(s.waves.size())) add(Issue::Error, ti, item, e.action, where + "the mission has no wave " + std::to_string(v));
        break;
      case Kind::Path:
        if (v < 0 || v >= static_cast<int>(s.paths.size())) add(Issue::Error, ti, item, e.action, where + "the mission has no path " + std::to_string(v));
        break;
      case Kind::Goal:
        if (v < 0 || v >= static_cast<int>(s.goals.size())) add(Issue::Error, ti, item, e.action, where + "the mission has no goal " + std::to_string(v));
        break;
    }
  };
  // Conditions and actions wherever they stand (triggers, phases, goals). `ti`: the trigger, or -1.
  auto checkCondition = [&](const Condition& c, int ti, int item) {
    if (c.type >= CondType::Count) return add(Issue::Error, ti, item, false, "unknown condition");
    const Entry& e = entry(c.type);
    for (const Field& f : e.fields) field(e, f, f.slot == Slot::Text ? 0 : get(c, f.slot), c.text, ti, item);
  };
  auto checkAction = [&](const Action& a, int ti, int item) {
    if (a.type >= ActionType::Count) return add(Issue::Error, ti, item, true, "unknown action");
    const Entry& e = entry(a.type);
    const bool byName = e.takesUnits && !actionUsesText(a.type) && !a.text.empty();
    for (const Field& f : e.fields) {
      if (byName && (f.slot == Slot::Player || f.slot == Slot::Area || f.slot == Slot::UnitType)) continue;
      field(e, f, f.slot == Slot::Text ? 0 : get(a, f.slot), a.text, ti, item);
    }
  };
  // --- The mission model -----------------------------------------------------
  const int nPhases = static_cast<int>(s.phases.size());
  std::vector<char> phaseEntered(s.phases.size(), 0);
  auto notePhase = [&](const std::vector<Action>& acts) {
    for (const Action& a : acts)
      if (a.type == ActionType::GotoPhase && a.value >= 0 && a.value < nPhases) phaseEntered[static_cast<size_t>(a.value)] = 1;
  };
  for (const Trigger& t : s.triggers) notePhase(t.actions);
  for (const MissionPhase& p : s.phases) notePhase(p.onEnter);
  for (const MissionGoal& g : s.goals) {
    notePhase(g.onDone);
    notePhase(g.onFail);
  }
  for (int pi = 0; pi < nPhases; ++pi) {
    const MissionPhase& p = s.phases[static_cast<size_t>(pi)];
    const std::string where = "phase '" + p.name + "': ";
    if (p.from < -2 || p.from >= nPhases) add(Issue::Error, -1, -1, false, where + "it follows a phase that doesn't exist");
    if (p.from == pi) add(Issue::Error, -1, -1, false, where + "it can't follow itself");
    if (pi > 0 && p.enter.empty() && !phaseEntered[static_cast<size_t>(pi)])
      add(Issue::Warning, -1, -1, false, where + "it has no conditions to begin on and no Go to phase action leads to it");
    const size_t before = out.size();
    for (const Condition& c : p.enter) checkCondition(c, -1, -1);
    for (const Action& a : p.onEnter) checkAction(a, -1, -1);
    for (size_t i = before; i < out.size(); ++i) out[i].text = where + out[i].text;
  }
  for (const Trigger& t : s.triggers)
    if (t.phase >= nPhases) add(Issue::Error, static_cast<int>(&t - s.triggers.data()), -1, false, "'" + t.name + "' belongs to a phase that doesn't exist");
  for (const MissionPath& p : s.paths) {
    const std::string where = "path '" + p.name + "': ";
    if (p.points.size() < 2) add(Issue::Warning, -1, -1, false, where + "a path needs at least two points");
    if (ctx.map) {
      Pathfinder pf(*ctx.map);
      pf.refresh();
      for (size_t i = 0; i < p.points.size(); ++i) {
        const Vec2i pt = p.points[i];
        if (!ctx.map->inBounds(pt.x, pt.y)) add(Issue::Error, -1, -1, false, where + "point " + std::to_string(i + 1) + " is off the map");
        else if (!ctx.map->passable(pt.x, pt.y, Locomotor::Foot)) add(Issue::Warning, -1, -1, false, where + "point " + std::to_string(i + 1) + " is on ground nobody can stand on");
        else if (i > 0 && ctx.map->inBounds(p.points[i - 1].x, p.points[i - 1].y) &&
                 pf.region(pt.x, pt.y, Locomotor::Foot) != pf.region(p.points[i - 1].x, p.points[i - 1].y, Locomotor::Foot))
          add(Issue::Warning, -1, -1, false, where + "there is no way on foot from point " + std::to_string(i) + " to point " + std::to_string(i + 1));
      }
    }
  }
  std::vector<char> waveSent(s.waves.size(), 0);
  auto noteWave = [&](const std::vector<Action>& acts) {
    for (const Action& a : acts)
      if (a.type == ActionType::SendWave && a.value >= 0 && a.value < static_cast<int>(s.waves.size())) waveSent[static_cast<size_t>(a.value)] = 1;
  };
  for (const Trigger& t : s.triggers) noteWave(t.actions);
  for (const MissionPhase& p : s.phases) noteWave(p.onEnter);
  for (const MissionGoal& g : s.goals) {
    noteWave(g.onDone);
    noteWave(g.onFail);
  }
  for (size_t wi = 0; wi < s.waves.size(); ++wi) {
    const MissionWave& w = s.waves[wi];
    const std::string where = "wave '" + w.name + "': ";
    if (w.area < 0 || w.area >= nAreas) add(Issue::Error, -1, -1, false, where + "no area to arrive in");
    else ++areaUses[static_cast<size_t>(w.area)];
    if (w.player > 7) add(Issue::Error, -1, -1, false, where + "a side from 0 to 7 is needed");
    if (w.units.empty()) add(Issue::Warning, -1, -1, false, where + "it has no units");
    for (const MissionWave::Entry& e : w.units) {
      if (e.unitType < 0 || (ctx.types && e.unitType >= static_cast<int>(ctx.types->size()))) add(Issue::Error, -1, -1, false, where + "a unit type is missing or unknown");
      if (e.count[0] > e.count[2]) add(Issue::Note, -1, -1, false, where + "more units on easy than on hard");
    }
    if ((w.order == MissionWave::AttackTo || w.order == MissionWave::MoveTo)) {
      if (w.target < 0 || w.target >= nAreas) add(Issue::Error, -1, -1, false, where + "no area to go for");
      else ++areaUses[static_cast<size_t>(w.target)];
    }
    if ((w.order == MissionWave::AlongPath || w.order == MissionWave::AttackAlongPath) && (w.path < 0 || w.path >= static_cast<int>(s.paths.size())))
      add(Issue::Error, -1, -1, false, where + "no path to follow");
    if (w.times > 1 && w.interval <= 0) add(Issue::Warning, -1, -1, false, where + "it comes " + std::to_string(w.times) + " times with no interval: a second apart");
    if (!waveSent[wi]) add(Issue::Warning, -1, -1, false, where + "nothing sends it");
    if (!w.unitName.empty()) namesGiven.insert(w.unitName);
  }
  for (size_t gi = 0; gi < s.goals.size(); ++gi) {
    const MissionGoal& g = s.goals[gi];
    const std::string where = "goal '" + g.text + "': ";
    const int type = goalTypeFromId(g.type);
    if (type < 0) {
      add(Issue::Error, -1, -1, false, where + "'" + g.type + "' is not a kind of goal");
      continue;
    }
    const GoalType& gt = goalTypes()[static_cast<size_t>(type)];
    if (gt.usesArea) {
      if (g.area < 0 || g.area >= nAreas) add(Issue::Error, -1, -1, false, where + "it needs an area");
      else ++areaUses[static_cast<size_t>(g.area)];
    }
    if (gt.usesUnit) {
      if (g.unit.empty()) add(Issue::Error, -1, -1, false, where + "it needs the name of the units it is about");
      else if (!namesGiven.count(g.unit)) add(Issue::Warning, -1, -1, false, where + "nothing is ever named '" + g.unit + "'");
    }
    if (g.phase >= nPhases) add(Issue::Error, -1, -1, false, where + "it belongs to a phase that doesn't exist");
    if (type == 6 && g.phase < 0) add(Issue::Note, -1, -1, false, where + "a protect goal outside any phase stays open to the end of the mission");
    const size_t before = out.size();
    for (const Action& a : g.onDone) checkAction(a, -1, -1);
    for (const Action& a : g.onFail) checkAction(a, -1, -1);
    for (size_t i = before; i < out.size(); ++i) out[i].text = where + out[i].text;
    objectivesAdded += 1;
  }
  if (!s.lua.empty()) {
    std::string err;
    if (!MissionLua::compiles(s.lua, err)) add(Issue::Error, -1, -1, false, "the Lua script does not compile: " + err);
  } else {
    for (int ti = 0; ti < nTriggers; ++ti) {
      for (const Condition& c : s.triggers[static_cast<size_t>(ti)].conditions)
        if (c.type == CondType::ScriptSays) add(Issue::Error, ti, -1, false, "Script says: the mission has no Lua script");
      for (const Action& a : s.triggers[static_cast<size_t>(ti)].actions)
        if (a.type == ActionType::CallScript) add(Issue::Error, ti, -1, true, "Call script: the mission has no Lua script");
    }
  }
  for (const MissionSequence& q : s.sequences)
    for (const MissionSequence::Event& e : q.events) {
      bool known = false;
      for (const SequenceTrack& t : sequenceTracks()) known = known || e.track == t.id;
      if (!known) add(Issue::Warning, -1, -1, false, "sequence '" + q.name + "': there is no track '" + e.track + "'");
    }

  for (int ti = 0; ti < nTriggers; ++ti) {
    const Trigger& t = s.triggers[static_cast<size_t>(ti)];
    if (t.actions.empty()) add(Issue::Warning, ti, -1, false, "'" + t.name + "' has no actions");
    if (t.conditions.empty()) add(Issue::Warning, ti, -1, false, "'" + t.name + "' has no conditions and never fires (add Always)");
    if (!t.enabled && !enabledByScript[static_cast<size_t>(ti)])
      add(Issue::Warning, ti, -1, false, "'" + t.name + "' is switched off and nothing switches it on");
    for (size_t ci = 0; ci < t.conditions.size(); ++ci) {
      const Condition& c = t.conditions[ci];
      if (c.type >= CondType::Count) {
        add(Issue::Error, ti, static_cast<int>(ci), false, "unknown condition");
        continue;
      }
      const Entry& e = entry(c.type);
      for (const Field& f : e.fields) field(e, f, f.slot == Slot::Text ? 0 : get(c, f.slot), c.text, ti, static_cast<int>(ci));
      if (c.type == CondType::Chance && !t.repeat)
        add(Issue::Note, ti, static_cast<int>(ci), false, "Chance on a trigger that doesn't repeat comes up within a second or two");
      if (c.type == CondType::Always && t.repeat && t.conditions.size() == 1)
        add(Issue::Warning, ti, static_cast<int>(ci), false, "Always on a repeating trigger fires every second for ever");
    }
    bool stopsClock = false;
    for (size_t ai = 0; ai < t.actions.size(); ++ai) {
      const Action& a = t.actions[ai];
      if (a.type >= ActionType::Count) {
        add(Issue::Error, ti, static_cast<int>(ai), true, "unknown action");
        continue;
      }
      const Entry& e = entry(a.type);
      const bool byName = e.takesUnits && !actionUsesText(a.type) && !a.text.empty();
      for (const Field& f : e.fields) {
        // Units picked by name: side, area and type are not looked at.
        if (byName && e.takesUnits && (f.slot == Slot::Player || f.slot == Slot::Area || f.slot == Slot::UnitType)) continue;
        field(e, f, f.slot == Slot::Text ? 0 : get(a, f.slot), a.text, ti, static_cast<int>(ai));
      }
      stopsClock = stopsClock || a.type == ActionType::StopCountdown || a.type == ActionType::Countdown;
      // Can they get there at all?
      if (ctx.map && (a.type == ActionType::AttackMove || a.type == ActionType::MoveUnits || a.type == ActionType::Patrol) && !byName &&
          a.area >= 0 && a.area < nAreas && a.area2 >= 0 && a.area2 < nAreas) {
        Pathfinder pf(*ctx.map);
        pf.refresh();
        const ScriptArea& from = s.areas[static_cast<size_t>(a.area)];
        const ScriptArea& to = s.areas[static_cast<size_t>(a.area2)];
        for (int li = 0; li < 3; ++li) {
          const Locomotor loco = static_cast<Locomotor>(li);
          std::set<uint32_t> start, goal;
          for (int y = from.y0; y <= from.y1; ++y)
            for (int x = from.x0; x <= from.x1; ++x)
              if (uint32_t r = pf.region(x, y, loco)) start.insert(r);
          for (int y = to.y0; y <= to.y1; ++y)
            for (int x = to.x0; x <= to.x1; ++x)
              if (uint32_t r = pf.region(x, y, loco)) goal.insert(r);
          bool way = false;
          for (uint32_t r : start) way = way || goal.count(r);
          static const char* kWho[3] = {"men on foot", "wheeled vehicles", "tracked vehicles"};
          if (!start.empty() && !way)
            add(Issue::Warning, ti, static_cast<int>(ai), true,
                std::string(e.name) + ": " + kWho[li] + " can't get from '" + from.name + "' to '" + to.name + "' (cliffs or water in the way)");
        }
      }
    }
    // The clock-hiding trap: this trigger stops a clock that a later trigger waits on.
    if (stopsClock)
      for (int tj = ti + 1; tj < nTriggers; ++tj)
        for (const Condition& c : s.triggers[static_cast<size_t>(tj)].conditions)
          if (c.type == CondType::CountdownDone) {
            bool same = false;
            for (const Action& a : t.actions)
              same = same || ((a.type == ActionType::StopCountdown || a.type == ActionType::Countdown) && a.text == c.text);
            if (same)
                add(Issue::Note, tj, -1, false,
                    "'" + s.triggers[static_cast<size_t>(tj)].name + "' waits for the countdown '" + c.text + "' that '" + t.name +
                        "' (earlier in the list) stops or restarts: if both are due in the same check, this one never sees it run out");
          }
  }
  for (size_t i = 0; i < s.areas.size(); ++i) {
    const ScriptArea& a = s.areas[i];
    if (!areaUses[i]) add(Issue::Note, -1, -1, false, "area '" + a.name + "' is not used");
    if (a.x1 < a.x0 || a.y1 < a.y0) add(Issue::Error, -1, -1, false, "area '" + a.name + "' is empty");
    if (ctx.map && (a.x0 < 0 || a.y0 < 0 || a.x1 >= ctx.map->width() || a.y1 >= ctx.map->height()))
      add(Issue::Warning, -1, -1, false, "area '" + a.name + "' reaches beyond the map");
  }
  if (nTriggers && !victory) add(Issue::Note, -1, -1, false, "no Victory action: the mission is won when every enemy is destroyed");
  return out;
}

const std::vector<GoalType>& goalTypes() {
  static const std::vector<GoalType> v = {
      {"destroy", "Destroy", "Done when every unit of the name is dead.", false, false, true, ""},
      {"reach", "Reach", "Done when the side has at least so many units in the area.", true, true, false, "Units (at least 1)"},
      {"clear", "Clear", "Done when the side has units in the area and no enemy of it has any.", true, false, false, ""},
      {"hold", "Hold", "Done when the side has controlled the area (as for Clear) for so many seconds without a break.", true, true, false, "Seconds"},
      {"survive", "Survive", "Done so many seconds after the goal began.", false, true, false, "Seconds"},
      {"escort", "Escort", "Done when at least so many units of the name are in the area; failed if all of them are dead.", true, true, true, "Units (at least 1)"},
      {"protect", "Protect", "Failed when no unit of the name is left alive; done when its phase ends.", false, false, true, ""},
  };
  return v;
}

const std::vector<SequenceTrack>& sequenceTracks() {
  static const std::vector<SequenceTrack> v = {
      {"camera", "Where the view is and how it moves.", "cut|pan|dolly|orbit|crane|zoom|follow|shake"},
      {"actor", "What a unit, a group or a set piece does.", "spawn|despawn|move|turn|stop|prone|mount|dismount|fire|damage|die|explode|surrender|enter|leave"},
      {"effects", "Things that happen on the map.", "explosion|barrage|smoke|fire|collapse|demolish|flyover|flare|flash"},
      {"dialogue", "Words and sound.", "radio|subtitle|voice|sound|music|ambience"},
      {"screen", "What lies over the picture.", "fade|title|caption|overlay|objective|letterbox"},
      {"flow", "The order of things.", "wait|wait_for|branch|action|end"},
  };
  return v;
}

// ---------------------------------------------------------------------------
// The reference document
// ---------------------------------------------------------------------------
std::string referenceMarkdown() {
  std::string md =
      "# Script reference\n\n"
      "Every condition and action a Legion Classic mission can use. This file is written by the game from its own "
      "script registry (`engine/src/sim/script_registry.cpp`): don't edit it by hand, run the tests with "
      "`LEGION_WRITE_DOCS=1` instead.\n\n"
      "A trigger fires when all its conditions hold (or any one, if set so) and then runs its actions in order, once, "
      "or again and again if it repeats. Any condition can be turned round with *not*. The names in `code` are the "
      "ids used in map files and in Lua.\n";
  auto kindName = [](const Field& f) -> std::string {
    switch (f.kind) {
      case Kind::Player: return "side 0-7";
      case Kind::PlayerOrAny: return "side 0-7 or everyone";
      case Kind::Area: return "area";
      case Kind::AreaOrMap: return "area or none";
      case Kind::Int: return "number";
      case Kind::Seconds: return "seconds";
      case Kind::Percent: return "percent";
      case Kind::Bool: return "on/off";
      case Kind::Choice: {
        std::string s = f.choices;
        std::replace(s.begin(), s.end(), '|', '/');
        return s;
      }
      case Kind::UnitType: return f.optional ? "unit type or any" : "unit type";
      case Kind::Trigger: return "trigger";
      case Kind::Order: return "order or any";
      case Kind::Objective: return "objective number";
      case Kind::Phase: return "phase number";
      case Kind::Wave: return "wave number";
      case Kind::Path: return "path number";
      case Kind::Goal: return "goal number";
      default: return f.optional ? "text (may be empty)" : "text";
    }
  };
  auto section = [&](const char* title, const std::vector<Entry>& list) {
    md += std::string("\n## ") + title + "\n";
    std::vector<std::string> groups;
    for (const Entry& e : list)
      if (std::find(groups.begin(), groups.end(), e.group) == groups.end()) groups.push_back(e.group);
    for (const std::string& g : groups) {
      md += "\n### " + g + "\n";
      for (const Entry& e : list) {
        if (g != e.group) continue;
        md += std::string("\n#### ") + e.name + " (`" + e.id + "`)\n\n" + e.summary + "\n\n";
        if (!e.fields.empty()) {
          md += "| Field | In files | Kind | What |\n| --- | --- | --- | --- |\n";
          for (const Field& f : e.fields)
            md += std::string("| ") + f.label + " | `" + f.key + "` | " + kindName(f) + " | " + f.help + " |\n";
          md += "\n";
        }
        if (*e.example) md += std::string("*Example:* ") + e.example + "\n\n";
        if (*e.pitfalls) md += std::string("*Watch out:* ") + e.pitfalls + "\n";
      }
    }
  };
  section("Conditions", conditions());
  section("Actions", actions());
  md += "\n## The mission model\n\n"
        "Beside triggers a mission can hold phases, paths, waves, goals and sequences as data of their own, in the "
        "script's `phases`, `paths`, `waves`, `goals` and `sequences` lists. All are numbered from 0 in the order they "
        "are listed.\n\n"
        "### Phases\n\n"
        "A mission with phases is always in exactly one of them, starting in the first. A phase begins by itself when "
        "the phase it follows (`from`: -1 the one before it, -2 any, or a number) is current and its `enter` conditions "
        "hold (all, or `any` one); a phase without conditions begins only by *Go to phase*. Two phases that follow the "
        "same one with different conditions make a branch: the first whose conditions hold wins. As a phase begins its "
        "`on_enter` actions run and its `music` (a track or mood) starts. A trigger with a `phase` is checked only "
        "while that phase is current. In a phase's own conditions *Timer* counts from the moment the current phase began.\n\n"
        "### Paths\n\n"
        "A named list of `points` (tiles), with `loop` to go round for ever. *Follow path* and waves use them.\n\n"
        "### Waves\n\n"
        "Units that arrive together: `units` (a type and three counts: easy, normal, hard), the `player` they belong "
        "to, the `area` they appear in, `delay` seconds after being sent, `times` they come and the `interval` between, "
        "and what they do: `order` `stay`, `attack_to` or `move_to` the `target` area, `along_path` or "
        "`attack_along_path` the `path`. `unit_name` names them (else `wave:<name>`), `flags` makes them set pieces "
        "(1 invulnerable, 2 kept from the computer, 4 untargetable, 8 locked), `stance` and `bind` as for the actions.\n\n"
        "### Goals\n\n"
        "A goal shows as an objective and keeps its own score. It begins with its `phase` (-1: with the mission) and "
        "runs its `on_done` or `on_fail` actions when it ends.\n\n"
        "| Type | Done / failed | Area | Value | Unit name |\n| --- | --- | --- | --- | --- |\n";
  for (const GoalType& g : goalTypes())
    md += std::string("| `") + g.id + "` | " + g.summary + " | " + (g.usesArea ? "yes" : "") + " | " + (g.usesValue ? g.valueLabel : "") + " | " +
          (g.usesUnit ? "yes" : "") + " |\n";
  md += "\n### Sequences\n\n"
        "A sequence is a timeline of events on tracks, for cutscenes and scripted scenes. Missions and packs can carry "
        "them now; they are played by the sequencer of Milestone 9.15. Each event has `at` (tenths of a second), a "
        "`track`, a `kind` and `params`.\n\n| Track | What | Kinds of event |\n| --- | --- | --- |\n";
  for (const SequenceTrack& t : sequenceTracks()) {
    std::string kinds = t.kinds;
    for (size_t i = 0; (i = kinds.find('|', i)) != std::string::npos; i += 2) kinds.replace(i, 1, ", ");
    md += std::string("| `") + t.id + "` | " + t.summary + " | " + kinds + " |\n";
  }
  md += "\n## Lua\n\n"
        "A mission can carry a Lua 5.4 script (the script's `lua` text). It runs inside the simulation, so it is "
        "sandboxed to be exactly repeatable: no files, clock or operating system; `pairs` walks keys in a fixed order "
        "(numbers, then texts); random numbers come from the game's own dice; a call may run at most 5 million steps "
        "and the script use at most 32 MB.\n\n"
        "**Rules.** Define functions at the top level and act on the world only from them. Keep whatever must survive "
        "a save in the global table `state` (numbers, texts, true/false and tables): nothing else is saved, and on "
        "loading the script is run again and `state` put back.\n\n"
        "**Actions and conditions.** Every action above is a function taking a table of its fields by the names in "
        "the *In files* column, every condition a function of the table `is` that returns true or false:\n\n"
        "```lua\n"
        "function on_start()\n"
        "  spawn_named{player = 1, area = \"North road\", unit = \"rus_rifleman\", value = 8, text = \"Raiders\"}\n"
        "  attack_move{text = \"Raiders\", area2 = \"Bridge\"}\n"
        "  after(90, \"second_wave\", 12)\n"
        "end\n"
        "function second_wave(n)\n"
        "  if is.units_in_area{player = 0, area = \"Bridge\", value = 1} then\n"
        "    spawn_units{player = 1, area = \"North road\", unit = \"rus_rifleman\", value = n}\n"
        "  end\n"
        "end\n"
        "```\n\n"
        "Areas, triggers, phases, waves and paths may be given by name or number, goals by their text, unit types by "
        "id, sides as a number or `\"all\"`, choices by their word. A misspelt field or name is an error that says "
        "what the choices were. Add `['not'] = true` to a condition's table to turn it round.\n\n"
        "**Handlers** the game calls if the script defines them: `on_start()`, `on_second()`, "
        "`on_unit_died(type, owner, x, y)`, `on_phase(number, name)`, `on_countdown(name)`, "
        "`on_wave(name, units, owner)`, `on_defeated(side)`, `on_objectives()`. Triggers, phases and goals call any "
        "function with the *Call script* action and ask one with the *Script says* condition.\n\n"
        "**Functions.** `tick()`, `seconds()`, `phase()` (number, name), `difficulty()` (number, name), "
        "`count(side[, area[, unit]])`, `named(name)` (how many alive), `supplies(side)`, `flag(name)`, "
        "`counter(name)`, `objective(number)`, `goal(number or text)`, `random(n)` / `random(a, b)`, "
        "`after(seconds, \"function\"[, number or text])`, `log(...)`; from Lua itself `pairs`, `ipairs`, `select`, "
        "`type`, `tostring`, `tonumber`, `pcall`, `error`, `assert`, `setmetatable` (no `__gc`, no `__mode`), the "
        "`string` and `table` libraries, and `math.floor`, `ceil`, `abs`, `sqrt`, `min`, `max`, `tointeger`, `random`, "
        "`huge`, `maxinteger`, `mininteger`. `^` is exact for whole powers only. Prefer whole numbers.\n\n"
        "Errors don't stop the mission: they are logged and listed by `lua.errors` in the console; `lua.run <code>` "
        "tries a line.\n";
  return md;
}

}  // namespace pioneer::script

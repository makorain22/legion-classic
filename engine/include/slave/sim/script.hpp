#pragma once
// Slave Engine - scenario scripting: areas, triggers and objectives.
//
// A scenario can name rectangular areas of the map and hold triggers. Each
// trigger has conditions (all must hold) and actions (run in order when they
// do). Triggers fire once unless set to repeat. Everything runs inside the
// simulation, so scripted missions replay and save exactly like the rest of
// the game. Stored in the map file as JSON, edited in the editor's Script tab.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace slave {

constexpr uint8_t ANY_PLAYER = 255;

struct ScriptArea {
  std::string name;
  int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // tiles, inclusive
  bool contains(int tx, int ty) const { return tx >= x0 && tx <= x1 && ty >= y0 && ty <= y1; }
};

enum class CondType : uint8_t {
  Always = 0,      // true from the start
  Timer,           // `value` seconds have passed since the start (or since this trigger last fired, if repeating)
  UnitsInArea,     // `player` has at least `value` units (of `unitType`, or any) in `area`
  NoUnitsInArea,   // `player` has no units (of `unitType`, or any) in `area`
  UnitCount,       // `player` has at most `value` units (of `unitType`, or any) on the whole map
  PlayerDefeated,  // `player` has been defeated
  Supplies,        // `player` has at least `value` supplies
  TriggerFired,    // trigger number `trigger` has fired, at least `value` seconds ago
  UnitsAtLeast,    // `player` has at least `value` units (of `unitType`, or any) on the whole map
  InCover,         // `player` has at least `value` men inside buildings, bunkers or trenches
  OrderGiven,      // `player` (a human) has given at least `value` orders of kind `order` (move, attack...)
  FlagAtLeast,     // campaign flag `text` is at least `value`
  // --- Mission engine (9.9) ---------------------------------------------------------
  NamedAlive,      // a unit named `text` is alive
  NamedDead,       // no unit named `text` is alive
  NamedInArea,     // at least `value` (min 1) units named `text` are in `area`
  NamedHealthBelow,  // a unit named `text` has less than `value` percent of its health
  StructuresAtMost,  // `player` has at most `value` structures (of `unitType`, or any but field works) in `area` (-1: map)
  StructuresAtLeast, // ... at least `value` (min 1)
  UnitsInAreaAtMost, // `player` has at most `value` units (of `unitType`, or any) in `area`
  AreaControlled,  // `player` and its allies have at least `value` (min 1) units in `area`, and no enemy has any
  CountdownDone,   // the countdown `text` has run out
  CountdownBelow,  // the countdown `text` is running with at most `value` seconds left
  CounterAtLeast,  // mission counter `text` is at least `value`
  CounterAtMost,   // mission counter `text` is at most `value`
  KillsAtLeast,    // `player` has destroyed at least `value` enemy units and structures
  LossesAtLeast,   // `player` has lost at least `value` units and structures
  ObjectiveDone,   // objective number `value` is done
  ObjectiveFailed, // objective number `value` has failed
  DifficultyAtLeast,  // the mission is played at difficulty `value` or harder (0 easy, 1 normal, 2 hard)
  Chance,          // true `value` percent of the times it is checked (deterministic, replays the same)
  SuppliesAtMost,  // `player` has at most `value` supplies
  HealthBelow,     // `player`'s units (of `unitType`, or any) in `area` have less than `value` percent of their health in total
  AlliedWith,      // `player` is allied with player `value`
  Count
};

enum class ActionType : uint8_t {
  Message = 0,      // show `text` to `player` (or everyone)
  AddObjective,     // add objective `text` for `player`; `value` 1 = optional
  CompleteObjective,  // objective number `value` is done
  FailObjective,    // objective number `value` has failed
  Victory,          // `player` (and its allies) win
  Defeat,           // `player` loses
  SpawnUnits,       // `value` units of `unitType` for `player` in `area`
  GiveSupplies,     // `player` gains `value` supplies (negative: loses)
  RevealArea,       // `player` can see `area` for good
  AttackMove,       // `player`'s units in `area` attack-move to `area2`
  MoveUnits,        // `player`'s units in `area` move to `area2`
  ChangeOwner,      // `player`'s units in `area` go over to `player2`
  KillUnits,        // destroy `player`'s units in `area`
  EnableTrigger,    // switch trigger `trigger` on
  DisableTrigger,   // switch trigger `trigger` off
  SetComputer,      // `player` is played by the computer (`value` = difficulty 0..2), or a human (-1)
  CameraTo,         // move `player`'s view to `area`
  Hint,             // show `text` in `player`'s hint panel until the next hint (empty: close it)
  Highlight,        // make the interface element `text` glow for `player` ("tab.train", "build:gbr_power", "order.evacuate"...; empty: none)
  LockBuild,        // `player` can't build `unitType` (any = everything)
  UnlockBuild,      // `player` can build `unitType` again (any = everything)
  SetFlag,          // campaign flag `text` = `value` (kept by the campaign when the mission is won)
  // --- Mission engine (9.9) ---------------------------------------------------------
  // Actions on units pick them by name (`text`, when the action has no other use
  // for it - see the editor) or by `player`, `area` and `unitType`.
  Countdown,        // start (or restart) the countdown `text` at `value` seconds, shown to `player`
  StopCountdown,    // remove the countdown `text`
  PauseCountdown,   // pause the countdown `text` (`value` 1), or let it run on (0)
  SetPlayArea,      // the battle is confined to `area` (-1: the whole map) for `player` (or everyone)
  NameUnits,        // `player`'s units in `area` (of `unitType`) are called `text`; `value` = at most that many (0: all)
  SetInvulnerable,  // the units take no damage (`value` 1) or can be hurt again (0) - set pieces
  SetAiControl,     // the computer may command the units (`value` 1) or leaves them to the script (0)
  SetTargetable,    // enemies pick the units as targets (`value` 1) or ignore them (0)
  SetLocked,        // the owner's player can't give the units orders (`value` 1), or can again (0)
  SetHealth,        // the units' health becomes `value` percent of their maximum
  DamageUnits,      // the units lose `value` percent of their maximum health (they can die of it)
  SetStance,        // the units take stance `value` (0 aggressive, 1 hold ground, 2 hold fire)
  Patrol,           // the units walk between `area` and `area2` until given other orders
  Garrison,         // the infantry go into buildings, bunkers or trenches in `area2` (their own side's or empty)
  Evacuate,         // men inside buildings in `area` come out
  BindUnits,        // the units are bound into a fighting unit
  Barrage,          // `value` shells of weapon `text` (default: a field gun) fall on `area`, fired by `player`
  SetAlliance,      // `player` becomes an ally of player `player2` (`value` 1) or its enemy (0)
  AiAttack,         // the computer playing `player` sends its main force against `area`
  AiDefend,         // the computer playing `player` keeps its main force in `area` and defends it
  AiFree,           // the computer playing `player` chooses its own targets again
  AiBehaviour,      // the computer playing `player` may (`value` 1) or may not (0) do `text`:
                    // build, train, attack, raid, capture, fortify, scout, repair
  AiWaveSize,       // the computer playing `player` attacks with `value` percent of its usual force (0: usual)
  SetCounter,       // mission counter `text` = `value`
  AddCounter,       // mission counter `text` += `value`
  AddFlag,          // campaign flag `text` += `value`
  SpawnFromFlag,    // as many `unitType` as campaign flag `text` says (at most `value`, 0: 50) for `player` in `area`
  Transmission,     // a radio message to `player`: `text` = "Speaker: words"; `value` = seconds shown (0: by length)
  Cinematic,        // letterbox and no orders for `player` (`value` 1), or back to play (0)
  CameraPan,        // `player`'s view glides to `area` over `value` seconds
  MarkArea,         // a marker named `text` on `area` (and the minimap) for `player`; `value` 0 goal, 1 danger, 2 note
  ClearMarker,      // remove the marker `text` (empty: all markers)
  PlaySound,        // play sound `text` (at `area`, or everywhere) for `player`
  PlayMusic,        // play track `text` (or "calm", "battle", "victory", "defeat"; empty: back to automatic)
  SetIncomeRate,    // `player`'s income runs at `value` percent
  AddLogisticsPoint,  // a new logistics point appears in the middle of `area`
  FireTrigger,      // run trigger `trigger`'s actions now (it counts as fired)
  ResetTrigger,     // trigger `trigger` can fire again, as if it never had (and is switched on)
  HideObjective,    // objective number `value` is taken off the list
  SpawnNamed,       // as Spawn units, and the new units are called `text`
  // --- Terrain (9.11) ------------------------------------------------------------------
  FreezeWater,      // the water in `area` freezes: men and wheels can cross it, tracks can't
  BreakIce,         // the ice in `area` breaks up; whatever stands on it goes into the water
  FloodArea,        // the land in `area` floods (9.12): men can wade it, vehicles can't cross it
  DrainArea,        // the flooded land in `area` is pumped dry again
  Count
};

// What the script may say about a single unit (CScripted::flags).
enum ScriptFlag : uint8_t {
  kScriptInvulnerable = 1,  // takes no damage (set pieces)
  kScriptNoAi = 2,          // the computer player never commands it
  kScriptUntargetable = 4,  // enemies never pick it as a target on their own
  kScriptLocked = 8,        // its owner's player can't give it orders
};

struct Condition {
  CondType type = CondType::Always;
  uint8_t player = 0;
  int32_t area = -1;
  int32_t value = 0;
  int32_t unitType = -1;  // -1 = any
  int32_t trigger = -1;
  int32_t order = -1;     // OrderGiven: a CommandType, -1 = any order
  std::string text;       // FlagAtLeast: the flag; named units, countdowns, counters
  bool negate = false;    // the condition holds when the test is false ("not")
};

// Orders a player can be waited for, by name (OrderGiven): "move", "attack_move", "attack", "produce"...
const char* orderId(int commandType);
int orderFromId(const std::string& id);

struct Action {
  ActionType type = ActionType::Message;
  uint8_t player = ANY_PLAYER;
  uint8_t player2 = 0;
  int32_t area = -1, area2 = -1;
  int32_t value = 0;
  int32_t unitType = -1;
  int32_t trigger = -1;
  std::string text;
  bool scaled = false;  // Spawn units / Spawn named / Spawn from flag: more on hard, fewer on easy
};

struct Trigger {
  std::string name = "New trigger";
  bool enabled = true;
  bool repeat = false;
  bool any = false;     // fires when any one condition holds (else all of them must)
  std::vector<Condition> conditions;
  std::vector<Action> actions;
};

struct ScenarioScript {
  std::vector<ScriptArea> areas;
  std::vector<Trigger> triggers;
  bool empty() const { return areas.empty() && triggers.empty(); }
};

// Unit types are stored by id in files, so scenarios survive rule changes.
class UnitTypeRegistry;
nlohmann::json scriptToJson(const ScenarioScript& s, const UnitTypeRegistry* types);
ScenarioScript scriptFromJson(const nlohmann::json& j, const UnitTypeRegistry* types);

const char* condName(CondType t);
const char* actionName(ActionType t);
const char* condId(CondType t);
const char* actionId(ActionType t);
// The AI behaviours AiBehaviour can switch (bit i = kAiBehaviourIds[i]).
extern const char* const kAiBehaviourIds[8];
int aiBehaviourFromId(const std::string& id);
// Does this action pick units (by name or by player, area and type)?
bool actionTakesUnits(ActionType t);
// Does this action use `text` for something other than a unit name?
bool actionUsesText(ActionType t);

// A mission goal shown to a player.
struct Objective {
  std::string text;
  uint8_t player = ANY_PLAYER;
  uint8_t state = 0;  // 0 open, 1 done, 2 failed, 3 hidden
  bool optional = false;
  int32_t area = -1;  // shown as a marker on the map while open (-1: none)
};

// A clock on screen (Countdown action).
struct MissionCountdown {
  std::string label;
  uint8_t player = 255;  // ANY_PLAYER: everyone
  uint32_t endTick = 0;  // when it runs out
  uint32_t left = 0;     // ticks left while paused
  bool paused = false;
  bool done = false;     // ran out (stays on the list, at 0:00, until stopped)
};

// A marker on the map (MarkArea action).
struct MissionMarker {
  std::string label;
  uint8_t player = 255;
  int32_t area = -1;
  uint8_t kind = 0;  // 0 goal, 1 danger, 2 note
};

}  // namespace slave

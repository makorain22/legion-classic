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
  TriggerFired,    // trigger number `trigger` has fired
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
  Count
};

struct Condition {
  CondType type = CondType::Always;
  uint8_t player = 0;
  int32_t area = -1;
  int32_t value = 0;
  int32_t unitType = -1;  // -1 = any
  int32_t trigger = -1;
};

struct Action {
  ActionType type = ActionType::Message;
  uint8_t player = ANY_PLAYER;
  uint8_t player2 = 0;
  int32_t area = -1, area2 = -1;
  int32_t value = 0;
  int32_t unitType = -1;
  int32_t trigger = -1;
  std::string text;
};

struct Trigger {
  std::string name = "New trigger";
  bool enabled = true;
  bool repeat = false;
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

// A mission goal shown to a player.
struct Objective {
  std::string text;
  uint8_t player = ANY_PLAYER;
  uint8_t state = 0;  // 0 open, 1 done, 2 failed
  bool optional = false;
};

}  // namespace slave

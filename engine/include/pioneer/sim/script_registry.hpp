#pragma once
// Pioneer Engine - the script registry.
//
// Every condition and action a mission can use is described here once: its
// id, name and group, the fields it reads (which slot of Condition / Action,
// what kind of value, its range and default) and what it does, with an
// example and the usual mistakes. The editor's panels and help, the script
// reference (docs/SCRIPT_REFERENCE.md is written from it), validation, and
// the Lua bridge (an action called by id with a table of fields) all read
// this one table. A test fails if an entry is missing or has no help.

#include <cstdint>
#include <string>
#include <vector>

#include "pioneer/sim/script.hpp"

namespace pioneer {
class Map;
class UnitTypeRegistry;
}  // namespace pioneer

namespace pioneer::script {

// Which member of Condition / Action a field lives in.
enum class Slot : uint8_t { Player, Player2, Area, Area2, Value, UnitType, Trigger, Order, Text, Scaled };

// What a field means, for the editor's widget and for checking.
enum class Kind : uint8_t {
  Player,       // a side, 0..7
  PlayerOrAny,  // a side, or everyone (ANY_PLAYER)
  Area,         // an area of the script (required)
  AreaOrMap,    // an area, or -1 for the whole map / none
  Int,
  Seconds,
  Percent,
  Bool,         // 0 or 1
  Choice,       // one of `choices` ("easy|normal|hard"), by position
  UnitType,     // a unit or building type; -1 = any
  Trigger,      // another trigger of the script
  Order,        // a kind of order (orderId)
  Text,         // words shown to the player
  UnitName,     // the name of named units
  Countdown,    // the name of a countdown
  Counter,      // the name of a mission counter
  Flag,         // the name of a campaign flag
  Marker,       // the name of a map marker
  Objective,    // an objective's number (the first one added is 0)
  Sound,        // a sound of audio.json
  Music,        // a track, or a mood
  Weapon,       // a weapon id
  Interface,    // an interface element ("tab.train", "build:<id>"...)
  AiBehaviour,  // build, train, attack, raid, capture, fortify, scout, repair
  Phase,        // a phase of the mission model, by number
  Wave,         // a wave of the mission model, by number
  Path,         // a path of the mission model, by number
  Goal,         // a goal of the mission model, by number
};

struct Field {
  Slot slot = Slot::Value;
  Kind kind = Kind::Int;
  const char* key = "";      // the name in files and in Lua ("player", "area", "value"...)
  const char* label = "";    // for people
  const char* help = "";     // one line
  int32_t def = 0, min = INT32_MIN, max = INT32_MAX;
  const char* choices = "";  // Kind::Choice: "a|b|c"
  bool optional = false;     // may be left empty / unset
};

struct Entry {
  bool action = false;
  int type = 0;               // CondType or ActionType
  const char* id = "";        // as in files
  const char* name = "";      // as in the editor
  const char* group = "";     // the editor's menu it sits under
  const char* summary = "";   // one sentence: what it does or tests
  const char* example = "";   // a use of it, in words
  const char* pitfalls = "";  // the usual mistakes (may be empty)
  bool takesUnits = false;    // picks units by name (text) or by side, area and type
  std::vector<Field> fields;
  const Field* field(Slot s) const;
  const Field* fieldByKey(const std::string& key) const;
};

const std::vector<Entry>& conditions();  // indexed by CondType
const std::vector<Entry>& actions();     // indexed by ActionType
const Entry& entry(CondType t);
const Entry& entry(ActionType t);
// By id ("units_in_area"); null if there is none.
const Entry* findCondition(const std::string& id);
const Entry* findAction(const std::string& id);

// Reading and writing a field through its slot (numbers; Text goes through text()).
int32_t get(const Condition& c, Slot s);
int32_t get(const Action& a, Slot s);
void set(Condition& c, Slot s, int32_t v);
void set(Action& a, Slot s, int32_t v);
// A condition or action with every field at its default.
Condition makeCondition(CondType t);
Action makeAction(ActionType t);

// --- Checking a script --------------------------------------------------------
struct Issue {
  enum Severity : uint8_t { Note, Warning, Error };
  Severity severity = Warning;
  int trigger = -1;     // which trigger (-1: the script as a whole)
  int item = -1;        // which condition or action of it (-1: the trigger itself)
  bool action = false;  // `item` is an action
  std::string text;
};
struct CheckContext {
  const UnitTypeRegistry* types = nullptr;
  const Map* map = nullptr;                    // with a map: areas are checked against it, and for ways between them
  std::vector<std::string> placedNames;        // names units placed on the map already carry
  std::vector<std::string> sounds, weapons;    // known ids (empty: not checked)
};
std::vector<Issue> check(const ScenarioScript& s, const CheckContext& ctx);

// The kinds of goal the mission model keeps score of (MissionGoal::type).
struct GoalType {
  const char* id;
  const char* name;
  const char* summary;
  bool usesArea, usesValue, usesUnit;
  const char* valueLabel;
};
const std::vector<GoalType>& goalTypes();
// The tracks a sequence can hold and the kinds of event on each (data for the
// sequencer of 9.15; listed so tools and packs agree on the names).
struct SequenceTrack {
  const char* id;
  const char* summary;
  const char* kinds;  // "cut|pan|orbit|..."
};
const std::vector<SequenceTrack>& sequenceTracks();

// The whole registry as a Markdown document (docs/SCRIPT_REFERENCE.md).
std::string referenceMarkdown();

}  // namespace pioneer::script

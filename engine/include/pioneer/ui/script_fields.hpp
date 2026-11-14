#pragma once
// Pioneer Engine - property grids for script lines.
//
// Draws the fields of a condition or action from the script registry: a
// widget for each field by its kind (a list of the mission's areas, of unit
// types, a checkbox, a number in its range...), labelled and with the
// field's help on hover. Editors don't write a panel per script line any
// more: a line added to the registry can be edited at once.

#include "pioneer/sim/script.hpp"
#include "pioneer/sim/script_registry.hpp"

namespace pioneer {
class UnitTypeRegistry;
}

namespace pioneer::ui {

struct ScriptFieldContext {
  const ScenarioScript* script = nullptr;   // for the lists of areas, triggers, phases, waves, paths, goals
  const UnitTypeRegistry* types = nullptr;  // for the list of unit types
  float labelWidth = 110.0f;                // at 100% interface size
};

// The fields of one condition / action. True when something changed.
bool editFields(const script::Entry& e, Condition& c, const ScriptFieldContext& ctx);
bool editFields(const script::Entry& e, Action& a, const ScriptFieldContext& ctx);

}  // namespace pioneer::ui

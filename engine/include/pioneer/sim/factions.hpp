#pragma once
// Pioneer Engine - the playable sides. Their units and buildings come from the
// rules (each entry's "faction"); this is the list the menus, the skirmish
// setup and the editor offer, in that order.

#include <string>

namespace pioneer {

struct FactionInfo {
  const char* id;    // the "faction" in the rules: "ger"
  const char* name;  // shown to the player: "Germany"
};

inline constexpr FactionInfo kPlayableFactions[] = {
    {"ger", "Germany"},
    {"rus", "Russia"},
    {"gbr", "Britain"},
};
inline constexpr int kPlayableFactionCount = static_cast<int>(sizeof(kPlayableFactions) / sizeof(kPlayableFactions[0]));

// Index in kPlayableFactions, or 0 (Germany) for an unknown id.
inline int factionIndex(const std::string& id) {
  for (int i = 0; i < kPlayableFactionCount; ++i)
    if (id == kPlayableFactions[i].id) return i;
  return 0;
}
inline const char* factionName(const std::string& id) { return kPlayableFactions[factionIndex(id)].name; }
// The names joined with '\0' for an ImGui combo.
inline const char* factionComboItems() { return "Germany\0Russia\0Britain\0"; }

}  // namespace pioneer

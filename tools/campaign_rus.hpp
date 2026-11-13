// The Soviet campaign, The Road West (9.11): sides and pieces the five acts share.
// The player is always side 0, a Soviet formation; the Reich is side 1 unless a
// mission says otherwise.
#pragma once
#include "mission_kit.hpp"

namespace mk::rus {

// Soviet formations in blue, their neighbours in pale blue; the Polish forces
// in crimson; the coalition (the cut-off army group) in amber as in Germany.
constexpr uint32_t kSovietAlly = 0xFFE0A070, kPolish = 0xFF5040C0;

inline void gromov(Builder& b, int credits, int x, int y) {
  b.side(0, "5th Guards Rifle Division", "rus", kSoviet, 0, true, credits, x, y);
}
inline void zharkov(Builder& b, int credits, int x, int y) {
  b.side(0, "17th Guards Tank Brigade", "rus", kSoviet, 0, true, credits, x, y);
}
inline void orlova(Builder& b, int credits, int x, int y) {
  b.side(0, "Partisan brigade \"Za Rodinu\"", "rus", kSoviet, 0, true, credits, x, y);
}
inline void nazarov(Builder& b, int credits, int x, int y) {
  b.side(0, "61st Assault Engineer Brigade", "rus", kSoviet, 0, true, credits, x, y);
}
// The Reich: loyalist Wehrmacht and the Party's divisions, never the SS.
inline void reich(Builder& b, uint8_t i, const std::string& name, int credits, int x, int y, uint8_t difficulty = 1) {
  b.side(i, name, "ger", kReich, 1, false, credits, x, y, difficulty);
}
// A neighbouring Soviet formation, driven by the script.
inline void sovietAlly(Builder& b, uint8_t i, const std::string& name, int x, int y, int credits = 0) {
  b.side(i, name, "rus", kSovietAlly, 0, false, credits, x, y, 1);
}

// A Reich base: yard, power, barracks, factory, a bunker and a gun towards the
// enemy at (awayX, awayY), and its income buildings behind.
inline void reichBase(Builder& b, uint8_t p, int x, int y, int awayX, int awayY, bool strong = false) {
  const int dx = awayX > x ? 1 : awayX < x ? -1 : 0, dy = awayY > y ? 1 : awayY < y ? -1 : 0;
  b.place("ger_yard", p, x, y);
  b.place("ger_power", p, x - dx * 7 - dy * 6, y - dy * 7 - dx * 6);
  b.place("ger_barracks", p, x + dy * 8 - dx * 2, y + dx * 8 - dy * 2);
  b.place("ger_factory", p, x - dy * 9 - dx * 2, y - dx * 9 - dy * 2);
  b.place("ger_bunker", p, x + dx * 10 + dy * 4, y + dy * 10 + dx * 4);
  b.place("ger_atgun", p, x + dx * 10 - dy * 4, y + dy * 10 - dx * 4);
  if (strong) {
    b.place("ger_power", p, x - dx * 7 + dy * 6, y - dy * 7 + dx * 6);
    b.place("ger_mg_nest", p, x + dx * 12, y + dy * 12);
    b.place("ger_tower", p, x + dy * 6 + dx * 4, y + dx * 6 + dy * 4);
  }
  b.income("ger", p, x, y, awayX, awayY);
}

// A Russian village of log houses round a church or a well.
inline void izbaVillage(Builder& b, int x, int y, bool church = true) {
  for (auto [dx, dy, id] : {std::tuple{-6, -5, "neutral_izba"}, {6, -5, "neutral_izba"}, {-6, 6, "neutral_barn"},
                            {6, 6, "neutral_izba"}, {0, 10, "neutral_izba"}, {-11, 0, "neutral_izba"}})
    b.place(id, NEUTRAL_OWNER, x + dx, y + dy, 0, true);
  b.place(church ? "neutral_onion_church" : "neutral_well", NEUTRAL_OWNER, x, y - 1, 0, true);
}

// The usual end of a mission: the defeat of the player's side loses it.
inline void standardDefeat(Script& s) { s.on("Defeat", {defeated(0)}, {lose(0)}); }

}  // namespace mk::rus

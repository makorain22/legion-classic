// The British campaign, Hold Fast (9.12): sides and pieces the five acts share.
// The player is always side 0, a British formation; the Reich is side 1 unless
// a mission says otherwise. Canon: the same timeline as The Long Road Home.
#pragma once
#include "mission_kit.hpp"

namespace mk::gbr {

// British formations in green; the Commonwealth and the Guards in pale green;
// resistance movements (Dutch, Norwegian, Danish) in orange; Sonnenberg's group
// and the Free German Army in the coalition's amber, as in the German campaign.
constexpr uint32_t kCommonwealth = 0xFF80C890, kResistance = 0xFF2C8CE8;

inline void kerr(Builder& b, int credits, int x, int y) {
  b.side(0, "2nd Battalion, the Wessex Rifles", "gbr", kBritish, 0, true, credits, x, y);
}
inline void ashworth(Builder& b, int credits, int x, int y) {
  b.side(0, "7th Armoured Brigade", "gbr", kBritish, 0, true, credits, x, y);
}
inline void munro(Builder& b, int credits, int x, int y) {
  b.side(0, "No. 15 Commando", "gbr", kBritish, 0, true, credits, x, y);
}
inline void holloway(Builder& b, int credits, int x, int y) {
  b.side(0, "Special Operations Executive", "gbr", kBritish, 0, true, credits, x, y);
}
inline void reich(Builder& b, uint8_t i, const std::string& name, int credits, int x, int y, uint8_t difficulty = 1) {
  b.side(i, name, "ger", kReich, 1, false, credits, x, y, difficulty);
}
inline void commonwealth(Builder& b, uint8_t i, const std::string& name, int x, int y, int credits = 0) {
  b.side(i, name, "gbr", kCommonwealth, 0, false, credits, x, y, 1);
}
inline void resistance(Builder& b, uint8_t i, const std::string& name, int x, int y) {
  b.side(i, name, "gbr", kResistance, 0, false, 0, x, y, 1);
}
// Sonnenberg's group or the Free German Army: Germans who fight the regency, on our side.
inline void coalitionAlly(Builder& b, uint8_t i, const std::string& name, int x, int y, int credits = 0) {
  b.side(i, name, "ger", kCoalition, 0, false, credits, x, y, 1);
}

// A Reich base: yard, power, barracks, factory, a bunker and a gun towards
// (awayX, awayY), income buildings behind.
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
// A British base: yard, power, barracks, workshop, income behind.
inline void britishBase(Builder& b, int x, int y, int awayX, int awayY) {
  const int dx = awayX > x ? 1 : awayX < x ? -1 : 0, dy = awayY > y ? 1 : awayY < y ? -1 : 0;
  b.place("gbr_yard", 0, x, y);
  b.place("gbr_power", 0, x - dx * 7 - dy * 6, y - dy * 7 - dx * 6);
  b.place("gbr_barracks", 0, x + dy * 8 - dx * 2, y + dx * 8 - dy * 2);
  b.place("gbr_factory", 0, x - dy * 9 - dx * 2, y - dx * 9 - dy * 2);
  b.income("gbr", 0, x, y, awayX, awayY);
}
// A Low Countries town: brick rows, a church, a town hall.
inline void dutchTown(Builder& b, int x, int y) {
  for (auto [dx, dy, id] : {std::tuple{-7, -6, "neutral_row_brick"}, {-5, -6, "neutral_row_brick"}, {-3, -6, "neutral_row_brick"},
                            {5, -6, "neutral_row_brick"}, {7, -6, "neutral_row_brick"}, {-7, 7, "neutral_townhouse"},
                            {7, 7, "neutral_shopfront"}, {0, 9, "neutral_town_hall"}, {-10, 1, "neutral_terrace"}})
    b.place(id, NEUTRAL_OWNER, x + dx, y + dy, 0, true);
  b.place("neutral_church", NEUTRAL_OWNER, x, y - 1, 0, true);
}
// A desert village of mud-brick houses round a well.
inline void desertVillage(Builder& b, int x, int y) {
  for (auto [dx, dy, id] : {std::tuple{-6, -4, "neutral_adobe"}, {5, -5, "neutral_adobe"}, {-5, 6, "neutral_adobe"},
                            {6, 5, "neutral_adobe"}, {0, 9, "neutral_adobe"}})
    b.place(id, NEUTRAL_OWNER, x + dx, y + dy, 0, true);
  b.place("neutral_well", NEUTRAL_OWNER, x, y, 0, true);
}

inline void standardDefeat(Script& s) { s.on("Defeat", {defeated(0)}, {lose(0)}); }

}  // namespace mk::gbr

#pragma once
// Slave Engine - reading and writing map files (.lmap).
//
// Layout (little-endian):
//   "LMAP"  u32 version  u32 rawSize  u32 compressedSize  <deflate data>
// The compressed data holds a JSON header (name, water level, props, units,
// player settings) followed by the raw height and ground-layer arrays.
// Newer versions of the game can always read older map files.

#include <cstdint>
#include <string>
#include <vector>

#include "slave/map/map.hpp"

namespace slave {

// 1: terrain, props, units.  2: adds the oil layer and scenario sides.
constexpr uint32_t MAP_FILE_VERSION = 2;

// A unit placed in a scenario (by type id, so maps survive rule changes).
struct UnitPlacement {
  std::string type;
  uint8_t owner = 0;
  int32_t x = 0, y = 0;  // leptons
  uint16_t facing = 0;   // Angle
  int32_t health = -1;   // -1 = full
  std::string name;      // for mission scripts ("" = none)
  uint8_t flags = 0;     // ScriptFlag bits: invulnerable, kept from the computer, untargetable, locked
  uint8_t stance = 0;    // Stance: 0 aggressive, 1 hold ground, 2 hold fire
};

// One side in a scenario. Up to 8; `active` sides appear in the game.
struct PlayerSetup {
  std::string name;
  std::string faction = "ger";  // "ger", "rus", "gbr" (slave/sim/factions.hpp)
  uint32_t color = 0xFF2C3AC4;  // RGBA8, little-endian (R in the low byte)
  uint8_t team = 0;             // same team = allies
  bool active = false;
  bool human = true;            // false = computer player
  uint8_t difficulty = 1;       // computer: 0 easy, 1 normal, 2 hard
  int32_t startX = -1, startY = -1;  // where this side's camera starts, leptons (-1 = map centre)
  int32_t credits = 5000;            // supplies at the start
};

struct MapFile {
  Map map;
  std::vector<UnitPlacement> units;
  std::vector<PlayerSetup> players;  // empty = the game's default two sides
  std::string scriptJson;            // scenario triggers (see sim/script.hpp), "" = none
};

bool saveMap(const std::string& path, const Map& map, const std::vector<UnitPlacement>& units, std::string& error);
bool saveMap(const std::string& path, const Map& map, const std::vector<UnitPlacement>& units,
             const std::vector<PlayerSetup>& players, std::string& error, const std::string& scriptJson = "");
bool loadMap(const std::string& path, MapFile& out, std::string& error);

// In-memory versions (used by the file functions and by tests).
std::vector<uint8_t> encodeMap(const Map& map, const std::vector<UnitPlacement>& units,
                               const std::vector<PlayerSetup>& players = {}, const std::string& scriptJson = "");
bool decodeMap(const std::vector<uint8_t>& bytes, MapFile& out, std::string& error);

}  // namespace slave

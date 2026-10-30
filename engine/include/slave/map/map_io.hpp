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

constexpr uint32_t MAP_FILE_VERSION = 1;

// A unit placed in a scenario (by type id, so maps survive rule changes).
struct UnitPlacement {
  std::string type;
  uint8_t owner = 0;
  int32_t x = 0, y = 0;  // leptons
  uint16_t facing = 0;   // Angle
  int32_t health = -1;   // -1 = full
};

struct MapFile {
  Map map;
  std::vector<UnitPlacement> units;
};

bool saveMap(const std::string& path, const Map& map, const std::vector<UnitPlacement>& units, std::string& error);
bool loadMap(const std::string& path, MapFile& out, std::string& error);

// In-memory versions (used by the file functions and by tests).
std::vector<uint8_t> encodeMap(const Map& map, const std::vector<UnitPlacement>& units);
bool decodeMap(const std::vector<uint8_t>& bytes, MapFile& out, std::string& error);

}  // namespace slave

#pragma once
// Pioneer Engine - save games (.lsav) and replays (.lrep).
//
// Both hold a JSON description (for the load screen and the game's own
// settings), the map as it was (terrain, oil, props) and the simulation state.
// A replay also holds every command given after that state, with its tick,
// so the game can be played back exactly. Files are compressed and checked:
// a damaged file is refused, never half-loaded.

#include <cstdint>
#include <string>
#include <vector>

#include "pioneer/sim/determinism.hpp"
#include "pioneer/sim/simulation.hpp"

namespace pioneer {

constexpr uint32_t SAVE_FILE_VERSION = 3;  // 2: commands carry text (9.9); 3: replays carry state checksums

struct SaveFile {
  std::string metaJson;             // title, date, map name, the game's own view settings...
  std::vector<uint8_t> mapBytes;    // encodeMap()
  std::vector<uint8_t> stateBytes;  // Simulation::saveState()
  // Replays only:
  std::vector<Simulation::LoggedCommand> commands;
  uint32_t endTick = 0;
  // The state checksum once a second while the game was recorded: playing
  // the replay back checks itself against it (empty in older replays).
  std::vector<HashSample> hashes;
};

bool writeSaveFile(const std::string& path, const SaveFile& f, bool replay, std::string& error);
bool readSaveFile(const std::string& path, SaveFile& f, bool replay, std::string& error);
// In memory (used by the file functions and tests).
std::vector<uint8_t> encodeSaveFile(const SaveFile& f, bool replay);
bool decodeSaveFile(const std::vector<uint8_t>& bytes, SaveFile& f, bool replay, std::string& error);
// Just the description, for listing saves quickly.
bool readSaveMeta(const std::string& path, std::string& metaJson, bool replay);

}  // namespace pioneer

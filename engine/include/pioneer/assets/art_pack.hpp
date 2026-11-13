#pragma once
// Pioneer Engine - art packs: complete sets of unit sprites made outside the
// built-in voxel models, and the importer that makes one from the data of
// Nival's "Blitzkrieg".
//
// A pack is a folder of PNG files laid out like the art override folder
// (units/<model@faction>/body_f03_a0.png ...) with a sprites.json holding the
// anchors and a "_units" block that says how each unit's sheets are laid out.
// Keys may carry a biome: "tank@ger~winter" is used on winter maps, falling
// back to "tank@ger". See docs/ART_PIPELINE.md.

#include <array>
#include <string>
#include <unordered_map>

namespace pioneer {

struct BlitzkriegImport {
  std::string dataDir;      // Blitzkrieg's Data folder (Units/, Buildings/ ...)
  std::string mappingFile;  // which Blitzkrieg unit plays which of ours (content/art/blitzkrieg.json)
  std::string outDir;       // the pack folder to write
  // Footprints of our structure models, in tiles, so buildings are sized to fit.
  std::unordered_map<std::string, std::array<int, 2>> footprints;
};

// Renders every mapped unit, infantry and building into a pack. `report`
// gets one line per unit. False with `error` if nothing could be imported.
bool importBlitzkrieg(const BlitzkriegImport& job, std::string& error, std::string& report);

}  // namespace pioneer

#pragma once
// Pioneer Engine - MagicaVoxel .vox files, in and out.
//
// The engine's models are voxel grids with a 256-entry palette (voxel.hpp);
// MagicaVoxel (free, widely used) edits exactly that. Reading takes every
// model in the file (SIZE + XYZI chunk pairs) and the palette (RGBA chunk; a
// file without one, from very old versions, gets a grey ramp); the scene
// graph and materials are skipped. Writing makes one model and its palette.
//
// Axes match: x and y on the ground, z up. Sizes up to 256 on each side.
//
// Team colour: the engine marks some palette entries as "team" (painted in
// each side's colour at a brightness). A .vox has no such idea, so team
// entries travel as magenta: red and blue equal, no green, red = brightness
// x 192 - (192, 0, 192) is the team colour itself, darker magenta its shades,
// brighter (up to 255) its highlights. Reading turns such entries back into
// team entries, so a modder paints team colour in magenta.

#include <cstdint>
#include <string>
#include <vector>

#include "pioneer/assets/voxel.hpp"

namespace pioneer {

struct VoxFile {
  std::vector<VoxelModel> models;
  VoxelPalette palette;
};

bool readVox(const uint8_t* data, size_t size, VoxFile& out, std::string& error);
bool readVoxFile(const std::string& path, VoxFile& out, std::string& error);
std::vector<uint8_t> writeVox(const VoxelModel& model, const VoxelPalette& palette);
bool writeVoxFile(const std::string& path, const VoxelModel& model, const VoxelPalette& palette, std::string& error);

// The colour a palette entry travels as in a .vox (team entries as magenta).
Rgba voxColour(const VoxelPalette& palette, int index);

}  // namespace pioneer

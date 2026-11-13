#pragma once
// Pioneer Engine - voxel models as triangle meshes for the 3D view.
//
// Only faces between a solid voxel and air are kept, and neighbouring faces
// of the same colour and shading are merged into larger rectangles (greedy
// meshing). Positions are in voxel units with the model's own origin; the
// renderer moves the pivot to the unit's position, turns and scales it.
// Per-voxel grain and the darkening near the ground are worked out in the
// shader from the voxel coordinates, exactly as the sprite baker does, so
// merged faces still show them.

#include <cstdint>
#include <vector>

#include "pioneer/assets/voxel.hpp"

namespace pioneer {

struct MeshVertex {
  float x, y, z;     // voxel corner, model space
  uint8_t normal;    // 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z
  uint8_t occlusion; // crowding around the face, in half steps (0..12, the baker's "occ" x 2)
  uint8_t team;      // 0 = plain colour, else team colour at team / 100 brightness
  uint8_t index;     // palette index (seeds the per-voxel grain)
  uint8_t r, g, b, a;
};
static_assert(sizeof(MeshVertex) == 20, "MeshVertex layout");

// Two triangles per merged face, six vertices each.
std::vector<MeshVertex> meshVoxels(const VoxelModel& model, const VoxelPalette& palette);

// Half the resolution, for models seen from far off: each 2 x 2 x 2 block
// becomes one voxel of its most common colour when at least two of its eight
// cells are solid (so one-voxel-thin barrels and poles survive).
VoxelModel halveVoxels(const VoxelModel& model);

}  // namespace pioneer

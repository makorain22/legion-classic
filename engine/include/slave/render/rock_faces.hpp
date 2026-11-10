#pragma once
// Slave Engine - rock faces on steep ground (Milestone 9.2).
//
// Wherever the ground is too steep for anything to climb, the 3D view covers
// it with voxel rock: columns of stone hugging the slope in bands of colour,
// a ragged top edge, and here and there a ledge jutting out over the face
// below (a real overhang, with air under it). Built per terrain chunk from
// the map's smooth surface, meshed like any voxel model.

#include <vector>

#include "slave/assets/voxel_mesh.hpp"
#include "slave/map/map.hpp"
#include "slave/math/mat.hpp"

namespace slave {

constexpr int kRockVoxelsPerTile = 8;     // an eighth of a tile each way
constexpr int kCliffSlope = 41;           // tiles steeper than this (height units) are rock

// True when a tile is rock face.
inline bool isCliff(const Map& m, int tx, int ty) { return m.inBounds(tx, ty) && m.info(tx, ty).slope >= kCliffSlope; }

struct RockMesh {
  std::vector<MeshVertex> verts;  // voxel units from `origin`
  Vec3f origin;                   // world position of voxel (0,0,0), in tiles
};

// The rock for the tiles [tx0, tx1] x [ty0, ty1] (inclusive). Empty when
// none of them is steep enough.
RockMesh buildRockFaces(const Map& m, int tx0, int ty0, int tx1, int ty1);

}  // namespace slave

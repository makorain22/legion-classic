#pragma once
// Pioneer Engine - voxel figures posed by a skeleton.
//
// A voxel figure (a soldier, a crewman, a horse) is cut into rigid parts -
// thighs, shins, torso, head, upper and lower arms, the weapon - each fixed to
// one joint of a skeleton (skeleton.hpp). Posing it puts every part where its
// joint has gone and lays the voxels down again in a fresh grid: for each cell
// of the grid that a moved part could cover, the cell's centre is carried
// back into the part's rest position and the part's voxel there (if any) is
// taken. Working backwards like this leaves no holes when a part turns.
//
// humanoidRig() gives a standard figure: a skeleton of 15 joints sized for an
// 18-voxel man, and the infantry poses as clips - stand, walk (a four-step
// cycle), run, kneel, kneel and aim, prone, prone and aim, throw, and two
// ways of falling. A game cuts its own figure into the parts by joint name
// (assignByRegion() helps) and poses it with these clips or its own.
//
// Game-agnostic: no factions or kit here.

#include <string>
#include <vector>

#include "pioneer/assets/skeleton.hpp"
#include "pioneer/assets/voxel.hpp"

namespace pioneer {

struct VoxelPart {
  std::string name;
  int joint = -1;
  VoxelModel voxels;  // the part alone, in the rest pose, in the figure's grid
};

struct VoxelRig {
  Skeleton skeleton;              // in voxel units, z up, the figure's ground point at the origin
  std::vector<VoxelPart> parts;   // later parts are laid over earlier ones where they meet
  std::vector<AnimationClip> clips;
  // Where the figure's grid sits: rest grid cell (0,0,0) is at `origin` (voxel units).
  float origin[3] = {0, 0, 0};
  // The posed grid: its size and where its cell (0,0,0) is. Larger than the
  // rest grid, so a man lying down or throwing still fits.
  int poseX = 0, poseY = 0, poseZ = 0;
  float poseOrigin[3] = {0, 0, 0};
  int findClip(const std::string& name) const;
};

// The posed figure.
VoxelModel poseVoxels(const VoxelRig& rig, const Pose& pose);
// A clip at a time (seconds), from the rest pose.
VoxelModel poseVoxels(const VoxelRig& rig, const std::string& clip, float time);

// The standard humanoid: skeleton and infantry clips; no parts yet. The figure
// stands on the origin facing +x, `height` voxels tall (18 for the engine's soldiers).
VoxelRig humanoidRig(float height = 18.0f);
// The names of humanoidRig()'s clips.
const std::vector<std::string>& infantryPoses();

// Cuts a whole figure into parts: every solid voxel goes to the joint whose
// bone (from the joint to its first child, or the joint itself at an end)
// passes closest to the voxel's centre. `grid` is the rest figure in the rig's
// grid (cell (0,0,0) at rig.origin). Replaces rig.parts.
void assignByRegion(VoxelRig& rig, const VoxelModel& grid);

}  // namespace pioneer

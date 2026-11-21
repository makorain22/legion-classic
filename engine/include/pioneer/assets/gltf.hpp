#pragma once
// Pioneer Engine - glTF 2.0 models, in and out.
//
// glTF is the common exchange format of 3D tools (Blender, Maya, Godot, three.js).
// A ModelAsset is what the engine keeps of one: meshes (triangles with
// positions, normals, colours, texture coordinates and up to four joint
// weights), materials (the metallic-roughness model: base colour, metal,
// roughness, emission, an optional base-colour image, alpha mode, a team
// colour flag), a skeleton (from the first skin) and animation clips.
//
// Reading takes .gltf (JSON, with buffers embedded as base64 or in .bin files
// beside it) and .glb (the binary container). Node transforms are applied to
// the meshes they carry (except skinned ones, which the skeleton places), so
// a static model comes out in one space. Not read: morph targets, cameras,
// lights, sparse accessors, extensions.
//
// Writing makes a .glb with every mesh, material, the skeleton (as a skin)
// and the clips - and voxelModelAsset() turns an engine voxel model into one,
// so the game's units can be opened in Blender. glTF is y-up and the engine is
// z-up; both directions convert (x stays, engine y = -glTF z, engine z = glTF y).
//
// Game-agnostic: no unit or faction names here.

#include <cstdint>
#include <string>
#include <vector>

#include "pioneer/assets/skeleton.hpp"
#include "pioneer/assets/voxel.hpp"

namespace pioneer {

struct Material {
  std::string name;
  float baseColor[4] = {1, 1, 1, 1};
  float metallic = 0.0f, roughness = 0.8f;
  float emissive[3] = {0, 0, 0};
  enum class Alpha : uint8_t { Opaque, Mask, Blend } alpha = Alpha::Opaque;
  float alphaCutoff = 0.5f;
  bool doubleSided = false;
  bool team = false;            // painted in the owner's colour (base colour = brightness)
  std::string baseColorImage;   // file name (relative to the model) or "" (embedded images are not kept)
};

struct MeshVertexFull {
  float pos[3] = {0, 0, 0};
  float normal[3] = {0, 0, 1};
  float uv[2] = {0, 0};
  uint8_t color[4] = {255, 255, 255, 255};
  uint16_t joints[4] = {0, 0, 0, 0};
  float weights[4] = {0, 0, 0, 0};
};

struct MeshPart {
  std::string name;
  int material = -1;
  std::vector<MeshVertexFull> vertices;
  std::vector<uint32_t> indices;  // triangles
  bool skinned = false;
};

struct ModelAsset {
  std::vector<MeshPart> meshes;
  std::vector<Material> materials;
  Skeleton skeleton;
  std::vector<AnimationClip> clips;
  int findClip(const std::string& name) const;
  // Bounds of every vertex (engine space).
  void bounds(float lo[3], float hi[3]) const;
  size_t triangleCount() const;
};

bool readGltf(const std::string& path, ModelAsset& out, std::string& error);
// From bytes: a .glb, or .gltf JSON (external buffers are looked for in `baseDir`).
bool readGltfBytes(const std::vector<uint8_t>& bytes, const std::string& baseDir, ModelAsset& out, std::string& error);
std::vector<uint8_t> writeGlb(const ModelAsset& model);
bool writeGlbFile(const std::string& path, const ModelAsset& model, std::string& error);

// A voxel model as triangles: one mesh, a material for plain colours and one
// for team colour, sized `voxelTiles` per voxel (so 1 unit = 1 tile) with the
// pivot at the origin.
ModelAsset voxelModelAsset(const VoxelModel& model, const VoxelPalette& palette, float voxelTiles, float pivotX = 0,
                           float pivotY = 0, const std::string& name = "model");

}  // namespace pioneer

#pragma once
// Slave Engine - reader for the data of Nival's "Blitzkrieg" (2003), released
// by Nival for non-commercial use in 2025 (github.com/nival/Blitzkrieg).
//
// Reads the game's own files from a copy of its Data folder:
//   .mod  vehicles and guns: 3D meshes on a skeleton (hull, turret, gun...)
//   .san  sprite animations (infantry) and sprite packs (buildings)
//   .dds  textures (uncompressed, 565, 4444, DXT1/3/5)
// and renders them from our camera into sprites, so they can be used as a
// personal art pack (docs/ART_PIPELINE.md). No Blitzkrieg data ships with
// the engine.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "slave/assets/image.hpp"
#include "slave/assets/voxel.hpp"

namespace slave::bk {

// --- Files ------------------------------------------------------------------------------
bool loadDds(const std::string& path, Image& out);

struct Mesh {
  std::string name;
  std::vector<std::array<float, 3>> pos, norm;
  std::vector<std::array<float, 2>> uv;
  std::vector<std::array<int, 3>> comp;  // geometry, normal, texture index per vertex
  std::vector<uint16_t> idx;             // triangles, three vertices each
};
struct Node {
  std::string name;
  std::array<float, 3> bone{};            // offset from the parent
  std::array<float, 4> quat{0, 0, 0, 1};  // x, y, z, w
  std::vector<int> children;
};
// A model: meshes hang on skeleton nodes (mesh i belongs to node i). Units:
// Blitzkrieg world units, +Y forward, +X right, +Z up.
struct Model {
  std::vector<Node> nodes;
  std::vector<Mesh> meshes;
  int top = 0;
  int find(const std::string& nodeName) const;  // -1 if none
};
bool loadModel(const std::string& path, Model& out);

struct SpriteRect {
  float u0, v0, u1, v1;   // texture area
  int16_t x0, y0, x1, y1;  // placement around the sprite's foot point, pixels
};
struct SpriteAnim {
  std::vector<SpriteRect> rects;
  std::vector<std::vector<int16_t>> dirs;  // frame indices per direction
  int frameTime = 100;                     // ms
};
bool loadSpriteAnims(const std::string& path, std::vector<SpriteAnim>& out);

// Buildings: a picture assembled from square pieces of the texture.
struct Square {
  float x, y, size;  // top-left around the centre, pixels
  float u0, v0, u1, v1;
};
bool loadSpritePack(const std::string& path, std::vector<std::vector<Square>>& sprites);

// --- Rendering -------------------------------------------------------------------------------
struct MeshBake {
  float yaw = 0;                // radians: 0 = forward along our +X
  float tilesPerUnit = 1.0f / 77.0f;
  int supersample = 2;
  bool shadow = true;
  uint8_t shadowAlpha = 100;
  std::vector<int> nodes;       // which nodes' meshes to draw (empty = all)
  std::vector<int> shadowNodes; // which cast the shadow (empty = same as nodes)
  float pivotX = 0, pivotY = 0; // rotation centre, in model units (after the axis swap: x forward, y left)
  float brightness = 1.0f;
  std::array<float, 3> tint{1, 1, 1};
  bool pennant = false;         // a small team-coloured pennant on top (team markings)
};
// Renders the model from our isometric camera.
BakedSprite bakeModel(const Model& m, const Image& texture, const MeshBake& opt);
// Bounding box of the posed model in our axes (x forward, y left, z up), for pivots and scale.
void modelBounds(const Model& m, const std::vector<int>& nodes, float mn[3], float mx[3]);
// World position of a node's origin (our axes).
std::array<float, 3> nodeOrigin(const Model& m, int node);

// A sprite frame cut from a texture (scaled), with its foot point as anchor.
BakedSprite cutSprite(const Image& tex, const SpriteRect& r, float scale, float brightness = 1.0f);
// A building picture assembled from squares (scaled), anchored at its centre.
BakedSprite composeSquares(const Image& tex, const std::vector<Square>& squares, float scale, float brightness = 1.0f);

}  // namespace slave::bk

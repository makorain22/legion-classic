#pragma once
// Slave Engine - voxel models drawn as real 3D meshes.
//
// Every voxel model the art builder baked into sprites is remembered (see
// AtlasFrame::voxel). The first time the 3D view needs one it is meshed
// (voxel_mesh.hpp) and appended to one big vertex buffer. Each frame the
// scene queues instances (position, turn, team colour, tint); they are drawn
// grouped by mesh with GPU instancing, lit by the sun with the baker's
// shading, shadowed by the shadow map, and recoloured per player.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "slave/assets/voxel.hpp"
#include "slave/assets/voxel_mesh.hpp"
#include "slave/math/mat.hpp"
#include "slave/render/camera.hpp"
#include "slave/render/gl.hpp"

namespace slave {

class ShadowMap;
class TerrainRenderer;

struct MeshInstance {
  Vec3f pos;               // where the model's pivot stands, in tiles
  float yaw = 0;           // radians; 0 = the model's +X along the map's +X
  uint32_t tint = 0xFFFFFFFFu;  // RGBA8 multiplier; alpha below 255 fades it (dithered)
  uint32_t team = 0xFFFFFFFFu;  // RGBA8 team colour
  float scale = 1.0f;      // on top of the model's own voxel size
  float pitch = 0, roll = 0;  // lean, radians (vehicles on slopes)
  float blend = 0;         // tiles: the ground creeps this far up the model's foot (buildings, wrecks)
};

class MeshRenderer {
 public:
  MeshRenderer() = default;
  ~MeshRenderer();
  MeshRenderer(const MeshRenderer&) = delete;
  MeshRenderer& operator=(const MeshRenderer&) = delete;

  bool init(std::string& error);

  // The mesh for a voxel model, built on first use. -1 for a null source.
  int meshFor(const VoxelSourcePtr& source);
  // A mesh with its own buffer, for geometry that is rebuilt (rock faces
  // on the terrain): make one, then set (and later replace) its vertices.
  int createOwnedMesh();
  void setOwnedMesh(int mesh, const std::vector<MeshVertex>& verts, float voxelTiles, float ao, float grain);
  // Height of a mesh's model in tiles (for bars and pick boxes), and its
  // footprint radius in tiles.
  float meshHeight(int mesh) const { return mesh >= 0 ? meshes_[static_cast<size_t>(mesh)].height : 0; }
  float meshRadius(int mesh) const { return mesh >= 0 ? meshes_[static_cast<size_t>(mesh)].radius : 0; }

  void clear() { queue_.clear(); }
  // Far-off models are drawn from coarser meshes: after setView, add() picks
  // the detail by how many pixels a voxel covers there.
  void setView(const Camera& cam) { view_ = &cam; }
  // The ground whose textures creep over models' feet (null: none).
  void setGround(const TerrainRenderer* t) { ground_ = t; }
  void add(int mesh, const MeshInstance& inst);

  // Colour grade, as for sprites: rgb tint and saturation.
  void setGrade(float r, float g, float b, float saturation) {
    grade_[0] = r;
    grade_[1] = g;
    grade_[2] = b;
    grade_[3] = saturation;
  }
  // Draws everything queued since clear(). Depth test and depth writes on.
  void draw(const Camera& cam, const ShadowMap* shadow);
  // The queued models into a depth-only target (the sun's shadow map).
  void drawDepth(const Mat4& viewProj);

  size_t meshCount() const { return meshes_.size(); }
  size_t instancesLastFrame() const { return drawnInstances_; }
  size_t trianglesLastFrame() const { return drawnTriangles_; }
  size_t vertexBytes() const { return vertexCount_ * 20; }

 private:
  struct Mesh {
    GLint first = 0;
    GLsizei count = 0;
    float pivotX = 0, pivotY = 0, voxelTiles = 1, ao = 0, grain = 0;
    float height = 0, radius = 0;
    bool castsShadow = true;
    int coarser = -1;  // the same model at half the resolution (-1: none)
    GLuint ownVao = 0, ownVbo = 0;  // owned meshes keep their own buffer
  };
  struct GpuInstance {
    float x, y, z, yaw;
    float pivotX, pivotY, scale, ao;
    float grain, pitch, roll, blend;
    uint32_t tint, team;
  };
  struct Queued {
    int mesh;
    GpuInstance inst;
  };
  void upload(const std::vector<MeshVertex>& verts, Mesh& m);
  void setupVertexAttribs(GLuint vbo) const;
  void prepare();  // sorts the queue and uploads instance data
  void drawBatches(const gl::Shader& s, bool shadowPass);

  gl::Shader shader_, depthShader_;
  GLuint vao_ = 0, vbo_ = 0, instanceVbo_ = 0;
  size_t vertexCapacity_ = 0, vertexCount_ = 0, instanceCapacity_ = 0;
  std::vector<Mesh> meshes_;
  std::unordered_map<const VoxelSource*, int> bySource_;
  std::vector<VoxelSourcePtr> keepAlive_;
  std::vector<Queued> queue_;
  std::vector<GpuInstance> staged_;
  std::vector<std::pair<int, std::pair<size_t, size_t>>> batches_;  // mesh, first instance, count
  bool prepared_ = false;
  const Camera* view_ = nullptr;
  const TerrainRenderer* ground_ = nullptr;
  float grade_[4] = {1, 1, 1, 1};
  size_t drawnInstances_ = 0, drawnTriangles_ = 0;
};

}  // namespace slave

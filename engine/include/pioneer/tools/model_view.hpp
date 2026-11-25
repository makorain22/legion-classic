#pragma once
// Pioneer Engine - the model viewer's model: a model, a camera turning round
// it and an animation playing on it, without the window or the GPU (so it is
// tested without graphics; render/model_renderer.hpp draws what it gives).
//
//   tools::ModelView v;
//   v.setModel(model, "rifleman");   // the camera frames it
//   v.setClip("walk");
//   v.update(dt);                    // plays the clip
//   v.posedMeshes();                 // skinned on the CPU, as the pose has them
//   v.bones();                       // the skeleton as lines, for drawing over it
//
// Skinning is done on the CPU: a model in a viewer is one model, and the
// same code then runs on every graphics card the engine supports.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "pioneer/assets/gltf.hpp"
#include "pioneer/math/mat.hpp"

namespace pioneer::tools {

// A camera that circles a point: drag to turn, wheel to come closer.
struct OrbitCamera {
  float yaw = 0.7f;      // radians round z, from +x
  float pitch = 0.45f;   // radians above the ground plane (-1.5 .. 1.5)
  float distance = 4.0f; // from the target
  Vec3f target;
  float fovY = 0.75f;    // radians
  // Looks at a box from a pleasant angle, filling the view.
  void frame(const float lo[3], const float hi[3]);
  void orbit(float dx, float dy);  // radians
  void zoom(float steps);          // + closer, - further (each step 15 %)
  void pan(float dx, float dy);    // in screen fractions
  Vec3f eye() const;
  Mat4 view() const;
  Mat4 projection(float aspect) const;
};

struct Bone {
  Vec3f from, to;  // model space: a joint and its parent
  int joint = -1;
};

class ModelView {
 public:
  struct Options {
    bool skeleton = false;  // the bones over the model
    bool grid = true;       // the ground, one line per tile
    bool bounds = false;    // the model's box
    bool wireframe = false;
    bool turntable = false; // the camera goes round by itself
    uint32_t team = 0xFFC0502Cu;  // RGBA8 team colour for team-painted materials
  };
  struct Stats {
    size_t meshes = 0, vertices = 0, triangles = 0, materials = 0, joints = 0, clips = 0, skinned = 0;
    float size[3] = {0, 0, 0};
  };

  void setModel(std::shared_ptr<const ModelAsset> model, const std::string& name);
  void clear();
  const ModelAsset* model() const { return model_.get(); }
  const std::shared_ptr<const ModelAsset>& shared() const { return model_; }
  const std::string& name() const { return name_; }
  // Goes up whenever a different model is set (the renderer uploads it again).
  uint64_t modelRevision() const { return modelRevision_; }
  Stats stats() const;

  // ---- animation. Clip -1 is the rest pose.
  int clip() const { return clip_; }
  bool setClip(int index);
  bool setClip(const std::string& name);
  const AnimationClip* currentClip() const;
  void play(bool on) { playing_ = on; }
  bool playing() const { return playing_; }
  void setSpeed(float s) { speed_ = s; }
  float speed() const { return speed_; }
  void setLoop(bool on) { loop_ = on; }
  bool loop() const { return loop_; }
  float time() const { return time_; }
  void setTime(float t);
  float duration() const;
  // Advances the clip (and the turntable) by dt seconds of real time.
  void update(float dt);

  const Pose& pose();
  // Every mesh's vertices as the pose puts them (static meshes as they are).
  const std::vector<std::vector<MeshVertexFull>>& posedMeshes();
  // Goes up whenever posedMeshes() changes.
  uint64_t poseRevision() const { return poseRevision_; }
  std::vector<Bone> bones();

  OrbitCamera camera;
  Options options;

 private:
  void repose();
  std::shared_ptr<const ModelAsset> model_;
  std::string name_;
  int clip_ = -1;
  float time_ = 0, speed_ = 1;
  bool playing_ = true, loop_ = true, dirty_ = true;
  Pose pose_;
  std::vector<std::vector<MeshVertexFull>> posed_;
  uint64_t modelRevision_ = 0, poseRevision_ = 0;
};

}  // namespace pioneer::tools

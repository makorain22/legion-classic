#pragma once
// Pioneer Engine - skeletons and skeletal animation.
//
// A skeleton is a tree of joints, each with its rest placement relative to
// its parent (translation, rotation, scale) and the inverse of its bind
// matrix (model space to the joint's space when the mesh was bound). A clip
// is a set of channels, each moving one joint's translation, rotation or
// scale through keyframes (step or linear; rotations are slerped). Sampling
// a clip at a time gives a pose (every joint's local placement); poses blend
// (crossfades between clips, upper body over legs with a per-joint mask);
// from a pose come each joint's model-space matrix and the skinning matrices
// that move vertices bound to it.
//
// Plain maths, no graphics: glTF import fills these (gltf.hpp), the renderer
// or a tool skins with them, and voxel figures are posed with them.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "pioneer/math/mat.hpp"

namespace pioneer {

struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
};
Quat quatAxisAngle(float ax, float ay, float az, float radians);
Quat quatMul(const Quat& a, const Quat& b);
Quat quatSlerp(const Quat& a, const Quat& b, float t);
Quat quatNormalize(const Quat& q);

// Matrices are the renderer's Mat4 (math/mat.hpp): column-major, m[col * 4 + row], as glTF stores them.
inline Mat4 inverse(const Mat4& a) { return a.inverse(); }  // identity if singular
// Transforms a point (w = 1) or a direction (w = 0).
void transformPoint(const Mat4& m, const float in[3], float out[3]);
void transformDirection(const Mat4& m, const float in[3], float out[3]);

struct Transform {
  float t[3] = {0, 0, 0};
  Quat r;
  float s[3] = {1, 1, 1};
  Mat4 matrix() const;
};
Transform blend(const Transform& a, const Transform& b, float k);  // k = 0: a, 1: b
// Splits a matrix with no shear into translation, rotation and scale.
Transform decompose(const Mat4& m);

struct Joint {
  std::string name;
  int parent = -1;  // a parent always comes before its children
  Transform rest;
  Mat4 inverseBind;
};

class Skeleton {
 public:
  int add(const std::string& name, int parent, const Transform& rest);
  int find(const std::string& name) const;  // -1 if none
  size_t size() const { return joints_.size(); }
  const Joint& joint(int i) const { return joints_[static_cast<size_t>(i)]; }
  Joint& joint(int i) { return joints_[static_cast<size_t>(i)]; }
  const std::vector<Joint>& joints() const { return joints_; }
  // Sets every inverse bind matrix from the rest pose (when a file gives none).
  void bindRest();
  // Sorts the joints so parents come first (after reading a file in any order); returns old -> new.
  std::vector<int> sortParentsFirst();

 private:
  std::vector<Joint> joints_;
};

using Pose = std::vector<Transform>;  // local placement of each joint

Pose restPose(const Skeleton& s);
// Model-space matrix of every joint.
std::vector<Mat4> globalMatrices(const Skeleton& s, const Pose& p);
// Global x inverse bind: moves bind-pose vertices to where the pose puts them.
std::vector<Mat4> skinningMatrices(const Skeleton& s, const Pose& p);
// Blends b over a by k; with a mask (one weight per joint, 0..1) only where it says.
Pose blend(const Pose& a, const Pose& b, float k, const std::vector<float>* mask = nullptr);
// A mask of 1 for a joint and everything below it (the upper body from the spine), 0 elsewhere.
std::vector<float> maskBelow(const Skeleton& s, int joint);

enum class ChannelPath : uint8_t { Translation, Rotation, Scale };
enum class Interpolation : uint8_t { Step, Linear };

struct Channel {
  int joint = -1;
  ChannelPath path = ChannelPath::Rotation;
  Interpolation interpolation = Interpolation::Linear;
  std::vector<float> times;   // seconds, ascending
  std::vector<float> values;  // 3 per key (translation, scale) or 4 (rotation x y z w)
};

struct AnimationClip {
  std::string name;
  float duration = 0;  // seconds (the last key, if not set)
  bool loop = true;
  std::vector<Channel> channels;
  void computeDuration();
};

// The pose at time t (looped or held at the end). Joints the clip doesn't move keep `base`.
void sample(const AnimationClip& clip, float t, const Pose& base, Pose& out);

// Skins one vertex: position and normal by up to four joints and weights.
void skinVertex(const std::vector<Mat4>& skin, const float pos[3], const float nrm[3], const uint16_t joints[4],
                const float weights[4], float outPos[3], float outNrm[3]);

}  // namespace pioneer

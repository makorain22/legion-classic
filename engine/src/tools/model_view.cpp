// Pioneer Engine - the model viewer's model. See model_view.hpp.
#include "pioneer/tools/model_view.hpp"

#include <algorithm>
#include <cmath>

namespace pioneer::tools {

namespace {
constexpr float kPi = 3.14159265f;
}

// ---------------------------------------------------------------------------
// OrbitCamera

void OrbitCamera::frame(const float lo[3], const float hi[3]) {
  target = {(lo[0] + hi[0]) * 0.5f, (lo[1] + hi[1]) * 0.5f, (lo[2] + hi[2]) * 0.5f};
  const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
  const float radius = std::max(0.05f, 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz));
  distance = radius / std::sin(fovY * 0.5f) * 1.1f;
  yaw = 0.7f;
  pitch = 0.45f;
}

void OrbitCamera::orbit(float dx, float dy) {
  yaw = std::fmod(yaw + dx, 2 * kPi);
  pitch = std::clamp(pitch + dy, -1.5f, 1.5f);
}

void OrbitCamera::zoom(float steps) { distance = std::clamp(distance * std::pow(0.85f, steps), 0.05f, 500.0f); }

void OrbitCamera::pan(float dx, float dy) {
  // Along the screen's own right and up, scaled by how far away the target is.
  const Vec3f f = (target - eye()).normalized();
  const Vec3f right = cross(f, Vec3f{0, 0, 1}).normalized();
  const Vec3f up = cross(right, f);
  const float scale = distance * std::tan(fovY * 0.5f) * 2.0f;
  target += right * (-dx * scale) + up * (dy * scale);
}

Vec3f OrbitCamera::eye() const {
  const float c = std::cos(pitch);
  return target + Vec3f{std::cos(yaw) * c, std::sin(yaw) * c, std::sin(pitch)} * distance;
}

Mat4 OrbitCamera::view() const { return Mat4::lookAt(eye(), target, Vec3f{0, 0, 1}); }

Mat4 OrbitCamera::projection(float aspect) const {
  return Mat4::perspective(fovY, std::max(0.1f, aspect), std::max(0.01f, distance * 0.01f), distance * 20.0f + 10.0f);
}

// ---------------------------------------------------------------------------
// ModelView

void ModelView::setModel(std::shared_ptr<const ModelAsset> model, const std::string& name) {
  model_ = std::move(model);
  name_ = name;
  ++modelRevision_;
  clip_ = -1;
  time_ = 0;
  dirty_ = true;
  if (model_ && !model_->meshes.empty()) {
    float lo[3], hi[3];
    model_->bounds(lo, hi);
    camera.frame(lo, hi);
  }
  // A model that moves starts moving: its first clip (a figure: walking, if it can).
  if (model_ && !model_->clips.empty()) {
    const int walk = model_->findClip("walk");
    setClip(walk >= 0 ? walk : 0);
  }
}

void ModelView::clear() {
  model_.reset();
  name_.clear();
  ++modelRevision_;
  posed_.clear();
  pose_.clear();
  clip_ = -1;
  dirty_ = true;
}

ModelView::Stats ModelView::stats() const {
  Stats s;
  if (!model_) return s;
  s.meshes = model_->meshes.size();
  for (const MeshPart& p : model_->meshes) {
    s.vertices += p.vertices.size();
    if (p.skinned) ++s.skinned;
  }
  s.triangles = model_->triangleCount();
  s.materials = model_->materials.size();
  s.joints = model_->skeleton.size();
  s.clips = model_->clips.size();
  if (!model_->meshes.empty()) {
    float lo[3], hi[3];
    model_->bounds(lo, hi);
    for (int i = 0; i < 3; ++i) s.size[i] = hi[i] - lo[i];
  }
  return s;
}

bool ModelView::setClip(int index) {
  if (!model_ || index < -1 || index >= static_cast<int>(model_->clips.size())) return false;
  clip_ = index;
  time_ = 0;
  if (const AnimationClip* c = currentClip()) loop_ = c->loop;
  dirty_ = true;
  return true;
}

bool ModelView::setClip(const std::string& name) {
  if (!model_) return false;
  if (name.empty() || name == "rest") return setClip(-1);
  const int i = model_->findClip(name);
  return i >= 0 && setClip(i);
}

const AnimationClip* ModelView::currentClip() const {
  if (!model_ || clip_ < 0 || clip_ >= static_cast<int>(model_->clips.size())) return nullptr;
  return &model_->clips[static_cast<size_t>(clip_)];
}

float ModelView::duration() const {
  const AnimationClip* c = currentClip();
  return c ? c->duration : 0.0f;
}

void ModelView::setTime(float t) {
  const float d = duration();
  t = std::max(0.0f, t);
  if (d > 0) t = loop_ ? std::fmod(t, d) : std::min(t, d);
  else t = 0;
  if (t != time_) dirty_ = true;
  time_ = t;
}

void ModelView::update(float dt) {
  if (options.turntable) camera.orbit(dt * 0.6f, 0);
  if (!playing_ || !currentClip() || duration() <= 0) return;
  const float d = duration();
  float t = time_ + dt * speed_;
  if (loop_) {
    t = std::fmod(t, d);
    if (t < 0) t += d;
  } else {
    t = std::clamp(t, 0.0f, d);
  }
  if (t != time_) {
    time_ = t;
    dirty_ = true;
  }
}

void ModelView::repose() {
  if (!dirty_) return;
  dirty_ = false;
  ++poseRevision_;
  if (!model_) {
    pose_.clear();
    posed_.clear();
    return;
  }
  const Skeleton& sk = model_->skeleton;
  const Pose rest = restPose(sk);
  pose_ = rest;
  // Looping clips wrap; a clip held at its end shows its last frame.
  if (const AnimationClip* c = currentClip()) sample(*c, loop_ ? time_ : std::min(time_, c->duration), rest, pose_);
  const std::vector<Mat4> skin = sk.size() ? skinningMatrices(sk, pose_) : std::vector<Mat4>{};
  posed_.resize(model_->meshes.size());
  for (size_t m = 0; m < model_->meshes.size(); ++m) {
    const MeshPart& part = model_->meshes[m];
    std::vector<MeshVertexFull>& out = posed_[m];
    out = part.vertices;
    if (!part.skinned || skin.empty()) continue;
    for (MeshVertexFull& v : out) {
      const float p[3] = {v.pos[0], v.pos[1], v.pos[2]}, n[3] = {v.normal[0], v.normal[1], v.normal[2]};
      skinVertex(skin, p, n, v.joints, v.weights, v.pos, v.normal);
    }
  }
}

const Pose& ModelView::pose() {
  repose();
  return pose_;
}

const std::vector<std::vector<MeshVertexFull>>& ModelView::posedMeshes() {
  repose();
  return posed_;
}

std::vector<Bone> ModelView::bones() {
  std::vector<Bone> out;
  if (!model_ || model_->skeleton.size() == 0) return out;
  repose();
  const Skeleton& sk = model_->skeleton;
  const std::vector<Mat4> g = globalMatrices(sk, pose_);
  for (size_t j = 0; j < sk.size(); ++j) {
    const int parent = sk.joint(static_cast<int>(j)).parent;
    if (parent < 0) continue;
    Bone b;
    b.joint = static_cast<int>(j);
    b.to = {g[j].at(0, 3), g[j].at(1, 3), g[j].at(2, 3)};
    const Mat4& p = g[static_cast<size_t>(parent)];
    b.from = {p.at(0, 3), p.at(1, 3), p.at(2, 3)};
    out.push_back(b);
  }
  return out;
}

}  // namespace pioneer::tools

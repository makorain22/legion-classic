// Pioneer Engine - voxel figures posed by a skeleton. See voxel_rig.hpp.
#include "pioneer/assets/voxel_rig.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace pioneer {

namespace {
constexpr float kDeg = 3.14159265f / 180.0f;

Quat pitch(float degrees) { return quatAxisAngle(0, 1, 0, degrees * kDeg); }  // + leans forward / swings a limb back
Quat roll(float degrees) { return quatAxisAngle(1, 0, 0, degrees * kDeg); }
Quat yaw(float degrees) { return quatAxisAngle(0, 0, 1, degrees * kDeg); }

struct ClipMaker {
  const Skeleton& s;
  AnimationClip clip;
  ClipMaker(const Skeleton& sk, const std::string& name, bool loop) : s(sk) {
    clip.name = name;
    clip.loop = loop;
  }
  // A rotation channel through keys (times, quaternions).
  ClipMaker& rot(const char* joint, std::vector<float> times, std::vector<Quat> keys) {
    Channel c;
    c.joint = s.find(joint);
    c.path = ChannelPath::Rotation;
    c.times = std::move(times);
    for (const Quat& q : keys) c.values.insert(c.values.end(), {q.x, q.y, q.z, q.w});
    clip.channels.push_back(std::move(c));
    return *this;
  }
  ClipMaker& hold(const char* joint, const Quat& q) { return rot(joint, {0}, {q}); }
  // The pelvis's place (it is the root, so this is where the figure's middle is).
  ClipMaker& move(const char* joint, std::vector<float> times, std::vector<std::array<float, 3>> keys) {
    Channel c;
    c.joint = s.find(joint);
    c.path = ChannelPath::Translation;
    c.times = std::move(times);
    for (const auto& k : keys) c.values.insert(c.values.end(), {k[0], k[1], k[2]});
    clip.channels.push_back(std::move(c));
    return *this;
  }
  AnimationClip done() {
    clip.computeDuration();
    return clip;
  }
};
}  // namespace

int VoxelRig::findClip(const std::string& name) const {
  for (size_t i = 0; i < clips.size(); ++i)
    if (clips[i].name == name) return static_cast<int>(i);
  return -1;
}

VoxelModel poseVoxels(const VoxelRig& rig, const Pose& pose) {
  VoxelModel out(std::max(1, rig.poseX), std::max(1, rig.poseY), std::max(1, rig.poseZ));
  const std::vector<Mat4> rest = globalMatrices(rig.skeleton, restPose(rig.skeleton));
  const std::vector<Mat4> now = globalMatrices(rig.skeleton, pose);
  for (const VoxelPart& part : rig.parts) {
    if (part.joint < 0 || part.joint >= static_cast<int>(now.size())) continue;
    const VoxelModel& v = part.voxels;
    // The part's occupied box at rest.
    int lo[3] = {v.sizeX(), v.sizeY(), v.sizeZ()}, hi[3] = {-1, -1, -1};
    for (int z = 0; z < v.sizeZ(); ++z)
      for (int y = 0; y < v.sizeY(); ++y)
        for (int x = 0; x < v.sizeX(); ++x)
          if (v.get(x, y, z)) {
            const int c[3] = {x, y, z};
            for (int i = 0; i < 3; ++i) {
              lo[i] = std::min(lo[i], c[i]);
              hi[i] = std::max(hi[i], c[i]);
            }
          }
    if (hi[0] < 0) continue;
    const size_t j = static_cast<size_t>(part.joint);
    const Mat4 move = now[j] * rest[j].inverse();  // rest model space -> posed model space
    const Mat4 back = move.inverse();
    // Where the box goes: the cells of the posed grid it could touch.
    float plo[3] = {1e9f, 1e9f, 1e9f}, phi[3] = {-1e9f, -1e9f, -1e9f};
    for (int k = 0; k < 8; ++k) {
      const float corner[3] = {rig.origin[0] + static_cast<float>((k & 1) ? hi[0] + 1 : lo[0]),
                               rig.origin[1] + static_cast<float>((k & 2) ? hi[1] + 1 : lo[1]),
                               rig.origin[2] + static_cast<float>((k & 4) ? hi[2] + 1 : lo[2])};
      float p[3];
      transformPoint(move, corner, p);
      for (int i = 0; i < 3; ++i) {
        plo[i] = std::min(plo[i], p[i]);
        phi[i] = std::max(phi[i], p[i]);
      }
    }
    const int size[3] = {out.sizeX(), out.sizeY(), out.sizeZ()};
    int a[3], b[3];
    for (int i = 0; i < 3; ++i) {
      a[i] = std::max(0, static_cast<int>(std::floor(plo[i] - rig.poseOrigin[i])) - 1);
      b[i] = std::min(size[i] - 1, static_cast<int>(std::ceil(phi[i] - rig.poseOrigin[i])) + 1);
    }
    // Each posed cell's centre carried back into the part at rest.
    for (int z = a[2]; z <= b[2]; ++z)
      for (int y = a[1]; y <= b[1]; ++y)
        for (int x = a[0]; x <= b[0]; ++x) {
          const float c[3] = {rig.poseOrigin[0] + static_cast<float>(x) + 0.5f, rig.poseOrigin[1] + static_cast<float>(y) + 0.5f,
                              rig.poseOrigin[2] + static_cast<float>(z) + 0.5f};
          float r[3];
          transformPoint(back, c, r);
          const uint8_t col = v.get(static_cast<int>(std::floor(r[0] - rig.origin[0])), static_cast<int>(std::floor(r[1] - rig.origin[1])),
                                    static_cast<int>(std::floor(r[2] - rig.origin[2])));
          if (col) out.set(x, y, z, col);
        }
  }
  return out;
}

VoxelModel poseVoxels(const VoxelRig& rig, const std::string& clip, float time) {
  const int ci = rig.findClip(clip);
  Pose p = restPose(rig.skeleton);
  if (ci >= 0) sample(rig.clips[static_cast<size_t>(ci)], time, restPose(rig.skeleton), p);
  return poseVoxels(rig, p);
}

const std::vector<std::string>& infantryPoses() {
  static const std::vector<std::string> kPoses = {"stand", "walk", "run", "kneel", "kneel_aim", "prone", "prone_aim",
                                                  "throw", "fall_back", "fall_forward"};
  return kPoses;
}

VoxelRig humanoidRig(float height) {
  VoxelRig rig;
  const float k = height / 18.0f;
  Skeleton& s = rig.skeleton;
  auto at = [&](float x, float y, float z) {
    Transform t;
    t.t[0] = x * k;
    t.t[1] = y * k;
    t.t[2] = z * k;
    return t;
  };
  // Joints at their places on an 18-voxel man standing on the origin, facing +x
  // (left is +y); each local placement is the step from its parent.
  const int pelvis = s.add("pelvis", -1, at(0, 0, 8.5f));
  const int spine = s.add("spine", pelvis, at(0, 0, 3.5f));      // chest, 12
  s.add("head", spine, at(0, 0, 2.5f));                          // 14.5
  const int shL = s.add("shoulder_l", spine, at(0, 2.5f, 1.5f));  // 13.5
  const int elL = s.add("elbow_l", shL, at(0.5f, 0, -2.5f));
  s.add("hand_l", elL, at(1.5f, 0, -0.5f));
  const int shR = s.add("shoulder_r", spine, at(0, -2.5f, 1.5f));
  const int elR = s.add("elbow_r", shR, at(0.5f, 0, -2.5f));
  s.add("hand_r", elR, at(1.5f, 0, -0.5f));
  const int hipL = s.add("hip_l", pelvis, at(0, 1, -0.5f));        // 8
  const int knL = s.add("knee_l", hipL, at(0.3f, 0, -3.5f));      // 4.5
  s.add("foot_l", knL, at(-0.3f, 0, -3.5f));                      // 1
  const int hipR = s.add("hip_r", pelvis, at(0, -1, -0.5f));
  const int knR = s.add("knee_r", hipR, at(0.3f, 0, -3.5f));
  s.add("foot_r", knR, at(-0.3f, 0, -3.5f));
  s.bindRest();
  const float P = 8.5f * k;  // the pelvis's height standing

  rig.clips.push_back(ClipMaker(s, "stand", true).hold("pelvis", Quat{}).done());
  // Walking: four steps a second, the arms against the legs.
  rig.clips.push_back(ClipMaker(s, "walk", true)
                          .rot("hip_l", {0, 0.25f, 0.5f, 0.75f, 1}, {pitch(-24), pitch(0), pitch(24), pitch(0), pitch(-24)})
                          .rot("knee_l", {0, 0.25f, 0.5f, 0.75f, 1}, {pitch(4), pitch(8), pitch(20), pitch(30), pitch(4)})
                          .rot("hip_r", {0, 0.25f, 0.5f, 0.75f, 1}, {pitch(24), pitch(0), pitch(-24), pitch(0), pitch(24)})
                          .rot("knee_r", {0, 0.25f, 0.5f, 0.75f, 1}, {pitch(20), pitch(30), pitch(4), pitch(8), pitch(20)})
                          .rot("shoulder_l", {0, 0.5f, 1}, {pitch(14), pitch(-14), pitch(14)})
                          .rot("shoulder_r", {0, 0.5f, 1}, {pitch(-14), pitch(14), pitch(-14)})
                          .move("pelvis", {0, 0.25f, 0.5f, 0.75f, 1}, {{{0, 0, P - 0.3f * k}}, {{0, 0, P}}, {{0, 0, P - 0.3f * k}}, {{0, 0, P}}, {{0, 0, P - 0.3f * k}}})
                          .done());
  // Running: longer strides, leaning in.
  rig.clips.push_back(ClipMaker(s, "run", true)
                          .hold("spine", pitch(16))
                          .rot("hip_l", {0, 0.3f, 0.6f}, {pitch(-45), pitch(35), pitch(-45)})
                          .rot("knee_l", {0, 0.15f, 0.3f, 0.45f, 0.6f}, {pitch(15), pitch(60), pitch(30), pitch(10), pitch(15)})
                          .rot("hip_r", {0, 0.3f, 0.6f}, {pitch(35), pitch(-45), pitch(35)})
                          .rot("knee_r", {0, 0.15f, 0.3f, 0.45f, 0.6f}, {pitch(30), pitch(10), pitch(15), pitch(60), pitch(30)})
                          .rot("shoulder_l", {0, 0.3f, 0.6f}, {pitch(30), pitch(-30), pitch(30)})
                          .rot("shoulder_r", {0, 0.3f, 0.6f}, {pitch(-30), pitch(30), pitch(-30)})
                          .move("pelvis", {0, 0.15f, 0.3f, 0.45f, 0.6f}, {{{0, 0, P - 0.8f * k}}, {{0, 0, P - 0.2f * k}}, {{0, 0, P - 0.8f * k}}, {{0, 0, P - 0.2f * k}}, {{0, 0, P - 0.8f * k}}})
                          .done());
  // Kneeling on the left knee: left thigh straight down, its shin back along the
  // ground; right thigh forward, right shin down to the foot.
  auto kneel = [&](const char* name, bool aim) {
    ClipMaker m(s, name, true);
    m.move("pelvis", {0}, {{{0.2f * k, 0, 5.0f * k}}})
        .hold("hip_l", pitch(0))
        .hold("knee_l", pitch(90))
        .hold("hip_r", pitch(-85))
        .hold("knee_r", pitch(85))
        .hold("spine", pitch(aim ? 6 : 10));
    if (aim) m.hold("shoulder_l", pitch(-75)).hold("elbow_l", pitch(-10)).hold("shoulder_r", pitch(-60)).hold("elbow_r", pitch(-30)).hold("head", pitch(8));
    return m.done();
  };
  rig.clips.push_back(kneel("kneel", false));
  rig.clips.push_back(kneel("kneel_aim", true));
  // Prone: the whole figure laid forward along +x, face down, head up a little.
  auto prone = [&](const char* name, bool aim) {
    ClipMaker m(s, name, true);
    m.move("pelvis", {0}, {{{-3.0f * k, 0, 1.5f * k}}}).hold("pelvis", pitch(88)).hold("head", pitch(-55));
    if (aim) m.hold("shoulder_l", pitch(-160)).hold("shoulder_r", pitch(-150)).hold("elbow_r", pitch(-20));
    else m.hold("shoulder_l", pitch(-120)).hold("shoulder_r", pitch(-120));
    return m.done();
  };
  rig.clips.push_back(prone("prone", false));
  rig.clips.push_back(prone("prone_aim", true));
  // Throwing a grenade: wind up with the right arm high behind, then over.
  rig.clips.push_back(ClipMaker(s, "throw", false)
                          .rot("spine", {0, 0.35f, 0.6f, 0.9f}, {pitch(0), pitch(-15), pitch(25), pitch(10)})
                          .rot("shoulder_r", {0, 0.35f, 0.6f, 0.9f}, {pitch(0), pitch(150), pitch(-60), pitch(-20)})
                          .rot("elbow_r", {0, 0.35f, 0.6f, 0.9f}, {pitch(0), pitch(-60), pitch(0), pitch(0)})
                          .rot("shoulder_l", {0, 0.35f, 0.6f, 0.9f}, {pitch(0), pitch(-70), pitch(20), pitch(0)})
                          .rot("hip_r", {0, 0.35f, 0.6f, 0.9f}, {pitch(0), pitch(15), pitch(-10), pitch(0)})
                          .done());
  // Falling: back or forward, ending flat.
  rig.clips.push_back(ClipMaker(s, "fall_back", false)
                          .rot("pelvis", {0, 0.4f, 0.8f}, {pitch(0), pitch(-40), pitch(-88)})
                          .move("pelvis", {0, 0.4f, 0.8f}, {{{0, 0, P}}, {{-1.0f * k, 0, 5.0f * k}}, {{-1.5f * k, 0, 1.2f * k}}})
                          .rot("knee_l", {0, 0.4f, 0.8f}, {pitch(0), pitch(40), pitch(10)})
                          .rot("shoulder_l", {0, 0.8f}, {roll(0), roll(-60)})
                          .rot("shoulder_r", {0, 0.8f}, {roll(0), roll(60)})
                          .done());
  rig.clips.push_back(ClipMaker(s, "fall_forward", false)
                          .rot("pelvis", {0, 0.4f, 0.8f}, {pitch(0), pitch(40), pitch(88)})
                          .move("pelvis", {0, 0.4f, 0.8f}, {{{0, 0, P}}, {{1.0f * k, 0, 5.0f * k}}, {{1.5f * k, 0, 1.2f * k}}})
                          .rot("hip_r", {0, 0.4f, 0.8f}, {pitch(0), pitch(-20), pitch(0)})
                          .rot("shoulder_l", {0, 0.8f}, {pitch(0), pitch(-150)})
                          .rot("shoulder_r", {0, 0.8f}, {yaw(0), yaw(30)})
                          .done());
  return rig;
}

void assignByRegion(VoxelRig& rig, const VoxelModel& grid) {
  const Skeleton& s = rig.skeleton;
  const std::vector<Mat4> g = globalMatrices(s, restPose(s));
  // Each joint's bone: from it to its first child (or just the joint).
  struct Bone {
    float a[3], b[3];
  };
  std::vector<Bone> bones(s.size());
  for (size_t j = 0; j < s.size(); ++j) {
    const float zero[3] = {0, 0, 0};
    transformPoint(g[j], zero, bones[j].a);
    std::copy(bones[j].a, bones[j].a + 3, bones[j].b);
    for (size_t c = 0; c < s.size(); ++c)
      if (s.joint(static_cast<int>(c)).parent == static_cast<int>(j)) {
        transformPoint(g[c], zero, bones[j].b);
        break;
      }
  }
  rig.parts.clear();
  for (size_t j = 0; j < s.size(); ++j) {
    VoxelPart p;
    p.name = s.joint(static_cast<int>(j)).name;
    p.joint = static_cast<int>(j);
    p.voxels = VoxelModel(grid.sizeX(), grid.sizeY(), grid.sizeZ());
    rig.parts.push_back(std::move(p));
  }
  std::vector<int> count(s.size(), 0);
  for (int z = 0; z < grid.sizeZ(); ++z)
    for (int y = 0; y < grid.sizeY(); ++y)
      for (int x = 0; x < grid.sizeX(); ++x) {
        const uint8_t c = grid.get(x, y, z);
        if (!c) continue;
        const float p[3] = {rig.origin[0] + static_cast<float>(x) + 0.5f, rig.origin[1] + static_cast<float>(y) + 0.5f,
                            rig.origin[2] + static_cast<float>(z) + 0.5f};
        size_t best = 0;
        float bestD = 1e30f;
        for (size_t j = 0; j < bones.size(); ++j) {
          const Bone& b = bones[j];
          float ab[3], ap[3];
          for (int i = 0; i < 3; ++i) {
            ab[i] = b.b[i] - b.a[i];
            ap[i] = p[i] - b.a[i];
          }
          const float len2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
          const float t = len2 > 0 ? std::clamp((ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / len2, 0.0f, 1.0f) : 0.0f;
          float d = 0;
          for (int i = 0; i < 3; ++i) {
            const float q = ap[i] - ab[i] * t;
            d += q * q;
          }
          if (d < bestD) {
            bestD = d;
            best = j;
          }
        }
        rig.parts[best].voxels.set(x, y, z, c);
        ++count[best];
      }
  std::vector<VoxelPart> kept;
  for (size_t j = 0; j < rig.parts.size(); ++j)
    if (count[j]) kept.push_back(std::move(rig.parts[j]));
  rig.parts = std::move(kept);
  // A posed grid with room to lie down, kneel and throw.
  const float h = static_cast<float>(grid.sizeZ());
  rig.poseX = grid.sizeX() + static_cast<int>(2 * h);
  rig.poseY = grid.sizeY() + static_cast<int>(h);
  rig.poseZ = grid.sizeZ() + static_cast<int>(h / 3);
  rig.poseOrigin[0] = rig.origin[0] - h;
  rig.poseOrigin[1] = rig.origin[1] - h / 2;
  rig.poseOrigin[2] = rig.origin[2];
}

}  // namespace pioneer

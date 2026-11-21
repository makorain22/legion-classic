// Part 7 assets: MagicaVoxel .vox in and out.
#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>

#include "pioneer/assets/vox_file.hpp"

using namespace pioneer;

TEST_CASE("vox: a model and its palette go out and come back the same, team colour included") {
  VoxelModel m(20, 12, 9);
  m.box(2, 2, 0, 18, 10, 4, 3);    // hull
  m.box(6, 4, 4, 14, 8, 7, 5);     // turret in team colour
  m.box(14, 5, 5, 20, 7, 6, 7);    // barrel
  m.set(0, 0, 8, 9);               // a corner voxel
  VoxelPalette p;
  p.set(3, Rgba{90, 96, 70, 255});
  p.setTeam(5, 1.0f);
  p.setTeam(6, 0.72f);
  p.set(7, Rgba{40, 40, 44, 255});
  p.set(9, Rgba{200, 0, 200, 255});  // looks like a team mark but is a plain colour
  const std::vector<uint8_t> bytes = writeVox(m, p);
  REQUIRE(bytes.size() > 1024);
  CHECK(std::memcmp(bytes.data(), "VOX ", 4) == 0);
  VoxFile f;
  std::string err;
  REQUIRE_MESSAGE(readVox(bytes.data(), bytes.size(), f, err), err);
  REQUIRE(f.models.size() == 1);
  const VoxelModel& r = f.models[0];
  REQUIRE(r.sizeX() == 20);
  REQUIRE(r.sizeY() == 12);
  REQUIRE(r.sizeZ() == 9);
  int differ = 0;
  for (int z = 0; z < 9; ++z)
    for (int y = 0; y < 12; ++y)
      for (int x = 0; x < 20; ++x) differ += r.get(x, y, z) != m.get(x, y, z);
  CHECK(differ == 0);
  CHECK(f.palette.color[3] == Rgba{90, 96, 70, 255});
  CHECK(f.palette.isTeam[5]);
  CHECK(f.palette.teamLevel[5] == doctest::Approx(1.0f).epsilon(0.01));
  CHECK(f.palette.isTeam[6]);
  CHECK(f.palette.teamLevel[6] == doctest::Approx(0.72f).epsilon(0.01));
  CHECK_FALSE(f.palette.isTeam[9]);  // nudged off magenta on the way out
  CHECK(voxColour(p, 5) == Rgba{192, 0, 192, 255});
}

TEST_CASE("vox: several models in one file, and damaged files are refused") {
  VoxelModel a(4, 4, 4), b(8, 2, 3);
  a.set(1, 1, 1, 2);
  b.box(0, 0, 0, 8, 2, 3, 4);
  VoxelPalette p;
  std::vector<uint8_t> one = writeVox(a, p), two = writeVox(b, p);
  // Splice b's SIZE and XYZI chunks into a's MAIN (before its RGBA).
  auto chunks = [](const std::vector<uint8_t>& v) {  // SIZE..XYZI bytes of a written file
    const size_t start = 20, rgba = v.size() - (12 + 1024);
    return std::vector<uint8_t>(v.begin() + static_cast<long>(start), v.begin() + static_cast<long>(rgba));
  };
  std::vector<uint8_t> both(one.begin(), one.begin() + static_cast<long>(one.size() - (12 + 1024)));
  const auto extra = chunks(two);
  both.insert(both.end(), extra.begin(), extra.end());
  both.insert(both.end(), one.end() - (12 + 1024), one.end());
  const uint32_t children = static_cast<uint32_t>(both.size() - 20);
  std::memcpy(both.data() + 16, &children, 4);
  VoxFile f;
  std::string err;
  REQUIRE_MESSAGE(readVox(both.data(), both.size(), f, err), err);
  REQUIRE(f.models.size() == 2);
  CHECK(f.models[0].get(1, 1, 1) == 2);
  CHECK(f.models[1].sizeX() == 8);
  CHECK(f.models[1].get(7, 1, 2) == 4);

  CHECK_FALSE(readVox(reinterpret_cast<const uint8_t*>("RIFF...."), 8, f, err));
  std::vector<uint8_t> cut(one.begin(), one.begin() + 40);
  CHECK_FALSE(readVox(cut.data(), cut.size(), f, err));
  const auto path = (std::filesystem::temp_directory_path() / "pioneer_test.vox").string();
  REQUIRE(writeVoxFile(path, a, p, err));
  REQUIRE(readVoxFile(path, f, err));
  CHECK(f.models[0].get(1, 1, 1) == 2);
  std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
#include "pioneer/assets/skeleton.hpp"

namespace {
bool near3(const float* a, float x, float y, float z, float eps = 1e-4f) {
  return std::abs(a[0] - x) < eps && std::abs(a[1] - y) < eps && std::abs(a[2] - z) < eps;
}
// An arm: shoulder at the origin, elbow one along x, hand one further.
Skeleton arm() {
  Skeleton s;
  Transform t;
  const int sh = s.add("shoulder", -1, t);
  t.t[0] = 1;
  const int el = s.add("elbow", sh, t);
  s.add("hand", el, t);
  s.bindRest();
  return s;
}
}  // namespace

TEST_CASE("skeleton: rest pose, a bent elbow, and skinning a vertex with it") {
  const Skeleton s = arm();
  const auto g = globalMatrices(s, restPose(s));
  float o[3];
  const float zero[3] = {0, 0, 0};
  transformPoint(g[2], zero, o);
  CHECK(near3(o, 2, 0, 0));
  // At rest the skinning matrices do nothing.
  const auto skin0 = skinningMatrices(s, restPose(s));
  const float hand[3] = {2, 0, 0}, up[3] = {0, 0, 1};
  const uint16_t j[4] = {2, 0, 0, 0};
  const float w[4] = {1, 0, 0, 0};
  float p[3], n[3];
  skinVertex(skin0, hand, up, j, w, p, n);
  CHECK(near3(p, 2, 0, 0));
  // Bend the elbow 90 degrees about z: the hand swings to (1, 1, 0).
  Pose bent = restPose(s);
  bent[1].r = quatAxisAngle(0, 0, 1, 1.5707963f);
  const auto skin = skinningMatrices(s, bent);
  skinVertex(skin, hand, up, j, w, p, n);
  CHECK(near3(p, 1, 1, 0));
  CHECK(near3(n, 0, 0, 1));
  // Half on the elbow, half on the hand: a vertex at the joint stays put, a halfway one moves halfway.
  const uint16_t j2[4] = {1, 2, 0, 0};
  const float w2[4] = {0.5f, 0.5f, 0, 0};
  const float mid[3] = {1.5f, 0, 0};
  skinVertex(skin, mid, up, j2, w2, p, n);
  CHECK(near3(p, 1, 0.5f, 0));
}

TEST_CASE("skeleton: matrices decompose, invert, and quaternions slerp the short way") {
  Transform t;
  t.t[0] = 3;
  t.t[1] = -2;
  t.t[2] = 0.5f;
  t.r = quatAxisAngle(0.3f, 1, 0.2f, 0.8f);
  t.s[0] = 2;
  t.s[1] = 2;
  t.s[2] = 2;
  const Transform d = decompose(t.matrix());
  CHECK(near3(d.t, 3, -2, 0.5f));
  CHECK(near3(d.s, 2, 2, 2));
  const float dot = d.r.x * t.r.x + d.r.y * t.r.y + d.r.z * t.r.z + d.r.w * t.r.w;
  CHECK(std::abs(std::abs(dot) - 1.0f) < 1e-4f);
  const Mat4 id = t.matrix() * inverse(t.matrix());
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) CHECK(id.at(r, c) == doctest::Approx(r == c ? 1.0f : 0.0f).epsilon(1e-4));
  const Quat a = quatAxisAngle(0, 0, 1, 0.1f), b = quatAxisAngle(0, 0, 1, 0.5f);
  Quat negB{-b.x, -b.y, -b.z, -b.w};  // the same rotation, the other sign
  const Quat m1 = quatSlerp(a, b, 0.5f), m2 = quatSlerp(a, negB, 0.5f);
  const Quat want = quatAxisAngle(0, 0, 1, 0.3f);
  CHECK(std::abs(std::abs(m1.x * want.x + m1.y * want.y + m1.z * want.z + m1.w * want.w) - 1) < 1e-4f);
  CHECK(std::abs(std::abs(m2.x * want.x + m2.y * want.y + m2.z * want.z + m2.w * want.w) - 1) < 1e-4f);
}

TEST_CASE("skeleton: clips sample, loop, step, and blend with a mask") {
  const Skeleton s = arm();
  AnimationClip wave;
  wave.name = "wave";
  Channel c;
  c.joint = 1;
  c.path = ChannelPath::Rotation;
  c.times = {0, 1, 2};
  const Quat q0{}, q1 = quatAxisAngle(0, 0, 1, 1.0f);
  c.values = {q0.x, q0.y, q0.z, q0.w, q1.x, q1.y, q1.z, q1.w, q0.x, q0.y, q0.z, q0.w};
  wave.channels.push_back(c);
  Channel lift;
  lift.joint = 0;
  lift.path = ChannelPath::Translation;
  lift.interpolation = Interpolation::Step;
  lift.times = {0, 1};
  lift.values = {0, 0, 0, 0, 0, 5};
  wave.channels.push_back(lift);
  wave.computeDuration();
  CHECK(wave.duration == 2.0f);
  Pose p;
  sample(wave, 0.5f, restPose(s), p);
  const Quat half = quatAxisAngle(0, 0, 1, 0.5f);
  CHECK(std::abs(std::abs(p[1].r.z * half.z + p[1].r.w * half.w) - 1) < 1e-4f);
  CHECK(p[0].t[2] == 0.0f);  // step: still the first key
  sample(wave, 1.2f, restPose(s), p);
  CHECK(p[0].t[2] == 5.0f);
  CHECK(p[2].t[0] == 1.0f);  // a joint the clip leaves alone keeps the base
  sample(wave, 2.5f, restPose(s), p);  // loops round to 0.5
  CHECK(std::abs(std::abs(p[1].r.z * half.z + p[1].r.w * half.w) - 1) < 1e-4f);
  wave.loop = false;
  sample(wave, 9.0f, restPose(s), p);  // held at the end
  CHECK(std::abs(p[1].r.w - 1.0f) < 1e-4f);
  // Blend only from the elbow down.
  Pose a = restPose(s), b = restPose(s);
  b[0].t[1] = 4;
  b[1].t[1] = 4;
  const std::vector<float> mask = maskBelow(s, 1);
  CHECK(mask == std::vector<float>{0, 1, 1});
  const Pose m = blend(a, b, 0.5f, &mask);
  CHECK(m[0].t[1] == 0.0f);
  CHECK(m[1].t[1] == 2.0f);
}

TEST_CASE("skeleton: joints read in any order are sorted parents first") {
  Skeleton s;
  Transform t;
  s.add("hand", 2, t);
  s.add("shoulder", -1, t);
  s.add("elbow", 1, t);
  const std::vector<int> map = s.sortParentsFirst();
  CHECK(s.joint(0).name == "shoulder");
  CHECK(s.joint(1).name == "elbow");
  CHECK(s.joint(2).name == "hand");
  CHECK(s.joint(2).parent == 1);
  CHECK(s.joint(1).parent == 0);
  CHECK(map == std::vector<int>{2, 0, 1});
}

// ---------------------------------------------------------------------------
#include "pioneer/assets/gltf.hpp"

TEST_CASE("gltf: a voxel model goes out as .glb and comes back with its shape, colours and team paint") {
  VoxelModel m(10, 6, 5);
  m.box(0, 0, 0, 10, 6, 3, 3);
  m.box(3, 2, 3, 7, 4, 5, 5);
  VoxelPalette p;
  p.set(3, Rgba{100, 110, 80, 255});
  p.setTeam(5, 1.0f);
  const ModelAsset a = voxelModelAsset(m, p, 0.1f, 5, 3, "tank");
  REQUIRE(a.meshes.size() == 2);
  CHECK(a.materials.size() == 2);
  CHECK(a.materials[1].team);
  float lo[3], hi[3];
  a.bounds(lo, hi);
  CHECK(lo[0] == doctest::Approx(-0.5f));
  CHECK(hi[0] == doctest::Approx(0.5f));
  CHECK(lo[2] == doctest::Approx(0.0f));
  CHECK(hi[2] == doctest::Approx(0.5f));
  const std::vector<uint8_t> glb = writeGlb(a);
  REQUIRE(glb.size() > 100);
  CHECK(std::memcmp(glb.data(), "glTF", 4) == 0);
  ModelAsset b;
  std::string err;
  REQUIRE_MESSAGE(readGltfBytes(glb, "", b, err), err);
  REQUIRE(b.meshes.size() == 2);
  CHECK(b.triangleCount() == a.triangleCount());
  float lo2[3], hi2[3];
  b.bounds(lo2, hi2);
  for (int i = 0; i < 3; ++i) {  // z-up survives the trip through y-up
    CHECK(lo2[i] == doctest::Approx(lo[i]).epsilon(1e-4));
    CHECK(hi2[i] == doctest::Approx(hi[i]).epsilon(1e-4));
  }
  CHECK(b.materials[1].team);
  CHECK(b.meshes[0].vertices[0].color[0] > 0);
  // Normals still point the same way: a face on top points up.
  bool up = false;
  for (const auto& v : b.meshes[1].vertices) up = up || (v.normal[2] > 0.99f && v.pos[2] > 0.49f);
  CHECK(up);
}

TEST_CASE("gltf: a skinned, animated model keeps its skeleton, bind and clips through a round trip") {
  // Two joints (hip at the origin, knee one up), a quad bound to the knee, and a clip bending the knee.
  ModelAsset a;
  Transform t;
  const int hip = a.skeleton.add("hip", -1, t);
  t.t[2] = 1;
  a.skeleton.add("knee", hip, t);
  a.skeleton.bindRest();
  MeshPart part;
  part.name = "leg";
  part.skinned = true;
  const float quad[4][3] = {{0, 0, 1}, {0.2f, 0, 1}, {0.2f, 0, 2}, {0, 0, 2}};
  for (const auto& q : quad) {
    MeshVertexFull v;
    std::copy(q, q + 3, v.pos);
    v.joints[0] = 1;
    v.weights[0] = 1;
    part.vertices.push_back(v);
  }
  part.indices = {0, 1, 2, 0, 2, 3};
  a.meshes.push_back(part);
  AnimationClip bend;
  bend.name = "kneel";
  Channel c;
  c.joint = 1;
  c.path = ChannelPath::Rotation;
  c.times = {0, 1};
  const Quat q0{}, q1 = quatAxisAngle(1, 0, 0, 1.5707963f);
  c.values = {q0.x, q0.y, q0.z, q0.w, q1.x, q1.y, q1.z, q1.w};
  bend.channels.push_back(c);
  bend.computeDuration();
  a.clips.push_back(bend);

  auto tipAt = [](const ModelAsset& m, float time, float out[3]) {
    const int ci = m.findClip("kneel");
    REQUIRE(ci >= 0);
    Pose p;
    sample(m.clips[static_cast<size_t>(ci)], time, restPose(m.skeleton), p);
    const auto skin = skinningMatrices(m.skeleton, p);
    const MeshVertexFull& v = m.meshes[0].vertices[3];  // the top of the quad
    float n[3];
    skinVertex(skin, v.pos, v.normal, v.joints, v.weights, out, n);
  };
  float before[3], after[3];
  tipAt(a, 0.5f, before);  // half way: the knee bent 45 degrees
  CHECK(before[2] == doctest::Approx(1.0f + 0.70711f).epsilon(1e-3));
  CHECK(std::abs(before[1]) == doctest::Approx(0.70711f).epsilon(1e-3));

  ModelAsset b;
  std::string err;
  REQUIRE_MESSAGE(readGltfBytes(writeGlb(a), "", b, err), err);
  REQUIRE(b.meshes.size() == 1);
  CHECK(b.meshes[0].skinned);
  CHECK(b.meshes[0].vertices[3].pos[2] == doctest::Approx(2.0f).epsilon(1e-4));  // the bind pose comes back upright
  CHECK(b.meshes[0].vertices[3].pos[1] == doctest::Approx(0.0f).epsilon(1e-4));
  CHECK(b.skeleton.find("knee") >= 0);
  CHECK(b.clips.size() == 1);
  CHECK(b.clips[0].duration == doctest::Approx(1.0f));
  tipAt(b, 0.5f, after);
  for (int i = 0; i < 3; ++i) CHECK(after[i] == doctest::Approx(before[i]).epsilon(1e-3));
  float rest[3];
  tipAt(b, 0.0f, rest);
  CHECK(rest[2] == doctest::Approx(2.0f).epsilon(1e-3));
  // Read, written again, read again (the reader's upright joint must not pile up).
  ModelAsset c2;
  REQUIRE_MESSAGE(readGltfBytes(writeGlb(b), "", c2, err), err);
  CHECK(c2.skeleton.size() == b.skeleton.size());
  float again[3];
  tipAt(c2, 0.5f, again);
  for (int i = 0; i < 3; ++i) CHECK(again[i] == doctest::Approx(before[i]).epsilon(1e-3));
  tipAt(c2, 0.0f, again);
  CHECK(again[2] == doctest::Approx(2.0f).epsilon(1e-3));
}

TEST_CASE("gltf: a hand-written .gltf with an embedded buffer and a node transform reads in engine space") {
  // One triangle, y-up, lifted 2 along glTF y by its node: engine z.
  const float tri[9] = {0, 0, 0, 1, 0, 0, 0, 0, -1};  // glTF -z is engine +y
  std::string b64;
  {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* p = reinterpret_cast<const uint8_t*>(tri);
    for (size_t i = 0; i < sizeof tri; i += 3) {
      const uint32_t n = static_cast<uint32_t>(p[i]) << 16 | (i + 1 < sizeof tri ? p[i + 1] << 8 : 0) | (i + 2 < sizeof tri ? p[i + 2] : 0);
      b64 += k[(n >> 18) & 63];
      b64 += k[(n >> 12) & 63];
      b64 += i + 1 < sizeof tri ? k[(n >> 6) & 63] : '=';
      b64 += i + 2 < sizeof tri ? k[n & 63] : '=';
    }
  }
  const std::string text = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
    "nodes":[{"mesh":0,"translation":[0,2,0]}],
    "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
    "materials":[{"name":"steel","pbrMetallicRoughness":{"baseColorFactor":[0.5,0.5,0.6,1],"metallicFactor":0.9,"roughnessFactor":0.3}}],
    "buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,)" + b64 + R"("}],
    "bufferViews":[{"buffer":0,"byteLength":36}],
    "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,-1],"max":[1,0,0]}]})";
  ModelAsset m;
  std::string err;
  REQUIRE_MESSAGE(readGltfBytes(std::vector<uint8_t>(text.begin(), text.end()), "", m, err), err);
  REQUIRE(m.meshes.size() == 1);
  REQUIRE(m.meshes[0].vertices.size() == 3);
  const auto& v = m.meshes[0].vertices;
  CHECK(v[0].pos[2] == doctest::Approx(2.0f));  // lifted: engine z
  CHECK(v[1].pos[0] == doctest::Approx(1.0f));
  CHECK(v[2].pos[1] == doctest::Approx(1.0f));  // glTF -z: engine +y
  CHECK(m.meshes[0].indices.size() == 3);
  CHECK(m.materials[0].metallic == doctest::Approx(0.9f));
  CHECK(m.materials[0].baseColor[2] == doctest::Approx(0.6f));
  // Damaged input is refused, not crashed on.
  const std::string bad = R"({"asset":{"version":"1.0"}})";
  CHECK_FALSE(readGltfBytes(std::vector<uint8_t>(bad.begin(), bad.end()), "", m, err));
  std::vector<uint8_t> cut = writeGlb(voxelModelAsset(VoxelModel(2, 2, 2), VoxelPalette{}, 1.0f));
  cut.resize(30);
  CHECK_FALSE(readGltfBytes(cut, "", m, err));
}

// ---------------------------------------------------------------------------
#include "pioneer/assets/voxel_rig.hpp"

namespace {
// A blocky 18-voxel man in an 8 x 10 x 19 grid whose cell (0,0,0) sits at (-4, -5, 0).
VoxelModel blockMan() {
  VoxelModel m(8, 10, 19);
  auto box = [&](int x0, int y0, int z0, int x1, int y1, int z1, uint8_t c) { m.box(x0 + 4, y0 + 5, z0, x1 + 4, y1 + 5, z1, c); };
  box(-1, 0, 0, 1, 2, 8, 2);     // left leg
  box(-1, -2, 0, 1, 0, 8, 2);    // right leg
  box(-1, -2, 8, 2, 2, 14, 3);   // torso
  box(-1, -1, 14, 1, 1, 17, 4);  // head
  box(0, 2, 10, 1, 3, 14, 3);    // left arm
  box(0, -3, 10, 1, -2, 14, 3);  // right arm
  return m;
}
struct Extent {
  int count = 0;
  int lo[3] = {999, 999, 999}, hi[3] = {-1, -1, -1};
};
Extent extent(const VoxelModel& m) {
  Extent e;
  for (int z = 0; z < m.sizeZ(); ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x)
        if (m.get(x, y, z)) {
          ++e.count;
          const int c[3] = {x, y, z};
          for (int i = 0; i < 3; ++i) {
            e.lo[i] = std::min(e.lo[i], c[i]);
            e.hi[i] = std::max(e.hi[i], c[i]);
          }
        }
  return e;
}
VoxelRig manRig() {
  VoxelRig rig = humanoidRig(18);
  rig.origin[0] = -4;
  rig.origin[1] = -5;
  assignByRegion(rig, blockMan());
  return rig;
}
}  // namespace

TEST_CASE("voxel rig: the humanoid has its joints and every infantry pose") {
  const VoxelRig rig = humanoidRig();
  CHECK(rig.skeleton.size() == 15);
  for (const char* j : {"pelvis", "spine", "head", "hand_l", "hand_r", "foot_l", "foot_r"}) CHECK(rig.skeleton.find(j) >= 0);
  for (const auto& name : infantryPoses()) CHECK_MESSAGE(rig.findClip(name) >= 0, name);
  CHECK(rig.clips[static_cast<size_t>(rig.findClip("walk"))].loop);
  CHECK_FALSE(rig.clips[static_cast<size_t>(rig.findClip("fall_back"))].loop);
  // The joints stand where an 18-voxel man has them.
  const auto g = globalMatrices(rig.skeleton, restPose(rig.skeleton));
  CHECK(g[static_cast<size_t>(rig.skeleton.find("head"))].at(2, 3) == doctest::Approx(14.5f));
  CHECK(g[static_cast<size_t>(rig.skeleton.find("foot_l"))].at(2, 3) == doctest::Approx(1.0f));
}

TEST_CASE("voxel rig: standing is the figure as built, voxel for voxel") {
  const VoxelRig rig = manRig();
  CHECK(rig.parts.size() >= 8);
  const VoxelModel man = blockMan();
  const VoxelModel stood = poseVoxels(rig, "stand", 0);
  const int dx = static_cast<int>(rig.origin[0] - rig.poseOrigin[0]), dy = static_cast<int>(rig.origin[1] - rig.poseOrigin[1]);
  int differ = 0;
  for (int z = 0; z < man.sizeZ(); ++z)
    for (int y = 0; y < man.sizeY(); ++y)
      for (int x = 0; x < man.sizeX(); ++x) differ += man.get(x, y, z) != stood.get(x + dx, y + dy, z);
  CHECK(differ == 0);
  CHECK(extent(stood).count == extent(man).count);
}

TEST_CASE("voxel rig: kneeling is lower, prone is flat and long, walking moves the legs, nothing falls apart") {
  const VoxelRig rig = manRig();
  const Extent stand = extent(poseVoxels(rig, "stand", 0));
  const Extent kneel = extent(poseVoxels(rig, "kneel", 0));
  const Extent prone = extent(poseVoxels(rig, "prone", 0));
  CHECK(kneel.hi[2] <= stand.hi[2] - 3);
  CHECK(prone.hi[2] <= 6);
  CHECK(prone.hi[0] - prone.lo[0] > prone.hi[2] - prone.lo[2] + 6);
  const VoxelModel a = poseVoxels(rig, "walk", 0.0f), b = poseVoxels(rig, "walk", 0.5f);
  int differ = 0;
  for (int z = 0; z < 8; ++z)
    for (int y = 0; y < a.sizeY(); ++y)
      for (int x = 0; x < a.sizeX(); ++x) differ += a.get(x, y, z) != b.get(x, y, z);
  CHECK(differ > 10);
  // Turning parts neither lose much of the man nor smear him.
  for (const auto& name : infantryPoses())
    for (float t : {0.0f, 0.3f, 0.7f}) {
      const Extent e = extent(poseVoxels(rig, name, t));
      CHECK_MESSAGE(e.count > stand.count * 0.8, name << " at " << t << ": " << e.count);
      CHECK_MESSAGE(e.count < stand.count * 1.35, name << " at " << t << ": " << e.count);
      CHECK_MESSAGE(e.lo[2] >= 0, name);
    }
  // A fall ends on the ground.
  CHECK(extent(poseVoxels(rig, "fall_back", 5.0f)).hi[2] <= 7);
  CHECK(extent(poseVoxels(rig, "fall_forward", 5.0f)).hi[2] <= 7);
}

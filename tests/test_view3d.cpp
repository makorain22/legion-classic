// The 3D view's maths without a window: the camera's projection and picking
// rays, and voxel models turned into meshes.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "slave/assets/voxel.hpp"
#include "slave/assets/voxel_mesh.hpp"
#include "slave/render/camera.hpp"

using namespace slave;

namespace {
Camera camera3D() {
  Camera c;
  c.setViewport(1280, 720);
  c.set3D(true);
  c.setTarget({20, 30, 1});
  return c;
}
}  // namespace

TEST_CASE("3D camera: the target is in the middle of the screen at the classic scale") {
  Camera c = camera3D();
  Vec2f s;
  float depth = 0;
  REQUIRE(c.project(c.target(), s, &depth));
  CHECK(s.x == doctest::Approx(640).epsilon(0.001));
  CHECK(s.y == doctest::Approx(360).epsilon(0.001));
  CHECK(depth > 0.0f);
  CHECK(depth < 1.0f);
  // Zoom 1 shows the ground at the target as the classic view did at 1x.
  CHECK(c.pixelsPerTileAt(c.target()) == doctest::Approx(Camera::kClassicPxPerTile).epsilon(0.01));
  c.setZoom3D(2.0f);
  CHECK(c.pixelsPerTileAt(c.target()) == doctest::Approx(2 * Camera::kClassicPxPerTile).epsilon(0.01));
}

TEST_CASE("3D camera: at the classic angle the map lies as it did in the classic view") {
  Camera c = camera3D();
  c.setYaw(Camera::kClassicYaw);
  Vec2f o, x, y, up;
  c.project(c.target(), o);
  c.project(c.target() + Vec3f{1, 0, 0}, x);
  c.project(c.target() + Vec3f{0, 1, 0}, y);
  c.project(c.target() + Vec3f{0, 0, 1}, up);
  // +x runs to the lower right, +y to the lower left, height goes up the screen.
  CHECK(x.x > o.x);
  CHECK(x.y > o.y);
  CHECK(y.x < o.x);
  CHECK(y.y > o.y);
  CHECK(up.y < o.y);
  CHECK(std::abs(up.x - o.x) < 0.5f);
  // Nearer (larger x + y) is drawn in front: smaller depth.
  float dNear = 0, dFar = 0;
  c.project(c.target() + Vec3f{2, 2, 0}, o, &dNear);
  c.project(c.target() - Vec3f{2, 2, 0}, o, &dFar);
  CHECK(dNear < dFar);
}

TEST_CASE("3D camera: the ray through a pixel passes through what is drawn there") {
  Camera c = camera3D();
  c.setYaw(2.1f);
  c.setPitch(0.9f);
  const Vec3f p{23.5f, 27.25f, 0.5f};
  Vec2f s;
  REQUIRE(c.project(p, s));
  Vec3f o, d;
  c.ray(s, o, d);
  // Distance from p to the ray.
  const Vec3f op = p - o;
  const Vec3f closest = o + d * dot(op, d);
  CHECK((closest - p).length() < 0.01f);
  CHECK(d.z < 0.0f);
}

TEST_CASE("3D camera: turning, tilting and zooming stay in their limits; zoom keeps the point under the mouse") {
  Camera c = camera3D();
  c.setPitch(10.0f);
  CHECK(c.pitch() == doctest::Approx(Camera::kMaxPitch));
  c.setPitch(-1.0f);
  CHECK(c.pitch() == doctest::Approx(Camera::kMinPitch));
  c.setYaw(-0.5f);
  CHECK(c.yaw() == doctest::Approx(6.2831853f - 0.5f));
  c.setZoom3D(100);
  CHECK(c.zoom() == doctest::Approx(Camera::kMaxZoom));
  c.setZoom3D(1);
  c.setPitch(Camera::kDefaultPitch);
  // The ground point under the mouse (on the target's level) stays put.
  auto onPlane = [&](Vec2f px) {
    Vec3f o, d;
    c.ray(px, o, d);
    return o + d * ((c.target().z - o.z) / d.z);
  };
  const Vec2f mouse{900, 500};
  const Vec3f before = onPlane(mouse);
  c.zoomStep(+1, mouse);
  const Vec3f after = onPlane(mouse);
  CHECK((after - before).length() < 0.01f);
  CHECK(c.zoom() > 1.0f);
  // The 3D view's screen-world plane is the screen itself.
  CHECK(c.worldToScreen({12, 34}).x == doctest::Approx(12));
  CHECK(c.screenToWorld({12, 34}).y == doctest::Approx(34));
  // Bounds hold the target on the map.
  c.setBounds3D({0, 0, 64, 64});
  c.setTarget({-5, 80, 0});
  CHECK(c.target().x == doctest::Approx(0));
  CHECK(c.target().y == doctest::Approx(64));
}

TEST_CASE("Voxel meshes: only faces open to the air, merged where they look alike") {
  VoxelPalette pal;
  pal.set(5, {100, 120, 140, 255});
  pal.set(6, {10, 20, 30, 255});
  pal.setTeam(1, 1.0f);

  SUBCASE("a single voxel on the ground: five faces, none underneath") {
    VoxelModel m(3, 3, 3);
    m.set(1, 1, 0, 5);
    const auto v = meshVoxels(m, pal);
    CHECK(v.size() == 5 * 6);
    for (const MeshVertex& x : v) CHECK(x.normal != 5);
  }
  SUBCASE("a floating voxel shows all six") {
    VoxelModel m(3, 3, 3);
    m.set(1, 1, 1, 5);
    CHECK(meshVoxels(m, pal).size() == 6 * 6);
  }
  SUBCASE("a bar of one colour is five rectangles, not twenty faces") {
    VoxelModel m(6, 3, 3);
    m.box(1, 1, 0, 5, 2, 1, 5);
    const auto v = meshVoxels(m, pal);
    CHECK(v.size() == 5 * 6);
    float maxX = 0;
    for (const MeshVertex& x : v) maxX = std::max(maxX, x.x);
    CHECK(maxX == doctest::Approx(5));
  }
  SUBCASE("two colours don't merge") {
    VoxelModel m(6, 3, 3);
    m.box(1, 1, 0, 3, 2, 1, 5);
    m.box(3, 1, 0, 5, 2, 1, 6);
    CHECK(meshVoxels(m, pal).size() > 5 * 6);
  }
  SUBCASE("team colour is marked, plain colour carried") {
    VoxelModel m(3, 3, 3);
    m.set(1, 1, 0, 1);
    const auto v = meshVoxels(m, pal);
    REQUIRE(!v.empty());
    CHECK(v[0].team == 100);
    VoxelModel n(3, 3, 3);
    n.set(1, 1, 0, 5);
    const auto w = meshVoxels(n, pal);
    CHECK(w[0].team == 0);
    CHECK(w[0].r == 100);
    CHECK(w[0].b == 140);
  }
  SUBCASE("crowded faces are shaded: a face beside a wall counts its neighbours") {
    VoxelModel m(4, 4, 4);
    m.set(1, 1, 0, 5);
    m.box(0, 0, 1, 4, 4, 2, 6);  // a roof over the voxel's top... and around its sides above
    bool occluded = false;
    for (const MeshVertex& x : meshVoxels(m, pal))
      if (x.index == 5 && x.occlusion > 0) occluded = true;
    CHECK(occluded);
  }
}

TEST_CASE("Voxel sources: a unit's facings share one model") {
  VoxelPalette pal;
  pal.set(5, {100, 120, 140, 255});
  VoxelModel a(4, 4, 4);
  a.set(1, 1, 1, 5);
  VoxelModel b = a;
  b.set(2, 2, 2, 5);
  BakeOptions o;
  VoxelSourceCache cache;
  const VoxelSourcePtr s1 = cache.get(a, pal, o);
  o.yawRadians = 1.0f;  // a different facing: the same source
  const VoxelSourcePtr s2 = cache.get(a, pal, o);
  CHECK(s1 == s2);
  const VoxelSourcePtr s3 = cache.get(b, pal, o);
  CHECK(s3 != s2);
  o.pivotX = 3;  // placed differently: a new one
  CHECK(cache.get(b, pal, o) != s3);
}

TEST_CASE("Voxel meshes: the far-off level keeps the shape and thin parts") {
  VoxelModel m(8, 8, 8);
  m.box(0, 0, 0, 8, 8, 2, 5);  // a slab
  m.box(3, 3, 2, 4, 4, 8, 6);  // a pole one voxel thick
  m.set(7, 7, 7, 6);           // a lone speck
  const VoxelModel h = halveVoxels(m);
  CHECK(h.sizeX() == 4);
  CHECK(h.sizeZ() == 4);
  CHECK(h.get(0, 0, 0) == 5);  // the slab, two cells deep: one coarse layer
  CHECK(h.get(1, 1, 2) == 6);  // the pole survives
  CHECK(h.get(3, 3, 3) == 0);  // the speck does not
  VoxelPalette pal;
  pal.set(5, {100, 100, 100, 255});
  pal.set(6, {50, 50, 50, 255});
  CHECK(meshVoxels(h, pal).size() < meshVoxels(m, pal).size());
}

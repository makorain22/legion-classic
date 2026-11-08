// Legion Classic - the 3D battlefield: the camera's controls, picking the
// ground under the mouse, and drawing the battle with real voxel meshes,
// the sun's shadows, ground decals and camera-facing effects.

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "slave/core/log.hpp"

#include "editor.hpp"
#include "game_scene.hpp"
#include "slave/core/rng.hpp"
#include "slave/math/iso.hpp"

using namespace slave;

namespace legion {

namespace {
constexpr uint32_t kSelectGreen = rgba8(120, 236, 96);
constexpr float kTilesPerSpritePixel = 1.0f / Camera::kClassicPxPerTile;
constexpr float kDecalLift = 0.012f;  // tiles above the ground, against z-fighting
}  // namespace

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------
void GameScene::cameraKeys(float dt) {
  if (!cam_.is3D()) return;
  const bool* keys = SDL_GetKeyboardState(nullptr);
  const SDL_Keymod mods = SDL_GetModState();
  float turn = 0, tilt = 0;
  if (settings_.held(InputAction::RotateLeft, keys, mods)) turn -= 1;
  if (settings_.held(InputAction::RotateRight, keys, mods)) turn += 1;
  if (settings_.held(InputAction::TiltUp, keys, mods)) tilt += 1;
  if (settings_.held(InputAction::TiltDown, keys, mods)) tilt -= 1;
  if (turn != 0 || tilt != 0) cam_.rotate(turn * 1.7f * dt, tilt * 0.9f * dt);
  if (settings_.held(InputAction::ResetCamera, keys, mods)) {
    cam_.setYaw(Camera::kClassicYaw);
    cam_.setPitch(Camera::kDefaultPitch);
    cam_.setZoom3D(1.0f);
  }
}

void GameScene::mmbDrag(float dx, float dy) {
  if (!cam_.is3D() || (SDL_GetModState() & SDL_KMOD_SHIFT)) {
    cam_.pan({-dx, -dy});
    return;
  }
  // Sideways turns the view around the point it looks at; up and down tilt it.
  cam_.rotate(-dx * 0.0065f, dy * 0.0045f);
}

// The first point the ray through a pixel meets on the ground (or the water).
Vec2f GameScene::groundAt3D(Vec2f px) const {
  Vec3f o, d;
  cam_.ray(px, o, d);
  const float water = static_cast<float>(map_.waterLevel()) / HEIGHT_UNITS_PER_TILE;
  const float top = std::max<float>(map_.maxHeight(), map_.waterLevel()) / HEIGHT_UNITS_PER_TILE + 0.05f;
  auto surface = [&](Vec3f p) {
    const float lx = std::clamp(p.x * 256.0f, 0.0f, map_.widthLeptons() - 1.0f);
    const float ly = std::clamp(p.y * 256.0f, 0.0f, map_.heightLeptons() - 1.0f);
    return std::max(map_.heightAt(lx, ly) / HEIGHT_UNITS_PER_TILE, water);
  };
  if (d.z > -1e-4f) {
    // Looking at the sky: the far ground straight ahead.
    const Vec3f f = Vec3f{d.x, d.y, 0}.normalized();
    const Vec3f g = o + f * 400.0f;
    return {g.x * 256.0f, g.y * 256.0f};
  }
  // Skip the air above the highest ground, then march in small steps.
  float t = o.z > top ? (o.z - top) / -d.z : 0.0f;
  const float step = 0.2f;
  float prev = t;
  bool hit = false;
  for (int i = 0; i < 6000; ++i) {
    const Vec3f p = o + d * t;
    if (p.z <= surface(p)) {
      hit = true;
      break;
    }
    if (p.z < -1.0f) break;
    prev = t;
    t += step;
  }
  if (hit) {
    float a = prev, b = t;
    for (int i = 0; i < 12; ++i) {
      const float m = (a + b) * 0.5f;
      const Vec3f p = o + d * m;
      if (p.z <= surface(p)) b = m;
      else a = m;
    }
    t = b;
  } else {
    t = (o.z - water) / -d.z;
  }
  const Vec3f g = o + d * t;
  return {g.x * 256.0f, g.y * 256.0f};
}

// How a vehicle leans on a slope: the ground's rise ahead and to its side.
void GameScene::lean(float lx, float ly, float yaw, float reach, float& pitch, float& roll) const {
  const float c = std::cos(yaw), s = std::sin(yaw);
  auto h = [&](float dx, float dy) {
    const float x = std::clamp(lx + dx, 0.0f, map_.widthLeptons() - 1.0f), y = std::clamp(ly + dy, 0.0f, map_.heightLeptons() - 1.0f);
    return surfaceHeight(x, y) / HEIGHT_UNITS_PER_TILE;
  };
  const float r = reach / 256.0f;
  const float ahead = h(c * reach, s * reach) - h(-c * reach, -s * reach);
  const float side = h(-s * reach * 0.7f, c * reach * 0.7f) - h(s * reach * 0.7f, -c * reach * 0.7f);
  pitch = std::clamp(std::atan2(ahead, 2.0f * r), -0.5f, 0.5f);
  roll = std::clamp(std::atan2(side, 1.4f * r), -0.5f, 0.5f);
}

void GameScene::ghost(int frame, float lx, float ly, uint32_t team, uint32_t tint) {
  const float h = map_.heightAt(std::clamp(lx, 0.0f, map_.widthLeptons() - 1.0f), std::clamp(ly, 0.0f, map_.heightLeptons() - 1.0f));
  const int mesh = meshOf(frame);
  if (mesh < 0) {
    renderer_.billboard(frame, worldPoint(lx, ly, h), kTilesPerSpritePixel, team, tint);
    return;
  }
  MeshInstance in;
  in.pos = worldPoint(lx, ly, h);
  in.yaw = art_.frames[static_cast<size_t>(frame)].voxelYaw;
  in.team = team;
  in.tint = tint;
  meshes_.add(mesh, in);
}

int GameScene::meshOf(int frame) {
  if (frame < 0 || frame >= static_cast<int>(frameMesh_.size())) return -1;
  int& m = frameMesh_[static_cast<size_t>(frame)];
  if (m == -2) m = meshes_.meshFor(art_.frames[static_cast<size_t>(frame)].voxel);
  return m;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// The items drawUnits gathered: models where the sprite came from one,
// camera-facing sprites for the rest.
void GameScene::queueItems3D() {
  for (const DrawItem& d : drawList_) {
    const int mesh = meshOf(d.frame);
    if (mesh < 0) {
      billboards_.push_back(d);
      continue;
    }
    MeshInstance in;
    in.pos = worldPoint(d.lx, d.ly, d.h);
    in.yaw = d.turned ? d.yaw : art_.frames[static_cast<size_t>(d.frame)].voxelYaw;
    in.tint = d.tint;
    in.team = d.team;
    in.pitch = d.pitch;
    in.roll = d.roll;
    meshes_.add(mesh, in);
  }
}

// Oil stains and trenches, laid on the ground.
void GameScene::drawGround3D() {
  const bool fog = fogEnabled();
  const float vw = static_cast<float>(app_.pixelWidth()), vh = static_cast<float>(app_.pixelHeight());
  int tx0 = map_.width(), ty0 = map_.height(), tx1 = 0, ty1 = 0;
  for (Vec2f c : {Vec2f{0, 0}, Vec2f{vw, 0}, Vec2f{0, vh}, Vec2f{vw, vh}, Vec2f{vw * 0.5f, 0}, Vec2f{vw * 0.5f, vh}}) {
    const Vec2f g = groundAt(c);
    tx0 = std::min(tx0, static_cast<int>(g.x / 256) - 2);
    ty0 = std::min(ty0, static_cast<int>(g.y / 256) - 2);
    tx1 = std::max(tx1, static_cast<int>(g.x / 256) + 2);
    ty1 = std::max(ty1, static_cast<int>(g.y / 256) + 2);
  }
  tx0 = std::max(0, tx0);
  ty0 = std::max(0, ty0);
  tx1 = std::min(map_.width() - 1, tx1);
  ty1 = std::min(map_.height() - 1, ty1);
  auto fogTint = [&](int tx, int ty, float alpha) -> uint32_t {
    float k = 1.0f;
    if (fog) {
      const uint8_t f = terrain_.fogAt(tx, ty);
      if (f == 0) return 0;
      k = f < 128 ? 0.48f * f / 128.0f : 0.48f + 0.52f * (f - 128) / 127.0f;
    }
    const uint8_t c = static_cast<uint8_t>(255 * k);
    return rgba8(c, c, c, static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255));
  };
  auto ground = [&](float gx, float gy) {
    gx = std::clamp(gx, 0.0f, map_.widthLeptons() - 1.0f);
    gy = std::clamp(gy, 0.0f, map_.heightLeptons() - 1.0f);
    return map_.heightAt(gx, gy) / HEIGHT_UNITS_PER_TILE + kDecalLift;
  };
  for (int ty = ty0; ty <= ty1; ++ty)
    for (int tx = tx0; tx <= tx1; ++tx) {
      const uint8_t oil = map_.oil(tx, ty);
      if (!oil) continue;
      const uint32_t tint = fogTint(tx, ty, 0.35f + oil / 255.0f * 0.65f);
      if (tint) renderer_.decal(art_.oil[hash2(tx, ty, 5) & 3], tx * 256.0f + 128, ty * 256.0f + 128, tint, kWhite, 4, ground);
    }
  Registry& reg = sim_->registry();
  for (uint32_t e : reg.pool<CStructure>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.type >= trenchTypes_.size() || !trenchTypes_[u.type]) continue;
    const CStructure& st = reg.pool<CStructure>().get(e);
    if (st.tx < tx0 || st.tx > tx1 || st.ty < ty0 || st.ty > ty1) continue;
    if (fog && !sim_->allied(u.owner, player_) && terrain_.fogAt(st.tx, st.ty) == 0) continue;
    auto isTrench = [&](int x, int y) {
      const Entity o = sim_->structureAt(x, y);
      return o.valid() && trenchTypes_[reg.get<CUnit>(o).type];
    };
    const int mask = (isTrench(st.tx + 1, st.ty) ? 1 : 0) | (isTrench(st.tx, st.ty + 1) ? 2 : 0) |
                     (isTrench(st.tx - 1, st.ty) ? 4 : 0) | (isTrench(st.tx, st.ty - 1) ? 8 : 0);
    const uint32_t tint = fogTint(st.tx, st.ty, 1.0f);
    if (tint) renderer_.decal(art_.trench[static_cast<size_t>(mask)], st.tx * 256.0f + 128, st.ty * 256.0f + 128, tint, kWhite, 8, ground);
  }
}

// Selection rings and order markers, on the ground.
void GameScene::drawSelection3D() {
  Registry& reg = sim_->registry();
  auto ground = [&](float gx, float gy) {
    gx = std::clamp(gx, 0.0f, map_.widthLeptons() - 1.0f);
    gy = std::clamp(gy, 0.0f, map_.heightLeptons() - 1.0f);
    return surfaceHeight(gx, gy) / HEIGHT_UNITS_PER_TILE + kDecalLift * 2;
  };
  for (Entity e : selection_) {
    const UnitTypeDef& d = types_.get(reg.get<CUnit>(e).type);
    if (d.isStructure()) {
      // Structures: brackets at the corners of the footprint.
      const CStructure& st = reg.get<CStructure>(e);
      for (int cy : {st.ty, st.ty + st.h})
        for (int cx : {st.tx, st.tx + st.w}) {
          const float x = static_cast<float>(cx), y = static_cast<float>(cy);
          const float sx = cx == st.tx ? 1.0f : -1.0f, sy = cy == st.ty ? 1.0f : -1.0f;
          const float z = ground(x * 256.0f, y * 256.0f) + 0.02f;
          const float L = 0.45f, W = 0.07f;
          const Vec3f a[4] = {{x, y, z}, {x + sx * L, y, z}, {x + sx * L, y + sy * W, z}, {x, y + sy * W, z}};
          const Vec3f b[4] = {{x, y, z}, {x + sx * W, y, z}, {x + sx * W, y + sy * L, z}, {x, y + sy * L, z}};
          renderer_.solidQuad(a, kSelectGreen);
          renderer_.solidQuad(b, kSelectGreen);
        }
      continue;
    }
    const CPosition& p = reg.get<CPosition>(e);
    const CPrevPosition& q = reg.get<CPrevPosition>(e);
    const float lx = q.x + (p.x - q.x) * alpha_, ly = q.y + (p.y - q.y) * alpha_;
    renderer_.decal(d.radius > 60 ? art_.selectLarge : art_.selectSmall, lx, ly, kSelectGreen, kWhite, 3, ground);
  }
  for (const Marker& m : markers_) {
    const uint8_t alpha = static_cast<uint8_t>(255 * (1.0f - m.age / 0.6f));
    renderer_.decal(art_.moveMarker, m.pos.x, m.pos.y, (m.color & 0x00FFFFFFu) | (uint32_t(alpha) << 24), kWhite, 4, ground);
  }
}

void GameScene::render3D() {
  // A hazy sky where the view reaches past the edge of the map.
  static const float kSky[5][3] = {
      {0.46f, 0.50f, 0.50f}, {0.62f, 0.66f, 0.70f}, {0.66f, 0.60f, 0.48f}, {0.42f, 0.50f, 0.44f}, {0.44f, 0.45f, 0.45f}};
  const float* sky = kSky[static_cast<int>(map_.biome) % 5];
  glClearColor(sky[0] * 0.5f, sky[1] * 0.5f, sky[2] * 0.5f, 1.0f);
  glClearDepth(1.0);
  glDepthMask(GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  renderer_.resetStats();
  pruneSelection();
  {
    // Colour grade per biome, as in the classic view.
    static const float kGrade[5][4] = {
        {1.03f, 1.00f, 0.93f, 0.86f}, {0.97f, 1.00f, 1.05f, 0.82f}, {1.05f, 1.00f, 0.90f, 0.90f},
        {1.00f, 1.03f, 0.95f, 0.94f}, {0.98f, 0.98f, 0.97f, 0.74f},
    };
    const float* g = kGrade[static_cast<int>(map_.biome) % 5];
    if (settings_.atmosphere) {
      renderer_.setGrade(g[0], g[1], g[2], g[3]);
      meshes_.setGrade(g[0], g[1], g[2], g[3]);
    } else {
      renderer_.setGrade(1, 1, 1, 1);
      meshes_.setGrade(1, 1, 1, 1);
    }
  }
  const RectF view = cam_.visibleWorld(160);
  const bool fog = fogEnabled();
  const BattleFx::FogAt fogAt = [&](int tx, int ty) -> uint8_t { return fog ? terrain_.fogAt(tx, ty) : 255; };

  // Everything standing on the field, as models.
  meshes_.clear();
  meshes_.setView(cam_);
  billboards_.clear();
  drawUnits(view);

  // The sun's shadows: the ground and the models seen from the sun.
  const bool shadows = settings_.shadows && shadow_.ready() && !opt_.noShadows;
  if (shadows) {
    float z0 = 0, z1 = 0;
    terrain_.heightRange(z0, z1);
    const float reach = std::clamp(34.0f / cam_.zoom(), 18.0f, 110.0f);
    shadow_.fit(cam_, z0, z1 + 3.0f, reach);
    shadow_.begin();
    terrain_.drawDepth(shadow_.matrix());
    meshes_.drawDepth(shadow_.matrix());
    shadow_.end(app_.pixelWidth(), app_.pixelHeight());
  }
  terrain_.draw3D(cam_, time_, shadows ? &shadow_ : nullptr);

  // Ground decals: stains, trenches, craters, selection rings, markers.
  const BattleFx::ModelSink bodies = [&](int frame, float lx, float ly, uint32_t team, uint32_t tint) {
    const int mesh = meshOf(frame);
    if (mesh < 0) return false;
    MeshInstance in;
    in.pos = worldPoint(lx, ly, map_.heightAt(lx, ly));
    in.yaw = art_.frames[static_cast<size_t>(frame)].voxelYaw;
    in.team = team;
    in.tint = tint;
    meshes_.add(mesh, in);
    return true;
  };
  renderer_.begin3D(cam_);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -2.0f);
  drawGround3D();
  fx_.drawGround(renderer_, view, fogAt, bodies);
  drawSelection3D();
  editor_->renderWorld(renderer_);  // ghosts of what is being placed
  sidebar_->renderWorld(renderer_);
  renderer_.flush();
  glDisable(GL_POLYGON_OFFSET_FILL);

  meshes_.draw(cam_, shadows ? &shadow_ : nullptr);
  if (std::getenv("LEGION_STATS3D") && frame_ % 30 == 0)
    SLV_INFO("3D: %zu models drawn, %.0fk triangles, %zu meshes, zoom %.2f", meshes_.instancesLastFrame(),
             meshes_.trianglesLastFrame() / 1000.0, meshes_.meshCount(), cam_.zoom());
  terrain_.drawWater3D(cam_, time_);

  // In the air: sprites with no model, smoke, fire, flashes, tracers.
  renderer_.begin3D(cam_);
  for (const DrawItem& d : billboards_)
    renderer_.billboard(d.frame, worldPoint(d.lx, d.ly, d.h), kTilesPerSpritePixel, d.team, d.tint);
  fx_.drawAir(renderer_, view, fogAt);
  renderer_.flush();
  glDepthMask(GL_TRUE);
  glDisable(GL_DEPTH_TEST);

  renderer_.beginWorld(cam_);  // screen space from here on
  drawOverlays(view, false);
}

}  // namespace legion

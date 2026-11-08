#pragma once
// Legion Classic - battle effects: muzzle flashes, tracers, shell bursts,
// smoke, dust, craters, fallen infantry and burning wrecks.
//
// Effects are presentation only. The simulation reports what happened as
// events (shots, impacts, deaths) and this module turns them into sprites.
// Nothing here feeds back into the game, so effects may use their own random
// numbers and frame timing without breaking determinism.

#include <cstdint>
#include <functional>
#include <vector>

#include "slave/assets/placeholder_art.hpp"
#include "slave/math/vec.hpp"
#include "slave/render/sprite_renderer.hpp"
#include "slave/sim/simulation.hpp"

namespace legion {

class BattleFx {
 public:
  // Effects move on the flat classic plane (the 2:1 projection of the ground
  // at height 0) and remember the ground's height under them, so they can be
  // drawn either in the classic view or placed in the 3D world.
  using ToScreen = std::function<slave::Vec2f(float lx, float ly)>;  // ground point (leptons) -> flat classic plane
  using HeightAt = std::function<float(float lx, float ly)>;         // ground height in classic pixels
  // Draws a sprite that was baked from a voxel model as the model itself
  // (3D view): false when it has no model, and the sprite is used instead.
  using ModelSink = std::function<bool(int frame, float lx, float ly, uint32_t team, uint32_t tint)>;
  using FogAt = std::function<uint8_t(int tx, int ty)>;               // 0 unexplored .. 255 visible

  // A burning or burnt-out vehicle, drawn in the unit pass so it sorts with units.
  struct Wreck {
    int32_t lx = 0, ly = 0;  // leptons
    int bodyFrame = 0;
    int turretFrame = -1;
    slave::Vec2f turretShift;  // leptons from the body to the turret pivot
    uint32_t team = slave::kWhite;
    float age = 0;
    float burn = 0;  // seconds of fire left
    float smokeTimer = 0;
  };

  void init(const slave::SpriteCatalog* art, const std::vector<slave::UnitArt>* unitArt,
            const slave::UnitTypeRegistry* types, const slave::WeaponRegistry* weapons, ToScreen toScreen,
            HeightAt heightAt);
  void clear();

  // `visible`: whether the watching player can see where this happened.
  void onEvent(const slave::SimEvent& e, bool visible, const std::array<uint32_t, slave::MAX_PLAYERS>& teams);
  void update(float dt);
  // Smoke (and, when badly hurt, flames) rising from a damaged building.
  void emitDamage(slave::Vec2i atLeptons, int size, float severity, float dt);

  // Craters, scorch marks and bodies: drawn after the terrain, before units.
  // In the 3D view (the renderer in a 3D pass) decals are laid on the ground
  // and bodies drawn through `models` when given.
  void drawGround(slave::SpriteRenderer& r, const slave::RectF& view, const FogAt& fog,
                  const ModelSink& models = nullptr) const;
  // Fire, smoke, flashes and tracers: drawn after the units.
  void drawAir(slave::SpriteRenderer& r, const slave::RectF& view, const FogAt& fog) const;

  const std::vector<Wreck>& wrecks() const { return wrecks_; }
  // Fire drawn on burning wrecks needs the wreck's screen position; the scene
  // calls this from its unit pass so flames sit on top of the hull.
  size_t particleCount() const { return particles_.size(); }
  size_t decalCount() const { return decals_.size() + corpses_.size() + wrecks_.size(); }

 private:
  enum class Kind : uint8_t { Puff, Explosion, Flash, Spark, Tracer, Flame };
  struct Particle {
    Kind kind = Kind::Puff;
    bool additive = false;
    int frame = 0;
    float x = 0, y = 0, h = 0;      // flat classic-plane ground point and height above the ground (px)
    float gz = 0;                   // the ground's height there (px)
    float vx = 0, vy = 0, vh = 0;   // px per second
    float gravity = 0, drag = 0;
    float age = 0, life = 1;
    float scale0 = 1, scale1 = 1;
    float alpha0 = 1, alpha1 = 0;
    uint32_t color = slave::kWhite;  // RGB tint; alpha comes from alpha0..1
    float x1 = 0, y1 = 0, gz1 = 0;   // tracer end (flat plane) and its height (px)
    int tx = 0, ty = 0;              // tile, for fog
  };
  struct Decal {
    float x = 0, y = 0;  // flat classic plane
    float gz = 0;        // ground height (px)
    int frame = 0;
    int animFrames = 1, animStride = 0;  // a short animation (a man falling), then the last frame stays
    float animSec = 0.1f;
    uint32_t team = slave::kWhite;
    float age = 0, life = 90;
    float alpha = 1;
    int tx = 0, ty = 0;
  };

  float rnd();                     // 0..1
  float rnd(float a, float b) { return a + (b - a) * rnd(); }
  Particle& add(Kind kind, slave::Vec2f at, int tx, int ty);
  void puff(slave::Vec2f at, int tx, int ty, int frame, float h, float life, float s0, float s1, float alpha,
            uint32_t color, float rise, float spread);
  void sparks(slave::Vec2f at, int tx, int ty, float h, int count, float speed);
  void explosion(slave::Vec2f at, int tx, int ty, float scale, bool crater, bool large);
  void addDecal(std::vector<Decal>& list, size_t cap, Decal d);
  void drawParticle(slave::SpriteRenderer& r, const Particle& p, float fogK) const;
  void drawParticle3D(slave::SpriteRenderer& r, const Particle& p, float fogK) const;
  float groundPx(float flatX, float flatY) const;

  const slave::SpriteCatalog* art_ = nullptr;
  const std::vector<slave::UnitArt>* unitArt_ = nullptr;
  const slave::UnitTypeRegistry* types_ = nullptr;
  const slave::WeaponRegistry* weapons_ = nullptr;
  ToScreen toScreen_;
  HeightAt heightAt_;
  std::vector<Particle> particles_;
  std::vector<Decal> decals_, corpses_;
  std::vector<Wreck> wrecks_;
  uint32_t seed_ = 0x9e3779b9u;
};

}  // namespace legion

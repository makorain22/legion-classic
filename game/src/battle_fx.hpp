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
  using ToScreen = std::function<slave::Vec2f(float lx, float ly)>;  // ground point (leptons) -> screen-world
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
            const slave::UnitTypeRegistry* types, const slave::WeaponRegistry* weapons, ToScreen toScreen);
  void clear();

  // `visible`: whether the watching player can see where this happened.
  void onEvent(const slave::SimEvent& e, bool visible, const std::array<uint32_t, slave::MAX_PLAYERS>& teams);
  void update(float dt);

  // Craters, scorch marks and bodies: drawn after the terrain, before units.
  void drawGround(slave::SpriteRenderer& r, const slave::RectF& view, const FogAt& fog) const;
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
    float x = 0, y = 0, h = 0;      // screen-world ground point and height above it (px)
    float vx = 0, vy = 0, vh = 0;   // px per second
    float gravity = 0, drag = 0;
    float age = 0, life = 1;
    float scale0 = 1, scale1 = 1;
    float alpha0 = 1, alpha1 = 0;
    uint32_t color = slave::kWhite;  // RGB tint; alpha comes from alpha0..1
    float x1 = 0, y1 = 0;            // tracer end
    int tx = 0, ty = 0;              // tile, for fog
  };
  struct Decal {
    float x = 0, y = 0;
    int frame = 0;
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

  const slave::SpriteCatalog* art_ = nullptr;
  const std::vector<slave::UnitArt>* unitArt_ = nullptr;
  const slave::UnitTypeRegistry* types_ = nullptr;
  const slave::WeaponRegistry* weapons_ = nullptr;
  ToScreen toScreen_;
  std::vector<Particle> particles_;
  std::vector<Decal> decals_, corpses_;
  std::vector<Wreck> wrecks_;
  uint32_t seed_ = 0x9e3779b9u;
};

}  // namespace legion

#include "battle_fx.hpp"

#include <algorithm>
#include <cmath>

#include "slave/math/iso.hpp"

using namespace slave;

namespace legion {

namespace {
constexpr size_t kMaxParticles = 24000;
constexpr size_t kMaxDecals = 3000;
constexpr size_t kMaxCorpses = 4000;
constexpr size_t kMaxWrecks = 1500;
constexpr float kBurnSeconds = 22.0f;
constexpr float kWreckSeconds = 480.0f;   // burnt-out hulks stay this long
constexpr float kCorpseSeconds = 600.0f;  // the dead stay on the field for ten minutes

uint32_t withAlpha(uint32_t rgb, float a) {
  return (rgb & 0x00FFFFFFu) | (static_cast<uint32_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f) << 24);
}
uint32_t scaleRgb(uint32_t c, float k) {
  auto ch = [&](int shift) {
    return static_cast<uint32_t>(std::clamp(((c >> shift) & 0xFF) * k, 0.0f, 255.0f)) << shift;
  };
  return ch(0) | ch(8) | ch(16) | (c & 0xFF000000u);
}
// Matches the terrain shader: explored-but-unseen ground is drawn at 48 %.
float fogBrightness(uint8_t f) {
  return f < 128 ? 0.48f * f / 128.0f : 0.48f + 0.52f * (f - 128) / 127.0f;
}
}  // namespace

void BattleFx::init(const SpriteCatalog* art, const std::vector<UnitArt>* unitArt, const UnitTypeRegistry* types,
                    const WeaponRegistry* weapons, ToScreen toScreen, HeightAt heightAt) {
  art_ = art;
  unitArt_ = unitArt;
  types_ = types;
  weapons_ = weapons;
  toScreen_ = std::move(toScreen);
  heightAt_ = std::move(heightAt);
  particles_.reserve(4096);
}

void BattleFx::clear() {
  particles_.clear();
  decals_.clear();
  corpses_.clear();
  wrecks_.clear();
}

float BattleFx::rnd() {
  seed_ = seed_ * 1664525u + 1013904223u;
  return static_cast<float>(seed_ >> 8) / 16777216.0f;
}

float BattleFx::groundPx(float x, float y) const {
  if (!heightAt_) return 0;
  const Vec2f l = iso::screenToWorld({x, y});
  return heightAt_(l.x, l.y);
}

BattleFx::Particle& BattleFx::add(Kind kind, Vec2f at, int tx, int ty) {
  if (particles_.size() >= kMaxParticles) {
    // Full: recycle the oldest-looking slot instead of growing forever.
    Particle& p = particles_[static_cast<size_t>(rnd() * (particles_.size() - 1))];
    p = Particle{};
    p.kind = kind;
    p.x = at.x;
    p.y = at.y;
    p.gz = groundPx(at.x, at.y);
    p.tx = tx;
    p.ty = ty;
    return p;
  }
  Particle& p = particles_.emplace_back();
  p.kind = kind;
  p.x = at.x;
  p.y = at.y;
  p.gz = groundPx(at.x, at.y);
  p.tx = tx;
  p.ty = ty;
  return p;
}

void BattleFx::puff(Vec2f at, int tx, int ty, int frame, float h, float life, float s0, float s1, float alpha,
                    uint32_t color, float rise, float spread) {
  Particle& p = add(Kind::Puff, at, tx, ty);
  p.frame = frame;
  p.h = h;
  p.life = life * rnd(0.8f, 1.2f);
  p.scale0 = s0;
  p.scale1 = s1;
  p.alpha0 = alpha;
  p.alpha1 = 0;
  p.color = color;
  p.vx = rnd(-spread, spread) + 4.0f;  // a light breeze to the east
  p.vy = rnd(-spread, spread) * 0.5f;
  p.vh = rise * rnd(0.7f, 1.3f);
  p.drag = 1.2f;
}

void BattleFx::sparks(Vec2f at, int tx, int ty, float h, int count, float speed) {
  for (int i = 0; i < count; ++i) {
    Particle& p = add(Kind::Spark, at, tx, ty);
    p.frame = art_->effects.spark;
    p.additive = true;
    p.h = h;
    const float a = rnd(0, 6.2831853f);
    const float s = speed * rnd(0.4f, 1.0f);
    p.vx = std::cos(a) * s;
    p.vy = std::sin(a) * s * 0.5f;
    p.vh = rnd(0.4f, 1.0f) * speed;
    p.gravity = 260;
    p.life = rnd(0.25f, 0.6f);
    p.alpha0 = 1;
    p.alpha1 = 0.2f;
    p.color = rgba8(255, 210, 140);
  }
}

void BattleFx::addDecal(std::vector<Decal>& list, size_t cap, Decal d) {
  if (list.size() >= cap) list.erase(list.begin(), list.begin() + static_cast<long>(cap / 8));
  d.gz = groundPx(d.x, d.y);
  list.push_back(d);
}

void BattleFx::explosion(Vec2f at, int tx, int ty, float scale, bool crater, bool large) {
  const EffectArt& fx = art_->effects;
  Particle& e = add(Kind::Explosion, at, tx, ty);
  e.frame = fx.explosion[0];
  e.life = large ? 0.85f : 0.6f;
  e.scale0 = scale * 0.85f;
  e.scale1 = scale * 1.1f;
  e.alpha0 = e.alpha1 = 1;
  Particle& f = add(Kind::Flash, at, tx, ty);
  f.frame = fx.flash[2];
  f.additive = true;
  f.h = 6 * scale;
  f.life = 0.09f;
  f.scale0 = scale * 1.6f;
  f.scale1 = scale * 2.2f;
  f.alpha0 = 0.9f;
  f.alpha1 = 0;
  f.color = rgba8(255, 220, 160);
  // Thrown earth, then a slow column of grey-brown smoke.
  for (int i = 0; i < (large ? 7 : 4); ++i)
    puff(at, tx, ty, fx.dust, rnd(0, 6), rnd(0.9f, 1.6f), 0.6f * scale, 1.5f * scale, 0.75f, rgba8(150, 128, 98), 18,
         38 * scale);
  for (int i = 0; i < (large ? 5 : 2); ++i)
    puff(at, tx, ty, fx.smoke, rnd(6, 14) * scale, rnd(2.2f, 3.5f), 0.7f * scale, 2.0f * scale, 0.55f,
         rgba8(70, 66, 60), 16, 10);
  sparks(at, tx, ty, 8 * scale, large ? 14 : 6, 120 * scale);
  if (crater) {
    Decal d;
    d.x = at.x + rnd(-1, 1);
    d.y = at.y + rnd(-1, 1);
    d.frame = large ? fx.craterLarge : fx.craterSmall;
    d.life = 120;
    d.tx = tx;
    d.ty = ty;
    addDecal(decals_, kMaxDecals, d);
  }
}

void BattleFx::onEvent(const SimEvent& ev, bool visible, const std::array<uint32_t, MAX_PLAYERS>& teams) {
  const EffectArt& fx = art_->effects;
  const int tx = ev.at.x >> LEPTON_SHIFT, ty = ev.at.y >> LEPTON_SHIFT;
  switch (ev.type) {
    case EventType::Fire: {
      if (!visible) return;
      const WeaponDef& w = weapons_->get(ev.weapon);
      // Muzzle: a little way along the line of fire, at gun height.
      const float dx = static_cast<float>(ev.at.x - ev.from.x), dy = static_cast<float>(ev.at.y - ev.from.y);
      const float len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
      const float reach = w.muzzle >= 2 ? 150.0f : 40.0f;
      const float mx = ev.from.x + dx / len * reach, my = ev.from.y + dy / len * reach;
      const Vec2f m = toScreen_(mx, my);
      const float gunH = w.muzzle >= 2 ? 11.0f : 7.0f;
      const int ftx = ev.from.x >> LEPTON_SHIFT, fty = ev.from.y >> LEPTON_SHIFT;
      Particle& f = add(Kind::Flash, m, ftx, fty);
      f.frame = fx.flash[std::min<int>(w.muzzle, 2)];
      f.additive = true;
      f.h = gunH;
      f.life = w.muzzle >= 2 ? 0.08f : 0.05f;
      f.scale0 = 1.0f;
      f.scale1 = w.muzzle >= 2 ? 1.4f : 1.0f;
      f.alpha0 = 1;
      f.alpha1 = 0.3f;
      if (w.projectile == ProjectileKind::Instant) {
        // Tracer from the muzzle to the target.
        const Vec2f t = toScreen_(static_cast<float>(ev.at.x) + rnd(-40, 40), static_cast<float>(ev.at.y) + rnd(-40, 40));
        Particle& tr = add(Kind::Tracer, m, ftx, fty);
        tr.frame = fx.spark;
        tr.additive = true;
        tr.h = gunH;
        tr.x1 = t.x;
        tr.y1 = t.y;
        tr.gz1 = groundPx(t.x, t.y) + 5;
        const float px = std::hypot(tr.x1 - m.x, tr.y1 - m.y);
        tr.life = std::max(0.09f, px / 1100.0f);
        tr.alpha0 = 0.95f;
        tr.alpha1 = 0.6f;
        tr.color = rgba8(255, 214, 140);
      } else {
        // Big gun: blast smoke at the muzzle and dust kicked up around the vehicle.
        for (int i = 0; i < 3; ++i)
          puff(m, ftx, fty, fx.smoke, gunH, rnd(1.0f, 1.6f), 0.35f, 1.1f, 0.6f, rgba8(178, 172, 160), 8, 16);
        const Vec2f base = toScreen_(static_cast<float>(ev.from.x), static_cast<float>(ev.from.y));
        for (int i = 0; i < 3; ++i)
          puff({base.x + rnd(-10, 10), base.y + rnd(-4, 4)}, ftx, fty, fx.dust, 1, rnd(0.6f, 1.0f), 0.5f, 1.1f, 0.45f,
               rgba8(150, 130, 100), 4, 26);
      }
      break;
    }
    case EventType::Impact: {
      if (!visible) return;
      const WeaponDef& w = weapons_->get(ev.weapon);
      const Vec2f at = toScreen_(static_cast<float>(ev.at.x), static_cast<float>(ev.at.y));
      if (w.projectile == ProjectileKind::Shell) {
        explosion(at, tx, ty, 0.8f + w.splash / 400.0f, !ev.hit, false);
        if (ev.hit) sparks(at, tx, ty, 10, 10, 150);
      } else if (ev.hit) {
        sparks({at.x + rnd(-4, 4), at.y + rnd(-2, 2)}, tx, ty, rnd(4, 9), 2, 60);
      } else {
        // A miss kicks up a little dirt near the target.
        const Vec2f m{at.x + rnd(-14, 14), at.y + rnd(-7, 7)};
        puff(m, tx, ty, fx.dust, 1, rnd(0.4f, 0.7f), 0.18f, 0.45f, 0.7f, rgba8(140, 120, 92), 10, 6);
      }
      break;
    }
    case EventType::StructurePlaced:
    case EventType::StructureSold: {
      if (!visible) return;
      const UnitTypeDef& def = types_->get(ev.unitType);
      const Vec2f at = toScreen_(static_cast<float>(ev.at.x), static_cast<float>(ev.at.y));
      const float spread = def.footW * 10.0f;
      for (int i = 0; i < 3 + def.footW * 2; ++i)
        puff({at.x + rnd(-spread, spread), at.y + rnd(-spread * 0.5f, spread * 0.5f)}, tx, ty, fx.dust, 1, rnd(0.8f, 1.4f),
             0.5f, 1.4f, 0.55f, rgba8(150, 132, 104), 12, 14);
      break;
    }
    case EventType::UnitDied: {
      const UnitTypeDef& def = types_->get(ev.unitType);
      const UnitArt& ua = (*unitArt_)[ev.unitType];
      const Vec2f at = toScreen_(static_cast<float>(ev.at.x), static_cast<float>(ev.at.y));
      const uint32_t team = teams[ev.owner % MAX_PLAYERS];
      if (def.isStructure()) {
        // A building goes up in a string of blasts and leaves a burning ruin.
        if (def.category == UnitCategory::Fortification) {
          if (visible) puff(at, tx, ty, fx.dust, 2, 0.9f, 0.5f, 1.2f, 0.6f, rgba8(140, 120, 92), 10, 12);
          break;
        }
        const float spread = def.footW * 9.0f;
        if (visible)
          for (int i = 0; i < 2 + def.footW; ++i)
            explosion({at.x + rnd(-spread, spread), at.y + rnd(-spread * 0.5f, spread * 0.5f)}, tx, ty,
                      1.1f + def.footW * 0.15f, false, true);
        Decal r;
        r.x = at.x;
        r.y = at.y;
        r.frame = art_->rubble[static_cast<size_t>(std::clamp(std::max(def.footW, def.footH) - 2, 0, 2))];
        r.life = 600;
        r.tx = tx;
        r.ty = ty;
        addDecal(decals_, kMaxDecals, r);
        if (wrecks_.size() >= kMaxWrecks) wrecks_.erase(wrecks_.begin());
        Wreck w;
        w.lx = ev.at.x;
        w.ly = ev.at.y;
        w.bodyFrame = -1;  // nothing standing: just fire and smoke
        w.burn = kBurnSeconds * 1.5f;
        wrecks_.push_back(w);
      } else if (def.infantry()) {
        if (ua.hasCorpse) {
          Decal d;
          d.x = at.x;
          d.y = at.y;
          d.frame = ua.corpse.frame(facingOf(ev.facing), 0);
          d.animFrames = ua.corpse.animFrames;
          d.animStride = ua.corpse.facings;
          d.animSec = std::max(0.02f, ua.corpseMs / 1000.0f);
          d.team = team;
          d.life = kCorpseSeconds * rnd(0.85f, 1.15f);
          d.tx = tx;
          d.ty = ty;
          addDecal(corpses_, kMaxCorpses, d);
        }
        if (visible) puff(at, tx, ty, fx.dust, 1, 0.6f, 0.3f, 0.7f, 0.5f, rgba8(140, 120, 92), 6, 8);
      } else {
        if (visible) explosion(at, tx, ty, 1.25f, false, true);
        Decal s;
        s.x = at.x;
        s.y = at.y;
        s.frame = fx.scorch;
        s.life = 150;
        s.alpha = 0.9f;
        s.tx = tx;
        s.ty = ty;
        addDecal(decals_, kMaxDecals, s);
        if (wrecks_.size() >= kMaxWrecks) wrecks_.erase(wrecks_.begin());
        Wreck w;
        w.lx = ev.at.x;
        w.ly = ev.at.y;
        w.bodyFrame = ua.body.frame(facingOf(ev.facing), 0);
        if (ua.hasTurret) {
          w.turretFrame = ua.turret.frame(facingOf(ev.turret), 0);
          const float a = static_cast<float>(angleOfFacing(facingOf(ev.facing))) * (6.2831853f / ANGLE_FULL);
          w.turretShift = {std::cos(a) * ua.turretOffset, std::sin(a) * ua.turretOffset};
        }
        w.team = team;
        w.burn = kBurnSeconds * rnd(0.7f, 1.3f);
        wrecks_.push_back(w);
      }
      break;
    }
    default:
      break;
  }
}

void BattleFx::emitDamage(Vec2i at, int size, float severity, float dt) {
  // Puffs per second grow with the damage; badly hit buildings also burn.
  if (rnd() > dt * (1.5f + severity * 6.0f)) return;
  const EffectArt& fx = art_->effects;
  const Vec2f s = toScreen_(static_cast<float>(at.x) + rnd(-size * 80.0f, size * 80.0f), static_cast<float>(at.y) + rnd(-size * 80.0f, size * 80.0f));
  const int tx = at.x >> LEPTON_SHIFT, ty = at.y >> LEPTON_SHIFT;
  const uint8_t g = static_cast<uint8_t>(40 + 40 * (1.0f - severity));
  Particle& sm = add(Kind::Puff, s, tx, ty);
  sm.frame = fx.smoke;
  sm.h = rnd(14, 24);
  sm.life = rnd(3.0f, 5.0f);
  sm.scale0 = 0.5f;
  sm.scale1 = 2.0f + severity;
  sm.alpha0 = 0.35f + 0.4f * severity;
  sm.alpha1 = 0;
  sm.color = rgba8(g, g, g);
  sm.vx = rnd(5, 10);
  sm.vh = rnd(20, 30);
  sm.drag = 0.3f;
  if (severity > 0.5f && rnd() < 0.6f) {
    Particle& f = add(Kind::Flame, {s.x + rnd(-4, 4), s.y}, tx, ty);
    f.frame = fx.fire[0];
    f.additive = true;
    f.h = rnd(10, 20);
    f.vh = 8;
    f.life = rnd(0.3f, 0.5f);
    f.scale0 = 0.4f + 0.4f * severity;
    f.scale1 = 0.15f;
    f.alpha0 = 0.85f;
    f.alpha1 = 0;
    f.color = rgba8(255, 190, 120);
  }
}

void BattleFx::update(float dt) {
  if (dt <= 0) return;
  for (Particle& p : particles_) {
    p.age += dt;
    const float drag = std::max(0.0f, 1.0f - p.drag * dt);
    p.vx *= drag;
    p.vy *= drag;
    p.vh = p.vh * drag - p.gravity * dt;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    p.h = std::max(0.0f, p.h + p.vh * dt);
  }
  std::erase_if(particles_, [](const Particle& p) { return p.age >= p.life; });

  for (Decal& d : decals_) d.age += dt;
  for (Decal& d : corpses_) d.age += dt;
  std::erase_if(decals_, [](const Decal& d) { return d.age >= d.life; });
  std::erase_if(corpses_, [](const Decal& d) { return d.age >= d.life; });

  // Burning wrecks pour out smoke and the odd flame.
  const EffectArt& fx = art_->effects;
  for (Wreck& w : wrecks_) {
    w.age += dt;
    if (w.burn <= 0) continue;
    w.burn -= dt;
    w.smokeTimer -= dt;
    if (w.smokeTimer > 0) continue;
    const float heat = std::clamp(w.burn / kBurnSeconds, 0.0f, 1.0f);
    w.smokeTimer = 0.09f + (1.0f - heat) * 0.3f;
    const Vec2f at = toScreen_(static_cast<float>(w.lx), static_cast<float>(w.ly));
    const int tx = w.lx >> LEPTON_SHIFT, ty = w.ly >> LEPTON_SHIFT;
    // Thick oily black smoke while the fire is hot, thinning to grey wisps.
    const uint8_t g = static_cast<uint8_t>(22 + 50 * (1.0f - heat));
    Particle& sm = add(Kind::Puff, {at.x + rnd(-4, 4), at.y + rnd(-2, 2)}, tx, ty);
    sm.frame = fx.smoke;
    sm.h = rnd(8, 12);
    sm.life = rnd(4.5f, 6.5f);
    sm.scale0 = 0.5f;
    sm.scale1 = 2.6f + 1.2f * heat;
    sm.alpha0 = 0.55f + 0.4f * heat;
    sm.alpha1 = 0;
    sm.color = rgba8(g, g, static_cast<uint8_t>(g - 2));
    sm.vx = rnd(6, 12);  // the wind leans the column east
    sm.vy = rnd(-2, 1);
    sm.vh = rnd(26, 36);
    sm.drag = 0.25f;
    if (heat > 0.2f && rnd() < 0.7f) {
      Particle& f = add(Kind::Flame, {at.x + rnd(-6, 6), at.y + rnd(-3, 3)}, tx, ty);
      f.frame = fx.fire[0];
      f.additive = true;
      f.h = rnd(3, 7);
      f.vh = 8;
      f.life = rnd(0.3f, 0.5f);
      f.scale0 = 0.35f + 0.4f * heat;
      f.scale1 = 0.15f;
      f.alpha0 = 0.8f;
      f.alpha1 = 0.0f;
      f.color = rgba8(255, 190, 120);
    }
  }
  std::erase_if(wrecks_, [](const Wreck& w) { return w.age >= kWreckSeconds; });
}

void BattleFx::drawGround(SpriteRenderer& r, const RectF& view, const FogAt& fog, const ModelSink& models) const {
  auto draw = [&](const Decal& d) {
    const bool threeD = r.in3D();
    if (!threeD && !view.contains({d.x, d.y - d.gz})) return;
    const uint8_t f = fog(d.tx, d.ty);
    if (f == 0) return;
    const float fade = std::clamp((d.life - d.age) / 8.0f, 0.0f, 1.0f);  // fade out over the last 8 s
    const float k = fogBrightness(f);
    const uint32_t tint = withAlpha(scaleRgb(kWhite, k), fade * d.alpha);
    const int step = d.animFrames > 1 ? std::min(d.animFrames - 1, static_cast<int>(d.age / d.animSec)) : 0;
    const int frame = d.frame + step * d.animStride;
    if (!threeD) {
      r.sprite(frame, d.x, d.y - d.gz, d.team, tint);
      return;
    }
    const Vec2f l = iso::screenToWorld({d.x, d.y});
    if (models && models(frame, l.x, l.y, d.team, tint)) return;
    r.decal(frame, l.x, l.y, tint, d.team, 4, [&](float gx, float gy) {
      return (heightAt_ ? heightAt_(gx, gy) : 0.0f) / iso::HEIGHT_PX_PER_TILE + 0.015f;
    });
  };
  for (const Decal& d : decals_) draw(d);
  for (const Decal& d : corpses_) draw(d);
}

void BattleFx::drawParticle(SpriteRenderer& r, const Particle& p, float fogK) const {
  const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
  const float alpha = p.alpha0 + (p.alpha1 - p.alpha0) * t;
  const float scale = p.scale0 + (p.scale1 - p.scale0) * t;
  uint32_t color = scaleRgb(p.color, p.additive ? 1.0f : fogK);
  switch (p.kind) {
    case Kind::Explosion: {
      const int i = std::min(7, static_cast<int>(t * 8));
      r.spriteScaled(art_->effects.explosion[static_cast<size_t>(i)], p.x, p.y - p.gz - p.h, scale,
                     withAlpha(scaleRgb(kWhite, fogK), alpha));
      break;
    }
    case Kind::Flame: {
      const int i = static_cast<int>(p.age * 14 + p.x) & 3;
      r.spriteScaled(art_->effects.fire[static_cast<size_t>(i)], p.x, p.y - p.gz - p.h, scale, withAlpha(color, alpha));
      break;
    }
    case Kind::Tracer: {
      // A short bright streak travelling from the muzzle to the target.
      // A long glowing streak, about 40 px, fading towards its tail.
      const float y0 = p.y - p.gz - p.h, ye = p.y1 - p.gz1;
      const float len = std::max(1.0f, std::hypot(p.x1 - p.x, ye - y0));
      const float tail = std::min(1.0f, 42.0f / len);
      constexpr int kSteps = 22;
      for (int k = 0; k < kSteps; ++k) {
        const float f = static_cast<float>(k) / (kSteps - 1);  // 0 tail .. 1 head
        const float s = std::clamp(t * (1.0f + tail) - tail * (1.0f - f), 0.0f, 1.0f);
        if (s <= 0.0f) continue;
        const float x = p.x + (p.x1 - p.x) * s, y = y0 + (ye - y0) * s;
        r.spriteScaled(p.frame, x, y, 0.35f + 0.5f * f, withAlpha(color, alpha * (0.15f + 0.85f * f * f)));
      }
      break;
    }
    default:
      r.spriteScaled(p.frame, p.x, p.y - p.gz - p.h, scale, withAlpha(color, alpha));
      break;
  }
}

void BattleFx::drawParticle3D(SpriteRenderer& r, const Particle& p, float fogK) const {
  const float t = std::clamp(p.age / p.life, 0.0f, 1.0f);
  const float alpha = p.alpha0 + (p.alpha1 - p.alpha0) * t;
  const float scale = p.scale0 + (p.scale1 - p.scale0) * t;
  const uint32_t color = scaleRgb(p.color, p.additive ? 1.0f : fogK);
  // A point of the flat plane plus a height in classic pixels, in world tiles.
  auto world = [](float x, float y, float hpx) {
    const Vec2f l = iso::screenToWorld({x, y});
    return Vec3f{l.x / 256.0f, l.y / 256.0f, hpx / iso::HEIGHT_PX_PER_TILE};
  };
  constexpr float kTpp = 1.0f / 45.2548f;  // tiles per sprite pixel: the classic scale
  const Vec3f at = world(p.x, p.y, p.gz + p.h);
  switch (p.kind) {
    case Kind::Explosion: {
      const int i = std::min(7, static_cast<int>(t * 8));
      r.billboard(art_->effects.explosion[static_cast<size_t>(i)], at, kTpp * scale, kWhite,
                  withAlpha(scaleRgb(kWhite, fogK), alpha));
      break;
    }
    case Kind::Flame: {
      const int i = static_cast<int>(p.age * 14 + p.x) & 3;
      r.billboard(art_->effects.fire[static_cast<size_t>(i)], at, kTpp * scale, kWhite, withAlpha(color, alpha));
      break;
    }
    case Kind::Tracer: {
      // A glowing streak about a tile long travelling from the muzzle to the target.
      const Vec3f end = world(p.x1, p.y1, p.gz1);
      const Vec3f d = end - at;
      const float len = std::max(0.01f, d.length());
      const float tail = std::min(1.0f, 0.9f / len);
      const float head = std::clamp(t * (1.0f + tail), 0.0f, 1.0f), back = std::clamp(t * (1.0f + tail) - tail, 0.0f, 1.0f);
      if (head > back) r.streak(p.frame, at + d * back, at + d * head, 0.035f, withAlpha(color, alpha));
      break;
    }
    default:
      r.billboard(p.frame, at, kTpp * scale, kWhite, withAlpha(color, alpha));
      break;
  }
}

void BattleFx::drawAir(SpriteRenderer& r, const RectF& view, const FogAt& fog) const {
  // Smoke and dust first (normal blending), then everything that glows.
  for (int pass = 0; pass < 2; ++pass) {
    r.setAdditive(pass == 1);
    for (const Particle& p : particles_) {
      if (p.additive != (pass == 1)) continue;
      if (!r.in3D() && !view.contains({p.x, p.y - p.gz - p.h})) continue;
      const uint8_t f = fog(p.tx, p.ty);
      if (f < 100) continue;  // out of sight
      if (r.in3D()) drawParticle3D(r, p, fogBrightness(f));
      else drawParticle(r, p, fogBrightness(f));
    }
  }
  r.setAdditive(false);
}

}  // namespace legion

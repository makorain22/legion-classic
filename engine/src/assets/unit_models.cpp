// Slave Engine - the detailed unit models: soldiers and fighting vehicles of
// both armies, built voxel by voxel and baked in 16 facings.
//
// Vehicles: one voxel = 1/36 tile (about 14 cm at true scale). Soldiers: one
// voxel = 1/56 tile. Every model faces +X; the hull sits centred in its grid
// so the grid centre is the unit's position. Turrets are separate models in
// the same grid that the game turns around their ring.
//
// Markings are the plain national cross, the red star and the British
// white-red-white recognition flash only.

#include <algorithm>
#include <cmath>

#include "art_common.hpp"
#include "slave/core/rng.hpp"

namespace slave::art {

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
// A straight round bar between two points (elevated guns, rocket rails).
void bar(VoxelModel& m, float x0, float y0, float z0, float x1, float y1, float z1, float r, uint8_t c) {
  const float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0) + (z1 - z0) * (z1 - z0));
  const int steps = std::max(1, static_cast<int>(len * 2.5f));
  for (int i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / steps;
    const float cx = x0 + (x1 - x0) * t, cy = y0 + (y1 - y0) * t, cz = z0 + (z1 - z0) * t;
    const float rr = std::max(r, 0.5f);
    for (int z = static_cast<int>(cz - rr - 1); z <= static_cast<int>(cz + rr + 1); ++z)
      for (int y = static_cast<int>(cy - rr - 1); y <= static_cast<int>(cy + rr + 1); ++y)
        for (int x = static_cast<int>(cx - rr - 1); x <= static_cast<int>(cx + rr + 1); ++x) {
          const float dx = x + 0.5f - cx, dy = y + 0.5f - cy, dz = z + 0.5f - cz;
          if (dx * dx + dy * dy + dz * dz <= rr * rr) m.set(x, y, z, c);
        }
  }
}

// Cuts away everything outside an ellipse in plan (cast turrets, rounded bustles).
void roundPlan(VoxelModel& m, float cx, float cy, float rx, float ry, int z0, int z1, bool rearOnly = false) {
  for (int z = z0; z < z1; ++z)
    for (int y = 0; y < m.sizeY(); ++y)
      for (int x = 0; x < m.sizeX(); ++x) {
        if (rearOnly && x + 0.5f > cx) continue;
        const float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
        if (dx * dx + dy * dy > 1.0f) m.set(x, y, z, 0);
      }
}

void hollow(VoxelModel& m, int x0, int y0, int z0, int x1, int y1, int z1) { m.box(x0, y0, z0, x1, y1, z1, 0); }

// A crewman's head and shoulders standing in a hatch.
void crewman(VoxelModel& m, const FactionLook& look, float cx, float cy, int z) {
  const int x = static_cast<int>(cx), y = static_cast<int>(cy);
  m.box(x - 1, y - 1, z, x + 2, y + 2, z + 1, UNIFORM_DARK);  // shoulders
  m.box(x, y, z + 1, x + 1, y + 1, z + 3, SKIN);                // head
  m.box(x - 1, y, z + 1, x, y + 1, z + 2, UNIFORM_DARK);
  // German black cap with headphones; Soviet padded leather helmet; British black beret.
  m.box(x, y, z + 2, x + 1, y + 1, z + 3, look.sloped ? LEATHER : MARK_BLACK);
  if (look.british) m.set(x - 1, y, z + 2, MARK_BLACK);  // the beret pulled over to one side
}

void headlight(VoxelModel& m, int x, int y, int z) {
  m.set(x, y, z, STEEL_DARK);
  m.set(x + 1, y, z, LAMP);
}

// Spare track links bolted on as extra armour.
void spareLinks(VoxelModel& m, int x, int y0, int y1, int z0, int z1) {
  for (int y = y0; y < y1; ++y)
    for (int z = z0; z < z1; ++z) m.set(x, y, z, ((y + z) % 3 == 0) ? TRACK_B : TRACK_A);
}

// Pioneer tools along a fender: a jack, a shovel, a pick.
void fenderTools(VoxelModel& m, int x0, int y, int z) {
  m.box(x0, y, z, x0 + 4, y + 1, z + 1, STEEL_DARK);
  m.box(x0 + 6, y, z, x0 + 13, y + 1, z + 1, STOCK);
  m.box(x0 + 13, y, z, x0 + 15, y + 1, z + 1, STEEL);
}

void finish(UnitModel& um, const FactionLook& look, uint32_t seed, int mudHeight, float dirt = 1.0f) {
  camouflage(um.body, look, seed);
  grime(um.body, seed, mudHeight, dirt);
  if (um.hasTurret()) {
    camouflage(um.turret, look, seed);  // same coordinates: the patterns line up
    grime(um.turret, seed + 1, 0, dirt * 0.8f);
  }
}

UnitModel blank(int gx, int gy, int gz, bool turret) {
  UnitModel um;
  um.body = VoxelModel(gx, gy, gz);
  if (turret) um.turret = VoxelModel(gx, gy, gz);
  um.pivotX = gx / 2.0f;
  um.pivotY = gy / 2.0f;
  return um;
}

// ---------------------------------------------------------------------------
// Medium tanks
// ---------------------------------------------------------------------------
// German: a boxy hull with an upright front, side skirts, a slab-sided turret
// with a commander's cupola and a long 75 mm gun.
UnitModel germanMedium(const FactionLook& look, int f) {
  const int GX = 76, GY = 24, GZ = 26;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 19, X1 = 57;  // hull, centred
  for (float y0 : {1.0f, 18.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 5, 9, 8, 1.8f, true, true}, f);
  b.box(X0 + 1, 6, 2, X1 - 1, 18, 9, BASE_DARK);           // lower hull
  armourBox(b, X1 - 7, X1 - 1, 6, 18, 2, 9, -0.35f, 0, 0, BASE_DARK);  // nose plate
  b.box(X0 - 1, 0, 9, X1 + 1, 24, 10, BASE);               // track guards
  armourBox(b, X0 + 1, X1 - 3, 2, 22, 10, 15, 0, 0.3f, 0.05f, BASE);  // superstructure
  b.box(X1 - 4, 3, 10, X1 - 3, 21, 15, BASE_DARK);         // upright driver's plate
  b.box(X1 - 4, 13, 12, X1 - 3, 17, 13, STEEL_DARK);       // driver's visor
  b.box(X1 - 4, 5, 11, X1 - 3, 8, 14, STEEL);              // hull MG ball
  cylX(b, X1 - 3, X1 - 1, 6.5f, 12.5f, 0.5f, STEEL_DARK);
  spareLinks(b, X1 - 2, 4, 20, 9, 11);
  for (int x = X0 + 3; x < X0 + 14; x += 2) b.box(x, 6, 15, x + 1, 18, 16, STEEL_DARK);  // engine grilles
  cylY(b, X0 - 1, 7.5f, 1.3f, 8, 16, EXHAUST);             // muffler
  b.box(X0 - 2, 11, 7, X0 - 1, 13, 9, RUST);
  fenderTools(b, X0 + 6, 1, 10);
  fenderTools(b, X0 + 14, 22, 10);
  headlight(b, X1, 3, 10);
  headlight(b, X1, 20, 10);
  // Side skirts on hangers, one plate lost in battle.
  for (int s = 0; s < 5; ++s) {
    const int x = X0 + 3 + s * 7;
    if (s != 3) b.box(x, 0, 4, x + 6, 1, 10, BASE);
    if (s != 1) b.box(x, 23, 4, x + 6, 24, 10, BASE);
  }
  b.box(X0 + 3, 0, 10, X1 - 3, 1, 11, STEEL_DARK);         // skirt rails
  b.box(X0 + 3, 23, 10, X1 - 3, 24, 11, STEEL_DARK);
  stampSide(b, X0 + 20, 14, false, nationalMark(look, true));
  stampSide(b, X0 + 20, 14, true, nationalMark(look, true));

  // Turret.
  const float tc = 34.0f;
  armourBox(t, tc - 8, tc + 7, 5, 19, 15, 21, 0.25f, 0.15f, 0.35f, BASE);
  t.box(static_cast<int>(tc) - 12, 7, 16, static_cast<int>(tc) - 8, 17, 20, BASE);  // stowage bin
  t.box(static_cast<int>(tc) + 7, 9, 16, static_cast<int>(tc) + 9, 15, 20, BASE_DARK);  // mantlet
  cylX(t, tc + 9, tc + 26, 12, 18, 0.85f, BASE_DARK);       // 75 mm L/48
  cylX(t, tc + 26, tc + 29, 12, 18, 1.3f, BASE_DARK);       // muzzle brake
  t.set(static_cast<int>(tc) + 27, 12, 18, STEEL_DARK);
  cylX(t, tc + 9, tc + 11, 14.5f, 17, 0.45f, STEEL_DARK);   // coaxial MG
  cylinder(t, tc - 4, 15.5f, 2.3f, 21, 22, BASE);           // cupola
  cylinder(t, tc - 4, 15.5f, 1.5f, 22, 23, STEEL_DARK);
  crewman(t, look, tc - 4, 15.5f, 22);
  t.box(static_cast<int>(tc) + 1, 7, 21, static_cast<int>(tc) + 4, 10, 22, BASE_LIGHT);  // loader's hatch
  teamBand(t, 17, 18, 0, static_cast<int>(tc) + 7);
  um.pivotX = tc;
  um.pivotY = 12;
  finish(um, look, 1101, 8);
  return um;
}

// Soviet: sloped plates all round, five big road wheels, a rounded cast
// turret set well forward, fuel drums on the hull sides.
UnitModel sovietMedium(const FactionLook& look, int f) {
  const int GX = 70, GY = 24, GZ = 25;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 15, X1 = 55;
  for (float y0 : {0.0f, 19.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 5, 10, 5, 3.3f, false, false}, f);
  b.box(X0 + 1, 5, 2, X1 - 2, 19, 10, BASE_DARK);
  armourBox(b, X1 - 9, X1 - 3, 5, 19, 3, 10, -0.6f, 0, 0, BASE_DARK);  // lower glacis
  armourBox(b, X0 + 1, X1 + 0.5f, 1, 23, 10, 15, 1.7f, 0.9f, 0.55f, BASE);  // sloped upper hull
  b.box(X1 - 6, 13, 11, X1 - 4, 17, 12, BASE_LIGHT);       // driver's hatch on the glacis
  b.box(X1 - 5, 5, 11, X1 - 4, 8, 13, STEEL);              // hull MG
  cylX(b, X1 - 4, X1 - 2, 6.5f, 12, 0.5f, STEEL_DARK);
  spareLinks(b, X1 - 3, 4, 9, 10, 11);
  spareLinks(b, X1 - 3, 15, 20, 10, 11);
  cylX(b, X0 + 2, X0 + 10, 2.0f, 13.5f, 1.5f, BASE_DARK);   // external fuel drums
  cylX(b, X0 + 2, X0 + 10, 22.0f, 13.5f, 1.5f, BASE_DARK);
  for (int x = X0 + 3; x < X0 + 11; x += 2) b.box(x, 8, 14, x + 1, 16, 15, STEEL_DARK);  // engine louvres
  cylY(b, X0, 9, 0.9f, 7, 9, EXHAUST);
  cylY(b, X0, 9, 0.9f, 15, 17, EXHAUST);
  b.box(X0 + 12, 1, 11, X0 + 22, 2, 12, STEEL_DARK);       // tow cable
  b.box(X0 + 12, 22, 11, X0 + 22, 23, 12, STEEL_DARK);
  headlight(b, X1 - 2, 3, 11);

  const float tc = 37.0f;
  armourBox(t, tc - 9, tc + 7, 4, 20, 15, 21, 0.55f, 0.45f, 0.55f, BASE);
  roundPlan(t, tc - 1, 12, 9.0f, 8.3f, 15, 22);
  t.box(static_cast<int>(tc) + 5, 9, 16, static_cast<int>(tc) + 8, 15, 20, BASE_DARK);  // mantlet
  cylX(t, tc + 8, tc + 12, 12, 18, 1.15f, BASE_DARK);       // gun sleeve
  cylX(t, tc + 12, tc + 24, 12, 18, 0.8f, BASE_DARK);       // 76 mm
  t.box(static_cast<int>(tc) - 6, 7, 21, static_cast<int>(tc) + 1, 17, 22, BASE_LIGHT);  // big hatch
  crewman(t, look, tc - 3, 12, 21);
  stampSide(t, tc - 1, 20, false, nationalMark(look, true));
  stampSide(t, tc - 1, 20, true, nationalMark(look, true));
  teamBand(t, 16, 17, 0, static_cast<int>(tc) + 5);
  um.pivotX = tc;
  um.pivotY = 12;
  finish(um, look, 2202, 9, 1.3f);
  return um;
}

UnitModel mediumTank(const FactionLook& look, int f) { return look.sloped ? sovietMedium(look, f) : germanMedium(look, f); }

// ---------------------------------------------------------------------------
// Light tanks
// ---------------------------------------------------------------------------
UnitModel lightTank(const FactionLook& look, int f) {
  const int GX = 58, GY = 22, GZ = 21;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 14, X1 = 44;
  if (!look.sloped) {
    // A riveted Czech-built chassis: four big road wheels, upright plates.
    for (float y0 : {1.0f, 16.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 5, 8, 4, 2.6f, true, true}, f);
    b.box(X0 + 1, 6, 2, X1 - 1, 16, 8, BASE_DARK);
    b.box(X0, 0, 8, X1, 22, 9, BASE);
    b.box(X0 + 2, 3, 9, X1 - 3, 19, 13, BASE);
    b.box(X1 - 6, 11, 9, X1 - 3, 19, 14, BASE);             // driver's box
    b.box(X1 - 3, 13, 11, X1 - 2, 17, 12, STEEL_DARK);
    b.box(X1 - 3, 4, 10, X1 - 2, 7, 12, STEEL);
    cylX(b, X1 - 2, X1, 5.5f, 11, 0.45f, STEEL_DARK);
    for (int x = X0 + 3; x < X1 - 3; x += 3)                // rivets
      for (int y : {3, 18}) b.set(x, y, 12, STEEL_DARK);
    cylY(b, X0 - 1, 7, 1.0f, 8, 14, EXHAUST);
    fenderTools(b, X0 + 4, 0, 9);
    headlight(b, X1, 2, 9);
    stampSide(b, X0 + 12, 13, false, nationalMark(look, true));
    stampSide(b, X0 + 12, 13, true, nationalMark(look, true));
    const float tc = 30;
    armourBox(t, tc - 5, tc + 5, 5, 17, 13, 18, 0.1f, 0.1f, 0.15f, BASE);
    t.box(static_cast<int>(tc) + 5, 8, 14, static_cast<int>(tc) + 6, 14, 17, BASE_DARK);
    cylX(t, tc + 6, tc + 15, 11, 15.5f, 0.6f, BASE_DARK);   // 37 mm
    cylX(t, tc + 6, tc + 8, 13.5f, 15, 0.4f, STEEL_DARK);
    cylinder(t, tc - 2, 13, 1.8f, 18, 19, BASE);
    crewman(t, look, tc - 2, 13, 18);
    teamBand(t, 15, 16, 0, static_cast<int>(tc) + 5);
    um.pivotX = tc;
  } else {
    // A small, sloped Soviet light tank with a faceted turret.
    for (float y0 : {1.0f, 16.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 5, 7, 5, 2.1f, true, false}, f);
    b.box(X0 + 1, 6, 2, X1 - 1, 16, 8, BASE_DARK);
    armourBox(b, X1 - 7, X1 - 2, 6, 16, 2, 8, -0.5f, 0, 0, BASE_DARK);
    armourBox(b, X0, X1 + 1, 1, 21, 8, 12, 1.5f, 0.6f, 0.4f, BASE);
    b.box(X1 - 5, 12, 9, X1 - 4, 15, 10, BASE_LIGHT);
    cylY(b, X0, 7, 0.8f, 9, 13, EXHAUST);
    b.box(X0 + 4, 1, 9, X0 + 12, 2, 10, STEEL_DARK);
    headlight(b, X1 - 2, 3, 9);
    const float tc = 31;
    armourBox(t, tc - 5, tc + 5, 6, 16, 12, 17, 0.5f, 0.4f, 0.5f, BASE);
    t.box(static_cast<int>(tc) + 3, 9, 13, static_cast<int>(tc) + 6, 13, 16, BASE_DARK);
    cylX(t, tc + 6, tc + 16, 11, 14.5f, 0.6f, BASE_DARK);   // 45 mm
    t.box(static_cast<int>(tc) - 3, 8, 17, static_cast<int>(tc) + 1, 12, 18, BASE_LIGHT);
    crewman(t, look, tc - 1, 12, 17);
    stampSide(t, tc, 16, false, nationalMark(look, true));
    stampSide(t, tc, 16, true, nationalMark(look, true));
    teamBand(t, 13, 14, 0, static_cast<int>(tc) + 3);
    um.pivotX = tc;
  }
  um.pivotY = 11;
  finish(um, look, 3303, 7, look.sloped ? 1.3f : 1.0f);
  return um;
}

// ---------------------------------------------------------------------------
// Heavy tanks
// ---------------------------------------------------------------------------
UnitModel heavyTank(const FactionLook& look, int f) {
  const int GX = 96, GY = 30, GZ = 28;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 25, X1 = 71;
  if (!look.sloped) {
    // The big cat: a slab-sided box on interleaved wheels, the long 88.
    for (float y0 : {1.0f, 23.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 6, 11, 8, 3.4f, false, true}, f);
    // Second row of wheels, set in and between the first.
    for (int i = 0; i < 7; ++i) {
      const float wx = X0 + 9.6f + i * 4.55f;
      for (float y0 : {2.2f, 24.2f}) roadWheel(b, wx, 4.5f, 3.4f, y0, y0 + 3.6f, f);
    }
    b.box(X0 + 1, 7, 2, X1 - 1, 23, 11, BASE_DARK);
    b.box(X0 - 1, 0, 11, X1 + 1, 30, 12, BASE);
    b.box(X0 + 1, 1, 12, X1 - 2, 29, 17, BASE);               // superstructure over the tracks
    armourBox(b, X1 - 6, X1, 7, 23, 3, 11, -0.4f, 0, 0, BASE_DARK);
    b.box(X1 - 2, 15, 14, X1 - 1, 20, 15, STEEL_DARK);        // visor
    b.box(X1 - 2, 6, 13, X1 - 1, 9, 16, STEEL);
    cylX(b, X1 - 1, X1 + 1, 7.5f, 14.5f, 0.5f, STEEL_DARK);
    for (int x = X0 + 3; x < X0 + 15; x += 2) b.box(x, 8, 17, x + 1, 22, 18, STEEL_DARK);
    cylY(b, X0 - 1, 9, 1.4f, 9, 12, EXHAUST);
    cylY(b, X0 - 1, 9, 1.4f, 18, 21, EXHAUST);
    fenderTools(b, X0 + 8, 1, 12);
    fenderTools(b, X0 + 20, 28, 12);
    b.box(X0 + 4, 1, 12, X0 + 30, 2, 13, STEEL_DARK);         // tow cable
    headlight(b, X1, 4, 12);
    headlight(b, X1, 25, 12);
    stampSide(b, X0 + 30, 16, false, nationalMark(look, true));
    stampSide(b, X0 + 30, 16, true, nationalMark(look, true));
    const float tc = 46;
    armourBox(t, tc - 10, tc + 9, 5, 25, 17, 24, 0.05f, 0, 0.12f, BASE);
    roundPlan(t, tc + 1, 15, 11.5f, 10.2f, 17, 25, true);     // horseshoe rear
    t.box(static_cast<int>(tc) - 16, 9, 18, static_cast<int>(tc) - 10, 21, 22, BASE);  // stowage bin
    t.box(static_cast<int>(tc) + 9, 9, 18, static_cast<int>(tc) + 12, 21, 23, BASE_DARK);  // mantlet
    cylX(t, tc + 12, tc + 38, 15, 20.5f, 1.0f, BASE_DARK);    // 88 mm
    cylX(t, tc + 12, tc + 18, 15, 20.5f, 1.3f, BASE_DARK);
    cylX(t, tc + 38, tc + 42, 15, 20.5f, 1.6f, BASE_DARK);    // muzzle brake
    t.box(static_cast<int>(tc) + 39, 14, 20, static_cast<int>(tc) + 41, 16, 21, STEEL_DARK);
    cylinder(t, tc - 4, 20, 2.6f, 24, 26, BASE);              // cupola
    crewman(t, look, tc - 4, 20, 25);
    t.box(static_cast<int>(tc) + 2, 7, 24, static_cast<int>(tc) + 6, 11, 25, BASE_LIGHT);
    teamBand(t, 19, 20, 0, static_cast<int>(tc) + 9);
    um.pivotX = tc;
  } else {
    // The breakthrough tank: long hull, stepped glacis, huge cast turret,
    // a 122 mm gun with a double-baffle brake.
    for (float y0 : {1.0f, 23.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 6, 11, 6, 3.3f, true, false}, f);
    b.box(X0 + 1, 7, 2, X1 - 1, 23, 11, BASE_DARK);
    armourBox(b, X1 - 9, X1 - 2, 7, 23, 3, 11, -0.6f, 0, 0, BASE_DARK);
    b.box(X0 - 1, 0, 11, X1 + 1, 30, 12, BASE);
    armourBox(b, X0 + 1, X1 + 0.5f, 1, 29, 12, 17, 1.2f, 0.7f, 0.3f, BASE);
    b.box(X1 - 5, 14, 13, X1 - 4, 18, 14, STEEL_DARK);
    cylX(b, X0 + 1, X0 + 11, 2.5f, 14.5f, 1.6f, BASE_DARK);   // fuel drums
    cylX(b, X0 + 1, X0 + 11, 27.5f, 14.5f, 1.6f, BASE_DARK);
    for (int x = X0 + 3; x < X0 + 13; x += 2) b.box(x, 9, 17, x + 1, 21, 18, STEEL_DARK);
    cylY(b, X0 - 1, 10, 1.1f, 10, 12, EXHAUST);
    cylY(b, X0 - 1, 10, 1.1f, 18, 20, EXHAUST);
    spareLinks(b, X1 - 3, 5, 12, 12, 13);
    b.box(X0 + 8, 1, 12, X0 + 26, 2, 13, STEEL_DARK);
    headlight(b, X1 - 1, 4, 12);
    const float tc = 49;
    armourBox(t, tc - 11, tc + 9, 4, 26, 17, 24, 0.6f, 0.5f, 0.6f, BASE);
    roundPlan(t, tc - 1, 15, 11.0f, 10.6f, 17, 25);
    t.box(static_cast<int>(tc) + 7, 10, 18, static_cast<int>(tc) + 11, 20, 23, BASE_DARK);
    cylX(t, tc + 11, tc + 16, 15, 20.5f, 1.35f, BASE_DARK);
    cylX(t, tc + 16, tc + 40, 15, 20.5f, 1.05f, BASE_DARK);  // 122 mm
    cylX(t, tc + 40, tc + 42, 15, 20.5f, 1.8f, BASE_DARK);   // double-baffle brake
    cylX(t, tc + 43, tc + 45, 15, 20.5f, 1.8f, BASE_DARK);
    cylX(t, tc + 42, tc + 43, 15, 20.5f, 1.05f, BASE_DARK);
    cylinder(t, tc - 5, 20, 2.4f, 24, 25, BASE);
    crewman(t, look, tc - 5, 20, 25);
    t.box(static_cast<int>(tc) - 6, 8, 24, static_cast<int>(tc) - 2, 12, 25, BASE_LIGHT);
    stampSide(t, tc - 2, 23, false, nationalMark(look, false));
    stampSide(t, tc - 2, 23, true, nationalMark(look, false));
    teamBand(t, 18, 19, 0, static_cast<int>(tc) + 7);
    um.pivotX = tc;
  }
  um.pivotY = 15;
  finish(um, look, 4404, 10, look.sloped ? 1.3f : 1.0f);
  return um;
}

// ---------------------------------------------------------------------------
// Tank destroyers: no turret, the gun in a low casemate.
// ---------------------------------------------------------------------------
UnitModel tankDestroyer(const FactionLook& look, int f) {
  const int GX = 78, GY = 24, GZ = 21;
  UnitModel um = blank(GX, GY, GZ, false);
  VoxelModel& b = um.body;
  if (!look.sloped) {
    const int X0 = 20, X1 = 58;
    for (float y0 : {1.0f, 18.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 5, 9, 8, 1.8f, true, true}, f);
    b.box(X0 + 1, 6, 2, X1 - 1, 18, 9, BASE_DARK);
    armourBox(b, X1 - 8, X1, 6, 18, 2, 9, -0.6f, 0, 0, BASE_DARK);
    b.box(X0 - 1, 0, 9, X1 + 1, 24, 10, BASE);
    armourBox(b, X0 + 1, X1 + 1, 2, 22, 10, 16, 1.6f, 0.5f, 0.55f, BASE);  // low, steeply sloped casemate
    dome(b, X1 - 7, 10, 11, 2.6f, 2.6f, 3.2f, BASE_DARK);     // pig's-head mantlet
    cylX(b, X1 - 6, X1 + 18, 10, 13, 0.85f, BASE_DARK);       // 75 mm L/70
    b.box(X1 - 9, 15, 15, X1 - 6, 19, 16, STEEL_DARK);        // gunner's periscope
    for (int x = X0 + 3; x < X0 + 12; x += 2) b.box(x, 7, 15, x + 1, 17, 16, STEEL_DARK);
    crewman(b, look, X0 + 14, 15, 15);
    cylY(b, X0 - 1, 7.5f, 1.2f, 8, 16, EXHAUST);
    for (int s = 0; s < 5; ++s) {
      const int x = X0 + 3 + s * 7;
      if (s != 2) b.box(x, 0, 4, x + 6, 1, 10, BASE);
      b.box(x, 23, 4, x + 6, 24, 10, BASE);
    }
    stampSide(b, X0 + 14, 14, false, nationalMark(look, true));
    stampSide(b, X0 + 14, 14, true, nationalMark(look, true));
    teamBand(b, 13, 14, X0, X1 - 12);
    finish(um, look, 5505, 8);
  } else {
    const int X0 = 18, X1 = 58;
    for (float y0 : {0.0f, 19.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 5, 10, 5, 3.3f, false, false}, f);
    b.box(X0 + 1, 5, 2, X1 - 2, 19, 10, BASE_DARK);
    armourBox(b, X1 - 9, X1 - 3, 5, 19, 3, 10, -0.6f, 0, 0, BASE_DARK);
    armourBox(b, X0 + 1, X1 + 0.5f, 1, 23, 10, 18, 1.4f, 0.4f, 0.45f, BASE);  // casemate on the hull
    dome(b, X1 - 9, 12, 13, 2.4f, 2.4f, 3.0f, BASE_DARK);
    cylX(b, X1 - 8, X1 + 16, 12, 15, 0.9f, BASE_DARK);        // 85 mm
    cylinder(b, X0 + 16, 17, 2.0f, 18, 19, BASE);             // commander's cupola
    crewman(b, look, X0 + 16, 17, 18);
    cylX(b, X0 + 2, X0 + 10, 2.0f, 13.5f, 1.5f, BASE_DARK);
    cylX(b, X0 + 2, X0 + 10, 22.0f, 13.5f, 1.5f, BASE_DARK);
    cylY(b, X0, 9, 0.9f, 7, 9, EXHAUST);
    cylY(b, X0, 9, 0.9f, 15, 17, EXHAUST);
    stampSide(b, X0 + 22, 16, false, nationalMark(look, true));
    stampSide(b, X0 + 22, 16, true, nationalMark(look, true));
    teamBand(b, 14, 15, X0, X1 - 14);
    finish(um, look, 5606, 9, 1.3f);
  }
  return um;
}

// ---------------------------------------------------------------------------
// Artillery: a self-propelled howitzer, and a truck with a rocket rack.
// ---------------------------------------------------------------------------
void truckWheel(VoxelModel& m, float cx, float r, float y0, float y1, int f) {
  roadWheel(m, cx, r, r, y0, y1, f, STEEL_DARK);
}

UnitModel artillery(const FactionLook& look, int f) {
  if (!look.sloped) {
    const int GX = 66, GY = 22, GZ = 22;
    UnitModel um = blank(GX, GY, GZ, false);
    VoxelModel& b = um.body;
    const int X0 = 18, X1 = 48;
    for (float y0 : {1.0f, 16.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 5, 8, 5, 2.4f, true, true}, f);
    b.box(X0 + 1, 6, 2, X1 - 1, 16, 8, BASE_DARK);
    b.box(X0, 0, 8, X1, 22, 9, BASE);
    armourBox(b, X1 - 12, X1 + 1, 3, 19, 9, 12, 1.2f, 0, 0.2f, BASE);  // engine deck in front
    // The open fighting compartment at the back.
    b.box(X0, 2, 9, X0 + 18, 20, 15, BASE);
    hollow(b, X0 + 1, 3, 10, X0 + 17, 19, 15);
    b.box(X0 + 1, 3, 9, X0 + 17, 19, 10, BASE_DARK);
    // 105 mm howitzer, elevated.
    b.box(X0 + 8, 8, 10, X0 + 14, 14, 14, STEEL_DARK);         // cradle
    bar(b, X0 + 12, 11, 13.5f, X0 + 40, 11, 19.5f, 1.15f, BASE_DARK);
    bar(b, X0 + 39, 11, 19.3f, X0 + 42, 11, 19.9f, 1.6f, BASE_DARK);  // muzzle brake
    b.box(X0 + 15, 6, 12, X0 + 16, 16, 17, BASE);             // gun shield
    crewman(b, look, X0 + 5, 6, 11);
    crewman(b, look, X0 + 4, 15, 11);
    b.box(X0 + 2, 15, 10, X0 + 6, 18, 12, WOOD);              // ammunition boxes
    cylY(b, X0 - 1, 6, 1.0f, 8, 14, EXHAUST);
    headlight(b, X1, 2, 9);
    stampSide(b, X0 + 9, 14, false, nationalMark(look, true));
    stampSide(b, X0 + 9, 14, true, nationalMark(look, true));
    teamBand(b, 12, 13, X0, X0 + 18);
    finish(um, look, 6707, 7);
    return um;
  }
  // A six-wheeled truck with a rack of rocket rails over the bed.
  const int GX = 60, GY = 20, GZ = 22;
  UnitModel um = blank(GX, GY, GZ, false);
  VoxelModel& b = um.body;
  const int X0 = 10, X1 = 50;
  for (float wx : {X0 + 5.0f, X0 + 11.0f, X1 - 6.0f})
    for (float y0 : {0.0f, 17.0f}) truckWheel(b, wx, 2.8f, y0, y0 + 3, f);
  b.box(X0, 3, 3, X1 - 2, 17, 5, STEEL_DARK);                 // chassis
  b.box(X1 - 15, 2, 5, X1 - 8, 18, 12, BASE);                 // cab
  b.box(X1 - 14, 2, 12, X1 - 8, 18, 13, BASE_DARK);
  b.box(X1 - 9, 3, 8, X1 - 8, 17, 11, GLASS);                 // windscreen
  armourBox(b, X1 - 8, X1, 5, 15, 5, 10, 0.4f, 0, 0.3f, BASE);  // bonnet
  b.box(X1 - 1, 6, 5, X1, 14, 9, STEEL_DARK);                  // radiator
  b.box(X1 - 9, 1, 5, X1 - 1, 3, 6, BASE_DARK);                // mudguards
  b.box(X1 - 9, 17, 5, X1 - 1, 19, 6, BASE_DARK);
  // Launcher: a frame raised at the front, eight rails with rockets.
  b.box(X0 + 2, 5, 5, X0 + 4, 15, 9, STEEL_DARK);             // rear mount
  b.box(X1 - 18, 5, 5, X1 - 16, 15, 13, STEEL_DARK);          // front prop
  for (int r = 0; r < 4; ++r) {
    const float y = 6.0f + r * 2.6f;
    bar(b, X0 + 1, y, 10.0f, X1 - 12, y, 15.5f, 0.55f, STEEL_DARK);  // rail
    bar(b, X0 + 2, y, 11.1f, X1 - 14, y, 16.5f, 0.7f, STEEL);        // rocket on top
    bar(b, X0 + 2, y, 9.0f, X1 - 14, y, 14.5f, 0.7f, STEEL);         // and one beneath
  }
  b.box(X1 - 16, 2, 13, X1 - 9, 18, 14, CANVAS_DARK);         // tarpaulin rolled on the cab roof
  stampSide(b, X1 - 12, 11, false, nationalMark(look, true));
  stampSide(b, X1 - 12, 11, true, nationalMark(look, true));
  teamBand(b, 12, 13, X1 - 15, X1 - 8);
  finish(um, look, 6808, 6, 1.3f);
  return um;
}

// ---------------------------------------------------------------------------
// Half-tracks: wheels in front, tracks behind, an MG on a pivot (the turret).
// ---------------------------------------------------------------------------
UnitModel halftrack(const FactionLook& look, int f) {
  const int GX = 56, GY = 22, GZ = 20;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 8, X1 = 48;
  for (float y0 : {1.0f, 16.0f}) tracks(b, {X0 + 0.0f, X0 + 25.0f, y0, y0 + 5, 7, 6, 1.9f, false, true}, f);
  for (float y0 : {1.0f, 18.0f}) truckWheel(b, X1 - 5, 2.6f, y0, y0 + 3, f);
  if (!look.sloped) {
    armourBox(b, X0 + 1, X1 - 8, 4.5f, 17.5f, 3, 9, 0, 0, -0.45f, BASE);  // lower hull flares out
    armourBox(b, X0 + 1, X1 - 8, 1.8f, 20.2f, 9, 13, 0.0f, 0, 0.4f, BASE);  // upper hull leans in
    armourBox(b, X1 - 9, X1 + 1, 5, 17, 4, 11, 0.9f, 0, 0.45f, BASE);       // angled engine cover
  } else {
    b.box(X0 + 1, 3, 3, X1 - 8, 19, 13, BASE);                               // boxy armoured body
    b.box(X1 - 9, 5, 4, X1, 17, 11, BASE);
    for (int y = 6; y < 16; y += 2) b.box(X1, y, 5, X1 + 1, y + 1, 10, STEEL_DARK);  // radiator louvres
    cylY(b, X1 + 1.5f, 5, 1.2f, 4, 18, STEEL_DARK);                           // unditching roller
  }
  hollow(b, X0 + 2, 4, 9, X1 - 10, 18, 14);                     // open troop compartment
  b.box(X0 + 2, 4, 8, X1 - 10, 18, 9, BASE_DARK);
  b.box(X1 - 6, 0, 8, X1 - 1, 4, 9, BASE_DARK);                 // front mudguards
  b.box(X1 - 6, 18, 8, X1 - 1, 22, 9, BASE_DARK);
  b.box(X0 - 1, 0, 7, X0 + 25, 1, 8, BASE_DARK);                // track guards / stowage bins
  b.box(X0 - 1, 21, 7, X0 + 25, 22, 8, BASE_DARK);
  b.box(X1 - 11, 7, 12, X1 - 10, 15, 13, STEEL_DARK);           // driver's visor slit
  for (int i = 0; i < 3; ++i) crewman(b, look, X0 + 6 + i * 6, i % 2 ? 7 : 14, 9);
  headlight(b, X1, 4, 10);
  headlight(b, X1, 17, 10);
  stampSide(b, X0 + 14, 12, false, nationalMark(look, true));
  stampSide(b, X0 + 14, 12, true, nationalMark(look, true));
  teamBand(b, 11, 12, X0 + 1, X1 - 10);
  // MG and shield on the pivot at the front of the compartment.
  const float pc = X1 - 12.0f;
  t.box(static_cast<int>(pc) + 1, 8, 13, static_cast<int>(pc) + 2, 14, 16, BASE);
  cylX(t, pc - 1, pc + 7, 11, 14.5f, 0.45f, STEEL_DARK);
  crewman(t, look, pc - 2, 11, 12);
  um.pivotX = pc;
  um.pivotY = 11;
  finish(um, look, 7909, 7);
  return um;
}

// ---------------------------------------------------------------------------
// Scout cars: four wheels, an open turret with a machine gun.
// ---------------------------------------------------------------------------
UnitModel scoutCar(const FactionLook& look, int f) {
  const int GX = 46, GY = 18, GZ = 17;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 7, X1 = 39;
  for (float wx : {X0 + 6.0f, X1 - 7.0f})
    for (float y0 : {0.0f, 15.0f}) truckWheel(b, wx, 2.6f, y0, y0 + 3, f);
  if (!look.sloped) {
    armourBox(b, X0, X1, 3.5f, 14.5f, 3, 8, -0.5f, -0.4f, -0.4f, BASE);   // faceted lower body
    armourBox(b, X0 + 2, X1 - 2, 1.5f, 16.5f, 8, 12, 0.9f, 0.6f, 0.5f, BASE);
  } else {
    armourBox(b, X0 + 2, X1, 3, 15, 3, 7, -0.3f, -0.3f, -0.3f, BASE);
    armourBox(b, X0 + 1, X1 + 1, 2, 16, 7, 12, 1.4f, 1.1f, 0.6f, BASE);  // steeply sloped plates
  }
  b.box(X0 + 2, 0, 6, X0 + 10, 3, 7, BASE_DARK);                 // mudguards
  b.box(X0 + 2, 15, 6, X0 + 10, 18, 7, BASE_DARK);
  b.box(X1 - 11, 0, 6, X1 - 3, 3, 7, BASE_DARK);
  b.box(X1 - 11, 15, 6, X1 - 3, 18, 7, BASE_DARK);
  b.box(X1 - 7, 6, 10, X1 - 6, 12, 11, STEEL_DARK);             // driver's slit
  headlight(b, X1 - 1, 3, 7);
  headlight(b, X1 - 1, 14, 7);
  stampSide(b, X0 + 8, 10, false, nationalMark(look, true));
  stampSide(b, X0 + 8, 10, true, nationalMark(look, true));
  const float tc = 21;
  if (!look.sloped) {
    armourBox(t, tc - 5, tc + 5, 4, 14, 12, 15, 0.35f, 0.35f, 0.35f, BASE);
    roundPlan(t, tc, 9, 5.2f, 5.2f, 12, 15);
  } else {
    cylinder(t, tc, 9, 4.0f, 12, 15, BASE);
  }
  hollow(t, static_cast<int>(tc) - 3, 6, 13, static_cast<int>(tc) + 3, 12, 15);
  cylX(t, tc, tc + 10, 9, 14, 0.5f, STEEL_DARK);
  crewman(t, look, tc - 1, 9, 13);
  teamBand(t, 12, 13);
  um.pivotX = tc;
  um.pivotY = 9;
  finish(um, look, 8010, 6);
  return um;
}

// ---------------------------------------------------------------------------
// Supply trucks (an oil tank on the bed) and construction trucks.
// ---------------------------------------------------------------------------
void truckCab(VoxelModel& b, const FactionLook& look, int X1, int GY) {
  const int y0 = 2, y1 = GY - 2;
  // Soviet trucks of the time had wooden cabs; German ones steel.
  const uint8_t cab = look.sloped ? WOOD : BASE;
  b.box(X1 - 14, y0, 5, X1 - 8, y1, 12, cab);
  b.box(X1 - 14, y0, 12, X1 - 8, y1, 13, look.sloped ? WOOD_DARK : BASE_DARK);
  b.box(X1 - 9, y0 + 1, 8, X1 - 8, y1 - 1, 11, GLASS);
  b.box(X1 - 14, y0, 8, X1 - 13, y0 + 1, 11, GLASS);
  armourBox(b, X1 - 8, X1, y0 + 2.5f, y1 - 2.5f, 5, 10, 0.3f, 0, 0.2f, BASE);  // bonnet
  b.box(X1 - 1, y0 + 4, 5, X1, y1 - 4, 9, STEEL_DARK);
  // Rounded front mudguards.
  for (int yy : {y0 - 2, y1}) {
    b.box(X1 - 8, yy, 6, X1 - 1, yy + 2, 7, BASE_DARK);
    b.box(X1 - 7, yy, 7, X1 - 2, yy + 2, 8, BASE_DARK);
  }
  headlight(b, X1, y0 + 1, 8);
  headlight(b, X1, y1 - 2, 8);
}

UnitModel supplyTruck(const FactionLook& look, int f) {
  const int GX = 50, GY = 18, GZ = 17;
  UnitModel um = blank(GX, GY, GZ, false);
  VoxelModel& b = um.body;
  const int X0 = 6, X1 = 44;
  for (float wx : {X0 + 5.0f, X0 + 11.0f, X1 - 6.0f})
    for (float y0 : {0.0f, 15.0f}) truckWheel(b, wx, 2.7f, y0, y0 + 3, f);
  b.box(X0, 3, 3, X1 - 2, 15, 5, STEEL_DARK);
  truckCab(b, look, X1, GY);
  // The tank: a rounded drum along the bed, with a walkway and filler caps.
  cylX(b, X0, X1 - 15, 9, 9.5f, 4.6f, BASE_DARK);
  b.box(X0 + 2, 7, 14, X1 - 17, 11, 15, STEEL_DARK);
  for (int x : {X0 + 6, X0 + 16}) cylinder(b, x, 9, 1.2f, 14, 16, STEEL);
  for (int x : {X0 + 3, X0 + 13, X0 + 22}) b.box(x, 4, 4, x + 1, 14, 14, STEEL_DARK);  // straps
  b.box(X0 - 1, 7, 6, X0, 11, 9, STEEL);                       // valve
  teamBand(b, 6, 7, X0, X1 - 15);
  stampSide(b, X1 - 11, 11, false, nationalMark(look, true));
  stampSide(b, X1 - 11, 11, true, nationalMark(look, true));
  finish(um, look, 9111, 5, 1.2f);
  return um;
}

UnitModel constructionTruck(const FactionLook& look, int f) {
  const int GX = 62, GY = 22, GZ = 22;
  UnitModel um = blank(GX, GY, GZ, false);
  VoxelModel& b = um.body;
  const int X0 = 6, X1 = 56;
  for (float wx : {X0 + 6.0f, X0 + 13.0f, X0 + 22.0f, X1 - 7.0f})
    for (float y0 : {0.0f, 19.0f}) truckWheel(b, wx, 3.0f, y0, y0 + 3, f);
  b.box(X0, 3, 3, X1 - 2, 19, 6, STEEL_DARK);
  truckCab(b, look, X1, GY);
  // A folded workshop under canvas, with a crane on top.
  b.box(X0, 2, 6, X1 - 15, 20, 15, CANVAS);
  b.box(X0, 2, 6, X1 - 15, 20, 7, CANVAS_DARK);
  for (int x = X0 + 2; x < X1 - 15; x += 6) b.box(x, 2, 7, x + 1, 20, 15, CANVAS_DARK);  // ribs
  b.box(X0 + 4, 9, 15, X0 + 7, 13, 18, STEEL_DARK);            // crane base
  b.box(X0 + 5, 10, 18, X1 - 16, 12, 19, STEEL);               // jib folded forward
  b.box(X1 - 18, 10, 15, X1 - 17, 12, 18, STEEL_DARK);
  b.box(X0 - 1, 4, 4, X0, 18, 6, STEEL_DARK);                   // outrigger
  teamBand(b, 9, 10, X0, X1 - 15);
  finish(um, look, 9212, 6, 1.2f);
  return um;
}

// ---------------------------------------------------------------------------
// Infantry
// ---------------------------------------------------------------------------
UnitModel single(const FactionLook& look, int f, Kit kit, bool cap = false, bool smock = false) {
  UnitModel um;
  um.body = VoxelModel(20, 12, 21);
  SoldierSpec s;
  s.kit = kit;
  s.frame = f;
  s.cap = cap;
  s.smock = smock;
  soldier(um.body, look, 10, 6, s);
  return um;
}

UnitModel rifleman(const FactionLook& look, int f) { return single(look, f, Kit::Rifle); }
UnitModel sniper(const FactionLook& look, int f) { return single(look, f, Kit::ScopedRifle, true, true); }
UnitModel engineer(const FactionLook& look, int f) { return single(look, f, Kit::Tools); }

UnitModel team(const FactionLook& look, int f, Kit lead, Kit second) {
  UnitModel um;
  um.body = VoxelModel(24, 20, 21);
  SoldierSpec a;
  a.kit = lead;
  a.frame = f;
  soldier(um.body, look, 13, 6, a);
  SoldierSpec b;
  b.kit = second;
  b.frame = (f + 2) % 4;  // out of step
  soldier(um.body, look, 9, 14, b);
  return um;
}

UnitModel mgTeam(const FactionLook& look, int f) { return team(look, f, Kit::MachineGun, Kit::Ammo); }
UnitModel atTeam(const FactionLook& look, int f) {
  return team(look, f, look.sloped ? Kit::AtRifle : Kit::RocketTube, Kit::Rifle);
}

VoxelModel corpseOne(const FactionLook& look, int) {
  VoxelModel m(22, 12, 6);
  SoldierSpec s;
  s.pose = Pose::Dead;
  soldier(m, look, 11, 6, s);
  return m;
}

VoxelModel corpseTwo(const FactionLook& look, int) {
  VoxelModel m(24, 20, 6);
  SoldierSpec s;
  s.pose = Pose::Dead;
  soldier(m, look, 12, 6, s);
  s.kit = Kit::Ammo;
  soldier(m, look, 10, 14, s);
  return m;
}

}  // namespace

// ---------------------------------------------------------------------------
// Soldiers. One voxel = 1/56 tile; a man stands 18 voxels tall.
// ---------------------------------------------------------------------------
void soldier(VoxelModel& m, const FactionLook& look, float ox, float oy, const SoldierSpec& s) {
  const int X = static_cast<int>(std::floor(ox)) - 1;  // body spans X..X+3 front to back
  const int Y = static_cast<int>(std::floor(oy)) - 2;  // and Y..Y+4 side to side
  const uint8_t tunic = UNIFORM, trousers = UNIFORM_DARK;
  const bool sov = look.sloped;

  if (s.pose == Pose::Dead || s.pose == Pose::Prone) {
    // Lying along X, head toward +X.
    const int z = 0;
    m.box(X - 6, Y + 1, z, X - 1, Y + 3, z + 1, trousers);     // legs
    m.box(X - 7, Y + 1, z, X - 6, Y + 3, z + 2, BOOT);
    m.box(X - 1, Y, z, X + 4, Y + 4, z + 2, tunic);             // torso
    m.box(X, Y + 1, z + 2, X + 3, Y + 3, z + 3, TEAM_DARK);     // pack
    m.box(X + 4, Y + 1, z, X + 6, Y + 3, z + 2, SKIN);          // head
    if (s.pose == Pose::Dead) {
      m.box(X + 7, Y + 3, z, X + 9, Y + 5, z + 1, sov ? HELMET : HELMET);  // helmet rolled away
      m.box(X - 2, Y + 5, z, X + 7, Y + 6, z + 1, STOCK);       // dropped rifle
      m.box(X + 1, Y - 1, z, X + 3, Y, z + 1, SKIN);            // flung arm
    } else {
      m.box(X + 4, Y + 1, z + 2, X + 6, Y + 3, z + 3, HELMET);
      m.box(X + 6, Y + 2, z + 1, X + 12, Y + 3, z + 2, STEEL_DARK);
    }
    return;
  }

  // Walking: legs swing, the body bobs at mid-stride.
  static const int kStride[4] = {0, 2, 0, -2};
  const int st = kStride[s.frame & 3];
  const int bob = (st == 0) ? 1 : 0;
  // Legs.
  for (int side = 0; side < 2; ++side) {
    const int ly = Y + side * 2;
    const int sw = side == 0 ? st : -st;
    m.box(X + 1 + sw, ly, 0, X + 3 + sw, ly + 2, 2, BOOT);                  // boots
    m.box(X + 1 + sw, ly, 2, X + 3 + sw, ly + 2, 5, trousers);              // shins
    m.box(X + 1 + sw / 2, ly, 5, X + 3 + sw / 2, ly + 2, 8 + bob, trousers);  // thighs
  }
  const int zt = 8 + bob;  // waist
  // Torso and kit.
  m.box(X, Y, zt, X + 3, Y + 4, zt + 6, tunic);
  if (s.smock) speckle(m, tunic, CAMO_A, CAMO_B, 4141u, 30, 22);
  m.box(X, Y, zt, X + 3, Y + 4, zt + 1, LEATHER);                // belt
  m.box(X + 2, Y, zt + 1, X + 3, Y + 1, zt + 2, LEATHER);        // ammunition pouches
  m.box(X + 2, Y + 3, zt + 1, X + 3, Y + 4, zt + 2, LEATHER);
  m.set(X + 2, Y + 1, zt + 3, WEBBING);                          // braces
  m.set(X + 2, Y + 2, zt + 4, WEBBING);
  // Pack and rolled blanket in team colour: the side reads from above.
  m.box(X - 1, Y + 1, zt + 2, X, Y + 3, zt + 5, TEAM_DARK);
  m.box(X - 1, Y, zt + 5, X + 1, Y + 4, zt + 6, TEAM);
  m.set(X - 1, Y + 3, zt + 1, STOCK);                            // entrenching tool
  // Head and helmet.
  const int zh = zt + 6;
  m.box(X + 1, Y + 1, zh, X + 3, Y + 3, zh + 3, SKIN);
  m.set(X + 1, Y + 1, zh + 1, SKIN);
  if (s.cap) {
    m.box(X + 1, Y + 1, zh + 2, X + 3, Y + 3, zh + 3, CAP);
    m.box(X + 3, Y + 1, zh + 2, X + 4, Y + 3, zh + 3, CAP);      // peak
  } else if (look.british) {
    m.box(X - 1, Y, zh + 2, X + 5, Y + 4, zh + 3, HELMET);       // the wide flat brim of the Brodie helmet
    m.box(X, Y - 1, zh + 2, X + 4, Y + 5, zh + 3, HELMET);
    m.box(X - 1, Y, zh + 2, X, Y + 4, zh + 3, HELMET_DARK);      // brim edge in shade
    m.box(X + 1, Y + 1, zh + 3, X + 3, Y + 3, zh + 4, HELMET);   // shallow crown
  } else if (sov) {
    m.box(X, Y + 1, zh + 2, X + 4, Y + 3, zh + 3, HELMET);       // round helmet
    m.box(X + 1, Y, zh + 2, X + 3, Y + 4, zh + 3, HELMET);
    m.box(X + 1, Y + 1, zh + 3, X + 3, Y + 3, zh + 4, HELMET);
  } else {
    m.box(X, Y, zh + 2, X + 4, Y + 4, zh + 3, HELMET);           // flared helmet
    m.box(X, Y, zh + 2, X + 1, Y + 4, zh + 3, HELMET_DARK);      // deep neck guard
    m.box(X + 1, Y + 1, zh + 3, X + 3, Y + 3, zh + 4, HELMET);
  }
  // Arms and the weapon.
  const int za = zt + 3;  // hands
  auto arms = [&](int fwd) {
    m.box(X + 1, Y - 1, za, X + 2, Y, zt + 6, tunic);
    m.box(X + 1, Y + 4, za, X + 2, Y + 5, zt + 6, tunic);
    m.box(X + 2, Y - 1, za, X + 2 + fwd, Y, za + 1, tunic);
    m.box(X + 2, Y + 4, za, X + 2 + fwd, Y + 5, za + 1, tunic);
    m.set(X + 2 + fwd, Y - 1, za, SKIN);
    m.set(X + 2 + fwd, Y + 4, za, SKIN);
  };
  switch (s.kit) {
    case Kit::Rifle:
    case Kit::ScopedRifle:
      arms(2);
      m.box(X - 1, Y + 3, za + 1, X + 6, Y + 4, za + 2, STOCK);  // rifle held across the body
      m.box(X + 6, Y + 3, za + 1, X + 9, Y + 4, za + 2, STEEL_DARK);
      if (s.kit == Kit::ScopedRifle) m.box(X + 3, Y + 3, za + 2, X + 6, Y + 4, za + 3, STEEL_DARK);
      break;
    case Kit::Smg:
      arms(2);
      m.box(X + 1, Y + 2, za + 1, X + 7, Y + 3, za + 2, STEEL_DARK);
      break;
    case Kit::MachineGun:
      arms(1);
      // MG carried over the right shoulder.
      m.box(X - 2, Y + 4, zt + 6, X + 9, Y + 5, zt + 7, STEEL_DARK);
      m.box(X - 3, Y + 4, zt + 5, X - 1, Y + 5, zt + 7, STOCK);
      m.set(X + 6, Y + 4, zt + 5, STEEL_DARK);  // bipod legs
      break;
    case Kit::RocketTube:
      arms(2);
      // Rocket launcher on the shoulder, with its blast shield.
      cylX(m, X - 6, X + 8, Y + 4.5f, zt + 6.5f, 0.85f, BASE_DARK);
      m.box(X + 3, Y + 3, zt + 5, X + 4, Y + 6, zt + 9, BASE_DARK);
      break;
    case Kit::AtRifle:
      arms(1);
      // A very long anti-tank rifle over the shoulder.
      m.box(X - 3, Y + 4, zt + 6, X + 13, Y + 5, zt + 7, STEEL_DARK);
      m.box(X - 3, Y + 4, zt + 5, X + 1, Y + 5, zt + 6, STOCK);
      m.set(X + 12, Y + 4, zt + 7, STEEL);
      break;
    case Kit::Tools:
      arms(1);
      m.box(X + 1, Y + 5, za - 3, X + 4, Y + 7, za, WOOD);       // tool chest in the right hand
      m.box(X + 2, Y + 5, za, X + 3, Y + 7, za + 1, STEEL_DARK);
      m.box(X - 2, Y + 1, zt + 1, X - 1, Y + 2, zt + 8, STOCK);  // pick handle on the pack
      m.box(X - 2, Y, zt + 7, X - 1, Y + 4, zt + 8, STEEL);
      break;
    case Kit::Ammo:
      arms(1);
      m.box(X + 1, Y + 5, za - 3, X + 3, Y + 6, za, BASE_DARK);  // ammunition boxes
      m.box(X + 1, Y - 2, za - 3, X + 3, Y - 1, za, BASE_DARK);
      m.box(X - 1, Y + 3, za + 1, X + 5, Y + 4, za + 2, STOCK);  // slung carbine
      break;
  }
}

void addUnitModels(BakeQueue& q, SpriteCatalog& cat) {
  addUnit(q, cat, "rifleman", &rifleman, 4, &corpseOne, kInfantryVoxel);
  addUnit(q, cat, "mg_team", &mgTeam, 4, &corpseTwo, kInfantryVoxel);
  addUnit(q, cat, "at_team", &atTeam, 4, &corpseTwo, kInfantryVoxel);
  addUnit(q, cat, "engineer", &engineer, 4, &corpseOne, kInfantryVoxel);
  addUnit(q, cat, "sniper", &sniper, 4, &corpseOne, kInfantryVoxel);
  addUnit(q, cat, "scoutcar", &scoutCar, 3);
  addUnit(q, cat, "halftrack", &halftrack, 4);
  addUnit(q, cat, "light_tank", &lightTank, 4);
  addUnit(q, cat, "tank", &mediumTank, 4);
  addUnit(q, cat, "heavy_tank", &heavyTank, 4);
  addUnit(q, cat, "tank_destroyer", &tankDestroyer, 4);
  addUnit(q, cat, "artillery", &artillery, 4);
  addUnit(q, cat, "truck", &supplyTruck, 3);
  addUnit(q, cat, "mcv", &constructionTruck, 3);
}

}  // namespace slave::art

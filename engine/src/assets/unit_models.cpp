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

UnitModel britishCrusader(const FactionLook&, int);
UnitModel britishChurchill(const FactionLook&, int);
UnitModel britishAchilles(const FactionLook&, int);
UnitModel britishSexton(const FactionLook&, int);
UnitModel britishCarrier(const FactionLook&, int);
UnitModel britishDingo(const FactionLook&, int);
UnitModel britishLorry(const FactionLook&, int);
UnitModel britishWorkshop(const FactionLook&, int);

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

UnitModel britishCromwell(const FactionLook& look, int f);
UnitModel mediumTank(const FactionLook& look, int f) {
  if (look.british) return britishCromwell(look, f);
  return look.sloped ? sovietMedium(look, f) : germanMedium(look, f);
}

// ---------------------------------------------------------------------------
// Light tanks
// ---------------------------------------------------------------------------
UnitModel lightTank(const FactionLook& look, int f) {
  if (look.british) return britishCrusader(look, f);
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
  if (look.british) return britishChurchill(look, f);
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
  if (look.british) return britishAchilles(look, f);
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
  if (look.british) return britishSexton(look, f);
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
  if (look.british) return britishCarrier(look, f);
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
  if (look.british) return britishDingo(look, f);
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
  if (look.british) return britishLorry(look, f);
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
  if (look.british) return britishWorkshop(look, f);
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


// ---------------------------------------------------------------------------
// British vehicles: Daimler Dingo, Universal Carrier, Crusader, Cromwell,
// Sherman Firefly, Achilles, Sexton, Churchill and Bedford lorries. Plain
// service-drab paint, the white-red-white recognition flash, black berets.
// ---------------------------------------------------------------------------
// American-pattern running gear (Sherman, M10, Sexton): three bogies a side,
// two small wheels each under a sprung bracket, a raised front sprocket.
void vvss(VoxelModel& b, float x0, float x1, int f) {
  for (float y0 : {0.0f, 21.0f}) {
    tracks(b, {x0, x1, y0, y0 + 5, 9, 6, 2.0f, true, true}, f);
    const float step = (x1 - x0 - 6) / 3.0f;
    for (int k = 0; k < 3; ++k) {
      const int bx = static_cast<int>(x0 + 3 + step * k + step * 0.5f);
      const int yo = y0 < 1 ? 0 : static_cast<int>(y0) + 4;
      b.box(bx - 3, yo, 3, bx + 3, yo + 1, 6, STEEL_DARK);     // bogie bracket
      b.box(bx - 1, yo, 6, bx + 1, yo + 1, 8, STEEL_DARK);     // volute spring
    }
  }
}

// The cast, rounded hull front of the Sherman family.
void shermanNose(VoxelModel& b, int X0, int X1) {
  b.box(X0 + 1, 5, 2, X1 - 3, 21, 9, BASE_DARK);
  for (int z = 2; z < 8; ++z) {                               // final-drive housing, round in section
    const float dz = (z - 4.5f) / 3.5f;
    const int out = static_cast<int>(std::sqrt(std::max(0.0f, 1.0f - dz * dz)) * 4.0f);
    b.box(X1 - 4, 4, z, X1 - 3 + out, 22, z + 1, BASE_DARK);
  }
  for (int y = 6; y < 21; y += 4) b.box(X1, y, 4, X1 + 1, y + 1, 6, STEEL_DARK);  // housing bolts
}

UnitModel britishDingo(const FactionLook& look, int f) {
  const int GX = 42, GY = 18, GZ = 15;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 8, X1 = 34;
  for (float wx : {X0 + 5.0f, X1 - 6.0f})
    for (float y0 : {0.0f, 15.0f}) truckWheel(b, wx, 2.5f, y0, y0 + 3, f);
  armourBox(b, X0, X1, 3, 15, 3, 7, -0.35f, -0.3f, -0.25f, BASE);
  armourBox(b, X0 + 1, X1, 3, 15, 7, 10, 1.8f, 0.7f, 0.35f, BASE);   // low, steeply sloped deck
  hollow(b, X0 + 9, 5, 8, X1 - 9, 13, 10);                           // the open crew compartment
  b.box(X0 + 9, 5, 7, X1 - 9, 13, 8, BASE_DARK);
  b.box(X0 + 6, 3, 9, X0 + 9, 15, 10, CANVAS_DARK);                  // folded sliding roof
  b.box(X1 - 9, 6, 9, X1 - 8, 12, 10, STEEL_DARK);                  // driver's visor flap
  for (int y0 : {0, 15}) {                                           // flat mudguards
    b.box(X0 + 1, y0, 6, X0 + 10, y0 + 3, 7, BASE_DARK);
    b.box(X1 - 11, y0, 6, X1 - 1, y0 + 3, 7, BASE_DARK);
  }
  cylY(b, X0 + 16, 6.5f, 1.8f, 15, 16.6f, RUBBER);                   // spare wheel on the side
  b.box(X0 - 1, 6, 4, X0, 12, 7, STEEL_DARK);                        // engine grille at the back
  headlight(b, X1 - 1, 4, 7);
  headlight(b, X1 - 1, 13, 7);
  stampSide(b, X0 + 7, 8, false, nationalMark(look, true));
  stampSide(b, X0 + 7, 8, true, nationalMark(look, true));
  // A Bren on a pintle, the commander behind it.
  const float pc = X0 + 18.0f;
  t.box(static_cast<int>(pc), 8, 9, static_cast<int>(pc) + 1, 10, 11, STEEL_DARK);
  cylX(t, pc - 1, pc + 6, 9, 11.5f, 0.45f, STEEL_DARK);
  t.box(static_cast<int>(pc) + 1, 9, 12, static_cast<int>(pc) + 2, 10, 13, STEEL_DARK);  // curved magazine
  crewman(t, look, pc - 2, 9, 9);
  teamBand(t, 9, 10);
  teamBand(b, 7, 8, X0, X1 - 12);
  um.pivotX = pc;
  um.pivotY = 9;
  finish(um, look, 8111, 6);
  return um;
}

UnitModel britishCarrier(const FactionLook& look, int f) {
  const int GX = 48, GY = 22, GZ = 15;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 8, X1 = 39;
  for (float y0 : {1.0f, 16.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 5, 6, 3, 2.1f, false, false}, f);
  // A small open steel box: crew front and back, the engine down the middle.
  b.box(X0 + 1, 4, 2, X1 - 1, 18, 9, BASE);
  armourBox(b, X1 - 7, X1, 4, 18, 2, 9, 0.5f, 0, 0, BASE);
  hollow(b, X0 + 2, 5, 4, X1 - 6, 17, 9);
  b.box(X0 + 2, 5, 3, X1 - 6, 17, 4, BASE_DARK);
  b.box(X0 + 11, 9, 4, X0 + 21, 13, 10, BASE_DARK);                 // engine housing between the crew
  for (int x = X0 + 12; x < X0 + 21; x += 2) b.box(x, 9, 10, x + 1, 13, 11, STEEL_DARK);
  b.box(X1 - 7, 4, 9, X1 - 2, 11, 11, BASE);                        // the gunner's raised box at the front
  b.box(X0 - 1, 0, 6, X1 + 1, 1, 7, BASE_DARK);                     // track guards
  b.box(X0 - 1, 21, 6, X1 + 1, 22, 7, BASE_DARK);
  for (int x : {X0 + 3, X0 + 6}) b.box(x, 1, 7, x + 2, 3, 10, BASE_DARK);  // jerrycans on the side
  b.box(X0 + 1, 18, 7, X0 + 10, 21, 9, BLANKET);                    // bedrolls
  b.box(X0, 6, 7, X0 + 1, 16, 9, STEEL_DARK);                       // rear tow plate
  crewman(b, look, X1 - 9, 14, 5);                                  // driver
  crewman(b, look, X0 + 5, 8, 5);
  crewman(b, look, X0 + 5, 14, 5);
  headlight(b, X1 - 1, 5, 8);
  headlight(b, X1 - 1, 16, 8);
  stampSide(b, X0 + 14, 8, false, nationalMark(look, true));
  stampSide(b, X0 + 14, 8, true, nationalMark(look, true));
  teamBand(b, 8, 9, X0 + 1, X1 - 7);
  // The Bren through the front plate.
  const float pc = X1 - 5.0f;
  cylX(t, pc - 2, pc + 7, 7.5f, 10, 0.45f, STEEL_DARK);
  t.box(static_cast<int>(pc), 7, 11, static_cast<int>(pc) + 1, 8, 12, STEEL_DARK);
  crewman(t, look, pc - 3, 7.5f, 7);
  um.pivotX = pc;
  um.pivotY = 7.5f;
  finish(um, look, 8212, 6, 1.2f);
  return um;
}

// Christie tanks: five big road wheels a side, no return rollers.
UnitModel britishCrusader(const FactionLook& look, int f) {
  const int GX = 68, GY = 24, GZ = 21;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 14, X1 = 54;
  for (float y0 : {1.0f, 18.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 5, 8, 5, 3.3f, false, false}, f);
  b.box(X0 + 1, 6, 2, X1 - 2, 18, 8, BASE_DARK);
  armourBox(b, X1 - 7, X1, 6, 18, 2, 8, -0.7f, 0, 0, BASE_DARK);    // stepped nose
  b.box(X0 - 1, 0, 8, X1 + 1, 24, 9, BASE);                          // track guards
  b.box(X0, 0, 5, X1, 1, 8, BASE);                                   // sand shields over the top run
  b.box(X0, 23, 5, X1, 24, 8, BASE);
  armourBox(b, X0 + 1, X1 - 2, 2, 22, 9, 12, 0.5f, 0.3f, 0.1f, BASE);  // long, low hull
  b.box(X1 - 8, 13, 12, X1 - 4, 19, 13, BASE_LIGHT);                 // driver's hatch, offset right
  b.box(X1 - 3, 14, 10, X1 - 2, 18, 11, STEEL_DARK);
  for (int x = X0 + 2; x < X0 + 15; x += 2) b.box(x, 5, 12, x + 1, 19, 13, STEEL_DARK);  // engine louvres
  b.box(X0 + 4, 9, 12, X0 + 12, 15, 14, BASE_DARK);                  // air cleaner housing
  cylY(b, X0 - 1, 7, 1.0f, 6, 9, EXHAUST);
  cylY(b, X0 - 1, 7, 1.0f, 15, 18, EXHAUST);
  b.box(X0 + 18, 1, 9, X0 + 30, 2, 10, STEEL_DARK);                  // tow cable
  headlight(b, X1, 3, 9);
  headlight(b, X1, 20, 9);
  stampSide(b, X0 + 16, 11, false, nationalMark(look, true));
  stampSide(b, X0 + 16, 11, true, nationalMark(look, true));
  // A low, many-sided turret with a broad mantlet and the 6-pounder.
  const float tc = 34.0f;
  armourBox(t, tc - 8, tc + 6, 4, 20, 12, 17, 0.7f, 0.5f, 0.6f, BASE);
  roundPlan(t, tc - 1, 12, 9.5f, 8.6f, 12, 17);
  t.box(static_cast<int>(tc) + 5, 7, 12, static_cast<int>(tc) + 8, 17, 16, BASE_DARK);   // mantlet
  cylX(t, tc + 8, tc + 23, 12, 14.5f, 0.6f, BASE_DARK);              // 6-pdr
  cylX(t, tc + 8, tc + 10, 9.5f, 14.5f, 0.4f, STEEL_DARK);           // Besa
  t.box(static_cast<int>(tc) - 12, 7, 13, static_cast<int>(tc) - 8, 17, 16, BASE);       // turret bin
  t.box(static_cast<int>(tc) - 5, 6, 17, static_cast<int>(tc) + 1, 18, 18, BASE_LIGHT);  // split hatch
  crewman(t, look, tc - 3, 12, 17);
  cylinder(t, tc - 6, 5, 0.5f, 17, 22, STEEL_DARK);                  // wireless aerial
  teamBand(t, 14, 15, 0, static_cast<int>(tc) + 5);
  um.pivotX = tc;
  um.pivotY = 12;
  finish(um, look, 4401, 7);
  return um;
}

UnitModel britishCromwell(const FactionLook& look, int f) {
  const int GX = 72, GY = 26, GZ = 24;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 16, X1 = 55;
  for (float y0 : {0.0f, 21.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 5, 10, 5, 3.6f, false, false}, f);
  b.box(X0 + 1, 5, 2, X1 - 1, 21, 10, BASE_DARK);
  b.box(X1 - 2, 4, 3, X1, 22, 14, BASE);                             // upright nose plate
  b.box(X0, 0, 10, X1, 26, 15, BASE);                                // slab sides over the tracks
  b.box(X0, 0, 10, X1, 1, 11, BASE_DARK);                            // their lower edge in shadow
  b.box(X0, 25, 10, X1, 26, 11, BASE_DARK);
  for (int x = X0 + 4; x < X1 - 2; x += 5)                           // bolted side panels
    for (int y : {0, 25}) b.set(x, y, 13, STEEL_DARK);
  b.box(X1 - 1, 15, 12, X1, 20, 13, STEEL_DARK);                     // driver's visor
  b.box(X1 - 1, 5, 11, X1, 8, 13, STEEL);                            // hull Besa
  cylX(b, X1, X1 + 2, 6.5f, 12, 0.45f, STEEL_DARK);
  for (int x = X0 + 2; x < X0 + 16; x += 2) b.box(x, 4, 15, x + 1, 22, 16, STEEL_DARK);  // engine deck louvres
  b.box(X0 - 3, 5, 9, X0, 21, 14, BASE_DARK);                        // the big exhaust cowl at the back
  cylY(b, X0 - 2, 11, 1.1f, 7, 10, EXHAUST);
  cylY(b, X0 - 2, 11, 1.1f, 16, 19, EXHAUST);
  b.box(X1 - 6, 0, 15, X1 - 1, 3, 16, BASE_DARK);                    // front mudguards
  b.box(X1 - 6, 23, 15, X1 - 1, 26, 16, BASE_DARK);
  fenderTools(b, X0 + 18, 0, 15);
  headlight(b, X1, 2, 13);
  headlight(b, X1, 23, 13);
  stampSide(b, X0 + 20, 14, false, nationalMark(look, true));
  stampSide(b, X0 + 20, 14, true, nationalMark(look, true));
  // A square, flat-sided turret with a bolted face plate.
  const float tc = 35.0f;
  t.box(static_cast<int>(tc) - 8, 5, 15, static_cast<int>(tc) + 6, 21, 21, BASE);
  t.box(static_cast<int>(tc) + 6, 4, 15, static_cast<int>(tc) + 7, 22, 21, BASE_DARK);   // face plate, wider than the turret
  for (int y = 5; y < 22; y += 4) t.set(static_cast<int>(tc) + 7, y, 20, STEEL_DARK);    // its bolts
  t.box(static_cast<int>(tc) + 7, 10, 16, static_cast<int>(tc) + 9, 16, 20, BASE_DARK);  // mantlet
  cylX(t, tc + 9, tc + 24, 13, 18, 0.8f, BASE_DARK);                 // 75 mm
  cylX(t, tc + 24, tc + 25, 13, 18, 1.0f, BASE_DARK);
  cylX(t, tc + 9, tc + 11, 10, 18, 0.4f, STEEL_DARK);                // coaxial Besa
  t.box(static_cast<int>(tc) - 13, 6, 16, static_cast<int>(tc) - 8, 20, 20, BASE);       // stowage bin
  t.box(static_cast<int>(tc) - 13, 6, 20, static_cast<int>(tc) - 8, 20, 21, CANVAS_DARK);  // tarpaulin on it
  for (int y : {4, 21}) t.box(static_cast<int>(tc) + 2, y, 18, static_cast<int>(tc) + 4, y + 1, 20, STEEL_DARK);  // smoke dischargers
  cylinder(t, tc - 3, 15, 2.2f, 21, 22, BASE);                       // commander's cupola
  crewman(t, look, tc - 3, 15, 22);
  t.box(static_cast<int>(tc) - 4, 6, 21, static_cast<int>(tc), 10, 22, BASE_LIGHT);      // loader's hatch
  cylinder(t, tc - 7, 6, 0.5f, 21, 26, STEEL_DARK);                  // aerial
  teamBand(t, 18, 19, 0, static_cast<int>(tc) + 6);
  um.pivotX = tc;
  um.pivotY = 13;
  finish(um, look, 4502, 8);
  return um;
}

// The Sherman hull, shared by the Firefly.
void shermanHull(VoxelModel& b, const FactionLook& look, int X0, int X1, int f) {
  vvss(b, static_cast<float>(X0), static_cast<float>(X1), f);
  shermanNose(b, X0, X1);
  armourBox(b, X1 - 15, X1, 2, 24, 9, 16, 1.7f, 0, 0.25f, BASE);    // the sloped glacis
  armourBox(b, X0, X1 - 13, 1, 25, 9, 16, 0, 0.6f, 0.15f, BASE);    // upper hull
  b.box(X0 - 1, 1, 9, X1 - 13, 25, 10, BASE_DARK);                   // sponson lip
  b.box(X1 - 11, 5, 14, X1 - 7, 10, 15, BASE_LIGHT);                 // driver's hatches on the glacis
  b.box(X1 - 11, 16, 14, X1 - 7, 21, 15, BASE_LIGHT);
  b.box(X1 - 8, 4, 13, X1 - 6, 8, 14, STEEL);                        // bow MG
  cylX(b, X1 - 6, X1 - 4, 6, 13.5f, 0.45f, STEEL_DARK);
  for (int x = X0 + 2; x < X0 + 12; x += 2) b.box(x, 6, 16, x + 1, 20, 17, STEEL_DARK);  // engine deck
  b.box(X0 - 2, 5, 6, X0, 21, 12, BASE_DARK);                        // rear plate and exhaust deflector
  b.box(X0 + 16, 1, 16, X0 + 26, 3, 17, WOOD);                       // spare track blocks on the hull side
  headlight(b, X1 - 3, 3, 10);
  headlight(b, X1 - 3, 22, 10);
  stampSide(b, X0 + 14, 14, false, nationalMark(look, true));
  stampSide(b, X0 + 14, 14, true, nationalMark(look, true));
}

UnitModel britishFirefly(const FactionLook& look, int f) {
  if (!look.british) return mediumTank(look, f);
  const int GX = 96, GY = 26, GZ = 26;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 24, X1 = 62;
  shermanHull(b, look, X0, X1, f);
  // A rounded cast turret, the armoured wireless box hung on its back, and
  // the very long 17-pounder (its muzzle half painted to hide the length).
  const float tc = 43.0f;
  armourBox(t, tc - 8, tc + 7, 4, 22, 16, 22, 0.4f, 0.45f, 0.45f, BASE);
  roundPlan(t, tc, 13, 8.6f, 9.0f, 16, 22);
  t.box(static_cast<int>(tc) - 15, 6, 16, static_cast<int>(tc) - 7, 20, 21, BASE);       // wireless box
  t.box(static_cast<int>(tc) + 6, 8, 16, static_cast<int>(tc) + 9, 18, 21, BASE_DARK);   // mantlet
  cylX(t, tc + 9, tc + 38, 13, 18.5f, 0.75f, BASE_DARK);             // 17-pdr
  cylX(t, tc + 26, tc + 38, 13, 18.5f, 0.75f, MARK_WHITE);           // the light half of the countershading
  for (int x = static_cast<int>(tc) + 27; x < static_cast<int>(tc) + 38; x += 3) cylX(t, x, x + 1, 13, 18.5f, 0.8f, BASE_DARK);
  cylX(t, tc + 38, tc + 41, 13, 18.5f, 1.3f, BASE_DARK);             // muzzle brake
  t.set(static_cast<int>(tc) + 39, 13, 18, STEEL_DARK);
  cylinder(t, tc - 2, 17, 2.3f, 22, 23, BASE);                       // cupola
  crewman(t, look, tc - 2, 17, 23);
  t.box(static_cast<int>(tc) - 3, 6, 22, static_cast<int>(tc) + 1, 10, 23, BASE_LIGHT);
  cylinder(t, tc - 12, 7, 0.5f, 21, 26, STEEL_DARK);                 // aerial
  teamBand(t, 19, 20, 0, static_cast<int>(tc) + 6);
  um.pivotX = tc;
  um.pivotY = 13;
  finish(um, look, 4603, 8);
  return um;
}

UnitModel britishAchilles(const FactionLook& look, int f) {
  const int GX = 96, GY = 26, GZ = 24;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 24, X1 = 62;
  vvss(b, static_cast<float>(X0), static_cast<float>(X1), f);
  shermanNose(b, X0, X1);
  // The M10's angular hull: everything sloped, bolt-on armour bosses.
  armourBox(b, X0, X1, 1, 25, 9, 15, 1.8f, 0.9f, 0.7f, BASE);
  for (int x = X0 + 4; x < X1 - 6; x += 6)
    for (int y : {2, 23}) b.set(x, y, 12, STEEL_DARK);
  b.box(X1 - 12, 6, 14, X1 - 8, 11, 15, BASE_LIGHT);                 // drivers' hatches
  b.box(X1 - 12, 15, 14, X1 - 8, 20, 15, BASE_LIGHT);
  for (int x = X0 + 2; x < X0 + 11; x += 2) b.box(x, 7, 15, x + 1, 19, 16, STEEL_DARK);
  b.box(X0 - 2, 6, 6, X0, 20, 11, BASE_DARK);
  headlight(b, X1 - 3, 3, 10);
  headlight(b, X1 - 3, 22, 10);
  stampSide(b, X0 + 13, 13, false, nationalMark(look, true));
  stampSide(b, X0 + 13, 13, true, nationalMark(look, true));
  // An open-topped, five-sided turret, the counterweights hung on its back.
  const float tc = 42.0f;
  armourBox(t, tc - 8, tc + 7, 3, 23, 15, 21, 1.0f, 0.3f, 0.7f, BASE);
  hollow(t, static_cast<int>(tc) - 6, 6, 16, static_cast<int>(tc) + 4, 20, 21);
  t.box(static_cast<int>(tc) - 6, 6, 15, static_cast<int>(tc) + 4, 20, 16, BASE_DARK);
  t.box(static_cast<int>(tc) - 13, 6, 16, static_cast<int>(tc) - 8, 20, 19, BASE_DARK);  // counterweights
  t.box(static_cast<int>(tc) + 6, 9, 16, static_cast<int>(tc) + 10, 17, 20, BASE_DARK);  // mantlet
  cylX(t, tc + 10, tc + 37, 13, 18, 0.75f, BASE_DARK);               // 17-pdr
  cylX(t, tc + 37, tc + 40, 13, 18, 1.3f, BASE_DARK);
  t.set(static_cast<int>(tc) + 38, 13, 18, STEEL_DARK);
  crewman(t, look, tc - 2, 8, 16);
  crewman(t, look, tc - 2, 17, 16);
  b.box(X0 + 4, 3, 15, X0 + 12, 6, 16, CANVAS_DARK);                 // rolled tarpaulin
  teamBand(t, 19, 20, 0, static_cast<int>(tc) + 6);
  um.pivotX = tc;
  um.pivotY = 13;
  finish(um, look, 4704, 8);
  return um;
}

UnitModel britishSexton(const FactionLook& look, int f) {
  const int GX = 76, GY = 26, GZ = 24;
  UnitModel um = blank(GX, GY, GZ, false);
  VoxelModel& b = um.body;
  const int X0 = 16, X1 = 54;
  vvss(b, static_cast<float>(X0), static_cast<float>(X1), f);
  shermanNose(b, X0, X1);
  armourBox(b, X1 - 12, X1, 2, 24, 9, 14, 1.2f, 0, 0.1f, BASE);     // glacis
  // A tall open box over the hull, the 25-pounder offset to the right.
  b.box(X0, 1, 9, X1 - 10, 25, 18, BASE);
  hollow(b, X0 + 1, 2, 10, X1 - 11, 24, 18);
  b.box(X0 + 1, 2, 9, X1 - 11, 24, 10, BASE_DARK);
  b.box(X1 - 13, 9, 10, X1 - 9, 17, 16, STEEL_DARK);                 // gun cradle
  bar(b, X1 - 11, 13, 15, X1 + 13, 13, 18.5f, 1.0f, BASE_DARK);      // 25-pdr, elevated a little
  bar(b, X1 + 11, 13, 18.2f, X1 + 14, 13, 18.7f, 1.5f, BASE_DARK);   // muzzle brake
  crewman(b, look, X0 + 6, 6, 10);
  crewman(b, look, X0 + 10, 19, 10);
  crewman(b, look, X0 + 16, 7, 10);
  b.box(X0 + 2, 15, 10, X0 + 7, 23, 13, WOOD);                       // ammunition boxes
  b.box(X0, 0, 17, X1 - 10, 2, 18, CANVAS_DARK);                     // tarpaulin rolled on the rim
  for (int x = X0 + 2; x < X0 + 9; x += 2) b.box(x, 1, 18, x + 1, 25, 19, STEEL_DARK);  // tarpaulin hoops
  b.box(X0 - 2, 5, 6, X0, 21, 12, BASE_DARK);
  headlight(b, X1 - 3, 3, 10);
  headlight(b, X1 - 3, 22, 10);
  stampSide(b, X0 + 10, 16, false, nationalMark(look, true));
  stampSide(b, X0 + 10, 16, true, nationalMark(look, true));
  teamBand(b, 15, 16, X0, X1 - 10);
  finish(um, look, 4805, 7);
  return um;
}

UnitModel britishChurchill(const FactionLook& look, int f) {
  const int GX = 102, GY = 30, GZ = 26;
  UnitModel um = blank(GX, GY, GZ, true);
  VoxelModel& b = um.body;
  VoxelModel& t = um.turret;
  const int X0 = 23, X1 = 77;
  // Tracks run right round the hull sides, many small wheels hidden behind
  // the panniers; only the top run and the horns at each end show.
  for (float y0 : {0.0f, 24.0f}) tracks(b, {X0 + 0.0f, X1 + 0.0f, y0, y0 + 6, 14, 11, 1.6f, true, true}, f);
  for (int y0 : {0, 29}) {
    b.box(X0 + 4, y0, 3, X1 - 4, y0 + 1, 12, BASE);                  // pannier plate
    for (int x = X0 + 7; x < X1 - 6; x += 4) b.box(x, y0, 3, x + 2, y0 + 1, 4, STEEL_DARK);  // mud chutes
    const int dx = (X0 + X1) / 2;
    b.box(dx - 3, y0, 5, dx + 3, y0 + 1, 11, BASE_DARK);             // escape door
    b.set(dx + 2, y0, 8, STEEL);
  }
  b.box(X0 + 2, 6, 3, X1 - 2, 24, 15, BASE_DARK);                    // hull between the tracks
  b.box(X0 + 2, 6, 15, X1 - 2, 24, 16, BASE);                        // deck
  b.box(X1 - 3, 6, 4, X1 - 1, 24, 15, BASE);                         // upright nose
  b.box(X1 - 1, 16, 12, X1, 21, 13, STEEL_DARK);                     // driver's visor
  b.box(X1 - 1, 8, 11, X1, 11, 13, STEEL);                           // Besa ball
  cylX(b, X1, X1 + 2, 9.5f, 12, 0.45f, STEEL_DARK);
  for (int x = X0 + 4; x < X0 + 18; x += 2) b.box(x, 8, 16, x + 1, 22, 17, STEEL_DARK);  // engine louvres
  for (int y0 : {0, 24}) b.box(X0 + 3, y0, 14, X0 + 14, y0 + 6, 17, BASE);  // air intakes on the track tops
  b.box(X0 - 1, 9, 6, X0 + 2, 21, 13, BASE_DARK);                    // rear plate
  cylY(b, X0 - 1, 11, 1.0f, 10, 13, EXHAUST);
  cylY(b, X0 - 1, 11, 1.0f, 17, 20, EXHAUST);
  fenderTools(b, X0 + 22, 0, 15);
  headlight(b, X1 - 1, 7, 14);
  headlight(b, X1 - 1, 22, 14);
  stampSide(b, X0 + 14, 11, false, nationalMark(look, true), 1);
  stampSide(b, X0 + 14, 11, true, nationalMark(look, true), 28);
  // The heavy turret of the Mark VII, cast, with the 75 mm.
  const float tc = 52.0f;
  armourBox(t, tc - 9, tc + 8, 6, 24, 16, 23, 0.25f, 0.25f, 0.3f, BASE);
  roundPlan(t, tc, 15, 10.0f, 9.6f, 16, 23);
  t.box(static_cast<int>(tc) + 7, 11, 17, static_cast<int>(tc) + 10, 19, 22, BASE_DARK);  // mantlet
  cylX(t, tc + 10, tc + 25, 15, 19.5f, 0.8f, BASE_DARK);
  cylX(t, tc + 25, tc + 26, 15, 19.5f, 1.0f, BASE_DARK);
  cylX(t, tc + 10, tc + 12, 12, 19.5f, 0.4f, STEEL_DARK);
  cylinder(t, tc - 3, 19, 2.4f, 23, 24, BASE);
  crewman(t, look, tc - 3, 19, 24);
  t.box(static_cast<int>(tc) - 4, 8, 23, static_cast<int>(tc), 12, 24, BASE_LIGHT);
  t.box(static_cast<int>(tc) - 13, 9, 17, static_cast<int>(tc) - 9, 21, 21, BASE);       // stowage bin
  cylinder(t, tc - 9, 8, 0.5f, 22, 26, STEEL_DARK);
  teamBand(t, 20, 21, 0, static_cast<int>(tc) + 7);
  um.pivotX = tc;
  um.pivotY = 15;
  finish(um, look, 4906, 10, 1.1f);
  return um;
}

// A Bedford cab-forward cab: the driver sits over the engine behind a flat
// two-pane windscreen.
void bedfordCab(VoxelModel& b, int X1, int GY) {
  const int y0 = 2, y1 = GY - 2;
  b.box(X1 - 9, y0, 5, X1, y1, 14, BASE);
  b.box(X1 - 9, y0, 14, X1, y1, 15, BASE_DARK);                      // roof
  b.box(X1 - 1, y0 + 1, 10, X1, y1 - 1, 13, GLASS);                  // windscreen
  b.box(X1 - 1, GY / 2, 10, X1, GY / 2 + 1, 13, BASE);               // its centre bar
  b.box(X1 - 1, y0 + 3, 5, X1, y1 - 3, 9, STEEL_DARK);               // grille
  b.box(X1 - 6, y0, 10, X1 - 2, y0 + 1, 13, GLASS);                  // door windows
  b.box(X1 - 6, y1 - 1, 10, X1 - 2, y1, 13, GLASS);
  for (int yy : {y0 - 2, y1}) {                                      // round mudguards over the front wheels
    b.box(X1 - 8, yy, 6, X1 - 1, yy + 2, 7, BASE_DARK);
    b.box(X1 - 7, yy, 7, X1 - 2, yy + 2, 8, BASE_DARK);
  }
  headlight(b, X1, y0 + 1, 7);
  headlight(b, X1, y1 - 2, 7);
}

UnitModel britishLorry(const FactionLook& look, int f) {
  const int GX = 50, GY = 18, GZ = 17;
  UnitModel um = blank(GX, GY, GZ, false);
  VoxelModel& b = um.body;
  const int X0 = 6, X1 = 44;
  for (float wx : {X0 + 8.0f, X1 - 5.0f})
    for (float y0 : {0.0f, 15.0f}) truckWheel(b, wx, 3.0f, y0, y0 + 3, f);
  b.box(X0, 3, 3, X1 - 2, 15, 5, STEEL_DARK);
  bedfordCab(b, X1, GY);
  // A fuel tanker body, with a rack of jerrycans behind.
  cylX(b, X0 + 4, X1 - 10, 9, 9.5f, 4.4f, BASE_DARK);
  b.box(X0 + 6, 7, 14, X1 - 12, 11, 15, STEEL_DARK);
  for (int x : {X0 + 10, X0 + 20}) cylinder(b, x, 9, 1.2f, 14, 16, STEEL);
  b.box(X0, 3, 5, X0 + 4, 15, 6, STEEL_DARK);
  for (int y = 4; y < 14; y += 3) b.box(X0, y, 6, X0 + 3, y + 2, 10, BASE_DARK);  // jerrycans
  teamBand(b, 6, 7, X0 + 4, X1 - 10);
  stampSide(b, X1 - 5, 13, false, nationalMark(look, true));
  stampSide(b, X1 - 5, 13, true, nationalMark(look, true));
  finish(um, look, 9313, 5, 1.2f);
  return um;
}

UnitModel britishWorkshop(const FactionLook& look, int f) {
  const int GX = 62, GY = 22, GZ = 22;
  UnitModel um = blank(GX, GY, GZ, false);
  VoxelModel& b = um.body;
  const int X0 = 6, X1 = 56;
  for (float wx : {X0 + 6.0f, X0 + 14.0f, X1 - 6.0f})
    for (float y0 : {0.0f, 19.0f}) truckWheel(b, wx, 3.2f, y0, y0 + 3, f);
  b.box(X0, 3, 3, X1 - 2, 19, 6, STEEL_DARK);
  bedfordCab(b, X1, GY);
  // A box-bodied workshop with roll-up sides, a crane on the back.
  b.box(X0, 2, 6, X1 - 11, 20, 16, BASE);
  b.box(X0, 2, 16, X1 - 11, 20, 17, BASE_DARK);
  b.box(X0 + 4, 1, 9, X1 - 15, 2, 14, CANVAS_DARK);                  // rolled side shutters
  b.box(X0 + 4, 20, 9, X1 - 15, 21, 14, CANVAS_DARK);
  b.box(X0 + 18, 1, 7, X0 + 22, 2, 9, WOOD);                         // fold-down steps
  b.box(X0 + 2, 8, 17, X0 + 6, 14, 19, STEEL_DARK);                  // crane slewing ring
  bar(b, X0 + 4, 11, 19, X1 - 14, 11, 21, 0.8f, STEEL);              // jib laid forward
  b.box(X1 - 15, 10, 17, X1 - 14, 12, 21, STEEL_DARK);               // jib rest
  b.box(X0 - 1, 4, 4, X0, 18, 6, STEEL_DARK);                        // outrigger
  teamBand(b, 10, 11, X0, X1 - 11);
  stampSide(b, X1 - 5, 13, false, nationalMark(look, true));
  stampSide(b, X1 - 5, 13, true, nationalMark(look, true));
  finish(um, look, 9414, 6, 1.2f);
  return um;
}

// ---------------------------------------------------------------------------
// Dug-in infantry: the same men down in a trench, or kneeling behind a low
// wall of bags, each with his own stretch of parapet in front so he reads as
// dug in whichever way he faces. Frame 0 keeps low, frame 1 is up and firing.
// ---------------------------------------------------------------------------
enum class Post : uint8_t { Trench, Bags };

float hashf(uint32_t a, uint32_t b) { return (hash32(a * 2654435761u ^ (b + 0x9e3779b9u)) & 0xFFFF) / 65535.0f; }

// A sandbag at soldier scale (about 50 x 30 x 15 cm).
void smallBag(VoxelModel& m, float cx, float cy, float cz, bool alongX, uint32_t seed) {
  const float hx = alongX ? 2.6f : 1.6f, hy = alongX ? 1.6f : 2.6f, hz = 1.1f;
  const float tone = hashf(seed, 3);
  const uint8_t col = tone < 0.55f ? SANDBAG : tone < 0.85f ? SANDBAG_DARK : DRY_GRASS;
  for (int z = static_cast<int>(cz - hz - 1); z <= static_cast<int>(cz + hz + 1); ++z)
    for (int y = static_cast<int>(cy - hy - 1); y <= static_cast<int>(cy + hy + 1); ++y)
      for (int x = static_cast<int>(cx - hx - 1); x <= static_cast<int>(cx + hx + 1); ++x) {
        const float dx = (x + 0.5f - cx) / hx, dy = (y + 0.5f - cy) / hy, dz = (z + 0.5f - cz) / hz;
        if (std::pow(std::abs(dx), 2.4f) + std::pow(std::abs(dy), 2.4f) + dz * dz <= 1.0f)
          m.set(x, y, z, std::abs(dz) > 0.6f && hashf(seed + x * 7 + y, 5) < 0.3f ? static_cast<uint8_t>(SANDBAG_DARK) : col);
      }
}

// The parapet in front of a man (or two) at x = fx, spanning y0..y1.
void parapet(VoxelModel& m, Post post, int fx, int y0, int y1, uint32_t seed) {
  const float mid = (y0 + y1) * 0.5f, half = (y1 - y0) * 0.5f;
  if (post == Post::Trench) {
    // Thrown-up earth, curving back at the ends, a few bags along the top.
    for (int y = y0 - 2; y <= y1 + 2; ++y) {
      const float t = std::abs(y + 0.5f - mid) / (half + 2.0f);
      const int bend = static_cast<int>(t * t * 4.0f);
      const int h = t > 0.85f ? 1 : 2;
      for (int x = fx - bend; x < fx - bend + 4; ++x)
        for (int z = 0; z < h + (x == fx - bend + 1 ? 1 : 0); ++z)
          m.set(x, y, z, hashf(seed + x * 13 + y * 7, z) < 0.35f ? static_cast<uint8_t>(EARTH_DARK) : static_cast<uint8_t>(EARTH));
    }
    for (float y = static_cast<float>(y0); y <= y1; y += 5.4f) smallBag(m, fx + 1.5f, y + 1.5f, 3.0f, false, seed++);
  } else {
    // Three courses of bags in a shallow arc, laid in a stretcher bond.
    for (int course = 0; course < 3; ++course)
      for (float y = static_cast<float>(y0 - 3) + (course & 1) * 2.6f; y <= y1 + 3; y += 5.2f) {
        const float t = std::abs(y - mid) / (half + 3.0f);
        smallBag(m, fx + 1.6f - t * t * 4.0f, y, 1.1f + course * 2.1f, false, seed++);
      }
  }
}

UnitModel dugIn(const FactionLook& look, int f, Post post, Kit lead, Kit second, bool cap = false, bool smock = false) {
  const bool pair = second != lead || lead == Kit::MachineGun;
  const int GX = 30, GY = pair ? 24 : 16, GZ = 20;
  UnitModel um;
  um.body = VoxelModel(GX, GY, GZ);
  VoxelModel& m = um.body;
  const bool up = f == 1;
  // How far each man sits below the ground line.
  const int sink = post == Post::Trench ? (up ? 7 : 10) : (up ? 0 : 3);
  const int cx = 13;  // the men's feet, front to back
  auto place = [&](float cy, SoldierSpec s) {
    VoxelModel man(GX, GY, 24);
    soldier(man, look, cx, cy, s);
    for (int z = sink; z < 24; ++z)
      for (int y = 0; y < GY; ++y)
        for (int x = 0; x < GX; ++x)
          if (const uint8_t c = man.get(x, y, z)) m.set(x, y, z - sink, c);
  };
  SoldierSpec a;
  a.kit = lead;
  a.cap = cap;
  a.smock = smock;
  a.aim = up;
  a.frame = 0;
  const float leadY = pair ? GY * 0.5f - 4.5f : GY * 0.5f;
  const int fx = cx + 4;
  if (lead == Kit::MachineGun) {
    // The gun lies on the parapet on its bipod; the gunner behind it, the
    // loader beside him with a belt.
    a.kit = Kit::Ammo;
    a.aim = false;
    place(leadY, a);
    SoldierSpec b = a;
    place(leadY + 8, b);
    const int zg = post == Post::Trench ? 4 : 6;
    m.box(cx + 1, static_cast<int>(leadY), zg, cx + 12, static_cast<int>(leadY) + 1, zg + 1, STEEL_DARK);  // barrel and receiver
    m.box(cx, static_cast<int>(leadY), zg, cx + 3, static_cast<int>(leadY) + 1, zg + 1, STOCK);
    m.box(cx + 3, static_cast<int>(leadY) - 1, zg - 1, cx + 6, static_cast<int>(leadY) + 2, zg + 1, STEEL_DARK);
    m.set(cx + 9, static_cast<int>(leadY) - 1, zg - 1, STEEL_DARK);  // bipod
    m.set(cx + 9, static_cast<int>(leadY) + 1, zg - 1, STEEL_DARK);
    m.box(cx + 4, static_cast<int>(leadY) + 1, zg, cx + 5, static_cast<int>(leadY) + 4, zg + 1, BASE_LIGHT);  // the belt
    if (up) m.set(cx + 12, static_cast<int>(leadY), zg, LAMP);  // muzzle glow
    parapet(m, post, fx + 2, static_cast<int>(leadY) - 3, static_cast<int>(leadY) + 10, 811u + f);
  } else {
    place(leadY, a);
    if (pair) {
      SoldierSpec b;
      b.kit = second;
      b.aim = up;
      place(leadY + 9, b);
    }
    parapet(m, post, fx + (lead == Kit::RocketTube || lead == Kit::AtRifle ? 1 : 0), static_cast<int>(leadY) - 3,
            static_cast<int>(leadY) + (pair ? 12 : 3), 523u + static_cast<uint32_t>(lead) * 17u + f);
  }
  return um;
}

UnitModel dugRifleman(const FactionLook& look, int f) { return dugIn(look, f, Post::Trench, Kit::Rifle, Kit::Rifle); }
UnitModel dugSniper(const FactionLook& look, int f) { return dugIn(look, f, Post::Trench, Kit::ScopedRifle, Kit::ScopedRifle, true, true); }
UnitModel dugEngineer(const FactionLook& look, int f) { return dugIn(look, f, Post::Trench, Kit::Smg, Kit::Smg); }
UnitModel dugMg(const FactionLook& look, int f) { return dugIn(look, f, Post::Trench, Kit::MachineGun, Kit::Ammo); }
UnitModel dugAt(const FactionLook& look, int f) {
  return dugIn(look, f, Post::Trench, look.sloped ? Kit::AtRifle : Kit::RocketTube, Kit::Rifle);
}
UnitModel bagRifleman(const FactionLook& look, int f) { return dugIn(look, f, Post::Bags, Kit::Rifle, Kit::Rifle); }
UnitModel bagSniper(const FactionLook& look, int f) { return dugIn(look, f, Post::Bags, Kit::ScopedRifle, Kit::ScopedRifle, true, true); }
UnitModel bagEngineer(const FactionLook& look, int f) { return dugIn(look, f, Post::Bags, Kit::Smg, Kit::Smg); }
UnitModel bagMg(const FactionLook& look, int f) { return dugIn(look, f, Post::Bags, Kit::MachineGun, Kit::Ammo); }
UnitModel bagAt(const FactionLook& look, int f) {
  return dugIn(look, f, Post::Bags, look.sloped ? Kit::AtRifle : Kit::RocketTube, Kit::Rifle);
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
  if (s.aim && (s.kit == Kit::Rifle || s.kit == Kit::ScopedRifle || s.kit == Kit::Smg || s.kit == Kit::Ammo)) {
    // Shouldered and laid on the target: both arms forward to the stock.
    const int zs = zt + 5;
    m.box(X + 1, Y - 1, zs - 1, X + 2, Y, zt + 6, tunic);
    m.box(X + 2, Y - 1, zs - 1, X + 5, Y, zs, tunic);
    m.set(X + 5, Y - 1, zs - 1, SKIN);
    m.box(X + 2, Y + 3, zs - 1, X + 4, Y + 4, zs, tunic);
    if (s.kit == Kit::Smg) {
      m.box(X + 1, Y + 2, zs, X + 8, Y + 3, zs + 1, STEEL_DARK);
    } else {
      m.box(X, Y + 2, zs, X + 7, Y + 3, zs + 1, STOCK);
      m.box(X + 7, Y + 2, zs, X + 11, Y + 3, zs + 1, STEEL_DARK);
      if (s.kit == Kit::ScopedRifle) m.box(X + 3, Y + 2, zs + 1, X + 6, Y + 3, zs + 2, STEEL_DARK);
    }
    return;
  }
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
  // The same men dug in: "<model>_dug" in a trench, "<model>_bags" behind sandbags.
  addUnit(q, cat, "rifleman_dug", &dugRifleman, 2, nullptr, kInfantryVoxel);
  addUnit(q, cat, "mg_team_dug", &dugMg, 2, nullptr, kInfantryVoxel);
  addUnit(q, cat, "at_team_dug", &dugAt, 2, nullptr, kInfantryVoxel);
  addUnit(q, cat, "engineer_dug", &dugEngineer, 2, nullptr, kInfantryVoxel);
  addUnit(q, cat, "sniper_dug", &dugSniper, 2, nullptr, kInfantryVoxel);
  addUnit(q, cat, "rifleman_bags", &bagRifleman, 2, nullptr, kInfantryVoxel);
  addUnit(q, cat, "mg_team_bags", &bagMg, 2, nullptr, kInfantryVoxel);
  addUnit(q, cat, "at_team_bags", &bagAt, 2, nullptr, kInfantryVoxel);
  addUnit(q, cat, "engineer_bags", &bagEngineer, 2, nullptr, kInfantryVoxel);
  addUnit(q, cat, "sniper_bags", &bagSniper, 2, nullptr, kInfantryVoxel);
  addUnit(q, cat, "scoutcar", &scoutCar, 3);
  addUnit(q, cat, "halftrack", &halftrack, 4);
  addUnit(q, cat, "light_tank", &lightTank, 4);
  addUnit(q, cat, "tank", &mediumTank, 4);
  addUnit(q, cat, "firefly", &britishFirefly, 4);  // the British Sherman with the 17-pounder
  addUnit(q, cat, "heavy_tank", &heavyTank, 4);
  addUnit(q, cat, "tank_destroyer", &tankDestroyer, 4);
  addUnit(q, cat, "artillery", &artillery, 4);
  addUnit(q, cat, "truck", &supplyTruck, 3);
  addUnit(q, cat, "mcv", &constructionTruck, 3);
}

}  // namespace slave::art

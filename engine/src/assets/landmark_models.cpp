// Pioneer Engine - landmarks for the campaigns (9.10): a radio transmitter, an
// aircraft hangar and a transport aeroplane for the airfields, a steelworks
// with its blast furnace, a government ministry, and the concrete flak tower
// that guards a capital. Building voxels are 1/12 tile, as in world_models.cpp.
#include <algorithm>
#include <cmath>

#include "art_common.hpp"
#include "pioneer/core/rng.hpp"

namespace pioneer::art {

namespace {
void lattice(VoxelModel& m, float cx, float cy, int z0, int z1, float r0, float r1, uint8_t c) {
  // A four-legged steel lattice, tapering, braced every few voxels.
  for (int z = z0; z < z1; ++z) {
    const float t = static_cast<float>(z - z0) / std::max(1, z1 - z0);
    const float r = r0 + (r1 - r0) * t;
    for (int sx : {-1, 1})
      for (int sy : {-1, 1}) m.set(static_cast<int>(cx + sx * r), static_cast<int>(cy + sy * r), z, c);
    if ((z - z0) % 4 == 0)
      for (float k = -r; k <= r; k += 0.5f) {
        m.set(static_cast<int>(cx + k), static_cast<int>(cy - r), z, c);
        m.set(static_cast<int>(cx + k), static_cast<int>(cy + r), z, c);
        m.set(static_cast<int>(cx - r), static_cast<int>(cy + k), z, c);
        m.set(static_cast<int>(cx + r), static_cast<int>(cy + k), z, c);
      }
  }
}
}  // namespace

// A broadcasting station: a tall lattice mast with a red lamp, the transmitter
// house at its foot.
VoxelModel makeRadioMast(const FactionLook&) {
  VoxelModel m(24, 24, 96);
  m.box(1, 13, 0, 13, 23, 9, BRICK);                      // the transmitter house
  houseWalls(m, 1, 13, 13, 23, 9, BRICK, CONCRETE_LIGHT);
  tiledRoof(m, 1, 13, 13, 23, 9);
  for (int x = 3; x < 12; x += 3) m.box(x, 22, 3, x + 2, 23, 7, WINDOW);
  m.box(5, 13, 0, 8, 14, 6, WOOD_DARK);
  m.box(13, 2, 0, 23, 12, 1, CONCRETE_DARK);              // the mast's foot
  lattice(m, 18, 7, 1, 92, 4.2f, 0.9f, STEEL_DARK);
  for (int z = 30; z < 92; z += 20) m.box(15, 4, z, 21, 10, z + 1, STEEL);   // aerial spreaders
  m.box(17, 6, 92, 19, 8, 95, MARK_RED);                  // the warning lamp
  weather(m, BRICK, BRICK_DARK, SOOT, 211, 12, 6);
  return m;
}

// An airfield hangar: a curved roof on a long shed, doors open to the apron.
VoxelModel makeHangar(const FactionLook&) {
  VoxelModel m(48, 36, 26);
  for (int x = 1; x < 47; ++x)
    for (int y = 2; y < 35; ++y) {
      const float u = (y - 18.5f) / 16.5f;
      const int top = static_cast<int>(8 + 16 * std::sqrt(std::max(0.0f, 1.0f - u * u)));
      for (int z = 0; z < top; ++z) {
        const bool shell = z >= top - 1 || y <= 2 || y >= 34;
        if (shell || x == 1) m.set(x, y, z, shell && z >= top - 1 ? ((x % 6 == 0) ? STEEL_DARK : ROOF_DARK) : CONCRETE);
      }
    }
  // Open doors on the east end: a dark interior with a glimpse of a fuselage.
  m.box(46, 4, 0, 47, 33, 20, CONCRETE_DARK);
  m.box(40, 8, 0, 47, 29, 18, 0);
  m.box(30, 16, 3, 44, 21, 8, STEEL);
  m.box(3, 34, 0, 9, 36, 5, WOOD_DARK);                   // a side office
  weather(m, CONCRETE, CONCRETE_DARK, CONCRETE_LIGHT, 223, 14, 6);
  return m;
}

// A three-engined transport aeroplane in corrugated metal, parked on the field.
VoxelModel makeTransportPlane(const FactionLook& look) {
  VoxelModel m(48, 48, 18);
  const uint8_t skin = BASE, dark = BASE_DARK;
  // Fuselage along x, nose to the east.
  for (int x = 6; x < 44; ++x) {
    const float t = (x - 6) / 38.0f;
    const float r = x > 38 ? 3.6f - (x - 38) * 0.45f : x < 14 ? 1.6f + (x - 6) * 0.25f : 3.6f;
    for (int y = 0; y < 48; ++y)
      for (int z = 0; z < 18; ++z) {
        const float dy = y + 0.5f - 24, dz = z + 0.5f - (6.5f + t * 0.5f);
        if (dy * dy + dz * dz <= r * r) m.set(x, y, z, (x % 3 == 0) ? dark : skin);
      }
  }
  m.box(36, 22, 7, 39, 26, 9, GLASS);                       // the cockpit glazing
  for (int x = 18; x < 34; x += 3) {
    m.set(x, 20, 7, GLASS);
    m.set(x, 27, 7, GLASS);
  }
  // Low wing, square-tipped, corrugated.
  for (int y = 1; y < 47; ++y) {
    const int span = std::abs(y - 24);
    const int chord = span < 6 ? 9 : 9 - (span - 6) / 6;
    m.box(24, y, 3, 24 + chord, y + 1, 4, (y % 2) ? skin : dark);
  }
  // Three engines: the nose and one on each wing.
  m.box(43, 22, 4, 46, 27, 9, STEEL_DARK);
  m.box(46, 21, 4, 47, 28, 9, STEEL);
  for (int side : {-1, 1}) {
    const int cy = 24 + side * 10;
    m.box(31, cy - 2, 2, 36, cy + 2, 6, STEEL_DARK);
    m.box(36, cy - 3, 2, 37, cy + 3, 7, STEEL);
  }
  // Tail fin and tailplane.
  m.box(6, 23, 6, 12, 25, 16, skin);
  m.box(4, 15, 7, 11, 33, 8, dark);
  // Wheels under the engines.
  for (int side : {-1, 1}) m.box(32, 24 + side * 10 - 1, 0, 34, 24 + side * 10 + 1, 2, RUBBER);
  (void)look;
  return m;
}

// A steelworks: a blast furnace in its steel jacket, the hot-blast stoves,
// a casting shed, a gantry of pipes and a tall chimney.
VoxelModel makeSteelworks(const FactionLook&) {
  VoxelModel m(48, 48, 72);
  m.box(2, 26, 0, 46, 46, 14, BRICK);                     // the casting shed
  houseWalls(m, 2, 26, 46, 46, 14, BRICK, CONCRETE_DARK);
  for (int x = 4; x < 44; x += 5) m.box(x, 45, 4, x + 3, 46, 11, WINDOW);
  for (int y = 26; y < 46; ++y) m.box(2, y, 14, 46, y + 1, 15 + (y % 4 < 2 ? 1 : 0), ROOF_DARK);
  for (int z = 0; z < 50; ++z) cylinder(m, 14, 14, z < 10 ? 7.5f : z < 40 ? 6.0f - (z - 10) * 0.05f : 4.0f, z, z + 1,
                                        (z % 7 == 6) ? STEEL_DARK : RUST);   // the furnace
  cylinder(m, 14, 14, 2.5f, 50, 58, STEEL_DARK);
  for (int i = 0; i < 3; ++i)                             // the hot-blast stoves
    for (int z = 0; z < 42; ++z) cylinder(m, 30.0f + i * 6, 8, 2.6f, z, z + 1, z > 38 ? STEEL_DARK : STEEL);
  for (int x = 14; x < 44; ++x) m.box(x, 7, 44, x + 1, 9, 46, RUST);   // the gas main
  for (int z = 0; z < 70; ++z) cylinder(m, 42, 40, 2.6f - z * 0.012f, z, z + 1, (z % 9 == 8) ? BRICK_DARK : BRICK);
  cylinder(m, 42, 40, 1.5f, 60, 70, 0);
  cylinder(m, 42, 40, 2.7f, 66, 70, SOOT);
  m.box(20, 18, 0, 24, 26, 3, RUST);                      // slag ladles
  m.box(4, 2, 0, 9, 7, 4, SOOT);                          // coke heap
  weather(m, BRICK, BRICK_DARK, SOOT, 241, 16, 6);
  return m;
}

// A government ministry: three storeys of pale stone, a portico of square
// pillars, tall windows and a flat roof with a parapet.
VoxelModel makeMinistry(const FactionLook&) {
  VoxelModel m(60, 36, 34);
  m.box(1, 3, 0, 59, 33, 2, CONCRETE_DARK);               // the plinth
  m.box(2, 4, 2, 58, 32, 28, CONCRETE_LIGHT);
  for (int z = 6; z < 26; z += 7)
    for (int x = 4; x < 56; x += 4) {
      m.box(x, 31, z, x + 2, 32, z + 4, WINDOW);
      m.box(x, 4, z, x + 2, 5, z + 4, WINDOW);
    }
  for (int y = 6; y < 30; y += 4)
    for (int z = 6; z < 26; z += 7) {
      m.box(2, y, z, 3, y + 2, z + 4, WINDOW);
      m.box(57, y, z, 58, y + 2, z + 4, WINDOW);
    }
  m.box(18, 32, 0, 42, 36, 2, CONCRETE_DARK);             // the portico steps and pillars
  for (int x = 19; x < 42; x += 4) m.box(x, 33, 2, x + 2, 35, 24, CONCRETE_LIGHT);
  m.box(17, 32, 24, 43, 36, 27, CONCRETE);
  m.box(26, 31, 2, 34, 32, 12, WOOD_DARK);                // the doors
  m.box(1, 3, 28, 59, 33, 29, CONCRETE);                  // cornice and parapet
  for (int x = 1; x < 59; ++x) {
    m.set(x, 3, 29, CONCRETE);
    m.set(x, 32, 29, CONCRETE);
  }
  for (int y = 3; y < 33; ++y) {
    m.set(1, y, 29, CONCRETE);
    m.set(58, y, 29, CONCRETE);
  }
  m.box(28, 16, 29, 32, 20, 33, STEEL_DARK);              // a flagpole base, no flag
  weather(m, CONCRETE_LIGHT, CONCRETE, SOOT, 257, 10, 4);
  return m;
}

// A flak tower: a windowless block of reinforced concrete with four round gun
// platforms at its corners; a fortress that dominates a city district.
VoxelModel makeFlakTower(const FactionLook&) {
  VoxelModel m(40, 40, 52);
  m.box(4, 4, 0, 36, 36, 42, CONCRETE);
  for (int z = 6; z < 40; z += 8)                         // slit embrasures
    for (int i = 8; i < 32; i += 6) {
      m.box(i, 4, z, i + 2, 5, z + 2, CONCRETE_DARK);
      m.box(i, 35, z, i + 2, 36, z + 2, CONCRETE_DARK);
      m.box(4, i, z, 5, i + 2, z + 2, CONCRETE_DARK);
      m.box(35, i, z, 36, i + 2, z + 2, CONCRETE_DARK);
    }
  for (int cx : {6, 34})
    for (int cy : {6, 34}) {
      cylinder(m, static_cast<float>(cx), static_cast<float>(cy), 6.0f, 34, 44, CONCRETE);
      cylinder(m, static_cast<float>(cx), static_cast<float>(cy), 4.6f, 43, 44, 0);
      cylinder(m, static_cast<float>(cx), static_cast<float>(cy), 1.6f, 43, 47, STEEL_DARK);   // the twin guns
      m.box(cx - 1, cy - 4, 46, cx + 1, cy - 1, 47, STEEL_DARK);
    }
  m.box(16, 16, 42, 24, 24, 48, CONCRETE_DARK);           // the fire-control post
  m.box(18, 17, 46, 22, 18, 47, WINDOW);
  m.box(17, 4, 0, 23, 5, 7, STEEL_DARK);                  // the steel door
  weather(m, CONCRETE, CONCRETE_DARK, SOOT, 263, 18, 6);
  return m;
}

// A brick fort of the old fortress rings (9.11: Poznan, Antwerp): a low
// polygonal casemate of dark brick, earth and turf heaped over its roof, gun
// embrasures all round, a gate in the gorge wall and a ditch before it.
VoxelModel makeFort(const FactionLook&) {
  VoxelModel m(60, 60, 22);
  m.box(0, 0, 0, 60, 60, 1, EARTH_DARK);                  // the ditch floor
  // The casemate: a blunted pentagon (the corners cut off).
  for (int y = 4; y < 56; ++y)
    for (int x = 4; x < 56; ++x) {
      const int cut = std::max(0, 14 - std::min({x - 4, 55 - x})) + std::max(0, 10 - (55 - y));
      if (cut > 14 && y > 40) continue;
      m.box(x, y, 1, x + 1, y + 1, 12, (x + y * 3) % 7 == 0 ? BRICK_DARK : BRICK);
    }
  for (int y = 6; y < 54; ++y)                            // earth over the roof, turf on top
    for (int x = 6; x < 54; ++x)
      if (m.get(x, y, 11)) {
        m.box(x, y, 12, x + 1, y + 1, 15, EARTH);
        m.set(x, y, 15, (x * 5 + y) % 9 == 0 ? DRY_GRASS : LEAF_DARK);
      }
  for (int i = 8; i < 52; i += 6) {                       // embrasures on every face
    m.box(i, 4, 5, i + 2, 5, 8, SOOT);
    m.box(4, i, 5, 5, i + 2, 8, SOOT);
    m.box(55, i, 5, 56, i + 2, 8, SOOT);
  }
  m.box(26, 54, 1, 34, 56, 9, WOOD_DARK);                 // the gate in the gorge wall
  m.box(25, 54, 9, 35, 56, 11, CONCRETE_LIGHT);
  m.box(28, 56, 0, 32, 60, 1, CONCRETE_DARK);             // the causeway over the ditch
  m.box(26, 26, 15, 34, 34, 19, CONCRETE);                // an observation cupola
  m.box(27, 26, 17, 33, 27, 18, SOOT);
  weather(m, BRICK, BRICK_DARK, SOOT, 271, 14, 6);
  return m;
}

// --- The British campaign's coasts and harbours (9.12) ---------------------------

// A landing craft run up on the beach: a flat steel hull, the bow ramp down on
// the sand, the wheelhouse aft. 2 x 4 tiles, bow towards +y.
VoxelModel makeLandingCraft(const FactionLook&) {
  VoxelModel m(24, 48, 18);
  m.box(2, 6, 0, 22, 44, 6, STEEL_DARK);                  // the hull
  m.box(3, 8, 2, 21, 42, 6, 0);                           // the well deck, open
  m.box(3, 8, 1, 21, 42, 2, STEEL);
  for (int y = 6; y < 44; ++y) {                          // gunwales
    m.box(2, y, 6, 3, y + 1, 9, STEEL_DARK);
    m.box(21, y, 6, 22, y + 1, 9, STEEL_DARK);
  }
  for (int i = 0; i < 6; ++i) m.box(4, 44 + i, std::max(0, 5 - i), 20, 45 + i, std::max(1, 6 - i), STEEL);  // the ramp, down
  m.box(4, 0, 4, 20, 8, 14, STEEL_DARK);                  // the wheelhouse aft
  m.box(6, 7, 10, 18, 8, 12, WINDOW);
  m.box(11, 2, 14, 13, 4, 17, STEEL);                     // a mast stub
  m.box(5, 30, 2, 9, 36, 5, CANVAS);                      // stores left aboard
  m.box(15, 18, 2, 19, 24, 4, CANVAS_DARK);
  weather(m, STEEL_DARK, SOOT, STEEL, 281, 12, 8);
  return m;
}

// A harbour crane on its portal: four legs straddling the quay rails, the cab
// and the jib over the water.
VoxelModel makeCrane(const FactionLook&) {
  VoxelModel m(24, 24, 70);
  for (int x : {3, 19})
    for (int y : {3, 19}) m.box(x, y, 0, x + 2, y + 2, 20, RUST);
  m.box(3, 3, 18, 21, 21, 22, RUST);                      // the portal
  m.box(7, 7, 22, 17, 17, 34, STEEL_DARK);                // the turntable and cab
  m.box(8, 16, 26, 16, 17, 30, WINDOW);
  for (int z = 34, y = 12; z < 60; z += 2, y += 2) m.box(10, std::min(y, 23), z, 14, std::min(y + 2, 24), z + 2, RUST);  // the jib
  m.box(11, 22, 30, 13, 24, 58, STEEL);                   // the hook cable
  m.box(10, 22, 28, 14, 24, 30, STEEL_DARK);
  m.box(8, 2, 22, 16, 7, 30, CONCRETE_DARK);              // counterweight
  weather(m, RUST, SOOT, STEEL, 283, 10, 6);
  return m;
}

// A U-boat pen: a vast low bunker of reinforced concrete with a thick roof
// and three bays open to the water. 6 x 5 tiles, the bays towards +y.
VoxelModel makeUboatPen(const FactionLook&) {
  VoxelModel m(72, 60, 30);
  m.box(0, 0, 0, 72, 60, 24, CONCRETE);
  for (int i = 0; i < 3; ++i) {                           // the bays
    const int x0 = 6 + i * 22;
    m.box(x0, 20, 0, x0 + 16, 60, 16, 0);
    m.box(x0 + 2, 24, 0, x0 + 14, 60, 2, STEEL_DARK);     // the dark water inside
    m.box(x0 + 5, 30, 2, x0 + 11, 56, 6, STEEL_DARK);     // a boat in its bay
    m.box(x0 + 7, 40, 6, x0 + 9, 46, 10, STEEL_DARK);     // its conning tower
  }
  m.box(0, 0, 24, 72, 60, 28, CONCRETE_DARK);             // the burster slab on the roof
  for (int x = 2; x < 70; x += 6) m.box(x, 2, 28, x + 3, 58, 29, CONCRETE);  // its ribs
  m.box(30, 0, 0, 42, 2, 12, STEEL_DARK);                 // the landward doors
  weather(m, CONCRETE, CONCRETE_DARK, SOOT, 285, 16, 6);
  return m;
}

// A coastal gun casemate: a squat concrete block with a wide embrasure and the
// long barrel of a heavy gun reaching out of it, earth heaped at its back.
VoxelModel makeCasemate(const FactionLook&) {
  VoxelModel m(36, 36, 20);
  m.box(2, 2, 0, 34, 34, 14, CONCRETE);
  m.box(0, 0, 0, 36, 10, 6, EARTH);                       // earth banked behind
  m.box(1, 1, 14, 35, 35, 17, CONCRETE_DARK);             // the roof slab, overhanging
  m.box(8, 33, 4, 28, 36, 10, SOOT);                      // the embrasure
  m.box(16, 20, 6, 20, 36, 9, STEEL_DARK);                // the gun
  m.box(17, 36, 7, 19, 36, 8, STEEL_DARK);
  m.box(14, 2, 0, 22, 3, 8, STEEL_DARK);                  // the rear door
  weather(m, CONCRETE, CONCRETE_DARK, SOOT, 287, 14, 6);
  return m;
}

// A polder pumping station: a brick engine house with tall arched windows, a
// chimney, and the outfall into the dyke.
VoxelModel makePumpingStation(const FactionLook&) {
  VoxelModel m(36, 36, 52);
  m.box(4, 6, 0, 32, 30, 18, BRICK);
  houseWalls(m, 4, 6, 32, 30, 18, BRICK, CONCRETE_LIGHT);
  for (int x = 8; x < 30; x += 6) m.box(x, 29, 4, x + 3, 30, 15, WINDOW);
  tiledRoof(m, 4, 6, 32, 30, 18);
  cylinder(m, 28.0f, 10.0f, 2.6f, 0, 48, BRICK_DARK);     // the chimney
  cylinder(m, 28.0f, 10.0f, 1.4f, 46, 48, 0);
  m.box(12, 30, 0, 24, 36, 4, CONCRETE_DARK);             // the outfall
  m.box(14, 32, 0, 22, 36, 2, STEEL_DARK);
  weather(m, BRICK, BRICK_DARK, SOOT, 289, 14, 6);
  return m;
}

// A lighthouse: a white tower with a red band, the lantern room on top.
VoxelModel makeLighthouse(const FactionLook&) {
  VoxelModel m(24, 24, 84);
  m.box(4, 4, 0, 20, 20, 3, CONCRETE_DARK);
  for (int z = 3; z < 66; ++z) {
    const float r = 7.0f - z * 0.05f;
    cylinder(m, 12.0f, 12.0f, r, z, z + 1, (z / 12) % 2 ? MARK_RED : MARK_WHITE);
  }
  cylinder(m, 12.0f, 12.0f, 5.0f, 66, 68, STEEL_DARK);    // the gallery
  cylinder(m, 12.0f, 12.0f, 3.2f, 68, 76, LAMP);          // the lantern
  cylinder(m, 12.0f, 12.0f, 3.6f, 76, 79, MARK_RED);      // its cap
  m.box(10, 4, 3, 14, 5, 10, WOOD_DARK);                  // the door
  return m;
}

// An oil derrick: a lattice tower over the well, a nodding pump beside it.
VoxelModel makeDerrick(const FactionLook&) {
  VoxelModel m(24, 24, 60);
  m.box(2, 2, 0, 22, 22, 1, CONCRETE_DARK);
  lattice(m, 12, 12, 1, 56, 8.5f, 1.5f, WOOD_DARK);
  m.box(10, 10, 54, 14, 14, 58, STEEL_DARK);              // the crown block
  m.box(15, 3, 1, 21, 6, 5, STEEL_DARK);                  // the pump and its beam
  m.box(14, 4, 7, 22, 5, 8, STEEL);
  m.box(17, 4, 5, 18, 5, 7, STEEL_DARK);
  m.box(2, 16, 1, 7, 21, 6, RUST);                        // a tank of crude
  m.box(3, 2, 0, 9, 7, 1, SOOT);                          // oil on the ground
  weather(m, WOOD_DARK, SOOT, WOOD, 271, 18, 4);
  return m;
}

// An oil refinery: a fractionating column, storage tanks, pipework and a flare.
VoxelModel makeOilRefinery(const FactionLook&) {
  VoxelModel m(48, 48, 64);
  m.box(1, 1, 0, 47, 47, 1, CONCRETE_DARK);
  for (int z = 1; z < 54; ++z) cylinder(m, 12, 12, z < 6 ? 4.5f : 3.4f, z, z + 1, (z % 8 == 0) ? STEEL_DARK : STEEL);
  for (int z = 8; z < 52; z += 8) cylinder(m, 12, 12, 4.4f, z, z + 1, RUST);   // platforms round the column
  for (int z = 1; z < 40; ++z) cylinder(m, 22, 8, 2.0f, z, z + 1, STEEL);
  for (int i = 0; i < 3; ++i)                                                   // storage tanks
    for (int z = 1; z < 14; ++z) cylinder(m, 12.0f + i * 13, 36, 5.6f, z, z + 1, (z == 13) ? CONCRETE : CONCRETE_LIGHT);
  for (int x = 12; x < 40; ++x) m.box(x, 20, 6, x + 1, 22, 8, RUST);           // pipe racks
  for (int y = 12; y < 36; ++y) m.box(38, y, 6, 40, y + 1, 8, RUST);
  m.box(30, 4, 1, 44, 16, 9, BRICK);                                            // the pump house
  tiledRoof(m, 30, 4, 44, 16, 9);
  for (int z = 1; z < 62; ++z) cylinder(m, 44, 44, 1.0f, z, z + 1, STEEL_DARK);  // the flare stack
  m.box(43, 43, 60, 46, 46, 63, MARK_RED);
  weather(m, STEEL, STEEL_DARK, SOOT, 283, 16, 4);
  return m;
}

}  // namespace pioneer::art

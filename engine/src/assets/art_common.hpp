#pragma once
// Slave Engine - shared pieces of the procedural art: the voxel palette, the
// faction looks, noise, a small modelling toolkit for vehicles, soldiers and
// props, and a parallel bake queue.
//
// Internal to the assets module (not installed with the public headers).

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "slave/assets/atlas.hpp"
#include "slave/assets/placeholder_art.hpp"
#include "slave/assets/voxel.hpp"

namespace slave::art {

// ---------------------------------------------------------------------------
// Voxel palette. Faction colours (BASE*, CAMO*, UNIFORM*, HELMET) change per
// faction; TEAM* entries are recoloured per player by the sprite shader.
// ---------------------------------------------------------------------------
enum : uint8_t {
  TEAM = 1, TEAM_DARK = 2, TEAM_LIGHT = 3,
  BASE = 4, BASE_DARK = 5, BASE_LIGHT = 6, UNIFORM = 7, UNIFORM_DARK = 8, UNIFORM_LIGHT = 9,
  STEEL_DARK = 10, STEEL = 11, TRACK_A = 12, TRACK_B = 13, RUBBER = 14, SKIN = 15,
  HELMET = 16, GLASS = 17, LAMP = 18, BOOT = 19,
  TRUNK = 20, LEAF_DARK = 21, LEAF = 22, LEAF_LIGHT = 23, PINE = 24, PINE_DARK = 25,
  ROCK = 26, ROCK_DARK = 27, ROCK_LIGHT = 28, BUSH = 29,
  // Structures.
  CONCRETE = 30, CONCRETE_DARK = 31, CONCRETE_LIGHT = 32, BRICK = 33, BRICK_DARK = 34,
  ROOF = 35, ROOF_DARK = 36, ROOF_LIGHT = 37, WOOD = 38, WOOD_DARK = 39, WINDOW = 40, WINDOW_LIT = 41,
  SANDBAG = 42, SANDBAG_DARK = 43, EARTH = 44, EARTH_DARK = 45, RUST = 46, TANK_PAINT = 47, TANK_PAINT_DARK = 48,
  CANVAS = 49, CANVAS_DARK = 50, SOOT = 51,
  // Vehicles in detail: camouflage, mud, dust, worn edges, markings.
  CAMO_A = 52, CAMO_A_DARK = 53, CAMO_B = 54, CAMO_B_DARK = 55, MUD = 56, MUD_DARK = 57, DUST = 58,
  CHIP = 59, MARK_WHITE = 60, MARK_BLACK = 61, MARK_RED = 62, EXHAUST = 63,
  // Soldiers in detail.
  HELMET_DARK = 64, LEATHER = 65, WEBBING = 66, STOCK = 67, BLANKET = 68, CAP = 69,
  // Biome props.
  SNOW = 70, SNOW_SHADE = 71, PALM = 72, PALM_DARK = 73, PALM_TRUNK = 74, DRY_GRASS = 75, DRY_DARK = 76,
  DEAD_WOOD = 77, JUNGLE = 78, JUNGLE_DARK = 79, JUNGLE_LIGHT = 80, SAND_ROCK = 81, SAND_ROCK_DARK = 82,
  PLASTER = 83, PLASTER_DARK = 84, TILE_ROOF = 85, TILE_ROOF_DARK = 86, CACTUS = 87, BIRCH = 88,
};

struct FactionLook {
  const char* id;
  Rgba base, uniform, helmet;
  bool sloped;     // Soviet armour: sloped plates and cast, rounded turrets
  bool camo;       // three-tone camouflage over the base colour
  Rgba camoA, camoB;
};

extern const FactionLook kFactions[2];
VoxelPalette makePalette(const FactionLook& f);

Rgba scale(Rgba c, float k);
uint8_t u8(float v);
float noise2(float x, float y, uint32_t seed);
float fbm2(float x, float y, uint32_t seed);
float noise3(float x, float y, float z, uint32_t seed);

constexpr float kTwoPi = 6.2831853f;
// Scale of one voxel, in tiles. Vehicles and infantry are modelled at
// different resolutions but the same true scale: a soldier stands about two
// thirds as tall as a medium tank, and a medium tank is a little over a tile
// long, so masses of units read as an army on a big battlefield.
constexpr float kVehicleVoxel = 1.0f / 36.0f;
constexpr float kInfantryVoxel = 1.0f / 56.0f;
constexpr float kStructureVoxel = 1.0f / 12.0f;

BakeOptions unitBake(float voxelTiles);

// --- Modelling toolkit ---------------------------------------------------------
// Random recolour of one material into two others (percentages).
void speckle(VoxelModel& m, uint8_t from, uint8_t a, uint8_t b, uint32_t seed, int pa, int pb);
void weather(VoxelModel& m, uint8_t from, uint8_t dark, uint8_t light, uint32_t seed, int pd = 18, int pl = 8);
// A vertical cylinder (axis along Z).
void cylinder(VoxelModel& m, float cx, float cy, float r, int z0, int z1, uint8_t c);
// Cylinders along X (gun barrels) and Y (wheels).
void cylX(VoxelModel& m, float x0, float x1, float cy, float cz, float r, uint8_t c);
void cylY(VoxelModel& m, float cx, float cz, float r, float y0, float y1, uint8_t c);
// The upper half of an ellipsoid sitting on z0 (cast turrets, domes).
void dome(VoxelModel& m, float cx, float cy, float z0, float rx, float ry, float h, uint8_t c);
// A slab whose outline changes with height: at each z in [z0,z1), fills the
// rectangle shape(z) returns (x0, x1, y0, y1 as floats). Sloped armour,
// tapering casemates, roofs.
struct Rect {
  float x0, x1, y0, y1;
};
template <class F>
void slab(VoxelModel& m, int z0, int z1, F shape, uint8_t c) {
  for (int z = z0; z < z1; ++z) {
    const Rect r = shape(z);
    for (int y = static_cast<int>(r.y0 + 0.5f); y < static_cast<int>(r.y1 + 0.5f); ++y)
      for (int x = static_cast<int>(r.x0 + 0.5f); x < static_cast<int>(r.x1 + 0.5f); ++x) m.set(x, y, z, c);
  }
}
// A box with sloped faces: each face leans in by the given voxels per voxel of
// height (front = +X, rear = -X, sides = +-Y).
void armourBox(VoxelModel& m, float x0, float x1, float y0, float y1, int z0, int z1, float front, float rear,
               float side, uint8_t c);
// A run of track with road wheels between x0 and x1 on the side [y0, y1),
// height h. `frame` moves the links and turns the wheels.
struct TrackSpec {
  float x0, x1, y0, y1;
  int height;            // top of the track run
  int wheels;            // road wheels per side
  float wheelR;          // road wheel radius
  bool returnRollers;    // small rollers under the top run (German style)
  bool raisedSprocket;   // drive sprocket higher than the wheels
};
void tracks(VoxelModel& m, const TrackSpec& t, int frame);
// A road wheel with tyre, painted disc and hub, at x along the side [y0, y1).
void roadWheel(VoxelModel& m, float cx, float cz, float r, float y0, float y1, int frame, uint8_t paint = BASE_DARK);
// Recolours BASE / BASE_DARK / BASE_LIGHT with soft camouflage patches.
void camouflage(VoxelModel& m, const FactionLook& look, uint32_t seed, float scale = 0.11f);
// Mud thrown up on the running gear and lower hull, dust on top faces, worn
// edges and faded paint: everything that makes a machine look used.
void grime(VoxelModel& m, uint32_t seed, int mudHeight, float amount = 1.0f);
// Stamps a small picture onto the outermost voxels of a side (+Y side when
// right, else -Y): rows top to bottom, 'W' white, 'B' black, 'R' red, 'T'
// team colour, '.' leaves the paint.
void stampSide(VoxelModel& m, float cx, int ztop, bool right, const std::vector<const char*>& rows, int yLimit = -1);
// Stamps onto the topmost voxels seen from above (roof markings, air ID).
void stampTop(VoxelModel& m, float cx, float cy, const std::vector<const char*>& rows);
// Recolours the outer shell of everything at heights [z0, z1) in team colour.
void teamBand(VoxelModel& m, int z0, int z1, int xMin = 0, int xMax = 1 << 20);
// The faction's national marking (plain cross or red star), for stampSide.
const std::vector<const char*>& nationalMark(const FactionLook& look, bool small = false);

// Copies voxels with z >= zMin into `top` and the rest into `bottom`.
void splitAt(const VoxelModel& m, int zMin, VoxelModel& bottom, VoxelModel& top);

// --- Soldiers ------------------------------------------------------------------------
enum class Kit : uint8_t { Rifle, Smg, MachineGun, RocketTube, AtRifle, ScopedRifle, Tools, Ammo };
enum class Pose : uint8_t { Walk, Kneel, Prone, Dead };
struct SoldierSpec {
  Kit kit = Kit::Rifle;
  Pose pose = Pose::Walk;
  int frame = 0;        // walk cycle 0..3
  bool cap = false;     // field cap instead of a helmet (snipers)
  bool smock = false;   // camouflage smock
};
// Draws one soldier facing +X with his feet centred at (ox, oy).
void soldier(VoxelModel& m, const FactionLook& look, float ox, float oy, const SoldierSpec& s);

// --- Units -------------------------------------------------------------------------
// A unit model in one animation frame: the hull and, if it has one, a turret
// that the game turns on its own around (pivotX, pivotY).
struct UnitModel {
  VoxelModel body;
  VoxelModel turret;  // empty when it has none
  float pivotX = 0, pivotY = 0;
  bool hasTurret() const { return turret.sizeX() > 0; }
};
using UnitBuilder = UnitModel (*)(const FactionLook&, int frame);
using CorpseBuilder = VoxelModel (*)(const FactionLook&, int variant);

// Bakes sprites on all cores, then adds them to the atlas in submission order.
class BakeQueue {
 public:
  explicit BakeQueue(AtlasBuilder& ab) : ab_(ab), base_(static_cast<int>(ab.count())) {}
  // Returns the atlas frame the sprite will get.
  int push(VoxelModel model, const VoxelPalette& pal, BakeOptions o, const VoxelModel* shadow = nullptr);
  int pushShadowed(VoxelModel model, VoxelModel shadow, const VoxelPalette& pal, BakeOptions o);
  void run();  // bakes everything and adds it to the atlas

 private:
  struct Job {
    VoxelModel model;
    VoxelModel shadow;
    bool ownShadow = false;
    const VoxelPalette* pal;
    BakeOptions o;
  };
  AtlasBuilder& ab_;
  int base_;
  std::deque<Job> jobs_;
  std::deque<VoxelPalette> palettes_;
};

// Queues a unit in 16 facings for each faction look.
void addUnit(BakeQueue& q, SpriteCatalog& cat, const std::string& name, UnitBuilder make, int animFrames,
             CorpseBuilder corpse = nullptr, float voxelTiles = kVehicleVoxel);

// Biome props and civilian buildings (world_models.cpp).
VoxelModel makeSnowPine(int v);
VoxelModel makeDeadTree(int v);
VoxelModel makePalm(int v);
VoxelModel makeScrub(int v);
VoxelModel makeJungleTree(int v);
VoxelModel makeDebris(int v);
VoxelModel makeSandRock(int v);
VoxelModel makeCottage(const FactionLook&);
VoxelModel makeCottageBombed(const FactionLook&);
VoxelModel makeTownhouse(const FactionLook&);
VoxelModel makeChurch(const FactionLook&);
VoxelModel makeRuin(const FactionLook&);
VoxelModel makeAdobe(const FactionLook&);

// The detailed vehicles and soldiers (unit_models.cpp).
void addUnitModels(BakeQueue& q, SpriteCatalog& cat);

}  // namespace slave::art

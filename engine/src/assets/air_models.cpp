// Pioneer Engine - placeholder art for call-ins (Milestone 9.14): aircraft in
// five roles, a warship and a landing craft. Each is baked in 16 facings for
// every faction look, without a ground shadow: the game draws planes high up
// and lays their shadow on the ground itself.
#include <algorithm>
#include <cmath>
#include <string>

#include "art_common.hpp"

namespace pioneer::art {

namespace {

// A roundel: red centre, white ring, dark rim (RAF), seen from above.
const std::vector<const char*>& roundel() {
  static const std::vector<const char*> r = {"..BBB..", ".BWWWB.", "BWWRWWB", "BWRRRWB", "BWWRWWB", ".BWWWB.", "..BBB.."};
  return r;
}
const std::vector<const char*>& wingMark(const FactionLook& look) { return look.british ? roundel() : nationalMark(look); }

struct PlaneSpec {
  int length = 54;     // nose to tail, voxels
  int span = 66;       // wing tip to wing tip
  float body = 3.5f;   // fuselage radius
  float wingAt = 0.58f;  // leading edge, fraction of length from the tail
  int chord = 11;      // wing chord at the root
  int engines = 0;     // 0: one in the nose; 2 or 4 on the wings
  bool glassNose = false;
  bool highWing = false;
  bool twinFin = false;
  bool gullWing = false;  // the dive bomber's cranked wing
  bool fixedGear = false;
};

// A propeller in the YZ plane at x, turned by `frame` (two blade positions).
void prop(VoxelModel& m, int x, float cy, float cz, float r, int frame) {
  const float a0 = frame ? 0.785f : 0.0f;
  for (int blade = 0; blade < 3; ++blade) {
    const float a = a0 + blade * 2.0944f;
    for (float t = 1.0f; t <= r; t += 0.5f)
      m.set(x, static_cast<int>(std::lround(cy + std::cos(a) * t)), static_cast<int>(std::lround(cz + std::sin(a) * t)), STEEL_DARK);
  }
  m.set(x + 1, static_cast<int>(cy), static_cast<int>(cz), STEEL);  // spinner
}

UnitModel plane(const FactionLook& look, int frame, const PlaneSpec& s, uint32_t seed) {
  const int GX = s.length + 2, GY = s.span + 2, GZ = static_cast<int>(s.body * 2 + 10);
  UnitModel um;
  um.body = VoxelModel(GX, GY, GZ);
  VoxelModel& m = um.body;
  const float cy = GY / 2.0f, cz = s.body + 2.0f;
  // Fuselage: thin at the tail, full at the wing, rounding off at the nose.
  for (int x = 0; x < s.length; ++x) {
    const float u = static_cast<float>(x) / s.length;
    const float r = s.body * (u < 0.55f ? 0.35f + 0.65f * u / 0.55f : u > 0.9f ? 1.0f - (u - 0.9f) * 3.0f : 1.0f);
    const float zc = cz + (u < 0.4f ? (0.4f - u) * s.body * 0.6f : 0.0f);  // the tail sweeps up a little
    for (int y = static_cast<int>(cy - r - 1); y <= static_cast<int>(cy + r + 1); ++y)
      for (int z = static_cast<int>(zc - r - 1); z <= static_cast<int>(zc + r + 1); ++z) {
        const float dy = y + 0.5f - cy, dz = z + 0.5f - zc;
        if (dy * dy + dz * dz <= r * r) m.set(x, y, z, dz < -r * 0.3f ? CONCRETE_LIGHT : BASE);
      }
  }
  // Wings: tapering to rounded tips.
  const int lead = static_cast<int>(s.length * s.wingAt);
  const int wz = s.highWing ? static_cast<int>(cz + s.body * 0.8f) : static_cast<int>(cz - s.body * 0.5f);
  const float half = s.span / 2.0f;
  for (int y = 1; y < GY - 1; ++y) {
    const float off = std::abs(y + 0.5f - cy) / half;  // 0 root .. 1 tip
    if (off > 1.0f) continue;
    const int c = std::max(3, static_cast<int>(s.chord * (1.0f - 0.5f * off) * std::sqrt(std::max(0.0f, 1.0f - off * off * off))));
    const int x1 = lead - static_cast<int>(off * 2);
    int z = wz;
    if (s.gullWing) z += off < 0.3f ? -static_cast<int>(off * 6) : static_cast<int>((off - 0.3f) * 4) - 2;
    else z += static_cast<int>(off * 2);  // dihedral
    for (int x = x1 - c; x <= x1; ++x) {
      m.set(x, y, z, BASE);
      m.set(x, y, z - 1, CONCRETE_LIGHT);  // pale undersides
    }
  }
  // Tailplane and fin.
  const int tail = std::max(4, s.length / 8);
  for (int y = static_cast<int>(cy - half * 0.36f); y <= static_cast<int>(cy + half * 0.36f); ++y) {
    const float off = std::abs(y + 0.5f - cy) / (half * 0.36f);
    for (int x = 1; x <= tail - static_cast<int>(off * 2); ++x) m.set(x, y, static_cast<int>(cz + s.body * 0.5f), BASE);
  }
  const int finH = static_cast<int>(s.body * 2.2f) + 2;
  for (int dyf : s.twinFin ? std::vector<int>{-static_cast<int>(half * 0.33f), static_cast<int>(half * 0.33f)} : std::vector<int>{0})
    for (int z = 0; z < finH; ++z)
      for (int x = 0; x < tail - z / 2; ++x) m.set(x, static_cast<int>(cy) + dyf, static_cast<int>(cz + s.body * 0.5f) + z, BASE);
  // Engines and propellers.
  if (s.engines == 0) {
    prop(m, s.length, cy, cz, s.body + 3.0f, frame);
  } else {
    const int per = s.engines / 2;
    for (int side : {-1, 1})
      for (int k = 0; k < per; ++k) {
        const float ey = cy + side * half * (per == 1 ? 0.33f : 0.25f + 0.22f * k);
        const int ex0 = lead - s.chord + 2, ex1 = std::min(lead + 6, GX - 3);
        cylX(m, static_cast<float>(ex0), static_cast<float>(ex1), ey, static_cast<float>(wz), 2.2f, BASE_DARK);
        prop(m, ex1, ey, static_cast<float>(wz), 4.5f, frame);
      }
  }
  // Canopy, glazed nose, gear.
  const int canopyX = s.glassNose ? lead + 2 : lead - 3;
  for (int x = canopyX - 4; x <= canopyX + 2; ++x)
    for (int y = static_cast<int>(cy - 1.5f); y <= static_cast<int>(cy + 1.5f); ++y) m.set(x, y, static_cast<int>(cz + s.body), GLASS);
  if (s.glassNose)
    for (int x = s.length - 4; x < s.length; ++x)
      for (int z = static_cast<int>(cz - 1); z <= static_cast<int>(cz + 1); ++z) m.set(x, static_cast<int>(cy), z, GLASS);
  if (s.fixedGear)
    for (int side : {-1, 1}) {
      m.box(lead - 6, static_cast<int>(cy + side * 6), 0, lead - 3, static_cast<int>(cy + side * 6) + 1, wz - 1, BASE_DARK);
      m.box(lead - 6, static_cast<int>(cy + side * 6), 0, lead - 3, static_cast<int>(cy + side * 6) + 1, 1, RUBBER);
    }
  // Markings on the wings and the side, a team band round the rear fuselage.
  stampTop(m, static_cast<float>(lead - s.chord / 2), cy - half * 0.62f, wingMark(look));
  stampTop(m, static_cast<float>(lead - s.chord / 2), cy + half * 0.62f, wingMark(look));
  teamBand(m, 0, GZ, tail + 2, tail + 4);
  camouflage(m, look, seed, 0.09f);
  um.pivotX = GX / 2.0f;
  um.pivotY = GY / 2.0f;
  return um;
}

UnitModel fighter(const FactionLook& look, int f) {
  PlaneSpec s;
  s.length = 46;
  s.span = 54;
  s.body = 3.2f;
  s.chord = 10;
  return plane(look, f, s, 9101);
}
UnitModel attacker(const FactionLook& look, int f) {
  PlaneSpec s;
  s.length = 52;
  s.span = 62;
  s.body = 3.8f;
  s.chord = 12;
  s.gullWing = !look.sloped && !look.british;  // the Stuka
  s.fixedGear = s.gullWing;
  return plane(look, f, s, 9102);
}
UnitModel bomber(const FactionLook& look, int f) {
  PlaneSpec s;
  s.length = 78;
  s.span = 108;
  s.body = 4.8f;
  s.chord = 16;
  s.engines = 2;
  s.glassNose = true;
  s.twinFin = look.british;  // the Mitchell
  return plane(look, f, s, 9103);
}
UnitModel transport(const FactionLook& look, int f) {
  PlaneSpec s;
  s.length = 82;
  s.span = 112;
  s.body = 6.0f;
  s.chord = 17;
  s.engines = 2;
  s.fixedGear = !look.sloped && !look.british;  // the Ju 52's spats
  return plane(look, f, s, 9104);
}
UnitModel recon(const FactionLook& look, int f) {
  PlaneSpec s;
  s.length = 48;
  s.span = 68;
  s.body = 3.0f;
  s.chord = 10;
  s.engines = look.sloped ? 0 : 2;  // the Po-2 has one engine; Fw 189 and Mosquito two
  s.highWing = look.sloped;
  s.twinFin = !look.sloped && !look.british;
  return plane(look, f, s, 9105);
}

// A destroyer or cruiser lying offshore: grey hull, superstructure, funnels, turrets.
UnitModel warship(const FactionLook& look, int) {
  const int GX = 150, GY = 22, GZ = 26;
  UnitModel um;
  um.body = VoxelModel(GX, GY, GZ);
  VoxelModel& m = um.body;
  const float cy = GY / 2.0f;
  for (int x = 0; x < GX; ++x) {
    const float u = x / static_cast<float>(GX);
    const float half = (GY / 2.0f - 1) * (u > 0.8f ? std::max(0.08f, (1.0f - u) / 0.2f) : u < 0.08f ? 0.6f + u * 5 : 1.0f);
    const int deck = 7 + (u > 0.75f ? static_cast<int>((u - 0.75f) * 10) : 0);  // the bow rises
    for (int y = static_cast<int>(cy - half); y <= static_cast<int>(cy + half); ++y)
      for (int z = 0; z < deck; ++z) m.set(x, y, z, z < 2 ? CONCRETE_DARK : CONCRETE);
  }
  m.box(52, 6, 7, 92, 16, 13, CONCRETE_LIGHT);   // superstructure
  m.box(86, 7, 13, 96, 15, 19, CONCRETE_LIGHT);  // bridge
  m.box(87, 7, 17, 96, 15, 18, GLASS);
  m.box(90, 10, 19, 92, 12, 25, STEEL_DARK);     // mast
  for (int fx : {62, 74}) {                       // funnels
    cylinder(m, static_cast<float>(fx), cy, 3.0f, 13, 20, CONCRETE_DARK);
    cylinder(m, static_cast<float>(fx), cy, 3.0f, 20, 21, SOOT);
  }
  for (int tx : {30, 44, 108, 122}) {             // gun turrets
    m.box(tx - 4, 7, 7, tx + 4, 15, 11, CONCRETE_DARK);
    const int dir = tx > 75 ? 1 : -1;
    cylX(m, static_cast<float>(tx), static_cast<float>(tx + dir * 12), cy - 1.5f, 9.5f, 0.6f, STEEL_DARK);
    cylX(m, static_cast<float>(tx), static_cast<float>(tx + dir * 12), cy + 1.5f, 9.5f, 0.6f, STEEL_DARK);
  }
  m.box(4, 10, 7, 6, 12, 14, WOOD_DARK);          // ensign staff
  m.box(4, 12, 11, 6, 17, 14, TEAM);
  stampSide(m, 120, 6, false, nationalMark(look, true));
  um.pivotX = GX / 2.0f;
  um.pivotY = GY / 2.0f;
  return um;
}

// A landing craft: a flat box with a bow ramp and men crouched in the well.
UnitModel landingCraft(const FactionLook& look, int f) {
  const int GX = 40, GY = 16, GZ = 12;
  UnitModel um;
  um.body = VoxelModel(GX, GY, GZ);
  VoxelModel& m = um.body;
  m.box(0, 0, 0, GX - 3, GY, 3, CONCRETE_DARK);
  m.box(0, 0, 3, GX - 3, 1, 7, CONCRETE);
  m.box(0, GY - 1, 3, GX - 3, GY, 7, CONCRETE);
  m.box(0, 0, 3, 6, GY, 8, CONCRETE);            // the stern, with the cox'n's position
  m.box(2, 5, 8, 5, 11, 10, CONCRETE_LIGHT);
  m.box(GX - 3, 1, 1, GX - 1, GY - 1, 8, CONCRETE);  // the ramp, up
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < 4; ++c) {
      const int x = 9 + c * 6, y = 3 + r * 4;
      m.box(x, y, 3, x + 2, y + 2, 5, UNIFORM);
      m.box(x, y, 5, x + 2, y + 2, 6, HELMET);
    }
  m.box(1, 6, 1, 2, 10, 2, f ? MARK_WHITE : CONCRETE_DARK);  // wash at the stern
  m.box(6, 1, 7, 8, 2, 9, TEAM);
  (void)look;
  um.pivotX = GX / 2.0f;
  um.pivotY = GY / 2.0f;
  return um;
}

// A parachute: a canopy with its shroud lines gathered at the foot of the
// sprite, where the man or crate hangs.
UnitModel parachute(const FactionLook&, int) {
  const int G = 22, GZ = 30;
  UnitModel um;
  um.body = VoxelModel(G, G, GZ);
  VoxelModel& m = um.body;
  const float c = G / 2.0f;
  for (int z = 22; z < GZ; ++z)
    for (int y = 0; y < G; ++y)
      for (int x = 0; x < G; ++x) {
        const float dx = x + 0.5f - c, dy = y + 0.5f - c, rr = 10.0f * std::sqrt(std::max(0.0f, 1.0f - (z - 22) / 8.0f));
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d <= rr && d > rr - 1.6f) m.set(x, y, z, ((static_cast<int>(std::atan2(dy, dx) * 2.6f) & 1) ? CANVAS : CANVAS_DARK));
        if (z == GZ - 1 && d <= rr) m.set(x, y, z, CANVAS);
      }
  for (int k = 0; k < 8; ++k) {  // shroud lines
    const float a = k * 0.785f;
    for (int z = 4; z < 22; ++z) {
      const float t = (z - 4) / 18.0f;
      m.set(static_cast<int>(c + std::cos(a) * 9.0f * t), static_cast<int>(c + std::sin(a) * 9.0f * t), z, STEEL_DARK);
    }
  }
  um.pivotX = c;
  um.pivotY = c;
  return um;
}

void addFlying(BakeQueue& q, SpriteCatalog& cat, const std::string& name, UnitBuilder make, int frames, float voxelTiles,
               bool shadow) {
  for (const FactionLook& look : kFactions) {
    if (cat.units.count(name + "@" + look.id)) continue;  // an art pack provides it
    const VoxelPalette pal = makePalette(look);
    UnitArt art;
    art.body.facings = 16;
    art.body.animFrames = frames;
    for (int a = 0; a < frames; ++a) {
      const UnitModel um = make(look, a);
      for (int f = 0; f < 16; ++f) {
        BakeOptions o = unitBake(voxelTiles);
        o.yawRadians = static_cast<float>(f) * kTwoPi / 16.0f;
        o.pivotX = um.body.sizeX() / 2.0f;
        o.pivotY = um.body.sizeY() / 2.0f;
        o.shadow = shadow;
        o.shadowAlpha = 70;
        const int idx = q.push(um.body, pal, o);
        if (a == 0 && f == 0) art.body.first = idx;
      }
    }
    cat.units[name + "@" + look.id] = art;
  }
}

}  // namespace

void addCallInModels(BakeQueue& q, SpriteCatalog& cat) {
  addFlying(q, cat, "plane_fighter", &fighter, 2, kVehicleVoxel, false);
  addFlying(q, cat, "plane_attack", &attacker, 2, kVehicleVoxel, false);
  addFlying(q, cat, "plane_bomber", &bomber, 2, kVehicleVoxel, false);
  addFlying(q, cat, "plane_transport", &transport, 2, kVehicleVoxel, false);
  addFlying(q, cat, "plane_recon", &recon, 2, kVehicleVoxel, false);
  addFlying(q, cat, "warship", &warship, 1, kVehicleVoxel, false);
  addFlying(q, cat, "landing_boat", &landingCraft, 2, kVehicleVoxel, false);
  addFlying(q, cat, "parachute", &parachute, 1, kInfantryVoxel * 1.4f, false);
}

}  // namespace pioneer::art

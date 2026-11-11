// Formations (Milestone 9.5): groups march as one body at the pace of their
// slowest member, keep their shape on the way, squeeze through gaps, stand to
// their front, and turn back to it after a fight.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "slave/map/map.hpp"
#include "slave/sim/simulation.hpp"

using namespace slave;

namespace {
constexpr int T = 256;
struct Rules {
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  Rules() {
    std::string err;
    const std::string dir = LEGION_RULES_DIR;
    REQUIRE(weapons.loadFile(dir + "weapons.json", err));
    REQUIRE(types.loadFile(dir + "units.json", err));
    REQUIRE(types.loadFile(dir + "buildings.json", err));
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
  }
  uint16_t t(const char* id) const {
    const int i = types.find(id);
    REQUIRE(i >= 0);
    return static_cast<uint16_t>(i);
  }
};
std::vector<Entity> spawn(Simulation& sim, uint16_t type, uint8_t owner, const std::vector<Vec2i>& at, Angle facing = 0) {
  Command c;
  c.type = CommandType::SpawnList;
  for (const Vec2i& p : at) c.spawns.push_back({type, owner, p, facing, -1});
  sim.queue(c);
  sim.tick();
  std::vector<Entity> out;
  for (const auto& r : sim.spawnLog()) out.push_back(r.entity);
  return out;
}
void move(Simulation& sim, const std::vector<Entity>& units, Vec2i to, Formation f = Formation::Box,
          CommandType type = CommandType::Move, int32_t facing = -1, int32_t width = 0) {
  Command c;
  c.type = type;
  c.units = units;
  c.target = to;
  c.formation = f;
  c.formFacing = facing;
  c.formWidth = width;
  sim.queue(c);
}
Vec2i at(Simulation& sim, Entity e) {
  const CPosition& p = sim.registry().get<CPosition>(e);
  return {p.x, p.y};
}
// How far the group is spread: the longest distance of a member from the middle.
int32_t spread(Simulation& sim, const std::vector<Entity>& units) {
  int64_t cx = 0, cy = 0;
  for (Entity e : units) {
    cx += at(sim, e).x;
    cy += at(sim, e).y;
  }
  cx /= static_cast<int64_t>(units.size());
  cy /= static_cast<int64_t>(units.size());
  int32_t worst = 0;
  for (Entity e : units)
    worst = std::max(worst, static_cast<int32_t>(distance(at(sim, e).x - static_cast<int32_t>(cx), at(sim, e).y - static_cast<int32_t>(cy))));
  return worst;
}
bool idle(Simulation& sim, const std::vector<Entity>& units) {
  for (Entity e : units)
    if (sim.registry().get<COrder>(e).type != OrderType::Idle) return false;
  return true;
}
std::vector<Vec2i> grid(Vec2i centre, int n, int cols, int step) {
  std::vector<Vec2i> out;
  for (int i = 0; i < n; ++i) out.push_back({centre.x + (i % cols) * step, centre.y + (i / cols) * step});
  return out;
}
}  // namespace

TEST_CASE("tanks and infantry ordered together march as one body and arrive together") {
  Rules R;
  Map m(128, 64, 100);
  Simulation sim(m, R.types, R.weapons, 3);
  std::vector<Entity> group = spawn(sim, R.t("ger_medium_tank"), 0, grid({12 * T, 28 * T}, 4, 2, 400));
  const std::vector<Entity> men = spawn(sim, R.t("ger_rifleman"), 0, grid({10 * T, 34 * T}, 12, 4, 160));
  group.insert(group.end(), men.begin(), men.end());
  move(sim, group, {70 * T, 32 * T});
  sim.tick();
  REQUIRE(sim.formationOf(group.front()) != nullptr);
  int first = 0, last = 0, worst = 0;
  for (int i = 1; i <= 15 * 240 && !last; ++i) {
    sim.tick();
    int arrived = 0;
    for (Entity e : group) arrived += at(sim, e).x > 64 * T;
    if (arrived && !first) first = i;
    if (arrived == static_cast<int>(group.size())) last = i;
    if (i > 15 * 6 && !first) worst = std::max(worst, spread(sim, group));
  }
  REQUIRE(last > 0);
  // The tanks could be there in a third of the time; they hold their pace for the men.
  CHECK(last - first < 15 * 12);
  CHECK(worst < 5 * T);  // never strung out along the road
  for (int i = 0; i < 15 * 20 && !idle(sim, group); ++i) sim.tick();
  CHECK(idle(sim, group));
  CHECK(sim.formationOf(group.front()) == nullptr);
  // Armour in front (east), men behind, everyone facing the way they came.
  int32_t tankX = 0, menX = 0;
  for (size_t i = 0; i < 4; ++i) tankX += at(sim, group[i]).x / 4;
  for (Entity e : men) menX += at(sim, e).x / 12;
  CHECK(tankX > menX + T);
  for (Entity e : group) CHECK(facingOf(sim.registry().get<CFacing>(e).angle) == 0);
}

TEST_CASE("a line stays a line on the move") {
  Rules R;
  Map m(128, 64, 100);
  Simulation sim(m, R.types, R.weapons, 4);
  const std::vector<Entity> tanks = spawn(sim, R.t("rus_medium_tank"), 0, grid({10 * T, 30 * T}, 6, 3, 420));
  move(sim, tanks, {80 * T, 32 * T}, Formation::Line);
  for (int i = 0; i < 15 * 12; ++i) sim.tick();  // formed up and under way
  for (int s = 0; s < 8; ++s) {
    for (int i = 0; i < 15 * 2; ++i) sim.tick();
    int32_t x0 = INT32_MAX, x1 = 0, y0 = INT32_MAX, y1 = 0;
    for (Entity e : tanks) {
      x0 = std::min(x0, at(sim, e).x);
      x1 = std::max(x1, at(sim, e).x);
      y0 = std::min(y0, at(sim, e).y);
      y1 = std::max(y1, at(sim, e).y);
    }
    INFO("sample " << s << ": along " << (x1 - x0) << ", across " << (y1 - y0));
    CHECK(x1 - x0 < T);           // abreast: nobody more than a tile ahead of anyone
    CHECK(y1 - y0 > 5 * 250);     // six tanks side by side
    CHECK(y1 - y0 < 5 * 340);
    for (Entity e : tanks) CHECK(facingOf(sim.registry().get<CFacing>(e).angle) == 0);
  }
  CHECK(sim.shapeOf(tanks.front()) == Formation::Line);
}

TEST_CASE("the body sets off without a straggler, who catches up") {
  Rules R;
  Map m(128, 64, 100);
  Simulation sim(m, R.types, R.weapons, 5);
  std::vector<Entity> group = spawn(sim, R.t("gbr_medium_tank"), 0, grid({30 * T, 30 * T}, 4, 2, 400));
  const Entity far = spawn(sim, R.t("gbr_medium_tank"), 0, {{8 * T, 32 * T}}).front();
  group.push_back(far);
  move(sim, group, {90 * T, 32 * T});
  for (int i = 0; i < 15 * 6; ++i) sim.tick();
  CHECK(at(sim, group[0]).x > 34 * T);  // the four didn't wait for the fifth
  int joined = 0;
  for (int i = 0; i < 15 * 120 && !joined; ++i) {
    sim.tick();
    if (spread(sim, group) < 3 * T) joined = i;
  }
  CHECK(joined > 0);  // and the fifth closed up on the way
}

TEST_CASE("a formation squeezes through a gap and opens out again") {
  Rules R;
  Map m(96, 64, 100);
  for (int y = 0; y <= 64; ++y)
    if (y < 30 || y > 33) m.setCorner(48, y, 500);  // a ridge with a three-tile gap
  m.recomputeInfo();
  Simulation sim(m, R.types, R.weapons, 6);
  const std::vector<Entity> men = spawn(sim, R.t("rus_rifleman"), 0, grid({20 * T, 26 * T}, 24, 6, 160));
  move(sim, men, {75 * T, 32 * T}, Formation::Line);
  int done = 0;
  for (int i = 1; i <= 15 * 400 && !done; ++i) {
    sim.tick();
    if (idle(sim, men)) done = i;
  }
  REQUIRE(done > 0);
  for (Entity e : men) CHECK(at(sim, e).x > 68 * T);  // everyone got through
  int32_t y0 = INT32_MAX, y1 = 0;
  for (Entity e : men) {
    y0 = std::min(y0, at(sim, e).y);
    y1 = std::max(y1, at(sim, e).y);
  }
  CHECK(y1 - y0 > 6 * 150);  // and stands in line again, wider than the gap
}

TEST_CASE("a new order, a halt or a death takes a unit out of the march") {
  Rules R;
  Map m(128, 64, 100);
  Simulation sim(m, R.types, R.weapons, 7);
  const std::vector<Entity> tanks = spawn(sim, R.t("ger_light_tank"), 0, grid({20 * T, 30 * T}, 5, 5, 300));
  move(sim, tanks, {90 * T, 32 * T}, Formation::Wedge);
  for (int i = 0; i < 15 * 5; ++i) sim.tick();
  const CFormation* f = sim.formationOf(tanks[0]);
  REQUIRE(f != nullptr);
  CHECK(f->members.size() == 5);
  Command stop;
  stop.type = CommandType::Stop;
  stop.units = {tanks[0]};
  sim.queue(stop);
  move(sim, {tanks[1]}, {20 * T, 50 * T});
  Command kill;
  kill.type = CommandType::Kill;
  kill.units = {tanks[2]};
  sim.queue(kill);
  for (int i = 0; i < 4; ++i) sim.tick();
  CHECK(sim.formationOf(tanks[0]) == nullptr);
  CHECK(sim.formationOf(tanks[1]) == nullptr);
  f = sim.formationOf(tanks[3]);
  REQUIRE(f != nullptr);
  CHECK(f->members.size() == 2);
  const Vec2i stopped = at(sim, tanks[0]);
  for (int i = 0; i < 15 * 5; ++i) sim.tick();
  CHECK(at(sim, tanks[0]) == stopped);
  CHECK(at(sim, tanks[3]).x > 30 * T);  // the rest march on
  CHECK(at(sim, tanks[1]).y > 36 * T);  // the one sent elsewhere goes there
  // Stopping everyone ends the march altogether.
  stop.units = tanks;
  sim.queue(stop);
  for (int i = 0; i < 3; ++i) sim.tick();
  CHECK(sim.registry().pool<CFormation>().size() == 0);
}

TEST_CASE("a march saved half way continues exactly") {
  Rules R;
  auto build = [&](Map& m, Simulation& sim) {
    std::vector<Entity> group = spawn(sim, R.t("ger_medium_tank"), 0, grid({12 * T, 28 * T}, 4, 2, 400));
    const std::vector<Entity> men = spawn(sim, R.t("ger_rifleman"), 0, grid({10 * T, 34 * T}, 9, 3, 160));
    group.insert(group.end(), men.begin(), men.end());
    move(sim, group, {36 * T, 40 * T}, Formation::Wedge);
    (void)m;
  };
  Map m1(96, 64, 100), m2(96, 64, 100);
  Simulation a(m1, R.types, R.weapons, 11);
  build(m1, a);
  for (int i = 0; i < 15 * 10; ++i) a.tick();
  REQUIRE(a.registry().pool<CFormation>().size() == 1);
  ByteWriter w;
  a.saveState(w);
  Simulation b(m2, R.types, R.weapons, 99);
  ByteReader r(w.bytes());
  std::string err;
  REQUIRE(b.loadState(r, err));
  CHECK(b.stateHash() == a.stateHash());
  CHECK(b.registry().pool<CFormation>().size() == 1);
  for (int i = 0; i < 15 * 60; ++i) {
    a.tick();
    b.tick();
  }
  CHECK(b.stateHash() == a.stateHash());
  CHECK(a.registry().pool<CFormation>().size() == 0);  // and it arrives
}

TEST_CASE("a dragged front: the first rank stands on the line, facing as dragged") {
  Rules R;
  Map m(96, 96, 100);
  Simulation sim(m, R.types, R.weapons, 8);
  const std::vector<Entity> men = spawn(sim, R.t("gbr_rifleman"), 0, grid({20 * T, 20 * T}, 12, 4, 160));
  // A front 12 tiles east, facing south (down the map), ten slots wide.
  const Angle south = ANGLE_FULL / 4;
  move(sim, men, {40 * T, 30 * T}, Formation::Line, CommandType::Move, south, 9 * 150);
  const std::vector<Vec2i> ghost = sim.formationPreview(men, {40 * T, 30 * T}, Formation::Line, south, 9 * 150);
  REQUIRE(ghost.size() == men.size());
  sim.tick();
  for (int i = 0; i < 15 * 120 && !idle(sim, men); ++i) sim.tick();
  REQUIRE(idle(sim, men));
  for (Entity e : men) {
    CHECK(facingOf(sim.registry().get<CFacing>(e).angle) == facingOf(south));
    // Each man stands on one of the slots the preview showed.
    uint32_t best = UINT32_MAX;
    for (const Vec2i& g : ghost) best = std::min(best, distance(g.x - at(sim, e).x, g.y - at(sim, e).y));
    CHECK(best < 40);
  }
  int front = 0;
  for (Entity e : men) front += std::abs(at(sim, e).y - 30 * T) < 40;
  CHECK(front == 10);  // ten on the line, two behind it
}

TEST_CASE("after a fight a unit turns back to the front it was given") {
  Rules R;
  Map m(64, 64, 100);
  Simulation sim(m, R.types, R.weapons, 9);
  const std::vector<Entity> guns = spawn(sim, R.t("ger_tank_destroyer"), 0, grid({20 * T, 30 * T}, 2, 1, 500));
  move(sim, guns, {30 * T, 32 * T});  // they end up facing east
  sim.tick();
  for (int i = 0; i < 15 * 60 && !idle(sim, guns); ++i) sim.tick();
  REQUIRE(idle(sim, guns));
  for (Entity e : guns) REQUIRE(facingOf(sim.registry().get<CFacing>(e).angle) == 0);
  // A lorry appears to the south; having no turret they turn to shoot it.
  const Entity prey = spawn(sim, R.t("rus_mcv"), 1, {{30 * T, 39 * T}}).front();
  bool turned = false;
  for (int i = 0; i < 15 * 40 && sim.registry().alive(prey); ++i) {
    sim.tick();
    turned = turned || facingOf(sim.registry().get<CFacing>(guns[0]).angle) != 0;
  }
  CHECK(turned);
  CHECK_FALSE(sim.registry().alive(prey));
  for (int i = 0; i < 15 * 8; ++i) sim.tick();
  for (Entity e : guns) CHECK(facingOf(sim.registry().get<CFacing>(e).angle) == 0);  // back to the east
}

TEST_CASE("a platoon sent a short way back reverses as a body, guns to the front") {
  Rules R;
  Map m(64, 64, 100);
  Simulation sim(m, R.types, R.weapons, 10);
  const std::vector<Entity> tanks = spawn(sim, R.t("ger_medium_tank"), 0, grid({30 * T, 28 * T}, 3, 1, 400));
  move(sim, tanks, {26 * T, 30 * T});  // four tiles west, behind them
  bool moved = false;
  for (int i = 0; i < 15 * 30 && !(moved && idle(sim, tanks)); ++i) {
    sim.tick();
    moved = moved || at(sim, tanks[0]).x < 29 * T;
    for (Entity e : tanks) {  // never turn their backs: at most a notch of steering either way
      const int f = facingOf(sim.registry().get<CFacing>(e).angle);
      CHECK((f == 0 || f == 1 || f == 15));
    }
  }
  CHECK(moved);
  CHECK(idle(sim, tanks));
  for (Entity e : tanks) CHECK(at(sim, e).x < 28 * T);
}

TEST_CASE("an attack-move formation halts to fight together, then goes on") {
  Rules R;
  Map m(128, 64, 100);
  Simulation sim(m, R.types, R.weapons, 12);
  const std::vector<Entity> tanks = spawn(sim, R.t("ger_medium_tank"), 0, grid({10 * T, 30 * T}, 4, 2, 400));
  const std::vector<Entity> foe = spawn(sim, R.t("rus_halftrack"), 1, {{50 * T, 34 * T}}, ANGLE_FULL / 2);
  Command hold;
  hold.type = CommandType::SetStance;
  hold.units = foe;
  hold.stance = Stance::HoldGround;
  sim.queue(hold);
  move(sim, tanks, {100 * T, 32 * T}, Formation::Line, CommandType::AttackMove);
  int killedAt = 0;
  int32_t spreadAtKill = 0;
  for (int i = 1; i <= 15 * 200 && !killedAt; ++i) {
    sim.tick();
    if (!sim.registry().alive(foe[0])) {
      killedAt = i;
      spreadAtKill = spread(sim, tanks);
    }
  }
  REQUIRE(killedAt > 0);
  for (Entity e : tanks) CHECK(sim.registry().alive(e));
  CHECK(spreadAtKill < 4 * T);  // they fought it as a body, not one by one
  for (int i = 0; i < 15 * 200 && !idle(sim, tanks); ++i) sim.tick();
  CHECK(idle(sim, tanks));
  for (Entity e : tanks) CHECK(at(sim, e).x > 95 * T);  // and carried on to where they were sent
}

TEST_CASE("a column follows its route: every rank takes the road the head took") {
  Rules R;
  Map m(96, 64, 100);
  for (int y = 0; y <= 64; ++y)
    if (y < 31 || y > 32) m.setCorner(48, y, 500);  // a ridge with a gap one tile wide, like a bridge
  m.recomputeInfo();
  Simulation sim(m, R.types, R.weapons, 13);
  const std::vector<Entity> tanks = spawn(sim, R.t("ger_medium_tank"), 0, grid({16 * T, 14 * T}, 8, 4, 400));
  move(sim, tanks, {78 * T, 46 * T}, Formation::Column);
  sim.tick();
  const CFormation* f = sim.formationOf(tanks.front());
  REQUIRE(f != nullptr);
  // The route: where it started, then its waypoints.
  std::vector<Vec2i> route{f->start};
  route.insert(route.end(), f->path.begin(), f->path.end());
  REQUIRE(route.size() >= 3);  // it has to bend to get through the gap
  auto offRoute = [&](Vec2i p) {
    double best = 1e18;
    for (size_t i = 1; i < route.size(); ++i) {
      const double ax = route[i - 1].x, ay = route[i - 1].y, bx = route[i].x, by = route[i].y;
      const double len2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
      double t = len2 > 0 ? ((p.x - ax) * (bx - ax) + (p.y - ay) * (by - ay)) / len2 : 0;
      t = std::clamp(t, 0.0, 1.0);
      const double qx = ax + t * (bx - ax), qy = ay + t * (by - ay);
      best = std::min(best, (p.x - qx) * (p.x - qx) + (p.y - qy) * (p.y - qy));
    }
    return static_cast<int32_t>(std::sqrt(best));
  };
  int done = 0, worst = 0;
  for (int i = 1; i <= 15 * 300 && !done; ++i) {
    sim.tick();
    const CFormation* on = sim.formationOf(tanks.front());
    if (i > 15 * 15 && on && on->remaining > 5 * T)  // formed up, and the head not yet past the end of the route
      for (Entity e : tanks) worst = std::max(worst, offRoute(at(sim, e)));
    if (idle(sim, tanks)) done = i;
  }
  REQUIRE(done > 0);
  INFO("farthest off the route: " << worst);
  CHECK(worst < 220);  // two abreast on the road all the way: no tail swinging wide at the corners
  for (Entity e : tanks) CHECK(at(sim, e).x > 70 * T);
}

TEST_CASE("a formation sent to the edge of the map stands inside it, in shape") {
  Rules R;
  Map m(64, 64, 100);
  Simulation sim(m, R.types, R.weapons, 14);
  const std::vector<Entity> men = spawn(sim, R.t("rus_rifleman"), 0, grid({20 * T, 20 * T}, 16, 4, 160));
  move(sim, men, {63 * T + 200, 30 * T}, Formation::Loose);
  sim.tick();
  for (int i = 0; i < 15 * 200 && !idle(sim, men); ++i) sim.tick();
  REQUIRE(idle(sim, men));
  uint32_t nearest = UINT32_MAX;
  for (size_t i = 0; i < men.size(); ++i) {
    CHECK(at(sim, men[i]).x < 64 * T - 64);
    for (size_t j = i + 1; j < men.size(); ++j)
      nearest = std::min(nearest, distance(at(sim, men[i]).x - at(sim, men[j]).x, at(sim, men[i]).y - at(sim, men[j]).y));
  }
  CHECK(nearest > 200);  // nobody squashed against the edge
}

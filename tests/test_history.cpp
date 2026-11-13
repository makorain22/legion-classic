#include <doctest/doctest.h>

#include "history.hpp"
#include "pioneer/core/rng.hpp"

using namespace pioneer;
using legion::History;

namespace {
// A brush-like edit across tiles [x0..x1] x [y0..y1], recorded in history.
void edit(History& h, Map& m, int x0, int y0, int x1, int y1, uint16_t add, Layer layer) {
  h.touchTerrain(m, x0, y0, x1, y1);
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) m.setCorner(x, y, static_cast<uint16_t>(m.corner(x, y) + add));
  for (int y = y0 * 2; y < y1 * 2; ++y)
    for (int x = x0 * 2; x < x1 * 2; ++x) m.paint(x, y, layer, 120);
}
}  // namespace

TEST_CASE("undo and redo restore terrain exactly, across chunk borders") {
  Map m(96, 64);
  m.generateCountryside(3);
  const Map original = m;
  History h;

  h.begin("raise");
  edit(h, m, 20, 20, 40, 40, 30, Layer::Dirt);  // spans chunks (0,0),(1,0),(0,1),(1,1)
  edit(h, m, 28, 28, 70, 50, 15, Layer::Mud);   // overlaps, reaches chunk (2,1)
  h.end();
  const Map afterFirst = m;

  h.begin("map edge");
  edit(h, m, 80, 50, 96, 64, 50, Layer::Rock);  // touches the far corners of the map
  h.end();
  const Map afterSecond = m;

  REQUIRE(h.canUndo());
  h.undo(m);
  CHECK(m.heights() == afterFirst.heights());
  CHECK(m.splatData() == afterFirst.splatData());
  h.undo(m);
  CHECK(m.heights() == original.heights());
  CHECK(m.splatData() == original.splatData());
  CHECK_FALSE(h.canUndo());

  h.redo(m);
  h.redo(m);
  CHECK(m.heights() == afterSecond.heights());
  CHECK(m.splatData() == afterSecond.splatData());
}

TEST_CASE("props and water level are undoable; empty steps are dropped") {
  Map m(32, 32);
  History h;
  h.begin("nothing");
  h.end();
  CHECK_FALSE(h.canUndo());

  h.begin("trees");
  h.touchProps(m);
  m.props().push_back({PropKind::Tree, 0, 100, 100});
  m.props().push_back({PropKind::Rock, 1, 300, 300});
  h.end();
  h.begin("water");
  h.touchWater(m);
  m.setWaterLevel(400);
  h.end();

  h.undo(m);
  CHECK(m.waterLevel() == 0);
  CHECK(m.props().size() == 2);
  h.undo(m);
  CHECK(m.props().empty());
  h.redo(m);
  CHECK(m.props().size() == 2);
  h.redo(m);
  CHECK(m.waterLevel() == 400);
}

TEST_CASE("a new step clears the redo stack") {
  Map m(32, 32);
  History h;
  h.begin("a");
  h.touchWater(m);
  m.setWaterLevel(10);
  h.end();
  h.undo(m);
  CHECK(h.canRedo());
  h.begin("b");
  h.touchWater(m);
  m.setWaterLevel(20);
  h.end();
  CHECK_FALSE(h.canRedo());
}

TEST_CASE("actions share the undo stack with terrain steps, in order") {
  struct Counter : History::Action {
    int* value;
    explicit Counter(int* v) : value(v) {}
    void undo() override { --*value; }
    void redo() override { ++*value; }
  };
  Map m(64, 64);
  History h;
  int units = 1;  // the caller already did the action once
  h.push("Place units", std::make_shared<Counter>(&units));
  h.begin("brush");  // a terrain step on top
  edit(h, m, 0, 0, 3, 3, 500, Layer::Dirt);
  h.end();
  CHECK(h.undoLabel() == "brush");
  h.undo(m);
  CHECK(units == 1);
  h.undo(m);
  CHECK(units == 0);
  CHECK_FALSE(h.canUndo());
  h.redo(m);
  CHECK(units == 1);
  h.push("Again", std::make_shared<Counter>(&units));  // a new step drops the redo branch
  CHECK_FALSE(h.canRedo());
}

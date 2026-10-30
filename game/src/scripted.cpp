// Legion Classic - scripted sessions for automated screenshots and self-tests
// (--editor-demo, --live-demo, --editor-selftest, --economy-demo).
#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "editor.hpp"
#include "game_scene.hpp"
#include "slave/core/log.hpp"

using namespace slave;

namespace legion {

// A scripted editing session used by the automated screenshot tests: sculpts
// a hill, paints a track and a field, plants a wood, then undoes the last step.
void GameScene::runEditorDemo() {
  const Vec2f c = groundAt({app_.pixelWidth() * 0.62f, app_.pixelHeight() * 0.55f});
  const float T = static_cast<float>(LEPTONS_PER_TILE);
  auto stroke = [&](Tool tool, float radius, float strength, Vec2f from, Vec2f to, int steps) {
    editor_->setTool(tool);
    editor_->setBrush(radius, strength);
    editor_->beginStroke();
    for (int i = 0; i <= steps; ++i) {
      const float t = steps ? static_cast<float>(i) / steps : 0.0f;
      editor_->strokeAt({from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t}, 1.0f / 15.0f);
    }
    editor_->endStroke();
  };
  if (frame_ == 2) {
    editor_->setActive(true);
    stroke(Tool::Raise, 7, 60, {c.x, c.y}, {c.x, c.y}, 40);
    stroke(Tool::Smooth, 9, 80, {c.x, c.y}, {c.x, c.y}, 10);
    editor_->setPaintLayer(Layer::Road);
    stroke(Tool::Paint, 1.2f, 100, {c.x - 14 * T, c.y + 10 * T}, {c.x + 6 * T, c.y - 6 * T}, 60);
    editor_->setPaintLayer(Layer::Dirt);
    stroke(Tool::Paint, 4, 60, {c.x + 8 * T, c.y + 8 * T}, {c.x + 12 * T, c.y + 6 * T}, 12);
    stroke(Tool::Props, 5, 100, {c.x - 8 * T, c.y - 4 * T}, {c.x - 6 * T, c.y - 2 * T}, 30);
    stroke(Tool::Lower, 3, 100, {c.x + 10 * T, c.y - 10 * T}, {c.x + 10 * T, c.y - 10 * T}, 20);
    editor_->undo();  // takes the last dip back out
    editor_->save(mapsDir_ + "editor_demo.lmap");
  }
}

// Drives the real editor through place / undo / redo / delete / move / give
// and checks the battlefield after each step. Run with --editor-selftest.
void GameScene::runEditorSelfTest() {
  Registry& reg = sim_->registry();
  auto count = [&]() { return static_cast<int>(reg.pool<CUnit>().size()); };
  auto check = [&](bool ok, const char* what) {
    if (!ok) ++selfTestFailures_;
    SLV_INFO("EDITOR SELFTEST %s: %s (units %d)", ok ? "ok  " : "FAIL", what, count());
  };
  auto selectAll = [&]() {
    selection_.clear();
    for (uint32_t i : reg.pool<CUnit>().entities()) selection_.push_back(reg.handle(i));
  };
  static int placed = 0;
  // Steps run every 8 frames so each command has been applied by a tick.
  if (frame_ % 8 != 0) return;
  switch (frame_ / 8) {
    case 1: {
      editor_->setActive(true);
      Command rm;
      rm.type = CommandType::Remove;
      for (uint32_t i : reg.pool<CUnit>().entities()) rm.units.push_back(reg.handle(i));
      sim_->queue(std::move(rm));
      break;
    }
    case 2:
      check(count() == 0, "battlefield cleared");
      editor_->setPlaceUnit("ger_rifleman", 0, PlaceMode::Block, 10);
      {
        // Somewhere dry and open, wherever the camera happens to be.
        const Vec2i land = findLand({map_.widthLeptons() / 2, map_.heightLeptons() / 3});
        centerOn(land);
        mouse_ = cam_.worldToScreen(groundToScreenWorld(static_cast<float>(land.x), static_cast<float>(land.y)));
      }
      editor_->beginStroke();
      editor_->endStroke();
      break;
    case 3:
      placed = count();
      check(placed >= 6 && placed <= 10, "place a block of 10 (those on dry land)");
      editor_->undo();
      break;
    case 4: check(count() == 0, "undo the placement"); editor_->redo(); break;
    case 5: check(count() == placed, "redo the placement"); selectAll(); editor_->deleteSelection(); break;
    case 6: check(count() == 0, "delete the selection"); editor_->undo(); break;
    case 7: {
      check(count() == placed, "undo the delete brings them back");
      selectAll();
      before_.clear();
      for (uint32_t i : reg.pool<CUnit>().entities()) before_.push_back({reg.pool<CPosition>().get(i).x, reg.pool<CPosition>().get(i).y});
      // Pick a direction where every unit lands on dry ground.
      const int32_t T = LEPTONS_PER_TILE;
      moveDelta_ = {};
      for (Vec2i d : {Vec2i{3 * T, 0}, Vec2i{0, 3 * T}, Vec2i{-3 * T, 0}, Vec2i{0, -3 * T}}) {
        bool ok = true;
        for (const Vec2i& p : before_) ok = ok && map_.passable((p.x + d.x) >> LEPTON_SHIFT, (p.y + d.y) >> LEPTON_SHIFT, Locomotor::Foot);
        if (ok) {
          moveDelta_ = d;
          break;
        }
      }
      editor_->moveSelection(moveDelta_);
      break;
    }
    case 8: {
      bool ok = moveDelta_.x != 0 || moveDelta_.y != 0;
      size_t k = 0;
      for (uint32_t i : reg.pool<CUnit>().entities()) {
        const CPosition& p = reg.pool<CPosition>().get(i);
        // Separation may nudge units a little after they land.
        ok = ok && k < before_.size() && std::abs(p.x - before_[k].x - moveDelta_.x) < 64 &&
             std::abs(p.y - before_[k].y - moveDelta_.y) < 64;
        ++k;
      }
      check(ok, "move the selection 3 tiles");
      editor_->undo();
      break;
    }
    case 9: {
      bool ok = true;
      size_t k = 0;
      for (uint32_t i : reg.pool<CUnit>().entities()) {
        const CPosition& p = reg.pool<CPosition>().get(i);
        ok = ok && k < before_.size() && std::abs(p.x - before_[k].x) < 64 && std::abs(p.y - before_[k].y) < 64;
        ++k;
      }
      check(ok, "undo the move");
      selectAll();
      editor_->setSelectionOwner(1);
      break;
    }
    case 10: {
      bool all = true;
      for (uint32_t i : reg.pool<CUnit>().entities()) all = all && reg.pool<CUnit>().get(i).owner == 1;
      check(all && count() == placed, "give units to side 2");
      editor_->undo();
      break;
    }
    case 11: {
      bool all = true;
      for (uint32_t i : reg.pool<CUnit>().entities()) all = all && reg.pool<CUnit>().get(i).owner == 0;
      check(all, "undo the gift");
      editor_->undo();  // the placement itself, through two respawns' worth of remapping
      break;
    }
    case 12:
      check(count() == 0, "undo the original placement after delete/undo");
      takeSnapshot();  // an empty battlefield
      editor_->redo();
      break;
    case 13: check(count() == placed, "redo the placement once more"); restoreSnapshot(); break;
    case 14:
      check(count() == 0, "restore the empty snapshot");
      SLV_INFO("EDITOR SELFTEST %s (%d failures)", selfTestFailures_ ? "FAILED" : "PASSED", selfTestFailures_);
      quit_ = true;
      break;
    default: break;
  }
}

// A scripted Live Scenario Editor session for the automated screenshots:
// places two forces with the unit tools, sets them fighting, calls in
// artillery, then selects a tank to show the inspector.
void GameScene::runLiveDemo() {
  const float W = static_cast<float>(app_.pixelWidth()), H = static_cast<float>(app_.pixelHeight());
  auto clickAt = [&](float fx, float fy) {
    mouse_ = {W * fx, H * fy};
    editor_->beginStroke();
    editor_->strokeAt(groundAt(mouse_), 1.0f / 60.0f);
    editor_->endStroke();
  };
  if (frame_ == 2) {
    editor_->setActive(true);
    paused_ = false;
    // Clear the default armies so the scene is built entirely with the editor.
    Command rm;
    rm.type = CommandType::Remove;
    for (uint32_t i : sim_->registry().pool<CUnit>().entities()) rm.units.push_back(sim_->registry().handle(i));
    sim_->queue(std::move(rm));
    editor_->setPlaceFacing(14);
    editor_->setPlaceFacing(12);
    editor_->setPlaceUnit("ger_medium_tank", 0, PlaceMode::Block, 6);
    clickAt(0.78f, 0.80f);
    editor_->setPlaceUnit("ger_rifleman", 0, PlaceMode::Block, 24);
    clickAt(0.90f, 0.70f);
    editor_->setPlaceFacing(4);
    editor_->setPlaceUnit("rus_rifleman", 1, PlaceMode::Block, 40);
    clickAt(0.42f, 0.52f);
    editor_->setPlaceUnit("rus_medium_tank", 1, PlaceMode::Block, 4);
    clickAt(0.36f, 0.66f);
  }
  if (frame_ == 6) {
    Registry& reg = sim_->registry();
    Command g, r;
    g.type = r.type = CommandType::AttackMove;
    for (uint32_t i : reg.pool<CUnit>().entities()) (reg.pool<CUnit>().get(i).owner == 0 ? g : r).units.push_back(reg.handle(i));
    mouse_ = {W * 0.42f, H * 0.55f};
    g.target = mouseWorldLeptons();
    mouse_ = {W * 0.80f, H * 0.78f};
    r.target = mouseWorldLeptons();
    queueOrder(std::move(g));
    queueOrder(std::move(r));
  }
  if (frame_ == 150) {
    editor_->setTool(Tool::Artillery);
    editor_->setBrush(3, 40);
    clickAt(0.42f, 0.52f);
  }
  if (frame_ == 290) {
    editor_->setTool(Tool::SelectUnits);
    selection_.clear();
    Registry& reg = sim_->registry();
    for (uint32_t i : reg.pool<CUnit>().entities())
      if (types_.get(reg.pool<CUnit>().get(i).type).id == "ger_medium_tank") {
        selection_.push_back(reg.handle(i));
        break;
      }
    mouse_ = {W * 0.6f, H * 0.6f};
  }
  if (frame_ == 340) {  // second picture: the unit palette with a block of ghosts under the mouse
    editor_->setPlaceFacing(10);
    editor_->setPlaceUnit("rus_rifleman", 1, PlaceMode::Block, 30);
    mouse_ = {W * 0.62f, H * 0.30f};
  }
}

// Base building for the automated screenshots: digs a trench line with
// sandbags and wire in front of the base, builds a tank works, trains
// riflemen, and ends with a bunker ready to place under the mouse.
void GameScene::runEconomyDemo() {
  Registry& reg = sim_->registry();
  const int32_t T = LEPTONS_PER_TILE;
  auto findOwn = [&](const char* id) {
    for (uint32_t i : reg.pool<CUnit>().entities())
      if (reg.pool<CUnit>().get(i).owner == 0 && types_.get(reg.pool<CUnit>().get(i).type).id == id) return reg.handle(i);
    return Entity{};
  };
  const Entity yard = findOwn("ger_yard");
  if (!yard.valid()) return;
  const CPosition yp = reg.get<CPosition>(yard);
  auto produce = [&](const char* id, int n) {
    Command c;
    c.type = CommandType::Produce;
    c.player = 0;
    c.unitType = static_cast<uint16_t>(types_.find(id));
    c.amount = n;
    sim_->queue(std::move(c));
  };
  auto walls = [&](const char* id, Vec2i from, Vec2i to) {
    Command c;
    c.type = CommandType::PlaceWalls;
    c.player = 0;
    c.unitType = static_cast<uint16_t>(types_.find(id));
    const int n = std::max(std::abs(to.x - from.x), std::abs(to.y - from.y));
    for (int i = 0; i <= n; ++i)
      c.spawns.push_back({0, 0, {(from.x + (to.x - from.x) * i / std::max(1, n)) * T + 128, (from.y + (to.y - from.y) * i / std::max(1, n)) * T + 128}, 0, -1});
    sim_->queue(std::move(c));
  };
  const int tx = yp.x / T, ty = yp.y / T;
  if (frame_ == 2) {
    speed_ = 4.0f;
    Command m;
    m.type = CommandType::SetCredits;
    m.player = 0;
    m.amount = 20000;
    sim_->queue(std::move(m));
    produce("ger_factory", 1);
    produce("ger_rifleman", 6);
    centerOn({yp.x + 5 * T, yp.y + 1 * T});
  }
  if (frame_ == 4) {
    walls("ger_trench", {tx + 8, ty - 6}, {tx + 8, ty + 6});
    walls("ger_sandbags", {tx + 10, ty - 3}, {tx + 10, ty + 3});
    walls("ger_wire", {tx + 12, ty - 7}, {tx + 12, ty + 7});
    walls("ger_traps", {tx + 13, ty - 2}, {tx + 13, ty + 2});
  }
  // Put the tank works down as soon as it is ready.
  const ProductionQueue& bq = sim_->playerState(0).queues[static_cast<size_t>(Queue::Buildings)];
  if (bq.ready && frame_ % 10 == 0) {
    sidebar_->startPlacing(bq.items.front());
    for (int r = 0; r < 12; ++r)
      if (sim_->canPlace(bq.items.front(), sim_->footprintOrigin(bq.items.front(), {yp.x - r * T, yp.y + (6 + r) * T})) &&
          sidebar_->placeAt({yp.x - r * T, yp.y + (6 + r) * T}))
        break;
    produce("ger_bunker", 1);
    produce("ger_medium_tank", 2);
  }
  if (frame_ == 300) {
    // Send the riflemen into the trench.
    selection_.clear();
    for (uint32_t i : reg.pool<CUnit>().entities())
      if (reg.pool<CUnit>().get(i).owner == 0 && types_.get(reg.pool<CUnit>().get(i).type).id == "ger_rifleman" &&
          distance(reg.pool<CPosition>().get(i).x - yp.x, reg.pool<CPosition>().get(i).y - yp.y) < 14u * T)
        selection_.push_back(reg.handle(i));
    Command c;
    c.type = CommandType::Move;
    c.player = 0;
    c.units = selection_;
    c.target = {(tx + 8) * T + 128, ty * T};
    c.formation = Formation::Line;
    c.formFacing = 0;
    c.formWidth = 10 * T;
    sim_->queue(std::move(c));
  }
  // Last: the bunker waits to be placed, its ghost under the mouse.
  const ProductionQueue& dq = sim_->playerState(0).queues[static_cast<size_t>(Queue::Defenses)];
  if (dq.ready && !sidebar_->busy()) {
    sidebar_->startPlacing(dq.items.front());
    const Vec2f s = cam_.worldToScreen(groundToScreenWorld(static_cast<float>((tx + 6) * T), static_cast<float>((ty - 3) * T)));
    mouse_ = s;
  }
}

}  // namespace legion

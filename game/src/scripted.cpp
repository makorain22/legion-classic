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

// Plays a skirmish through the real game: saves, loads and checks the state
// matches; saves a replay, plays it back and checks it ends where the game did.
void GameScene::runFlowSelfTest() {
  auto check = [&](bool ok, const char* what) {
    if (!ok) ++selfTestFailures_;
    SLV_INFO("FLOW SELFTEST %s: %s", ok ? "ok  " : "FAIL", what);
  };
  std::string err;
  if (frame_ == 20) {
    // Give some orders, as a player would.
    Command p;
    p.type = CommandType::Produce;
    p.player = 0;
    p.unitType = static_cast<uint16_t>(types_.find("ger_rifleman"));
    p.amount = 4;
    sim_->queue(p);
    speed_ = 4;
  }
  if (frame_ == 400) {
    check(saveGame(savesDir_ + "selftest.lsav", err), "save the game");
    flowHash_ = sim_->stateHash();
    flowTick_ = sim_->tickCount();
    check(saveReplay(replaysDir_ + "selftest.lrep", err), "save the replay");
  }
  if (frame_ == 401) {
    check(loadGame(savesDir_ + "selftest.lsav", err), "load the game");
    check(sim_->tickCount() == flowTick_ && sim_->stateHash() == flowHash_, "loaded state matches the saved one");
    check(playReplay(replaysDir_ + "selftest.lrep", err), "start the replay");
    speed_ = 8;
  }
  if (frame_ > 402 && mode_ == Mode::Replay && sim_->tickCount() >= replayEnd_ && flowHash_ != 0) {
    check(sim_->stateHash() == flowHash_, "the replay ends exactly where the game was saved");
    SLV_INFO("FLOW SELFTEST %s (%d failures)", selfTestFailures_ ? "FAILED" : "PASSED", selfTestFailures_);
    flowHash_ = 0;
    quit_ = true;
  }
}

// Every unit of every army lined up for inspection (--gallery): Germany in
// the front rows, then the Soviet Union, then Britain, each type facing a different way.
void GameScene::runGallery() {
  Registry& reg = sim_->registry();
  const int32_t T = LEPTONS_PER_TILE;
  if (frame_ == 2) {
    Command rm;
    rm.type = CommandType::Remove;
    for (uint32_t i : reg.pool<CUnit>().entities()) rm.units.push_back(reg.handle(i));
    sim_->queue(std::move(rm));
    const Vec2i c = findLand({map_.widthLeptons() / 2, map_.heightLeptons() / 2});
    galleryAt_ = c;
    for (uint8_t pl : {uint8_t(1), uint8_t(2)}) {
      Command team;
      team.type = CommandType::SetTeam;
      team.player = pl;
      team.count = 0;
      sim_->queue(std::move(team));
    }
    int row = 0;
    uint8_t owner = 0;
    // --factions narrows it to the armies named ("gbr" for a close look at one).
    std::vector<std::string> armies{"ger", "rus", "gbr"};
    if (!opt_.factions.empty()) {
      armies.clear();
      std::string f = opt_.factions + ",";
      for (size_t a = 0, b; (b = f.find(',', a)) != std::string::npos; a = b + 1)
        if (b > a) armies.push_back(f.substr(a, b - a));
    }
    for (const std::string& fac : armies) {
      std::vector<int> inf, veh;
      for (size_t t = 0; t < types_.size(); ++t) {
        const UnitTypeDef& d = types_.get(static_cast<int>(t));
        if (d.faction != fac || d.isStructure()) continue;
        (d.infantry() ? inf : veh).push_back(static_cast<int>(t));
      }
      for (const auto* list : {&inf, &veh}) {
        const float step = list == &inf ? 1.2f : 2.4f;
        for (size_t i = 0; i < list->size(); ++i) {
          Command s;
          s.type = CommandType::Spawn;
          s.unitType = static_cast<uint16_t>((*list)[i]);
          s.player = owner;
          s.count = 1;
          s.target = {c.x + static_cast<int32_t>((i - list->size() / 2.0f) * step * T), c.y + (row - 2) * 3 * T};
          s.facing = angleOfFacing(static_cast<int>((i * 3 + row) % 16));
          sim_->queue(std::move(s));
        }
        ++row;
      }
      ++owner;
    }
    // The civilian buildings, in a row behind.
    int k = 0;
    for (size_t t = 0; t < types_.size(); ++t) {
      const UnitTypeDef& d = types_.get(static_cast<int>(t));
      if (d.faction != "neutral") continue;
      Command s;
      s.type = CommandType::Spawn;
      s.unitType = static_cast<uint16_t>(t);
      s.player = NEUTRAL_OWNER;
      s.count = 1;
      s.target = {c.x + (k - 3) * 4 * T, c.y - 9 * T};
      sim_->queue(std::move(s));
      ++k;
    }
    centerOn(c);
  }
  if (frame_ == 3) {
    Command hold;
    hold.type = CommandType::SetStance;
    hold.stance = Stance::HoldFire;
    for (uint32_t i : reg.pool<CUnit>().entities()) hold.units.push_back(reg.handle(i));
    sim_->queue(std::move(hold));
    revealMap_ = true;
    centerOn(galleryAt_);
  }
}

// Two forces meeting head-on in open country, for screenshots of the fighting (--battle-demo).
void GameScene::runBattleDemo() {
  Registry& reg = sim_->registry();
  const int32_t T = LEPTONS_PER_TILE;
  if (frame_ == 2) {
    Command rm;
    rm.type = CommandType::Remove;
    for (uint32_t i : reg.pool<CUnit>().entities()) rm.units.push_back(reg.handle(i));
    sim_->queue(std::move(rm));
    const Vec2i c = findLand({map_.widthLeptons() / 2, map_.heightLeptons() / 2});
    galleryAt_ = c;
    auto line = [&](const char* id, uint8_t owner, Vec2i at, int n, int gap, int facing) {
      Command sp;
      sp.type = CommandType::SpawnList;
      const int t = types_.find(id);
      if (t < 0) return;
      for (int i = 0; i < n; ++i)
        sp.spawns.push_back({static_cast<uint16_t>(t), owner, {at.x + (i - n / 2) * gap / 2, at.y + (i - n / 2) * gap}, angleOfFacing(facing), -1});
      sim_->queue(std::move(sp));
    };
    line("ger_rifleman", 0, {c.x - 5 * T, c.y}, 15, T / 2, 0);
    line("ger_mg_team", 0, {c.x - 6 * T, c.y + T}, 2, T * 2, 0);
    line("ger_medium_tank", 0, {c.x - 8 * T, c.y - T}, 3, T * 2, 0);
    line("rus_rifleman", 1, {c.x + 5 * T, c.y}, 18, T / 2, 8);
    line("rus_mg_team", 1, {c.x + 6 * T, c.y - T}, 2, T * 2, 8);
    line("rus_medium_tank", 1, {c.x + 8 * T, c.y + T}, 3, T * 2, 8);
    centerOn(c);
  }
  if (frame_ == 4) {
    revealMap_ = true;
    Command g, r;
    g.type = r.type = CommandType::AttackMove;
    for (uint32_t i : reg.pool<CUnit>().entities()) (reg.pool<CUnit>().get(i).owner == 0 ? g : r).units.push_back(reg.handle(i));
    g.target = {galleryAt_.x + 6 * T, galleryAt_.y};
    r.target = {galleryAt_.x - 6 * T, galleryAt_.y};
    sim_->queue(std::move(g));
    sim_->queue(std::move(r));
    centerOn(galleryAt_);
  }
}

// Unit organisation, garrisons and fog chips on show (--org-demo).
void GameScene::runOrgDemo() {
  Registry& reg = sim_->registry();
  const int32_t T = LEPTONS_PER_TILE;
  auto spawnList = [&](const char* id, uint8_t owner, Vec2i at, int n, int cols, int gap) {
    Command c;
    c.type = CommandType::SpawnList;
    const int t = types_.find(id);
    if (t < 0) return;
    for (int i = 0; i < n; ++i)
      c.spawns.push_back({static_cast<uint16_t>(t), owner, {at.x + (i % cols) * gap, at.y + (i / cols) * gap}, angleOfFacing(2), -1});
    sim_->queue(std::move(c));
  };
  if (frame_ == 2) {
    Command rm;
    rm.type = CommandType::Remove;
    for (uint32_t i : reg.pool<CUnit>().entities()) rm.units.push_back(reg.handle(i));
    sim_->queue(std::move(rm));
    galleryAt_ = findLand({map_.widthLeptons() / 2, map_.heightLeptons() / 2});
    const Vec2i c = galleryAt_;
    spawnList("neutral_townhouse", NEUTRAL_OWNER, {c.x + 4 * T, c.y - 3 * T}, 1, 1, 0);
    spawnList("ger_rifleman", 0, {c.x, c.y}, 12, 4, 70);              // will hold the house
    spawnList("ger_rifleman", 0, {c.x - 8 * T, c.y + 2 * T}, 25, 5, 80);  // a platoon
    spawnList("ger_medium_tank", 0, {c.x - 7 * T, c.y - 5 * T}, 5, 5, 180);  // an armoured platoon
    spawnList("ger_mg_team", 0, {c.x - 2 * T, c.y + 5 * T}, 5, 5, 90);    // a fireteam of MGs
    spawnList("rus_rifleman", 1, {c.x + 17 * T, c.y + 2 * T}, 40, 8, 80);  // out in the fog
    spawnList("rus_medium_tank", 1, {c.x + 19 * T, c.y - 6 * T}, 4, 4, 200);
    spawnList("rus_rifleman", 1, {c.x + 14 * T, c.y - 10 * T}, 6, 3, 80);
    centerOn(c);
  }
  if (frame_ == 4) {
    Command team;
    team.type = CommandType::SetStance;
    team.stance = Stance::HoldFire;
    for (uint32_t i : reg.pool<CUnit>().entities()) team.units.push_back(reg.handle(i));
    sim_->queue(std::move(team));
    std::vector<Entity> houseMen, platoon, tanks;
    Entity house;
    const Vec2i c = galleryAt_;
    for (uint32_t i : reg.pool<CUnit>().entities()) {
      const CUnit& u = reg.pool<CUnit>().get(i);
      const CPosition& p = reg.pool<CPosition>().get(i);
      const std::string& id = types_.get(u.type).id;
      if (id == "neutral_townhouse") house = reg.handle(i);
      if (id == "ger_rifleman" && p.x >= c.x - T) houseMen.push_back(reg.handle(i));
      if (id == "ger_rifleman" && p.x < c.x - 4 * T) platoon.push_back(reg.handle(i));
      if (id == "ger_medium_tank") tanks.push_back(reg.handle(i));
    }
    Command occ;
    occ.type = CommandType::Occupy;
    occ.player = 0;
    occ.units = houseMen;
    occ.targetUnit = house;
    sim_->queue(std::move(occ));
    Command bind;
    bind.type = CommandType::BindGroup;
    bind.player = 0;
    bind.units = platoon;
    sim_->queue(bind);
    bind.units = tanks;
    sim_->queue(std::move(bind));
  }
  if (frame_ == 6) {
    selection_.clear();
    for (uint32_t i : reg.pool<CUnit>().entities())
      if (types_.get(reg.pool<CUnit>().get(i).type).id == "ger_medium_tank") selection_.push_back(reg.handle(i));
    centerOn({galleryAt_.x + 4 * T, galleryAt_.y});
  }
}

// Formations on the march (--formation-demo): five mixed groups, one in each
// shape, cross the field side by side.
void GameScene::runFormationDemo() {
  Registry& reg = sim_->registry();
  const int32_t T = LEPTONS_PER_TILE;
  const int kGroups = static_cast<int>(Formation::Count);
  if (frame_ == 2) {
    Command rm;
    rm.type = CommandType::Remove;
    for (uint32_t i : reg.pool<CUnit>().entities())
      if (reg.pool<CUnit>().get(i).owner < MAX_PLAYERS) rm.units.push_back(reg.handle(i));
    sim_->queue(std::move(rm));
    galleryAt_ = findLand({map_.widthLeptons() / 2, map_.heightLeptons() / 2});
    for (int g = 0; g < kGroups; ++g) {
      Command sp;
      sp.type = CommandType::SpawnList;
      const Vec2i at{galleryAt_.x - 16 * T, galleryAt_.y + (g * 2 - (kGroups - 1)) * 7 * T};
      const int tank = types_.find(g % 2 ? "rus_medium_tank" : "ger_medium_tank");
      const int man = types_.find(g % 2 ? "rus_rifleman" : "ger_rifleman");
      if (tank < 0 || man < 0) return;
      for (int i = 0; i < 5; ++i)
        sp.spawns.push_back({static_cast<uint16_t>(tank), 0, {at.x + (i % 3) * 380, at.y + (i / 3) * 380}, 0, -1});
      for (int i = 0; i < 12; ++i)
        sp.spawns.push_back({static_cast<uint16_t>(man), 0, {at.x - 2 * T + (i % 4) * 150, at.y + (i / 4) * 150}, 0, -1});
      sp.tag = static_cast<uint32_t>(1000 + g);
      sim_->queue(std::move(sp));
    }
    revealMap_ = true;
    centerOn(galleryAt_);
  }
  if (frame_ == 3)
    for (const SpawnRecord& r : sim_->spawnLog())
      if (r.tag >= 1000 && r.tag < 1000u + static_cast<uint32_t>(kGroups)) demoGroups_[r.tag - 1000].push_back(r.entity);
  if (frame_ == 5) {
    selection_.clear();
    for (int g = 0; g < kGroups; ++g) {
      Command mv;
      mv.type = CommandType::Move;
      mv.player = 0;
      mv.units = demoGroups_[static_cast<size_t>(g)];
      mv.target = {galleryAt_.x + 30 * T, galleryAt_.y + (g * 2 - (kGroups - 1)) * 7 * T};
      mv.formation = static_cast<Formation>(g);
      sim_->queue(std::move(mv));
    }
  }
  // Follow the middle of them all.
  if (frame_ > 5) {
    int64_t cx = 0, cy = 0, n = 0;
    for (const auto& g : demoGroups_)
      for (Entity e : g)
        if (reg.alive(e)) {
          cx += reg.get<CPosition>(e).x;
          cy += reg.get<CPosition>(e).y;
          ++n;
        }
    if (n) centerOn({static_cast<int32_t>(cx / n), static_cast<int32_t>(cy / n)});
  }
}

// Field works manned (--works-demo): for each army a trench line and a wall
// of sandbags, held by every kind of infantry, with a target out in front.
void GameScene::runWorksDemo() {
  Registry& reg = sim_->registry();
  const int32_t T = LEPTONS_PER_TILE;
  if (frame_ == 2) {
    Command rm;
    rm.type = CommandType::Remove;
    for (uint32_t i : reg.pool<CUnit>().entities()) rm.units.push_back(reg.handle(i));
    sim_->queue(std::move(rm));
    galleryAt_ = findLand({map_.widthLeptons() / 2, map_.heightLeptons() / 2});
    revealMap_ = true;
    for (uint8_t pl = 0; pl < MAX_PLAYERS; ++pl) {
      Command ctl;
      ctl.type = CommandType::SetController;  // nobody but the demo gives orders
      ctl.player = pl;
      ctl.amount = -1;
      sim_->queue(std::move(ctl));
      Command team;
      team.type = CommandType::SetTeam;  // the British beside the Germans, the Soviets between
      team.player = pl;
      team.count = pl == 1 ? 1 : 0;
      sim_->queue(std::move(team));
    }
    const Vec2i c = galleryAt_;
    const int ct = c.x / T, cr = c.y / T;
    Command sp;
    sp.type = CommandType::SpawnList;
    auto put = [&](const std::string& id, uint8_t owner, int tx, int ty, int facing) {
      const int t = types_.find(id);
      if (t >= 0) sp.spawns.push_back({static_cast<uint16_t>(t), owner, {tx * T + 128, ty * T + 128}, angleOfFacing(facing), -1});
    };
    const char* facs[3] = {"ger", "rus", "gbr"};
    for (int f = 0; f < 3; ++f) {
      const int y0 = cr - 6 + f * 5;
      for (int i = 0; i < 6; ++i) put(std::string(facs[f]) + "_trench", static_cast<uint8_t>(f), ct - 8 + i, y0, 0);
      put(std::string(facs[f]) + "_trench", static_cast<uint8_t>(f), ct - 3, y0 + 1, 0);
      for (int i = 0; i < 6; ++i) put(std::string(facs[f]) + "_sandbags", static_cast<uint8_t>(f), ct + 1 + i, y0, 0);
      put(std::string(facs[f]) + "_sandbags", static_cast<uint8_t>(f), ct + 6, y0 + 1, 0);
      // Their men, standing behind; occupied two frames later.
      const char* kinds[5] = {"_rifleman", "_mg_team", "_at_team", "_sniper", "_engineer"};
      for (int side = 0; side < 2; ++side)
        for (int k = 0; k < 8; ++k)
          put(std::string(facs[f]) + kinds[k % 5], static_cast<uint8_t>(f), (side ? ct + 1 : ct - 8) + k % 6, y0 - 2, 2);
    }
    sim_->queue(std::move(sp));
    centerOn({c.x - T, c.y - 2 * T});
    if (opt_.zoom >= 2.0f) { cam_.zoomStep(+1, {cam_.viewportW() * 0.5f, cam_.viewportH() * 0.5f}); cam_.zoomStep(+1, {cam_.viewportW() * 0.5f, cam_.viewportH() * 0.5f}); }
  }
  if (frame_ == 4) {
    // Every man to the work in front of him.
    std::vector<Entity> pieces, men;
    for (uint32_t i : reg.pool<CUnit>().entities()) {
      const Entity h = reg.handle(i);
      if (sim_->isTrench(h)) pieces.push_back(h);
      else if (types_.get(reg.pool<CUnit>().get(i).type).infantry()) men.push_back(h);
    }
    for (Entity m : men) {
      const CPosition& p = reg.get<CPosition>(m);
      Entity best;
      int32_t bd = INT32_MAX;
      for (Entity pc : pieces) {
        if (reg.get<CUnit>(pc).owner != reg.get<CUnit>(m).owner) continue;
        const CPosition& q = reg.get<CPosition>(pc);
        const int32_t d = std::abs(q.x - p.x) + std::abs(q.y - p.y);
        if (d < bd) bd = d, best = pc;
      }
      if (!best.valid()) continue;
      Command occ;
      occ.type = CommandType::Occupy;
      occ.player = reg.get<CUnit>(m).owner;
      occ.units = {m};
      occ.targetUnit = best;
      sim_->queue(std::move(occ));
    }
    selection_.clear();
  }
}

// The civilian buildings in rows (--town-demo), or the camera on the first
// bridge of the map, with a column crossing it (--bridge-demo).
void GameScene::runTownDemo() {
  Registry& reg = sim_->registry();
  const int32_t T = LEPTONS_PER_TILE;
  if (opt_.townDemo == 4) {
    // --logistics-demo: the camera on the first logistics office to go up (before that, on the first point).
    if (frame_ == 3) revealMap_ = true;
    if (frame_ < 3 || map_.supplyPoints.empty()) return;
    Vec2i at{map_.supplyPoints.front().x * T + 128, map_.supplyPoints.front().y * T + 128};
    for (size_t i = 0; i < map_.supplyPoints.size(); ++i)
      if (sim_->officeOn(static_cast<int>(i)).valid()) {
        at = {map_.supplyPoints[i].x * T + 128, map_.supplyPoints[i].y * T + 128};
        break;
      }
    centerOn(at);
    return;
  }
  if (frame_ != 2) {
    if (frame_ == 3) revealMap_ = true;
    if (frame_ == 4 && opt_.townDemo == 2) {
      Command mv;
      mv.type = CommandType::Move;
      mv.player = 0;
      for (uint32_t i : reg.pool<CUnit>().entities())
        if (reg.pool<CUnit>().get(i).owner == 0 && !types_.get(reg.pool<CUnit>().get(i).type).isStructure())
          mv.units.push_back(reg.handle(i));
      mv.target = {bridgeTo_.x * T + 128, bridgeTo_.y * T + 128};
      mv.formation = Formation::Column;
      sim_->queue(std::move(mv));
      selection_.clear();
    }
    return;
  }
  for (uint8_t pl = 0; pl < MAX_PLAYERS; ++pl) {
    Command ctl;
    ctl.type = CommandType::SetController;
    ctl.player = pl;
    ctl.amount = -1;
    sim_->queue(std::move(ctl));
  }
  if (opt_.townDemo == 3) {
    // The camera on the most rock on the map (for screenshots of cliffs).
    int best = -1;
    Vec2i at{0, 0};
    for (int ty = 2; ty < map_.height() - 2; ++ty)
      for (int tx = 2; tx < map_.width() - 2; ++tx) {
        int n = 0;
        for (int oy = -3; oy <= 3; ++oy)
          for (int ox = -3; ox <= 3; ++ox) n += map_.info(tx + ox, ty + oy).slope >= 41;
        if (n > best) {
          best = n;
          at = {tx, ty};
        }
      }
    revealMap_ = true;
    centerOn({at.x * T + 128, at.y * T + 128});
    return;
  }
  if (opt_.townDemo == 2) {
    Entity bridge;
    for (uint32_t i : reg.pool<CUnit>().entities())
      if (types_.get(reg.pool<CUnit>().get(i).type).bridge) {
        bridge = reg.handle(i);
        break;
      }
    if (!bridge.valid()) return;
    const CPosition& bp = reg.get<CPosition>(bridge);
    // Men and a truck to walk over it: from one end of the span to the other.
    std::vector<Vec2i> span;
    for (int dy = -8; dy <= 8; ++dy)
      for (int dx = -8; dx <= 8; ++dx) {
        const Entity o = sim_->structureAt(bp.x / T + dx, bp.y / T + dy);
        if (o.valid() && types_.get(reg.get<CUnit>(o).type).bridge) span.push_back({bp.x / T + dx, bp.y / T + dy});
      }
    Vec2i a = span.front(), b = span.front();
    for (const Vec2i& t : span)
      if (std::abs(t.x - span.front().x) + std::abs(t.y - span.front().y) > std::abs(b.x - a.x) + std::abs(b.y - a.y)) b = t;
    const Vec2i dir{(b.x > a.x) - (b.x < a.x), (b.y > a.y) - (b.y < a.y)};
    Command sp;
    sp.type = CommandType::SpawnList;
    const int rifle = types_.find("gbr_rifleman"), truck = types_.find("gbr_scout_car");
    for (int i = 0; i < 6; ++i)
      sp.spawns.push_back({static_cast<uint16_t>(rifle), 0,
                           {(a.x - dir.x * (2 + i)) * T + 128 + (i % 2) * 60, (a.y - dir.y * (2 + i)) * T + 128}, 0, -1});
    sp.spawns.push_back({static_cast<uint16_t>(truck), 0, {(a.x - dir.x * 9) * T + 128, (a.y - dir.y * 9) * T + 128}, 0, -1});
    sim_->queue(std::move(sp));
    galleryAt_ = {(a.x + b.x) * T / 2 + 128, (a.y + b.y) * T / 2 + 128};
    bridgeFrom_ = a;
    bridgeTo_ = {b.x + dir.x * 6, b.y + dir.y * 6};
    centerOn(galleryAt_);
    return;
  }
  Command rm;
  rm.type = CommandType::Remove;
  for (uint32_t i : reg.pool<CUnit>().entities()) rm.units.push_back(reg.handle(i));
  sim_->queue(std::move(rm));
  const Vec2i c = findLand({map_.widthLeptons() * 3 / 10, map_.heightLeptons() * 3 / 10});
  galleryAt_ = c;
  Command sp;
  sp.type = CommandType::SpawnList;
  int x = 0, y = 0, rowH = 0;
  for (size_t t = 0; t < types_.size(); ++t) {
    const UnitTypeDef& d = types_.get(static_cast<int>(t));
    if (d.faction != "neutral" || d.bridge) continue;
    if (x > 26) x = 0, y += rowH + 2, rowH = 0;
    const int32_t lx = c.x + (x - 14) * T + (d.footW % 2 ? T / 2 : 0), ly = c.y + (y - 6) * T + (d.footH % 2 ? T / 2 : 0);
    sp.spawns.push_back({static_cast<uint16_t>(t), NEUTRAL_OWNER, {lx + d.footW * T / 2, ly + d.footH * T / 2}, angleOfFacing(0), -1});
    x += d.footW + 2;
    rowH = std::max(rowH, d.footH);
  }
  sim_->queue(std::move(sp));
  selection_.clear();
  centerOn(c);
}

// --bench N: the skirmish that --ai-battle set up, played for N game minutes
// without drawing anything. Every two minutes it prints what each side has
// (money, income buildings, logistics offices and trucks, army, how much of
// the army is bound into units, where the main force is going), and at the
// end who is left. For tuning the computer player and the balance.
void GameScene::runBench() {
  Registry& reg = sim_->registry();
  const int32_t T = LEPTONS_PER_TILE;
  struct Tally {
    int64_t convoy = 0;      // paid in by supply trucks
    int captured = 0;        // logistics points taken
    int lostMen = 0, lostVehicles = 0, lostStructures = 0;
    int64_t boundTicks = 0, armyTicks = 0;  // unit-ticks in a bound unit / in the army (sampled once a second)
    int64_t fireOn[MAX_PLAYERS] = {};       // shots fired at ground held by each other side
  };
  std::array<Tally, MAX_PLAYERS> tally{};
  auto home = [&](uint8_t p) { return Vec2i{players_[p].startX, players_[p].startY}; };
  auto report = [&](uint32_t tick) {
    std::printf("--- %2u:%02u ---\n", tick / 900, tick / 15 % 60);
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
      if (!players_[p].active) continue;
      const PlayerState& ps = sim_->playerState(p);
      int yards = 0, posts = 0, wh = 0, fuel = 0, offices = 0, trucks = 0, men = 0, veh = 0, bound = 0, other = 0, fort = 0, scouts = 0;
      for (uint32_t e : reg.pool<CUnit>().entities()) {
        const CUnit& u = reg.pool<CUnit>().get(e);
        if (u.owner != p) continue;
        const UnitTypeDef& d = types_.get(u.type);
        if (d.isStructure()) {
          yards += d.role == "yard";
          posts += d.role == "post";
          wh += d.role == "warehouse";
          fuel += d.role == "fuel";
          offices += d.role == "office";
          fort += d.category == UnitCategory::Fortification;
          other += d.role != "yard" && d.role != "post" && d.role != "warehouse" && d.role != "fuel" && d.role != "office" &&
                   d.category != UnitCategory::Fortification;
          continue;
        }
        if (d.capacity > 0) {
          ++trucks;
          continue;
        }
        if (d.weapons.empty()) continue;
        (d.infantry() ? men : veh) += 1;
        scouts += d.scout;
        bound += sim_->groupOf(reg.handle(e)) != nullptr;
      }
      const AiPlayer& ai = sim_->ai().player(p);
      const Tally& t = tally[p];
      std::string fire;
      for (uint8_t q = 0; q < MAX_PLAYERS; ++q)
        if (q != p && players_[q].active) fire += " >" + std::to_string(q) + ":" + std::to_string(t.fireOn[q]);
      std::printf("  %u %s%s $%-6d +%-4d/min  yards %d post %d wh %d fuel %d other %d works %d | offices %d trucks %d convoys $%lld (taken %d) |"
                  " army %d men %d veh (%d scouts), bound %d%% now, %d%% so far | wave %zu phase %d waves %u raiders %zu capture %zu |"
                  " lost %d men %d veh %d bld | fire%s\n",
                  p, players_[p].faction.c_str(), ps.defeated ? " DEFEATED" : "", ps.credits, ps.incomePerMinute, yards, posts, wh, fuel, other, fort,
                  offices, trucks, static_cast<long long>(t.convoy), t.captured, men, veh, scouts, men + veh ? bound * 100 / (men + veh) : 0,
                  t.armyTicks ? static_cast<int>(t.boundTicks * 100 / t.armyTicks) : 0, ai.wave.size(), ai.phase, ai.waves, ai.raiders.size(),
                  ai.capture.size(), t.lostMen, t.lostVehicles, t.lostStructures, fire.c_str());
    }
    std::fflush(stdout);
  };
  const uint32_t total = static_cast<uint32_t>(opt_.bench) * 60u * TICKS_PER_SECOND;
  const uint64_t t0 = SDL_GetTicks();
  uint32_t ended = 0;
  for (uint32_t i = 1; i <= total; ++i) {
    sim_->tick();
    for (const SimEvent& ev : sim_->events()) {
      if (ev.owner >= MAX_PLAYERS) continue;
      Tally& t = tally[ev.owner];
      if (ev.type == EventType::ConvoyArrived) t.convoy += ev.amount;
      else if (ev.type == EventType::PointCaptured) ++t.captured;
      else if (ev.type == EventType::UnitDied) {
        const UnitTypeDef& d = types_.get(ev.unitType);
        (d.isStructure() ? t.lostStructures : d.infantry() ? t.lostMen : t.lostVehicles) += 1;
      } else if (ev.type == EventType::Fire) {
        // Whose ground was it aimed at? The side whose base is nearest the aim point.
        int best = -1;
        int64_t bd = INT64_MAX;
        for (uint8_t q = 0; q < MAX_PLAYERS; ++q) {
          if (!players_[q].active || q == ev.owner) continue;
          const Vec2i h = home(q);
          const int64_t d = static_cast<int64_t>(ev.at.x - h.x) * (ev.at.x - h.x) + static_cast<int64_t>(ev.at.y - h.y) * (ev.at.y - h.y);
          if (d < bd && d < static_cast<int64_t>(34 * T) * (34 * T)) {
            bd = d;
            best = q;
          }
        }
        if (best >= 0) ++t.fireOn[best];
      }
    }
    if (i % TICKS_PER_SECOND == 0)
      for (uint32_t e : reg.pool<CUnit>().entities()) {
        const CUnit& u = reg.pool<CUnit>().get(e);
        if (u.owner >= MAX_PLAYERS) continue;
        const UnitTypeDef& d = types_.get(u.type);
        if (d.isStructure() || d.weapons.empty() || d.capacity > 0) continue;
        ++tally[u.owner].armyTicks;
        tally[u.owner].boundTicks += sim_->groupOf(reg.handle(e)) != nullptr;
      }
    if (i % (120 * TICKS_PER_SECOND) == 0 || i == total) report(i);
    int standing = 0;
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) standing += players_[p].active && !sim_->playerState(p).defeated;
    if (standing <= 1 && !ended) {
      ended = i;
      report(i);
      break;
    }
  }
  std::printf("=== %s after %u:%02u (%.1f s of machine time) ===\n", ended ? "decided" : "undecided", (ended ? ended : total) / 900,
              (ended ? ended : total) / 15 % 60, static_cast<double>(SDL_GetTicks() - t0) / 1000.0);
  for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
    if (players_[p].active) std::printf("  %u %s: %s\n", p, players_[p].faction.c_str(), sim_->playerState(p).defeated ? "defeated" : "standing");
  std::fflush(stdout);
}

}  // namespace legion

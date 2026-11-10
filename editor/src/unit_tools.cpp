// Slave Editor - the unit, side and battle tools of the Live Scenario Editor.
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "editor.hpp"
#include "rules_editor.hpp"
#include "game_scene.hpp"
#include "slave/core/rng.hpp"
#include "slave/math/iso.hpp"
#include "slave/sim/factions.hpp"

using namespace slave;

namespace legion {

namespace {
uint64_t key(Entity e) { return (uint64_t{e.index} << 32) | e.generation; }

ImVec4 toImVec(uint32_t c, float a = 1.0f) {
  return ImVec4((c & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, ((c >> 16) & 0xff) / 255.0f, a);
}
uint32_t fromImVec(const ImVec4& v) {
  auto ch = [](float f) { return static_cast<uint32_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return ch(v.x) | (ch(v.y) << 8) | (ch(v.z) << 16) | 0xFF000000u;
}
const char* kOrderNames[] = {"Idle", "Moving", "Attack-moving", "Attacking"};
const char* kArmorNames[] = {"infantry", "light", "medium", "heavy", "structure"};
Rng& toolRng() {
  static Rng rng(0x5CE7A510);  // presentation-side randomness; the sim receives exact positions
  return rng;
}
}  // namespace

// ---------------------------------------------------------------------------
// Undoable unit actions
// ---------------------------------------------------------------------------

// Units that exist after the action and not before (placement), or the other
// way round (deletion). Undo and redo remove or recreate them; recreated
// units are new entities, so the editor remaps old handles to new ones.
struct Editor::PresenceAction : History::Action, std::enable_shared_from_this<PresenceAction> {
  Editor* ed = nullptr;
  std::vector<SpawnSpec> specs;
  std::vector<Entity> entities;  // parallel to specs; invalid until spawned
  bool presentAfter = true;

  void setPresent(bool on) {
    if (on) {
      // Keep the old handles: when the new units appear, old -> new is remembered
      // so other history steps that refer to the old units still find them.
      entities.resize(specs.size(), kNullEntity);
      ed->spawnFor(shared_from_this(), 0);
    } else {
      std::vector<Entity> live;
      for (Entity e : entities)
        if (e.valid()) live.push_back(ed->resolve(e));
      ed->removeEntities(live);
    }
  }
  void undo() override { setPresent(!presentAfter); }
  void redo() override { setPresent(presentAfter); }
};

// Moves, turns, owner and health changes: the state before and after.
struct Editor::EditAction : History::Action {
  Editor* ed = nullptr;
  std::vector<UnitEdit> before, after;
  void undo() override { ed->queueEdits(before); }
  void redo() override { ed->queueEdits(after); }
};

Entity Editor::resolve(Entity e) const {
  for (int guard = 0; guard < 64; ++guard) {
    auto it = remap_.find(key(e));
    if (it == remap_.end()) break;
    e = it->second;
  }
  return e;
}

void Editor::spawnFor(const std::shared_ptr<PresenceAction>& action, size_t first) {
  if (first >= action->specs.size()) return;
  const uint32_t tag = nextTag_++;
  // Every spec keeps its index in the action, so spawns can be matched up later.
  Command c;
  c.type = CommandType::SpawnList;
  c.tag = tag;
  c.indexBase = static_cast<uint32_t>(first);
  c.spawns.assign(action->specs.begin() + static_cast<long>(first), action->specs.end());
  scene_.sim().queue(std::move(c));
  pendingTags_[tag] = action;
}

void Editor::onSimTick(const std::vector<SpawnRecord>& spawned) {
  for (const SpawnRecord& r : spawned) {
    if (r.tag == selectTag_) scene_.selection().push_back(r.entity);
    auto it = pendingTags_.find(r.tag);
    if (it == pendingTags_.end()) continue;
    const auto action = it->second.lock();
    if (!action || r.index >= action->entities.size()) continue;
    const Entity old = action->entities[r.index];
    if (old.valid() && !(old == r.entity)) remap_[key(old)] = r.entity;
    action->entities[r.index] = r.entity;
  }
  // Tags are answered within one tick; forget the ones that were.
  if (!spawned.empty() || pendingTags_.size() > 256) {
    std::erase_if(pendingTags_, [&](const auto& kv) {
      return kv.second.expired() ||
             std::any_of(spawned.begin(), spawned.end(), [&](const SpawnRecord& r) { return r.tag == kv.first; });
    });
  }
}

void Editor::removeEntities(const std::vector<Entity>& units) {
  if (units.empty()) return;
  Command c;
  c.type = CommandType::Remove;
  c.units = units;
  scene_.sim().queue(std::move(c));
  auto& sel = scene_.selection();
  std::erase_if(sel, [&](Entity e) { return std::find(units.begin(), units.end(), e) != units.end(); });
}

void Editor::queueEdits(std::vector<UnitEdit> edits) {
  for (UnitEdit& e : edits) e.unit = resolve(e.unit);
  Command c;
  c.type = CommandType::Edit;
  c.edits = std::move(edits);
  scene_.sim().queue(std::move(c));
}

void Editor::pushEdit(const std::string& label, std::vector<UnitEdit> before, std::vector<UnitEdit> after) {
  if (after.empty()) return;
  auto a = std::make_shared<EditAction>();
  a->ed = this;
  a->before = std::move(before);
  a->after = std::move(after);
  queueEdits(a->after);
  history_.push(label, a);
}

std::vector<SpawnSpec> Editor::specsOf(const std::vector<Entity>& units) const {
  Registry& reg = scene_.sim().registry();
  std::vector<SpawnSpec> out;
  for (Entity e : units) {
    if (!reg.alive(e) || !reg.has<CUnit>(e)) continue;
    const CUnit& u = reg.get<CUnit>(e);
    const CPosition& p = reg.get<CPosition>(e);
    out.push_back({u.type, u.owner, {p.x, p.y}, reg.get<CFacing>(e).angle, u.health});
  }
  return out;
}

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------
void Editor::setPlaceUnit(const std::string& typeId, uint8_t owner, PlaceMode mode, int blockCount) {
  const int t = scene_.types().find(typeId);
  if (t >= 0) placeType_ = t;
  placeOwner_ = owner;
  placeMode_ = mode;
  blockCount_ = std::max(1, blockCount);
  setTool(Tool::PlaceUnits);
}

bool Editor::placeable(int type, Vec2i at) const {
  const Map& m = scene_.map();
  return m.passable(at.x >> LEPTON_SHIFT, at.y >> LEPTON_SHIFT, scene_.types().get(type).locomotor);
}

namespace {
// A square-ish block of `n` positions centred on `c`, `spacing` apart.
std::vector<Vec2i> blockPositions(Vec2i c, int n, int32_t spacing) {
  int side = 1;
  while (side * side < n) ++side;
  const int rows = (n + side - 1) / side;
  std::vector<Vec2i> out;
  out.reserve(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const int gx = i % side, gy = i / side;
    out.push_back({c.x + (gx * 2 - (side - 1)) * spacing / 2, c.y + (gy * 2 - (rows - 1)) * spacing / 2});
  }
  return out;
}
int32_t spacingFor(const UnitTypeDef& d) { return std::max(110, d.radius * 2); }
}  // namespace

void Editor::beginUnitStroke() {
  const Vec2f g = scene_.groundAt(scene_.mouse());
  const Vec2i at{static_cast<int32_t>(g.x), static_cast<int32_t>(g.y)};
  godAccum_ = 1000.0f;  // fire on the first frame
  heal_ = {};
  if (tool_ == Tool::Area) {
    drawingArea_ = true;
    areaStart_ = areaEnd_ = {at.x >> LEPTON_SHIFT, at.y >> LEPTON_SHIFT};
    return;
  }
  if (tool_ != Tool::PlaceUnits) return;
  if (scene_.types().get(placeType_).faction != "neutral" && !scene_.players()[placeOwner_].active) {  // placing for a dormant side wakes it up
    scene_.players()[placeOwner_].active = true;
    scene_.applyPlayers();
  }
  const UnitTypeDef& def = scene_.types().get(placeType_);
  const Angle facing = angleOfFacing(placeFacing_);
  auto action = std::make_shared<PresenceAction>();
  action->ed = this;
  action->presentAfter = true;
  if (placeMode_ == PlaceMode::Brush) {
    placing_ = action;
    unitAccum_ = 1.0f;
    return;
  }
  const int n = placeMode_ == PlaceMode::Block ? blockCount_ : 1;
  for (const Vec2i& p : blockPositions(at, n, spacingFor(def)))
    if (placeable(placeType_, p)) action->specs.push_back({static_cast<uint16_t>(placeType_), placingOwner(), p, facing, -1});
  if (action->specs.empty()) {
    scene_.showStatus("Can't place " + def.name + " there", 2);
    return;
  }
  action->entities.assign(action->specs.size(), kNullEntity);
  spawnFor(action, 0);
  char label[96];
  std::snprintf(label, sizeof label, "Place %zu %s", action->specs.size(), def.name.c_str());
  history_.push(label, action);
}

void Editor::applyUnitBrush(Vec2f c, float dt) {
  if (!placing_ || placeMode_ != PlaceMode::Brush) return;
  const UnitTypeDef& def = scene_.types().get(placeType_);
  unitAccum_ += unitDensity_ * dt * std::max(1.0f, radius_ * 0.5f);
  const size_t first = placing_->specs.size();
  Rng& rng = toolRng();
  Registry& reg = scene_.sim().registry();
  while (unitAccum_ >= 1.0f) {
    unitAccum_ -= 1.0f;
    for (int attempt = 0; attempt < 6; ++attempt) {
      const float a = rng.below(36000) / 36000.0f * 6.2831853f;
      const float d = std::sqrt(rng.below(10000) / 10000.0f) * radius_ * LEPTONS_PER_TILE;
      const Vec2i p{static_cast<int32_t>(c.x + std::cos(a) * d), static_cast<int32_t>(c.y + std::sin(a) * d)};
      if (!placeable(placeType_, p)) continue;
      // Keep brushed units from landing on top of each other.
      const int32_t gap = def.radius * 3 / 2;
      bool crowded = false;
      for (size_t i = first > 40 ? first - 40 : 0; i < placing_->specs.size() && !crowded; ++i)
        crowded = std::abs(placing_->specs[i].pos.x - p.x) < gap && std::abs(placing_->specs[i].pos.y - p.y) < gap;
      if (!crowded)
        for (uint32_t e : reg.pool<CUnit>().entities()) {
          const CPosition& q = reg.pool<CPosition>().get(e);
          if (std::abs(q.x - p.x) < gap && std::abs(q.y - p.y) < gap) {
            crowded = true;
            break;
          }
        }
      if (crowded) continue;
      placing_->specs.push_back({static_cast<uint16_t>(placeType_), placingOwner(), p, angleOfFacing(placeFacing_), -1});
      placing_->entities.push_back(kNullEntity);
      break;
    }
  }
  spawnFor(placing_, first);
}

void Editor::endUnitStroke() {
  if (drawingArea_) {
    drawingArea_ = false;
    ScenarioScript& s = scene_.scenario();
    ScriptArea a;
    a.name = "Area " + std::to_string(s.areas.size() + 1);
    a.x0 = std::min(areaStart_.x, areaEnd_.x);
    a.y0 = std::min(areaStart_.y, areaEnd_.y);
    a.x1 = std::max(areaStart_.x, areaEnd_.x);
    a.y1 = std::max(areaStart_.y, areaEnd_.y);
    s.areas.push_back(a);
    selectedArea_ = static_cast<int>(s.areas.size()) - 1;
    scene_.scenarioChanged();
    tool_ = Tool::SelectUnits;
    return;
  }
  if (placing_ && !placing_->specs.empty()) {
    char label[96];
    std::snprintf(label, sizeof label, "Brush %zu %s", placing_->specs.size(),
                  scene_.types().get(placeType_).name.c_str());
    history_.push(label, placing_);
  }
  placing_.reset();
  if (!heal_.after.empty()) {
    auto a = std::make_shared<EditAction>();
    a->ed = this;
    a->before = std::move(heal_.before);
    a->after = std::move(heal_.after);
    history_.push("Heal " + std::to_string(a->after.size()) + " units", a);  // already applied while brushing
  }
  heal_ = {};
}

// ---------------------------------------------------------------------------
// Battle ("god") tools
// ---------------------------------------------------------------------------
void Editor::artilleryAt(Vec2i at) {
  const int w = scene_.weapons().find("artillery_105");
  if (w < 0) {
    scene_.showStatus("No artillery weapon in content/rules/weapons.json", 3);
    return;
  }
  Command c;
  c.type = CommandType::Strike;
  c.weapon = static_cast<uint16_t>(w);
  c.count = static_cast<uint16_t>(std::clamp(strikeRounds_, 1, 200));
  c.radius = static_cast<int32_t>(radius_ * LEPTONS_PER_TILE);
  c.target = at;
  scene_.sim().queue(std::move(c));
}

void Editor::applyGodTool(Vec2f c, float dt) {
  godAccum_ += dt;
  const float interval = tool_ == Tool::Artillery ? 0.8f : 0.15f;
  if (godAccum_ < interval) return;
  godAccum_ = 0;
  const Vec2i at{static_cast<int32_t>(c.x), static_cast<int32_t>(c.y)};
  if (tool_ == Tool::Artillery) {
    artilleryAt(at);
    return;
  }
  Registry& reg = scene_.sim().registry();
  const float r = radius_ * LEPTONS_PER_TILE;
  std::vector<Entity> hit;
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CPosition& p = reg.pool<CPosition>().get(e);
    const float dx = p.x - c.x, dy = p.y - c.y;
    if (dx * dx + dy * dy <= r * r) hit.push_back(reg.handle(e));
  }
  if (hit.empty()) return;
  if (tool_ == Tool::Smite) {
    Command k;
    k.type = CommandType::Kill;
    k.units = std::move(hit);
    scene_.sim().queue(std::move(k));
    return;
  }
  // Heal: remember each unit's health the first time the brush touches it.
  std::vector<UnitEdit> now;
  for (Entity e : hit) {
    const CUnit& u = reg.get<CUnit>(e);
    const int32_t maxHp = scene_.types().get(u.type).maxHealth;
    if (u.health >= maxHp || std::find(heal_.seen.begin(), heal_.seen.end(), key(e)) != heal_.seen.end()) continue;
    heal_.seen.push_back(key(e));
    UnitEdit b;
    b.unit = e;
    b.health = u.health;
    heal_.before.push_back(b);
    UnitEdit a = b;
    a.health = maxHp;
    heal_.after.push_back(a);
    now.push_back(a);
  }
  if (!now.empty()) queueEdits(std::move(now));
}

// ---------------------------------------------------------------------------
// Selection operations
// ---------------------------------------------------------------------------
void Editor::deleteSelection() {
  scene_.pruneSelection();
  auto& sel = scene_.selection();
  if (sel.empty()) return;
  auto action = std::make_shared<PresenceAction>();
  action->ed = this;
  action->presentAfter = false;
  action->specs = specsOf(sel);
  action->entities = sel;
  const std::vector<Entity> gone = sel;
  removeEntities(gone);
  history_.push("Delete " + std::to_string(gone.size()) + " units", action);
}

void Editor::duplicateSelection() {
  scene_.pruneSelection();
  auto& sel = scene_.selection();
  if (sel.empty()) return;
  auto action = std::make_shared<PresenceAction>();
  action->ed = this;
  action->specs = specsOf(sel);
  for (SpawnSpec& s : action->specs) {  // a copy two tiles to the south-east (down-right on screen)
    s.pos.x += 2 * LEPTONS_PER_TILE;
    s.health = -1;
  }
  action->entities.assign(action->specs.size(), kNullEntity);
  sel.clear();
  selectTag_ = nextTag_;  // the copies become the selection when they appear
  spawnFor(action, 0);
  history_.push("Duplicate " + std::to_string(action->specs.size()) + " units", action);
}

void Editor::rotateSelection(int steps) {
  scene_.pruneSelection();
  Registry& reg = scene_.sim().registry();
  std::vector<UnitEdit> before, after;
  for (Entity e : scene_.selection()) {
    UnitEdit b;
    b.unit = e;
    b.facing = reg.get<CFacing>(e).angle;
    UnitEdit a = b;
    a.facing = static_cast<int32_t>(wrapAngle(b.facing + steps * ANGLE_PER_FACING));
    before.push_back(b);
    after.push_back(a);
  }
  pushEdit("Turn units", std::move(before), std::move(after));
}

void Editor::setSelectionOwner(uint8_t owner) {
  scene_.pruneSelection();
  Registry& reg = scene_.sim().registry();
  std::vector<UnitEdit> before, after;
  for (Entity e : scene_.selection()) {
    UnitEdit b;
    b.unit = e;
    b.owner = reg.get<CUnit>(e).owner;
    UnitEdit a = b;
    a.owner = owner;
    before.push_back(b);
    after.push_back(a);
  }
  if (!scene_.players()[owner].active) {
    scene_.players()[owner].active = true;
    scene_.applyPlayers();
  }
  pushEdit("Give units to " + scene_.players()[owner].name, std::move(before), std::move(after));
}

void Editor::setSelectionHealth(float fraction) {
  scene_.pruneSelection();
  Registry& reg = scene_.sim().registry();
  std::vector<UnitEdit> before, after;
  for (Entity e : scene_.selection()) {
    const CUnit& u = reg.get<CUnit>(e);
    UnitEdit b;
    b.unit = e;
    b.health = u.health;
    UnitEdit a = b;
    a.health = std::max(1, static_cast<int32_t>(std::lround(scene_.types().get(u.type).maxHealth * fraction)));
    before.push_back(b);
    after.push_back(a);
  }
  pushEdit("Set health", std::move(before), std::move(after));
}

void Editor::moveSelection(Vec2i delta) {
  scene_.pruneSelection();
  if (delta.x == 0 && delta.y == 0) return;
  Registry& reg = scene_.sim().registry();
  std::vector<UnitEdit> before, after;
  for (Entity e : scene_.selection()) {
    const CPosition& p = reg.get<CPosition>(e);
    UnitEdit b;
    b.unit = e;
    b.move = true;
    b.pos = {p.x, p.y};
    UnitEdit a = b;
    a.pos = {p.x + delta.x, p.y + delta.y};
    before.push_back(b);
    after.push_back(a);
  }
  pushEdit("Move " + std::to_string(after.size()) + " units", std::move(before), std::move(after));
}

// ---------------------------------------------------------------------------
// World overlay: ghosts of what will be placed or where units will be moved
// ---------------------------------------------------------------------------
void Editor::renderUnitGhosts(SpriteRenderer& r, Vec2f g) {
  auto ghost = [&](int type, uint8_t owner, Vec2i at, int facing, bool ok) {
    const UnitArt& art = scene_.unitArt(type);
    const uint32_t tint = ok ? rgba8(255, 255, 255, 150) : rgba8(255, 80, 60, 150);
    if (r.in3D()) {
      scene_.ghost(art.body.frame(facing, 0), static_cast<float>(at.x), static_cast<float>(at.y), scene_.teamColor(owner), tint);
      if (art.hasTurret) {
        const float a = static_cast<float>(angleOfFacing(facing)) * (6.2831853f / ANGLE_FULL);
        scene_.ghost(art.turret.frame(facing, 0), at.x + std::cos(a) * art.turretOffset, at.y + std::sin(a) * art.turretOffset,
                     scene_.teamColor(owner), tint);
      }
      return;
    }
    const Vec2f s = scene_.groundToScreenWorld(static_cast<float>(at.x), static_cast<float>(at.y));
    r.sprite(art.body.frame(facing, 0), s.x, s.y, scene_.teamColor(owner), tint);
    if (art.hasTurret) {
      const float a = static_cast<float>(angleOfFacing(facing)) * (6.2831853f / ANGLE_FULL);
      const Vec2f t = scene_.groundToScreenWorld(at.x + std::cos(a) * art.turretOffset, at.y + std::sin(a) * art.turretOffset);
      r.sprite(art.turret.frame(facing, 0), std::round(t.x), std::round(t.y), scene_.teamColor(owner), tint);
    }
  };
  if (tool_ == Tool::PlaceUnits && placeMode_ != PlaceMode::Brush && !stroking_) {
    const Vec2i at{static_cast<int32_t>(g.x), static_cast<int32_t>(g.y)};
    const int n = placeMode_ == PlaceMode::Block ? blockCount_ : 1;
    const auto positions = blockPositions(at, n, spacingFor(scene_.types().get(placeType_)));
    // Draw back to front; very large blocks show their outline only.
    if (positions.size() <= 600) {
      for (const Vec2i& p : positions) ghost(placeType_, placeOwner_, p, placeFacing_, placeable(placeType_, p));
    } else {
      for (size_t i = 0; i < positions.size(); i += positions.size() / 300)
        ghost(placeType_, placeOwner_, positions[i], placeFacing_, placeable(placeType_, positions[i]));
    }
  }
  if (tool_ == Tool::SelectUnits && moving_) {
    Registry& reg = scene_.sim().registry();
    const Vec2i d{static_cast<int32_t>(g.x - moveStartGround_.x), static_cast<int32_t>(g.y - moveStartGround_.y)};
    size_t drawn = 0;
    for (Entity e : scene_.selection()) {
      if (!reg.alive(e) || ++drawn > 600) continue;
      const CUnit& u = reg.get<CUnit>(e);
      const CPosition& p = reg.get<CPosition>(e);
      const Vec2i at{p.x + d.x, p.y + d.y};
      ghost(u.type, u.owner, at, facingOf(reg.get<CFacing>(e).angle), placeable(u.type, at));
    }
  }
}

// ---------------------------------------------------------------------------
// Panels
// ---------------------------------------------------------------------------

// A 16-way compass: click a direction. Directions are drawn as they look on screen.
bool Editor::facingPicker(const char* id, int& facing) {
  const float size = 70.0f, r = size * 0.42f;
  ImGui::PushID(id);
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##compass", ImVec2(size, size * 0.62f));
  const bool clicked = ImGui::IsItemClicked();
  const ImVec2 c(p0.x + size * 0.5f, p0.y + size * 0.31f);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddEllipse(c, ImVec2(r, r * 0.5f), IM_COL32(150, 150, 140, 120));
  int best = facing;
  float bestD = 1e9f;
  const ImVec2 mp = ImGui::GetIO().MousePos;
  for (int f = 0; f < 16; ++f) {
    const float a = f * 6.2831853f / 16.0f;
    const Vec2f s = iso::worldToScreen(std::cos(a) * 256.0f, std::sin(a) * 256.0f);
    const float len = std::sqrt(s.x * s.x + s.y * s.y * 4.0f);
    const ImVec2 q(c.x + s.x / len * r, c.y + s.y / len * r);
    const bool on = f == facing;
    dl->AddCircleFilled(q, on ? 4.5f : 2.5f, on ? IM_COL32(240, 200, 90, 255) : IM_COL32(180, 180, 170, 200));
    if (on) dl->AddLine(c, q, IM_COL32(240, 200, 90, 255), 2.0f);
    const float d = (mp.x - q.x) * (mp.x - q.x) + (mp.y - q.y) * (mp.y - q.y);
    if (d < bestD) {
      bestD = d;
      best = f;
    }
  }
  ImGui::PopID();
  if (clicked) facing = best;
  return clicked;
}

void Editor::unitIcon(int type, uint8_t owner, float size) {
  (void)owner;
  const UnitArt& art = scene_.unitArt(type);
  const SpriteRenderer& r = scene_.renderer();
  const ImTextureRef tex(static_cast<ImTextureID>(r.atlasTexture()));
  const AtlasFrame& body = r.frame(art.body.frame(14, 0));  // facing the viewer, up-right
  const float scale = std::min(size / body.w, size / body.h) * 0.95f;
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(size, size));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + size, p0.y + size), IM_COL32(40, 42, 36, 255), 3.0f);
  const ImVec2 o(p0.x + (size - body.w * scale) * 0.5f, p0.y + (size - body.h * scale) * 0.5f);
  dl->AddImage(tex, o, ImVec2(o.x + body.w * scale, o.y + body.h * scale), ImVec2(body.u0, body.v0),
               ImVec2(body.u1, body.v1));
  if (art.hasTurret) {
    const AtlasFrame& t = r.frame(art.turret.frame(14, 0));
    const float a = static_cast<float>(angleOfFacing(14)) * (6.2831853f / ANGLE_FULL);
    const Vec2f off = iso::worldToScreen(std::cos(a) * art.turretOffset, std::sin(a) * art.turretOffset);
    const ImVec2 ta(o.x + (body.anchorX + off.x - t.anchorX) * scale, o.y + (body.anchorY + off.y - t.anchorY) * scale);
    dl->AddImage(tex, ta, ImVec2(ta.x + t.w * scale, ta.y + t.h * scale), ImVec2(t.u0, t.v0), ImVec2(t.u1, t.v1));
  }
}

bool Editor::placingStructure() const {
  return active() && tool_ == Tool::PlaceUnits && placeType_ >= 0 && scene_.types().get(placeType_).isStructure();
}

uint8_t Editor::placingOwner() const {
  return scene_.types().get(placeType_).faction == "neutral" ? NEUTRAL_OWNER : placeOwner_;
}

void Editor::drawUnitPalette() {
  const auto& types = scene_.types();
  const std::string& faction = scene_.players()[placeOwner_].faction;
  ImGui::Checkbox("Every faction", &paletteAllFactions_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1);
  ImGui::Combo("##cat", &paletteCategory_, [](void*, int i) {
    static const char* names[] = {"Everything", "Infantry", "Vehicles", "Base buildings", "Defences", "Fortifications"};
    return names[i];
  }, nullptr, 6);
  const float icon = 44.0f;
  static const char* kSections[] = {"Infantry", "Vehicles", "Base buildings", "Defences", "Fortifications", "Civilian buildings"};
  // Listed by kind under headings: soldiers, vehicles, the base, defences,
  // fortifications. Civilian houses belong to nobody: they are scenery, placed
  // with the props on the Terrain tab.
  auto sectionOf = [](const UnitTypeDef& d) { return d.faction == "neutral" ? 5 : static_cast<int>(d.category); };
  for (int sec = 0; sec < 5; ++sec) {
  if (paletteCategory_ > 0 && sec != paletteCategory_ - 1) continue;
  bool header = false;
  for (int i = 0; i < static_cast<int>(types.size()); ++i) {
    const UnitTypeDef& d = types.get(i);
    if (sectionOf(d) != sec || d.hidden) continue;
    if (!paletteAllFactions_ && d.faction != faction) continue;
    if (!header) {
      ImGui::SeparatorText(kSections[sec]);
      header = true;
    }
    ImGui::PushID(i);
    const bool sel = placeType_ == i && tool_ == Tool::PlaceUnits;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##unit", sel, 0, ImVec2(0, icon))) {
      placeType_ = i;
      tool_ = Tool::PlaceUnits;
    }
    ImGui::SetCursorScreenPos(start);
    unitIcon(i, placeOwner_, icon);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("%s", d.name.c_str());
    ImGui::TextDisabled("%s  %d hp  %s armour", d.faction.c_str(), d.maxHealth, kArmorNames[static_cast<int>(d.armor)]);
    ImGui::EndGroup();
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      ImGui::Text("%s (%s)", d.name.c_str(), d.id.c_str());
      ImGui::Text("Speed %.1f tiles/s, sight %.0f tiles", d.speed * 15.0f / 256.0f, d.sight / 256.0f);
      for (int w : d.weapons) {
        const WeaponDef& wd = scene_.weapons().get(w);
        ImGui::BulletText("%s: %d damage, range %.1f", wd.name.c_str(), wd.damage, wd.range / 256.0f);
      }
      ImGui::EndTooltip();
    }
    ImGui::PopID();
  }
  }
}

void Editor::drawUnitsTab() {
  // Tool choice.
  const float bw = (kPanelWidth - 26) * 0.5f;
  for (Tool t : {Tool::PlaceUnits, Tool::SelectUnits}) {
    const bool sel = tool_ == t;
    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.50f, 0.22f, 1));
    if (ImGui::Button(t == Tool::PlaceUnits ? "Place units" : "Select & edit", ImVec2(bw, 0))) tool_ = t;
    if (sel) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t == Tool::PlaceUnits ? "Place units for any side." : "Select units of any side, drag to move, edit below.");
    if (t == Tool::PlaceUnits) ImGui::SameLine();
  }

  if (tool_ == Tool::PlaceUnits) {
    // Side.
    auto& players = scene_.players();
    ImGui::SeparatorText("For side");
    ImGui::ColorButton("##col", toImVec(players[placeOwner_].color), ImGuiColorEditFlags_NoTooltip, ImVec2(18, 18));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##owner", players[placeOwner_].name.c_str())) {
      for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
        ImGui::PushID(p);
        ImGui::ColorButton("##c", toImVec(players[p].color), ImGuiColorEditFlags_NoTooltip, ImVec2(14, 14));
        ImGui::SameLine();
        std::string label = players[p].name + (players[p].active ? "" : "  (inactive)");
        if (ImGui::Selectable(label.c_str(), p == placeOwner_)) {
          placeOwner_ = p;
          // Keep the chosen unit if it fits the new side's faction, else pick its first unit.
          if (!paletteAllFactions_ && scene_.types().get(placeType_).faction != players[p].faction)
            for (int i = 0; i < static_cast<int>(scene_.types().size()); ++i)
              if (scene_.types().get(i).faction == players[p].faction) {
                placeType_ = i;
                break;
              }
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }

    ImGui::SeparatorText("Unit");
    drawUnitPalette();

    ImGui::SeparatorText("How");
    int mode = static_cast<int>(placeMode_);
    ImGui::RadioButton("One", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Brush", &mode, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Block", &mode, 2);
    placeMode_ = static_cast<PlaceMode>(mode);
    if (placeMode_ == PlaceMode::Brush) {
      ImGui::SetNextItemWidth(-60);
      ImGui::SliderFloat("Size", &radius_, 1.0f, 32.0f, "%.0f tiles");
      ImGui::SetNextItemWidth(-60);
      ImGui::SliderFloat("Density", &unitDensity_, 1.0f, 200.0f, "%.0f /s");
    }
    if (placeMode_ == PlaceMode::Block) {
      ImGui::SetNextItemWidth(-60);
      ImGui::SliderInt("Count", &blockCount_, 2, 10000, "%d", ImGuiSliderFlags_Logarithmic);
      for (int n : {10, 50, 100, 1000, 10000}) {
        char b[16];
        std::snprintf(b, sizeof b, "%d", n);
        if (ImGui::SmallButton(b)) blockCount_ = n;
        ImGui::SameLine();
      }
      ImGui::NewLine();
    }
    ImGui::Text("Facing");
    ImGui::SameLine();
    ImGui::TextDisabled("(, and . turn)");
    facingPicker("facing", placeFacing_);
    ImGui::TextDisabled("Left click places. Esc switches to Select.");
    return;
  }

  // --- Select & edit ---------------------------------------------------------
  scene_.pruneSelection();
  auto& sel = scene_.selection();
  ImGui::TextWrapped("Click or drag a box to select units of any side. Drag selected units to move them.");
  ImGui::SeparatorText(sel.empty() ? "Nothing selected" : (std::to_string(sel.size()) + " selected").c_str());
  if (sel.empty()) return;
  ImGui::BeginDisabled(sel.empty());
  if (ImGui::Button("Delete")) deleteSelection();
  ImGui::SameLine();
  if (ImGui::Button("Duplicate")) duplicateSelection();
  ImGui::SameLine();
  if (ImGui::Button("Kill")) {
    Command k;
    k.type = CommandType::Kill;
    k.units = sel;
    scene_.sim().queue(std::move(k));
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Destroy them as if in battle: wrecks, bodies, smoke. Can't be undone.");
  ImGui::SameLine();
  if (ImGui::Button("Stop")) {
    Command s;
    s.type = CommandType::Stop;
    s.units = sel;
    scene_.sim().queue(std::move(s));
  }
  if (ImGui::Button("Turn left")) rotateSelection(-1);
  ImGui::SameLine();
  if (ImGui::Button("Turn right")) rotateSelection(1);
  ImGui::SameLine();
  ImGui::TextDisabled(", .");

  // Give to side.
  auto& players = scene_.players();
  ImGui::Text("Give to");
  for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
    if (!players[p].active) continue;
    ImGui::SameLine();
    ImGui::PushID(p);
    ImGui::PushStyleColor(ImGuiCol_Button, toImVec(players[p].color, 0.75f));
    if (ImGui::Button(std::to_string(p + 1).c_str())) setSelectionOwner(p);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", players[p].name.c_str());
    ImGui::PopID();
  }
  // Health.
  static float hp = 100.0f;
  ImGui::SetNextItemWidth(-90);
  ImGui::SliderFloat("##hp", &hp, 1.0f, 100.0f, "%.0f %% health");
  ImGui::SameLine();
  if (ImGui::Button("Set health")) setSelectionHealth(hp / 100.0f);
  ImGui::EndDisabled();
  drawInspector();
}

void Editor::drawInspector() {
  Registry& reg = scene_.sim().registry();
  auto& sel = scene_.selection();
  ImGui::SeparatorText("Inspector");
  if (sel.size() == 1 && reg.alive(sel[0])) {
    const Entity e = sel[0];
    const CUnit& u = reg.get<CUnit>(e);
    const UnitTypeDef& d = scene_.types().get(u.type);
    const CPosition& p = reg.get<CPosition>(e);
    ImGui::Text("%s", d.name.c_str());
    ImGui::TextDisabled("%s  -  unit #%u", d.id.c_str(), e.index);
    ImGui::TextColored(toImVec(scene_.teamColor(u.owner)), "%s", scene_.ownerName(u.owner).c_str());
    ImGui::ProgressBar(static_cast<float>(u.health) / d.maxHealth, ImVec2(-1, 0),
                       (std::to_string(u.health) + " / " + std::to_string(d.maxHealth) + " hp").c_str());
    ImGui::Text("Tile %d, %d   facing %d", p.x >> LEPTON_SHIFT, p.y >> LEPTON_SHIFT, facingOf(reg.get<CFacing>(e).angle));
    const COrder& o = reg.get<COrder>(e);
    ImGui::Text("Order: %s", kOrderNames[static_cast<int>(o.type)]);
    const CMover& m = reg.get<CMover>(e);
    if (m.active) ImGui::Text("Route: waypoint %u of %zu", m.index + 1, m.path.size());
    else if (m.request) ImGui::Text("Route: waiting for a path");
    if (const CCombat* c = reg.tryGet<CCombat>(e)) {
      if (reg.alive(c->target) && reg.has<CUnit>(c->target)) {
        const CUnit& t = reg.get<CUnit>(c->target);
        ImGui::Text("Target: %s (%s)", scene_.types().get(t.type).name.c_str(), scene_.ownerName(t.owner).c_str());
      } else {
        ImGui::TextDisabled("No target");
      }
      for (size_t i = 0; i < d.weapons.size(); ++i) {
        const WeaponDef& wd = scene_.weapons().get(d.weapons[i]);
        const float ready = 1.0f - static_cast<float>(c->cooldown[i]) / wd.cooldown;
        ImGui::ProgressBar(ready, ImVec2(-1, 0), wd.name.c_str());
      }
    }
    return;
  }
  // Many: a breakdown by type and side.
  struct Row {
    uint16_t type;
    uint8_t owner;
    int count;
    int64_t health, maxHealth;
  };
  std::vector<Row> rows;
  for (Entity e : sel) {
    if (!reg.alive(e)) continue;
    const CUnit& u = reg.get<CUnit>(e);
    auto it = std::find_if(rows.begin(), rows.end(), [&](const Row& r) { return r.type == u.type && r.owner == u.owner; });
    if (it == rows.end()) {
      rows.push_back({u.type, u.owner, 0, 0, 0});
      it = rows.end() - 1;
    }
    ++it->count;
    it->health += u.health;
    it->maxHealth += scene_.types().get(u.type).maxHealth;
  }
  for (const Row& r : rows) {
    ImGui::TextColored(toImVec(scene_.teamColor(r.owner)), "%5d", r.count);
    ImGui::SameLine();
    ImGui::Text("%s", scene_.types().get(r.type).name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f%% hp", 100.0 * static_cast<double>(r.health) / static_cast<double>(std::max<int64_t>(1, r.maxHealth)));
  }
}

void Editor::drawSidesTab() {
  auto& players = scene_.players();
  bool changed = false;
  ImGui::TextWrapped("Up to 8 sides. Sides on the same team are allies: they share vision and don't fight.");
  for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
    PlayerSetup& s = players[p];
    ImGui::PushID(p);
    ImGui::ColorButton("##c", toImVec(s.color, s.active ? 1.0f : 0.35f), ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
    ImGui::SameLine();
    const std::string header = std::to_string(p + 1) + ". " + s.name + (s.active ? "" : "  (inactive)") +
                               (p == scene_.player() ? "  - you" : "") + "###side";
    if (ImGui::CollapsingHeader(header.c_str(), p < 2 ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
      changed |= ImGui::Checkbox("In the scenario", &s.active);
      ImGui::SetNextItemWidth(-70);
      ImGui::InputText("Name", &s.name);
      int f = factionIndex(s.faction);
      ImGui::SetNextItemWidth(-70);
      if (ImGui::Combo("Faction", &f, factionComboItems())) s.faction = kPlayableFactions[f].id;
      ImVec4 col = toImVec(s.color);
      ImGui::SetNextItemWidth(-70);
      if (ImGui::ColorEdit3("Colour", &col.x, ImGuiColorEditFlags_NoInputs)) {
        s.color = fromImVec(col);
        changed = true;
      }
      int team = s.team + 1;
      ImGui::SetNextItemWidth(-70);
      if (ImGui::SliderInt("Team", &team, 1, MAX_PLAYERS)) {
        s.team = static_cast<uint8_t>(team - 1);
        changed = true;
      }
      int ctl = s.human ? 0 : 1;
      ImGui::SetNextItemWidth(-70);
      if (ImGui::Combo("Played by", &ctl, "Human\0Computer (AI in Milestone 6)\0")) s.human = ctl == 0;
      ImGui::SetNextItemWidth(-70);
      ImGui::InputInt("Supplies", &s.credits, 500, 5000);
      s.credits = std::clamp(s.credits, 0, 1000000);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("What this side starts with. Saved with the scenario.");
      ImGui::SameLine();
      if (ImGui::SmallButton("Give now")) {
        Command c;
        c.type = CommandType::SetCredits;
        c.player = p;
        c.amount = s.credits;
        scene_.sim().queue(std::move(c));
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set this side's supplies in the running battle too.");
      if (ImGui::Button("Start view here")) {
        const ImGuiIO& io = ImGui::GetIO();
        const Vec2f c = scene_.groundAt({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f});
        s.startX = static_cast<int32_t>(c.x);
        s.startY = static_cast<int32_t>(c.y);
        scene_.showStatus(s.name + " will start here", 2);
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("This side's camera starts at the current screen centre.");
      ImGui::SameLine();
      ImGui::BeginDisabled(!s.active || p == scene_.player());
      if (ImGui::Button("Command this side")) scene_.setPlayer(p);
      ImGui::EndDisabled();
      int count = 0;
      Registry& reg = scene_.sim().registry();
      for (uint32_t e : reg.pool<CUnit>().entities()) count += reg.pool<CUnit>().get(e).owner == p;
      ImGui::TextDisabled("%d units on the field", count);
    }
    ImGui::PopID();
  }
  if (changed) scene_.applyPlayers();
}

void Editor::drawBattleTab() {
  Map& m = scene_.map();
  ImGui::SeparatorText("Creation tools");
  if (ImGui::Button("Rules editor", ImVec2(-1, 0))) scene_.rulesEditor().open();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Change units, buildings and weapons; the battle follows as you type.");
  ImGui::SeparatorText("Scenario");
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##name", "Scenario name", &m.name);
  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##author", "Author", &m.author);
  ImGui::InputTextMultiline("##desc", &m.description, ImVec2(-1, 70));
  if (m.description.empty() && !ImGui::IsItemActive()) {
    ImGui::SameLine(16);
    ImGui::TextDisabled("Briefing / description");
  }

  ImGui::SeparatorText("Time");
  bool paused = scene_.paused();
  if (ImGui::Checkbox("Paused", &paused)) scene_.setPaused(paused);
  ImGui::SameLine();
  float speed = scene_.speed();
  ImGui::SetNextItemWidth(-1);
  if (ImGui::SliderFloat("##speed", &speed, 0.25f, 8.0f, "speed x%.2f", ImGuiSliderFlags_Logarithmic)) scene_.setSpeed(speed);

  ImGui::SeparatorText("Snapshot");
  if (ImGui::Button("Take snapshot")) scene_.takeSnapshot();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Remember the whole battle now: terrain, every unit's position and health, the sides.");
  ImGui::SameLine();
  ImGui::BeginDisabled(!scene_.hasSnapshot());
  if (ImGui::Button("Restore")) scene_.restoreSnapshot();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Go back to the snapshot: try a battle, then rewind and try again. Clears undo.");

  ImGui::SeparatorText("Battle tools");
  const float bw = (kPanelWidth - 32) / 3.0f;
  for (Tool t : {Tool::Artillery, Tool::Smite, Tool::Heal}) {
    const bool sel = tool_ == t;
    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.50f, 0.22f, 1));
    const char* name = t == Tool::Artillery ? "Artillery" : t == Tool::Smite ? "Smite" : "Heal";
    if (ImGui::Button(name, ImVec2(bw, 0))) tool_ = sel ? Tool::SelectUnits : t;
    if (sel) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t == Tool::Artillery ? "Click to call a 105 mm barrage. Hurts every side."
                                                       : t == Tool::Smite    ? "Destroy everything under the brush."
                                                                             : "Restore units under the brush to full health (undoable).");
    if (t != Tool::Heal) ImGui::SameLine();
  }
  if (tool_ >= Tool::Artillery) {
    ImGui::SetNextItemWidth(-60);
    ImGui::SliderFloat("Size", &radius_, 1.0f, 16.0f, "%.0f tiles");
    if (tool_ == Tool::Artillery) {
      ImGui::SetNextItemWidth(-60);
      ImGui::SliderInt("Rounds", &strikeRounds_, 1, 60);
    }
  }

  ImGui::SeparatorText("View");
  ImGui::Checkbox("Show what the player's side would know (intel) while editing", &scene_.editorShowsFog());
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("See the battlefield as the side you command sees it.");
  if (ImGui::Button("Clear wrecks, bodies and craters")) scene_.fx().clear();

  ImGui::SeparatorText("Everyone");
  Registry& reg = scene_.sim().registry();
  if (ImGui::Button("Select all units")) {
    auto& sel = scene_.selection();
    sel.clear();
    for (uint32_t e : reg.pool<CUnit>().entities()) sel.push_back(reg.handle(e));
    tool_ = Tool::SelectUnits;
    tab_ = 1;
    tabRequest_ = 1;
  }
  ImGui::SameLine();
  if (ImGui::Button("Heal all")) {
    auto keep = scene_.selection();
    auto& sel = scene_.selection();
    sel.clear();
    for (uint32_t e : reg.pool<CUnit>().entities()) sel.push_back(reg.handle(e));
    setSelectionHealth(1.0f);
    sel = keep;
  }
}

}  // namespace legion

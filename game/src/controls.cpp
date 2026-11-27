// Legion Classic - controls added in Milestone 9.15: idle units, the unit
// bar (control groups along the bottom), photo mode, and the replay camera
// (follow a unit, follow the fighting).
#include <algorithm>
#include <cmath>
#include <ctime>

#include <imgui.h>

#include "game_scene.hpp"
#include "pioneer/math/iso.hpp"
#include "sidebar.hpp"
#include "ui_style.hpp"

using namespace pioneer;

namespace legion {

void drawUnitIcon(ImDrawList* dl, const SpriteRenderer& r, const UnitArt& art, ImVec2 p0, ImVec2 size, ImU32 tint);

// Our units with nothing to do: standing, no order, not in a building, not at work.
std::vector<Entity> GameScene::idleUnits() const {
  std::vector<Entity> out;
  Registry& reg = sim_->registry();
  for (uint32_t e : reg.pool<CUnit>().entities()) {
    const CUnit& u = reg.pool<CUnit>().get(e);
    if (u.owner != player_ || u.health <= 0) continue;
    const UnitTypeDef& d = types_.get(u.type);
    if (d.isStructure() || d.speed <= 0 || d.deploysTo >= 0) continue;
    if (reg.pool<CGarrisoned>().has(e)) continue;
    const COrder* o = reg.pool<COrder>().tryGet(e);
    const CMover* m = reg.pool<CMover>().tryGet(e);
    if (!o || o->type != OrderType::Idle || (m && (m->active || m->request))) continue;
    if (const CCombat* c = reg.pool<CCombat>().tryGet(e); c && c->target.valid()) continue;  // busy fighting
    if (reg.pool<CGroupMember>().has(e)) {
      // A bound unit counts once: by its first member.
      const CGroup* g = sim_->groupOf(reg.handle(e));
      if (g && !g->members.empty() && g->members.front().index != e) continue;
    }
    out.push_back(reg.handle(e));
  }
  return out;
}

void GameScene::selectIdle(bool all) {
  std::vector<Entity> idle = idleUnits();
  if (idle.empty()) {
    showStatus("Nobody is idle", 1.5f);
    return;
  }
  Registry& reg = sim_->registry();
  auto withGroup = [&](Entity e) {
    std::vector<Entity> who;
    if (const CGroup* g = sim_->groupOf(e)) {
      for (Entity m : g->members)
        if (reg.alive(m)) who.push_back(m);
    } else {
      who.push_back(e);
    }
    return who;
  };
  selection_.clear();
  if (all) {
    for (Entity e : idle)
      for (Entity m : withGroup(e)) selection_.push_back(m);
    showStatus(std::to_string(idle.size()) + " idle", 1.5f);
    return;
  }
  idleCursor_ = (idleCursor_ + 1) % idle.size();
  selection_ = withGroup(idle[idleCursor_]);
  const CPosition& p = reg.get<CPosition>(idle[idleCursor_]);
  centerOn({p.x, p.y});
}

// Control groups 1-9 and the idle units, as plates along the bottom of the battlefield.
void GameScene::drawUnitBar() {
  if (!settings_.unitBar || mode_ != Mode::Game || photoMode_ || sim_->cinematic(player_)) return;
  if (sim_->ai().player(player_).enabled) return;  // watching the computer play
  Registry& reg = sim_->registry();
  struct Plate {
    int group;  // 1..9, 0: idle
    int count;
    int type;   // the commonest type in it
  };
  std::vector<Plate> plates;
  for (int g = 1; g <= 9; ++g) {
    std::vector<int> counts(types_.size(), 0);
    int n = 0;
    for (Entity e : groups_[static_cast<size_t>(g)])
      if (reg.alive(e) && reg.has<CUnit>(e)) {
        ++counts[reg.get<CUnit>(e).type];
        ++n;
      }
    if (n == 0) continue;
    plates.push_back({g, n, static_cast<int>(std::max_element(counts.begin(), counts.end()) - counts.begin())});
  }
  const std::vector<Entity> idle = idleUnits();
  if (plates.empty() && idle.empty()) return;
  const ImGuiIO& io = ImGui::GetIO();
  const float pw = 64, ph = 50, gap = 4;
  const float width = (plates.size() + 1) * (pw + gap) + 12;
  ImGui::SetNextWindowPos(ImVec2(8, io.DisplaySize.y - ph - 18));
  ImGui::SetNextWindowSize(ImVec2(width, ph + 12));
  ImGui::SetNextWindowBgAlpha(0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
  ImGui::Begin("##unitbar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  auto plate = [&](const char* id, const char* label, int count, int type, bool lit) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(pw, ph));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 b(a.x + pw, a.y + ph);
    ui::panel(dl, a, b, false);
    dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, b.y - 2), hovered ? IM_COL32(52, 52, 40, 235) : IM_COL32(26, 27, 21, 225), 1.0f);
    if (type >= 0) drawUnitIcon(dl, renderer_, unitArt_[static_cast<size_t>(type)], ImVec2(a.x + 14, a.y + 3), ImVec2(pw - 16, ph - 8), IM_COL32_WHITE);
    dl->AddText(ImVec2(a.x + 5, a.y + 3), lit ? ui::kBrass : ui::kStencil, label);
    char n[16];
    std::snprintf(n, sizeof n, "%d", count);
    dl->AddText(ImVec2(b.x - ImGui::CalcTextSize(n).x - 5, b.y - 17), ui::kStencil, n);
    if (lit) dl->AddRect(a, b, ui::kBrass, 2.0f, 0, 1.5f);
    return clicked;
  };
  for (const Plate& p : plates) {
    char id[8], label[4];
    std::snprintf(id, sizeof id, "##g%d", p.group);
    std::snprintf(label, sizeof label, "%d", p.group);
    // Lit when the selection is this group.
    std::vector<Entity> g = groups_[static_cast<size_t>(p.group)];
    std::erase_if(g, [&](Entity x) { return !reg.alive(x); });
    const bool lit = !g.empty() && g == selection_;
    if (plate(id, label, p.count, p.type, lit)) {
      if (lit || (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
        const CPosition& c = reg.get<CPosition>(g.front());
        centerOn({c.x, c.y});
      }
      selection_ = g;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Group %d: %d (press %d; Ctrl+%d makes it; click again to look)", p.group, p.count, p.group, p.group);
    ImGui::SameLine(0, gap);
  }
  if (!idle.empty()) {
    const int type = reg.get<CUnit>(idle.front()).type;
    if (plate("##idle", "Idle", static_cast<int>(idle.size()), type, false)) selectIdle(ImGui::GetIO().KeyShift);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%zu with nothing to do. Click (or .) for the next; Shift: all of them.", idle.size());
  }
  ImGui::End();
  ImGui::PopStyleVar();
}

void GameScene::togglePhotoMode() {
  photoMode_ = !photoMode_;
  if (photoMode_) {
    photoWasPaused_ = paused_;
    if (!online()) paused_ = true;
    photoHint_ = 4.0f;
    attackMoveMode_ = attackGroundMode_ = false;
    abilityMode_ = -1;
    if (sidebar_) sidebar_->cancel();
  } else if (!online()) {
    paused_ = photoWasPaused_;
  }
}

// Photo mode's keys: look around, take pictures, leave. Everything else waits.
bool GameScene::photoModeEvent(const SDL_Event& e, bool uiMouse) {
  if (!photoMode_) return false;
  const float ps = app_.pixelScale();
  if (e.type == SDL_EVENT_KEY_DOWN) {
    const SDL_Keycode k = e.key.key;
    if (k == SDLK_ESCAPE || settings_.pressed(InputAction::PhotoMode, e.key)) {
      togglePhotoMode();
    } else if (settings_.pressed(InputAction::Screenshot, e.key) || k == SDLK_RETURN) {
      const std::time_t t = std::time(nullptr);
      char name[64];
      std::strftime(name, sizeof name, "photo_%Y%m%d_%H%M%S.png", std::localtime(&t));
      app_.requestScreenshot(app_.userDir() + name, false);
      photoSaved_ = app_.userDir() + name;
      photoHint_ = 3.0f;
    } else if (settings_.pressed(InputAction::Pause, e.key) && !online()) {
      paused_ = !paused_;
    }
    return true;
  }
  if (e.type == SDL_EVENT_MOUSE_MOTION && mmbDown_) mmbDrag(e.motion.xrel * ps, e.motion.yrel * ps);
  if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_MIDDLE) mmbDown_ = true;
  if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_MIDDLE) mmbDown_ = false;
  if (e.type == SDL_EVENT_MOUSE_WHEEL && !uiMouse && e.wheel.y != 0) cam_.zoomStep(e.wheel.y > 0 ? +1 : -1, mouse_);
  return true;
}

void GameScene::drawPhotoHint(float dt) {
  photoHint_ -= dt;
  if (photoHint_ <= 0) return;
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 16), ImGuiCond_Always, ImVec2(0.5f, 1));
  ImGui::SetNextWindowBgAlpha(0.6f * std::min(1.0f, photoHint_));
  ImGui::Begin("##photohint", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoInputs);
  if (!photoSaved_.empty()) ImGui::Text("Saved %s", photoSaved_.c_str());
  else
    ImGui::TextUnformatted("PHOTO MODE  -  F12 or Enter: take a photo   WASD, Q/R, wheel, middle mouse: the camera   "
                           "Space: pause   P or Esc: back");
  ImGui::End();
}

// Replays: the camera can ride with one unit, or go where the fighting is.
void GameScene::updateReplayCamera(float dt) {
  if (mode_ != Mode::Replay) {
    followUnit_ = {};
    followAction_ = false;
    return;
  }
  Registry& reg = sim_->registry();
  Vec2f want{-1, -1};
  if (followUnit_.valid()) {
    if (!reg.alive(followUnit_) || !reg.has<CPosition>(followUnit_)) {
      followUnit_ = {};
    } else {
      const CPosition& p = reg.get<CPosition>(followUnit_);
      const CPrevPosition& q = reg.get<CPrevPosition>(followUnit_);
      want = {q.x + (p.x - q.x) * alpha_, q.y + (p.y - q.y) * alpha_};
    }
  } else if (followAction_ && actionHeat_ > 0.01f) {
    want = actionAt_;
  }
  actionHeat_ = std::max(0.0f, actionHeat_ - dt * 0.2f);
  if (want.x < 0) return;
  const Vec2f now = cameraFocus();
  const float k = std::min(1.0f, dt * (followUnit_.valid() ? 6.0f : 1.2f));
  focusCamera({now.x + (want.x - now.x) * k, now.y + (want.y - now.y) * k});
}

// Where the fighting is (replays): shots landing and men falling pull it along.
void GameScene::noteAction(const SimEvent& ev) {
  if (ev.type != EventType::Impact && ev.type != EventType::UnitDied) return;
  const float w = ev.type == EventType::UnitDied ? 0.06f : 0.02f;
  if (actionHeat_ <= 0.01f) actionAt_ = {static_cast<float>(ev.at.x), static_cast<float>(ev.at.y)};
  actionAt_ = {actionAt_.x + (ev.at.x - actionAt_.x) * w, actionAt_.y + (ev.at.y - actionAt_.y) * w};
  actionHeat_ = std::min(1.0f, actionHeat_ + w);
}

}  // namespace legion

// Mission presentation (9.9): radio messages, countdown clocks, the
// cinematic letterbox, scripted camera glides, the play area shaded on the
// ground, goal and danger markers, script music and sounds, and the mission
// debugger that shows a scenario's triggers, flags and counters while it runs.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game_audio.hpp"
#include "game_scene.hpp"
#include "editor.hpp"
#include "sidebar.hpp"
#include "ui_style.hpp"
#include "pioneer/core/log.hpp"

using namespace pioneer;

namespace legion {

namespace {
bool forMe(uint8_t owner, uint8_t me) { return owner == ANY_PLAYER || owner == me; }
std::string clock(int32_t seconds) {
  char buf[16];
  if (seconds >= 3600) std::snprintf(buf, sizeof buf, "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
  else std::snprintf(buf, sizeof buf, "%02d:%02d", seconds / 60, seconds % 60);
  return buf;
}
float ease(float t) { return t * t * (3.0f - 2.0f * t); }
ImVec4 teamCol(uint32_t c) {
  return ImVec4((c & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, ((c >> 16) & 0xFF) / 255.0f, 1.0f);
}
}  // namespace

bool GameScene::missionInputBlocked() const {
  return mode_ == Mode::Game && sim_ && sim_->cinematic(player_) && !editor_->active();
}

void GameScene::onMissionEvent(const SimEvent& ev) {
  if (!forMe(ev.owner, player_) && ev.type != EventType::PlayAreaChanged) return;
  switch (ev.type) {
    case EventType::Transmission: {
      Transmission t;
      const size_t colon = ev.text.find(':');
      if (colon != std::string::npos && colon < 48) {
        t.speaker = ev.text.substr(0, colon);
        t.text = ev.text.substr(colon + 1);
        while (!t.text.empty() && t.text.front() == ' ') t.text.erase(t.text.begin());
      } else {
        t.speaker = "Radio";
        t.text = ev.text;
      }
      // Long enough to read: about fifteen characters a second, at least four seconds.
      t.duration = ev.amount > 0 ? static_cast<float>(ev.amount) : std::clamp(2.5f + t.text.size() / 15.0f, 4.0f, 14.0f);
      PNR_INFO("Radio: %s: %s", t.speaker.c_str(), t.text.c_str());
      transmissions_.push_back(t);
      radioLog_.push_back({t.speaker + ": " + t.text, static_cast<float>(sim_->tickCount()) / TICKS_PER_SECOND});
      if (radioLog_.size() > 40) radioLog_.erase(radioLog_.begin());
      break;
    }
    case EventType::CinematicChanged:
      if (ev.amount) {
        selection_.clear();
        attackMoveMode_ = false;
      }
      break;
    case EventType::CameraPan:
      if (ev.at.x < 0 || mode_ != Mode::Game) break;
      if (ev.amount <= 0) {
        panDuration_ = 0;
        centerOn(ev.at);
      } else {
        panFrom_ = cameraFocus();
        panTo_ = {static_cast<float>(ev.at.x), static_cast<float>(ev.at.y)};
        panTime_ = 0;
        panDuration_ = static_cast<float>(ev.amount);
      }
      break;
    case EventType::PlaySound:
      if (!audio_ || ev.text.empty()) break;
      if (ev.at.x >= 0) {
        audio::Emitter em;
        em.x = ev.at.x / 256.0f;
        em.y = ev.at.y / 256.0f;
        em.positional = true;
        audio_->play(ev.text, em);
      } else {
        audio_->play(ev.text);
      }
      break;
    case EventType::PlayMusic:
      if (audio_) audio_->scriptMusic(ev.text);
      break;
    case EventType::CountdownDone:
      showStatus(ev.text + ": time", 3);
      if (audio_) audio_->ui("ui_objective");
      break;
    case EventType::PlayAreaChanged:
      updateCameraBounds();
      break;
    default:
      break;
  }
}

void GameScene::updateMission(float dt) {
  if (!sim_) return;
  // The radio: one message at a time, the next once it has been read.
  if (!transmissions_.empty()) {
    Transmission& t = transmissions_.front();
    if (t.age == 0 && audio_ && mode_ == Mode::Game) audio_->ui("radio");
    t.age += dt * (transmissions_.size() > 3 ? 2.0f : 1.0f);  // a backlog is read faster
    if (t.age > t.duration) transmissions_.pop_front();
  }
  // A scripted camera glide.
  if (panDuration_ > 0) {
    panTime_ += dt;
    const float k = ease(std::clamp(panTime_ / panDuration_, 0.0f, 1.0f));
    focusCamera({panFrom_.x + (panTo_.x - panFrom_.x) * k, panFrom_.y + (panTo_.y - panFrom_.y) * k});
    if (panTime_ >= panDuration_) panDuration_ = 0;
  }
  // The letterbox eases in and out.
  const float want = missionInputBlocked() ? 1.0f : 0.0f;
  letterbox_ += std::clamp(want - letterbox_, -dt * 2.5f, dt * 2.5f);
  // The play area: lit inside, dimmed outside (the terrain's old fog shader does the shading).
  const bool limited = sim_->playAreaLimited(player_) && mode_ != Mode::Menu && !(editor_->active() && !editorFog_);
  if (!limited) {
    terrain_.setFog(nullptr, 0);
    playGridRect_ = {0, 0, -1, -2};
    return;
  }
  const TileRect r = sim_->playArea(player_);
  if (r.x0 != playGridRect_.x0 || r.y0 != playGridRect_.y0 || r.x1 != playGridRect_.x1 || r.y1 != playGridRect_.y1 ||
      playGrid_.size() != static_cast<size_t>(map_.width()) * map_.height()) {
    playGridRect_ = r;
    playGrid_.assign(static_cast<size_t>(map_.width()) * map_.height(), 2);  // "explored": dimmed and drained of colour
    for (int y = r.y0; y <= r.y1; ++y)
      for (int x = r.x0; x <= r.x1; ++x) playGrid_[static_cast<size_t>(y) * map_.width() + x] = 1;
    ++playGridRevision_;
    updateCameraBounds();
  }
  terrain_.setFog(&playGrid_, playGridRevision_);
}

// Markers and open objectives with an area: a frame on the ground with a
// label; off screen, an arrow at the edge pointing the way.
void GameScene::drawMissionMarkers(ImDrawList* dl) {
  if (!sim_ || mode_ == Mode::Menu) return;
  const float ps = app_.pixelScale();
  const float vw = static_cast<float>(app_.pixelWidth()) / ps - Sidebar::kWidth, vh = static_cast<float>(app_.pixelHeight()) / ps;
  const ScenarioScript& script = sim_->script();
  auto toScreen = [&](float lx, float ly) {
    const Vec2f s = cam_.worldToScreen(groundToScreenWorld(lx, ly));
    return ImVec2(s.x / ps, s.y / ps);
  };
  const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f);
  // Labels never sit on top of each other: each is pushed down until it is clear.
  std::vector<std::pair<ImVec2, ImVec2>> placed;
  auto clear = [&](ImVec2 p, ImVec2 size) {
    for (int tries = 0; tries < 8; ++tries) {
      bool hit = false;
      for (const auto& [a, b] : placed)
        if (p.x < b.x && p.x + size.x > a.x && p.y < b.y && p.y + size.y > a.y) {
          p.y = b.y + 2;
          hit = true;
        }
      if (!hit) break;
    }
    placed.push_back({p, ImVec2(p.x + size.x, p.y + size.y)});
    return p;
  };
  auto mark = [&](int area, const std::string& label, int kind) {
    if (area < 0 || area >= static_cast<int>(script.areas.size())) return;
    const ScriptArea& a = script.areas[static_cast<size_t>(area)];
    const ImU32 col = kind == 1 ? IM_COL32(236, 84, 64, 200 + static_cast<int>(55 * pulse))
                      : kind == 2 ? IM_COL32(150, 190, 230, 200)
                                  : IM_COL32(240, 206, 96, 190 + static_cast<int>(65 * pulse));
    // The outline follows the ground, a point every tile.
    std::vector<ImVec2> pts;
    auto edge = [&](int x0, int y0, int x1, int y1) {
      const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
      for (int i = 0; i < n; ++i)
        pts.push_back(toScreen((x0 + (x1 - x0) * static_cast<float>(i) / n) * 256.0f, (y0 + (y1 - y0) * static_cast<float>(i) / n) * 256.0f));
    };
    edge(a.x0, a.y0, a.x1 + 1, a.y0);
    edge(a.x1 + 1, a.y0, a.x1 + 1, a.y1 + 1);
    edge(a.x1 + 1, a.y1 + 1, a.x0, a.y1 + 1);
    edge(a.x0, a.y1 + 1, a.x0, a.y0);
    if (pts.size() >= 3) dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, ImDrawFlags_Closed, 2.0f);
    const ImVec2 c = toScreen((a.x0 + a.x1 + 1) * 128.0f, (a.y0 + a.y1 + 1) * 128.0f);
    const bool on = c.x > 0 && c.y > 0 && c.x < vw && c.y < vh;
    if (on) {
      if (!label.empty()) {
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const ImVec2 at = clear(ImVec2(c.x - ts.x * 0.5f - 6, c.y - 30), ImVec2(ts.x + 12, ts.y + 4));
        dl->AddRectFilled(at, ImVec2(at.x + ts.x + 12, at.y + ts.y + 4), IM_COL32(16, 14, 10, 190), 3);
        dl->AddText(ImVec2(at.x + 6, at.y + 2), col, label.c_str());
      }
      dl->AddTriangleFilled(ImVec2(c.x, c.y - 4), ImVec2(c.x - 7, c.y - 14), ImVec2(c.x + 7, c.y - 14), col);
      return;
    }
    // Off screen: an arrow on the edge, toward it.
    const ImVec2 mid(vw * 0.5f, vh * 0.5f);
    float dx = c.x - mid.x, dy = c.y - mid.y;
    const float len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
    dx /= len;
    dy /= len;
    const float k = std::min(std::abs(dx) > 1e-4f ? (mid.x - 28) / std::abs(dx) : 1e9f, std::abs(dy) > 1e-4f ? (mid.y - 28) / std::abs(dy) : 1e9f);
    const ImVec2 p(mid.x + dx * k, mid.y + dy * k);
    const ImVec2 tip(p.x + dx * 12, p.y + dy * 12), l(p.x - dy * 8, p.y + dx * 8), rr(p.x + dy * 8, p.y - dx * 8);
    dl->AddTriangleFilled(tip, l, rr, col);
    if (!label.empty()) {
      const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
      const ImVec2 at = clear(ImVec2(std::clamp(p.x - dx * 18 - ts.x * 0.5f - 4, 4.0f, vw - ts.x - 12),
                                     std::clamp(p.y - dy * 18 - ts.y * 0.5f, 4.0f, vh - ts.y - 8)),
                              ImVec2(ts.x + 8, ts.y + 4));
      dl->AddRectFilled(at, ImVec2(at.x + ts.x + 8, at.y + ts.y + 4), IM_COL32(16, 14, 10, 170), 3);
      dl->AddText(ImVec2(at.x + 4, at.y + 2), col, label.c_str());
    }
  };
  for (const Objective& o : sim_->objectives())
    if (o.state == 0 && o.area >= 0 && (o.player == ANY_PLAYER || sim_->allied(o.player, player_))) mark(o.area, o.text, 0);
  for (const MissionMarker& m : sim_->markers())
    if (forMe(m.player, player_)) mark(m.area, m.label, m.kind);
}

void GameScene::drawMission() {
  if (!sim_ || mode_ == Mode::Menu) return;
  const ImGuiIO& io = ImGui::GetIO();
  const bool cine = letterbox_ > 0.001f;
  const float viewW = io.DisplaySize.x - (cine ? 0.0f : Sidebar::kWidth);
  ImDrawList* fg = ImGui::GetForegroundDrawList();
  // Letterbox.
  if (cine) {
    const float bar = io.DisplaySize.y * 0.11f * ease(letterbox_);
    fg->AddRectFilled(ImVec2(0, 0), ImVec2(io.DisplaySize.x, bar), IM_COL32(0, 0, 0, 255));
    fg->AddRectFilled(ImVec2(0, io.DisplaySize.y - bar), io.DisplaySize, IM_COL32(0, 0, 0, 255));
  }
  // Countdown clocks, top centre (under the hint panel's place).
  {
    float y = 10;
    for (const MissionCountdown& d : sim_->countdowns()) {
      if (!forMe(d.player, player_) && !sim_->allied(d.player, player_)) continue;
      const int32_t left = sim_->countdownLeft(d.label);
      const std::string t = clock(left);
      const bool urgent = !d.done && left <= 60, flash = !d.done && left <= 10 && std::fmod(ImGui::GetTime(), 1.0) < 0.5;
      const ImU32 col = d.done ? IM_COL32(150, 150, 140, 255) : urgent ? IM_COL32(240, 96, 70, flash ? 140 : 255) : IM_COL32(240, 220, 160, 255);
      const float big = ImGui::GetFontSize() * 1.6f;
      const ImVec2 ls = ImGui::CalcTextSize(d.label.c_str());
      const float tw = ImGui::GetFont()->CalcTextSizeA(big, FLT_MAX, 0, t.c_str()).x;
      const float w = std::max(ls.x, tw) + 32, x = (viewW - w) * 0.5f;
      const float top = y + (cine ? io.DisplaySize.y * 0.11f * ease(letterbox_) : 0);
      fg->AddRectFilled(ImVec2(x, top), ImVec2(x + w, top + ls.y + big + 10), IM_COL32(18, 16, 12, 200), 4);
      fg->AddRect(ImVec2(x, top), ImVec2(x + w, top + ls.y + big + 10), IM_COL32(110, 98, 66, 220), 4);
      fg->AddText(ImVec2(x + (w - ls.x) * 0.5f, top + 3), IM_COL32(200, 190, 160, 255), d.label.c_str());
      fg->AddText(nullptr, big, ImVec2(x + (w - tw) * 0.5f, top + ls.y + 5), col, t.c_str());
      if (d.paused) fg->AddText(ImVec2(x + w - 18, top + 3), IM_COL32(200, 190, 160, 200), "||");
      y += ls.y + big + 16;
    }
  }
  // The radio: speaker and words typed out, lower left of the battlefield.
  if (!transmissions_.empty()) {
    const Transmission& t = transmissions_.front();
    const float a = std::clamp(std::min(t.age * 4.0f, (t.duration - t.age) * 2.0f), 0.0f, 1.0f);
    const size_t shown = std::min(t.text.size(), static_cast<size_t>(t.age * 45.0f));
    const float w = std::min(560.0f, viewW - 40 - (editor_->active() ? Editor::kPanelWidth : 0.0f));
    const float x = editor_->active() ? Editor::kPanelWidth + 20 : 20, y = io.DisplaySize.y - (cine ? io.DisplaySize.y * 0.11f * ease(letterbox_) + 12 : 30) - 96;
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::SetNextWindowBgAlpha(0.82f * a);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
    ImGui::Begin("##radio", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    // A little signal meter, alive while the words come in.
    for (int i = 0; i < 4; ++i) {
      const bool lit = shown < t.text.size() ? std::fmod(ImGui::GetTime() * 7.0 + i, 3.0) < 2.0 : i < 3;
      dl->AddRectFilled(ImVec2(p.x + i * 5, p.y + 12 - i * 3), ImVec2(p.x + i * 5 + 3, p.y + 14), lit ? IM_COL32(140, 220, 120, 255) : IM_COL32(60, 70, 55, 255));
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + 26, p.y));
    ImGui::TextColored(ImVec4(0.93f, 0.78f, 0.38f, 1), "%s", t.speaker.c_str());
    ImGui::PushTextWrapPos(w - 16);
    ImGui::TextUnformatted(t.text.substr(0, shown).c_str());
    ImGui::PopTextWrapPos();
    if (transmissions_.size() > 1) ImGui::TextDisabled("+%zu more", transmissions_.size() - 1);
    ImGui::End();
    ImGui::PopStyleVar();
  }
  // The radio log (L).
  if (showRadioLog_) {
    ImGui::SetNextWindowPos(ImVec2(viewW * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 360), ImGuiCond_Appearing);
    if (ImGui::Begin("Radio log", &showRadioLog_, ImGuiWindowFlags_NoSavedSettings)) {
      if (radioLog_.empty()) ImGui::TextDisabled("No messages yet.");
      for (auto it = radioLog_.rbegin(); it != radioLog_.rend(); ++it) {
        ImGui::TextDisabled("%s", clock(static_cast<int32_t>(it->second)).c_str());
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(it->first.c_str());
        ImGui::PopTextWrapPos();
      }
    }
    ImGui::End();
  }
  if (showMissionDebug_) drawMissionDebug();
}

// The mission debugger (from the editor's Script tab): every trigger with
// its state, the flags, counters, countdowns, markers, the play area and
// what each computer player has been told.
void GameScene::drawMissionDebug() {
  ImGui::SetNextWindowSize(ImVec2(470, 520), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Mission debugger", &showMissionDebug_, ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    return;
  }
  const Simulation& sim = *sim_;
  const uint32_t now = sim.tickCount();
  ImGui::Text("Time %s   Difficulty %s", clock(static_cast<int32_t>(now / TICKS_PER_SECOND)).c_str(),
              sim.missionDifficulty() == 0 ? "easy" : sim.missionDifficulty() == 2 ? "hard" : "normal");
  if (ImGui::CollapsingHeader("Triggers", ImGuiTreeNodeFlags_DefaultOpen)) {
    static char filter[64] = "";
    ImGui::InputTextWithHint("##filter", "filter by name", filter, sizeof filter);
    if (ImGui::BeginTable("##trig", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY, ImVec2(0, 200))) {
      ImGui::TableSetupColumn("#");
      ImGui::TableSetupColumn("Trigger", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("State");
      ImGui::TableSetupColumn("Last");
      ImGui::TableHeadersRow();
      for (size_t i = 0; i < sim.script().triggers.size(); ++i) {
        const Trigger& t = sim.script().triggers[i];
        if (filter[0] && t.name.find(filter) == std::string::npos) continue;
        const int ti = static_cast<int>(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%zu", i);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(t.name.c_str());
        ImGui::TableNextColumn();
        if (!sim.triggerEnabled(ti)) ImGui::TextDisabled("off");
        else if (sim.triggerFires(ti) == 0) ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.7f, 1), "waiting");
        else ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), t.repeat ? "fired x%u" : "fired", sim.triggerFires(ti));
        ImGui::TableNextColumn();
        if (sim.triggerFires(ti)) ImGui::Text("%s", clock(static_cast<int32_t>(sim.triggerLastFire(ti) / TICKS_PER_SECOND)).c_str());
      }
      ImGui::EndTable();
    }
  }
  if (ImGui::CollapsingHeader("Countdowns, counters and flags", ImGuiTreeNodeFlags_DefaultOpen)) {
    for (const MissionCountdown& d : sim.countdowns())
      ImGui::Text("clock  %-24s %s%s", d.label.c_str(), clock(sim.countdownLeft(d.label)).c_str(), d.done ? " (ran out)" : d.paused ? " (paused)" : "");
    for (const auto& [k, v] : sim.counters()) ImGui::Text("count  %-24s %d", k.c_str(), v);
    for (const auto& [k, v] : sim.flags()) ImGui::Text("flag   %-24s %d", k.c_str(), v);
    if (sim.countdowns().empty() && sim.counters().empty() && sim.flags().empty()) ImGui::TextDisabled("none");
  }
  if (ImGui::CollapsingHeader("Sides")) {
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
      if (!players_[p].active) continue;
      const AiPlayer& ai = sim.ai().player(p);
      std::string orders = !ai.enabled ? "human" : ai.order == 1 ? "attacking a place" : ai.order == 2 ? "defending a place" : "free hand";
      if (ai.enabled && ai.disabled)
        for (int b = 0; b < 8; ++b)
          if (!sim.ai().allowed(p, b)) orders += std::string(", no ") + kAiBehaviourIds[b];
      if (ai.enabled && ai.waveScale > 0) orders += ", attacks at " + std::to_string(ai.waveScale) + "%";
      ImGui::TextColored(teamCol(teamColors_[p]), "%s", players_[p].name.c_str());
      ImGui::SameLine();
      ImGui::TextDisabled("team %d  kills %d  losses %d  income %d%%", sim.team(p), sim.kills(p), sim.losses(p), sim.incomeRate(p));
      ImGui::TextWrapped("   %s%s%s", orders.c_str(), sim.playAreaLimited(p) ? ", play area set" : "", sim.cinematic(p) ? ", in a cinematic" : "");
    }
  }
  if (ImGui::CollapsingHeader("Named units")) {
    for (size_t i = 1; i < sim.names().size(); ++i)
      ImGui::Text("%-24s %zu alive", sim.names()[i].c_str(), sim.named(sim.names()[i]).size());
    if (sim.names().size() <= 1) ImGui::TextDisabled("none");
  }
  ImGui::End();
}

}  // namespace legion

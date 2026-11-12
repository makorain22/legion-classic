#include "menus.hpp"
#include "ui_style.hpp"
#include "rules_editor.hpp"
#include "game_audio.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "slave/core/rng.hpp"
#include "slave/sim/factions.hpp"

using namespace slave;

namespace legion {

namespace {
const uint32_t kSideColors[4] = {rgba8(196, 58, 44), rgba8(52, 96, 196), rgba8(66, 160, 72), rgba8(214, 184, 62)};
const ImVec4 kGold(0.93f, 0.78f, 0.38f, 1);

ImVec4 toImVec(uint32_t c) {
  return ImVec4((c & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f, ((c >> 16) & 0xff) / 255.0f, 1.0f);
}

// A big, flat menu button.
bool menuButton(const char* label, float width, bool enabled = true) {
  ImGui::BeginDisabled(!enabled);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.18f, 0.15f, 0.95f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.37f, 0.18f, 1));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.48f, 0.22f, 1));
  ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.06f, 0.5f));
  const bool pressed = ImGui::Button(label, ImVec2(width, 34));
  if (pressed) ui::clickSound(false);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
  ImGui::EndDisabled();
  return pressed;
}

void centredWindow(const char* id, ImVec2 size) {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size);
  ImGui::SetNextWindowBgAlpha(0.94f);
  ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
}

void title(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kGold);
  ImGui::SetWindowFontScale(1.5f);
  ImGui::TextUnformatted(text);
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();
  ImGui::Separator();
}
}  // namespace

Menus::Menus(GameScene& scene) : scene_(scene) {
  setup_.sides[0] = {true, "ger", true, 1, 0, kSideColors[0]};
  setup_.sides[1] = {true, "rus", false, 1, 1, kSideColors[1]};
  setup_.sides[2] = {false, "ger", false, 1, 2, kSideColors[2]};
  setup_.sides[3] = {false, "rus", false, 1, 3, kSideColors[3]};
}

void Menus::openInGameMenu() {
  page_ = Page::InGame;
  scene_.setPaused(true);
  error_.clear();
}

void Menus::close() {
  if (scene_.mode() == Mode::Menu) {
    page_ = Page::Main;
    return;
  }
  page_ = Page::None;
  scene_.setPaused(false);
}

bool Menus::backButton() {
  if (menuButton("Back", 120)) {
    page_ = returnTo_;
    error_.clear();
    return true;
  }
  return false;
}

void Menus::draw() {
  // In the main menu the battle behind is just scenery.
  if (scene_.mode() == Mode::Menu && page_ == Page::None) page_ = Page::Main;
  if (scene_.mode() == Mode::Menu) ui::setFaction("");  // the menus wear olive
  if (scene_.mode() == Mode::Replay) drawReplayBar();
  if (scene_.creationToolOpen() && scene_.mode() == Mode::Menu) return;  // the tool has the screen
  switch (page_) {
    case Page::Main: drawMain(); break;
    case Page::Skirmish: drawSkirmish(); break;
    case Page::Scenarios: drawScenarios(); break;
    case Page::LoadGame: drawFileList(false); break;
    case Page::Replays: drawFileList(true); break;
    case Page::InGame: drawInGame(); break;
    case Page::SaveGame: drawSaveGame(); break;
    case Page::About: drawAbout(); break;
    case Page::Settings: drawSettings(); break;
    case Page::Mods: drawMods(); break;
    case Page::Campaigns: drawCampaigns(); break;
    case Page::Missions: drawMissions(); break;
    case Page::Briefing: drawBriefing(); break;
    default: break;
  }
}

void Menus::drawMain() {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(340, 0));
  ImGui::SetNextWindowBgAlpha(0.88f);
  ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::PushStyleColor(ImGuiCol_Text, kGold);
  ImGui::SetWindowFontScale(2.2f);
  ImGui::TextUnformatted("LEGION CLASSIC");
  ImGui::SetWindowFontScale(1.0f);
  ImGui::PopStyleColor();
  ImGui::TextDisabled("1946. The war that never ended.");
  ImGui::Dummy(ImVec2(0, 10));
  const float w = 320;
  returnTo_ = Page::Main;
  {
    int tut = -1;
    for (size_t i = 0; i < scene_.campaigns().size(); ++i)
      if (scene_.campaigns()[i].tutorial) tut = static_cast<int>(i);
    if (tut >= 0) {
      const CampaignInfo& t = scene_.campaigns()[static_cast<size_t>(tut)];
      const int won = scene_.progress().wonCount(t);
      const std::string label = won == 0 ? "Tutorial: Basic Training" : "Tutorial (" + std::to_string(won) + " of " +
                                                                              std::to_string(t.missions.size()) + " done)";
      if (menuButton(label.c_str(), w)) openCampaign(t.id);
    }
  }
  if (menuButton("Campaigns", w)) {
    campaign_ = -1;
    page_ = Page::Campaigns;
  }
  if (menuButton("Skirmish", w)) page_ = Page::Skirmish;
  if (menuButton("Play a scenario", w)) {
    std::vector<FileEntry> builtIn;  // the ones that ship with the game, then the mods' missions
    for (const std::string& dir : scene_.scenarioDirs()) {
      refreshFiles(dir, ".lmap", false);
      builtIn.insert(builtIn.end(), files_.begin(), files_.end());
    }
    refreshFiles(scene_.mapsDir(), ".lmap", false);        // and the player's own
    for (FileEntry& f : files_) f.title += "  (your map)";
    files_.insert(files_.begin(), builtIn.begin(), builtIn.end());
    page_ = Page::Scenarios;
  }
  if (menuButton("Load game", w)) {
    refreshFiles(scene_.savesDir(), ".lsav", false);
    page_ = Page::LoadGame;
  }
  if (menuButton("Replays", w)) {
    refreshFiles(scene_.replaysDir(), ".lrep", true);
    page_ = Page::Replays;
  }
  if (menuButton("Scenario editor", w)) {
    SkirmishSetup s = setup_;
    s.sides[0].human = true;
    for (auto& side : s.sides) side.human = true;  // nobody plays on their own while you build
    scene_.startSkirmish(s);
    scene_.editorOpen(true);
    page_ = Page::None;  // the menu must close, or it keeps every click and key from the editor
  }
  if (menuButton("Encyclopedia", w)) scene_.openEncyclopedia();
  if (menuButton("Mods and creation tools", w)) openMods();
  if (menuButton("Settings", w)) page_ = Page::Settings;
  if (menuButton("About", w)) page_ = Page::About;
  if (menuButton("Quit", w)) scene_.quit();
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::TextDisabled("Slave Engine 1.0  -  build 0.9.0");
  if (scene_.content().modIds().size() > 1)
    ImGui::TextDisabled("%zu mod%s enabled", scene_.content().modIds().size() - 1,
                        scene_.content().modIds().size() > 2 ? "s" : "");
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Mods and creation tools
// ---------------------------------------------------------------------------
void Menus::openMods() {
  scene_.rescanMods();
  const auto& ids = scene_.content().modIds();
  modsDraft_.assign(ids.begin() + 1, ids.end());  // what is loaded now, in order
  error_.clear();
  page_ = Page::Mods;
}

void Menus::drawMods() {
  centredWindow("##mods", ImVec2(760, 580));
  title("Mods and creation tools");
  const auto& mods = scene_.installedMods();
  auto enabledAt = [&](const std::string& id) {
    auto it = std::find(modsDraft_.begin(), modsDraft_.end(), id);
    return it == modsDraft_.end() ? -1 : static_cast<int>(it - modsDraft_.begin());
  };

  // Left: every installed mod, ticked when enabled.
  ImGui::BeginChild("##modlist", ImVec2(330, 300), ImGuiChildFlags_Borders);
  if (mods.empty()) {
    ImGui::TextWrapped("No mods installed. Put mod folders in the mods folder, or make one below or with the "
                       "rules editor.");
  }
  for (const ModInfo& m : mods) {
    ImGui::PushID(m.id.c_str());
    bool on = enabledAt(m.id) >= 0;
    ImGui::BeginDisabled(!m.error.empty());
    if (ImGui::Checkbox("##on", &on)) {
      if (on) modsDraft_.push_back(m.id);
      else modsDraft_.erase(modsDraft_.begin() + enabledAt(m.id));
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const std::string label = m.name + (m.error.empty() ? "" : "  (broken)") + "##sel";
    if (ImGui::Selectable(label.c_str(), modSelected_ == m.id)) modSelected_ = m.id;
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::SameLine();

  // Right: the chosen mod, and the load order.
  ImGui::BeginChild("##moddetail", ImVec2(0, 300), ImGuiChildFlags_Borders);
  if (const ModInfo* m = findMod(mods, modSelected_)) {
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::TextUnformatted(m->name.c_str());
    ImGui::PopStyleColor();
    ImGui::TextDisabled("version %s%s%s", m->version.c_str(), m->author.empty() ? "" : "  -  by ", m->author.c_str());
    if (!m->description.empty()) ImGui::TextWrapped("%s", m->description.c_str());
    if (!m->dependencies.empty()) {
      std::string deps;
      for (const std::string& d : m->dependencies) deps += (deps.empty() ? "" : ", ") + d;
      ImGui::TextWrapped("Needs: %s", deps.c_str());
    }
    if (!m->error.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1), "%s", m->error.c_str());
    ImGui::TextDisabled("%s", m->user ? "In your mods folder" : "Installed with the game");
    std::string has;
    for (const char* part : {"rules", "art", "scenarios"}) {
      std::error_code ec;
      if (std::filesystem::exists(m->dir + part, ec) && !std::filesystem::is_empty(m->dir + part, ec))
        has += (has.empty() ? "" : ", ") + std::string(part);
    }
    ImGui::TextDisabled("Changes: %s", has.empty() ? "nothing yet" : has.c_str());
  } else {
    ImGui::TextDisabled("Pick a mod to see what it is.");
  }
  ImGui::SeparatorText("Load order");
  if (modsDraft_.empty()) ImGui::TextDisabled("The game as it comes, no mods.");
  for (size_t i = 0; i < modsDraft_.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const ModInfo* m = findMod(mods, modsDraft_[i]);
    ImGui::BeginDisabled(i == 0);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) std::swap(modsDraft_[i], modsDraft_[i - 1]);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(i + 1 == modsDraft_.size());
    if (ImGui::ArrowButton("##down", ImGuiDir_Down)) std::swap(modsDraft_[i], modsDraft_[i + 1]);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("%zu. %s", i + 1, m ? m->name.c_str() : modsDraft_[i].c_str());
    ImGui::PopID();
  }
  ImGui::TextDisabled("Later mods win where two change the same thing.");
  ImGui::EndChild();

  const auto& loaded = scene_.content().modIds();
  const bool changed = modsDraft_ != std::vector<std::string>(loaded.begin() + 1, loaded.end());
  if (menuButton(changed ? "Apply (reloads rules and art)" : "Applied", 260, changed)) {
    if (!scene_.setMods(modsDraft_, error_)) error_ = "Couldn't load those mods: " + error_;
    else openMods();
  }
  ImGui::SameLine();
  if (menuButton("Open mods folder", 200)) {
    std::string url = "file:///" + scene_.userModsDir();
    std::replace(url.begin(), url.end(), '\\', '/');
    SDL_OpenURL(url.c_str());
  }
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##newmod", "new mod name", newModName_, sizeof newModName_);
  ImGui::SameLine();
  ImGui::BeginDisabled(newModName_[0] == 0);
  if (ImGui::Button("Create mod")) {
    ModInfo made;
    if (createMod(scene_.userModsDir(), modIdFromName(newModName_), newModName_, made, error_)) {
      scene_.rescanMods();
      modSelected_ = made.id;
      scene_.settings().toolMod = made.id;
      newModName_[0] = 0;
      error_.clear();
    }
  }
  ImGui::EndDisabled();
  if (!error_.empty()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1), "%s", error_.c_str());
  if (!scene_.modWarnings().empty()) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1), "%s", scene_.modWarnings().c_str());

  ImGui::SeparatorText("Creation tools");
  if (menuButton("Rules editor", 200)) scene_.rulesEditor().open();
  ImGui::SameLine();
  ImGui::TextWrapped("Units, buildings, weapons and armour: change any number, see it in the battle behind, "
                     "save it as a mod.");
  ImGui::Dummy(ImVec2(0, 4));
  backButton();
  ImGui::End();
}

void Menus::drawSkirmish() {
  centredWindow("##skirmish", ImVec2(640, 540));
  title("Skirmish");
  {
    const auto maps = scene_.skirmishMaps();
    std::string current = "A new random map";
    for (const auto& m : maps)
      if (m.path == setup_.mapFile) current = m.name;
    ImGui::SetNextItemWidth(320);
    if (ImGui::BeginCombo("Map", current.c_str())) {
      if (ImGui::Selectable("A new random map", setup_.mapFile.empty())) setup_.mapFile.clear();
      for (const auto& m : maps) {
        const std::string label = m.name + "  (" + std::to_string(m.size) + ", " + m.biome + ")";
        if (ImGui::Selectable(label.c_str(), m.path == setup_.mapFile)) setup_.mapFile = m.path;
        if (ImGui::IsItemHovered() && !m.description.empty()) ImGui::SetTooltip("%s", m.description.c_str());
      }
      ImGui::EndCombo();
    }
    for (const auto& m : maps)
      if (m.path == setup_.mapFile) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m.description.c_str());
      }
  }
  ImGui::BeginDisabled(!setup_.mapFile.empty());
  static const char* sizes[] = {"64 x 64 (small)", "128 x 128", "192 x 192", "256 x 256 (large)", "512 x 512 (huge)"};
  static const int sizeValues[] = {64, 128, 192, 256, 512};
  int sizeIndex = 1;
  for (int i = 0; i < 5; ++i)
    if (sizeValues[i] == setup_.mapSize) sizeIndex = i;
  ImGui::SetNextItemWidth(200);
  if (ImGui::Combo("Map size", &sizeIndex, sizes, 5)) setup_.mapSize = sizeValues[sizeIndex];
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160);
  int biome = setup_.biome;
  if (ImGui::BeginCombo("Biome", biomeName(static_cast<Biome>(biome)))) {
    for (int b = 0; b < static_cast<int>(Biome::Count); ++b)
      if (ImGui::Selectable(biomeName(static_cast<Biome>(b)), b == biome)) setup_.biome = b;
    ImGui::EndCombo();
  }
  ImGui::SetNextItemWidth(200);
  if (ImGui::BeginCombo("Landscape", setup_.layout < 0 ? "Random (from the seed)" : mapLayoutName(static_cast<MapLayout>(setup_.layout)))) {
    if (ImGui::Selectable("Random (from the seed)", setup_.layout < 0)) setup_.layout = -1;
    for (int l = 0; l < static_cast<int>(MapLayout::Count); ++l)
      if (ImGui::Selectable(mapLayoutName(static_cast<MapLayout>(l)), l == setup_.layout)) setup_.layout = l;
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The lie of the land: river valley, front line river, confluence, lakeland, crossroads town,\n"
                      "ridge with passes, marshes, great lake, open steppe, deep forest, hill country or twin rivers.\n"
                      "Random lets the seed pick one - every seed gives a different map.");
  if (setup_.layout < 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("this seed: %s", mapLayoutName(layoutForSeed(setup_.seed)));
  }
  int seed = static_cast<int>(setup_.seed);
  ImGui::SetNextItemWidth(200);
  if (ImGui::InputInt("Map seed", &seed)) setup_.seed = static_cast<uint64_t>(std::max(0, seed));
  ImGui::SameLine();
  if (ImGui::Button("Random")) setup_.seed = hash32(static_cast<uint32_t>(std::time(nullptr))) % 100000;
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120);
  ImGui::InputInt("Supplies", &setup_.credits, 1000, 5000);
  setup_.credits = std::clamp(setup_.credits, 0, 100000);
  ImGui::SetNextItemWidth(320);
  ImGui::Combo("Starting positions", &setup_.startLayout,
               "Teams on opposite sides (west against east)\0Fixed: west, east, north, south\0Shuffled\0");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Teams on opposite sides: each team's bases stand together on its own side of the map,\n"
                      "facing the other across it.");

  ImGui::SeparatorText("Sides");
  static const char* who[] = {"Closed", "Human", "Computer: easy", "Computer: normal", "Computer: hard"};
  int humans = 0, open = 0;
  for (int i = 0; i < 4; ++i) {
    auto& s = setup_.sides[static_cast<size_t>(i)];
    ImGui::PushID(i);
    ImGui::ColorButton("##c", toImVec(s.color), ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20));
    ImGui::SameLine();
    int w = !s.open ? 0 : s.human ? 1 : 2 + s.difficulty;
    ImGui::SetNextItemWidth(190);
    if (ImGui::Combo("##who", &w, who, 5)) {
      s.open = w > 0;
      s.human = w == 1;
      if (w >= 2) s.difficulty = w - 2;
    }
    ImGui::SameLine();
    int f = slave::factionIndex(s.faction);
    ImGui::SetNextItemWidth(120);
    ImGui::BeginDisabled(!s.open);
    if (ImGui::Combo("##faction", &f, slave::factionComboItems())) s.faction = slave::kPlayableFactions[f].id;
    ImGui::SameLine();
    int team = s.team + 1;
    ImGui::SetNextItemWidth(100);
    if (ImGui::SliderInt("Team", &team, 1, 4)) s.team = team - 1;
    ImGui::EndDisabled();
    ImGui::PopID();
    if (s.open) {
      ++open;
      humans += s.human;
    }
  }
  ImGui::TextDisabled("Sides on the same team are allies. With no human side, you watch the computers play.");
  if (humans > 1) ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "More than one human side: Tab switches between them (hot seat).");
  ImGui::Dummy(ImVec2(0, 8));
  if (menuButton("Start", 200, open >= 1)) {
    page_ = Page::None;
    scene_.startSkirmish(setup_);
  }
  ImGui::SameLine();
  backButton();
  ImGui::End();
}

void Menus::refreshFiles(const std::string& dir, const char* ext, bool replays) {
  files_.clear();
  selected_ = -1;
  error_.clear();
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.path().extension() != ext) continue;
    FileEntry fe;
    fe.path = entry.path().string();
    fe.file = entry.path().stem().string();
    fe.title = fe.file;
    std::replace(fe.title.begin(), fe.title.end(), '_', ' ');
    if (std::string(ext) != ".lmap") {
      std::string meta;
      if (readSaveMeta(fe.path, meta, replays)) {
        const nlohmann::json j = nlohmann::json::parse(meta, nullptr, false);
        if (j.is_object()) {
          fe.title = j.value("title", fe.file);
          const uint32_t ticks = j.value("endTick", j.value("tick", 0u));
          char d[160];
          std::snprintf(d, sizeof d, "%s  -  %u:%02u game time", j.value("date", std::string()).c_str(),
                        ticks / TICKS_PER_SECOND / 60, ticks / TICKS_PER_SECOND % 60);
          fe.detail = d;
        }
      }
    }
    files_.push_back(std::move(fe));
  }
  std::sort(files_.begin(), files_.end(), [](const FileEntry& a, const FileEntry& b) { return a.file < b.file; });
}

void Menus::drawScenarios() {
  centredWindow("##scenarios", ImVec2(560, 460));
  title("Play a scenario");
  ImGui::TextDisabled("Missions that come with the game, then maps you made in the editor (%s).", scene_.mapsDir().c_str());
  if (ImGui::BeginListBox("##files", ImVec2(-1, 300))) {
    for (int i = 0; i < static_cast<int>(files_.size()); ++i)
      if (ImGui::Selectable(files_[static_cast<size_t>(i)].title.c_str(), selected_ == i)) selected_ = i;
    ImGui::EndListBox();
  }
  if (files_.empty()) ImGui::TextDisabled("No scenarios yet: make one in the scenario editor and save it.");
  if (!error_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
  if (menuButton("Play", 160, selected_ >= 0)) {
    if (scene_.startScenario(files_[static_cast<size_t>(selected_)].path, error_)) page_ = Page::None;
  }
  ImGui::SameLine();
  backButton();
  ImGui::End();
}

void Menus::drawFileList(bool replays) {
  centredWindow(replays ? "##replays" : "##load", ImVec2(600, 480));
  title(replays ? "Replays" : "Load game");
  if (ImGui::BeginListBox("##files", ImVec2(-1, 320))) {
    for (int i = 0; i < static_cast<int>(files_.size()); ++i) {
      const FileEntry& f = files_[static_cast<size_t>(i)];
      ImGui::PushID(i);
      if (ImGui::Selectable("##row", selected_ == i, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 34))) selected_ = i;
      ImGui::SameLine(8);
      ImGui::BeginGroup();
      ImGui::Text("%s", f.title.c_str());
      ImGui::TextDisabled("%s  (%s)", f.detail.c_str(), f.file.c_str());
      ImGui::EndGroup();
      ImGui::PopID();
    }
    ImGui::EndListBox();
  }
  if (files_.empty())
    ImGui::TextDisabled(replays ? "No replays yet. Your last game is kept automatically as last_game." : "No saved games yet.");
  if (!error_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
  if (menuButton(replays ? "Watch" : "Load", 160, selected_ >= 0)) {
    const std::string& path = files_[static_cast<size_t>(selected_)].path;
    const bool ok = replays ? scene_.playReplay(path, error_) : scene_.loadGame(path, error_);
    if (ok) {
      page_ = Page::None;
      scene_.setPaused(false);
    }
  }
  ImGui::SameLine();
  if (menuButton("Delete", 120, selected_ >= 0)) {
    std::error_code ec;
    std::filesystem::remove(files_[static_cast<size_t>(selected_)].path, ec);
    refreshFiles(replays ? scene_.replaysDir() : scene_.savesDir(), replays ? ".lrep" : ".lsav", replays);
  }
  ImGui::SameLine();
  backButton();
  ImGui::End();
}

void Menus::drawInGame() {
  centredWindow("##ingame", ImVec2(340, 0));
  title(scene_.mode() == Mode::Replay ? "Replay" : "Paused");
  const float w = 316;
  returnTo_ = Page::InGame;
  if (menuButton("Resume", w)) close();
  if (scene_.mode() == Mode::Game) {
    if (menuButton("Save game", w)) {
      const std::time_t t = std::time(nullptr);
      std::strftime(saveName_, sizeof saveName_, "Save %Y-%m-%d %H%M", std::localtime(&t));
      page_ = Page::SaveGame;
    }
  }
  if (menuButton("Load game", w)) {
    refreshFiles(scene_.savesDir(), ".lsav", false);
    page_ = Page::LoadGame;
  }
  if (scene_.mode() == Mode::Game) {
    if (menuButton("Save replay", w, scene_.replayValid())) {
      const std::time_t t = std::time(nullptr);
      char name[96];
      std::strftime(name, sizeof name, "replay_%Y%m%d_%H%M%S.lrep", std::localtime(&t));
      if (scene_.saveReplay(scene_.replaysDir() + name, error_)) scene_.showStatus(std::string("Replay saved as ") + name, 4);
    }
    if (!scene_.replayValid() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("The ground was edited during this game, so it can't be replayed.");
    if (menuButton("Restart", w)) {
      page_ = Page::None;
      scene_.setPaused(false);
      scene_.restart();
    }
  }
  if (menuButton("Settings", w)) page_ = Page::Settings;
  if (menuButton("Quit to main menu", w)) {
    page_ = Page::Main;
    scene_.setPaused(false);
    scene_.toMainMenu();
  }
  if (menuButton("Quit to desktop", w)) scene_.quit();
  if (!error_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
  ImGui::End();
}

void Menus::drawSaveGame() {
  centredWindow("##save", ImVec2(460, 0));
  title("Save game");
  ImGui::SetNextItemWidth(-1);
  const bool enter = ImGui::InputText("##name", saveName_, sizeof saveName_, ImGuiInputTextFlags_EnterReturnsTrue);
  std::string file;
  for (const char* c = saveName_; *c; ++c)
    file += (std::isalnum(static_cast<unsigned char>(*c)) || *c == '-' || *c == '_') ? *c : (*c == ' ' ? '_' : '\0');
  file.erase(std::remove(file.begin(), file.end(), '\0'), file.end());
  if (file.empty()) file = "save";
  ImGui::TextDisabled("%s%s.lsav", scene_.savesDir().c_str(), file.c_str());
  if (!error_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
  if (menuButton("Save", 140) || enter) {
    if (scene_.saveGame(scene_.savesDir() + file + ".lsav", error_)) close();
  }
  ImGui::SameLine();
  backButton();
  ImGui::End();
}

void Menus::drawReplayBar() {
  const ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x - 236) * 0.5f, io.DisplaySize.y - 12), ImGuiCond_Always, ImVec2(0.5f, 1));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin("##replaybar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings);
  const uint32_t now = scene_.sim().tickCount(), end = scene_.replayEndTick();
  ImGui::TextColored(kGold, "REPLAY");
  ImGui::SameLine();
  ImGui::Text("%u:%02u / %u:%02u", now / TICKS_PER_SECOND / 60, now / TICKS_PER_SECOND % 60, end / TICKS_PER_SECOND / 60,
              end / TICKS_PER_SECOND % 60);
  ImGui::SameLine();
  ImGui::ProgressBar(scene_.replayProgress(), ImVec2(260, 0), "");
  ImGui::SameLine();
  if (ImGui::Button(scene_.paused() ? "Play" : "Pause")) scene_.setPaused(!scene_.paused());
  for (float s : {1.0f, 2.0f, 4.0f, 8.0f}) {
    ImGui::SameLine();
    char b[24];
    std::snprintf(b, sizeof b, "x%g", s);
    if (ImGui::Button(b)) scene_.setSpeed(s);
  }
  ImGui::SameLine();
  ImGui::Checkbox("Reveal (V)", &scene_.revealMap());
  ImGui::SameLine();
  ImGui::TextDisabled("Tab: watch another side");
  ImGui::End();
}

void Menus::drawAbout() {
  centredWindow("##about", ImVec2(560, 0));
  title("About");
  ImGui::TextWrapped("Legion Classic - a 2.5D real-time strategy game set in an alternate 1940s, "
                     "on its own engine, Slave Engine 1.0.");
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::TextWrapped("Built with SDL3, OpenGL, Dear ImGui, nlohmann/json, stb and miniz.");
  ImGui::TextWrapped("Controls and the editor are described in docs/PLAYING.md. Press F1 in a game for the key list.");
  ImGui::Dummy(ImVec2(0, 6));
  returnTo_ = Page::Main;
  backButton();
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Settings: display and controls
// ---------------------------------------------------------------------------
void Menus::captureKey(const SDL_KeyboardEvent& e) {
  if (capture_ < 0) return;
  const SDL_Keycode k = e.key;
  // Modifier keys alone are not bindings; wait for the real key.
  if (k == SDLK_LCTRL || k == SDLK_RCTRL || k == SDLK_LSHIFT || k == SDLK_RSHIFT || k == SDLK_LALT || k == SDLK_RALT) return;
  Settings& st = scene_.settings();
  auto& slot = st.keys[static_cast<size_t>(capture_)][static_cast<size_t>(captureSlot_)];
  if (k == SDLK_ESCAPE) {
    // Esc cancels; Backspace / Delete clears the binding.
  } else if (k == SDLK_BACKSPACE || k == SDLK_DELETE) {
    slot = {};
  } else {
    slot = {k, modsOf(e.mod)};
  }
  capture_ = -1;
  scene_.saveSettings();
}

void Menus::drawSettings() {
  centredWindow("##settings", ImVec2(640, 600));
  title("Settings");
  Settings& st = scene_.settings();
  bool changed = false;
  if (ImGui::BeginTabBar("##settabs")) {
    if (ImGui::BeginTabItem("Display")) {
      static const int fpsValues[] = {30, 60, 75, 120, 144, 165, 240, 0};
      static const char* fpsNames[] = {"30", "60", "75", "120", "144", "165", "240", "Unlimited"};
      int fi = 4;
      for (int i = 0; i < 8; ++i)
        if (fpsValues[i] == st.fpsLimit) fi = i;
      ImGui::SetNextItemWidth(200);
      if (ImGui::Combo("Frame rate limit", &fi, fpsNames, 8)) {
        st.fpsLimit = fpsValues[fi];
        changed = true;
      }
      changed |= ImGui::Checkbox("Vertical sync", &st.vsync);
      ImGui::TextDisabled("With vsync on, the frame rate also stops at the monitor's refresh rate.");
      ImGui::Separator();
      changed |= ImGui::Checkbox("Lock the mouse inside the window while playing", &st.mouseLock);
      changed |= ImGui::Checkbox("Scroll when the mouse touches a screen edge", &st.edgeScroll);
      ImGui::SetNextItemWidth(200);
      changed |= ImGui::SliderFloat("Scroll speed", &st.scrollSpeed, 0.3f, 3.0f, "%.1fx");
      changed |= ImGui::Checkbox("Show the game info panel (I)", &st.showInfoPanel);
      changed |= ImGui::Checkbox("Unit cards over fireteams, platoons and companies", &st.showCards);
      changed |= ImGui::Checkbox("Unit cards over the other sides' units too", &st.showEnemyCards);
      changed |= ImGui::Checkbox("Chips for enemy forces out of sight", &st.showChips);
      changed |= ImGui::Checkbox("Atmosphere: period colour grading and a vignette", &st.atmosphere);
      changed |= ImGui::Checkbox("3D battlefield (off: the classic 2D view)", &st.view3D);
      if (st.view3D) changed |= ImGui::Checkbox("Shadows", &st.shadows);
      changed |= ImGui::Checkbox("Use the Blitzkrieg art pack when installed (restart to apply)", &st.artPack);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Sound")) {
      auto slider = [&](const char* label, float& v, const char* help) {
        int pct = static_cast<int>(v * 100.0f + 0.5f);
        ImGui::SetNextItemWidth(260);
        if (ImGui::SliderInt(label, &pct, 0, 100, "%d%%")) {
          v = static_cast<float>(pct) / 100.0f;
          changed = true;
        }
        if (help) ImGui::TextDisabled("%s", help);
      };
      slider("Master", st.volMaster, nullptr);
      ImGui::Separator();
      slider("Battle", st.volEffects, "Guns, explosions, engines, construction.");
      slider("Orders", st.volVoices, "The radio and whistles when you give an order.");
      slider("Music", st.volMusic, "Quiet pieces while all is calm; the battle music when fighting flares up near you.");
      slider("Ambience", st.volAmbience, "Wind, birds and weather, and the far-off front when the fighting is away from your view.");
      slider("Interface", st.volInterface, "Buttons, \"construction complete\" and messages.");
      ImGui::Separator();
      ImGui::TextDisabled("Sound fades with distance from the centre of your view: scroll toward a fight to hear it.");
      if (GameAudio* a = scene_.audio(); a && !a->nowPlaying().empty()) ImGui::TextDisabled("Now playing: %s", a->nowPlaying().c_str());
      if (GameAudio* a = scene_.audio(); !a || !a->deviceOpen()) ImGui::TextDisabled("No sound device was found when the game started.");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Controls")) {
      ImGui::TextDisabled("Click a key to change it, then press the new key (with Ctrl / Shift / Alt if you like).");
      ImGui::TextDisabled("Esc cancels, Backspace clears. The scenario editor's own keys (Ctrl+S, Ctrl+D, Ctrl+Z) win while it's open.");
      if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 380))) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Second key", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableHeadersRow();
        for (int a = 0; a < ACTION_COUNT; ++a) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(actionLabel(static_cast<InputAction>(a)));
          for (int slot = 0; slot < 2; ++slot) {
            ImGui::TableNextColumn();
            ImGui::PushID(a * 2 + slot);
            const bool waiting = capture_ == a && captureSlot_ == slot;
            const std::string label = waiting ? std::string("press a key...") : comboName(st.keys[static_cast<size_t>(a)][static_cast<size_t>(slot)]);
            if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
              capture_ = a;
              captureSlot_ = slot;
            }
            ImGui::PopID();
          }
        }
        ImGui::EndTable();
      }
      if (ImGui::Button("Reset to defaults")) {
        st.resetKeys();
        changed = true;
      }
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  if (changed) {
    scene_.applySettings();
    scene_.saveSettings();
  }
  if (menuButton("Back", 120)) {
    capture_ = -1;
    page_ = scene_.mode() == Mode::Menu ? Page::Main : Page::InGame;
  }
  ImGui::End();
}

// ---------------------------------------------------------------------------
// Campaigns and the tutorial
// ---------------------------------------------------------------------------
int Menus::campaignIndex(const std::string& id) const {
  for (size_t i = 0; i < scene_.campaigns().size(); ++i)
    if (scene_.campaigns()[i].id == id) return static_cast<int>(i);
  return -1;
}

void Menus::openCampaign(const std::string& id) {
  campaign_ = campaignIndex(id);
  if (campaign_ < 0) return;
  // Select the first mission not yet won.
  const CampaignInfo& c = scene_.campaigns()[static_cast<size_t>(campaign_)];
  mission_ = 0;
  for (size_t m = 0; m < c.missions.size(); ++m)
    if (scene_.progress().open(c, m)) mission_ = static_cast<int>(m);
  page_ = Page::Missions;
  error_.clear();
}

void Menus::openBriefing(const std::string& id, int mission) {
  campaign_ = campaignIndex(id);
  if (campaign_ < 0) return;
  const CampaignInfo& c = scene_.campaigns()[static_cast<size_t>(campaign_)];
  if (mission < 0 || mission >= static_cast<int>(c.missions.size())) return;
  mission_ = mission;
  briefingText_.clear();
  std::ifstream in(c.briefingPath(static_cast<size_t>(mission)));
  if (in) briefingText_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  page_ = Page::Briefing;
  error_.clear();
}

void Menus::drawCampaigns() {
  centredWindow("##campaigns", ImVec2(640, 520));
  title("Campaigns");
  ImGui::TextDisabled("Each campaign follows one army through a string of battles. Missions open one after another.");
  ImGui::Dummy(ImVec2(0, 4));
  ImGui::BeginChild("##clist", ImVec2(0, 380));
  for (size_t i = 0; i < scene_.campaigns().size(); ++i) {
    const CampaignInfo& c = scene_.campaigns()[i];
    if (c.tutorial) continue;
    ImGui::PushID(static_cast<int>(i));
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float wdt = ImGui::GetContentRegionAvail().x;
    const bool clicked = ImGui::InvisibleButton("##c", ImVec2(wdt, 96));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ui::setFaction(c.faction.c_str());
    ui::panel(dl, p0, ImVec2(p0.x + wdt, p0.y + 92), false);
    if (hovered) dl->AddRect(p0, ImVec2(p0.x + wdt, p0.y + 92), ui::kBrass, 2.0f, 0, 2.0f);
    ui::emblem(dl, ImVec2(p0.x + 46, p0.y + 46), 56);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.35f, ImVec2(p0.x + 90, p0.y + 10), ui::kBrass, c.title.c_str());
    const int won = scene_.progress().wonCount(c);
    const std::string prog = won == static_cast<int>(c.missions.size()) ? std::string("Complete")
                             : std::to_string(won) + " of " + std::to_string(c.missions.size()) + " missions";
    dl->AddText(ImVec2(p0.x + wdt - ImGui::CalcTextSize(prog.c_str()).x - 12, p0.y + 12), ui::kStencilDim, prog.c_str());
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p0.x + 90, p0.y + 40), ui::kStencil, c.description.c_str(),
                nullptr, wdt - 104);
    ui::setFaction("");
    if (clicked) openCampaign(c.id);
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PopID();
  }
  ImGui::EndChild();
  backButton();
  ImGui::End();
}

void Menus::drawMissions() {
  if (campaign_ < 0 || campaign_ >= static_cast<int>(scene_.campaigns().size())) {
    page_ = Page::Main;
    return;
  }
  const CampaignInfo& c = scene_.campaigns()[static_cast<size_t>(campaign_)];
  centredWindow("##missions", ImVec2(600, 500));
  title(c.title.c_str());
  ImGui::PushTextWrapPos(0);
  ImGui::TextDisabled("%s", c.description.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0, 4));
  if (ImGui::BeginListBox("##missionlist", ImVec2(-1, 300))) {
    for (size_t m = 0; m < c.missions.size(); ++m) {
      const MissionInfo& mi = c.missions[m];
      const bool open = scene_.progress().open(c, m);
      const bool won = scene_.progress().won(c.id, mi.id);
      if (!mi.act.empty() && (m == 0 || c.missions[m - 1].act != mi.act)) ImGui::SeparatorText(mi.act.c_str());
      const std::string label = std::string(won ? "[x]  " : open ? "[ ]  " : "[-]  ") + std::to_string(m + 1) + ".  " +
                                mi.title + (mi.place.empty() ? "" : "   -   " + mi.place) +
                                (mi.formation.empty() ? "" : "   (" + mi.formation + ")");
      ImGui::BeginDisabled(!open);
      if (ImGui::Selectable(label.c_str(), mission_ == static_cast<int>(m), ImGuiSelectableFlags_AllowDoubleClick)) {
        mission_ = static_cast<int>(m);
        if (ImGui::IsMouseDoubleClicked(0)) openBriefing(c.id, mission_);
      }
      ImGui::EndDisabled();
    }
    ImGui::EndListBox();
  }
  ImGui::TextDisabled("[x] won   [ ] open   [-] win the one before to open it");
  const bool can = mission_ >= 0 && scene_.progress().open(c, static_cast<size_t>(mission_));
  if (menuButton("Briefing", 160, can)) openBriefing(c.id, mission_);
  ImGui::SameLine();
  if (menuButton("Start again", 160, scene_.progress().wonCount(c) > 0)) ImGui::OpenPopup("##reset");
  if (ImGui::BeginPopup("##reset")) {
    ImGui::Text("Forget the missions won in %s?", c.title.c_str());
    if (ImGui::Button("Yes, start again")) {
      scene_.resetProgress(c.id);
      mission_ = 0;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("No")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (menuButton("Back", 120)) page_ = c.tutorial ? Page::Main : Page::Campaigns;
  ImGui::End();
}

void Menus::drawBriefing() {
  if (campaign_ < 0 || campaign_ >= static_cast<int>(scene_.campaigns().size())) {
    page_ = Page::Main;
    return;
  }
  const CampaignInfo& c = scene_.campaigns()[static_cast<size_t>(campaign_)];
  if (mission_ < 0 || mission_ >= static_cast<int>(c.missions.size())) {
    page_ = Page::Missions;
    return;
  }
  const MissionInfo& mi = c.missions[static_cast<size_t>(mission_)];
  ui::setFaction(c.faction.c_str());
  centredWindow("##briefing", ImVec2(680, 580));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 top = ImGui::GetCursorScreenPos();
  ui::emblem(dl, ImVec2(top.x + ImGui::GetContentRegionAvail().x - 30, top.y + 26), 44);
  title((std::to_string(mission_ + 1) + ".  " + mi.title).c_str());
  if (!mi.place.empty() || !mi.date.empty())
    ImGui::TextDisabled("%s%s%s", mi.place.c_str(), mi.place.empty() || mi.date.empty() ? "" : "   -   ", mi.date.c_str());
  if (!mi.act.empty() || !mi.formation.empty())
    ImGui::TextColored(kGold, "%s%s%s", mi.act.c_str(), mi.act.empty() || mi.formation.empty() ? "" : "   -   ", mi.formation.c_str());
  ImGui::Separator();
  ImGui::BeginChild("##btext", ImVec2(0, 420));
  ImGui::PushTextWrapPos(0);
  for (const BriefingLine& l : parseBriefing(briefingText_)) {
    if (l.kind == BriefingLine::Title && l.text == mi.title) continue;  // already the window's title
    switch (l.kind) {
      case BriefingLine::Title:
        ImGui::PushStyleColor(ImGuiCol_Text, kGold);
        ImGui::SetWindowFontScale(1.35f);
        ImGui::TextUnformatted(l.text.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        break;
      case BriefingLine::Heading:
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::TextColored(kGold, "%s", l.text.c_str());
        break;
      case BriefingLine::Bullet:
        ImGui::Bullet();
        ImGui::TextUnformatted(l.text.c_str());
        break;
      case BriefingLine::Quote:
        ImGui::Indent(18);
        ImGui::TextColored(ImVec4(0.8f, 0.78f, 0.68f, 1), "%s", l.text.c_str());
        ImGui::Unindent(18);
        break;
      case BriefingLine::Gap: ImGui::Dummy(ImVec2(0, 6)); break;
      default: ImGui::TextUnformatted(l.text.c_str()); break;
    }
  }
  if (briefingText_.empty()) ImGui::TextDisabled("(no briefing)");
  ImGui::PopTextWrapPos();
  ImGui::EndChild();
  if (!error_.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", error_.c_str());
  if (!c.tutorial) {
    int& d = scene_.settings().campaignDifficulty;
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("##difficulty", &d, "Difficulty: easy\0Difficulty: normal\0Difficulty: hard\0")) scene_.saveSettings();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Easy: fewer enemy reinforcements, a gentler computer.\nHard: more of them, and a sharper computer.");
    ImGui::SameLine();
  }
  if (menuButton("Begin", 160)) {
    if (scene_.startMission(c.id, mission_, error_)) {
      page_ = Page::None;
      scene_.setPaused(false);
    }
  }
  ImGui::SameLine();
  if (menuButton("Back", 120)) page_ = Page::Missions;
  ImGui::End();
  ui::setFaction("");
}

}  // namespace legion

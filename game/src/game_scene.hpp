#pragma once
// Legion Classic - the battlefield scene: the running world, the camera, unit
// control, and the editor overlay (F2) that edits the world while it runs.

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

#include <functional>

#include "battle_fx.hpp"
#include "settings.hpp"
#include "sidebar.hpp"
#include "slave/assets/placeholder_art.hpp"
#include "slave/map/map.hpp"
#include "slave/map/map_io.hpp"
#include "slave/platform/app.hpp"
#include "slave/render/camera.hpp"
#include "slave/render/sprite_renderer.hpp"
#include "slave/render/terrain_renderer.hpp"
#include "slave/sim/savefile.hpp"
#include "slave/sim/simulation.hpp"

namespace legion {

class Editor;
class Menus;
class Encyclopedia;

// A skirmish as set up in the menu.
struct SkirmishSetup {
  int mapSize = 128;
  uint64_t seed = 1940;
  int biome = 0;
  int credits = 5000;
  struct Side {
    bool open = false;
    std::string faction = "ger";
    bool human = false;
    int difficulty = 1;  // 0 easy, 1 normal, 2 hard
    int team = 0;
    uint32_t color = 0xFF2C3AC4;
  };
  std::array<Side, 4> sides;
};

enum class Mode { Menu, Game, Replay };

struct SceneOptions {
  int mapSize = 128;
  uint64_t seed = 1940;
  bool demo = false;         // scripted battle, for automated screenshots
  int stressUnits = 0;       // spawn this many extra units at start
  float zoom = 1.0f;
  bool showHelp = true;
  bool showDebug = false;
  bool startInEditor = false;
  std::string loadMap;       // open this .lmap at start
  bool editorDemo = false;   // scripted editor session, for automated screenshots
  bool liveDemo = false;     // scripted Live Scenario Editor session (units, battle tools)
  bool editorSelfTest = false;  // checks unit placement / undo / redo through the real editor
  std::string exportSprites;     // write every sprite as a PNG here, then quit
  bool noArtPack = false;
  int editorTab = -1, editorPage = -1;  // open the editor on this tab / script page at start
  std::string encyclopedia;      // open the encyclopedia on this unit id at start (screenshots)        // ignore content/art/blitzkrieg (compare with the built-in art)
  bool gallery = false;
  bool battleDemo = false;       // two forces fighting in the open (screenshots)
  bool orgDemo = false;          // fireteams, platoons, a garrison and fog chips, for screenshots         // every unit lined up, for screenshots of the art
  bool economyDemo = false;     // scripted base building, for automated screenshots
  bool noBases = false;         // start with armies only, no bases (classic skirmish sandbox)
  bool showMenu = false;        // start at the main menu (normal start-up)
  bool aiBattle = false;        // a skirmish between two computer players
  bool flowSelfTest = false;    // checks skirmish, save, load and replay through the real game
  int biome = 0;
};

class GameScene final : public slave::Scene {
 public:
  GameScene(slave::App& app, SceneOptions opt);
  ~GameScene() override;
  bool init(const std::string& contentDir, std::string& error);

  void onEvent(const SDL_Event& e, bool uiWantsMouse, bool uiWantsKeyboard) override;
  void onTick() override;
  void onUpdate(float dt, float alpha) override;
  void onRender() override;
  void onImGui() override;
  float timeScale() const override { return paused_ ? 0.0f : speed_; }
  bool wantsQuit() const override { return quit_; }

  // --- Used by the editor -------------------------------------------------
  const slave::SpriteCatalog& art() const { return art_; }
  slave::Map& map() { return map_; }
  slave::TerrainRenderer& terrain() { return terrain_; }
  slave::Simulation& sim() { return *sim_; }
  const slave::UnitTypeRegistry& types() const { return types_; }
  slave::App& app() { return app_; }
  slave::Camera& camera() { return cam_; }
  // Ground point under a screen pixel, in leptons (follows hills).
  slave::Vec2f groundAt(slave::Vec2f screenPx) const;
  slave::Vec2f mouse() const { return mouse_; }
  // Screen-world position of a ground point (leptons), lifted by its height.
  slave::Vec2f groundToScreenWorld(float lx, float ly) const;
  std::vector<slave::UnitPlacement> exportUnits();
  // New or loaded map. `keepCamera`: stay where we are (snapshot restore).
  void replaceWorld(slave::MapFile&& file, bool keepCamera = false);

  // Sides of the scenario.
  std::array<slave::PlayerSetup, slave::MAX_PLAYERS>& players() { return players_; }
  std::vector<slave::PlayerSetup> playerList() const { return {players_.begin(), players_.end()}; }
  void applyPlayers();  // after editing players(): colours, teams
  uint8_t player() const { return player_; }
  void setPlayer(uint8_t p);  // command and watch this side
  // Civilians (owner >= MAX_PLAYERS) are drawn and listed in grey.
  uint32_t teamColor(int p) const {
    return p >= 0 && p < slave::MAX_PLAYERS ? teamColors_[static_cast<size_t>(p)] : slave::rgba8(150, 150, 146);
  }
  std::string ownerName(int p) const {
    return p >= 0 && p < slave::MAX_PLAYERS ? players_[static_cast<size_t>(p)].name : std::string("Civilians");
  }
  const std::string& scenariosDir() const { return scenariosDir_; }
  Settings& settings() { return settings_; }
  void applySettings();  // after the settings page changes something
  void saveSettings();
  // Neutral houses on the generator's village and town plots.
  void addTownBuildings(slave::MapFile& f) const;

  // Units.
  std::vector<slave::Entity>& selection() { return selection_; }
  void pruneSelection();
  slave::Entity pickUnit(slave::Vec2f screenPx, bool ownOnly) const;
  slave::Vec2f unitScreenWorld(uint32_t entityIndex) const;
  const slave::UnitArt& unitArt(int type) const { return unitArt_[static_cast<size_t>(type)]; }
  bool artPack() const { return artPack_; }  // the Blitzkrieg art pack is in use
  void openEncyclopedia(int type = -1);
  const slave::WeaponRegistry& weapons() const { return weapons_; }
  slave::SpriteRenderer& renderer() { return renderer_; }
  BattleFx& fx() { return fx_; }

  // Snapshots: the whole battle (terrain, units, sides) kept in memory.
  void takeSnapshot();
  bool hasSnapshot() const { return snapshot_ != nullptr; }
  void restoreSnapshot();

  bool& editorShowsFog() { return editorFog_; }
  void centerOn(slave::Vec2i leptons);
  void orderMove(slave::Vec2i target, slave::CommandType type = slave::CommandType::Move, int formation = -1);
  bool fogEnabled() const;
  Sidebar& sidebar() { return *sidebar_; }
  void setPaused(bool p) { paused_ = p; }
  float speed() const { return speed_; }
  void setSpeed(float s) { speed_ = s; }
  bool paused() const { return paused_; }
  void showStatus(const std::string& text, float seconds = 3.0f);
  const std::string& mapsDir() const { return mapsDir_; }
  const std::string& currentMapPath() const { return currentMapPath_; }
  void setCurrentMapPath(const std::string& p) { currentMapPath_ = p; }

  // --- Game flow (frontend.cpp) ------------------------------------------------
  Mode mode() const { return mode_; }
  void startSkirmish(const SkirmishSetup& s);
  bool startScenario(const std::string& path, std::string& error);  // play a .lmap
  bool saveGame(const std::string& path, std::string& error);
  bool loadGame(const std::string& path, std::string& error);
  bool saveReplay(const std::string& path, std::string& error);
  bool playReplay(const std::string& path, std::string& error);
  void toMainMenu();
  void restart();
  void quit() { quit_ = true; }
  void editorOpen(bool on);
  void terrainEdited() { replayValid_ = false; }  // the editor changed the ground: replays can't follow
  bool replayValid() const { return replayValid_; }
  const std::string& savesDir() const { return savesDir_; }
  const std::string& replaysDir() const { return replaysDir_; }
  float replayProgress() const;
  uint32_t replayEndTick() const { return replayEnd_; }
  bool& revealMap() { return revealMap_; }
  // The scenario script being edited (the editor's Script tab).
  slave::ScenarioScript& scenario() { return scenario_; }
  void scenarioChanged();  // push edits to the running battle
  std::string scenarioJson() const;
  const SkirmishSetup& lastSkirmish() const { return lastSkirmish_; }

 private:
  struct Marker {
    slave::Vec2f pos;  // screen-world
    float age = 0;
    uint32_t color = 0;
  };
  struct DrawItem {
    int64_t depth;
    uint32_t order;
    int frame;
    float x, y;
    uint32_t team;
    uint32_t tint;
  };

  slave::Vec2i mouseWorldLeptons() const;
  void selectInBox(slave::Vec2f a, slave::Vec2f b, bool add);
  void selectSameTypeOnScreen(slave::Entity like);
  bool selectableOwner(uint8_t owner) const;  // the editor's select tool can pick any side
  void queueOrder(slave::Command c);          // splits mixed-owner selections per owner
  void orderAttack(slave::Entity target);
  void rightClick(slave::Vec2f screenPx, bool dragged, slave::Vec2f dragEnd);
  // Can the side we are watching see this tile right now?
  bool watching(int32_t lx, int32_t ly) const;
  void drawGroundLayer(const slave::RectF& view);
  void drawStructureEffects(float dt);
  void announce(const slave::SimEvent& ev);
  void drawUnits(const slave::RectF& view);
  void drawHealthBar(uint32_t entity, bool selected);
  void updateCursor();
  void spawnAt(const std::string& typeId, uint8_t player, slave::Vec2i at, int count);
  slave::Vec2i findLand(slave::Vec2i near) const;
  void setupStartingArmies();
  void buildBase(uint8_t player, slave::Vec2i centre);
  void runEconomyDemo();
  void runGallery();
  void runBattleDemo();
  void runOrgDemo();
  slave::Vec2i galleryAt_{0, 0};
  void drawFormationPicker();
  bool formationPicker_ = false;
  float hudBottom_ = 8;
  slave::Vec2f pickerScreen_;
  slave::Vec2i pickerTarget_;
  // Overlays over the battlefield (unit_cards.cpp).
  std::string groupName(const slave::CGroup& g) const;
  std::string groupSymbol(const slave::CGroup& g) const;
  void drawNatoSymbol(ImDrawList* dl, ImVec2 c, float scale, const std::string& nato, slave::Echelon e, uint32_t team,
                      float alpha, bool hostile);
  void drawWorldOverlays();
  void drawChips(ImDrawList* dl, const std::function<ImVec2(slave::Vec2f)>& toScreen);
  struct Chip {
    int64_t x = 0, y = 0;
    int count = 0;
    uint8_t owner = 0;
    int32_t bestCost = -1;
    std::string nato;
  };
  std::vector<Chip> chips_;
  std::vector<uint8_t> detect_;
  int chipTimer_ = 0;
  bool showCards_ = true, showChips_ = true;
  void bindSelection();
  void expandToGroups();
  slave::Entity garrisonUnder(slave::Vec2f screenPx) const;
  void unbindSelection();
  void updateCameraBounds();
  void drawHud();
  void drawHelp();
  void drawDebug();
  void runEditorDemo();
  void runLiveDemo();
  void runEditorSelfTest();
  void runFlowSelfTest();
  uint64_t flowHash_ = 0;
  uint32_t flowTick_ = 0;
  void startAttract();
  void beginRecording();
  void autosaveReplay();
  std::string metaJson(const std::string& title) const;
  bool loadFromSave(slave::SaveFile& f, bool replay, std::string& error);
  void drawObjectives();
  int selfTestFailures_ = 0;
  std::vector<slave::Vec2i> before_;
  slave::Vec2i moveDelta_;

  slave::App& app_;
  SceneOptions opt_;
  slave::Map map_;
  slave::UnitTypeRegistry types_;
  slave::WeaponRegistry weapons_;
  std::unique_ptr<slave::Simulation> sim_;
  slave::SpriteCatalog art_;
  slave::SpriteRenderer renderer_;
  slave::TerrainRenderer terrain_;
  slave::Camera cam_;
  std::unique_ptr<Editor> editor_;
  std::unique_ptr<Sidebar> sidebar_;
  std::unique_ptr<Menus> menus_;
  std::unique_ptr<Encyclopedia> encyclopedia_;
  friend class Sidebar;
  friend class Menus;
  Mode mode_ = Mode::Game;
  SkirmishSetup lastSkirmish_;
  std::string lastScenarioPath_;
  slave::SaveFile replayStart_;
  bool replayValid_ = false;
  std::vector<slave::Simulation::LoggedCommand> replayLog_;
  size_t replayNext_ = 0;
  uint32_t replayEnd_ = 0;
  slave::ScenarioScript scenario_;
  struct Message {
    std::string text;
    float age = 0;
  };
  std::vector<Message> messages_;
  float attractTime_ = 0;
  slave::Vec2f attractFocus_{-1, -1};
  std::string savesDir_, replaysDir_;
  bool rmbDown_ = false, rmbDragging_ = false;
  slave::Vec2f rmbStart_;
  std::string banner_;
  float bannerTime_ = 0;
  std::array<uint32_t, slave::MAX_PLAYERS> teamColors_{};
  std::string scenariosDir_;  // built-in missions, in the content folder
  Settings settings_;
  std::array<slave::PlayerSetup, slave::MAX_PLAYERS> players_;
  std::unique_ptr<slave::MapFile> snapshot_;
  bool editorFog_ = false;
  std::vector<slave::UnitArt> unitArt_;  // by unit type index
  bool artPack_ = false;
  bool pickUnitArt(std::string& error);
  std::vector<bool> trenchTypes_;         // drawn as ground decals that join up
  BattleFx fx_;

  uint8_t player_ = 0;  // which side the mouse commands
  std::vector<slave::Entity> selection_;
  std::array<std::vector<slave::Entity>, 10> groups_;
  slave::Vec2f mouse_;
  bool lmbDown_ = false, dragging_ = false, mmbDown_ = false;
  slave::Vec2f dragStart_;
  uint64_t lastClickMs_ = 0;
  slave::Entity lastClicked_;
  std::vector<Marker> markers_;
  std::vector<DrawItem> drawList_;
  float alpha_ = 0;
  float time_ = 0;
  int frame_ = 0;
  bool paused_ = false;
  float speed_ = 1.0f;
  bool quit_ = false;
  bool showHelp_ = true, showDebug_ = false;
  bool attackMoveMode_ = false;  // "A": next left click attack-moves
  bool revealMap_ = false;       // debug: no fog of war
  bool allHealthBars_ = false;   // debug: health bars on every damaged unit
  slave::Entity hovered_;
  SDL_Cursor* cursorArrow_ = nullptr;
  SDL_Cursor* cursorCross_ = nullptr;
  bool crossShown_ = false;
  uint64_t lastHash_ = 0;
  std::string status_;
  float statusTime_ = 0;
  std::string mapsDir_;
  std::string currentMapPath_;
};

}  // namespace legion

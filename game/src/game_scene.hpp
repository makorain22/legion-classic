#pragma once
// Legion Classic - the battlefield scene: the running world, the camera, unit
// control, and the editor overlay (F2) that edits the world while it runs.

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "battle_fx.hpp"
#include "slave/assets/placeholder_art.hpp"
#include "slave/map/map.hpp"
#include "slave/map/map_io.hpp"
#include "slave/platform/app.hpp"
#include "slave/render/camera.hpp"
#include "slave/render/sprite_renderer.hpp"
#include "slave/render/terrain_renderer.hpp"
#include "slave/sim/simulation.hpp"

namespace legion {

class Editor;

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
  uint32_t teamColor(int p) const { return teamColors_[static_cast<size_t>(p) % slave::MAX_PLAYERS]; }

  // Units.
  std::vector<slave::Entity>& selection() { return selection_; }
  void pruneSelection();
  slave::Entity pickUnit(slave::Vec2f screenPx, bool ownOnly) const;
  slave::Vec2f unitScreenWorld(uint32_t entityIndex) const;
  const slave::UnitArt& unitArt(int type) const { return unitArt_[static_cast<size_t>(type)]; }
  const slave::WeaponRegistry& weapons() const { return weapons_; }
  slave::SpriteRenderer& renderer() { return renderer_; }
  BattleFx& fx() { return fx_; }

  // Snapshots: the whole battle (terrain, units, sides) kept in memory.
  void takeSnapshot();
  bool hasSnapshot() const { return snapshot_ != nullptr; }
  void restoreSnapshot();

  bool& editorShowsFog() { return editorFog_; }
  void setPaused(bool p) { paused_ = p; }
  float speed() const { return speed_; }
  void setSpeed(float s) { speed_ = s; }
  bool paused() const { return paused_; }
  void showStatus(const std::string& text, float seconds = 3.0f);
  const std::string& mapsDir() const { return mapsDir_; }
  const std::string& currentMapPath() const { return currentMapPath_; }
  void setCurrentMapPath(const std::string& p) { currentMapPath_ = p; }

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
  void orderMove(slave::Vec2i target, slave::CommandType type = slave::CommandType::Move);
  void orderAttack(slave::Entity target);
  // Can the side we are watching see this tile right now?
  bool watching(int32_t lx, int32_t ly) const;
  bool fogEnabled() const;
  void drawUnits(const slave::RectF& view);
  void drawHealthBar(uint32_t entity, bool selected);
  void updateCursor();
  void spawnAt(const std::string& typeId, uint8_t player, slave::Vec2i at, int count);
  slave::Vec2i findLand(slave::Vec2i near) const;
  void centerOn(slave::Vec2i leptons);
  void setupStartingArmies();
  void updateCameraBounds();
  void drawHud();
  void drawHelp();
  void drawDebug();
  void runEditorDemo();
  void runLiveDemo();
  void runEditorSelfTest();
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
  std::array<uint32_t, slave::MAX_PLAYERS> teamColors_{};
  std::array<slave::PlayerSetup, slave::MAX_PLAYERS> players_;
  std::unique_ptr<slave::MapFile> snapshot_;
  bool editorFog_ = false;
  std::vector<slave::UnitArt> unitArt_;  // by unit type index
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

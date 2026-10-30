#pragma once
// Legion Classic - the battlefield scene (Milestone 1: a sandbox with units
// you can select and move, plus spawn tools that grow into the Live Editor).

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "slave/assets/placeholder_art.hpp"
#include "slave/map/map.hpp"
#include "slave/platform/app.hpp"
#include "slave/render/camera.hpp"
#include "slave/render/sprite_renderer.hpp"
#include "slave/render/terrain_renderer.hpp"
#include "slave/sim/simulation.hpp"

namespace legion {

struct SceneOptions {
  int mapSize = 128;
  uint64_t seed = 1940;
  bool demo = false;       // scripted orders, for automated screenshots
  int stressUnits = 0;     // spawn this many extra units at start
  float zoom = 1.0f;
  bool showHelp = true;
  bool showDebug = false;
};

class GameScene final : public slave::Scene {
 public:
  GameScene(slave::App& app, SceneOptions opt);
  bool init(const std::string& contentDir, std::string& error);

  void onEvent(const SDL_Event& e, bool uiWantsMouse, bool uiWantsKeyboard) override;
  void onTick() override;
  void onUpdate(float dt, float alpha) override;
  void onRender() override;
  void onImGui() override;
  float timeScale() const override { return paused_ ? 0.0f : speed_; }
  bool wantsQuit() const override { return quit_; }
  const slave::SpriteCatalog& art() const { return art_; }

 private:
  struct Marker {
    slave::Vec2f pos;  // screen-world
    float age = 0;
  };
  struct DrawItem {
    int64_t depth;
    uint32_t order;
    int frame;
    float x, y;
    uint32_t team;
  };

  // Helpers
  slave::Vec2f unitScreenWorld(uint32_t entityIndex) const;
  slave::Vec2i mouseWorldLeptons() const;
  slave::Entity pickUnit(slave::Vec2f screenPx, bool ownOnly) const;
  void selectInBox(slave::Vec2f a, slave::Vec2f b, bool add);
  void selectSameTypeOnScreen(slave::Entity like);
  void pruneSelection();
  void orderMove(slave::Vec2i target);
  void spawnAt(const std::string& typeId, uint8_t player, slave::Vec2i at, int count);
  void centerOn(slave::Vec2i leptons);
  void setupStartingArmies();
  void drawHud();
  void drawHelp();
  void drawDebug();

  slave::App& app_;
  SceneOptions opt_;
  slave::Map map_;
  slave::UnitTypeRegistry types_;
  std::unique_ptr<slave::Simulation> sim_;
  slave::SpriteCatalog art_;
  slave::SpriteRenderer renderer_;
  slave::TerrainRenderer terrain_;
  slave::Camera cam_;
  std::array<uint32_t, slave::MAX_PLAYERS> teamColors_{};
  std::array<std::string, slave::MAX_PLAYERS> playerNames_;
  std::vector<int> unitSheet_;  // unit type -> index of SpriteSheet in sheets_
  std::vector<slave::SpriteSheet> sheets_;

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
  bool paused_ = false;
  float speed_ = 1.0f;
  bool quit_ = false;
  bool showHelp_ = true, showDebug_ = false;
  uint64_t lastHash_ = 0;
  std::string status_;
  float statusTime_ = 0;
};

}  // namespace legion

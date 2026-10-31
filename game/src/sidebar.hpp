#pragma once
// Legion Classic - the command sidebar on the right of the screen, in the
// classic style: supplies and power at the top, the minimap, then the
// production tabs (buildings, defences, infantry, vehicles) with a build
// button for everything the side can make. Below that, orders for the
// selected units: stance, formation, deploy, sell.
//
// It also runs structure placement: a finished building follows the mouse
// as a ghost over its footprint (green where it may go, red where not);
// fortifications are dragged out as a line.

#include <SDL3/SDL.h>

#include <vector>

#include "slave/math/vec.hpp"
#include "slave/render/gl.hpp"
#include "slave/render/sprite_renderer.hpp"
#include "slave/sim/simulation.hpp"

namespace legion {

class GameScene;

class Sidebar {
 public:
  static constexpr float kWidth = 276.0f;

  explicit Sidebar(GameScene& scene);

  void draw();  // Dear ImGui window on the right edge
  void update(float dt);
  // Placement and sell-mode clicks on the battlefield. True if used.
  bool onEvent(const SDL_Event& e, bool uiWantsMouse);
  void renderWorld(slave::SpriteRenderer& r);  // placement ghost

  bool busy() const { return placeType_ >= 0 || sellMode_; }
  int placingType() const { return placeType_; }
  int hoveredType() const { return hoveredType_; }  // mouse belongs to the sidebar mode
  void cancel();
  slave::Formation formation() const { return formation_; }
  void startPlacing(int type);
  // Placement for scripted sessions.
  bool placeAt(slave::Vec2i groundLeptons);

 private:
  void drawMinimap(float width);
  void drawProduction(float width);
  void drawOrders(float width);
  void rebuildMinimap();
  // Tiles a fortification line covers from a to b (tile coordinates).
  std::vector<slave::Vec2i> lineTiles(slave::Vec2i a, slave::Vec2i b) const;
  slave::Vec2i snappedCentre(int type, slave::Vec2f ground) const;
  bool validAt(int type, slave::Vec2i centre) const;

  GameScene& scene_;
  int tab_ = 0;
  int hoveredType_ = -1;  // cameo under the mouse (for the encyclopedia)
  int placeType_ = -1;
  bool dragging_ = false;  // fortification line being dragged
  slave::Vec2i dragFrom_;
  bool sellMode_ = false;
  slave::Formation formation_ = slave::Formation::Box;

  slave::gl::Texture minimap_;
  std::vector<uint32_t> minimapPixels_;
  std::vector<uint32_t> terrainColors_;  // per tile, cached
  uint32_t terrainRevision_ = UINT32_MAX;
  int miniZoom_ = 1;  // 1, 2, 4, 8
  int minimapW_ = 0, minimapH_ = 0;
  float minimapTimer_ = 0;
  bool minimapDragging_ = false;
};

}  // namespace legion

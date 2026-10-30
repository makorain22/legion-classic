#pragma once
// Slave Editor - the map editor, opened with F2 over the running world.
//
// Milestone 2 provides the terrain tools: sculpting, ground painting, water,
// props, undo/redo and map files. The battle keeps running while you edit
// (pause with Space), which is the foundation of the Live Scenario Editor.

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "history.hpp"
#include "slave/map/map.hpp"
#include "slave/math/vec.hpp"
#include "slave/render/sprite_renderer.hpp"

namespace legion {

class GameScene;

enum class Tool { Raise, Lower, Smooth, Flatten, Roughen, Paint, Props, Erase, Count };

class Editor {
 public:
  static constexpr float kPanelWidth = 290.0f;

  explicit Editor(GameScene& scene);

  bool active() const { return active_; }
  void setActive(bool on);

  // Returns true when the editor used the event (so the game ignores it).
  bool onEvent(const SDL_Event& e, bool uiWantsMouse, bool uiWantsKeyboard);
  void update(float dt);
  void renderWorld(slave::SpriteRenderer& r);  // brush outline, in the world pass
  void imgui();
  void onWorldReplaced();  // new or loaded map: clear history

  // Tools, exposed for automated tests and scripted screenshots.
  void setTool(Tool t) { tool_ = t; }
  void setBrush(float radiusTiles, float strength) { radius_ = radiusTiles; strength_ = strength; }
  void setPaintLayer(slave::Layer l) { paintLayer_ = l; }
  void strokeAt(slave::Vec2f groundLeptons, float dt);  // one brush application
  void beginStroke();
  void endStroke();
  bool save(const std::string& path);
  bool load(const std::string& path);
  void undo();
  void redo();

 private:
  void applyHeightTool(slave::Vec2f c, float dt);
  void applyPaint(slave::Vec2f c, float dt);
  void applyProps(slave::Vec2f c, float dt);
  void applyErase(slave::Vec2f c);
  void afterTerrainEdit(int tx0, int ty0, int tx1, int ty1, bool heights, bool splat);
  void applyRegion(const History::Region& r);
  void drawToolPanel();
  void drawNewMapDialog();
  void drawOpenDialog();
  void drawSaveAsDialog();
  std::string sanitize(const std::string& name) const;

  GameScene& scene_;
  History history_;
  bool active_ = false;
  Tool tool_ = Tool::Raise;
  float radius_ = 4.0f;     // tiles
  float strength_ = 40.0f;  // 1..100
  bool softEdge_ = true;
  slave::Layer paintLayer_ = slave::Layer::Dirt;
  slave::PropKind propKind_ = slave::PropKind::Tree;
  float propDensity_ = 20.0f;  // props per second while dragging
  float propAccum_ = 0;
  bool stroking_ = false;
  float flattenTarget_ = 0;
  std::vector<float> fineHeights_;  // float copy during a stroke, so small changes add up
  uint32_t strokeCounter_ = 0;

  bool showNew_ = false, showOpen_ = false, showSaveAs_ = false;
  int newSizeIndex_ = 1;
  int newTerrain_ = 1;
  int newSeed_ = 1940;
  char nameBuf_[96] = "";
  std::vector<std::string> mapFiles_;
  std::string lastError_;
};

}  // namespace legion

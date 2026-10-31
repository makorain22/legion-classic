#pragma once
// Slave Editor - the Live Scenario Editor, opened with F2 over the running world.
//
// Everything is edited while the battle runs (pause with Space):
//   Terrain   sculpting, ground painting, water, props
//   Units     place units for any side (single, brush, block), select any
//             unit, move / rotate / convert / heal / delete / duplicate it,
//             and inspect what it is doing
//   Sides     up to 8 players: name, faction, colour, team, start view;
//             take command of any side
//   Battle    scenario info, pause and speed, snapshots, artillery, smite,
//             heal, fog preview
// Terrain and unit edits share one undo history (Ctrl+Z / Ctrl+Y). Unit edits
// go through simulation commands, so they stay deterministic.

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "history.hpp"
#include "slave/map/map.hpp"
#include "slave/math/vec.hpp"
#include "slave/render/sprite_renderer.hpp"
#include "slave/sim/simulation.hpp"

namespace legion {

class GameScene;

enum class Tool {
  Raise, Lower, Smooth, Flatten, Roughen, Paint, Props, Erase, Oil,  // terrain
  PlaceUnits, SelectUnits,                                     // units
  Artillery, Smite, Heal,                                      // battle ("god") tools
  Area,                                                        // scenario areas (Script tab)
  Count
};

enum class PlaceMode { Single, Brush, Block };

class Editor {
 public:
  static constexpr float kPanelWidth = 310.0f;

  explicit Editor(GameScene& scene);
  ~Editor();

  bool active() const { return active_; }
  void setActive(bool on);

  // Returns true when the editor used the event (so the game ignores it).
  bool onEvent(const SDL_Event& e, bool uiWantsMouse, bool uiWantsKeyboard);
  void update(float dt);
  void renderWorld(slave::SpriteRenderer& r);  // brush outline, ghosts, in the world pass
  void imgui();
  void onWorldReplaced();  // new or loaded map: clear history
  void onSimTick(const std::vector<slave::SpawnRecord>& spawned);
  // While the select tool is out, box and click selection pick any side's units.
  bool selectsAnyOwner() const { return active_ && tool_ == Tool::SelectUnits; }

  // Tools, exposed for automated tests and scripted screenshots.
  void setTool(Tool t);
  Tool tool() const { return tool_; }
  void setBrush(float radiusTiles, float strength) { radius_ = radiusTiles; strength_ = strength; }
  void setPaintLayer(slave::Layer l) { paintLayer_ = l; }
  void setPlaceUnit(const std::string& typeId, uint8_t owner, PlaceMode mode, int blockCount = 25);
  void setPlaceFacing(int facing) { placeFacing_ = facing & 15; }
  void strokeAt(slave::Vec2f groundLeptons, float dt);  // one brush application
  void beginStroke();
  void endStroke();
  bool save(const std::string& path);
  bool load(const std::string& path);
  void undo();
  void redo();
  // Unit operations on the scene's selection (all undoable).
  void deleteSelection();
  void duplicateSelection();
  void rotateSelection(int steps);
  void setSelectionOwner(uint8_t owner);
  void setSelectionHealth(float fraction);
  void moveSelection(slave::Vec2i delta);
  void artilleryAt(slave::Vec2i at);

 private:
  struct PresenceAction;
  struct EditAction;
  friend struct PresenceAction;
  friend struct EditAction;

  // terrain (editor.cpp)
  void applyHeightTool(slave::Vec2f c, float dt);
  void applyPaint(slave::Vec2f c, float dt);
  void applyProps(slave::Vec2f c, float dt);
  void applyErase(slave::Vec2f c);
  void applyOil(slave::Vec2f c, float dt);
  void afterTerrainEdit(int tx0, int ty0, int tx1, int ty1, bool heights, bool splat);
  void applyRegion(const History::Region& r);
  void drawToolPanel();
  void drawTerrainTab();
  void drawNewMapDialog();
  void drawOpenDialog();
  void drawSaveAsDialog();
  std::string sanitize(const std::string& name) const;
  bool isTerrainTool() const { return tool_ <= Tool::Oil; }

  // units (unit_tools.cpp)
  void drawUnitsTab();
  void drawSidesTab();
  void drawBattleTab();
  void drawScriptTab();  // script_tab.cpp
  void renderAreas(slave::SpriteRenderer& r);
  void drawAreaLabels();
  bool editCondition(slave::Condition& c);
  bool editAction(slave::Action& a);
  bool playerCombo(const char* label, uint8_t& p, bool allowAny);
  bool areaCombo(const char* label, int32_t& area);
  bool unitCombo(const char* label, int32_t& type, bool allowAny);
  bool triggerCombo(const char* label, int32_t& trigger);
  void drawInspector();
  void drawUnitPalette();
  void applyUnitBrush(slave::Vec2f c, float dt);
  void applyGodTool(slave::Vec2f c, float dt);
  void renderUnitGhosts(slave::SpriteRenderer& r, slave::Vec2f mouseGround);
  void beginUnitStroke();
  void endUnitStroke();
  bool placeable(int type, slave::Vec2i at) const;
  // Queues a SpawnList for `action` entries [first, end) and remembers the tag.
  void spawnFor(const std::shared_ptr<PresenceAction>& action, size_t first);
  void removeEntities(const std::vector<slave::Entity>& units);
  void queueEdits(std::vector<slave::UnitEdit> edits);
  slave::Entity resolve(slave::Entity e) const;  // follows respawns made by undo/redo
  std::vector<slave::SpawnSpec> specsOf(const std::vector<slave::Entity>& units) const;
  void pushEdit(const std::string& label, std::vector<slave::UnitEdit> before, std::vector<slave::UnitEdit> after);
  bool facingPicker(const char* id, int& facing);
  void unitIcon(int type, uint8_t owner, float size);

  GameScene& scene_;
  History history_;
  bool active_ = false;
  Tool tool_ = Tool::Raise;
  int tab_ = 0;
  int tabRequest_ = -1;  // select this tab next frame (setTool from code)
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

  // Unit placement.
  int placeType_ = 0;
  uint8_t placeOwner_ = 0;
  PlaceMode placeMode_ = PlaceMode::Single;
  int placeFacing_ = 2;     // 0..15
  int blockCount_ = 25;
  float unitDensity_ = 12;  // units per second while brushing
  float unitAccum_ = 0;
  bool paletteAllFactions_ = false;
  int paletteCategory_ = 0;  // 0 = everything, else 1 + UnitCategory
  std::shared_ptr<PresenceAction> placing_;  // the brush stroke in progress

  // Select-tool dragging.
  bool movePending_ = false, moving_ = false;
  slave::Vec2f moveStartMouse_;
  slave::Vec2f moveStartGround_;
  slave::Entity moveClicked_;

  // Battle tools.
  int strikeRounds_ = 12;
  // Script tab.
  int selectedTrigger_ = -1;
  int selectedArea_ = -1;
  bool drawingArea_ = false;
  slave::Vec2i areaStart_, areaEnd_;
  float godAccum_ = 0;
  uint32_t nextTag_ = 1;
  std::unordered_map<uint32_t, std::weak_ptr<PresenceAction>> pendingTags_;
  std::unordered_map<uint64_t, slave::Entity> remap_;  // old entity -> respawned entity
  uint32_t selectTag_ = 0;  // units spawned under this tag get selected (duplicate)
  struct HealStroke {
    std::vector<slave::UnitEdit> before, after;
    std::vector<uint64_t> seen;
  } heal_;

  bool showNew_ = false, showOpen_ = false, showSaveAs_ = false;
  int newSizeIndex_ = 1;
  int newTerrain_ = 1;
  // Civilian buildings always belong to nobody, whichever side is chosen.
  uint8_t placingOwner() const;
 public:
  bool placingStructure() const;  // the placement grid is shown
 private:
  bool oilClickDone_ = false;  // supply point tool: one change per click
  int newSeed_ = 1940;
  int newBiome_ = 0;
  bool newTowns_ = true;
  char nameBuf_[96] = "";
  std::vector<std::string> mapFiles_;
  std::string lastError_;
};

}  // namespace legion

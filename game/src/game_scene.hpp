#pragma once
// Legion Classic - the battlefield scene: the running world, the camera, unit
// control, and the editor overlay (F2) that edits the world while it runs.

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include <functional>

#include "battle_fx.hpp"
#include "campaign.hpp"
#include "settings.hpp"
#include "sidebar.hpp"
#include "slave/assets/placeholder_art.hpp"
#include "slave/core/content.hpp"
#include "slave/map/map.hpp"
#include "slave/map/map_io.hpp"
#include "slave/platform/app.hpp"
#include "slave/render/camera.hpp"
#include "slave/render/mesh_renderer.hpp"
#include "slave/render/shadow.hpp"
#include "slave/render/sprite_renderer.hpp"
#include "slave/render/terrain_renderer.hpp"
#include "slave/sim/savefile.hpp"
#include "slave/sim/simulation.hpp"

namespace legion {

class Editor;
class Menus;
class GameAudio;
class Encyclopedia;
class RulesEditor;

// A skirmish as set up in the menu.
struct SkirmishSetup {
  int mapSize = 128;
  uint64_t seed = 1940;
  int biome = 0;
  int layout = -1;      // MapLayout, or -1: the seed picks
  int credits = 5000;
  int startLayout = 0;  // 0 teams on opposite sides (west / east), 1 fixed (W, E, N, S), 2 shuffled
  std::string mapFile;  // a ready-made skirmish map (full path), "" = generate one from size, biome and seed
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
  int townDemo = 0;              // 1: every civilian building in rows; 2: the camera on a bridge; 3: on the steepest rock; 4: on a logistics office
  bool formationDemo = false;    // groups marching in each formation, for screenshots
  bool worksDemo = false;        // trenches and sandbag walls manned by every kind of infantry, for screenshots
  bool orgDemo = false;          // fireteams, platoons, a garrison and fog chips, for screenshots         // every unit lined up, for screenshots of the art
  bool economyDemo = false;     // scripted base building, for automated screenshots
  bool noBases = false;         // start with armies only, no bases (classic skirmish sandbox)
  bool showMenu = false;        // start at the main menu (normal start-up)
  bool aiBattle = false;        // a skirmish between two computer players
  int side = -1;                // watch from this side (player number) at the start
  float speed = 1.0f;           // starting game speed (testing)
  int layout = -1;              // map layout for generated maps (-1: from the seed)
  int sides = 2;                // --ai-battle: 2 (1v1), 3 (every side for itself) or 4 (2v2)
  bool noSound = false;         // --no-sound
  bool forceSound = false;      // --sound: sound even with --offscreen (testing)
  std::string factions;         // --ai-battle: "ger,gbr,..." per side (default Germany v Russia)
  int monkey = 0;
  std::string showcase;         // record a video of an AI battle to this .mp4, camera following the fighting
  int showcaseWarm = 15 * 60 * 5;  // ticks to fast-forward first
  int showcaseSeconds = 60;               // > 0: random clicks and keys from a human seat (crash hunting)
  bool flowSelfTest = false;    // checks skirmish, save, load and replay through the real game
  bool modsSelfTest = false;    // checks the rules editor and mods through the real game
  std::string tool;             // open a creation tool at start: rules[:id] | sprites[:group] | mods
  std::string campaign;         // --campaign id[:mission]: open a campaign's missions, or a mission's briefing (1-based)
  std::string mission;          // --mission id:N: play mission N (1-based) of a campaign straight away
  int biome = 0;
  bool classic = false;         // --classic: the old 2D sprite view instead of 3D
  bool noShadows = false;       // --no-shadows (testing, comparisons)
  bool intel = false;           // --intel: an AI battle seen with the viewing side's intel (no reveal)
  int bench = 0;                // --bench N: play an --ai-battle for N game minutes without drawing, print a report, quit
  float yaw = -1000, pitch = -1000;  // --yaw / --pitch: starting camera angles in degrees (screenshots)
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
  // A point of the world (leptons; height in height units) on the camera's
  // screen-world plane. In 3D that plane is the screen, so this is the pixel.
  slave::Vec2f project(float lx, float ly, float h) const;
  // World point (leptons, height units) -> 3D drawing space (tiles).
  static slave::Vec3f worldPoint(float lx, float ly, float h) {
    return {lx / 256.0f, ly / 256.0f, h / static_cast<float>(slave::HEIGHT_UNITS_PER_TILE)};
  }
  bool is3D() const { return cam_.is3D(); }
  // The ground point the camera looks at (leptons), and moving the camera to look at one.
  slave::Vec2f cameraFocus() const;
  void focusCamera(slave::Vec2f leptons);
  slave::MeshRenderer& meshes() { return meshes_; }
  // The 3D model an atlas frame was baked from (-1: a painted sprite).
  int meshOf(int frame);
  // Turns the 3D view from the keyboard (per frame).
  void cameraKeys(float dt);
  // A see-through model of something about to be placed (3D view), at a
  // ground point; turned as the frame was baked.
  void ghost(int frame, float lx, float ly, uint32_t team, uint32_t tint);
  // Middle mouse dragged: turn and tilt the 3D view (Shift: pan), or pan the classic one.
  void mmbDrag(float dx, float dy);
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
  Settings& settings() { return settings_; }
  GameAudio* audio() { return audio_.get(); }
  void applySettings();  // after the settings page changes something
  void saveSettings();
  // Neutral houses on the generator's village and town plots.
  void addTownBuildings(slave::MapFile& f) const;

  // Units.
  std::vector<slave::Entity>& selection() { return selection_; }
  void pruneSelection();
  // Formations (9.5). The shape the selected units move in (the commonest
  // among them); -1 when nothing that moves is selected.
  int selectionShape();
  // Gives the selection a new shape at once: on the march it re-forms on the
  // way, standing it forms up where it stands, keeping its front.
  void setFormation(slave::Formation f);
  slave::Entity pickUnit(slave::Vec2f screenPx, bool ownOnly) const;
  slave::Vec2f unitScreenWorld(uint32_t entityIndex) const;
  // Height the ground or a bridge deck stands at (height units) under (lx, ly).
  float surfaceHeight(float lx, float ly) const;
  const slave::UnitArt& unitArt(int type) const { return unitArt_[static_cast<size_t>(type)]; }
  bool artPack() const { return artPack_; }  // the Blitzkrieg art pack is in use
  void openEncyclopedia(int type = -1);
  void evacuateSelection();
  bool selectionCanEvacuate() const;
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
  // Intel is on (9.3): the enemy shows only where our side sees it.
  bool fogEnabled() const;
  // An enemy building our side has seen (ours, allies' and villages: always).
  bool structureKnown(uint32_t e) const;
  Sidebar& sidebar() { return *sidebar_; }
  void setPaused(bool p) { paused_ = p; }
  float speed() const { return speed_; }
  void setSpeed(float s) { speed_ = s; }
  bool paused() const { return paused_; }
  void showStatus(const std::string& text, float seconds = 3.0f);
  const std::string& mapsDir() const { return mapsDir_; }
  const std::string& currentMapPath() const { return currentMapPath_; }
  void setCurrentMapPath(const std::string& p) { currentMapPath_ = p; }

  // --- Content, mods and creation tools (game_content.cpp) ----------------------
  const slave::ContentLayers& content() const { return content_; }
  const std::vector<slave::ModInfo>& installedMods() const { return mods_; }
  void rescanMods();
  std::string userModsDir() const { return app_.userDir() + "mods/"; }
  // Problems found with the enabled mods at the last load (shown in the Mods page).
  const std::string& modWarnings() const { return modWarnings_; }
  // Switches to these mods: reloads rules and art, then back to the main menu.
  bool setMods(const std::vector<std::string>& enabled, std::string& error);
  // The merged rules in effect (slave::kRuleFiles order) and their fingerprint.
  const nlohmann::json& rulesDoc(int file) const { return rules_[file]; }
  const std::string& rulesHash() const { return rulesHash_; }
  // Puts new rules into the running battle (rules editor). `notes` lists
  // what could only be kept for the next battle.
  bool applyRules(const nlohmann::json (&docs)[slave::kRuleFileCount], std::string& notes, std::string& error);
  // Rebuilds every sprite (after the sprite studio saved overrides).
  bool reloadArt(std::string& error);
  std::vector<std::string> scenarioDirs() const { return content_.all("scenarios/"); }
  // The model art a type would be drawn with (false: no such model).
  bool artFor(const slave::UnitTypeDef& t, slave::UnitArt& out) const;
  RulesEditor& rulesEditor() { return *rulesEditor_; }
  bool creationToolOpen() const;

  // --- Game flow (frontend.cpp) ------------------------------------------------
  Mode mode() const { return mode_; }
  void startSkirmish(const SkirmishSetup& s);
  bool startScenario(const std::string& path, std::string& error);  // play a .lmap
  // Campaigns and the tutorial (campaign.hpp).
  const std::vector<CampaignInfo>& campaigns() const { return campaigns_; }
  struct SkirmishMapInfo {
    std::string path, name, description, biome;
    int size = 128;
  };
  // The ready-made skirmish maps (content/maps/skirmish/maps.json, then the mods').
  std::vector<SkirmishMapInfo> skirmishMaps() const;
  const CampaignProgress& progress() const { return progress_; }
  bool startMission(const std::string& campaign, int mission, std::string& error);
  const CampaignInfo* currentCampaign() const;
  int currentMission() const { return missionIndex_; }
  void resetProgress(const std::string& campaign);
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
    slave::Vec2f pos;  // ground point, leptons
    float age = 0;
    uint32_t color = 0;
    bool slot = false;  // a small mark where one unit of a formation will stand
  };
  // A figure that floats up from where a convoy paid in.
  struct Floater {
    slave::Vec2f pos;  // ground point, leptons
    float age = 0;
    std::string text;
  };
  std::vector<Floater> floaters_;
  // A front being dragged with the right button: where it is, which way it
  // faces and how wide. False while there is nothing to lay out.
  bool draggedFront(slave::Vec2f startPx, slave::Vec2f endPx, slave::Vec2i& mid, slave::Angle& facing, int32_t& width);
  void markSlots(const std::vector<slave::Vec2i>& slots, uint32_t color);
  struct DrawItem {
    int64_t depth;
    uint32_t order;
    int frame;
    float x, y;
    uint32_t team;
    uint32_t tint;
    // Where it stands in the world (3D view): leptons, height units, and the
    // turn in radians when it is not the frame's own (units turn smoothly).
    float lx = 0, ly = 0, h = 0;
    float yaw = 0;
    bool turned = false;
    float pitch = 0, roll = 0;
    float blend = 0;  // 3D: tiles of ground creeping up its foot
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
  void refreshBridges();  // the level each bridge is laid at, when bridges come or go
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
  void runWorksDemo();
  void runFormationDemo();
  void runTownDemo();
  void runBench();
  slave::Vec2i galleryAt_{0, 0};
  std::array<std::vector<slave::Entity>, 5> demoGroups_;
  slave::Vec2i bridgeFrom_{0, 0}, bridgeTo_{0, 0};  // --bridge-demo: the span's ends (tiles)
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
    bool identified = false;  // within earshot of our side: the kind of force is known
    bool structures = false;  // buildings not yet seen
  };
  // Intel (9.3): the whole map is in view; the enemy shows only where our
  // side sees it, as chips elsewhere. Enemy buildings, once seen, stay on
  // the map (dimmed while out of sight).
  std::unordered_map<uint32_t, uint32_t> spotted_;  // structure entity index -> handle generation
  int64_t lastScoutNote_ = -100000;                 // frame of the last "scout falling back" note
  uint8_t spottedFor_ = 255;                        // the side the memory belongs to
  void updateIntel();
  // 255 where our side sees now, 0 elsewhere (or 255 everywhere with intel off).
  uint8_t intelAt(int tx, int ty) const;
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
  void runModsSelfTest();
  uint64_t flowHash_ = 0;
  uint32_t flowTick_ = 0;
  void startAttract();
  void beginRecording();
  void autosaveReplay();
  std::string metaJson(const std::string& title) const;
  bool loadFromSave(slave::SaveFile& f, bool replay, std::string& error);
  bool rulesMatch(const nlohmann::json& meta, bool replay, std::string& error) const;
  void drawObjectives();
  int selfTestFailures_ = 0;
  std::vector<slave::Vec2i> before_;
  slave::Vec2i moveDelta_;

  bool loadRules(std::string& error);
  slave::ArtOptions artOptions() const;
  slave::ContentLayers content_;
  std::vector<slave::ModInfo> mods_;
  std::string contentDir_;
  std::string modWarnings_;
  nlohmann::json rules_[slave::kRuleFileCount];
  std::string rulesHash_;
  bool rulesEditedLive_ = false;  // the battle runs on rules changed in it (saves note it)
  std::unique_ptr<RulesEditor> rulesEditor_;

  slave::App& app_;
  SceneOptions opt_;
  slave::Map map_;
  slave::UnitTypeRegistry types_;
  slave::WeaponRegistry weapons_;
  std::unique_ptr<slave::Simulation> sim_;
  slave::SpriteCatalog art_;
  slave::SpriteRenderer renderer_;
  slave::MeshRenderer meshes_;
  slave::ShadowMap shadow_;
  std::vector<int> frameMesh_;  // atlas frame -> mesh (-2: not looked at yet)
  std::vector<DrawItem> billboards_;  // 3D: things with no model, drawn as sprites facing the camera
  std::vector<int> rockMesh_;            // per terrain chunk: its rock faces (owned mesh, reused)
  std::vector<slave::Vec3f> rockOrigin_;
  void updateRockFaces();
  void render3D();
  void drawOverlays(const slave::RectF& view, bool markers);
  void drawGround3D();
  void drawSelection3D();
  void queueItems3D();
  slave::Vec2f groundAt3D(slave::Vec2f screenPx) const;
  void lean(float lx, float ly, float yaw, float reach, float& pitch, float& roll) const;
  slave::TerrainRenderer terrain_;
  slave::Camera cam_;
  std::unique_ptr<Editor> editor_;
  std::unique_ptr<Sidebar> sidebar_;
  std::unique_ptr<Menus> menus_;
  std::unique_ptr<GameAudio> audio_;
  float audioBedsTimer_ = 0;
  void updateAudio(float dt);
  void orderSound(bool attack);
  std::unique_ptr<Encyclopedia> encyclopedia_;
  friend class Sidebar;
  friend class Menus;
  Mode mode_ = Mode::Game;
  SkirmishSetup lastSkirmish_;
  std::string lastScenarioPath_;
  std::vector<CampaignInfo> campaigns_;
  CampaignProgress progress_;
  std::string campaignId_;          // the campaign mission being played ("" = none)
  int missionIndex_ = -1;
  int missionResult_ = 0;           // 1 won, 2 lost (the debriefing shows once the banner is gone)
  std::map<std::string, int32_t> nextFlags_;  // flags the next battle starts with
  void loadCampaigns();
  void missionOver(bool won);
  void drawDebrief();
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
  Settings settings_;
  std::array<slave::PlayerSetup, slave::MAX_PLAYERS> players_;
  std::unique_ptr<slave::MapFile> snapshot_;
  bool editorFog_ = false;
  std::vector<slave::UnitArt> unitArt_;  // by unit type index
  // Infantry manning field works: in a trench (bit 1, dugArt_) or behind
  // sandbags (bit 2, bagArt_), by unit type index.
  std::vector<uint8_t> postArt_;
  std::vector<slave::UnitArt> dugArt_, bagArt_;
  bool artPack_ = false;
  bool pickUnitArt(std::string& error);
  struct CardHit {
    float x0, y0, x1, y1;
    slave::Entity group;
    std::vector<slave::Entity> stack;  // a stack card: every group in it (9.3)
  };
  std::vector<CardHit> cardHits_;  // unit cards drawn last frame, for clicks
  slave::Entity openStack_;        // the stack card opened by the mouse, and the area it covers open
  float openArea_[4] = {0, 0, 0, 0};
  slave::Entity lastCard_;
  slave::Entity cardAt(slave::Vec2f screenPx) const;
  // The groups behind the card under the mouse (several for a stack card).
  std::vector<slave::Entity> cardGroupsAt(slave::Vec2f screenPx) const;
  std::vector<bool> bridgeTypes_;         // bridge pieces: drawn at the water line, men cross on the deck
  std::vector<float> bridgeLevel_;        // per tile: the height a bridge there is laid at (0: none)
  uint64_t bridgeSig_ = ~0ull;
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
  uint32_t monkeyRng_ = 12345;
  // Showcase recording.
  void showcaseStep(float dt);
  struct Hot { slave::Vec2i at; float t; };
  std::vector<Hot> hot_;
  float showTime_ = 0, showFocusAt_ = -100;
  int showPhase_ = 0, showFrames_ = 0;
  slave::Vec2f showFocus_{-1, -1};
  void monkeyStep();
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

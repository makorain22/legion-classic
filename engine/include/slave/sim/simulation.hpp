#pragma once
// Slave Engine - the deterministic simulation.
//
// Everything that changes the game world (player orders, the editor, the AI)
// arrives as a Command and is applied at the start of the next tick. The
// simulation runs at a fixed 15 ticks per second and uses integer maths only.
//
// Each tick: commands, path requests, orders, formations, movement, collision, vision
// (every 4 ticks), targeting and firing, projectiles, then removal of the dead.
// What happened (shots, impacts, deaths) is reported as events, which the
// presentation turns into flashes, explosions and wrecks.

#include <array>
#include <map>
#include <cstdint>
#include <utility>
#include <vector>

#include "slave/core/bytes.hpp"
#include "slave/core/rng.hpp"
#include "slave/ecs/registry.hpp"
#include "slave/map/map.hpp"
#include "slave/math/vec.hpp"
#include "slave/sim/components.hpp"
#include "slave/sim/ai.hpp"
#include "slave/sim/pathfinder.hpp"
#include "slave/sim/script.hpp"
#include "slave/sim/spatial_hash.hpp"
#include "slave/sim/unit_types.hpp"
#include "slave/sim/vision.hpp"
#include "slave/sim/weapons.hpp"

namespace slave {

constexpr int TICKS_PER_SECOND = 15;
constexpr int MAX_PLAYERS = 8;
constexpr uint8_t NEUTRAL_OWNER = 255;  // owner of editor strikes: hurts everyone

enum class CommandType : uint8_t {
  Move,        // units -> target
  AttackMove,  // units -> target, fighting anything met on the way
  Attack,      // units attack targetUnit
  Stop,        // units stop
  Spawn,       // `count` units of unitType for player at target, facing `facing`
  Remove,      // delete units (no death effects)
  SpawnList,   // exact placements in `spawns`; spawned entities are logged under `tag`
  Edit,        // change position / facing / owner / health of units (`edits`)
  Kill,        // destroy units as if killed in battle (wrecks, bodies)
  Strike,      // artillery: `count` rounds of `weapon` within `radius` of target
  SetTeam,     // player `player` joins team `count` (same team = allies)
  SetStance,   // units take stance `stance`
  // --- Economy ----------------------------------------------------------------
  Produce,        // player queues `amount` of unitType (negative: cancel that many from the end)
  PlaceBuilding,  // player places the finished structure unitType at `target` (its footprint centre)
  PlaceWalls,     // player buys fortifications unitType at each of `spawns[].pos` (bought on the spot)
  Sell,           // structures in `units` are sold for half their cost
  Deploy,         // construction trucks in `units` unfold into their structure
  Harvest,        // supply trucks in `units` go to work (for the logistics office `targetUnit`, if given and it has room)
  SetRally,       // producing structures in `units` send new units to `target`
  SetCredits,     // player's supplies become `amount`
  SetController,  // player is played by the computer at difficulty `amount` (0..2), or by a human (-1)
  Repair,         // engineers in `units` mend `targetEntity` (a friendly structure or vehicle)
  // --- Organisation and shelter -------------------------------------------------
  BindGroup,      // `units` become one fighting unit (fireteam ... company / platoon ... battalion)
  UnbindGroup,    // the groups of `units` are disbanded
  Occupy,         // infantry in `units` enter `targetUnit` (a building or bunker) or man its trench line
  Unload,         // occupants of the structures in `units` (or the occupants listed) come out
};

// Production lines: one per kind of thing a side builds.
enum class Queue : uint8_t { Buildings = 0, Defenses, Infantry, Vehicles, Count };
constexpr int QUEUE_COUNT = static_cast<int>(Queue::Count);
Queue queueFor(UnitCategory c);

struct ProductionQueue {
  std::vector<uint16_t> items;  // unit types, front one in work
  int32_t progress = 0;         // ticks of work done on the front item
  int32_t paid = 0;             // supplies spent on it so far
  bool ready = false;           // a structure, finished and waiting to be placed
};

// Each side's economy.
struct PlayerState {
  int32_t credits = 0;
  int32_t powerMade = 0, powerUsed = 0;
  std::array<ProductionQueue, QUEUE_COUNT> queues;
  bool defeated = false;
  bool won = false;
  int32_t incomeAcc = 0;  // supplies earned but not yet whole (income is per minute)
  int32_t incomePerMinute = 0;  // for the sidebar
  bool lowPower() const { return powerUsed > powerMade; }
};

// Shapes a group marches in and stands in when it arrives.
enum class Formation : uint8_t {
  Box = 0,  // a compact block (default)
  Line,     // a broad front, two or three ranks deep
  Column,   // two abreast, for roads and gaps
  Wedge,    // an arrowhead with the point forward
  Loose,    // a spread-out block, safer against artillery
  Count
};

// A unit created by a SpawnList command: which command (tag), which entry of
// its list (index), and the new entity.
struct SpawnRecord {
  uint32_t tag = 0;
  uint32_t index = 0;
  Entity entity;
};

// One unit to create, exactly as described (editor placement, undo, snapshots).
struct SpawnSpec {
  uint16_t type = 0;
  uint8_t owner = 0;
  Vec2i pos;
  Angle facing = 0;
  int32_t health = -1;  // -1 = full
};

// A change to one unit. Fields left at their "keep" value are not touched.
struct UnitEdit {
  Entity unit;
  bool move = false;
  Vec2i pos;
  int32_t facing = -1;  // Angle, or -1 = keep
  int16_t owner = -1;   // -1 = keep
  int32_t health = -1;  // -1 = keep (clamped to 1..max)
};

struct Command {
  CommandType type = CommandType::Move;
  uint8_t player = 0;
  Vec2i target;  // leptons
  Entity targetUnit;
  uint16_t unitType = 0;
  uint16_t count = 1;
  Angle facing = 0;
  std::vector<Entity> units;
  uint32_t tag = 0;               // SpawnList: label for the spawn log
  uint32_t indexBase = 0;         // SpawnList: logged index of spawns[0]
  uint16_t weapon = 0;            // Strike
  int32_t radius = 0;             // Strike, leptons
  std::vector<SpawnSpec> spawns;  // SpawnList
  std::vector<UnitEdit> edits;    // Edit
  // Move / AttackMove: formation, the direction its front faces (Angle, or -1 =
  // facing the way the group travels), its width in leptons (0 = automatic),
  // and forced march (no stopping to return fire).
  Formation formation = Formation::Box;
  int32_t formFacing = -1;
  int32_t formWidth = 0;
  bool forced = false;
  Stance stance = Stance::Aggressive;  // SetStance
  int32_t amount = 0;                  // SetCredits
};

enum class EventType : uint8_t {
  Fire, Impact, UnitDied,
  StructurePlaced,  // unitType at `at`, for owner
  StructureSold,    // unitType at `at`
  UnitReady,        // owner's production finished a unitType (structure: ready to place)
  CannotBuild,      // owner tried something not allowed (no money, bad spot...)
  PlayerDefeated,   // owner has nothing left (or a script said so)
  PlayerWon,        // a script declared owner the winner
  ScriptMessage,    // `text` for owner (ANY_PLAYER = everyone)
  ObjectivesChanged,
  CameraTo,         // move owner's view to `at`
  GroupFormed,      // owner bound `amount` units into a fighting unit
  Hint,             // a script's hint for owner (`text`; empty: close the panel)
  Highlight,        // a script highlights the interface element `text` for owner
  ScoutFallsBack,   // owner's scout pulled back to `at` from an enemy that came close
  PointCaptured,    // owner took the logistics point at `at`: its office stands, the trucks are on their way
  ConvoyArrived,    // a supply truck paid `amount` in at owner's headquarters at `at`
};

struct SimEvent {
  EventType type = EventType::Fire;
  uint8_t owner = 0;
  uint16_t weapon = 0;    // Fire / Impact
  uint16_t unitType = 0;  // UnitDied
  Vec2i from;             // Fire: muzzle position
  Vec2i at;               // Fire: aim point; Impact / UnitDied: position
  Angle facing = 0;       // UnitDied: body facing
  Angle turret = 0;       // UnitDied: turret facing
  bool hit = true;        // Impact: did it hit a unit
  int32_t amount = 0;     // GroupFormed: members
  std::string text;       // ScriptMessage
};

struct SimStats {
  // Milliseconds spent in each step during the last tick (for profiling).
  float msPaths = 0, msOrders = 0, msMove = 0, msSpatial = 0, msSeparation = 0, msVision = 0, msTargeting = 0,
        msWeapons = 0, msProjectiles = 0, msEconomy = 0;
  uint32_t pathSearches = 0;
  uint64_t pathExpansions = 0;
  uint32_t pathQueue = 0;
  uint32_t projectiles = 0;
};

// Commands in binary, for save games and replays.
void writeCommand(ByteWriter& w, const Command& c);
Command readCommand(ByteReader& r);

class Simulation {
 public:
  // The simulation owns the map's building layer (Map::block) while it runs.
  Simulation(Map& map, const UnitTypeRegistry& types, const WeaponRegistry& weapons, uint64_t seed);

  // Commands from players and the editor. While recording, each one is also
  // logged with the tick it applies on, which is all a replay needs.
  void queue(Command cmd) {
    if (recording_) log_.push_back({tick_, cmd});
    pending_.push_back(std::move(cmd));
  }
  void tick();

  uint32_t tickCount() const { return tick_; }
  Registry& registry() { return reg_; }
  const UnitTypeRegistry& types() const { return types_; }
  const WeaponRegistry& weapons() const { return weapons_; }
  // The type and weapon registries were changed under the running battle
  // (rules editor, live): gives units the parts their new rules call for
  // (guns, turrets, supply loads) and caps health at the new maximum.
  // Type and weapon numbers must not have moved (see slave::stabiliseRules).
  void rulesChanged();
  const Map& map() const { return map_; }
  const PlayerState& playerState(uint8_t p) const { return players_[p % MAX_PLAYERS]; }

  // --- Building rules (also used by the UI to show what is allowed) -----------
  // Top-left tile of a structure whose footprint is centred on `centre`.
  Vec2i footprintOrigin(int type, Vec2i centre) const;
  // Ground is clear for this structure (terrain, other structures, units).
  bool canPlace(int type, Vec2i origin, bool anySlope = false, bool ignoreUnits = false) const;
  // Within building range of the player's own structures.
  bool inBuildRange(uint8_t player, int type, Vec2i origin) const;
  // The player owns what this type requires, and something that can make it.
  bool canProduce(uint8_t player, int type) const;
  // Structure entity on a tile, or kNullEntity.
  Entity structureAt(int tx, int ty) const;
  // Damage reduction (percent) infantry gets on a tile from fortifications.
  uint8_t coverAt(int tx, int ty) const;
  const Vision& vision() const { return vision_; }
  Vision& vision() { return vision_; }
  Pathfinder& pathfinder() { return pathfinder_; }
  const std::vector<SimEvent>& events() const { return events_; }  // from the last tick
  // Units created by SpawnList commands in the last tick, with their tags.
  const std::vector<SpawnRecord>& spawnLog() const { return spawnLog_; }
  // Teams: players on the same team are allies (no targeting, shared vision).
  uint8_t team(uint8_t player) const { return player < MAX_PLAYERS ? team_[player] : NEUTRAL_OWNER; }
  bool allied(uint8_t a, uint8_t b) const { return a == b || (a < MAX_PLAYERS && b < MAX_PLAYERS && team_[a] == team_[b]); }
  const SimStats& stats() const { return stats_; }

  // --- Scenario script ----------------------------------------------------------
  // `keepProgress`: an edit during play keeps what has fired so far.
  void setScript(const ScenarioScript& s, bool keepProgress);
  const ScenarioScript& script() const { return script_; }
  const std::vector<Objective>& objectives() const { return objectives_; }
  bool triggerFired(int i) const { return i >= 0 && i < static_cast<int>(triggerState_.size()) && triggerState_[static_cast<size_t>(i)].fires > 0; }
  bool triggerEnabled(int i) const { return i >= 0 && i < static_cast<int>(triggerState_.size()) && triggerState_[static_cast<size_t>(i)].enabled; }
  int countUnits(uint8_t player, int area, int unitType) const;  // area -1 = whole map

  // --- Computer players ---------------------------------------------------------
  const SkirmishAi& ai() const { return ai_; }
  // --- Tutorials and campaigns ----------------------------------------------------
  const std::string& hint(uint8_t p) const { return hints_[p % MAX_PLAYERS]; }
  const std::string& highlight(uint8_t p) const { return highlights_[p % MAX_PLAYERS]; }
  bool buildLocked(uint8_t p, int type) const {
    const auto& l = buildLocks_[p % MAX_PLAYERS];
    return type >= 0 && type < static_cast<int>(l.size()) && l[static_cast<size_t>(type)];
  }
  int32_t flag(const std::string& name) const {
    auto it = flags_.find(name);
    return it == flags_.end() ? 0 : it->second;
  }
  const std::map<std::string, int32_t>& flags() const { return flags_; }
  // Before a mission starts: the flags a campaign carries over from earlier missions.
  void setFlags(std::map<std::string, int32_t> f) { flags_ = std::move(f); }
  // For the AI (and scripts): queue a command without logging it.
  void queueInternal(Command cmd) { pending_.push_back(std::move(cmd)); }

  // --- Saving and replays -------------------------------------------------------
  // The whole simulation state (not the terrain, which is saved with the map).
  // Loading continues exactly where saving stopped: same state hash.
  void saveState(ByteWriter& w) const;
  // `savedTypes`: the unit type ids, in order, of the rules the state was saved
  // with (save meta "types"). Units and queued orders are renumbered to these
  // rules by id, so adding a unit type (a new faction, a mod) doesn't scramble
  // older saves. Sets `remapped` when any number changed.
  bool loadState(ByteReader& r, std::string& error, const std::vector<std::string>* savedTypes = nullptr,
                 bool* remapped = nullptr);
  // The unit type ids in registry order, for the save meta.
  std::vector<std::string> typeIds() const;
  struct LoggedCommand {
    uint32_t tick;
    Command cmd;
  };
  void startRecording() {
    recording_ = true;
    log_.clear();
  }
  void stopRecording() { recording_ = false; }
  bool recording() const { return recording_; }
  const std::vector<LoggedCommand>& commandLog() const { return log_; }

  // Deterministic checksum of the whole simulation state. Two runs given the
  // same commands must always produce the same hash.
  uint64_t stateHash();

  // --- Formations (formation.cpp) -------------------------------------------------
  // The march a unit is part of, or null.
  const CFormation* formationOf(Entity unit) const;
  // The shape a unit was last ordered in (Box if never).
  Formation shapeOf(Entity unit) const;
  // Where each of `units` would stand if ordered there now (world leptons, in
  // no particular unit order): for the interface's preview of a dragged front.
  std::vector<Vec2i> formationPreview(const std::vector<Entity>& units, Vec2i target, Formation f, int32_t formFacing,
                                      int32_t formWidth) const;
  // Reverse gear: would this vehicle back up to `to` (a short way to its rear)
  // instead of turning round? Farther when it is in contact with the enemy.
  bool wouldReverse(Entity e, Vec2i to) const;
  size_t unitCount() { return reg_.pool<CUnit>().size(); }
  int32_t maxRange(const UnitTypeDef& def) const;

 private:
  struct PathRequest {
    Entity unit;
    Vec2i goal;
    uint32_t id;
  };

  // commands (simulation.cpp)
  Entity spawnUnit(uint16_t type, uint8_t player, Vec2i pos, Angle facing);
  void applyCommand(const Command& cmd);
  void orderMove(const std::vector<Entity>& units, Vec2i target, OrderType type, const Command& cmd);
  // A group's slots: who stands where in the frame of the formation.
  struct FormationLayout {
    std::vector<Entity> units;    // every mover, block by block (tracked, wheeled, foot)
    std::vector<Vec2i> local;     // slots in the target's frame: x across the front, y backwards (leptons)
    std::vector<size_t> blockEnd; // end index of each block in `local`
    std::vector<Locomotor> blockLoco;
    std::vector<int32_t> blockSpacing;  // each block's distance between ranks
    Vec2i centre;                 // the frame's centre, same coordinates
    Vec2i centroid;               // where the group is now (world leptons)
    Angle facing = 0;             // the front the formation ends up with
  };
  bool layoutFormation(const std::vector<Entity>& units, Vec2i target, Formation f, int32_t formFacing, int32_t formWidth,
                       FormationLayout& out) const;
  Vec2i fitFormation(const FormationLayout& L, Vec2i target, Angle facing) const;
  void stepFormations();
  void releaseFormation(CFormation& f);
  void reactToFire(uint32_t ei, CCombat& c, CUnit& u, const CPosition& p, COrder& o);
  void setOrderIdle(Entity e);
  void applyEdit(const UnitEdit& ed);

  // movement (movement.cpp)
  void requestPath(Entity e, Vec2i goal);
  void setPath(Entity e, std::vector<Vec2i> path);
  int32_t reverseReach(uint32_t ei, const UnitTypeDef& def) const;
  void processPathQueue();
  void stepOrders();
  void stepMovement();
  void rebuildSpatial();
  void stepSeparation();
  void stepVision();

  // economy and structures (economy.cpp)
  void occupy(uint32_t e, const UnitTypeDef& def, Vec2i origin);
  // Levels a new building's plot and eases the ground round it into the slope.
  void levelPlot(uint32_t e, const UnitTypeDef& def, Vec2i origin);
  void destroyEntity(uint32_t e);
  void recomputeCover();
  void stepEconomy();
  void stepProduction(uint8_t player);
  void stepRepairs();
  void completeUnit(uint8_t player, uint16_t type);
  Entity producerFor(uint8_t player, const std::string& role) const;
  Vec2i exitOf(Entity structure) const;
  void applyEconomyCommand(const Command& cmd);
 public:
  bool canRepair(Entity engineer, Entity target) const;
  // --- Organisation, shelter and supply (organisation.cpp) ---------------------
  const CGroup* groupOf(Entity unit) const;
  int32_t groupBonus(Entity shooter) const;  // extra firepower, percent
  static int32_t echelonSize(bool vehicles, Echelon e);
  bool canGarrison(Entity unit, Entity building) const;
  // A piece of a field-work line that men man: a trench or a sandbag wall.
  // Men in it stay visible, fire out, and leave only when told to evacuate
  // (not when ordered to move).
  bool isTrench(Entity structure) const;
  // Where the i-th man of a trench or sandbag piece takes post (leptons):
  // along the dug channel, or just behind the wall of bags.
  Vec2i fieldworkSlot(Entity piece, size_t i) const;
  // Neighbours of the same kind and side: +X 1, +Y 2, -X 4, -Y 8.
  int fieldworkMask(Entity piece) const;
  int32_t groupStrength(const std::vector<Entity>& members, bool combined) const;
  void applyCommandBody(const Command& cmd);
  bool inTrench(Entity unit) const;
  int garrisonLevel(Entity building) const;  // 0 empty, 1..3 how fortified it looks
  std::vector<Vec2i> trenchLine(Entity piece) const;
  // --- Logistics points (9.6) --------------------------------------------------
  // The map's supply points are logistics points: a side that holds one alone
  // for a while gets a logistics office there, and three supply trucks that
  // carry money from it to the side's headquarters.
  struct PointState {
    uint8_t side = 0xFF;    // who is taking it (0xFF: nobody)
    uint16_t progress = 0;  // ticks held so far
  };
  static constexpr int32_t kCaptureTicks = 12 * TICKS_PER_SECOND;
  static constexpr int32_t kCaptureReach = 3 * LEPTONS_PER_TILE + 128;
  const PointState& pointState(size_t point) const {
    static const PointState none;
    return point < points_.size() ? points_[point] : none;
  }
  Entity officeOn(int point) const;       // the logistics office standing on a point, or null
  int pointUnder(int type, Vec2i origin) const;  // a logistics point this footprint would cover, or -1
  int officeTrucks(Entity office) const;  // supply trucks working for it
  // How many more of this type the player may have (per-yard limits); INT32_MAX = no limit.
  int32_t roomFor(uint8_t player, int type) const;
  // How fast `player`'s line builds this type, in halves of the normal speed
  // (2 = normal; every further building of the kind that builds it adds one, up to 6).
  int32_t lineSpeed(uint8_t player, int type) const;
  // Percent faster a unit of `owner` at (x, y) fires: the best friendly aura over it.
  int32_t fireRateBonus(uint8_t owner, int32_t x, int32_t y) const;
  // Tests and tools: hurt a unit as a weapon would (hundredths of a hit point).
  void debugDamage(Entity e, int32_t centi) {
    if (reg_.alive(e) && reg_.has<CUnit>(e)) damage(e.index, centi, kNullEntity);
  }
 private:
  uint16_t nextGroupNumber(uint8_t owner, bool vehicles, Echelon e);
  Entity makeGroup(const std::vector<Entity>& members, uint8_t owner, bool vehicles, Echelon e);
  void disband(Entity group);
  void bindUnits(const std::vector<Entity>& units, uint8_t owner);
  void refreshGroup(Entity group);
  void stepGroups();
  void enterBuilding(Entity u, Entity b);
  void leaveBuilding(Entity u);
  void applyOccupy(const Command& cmd);
  void applyUnload(const Command& cmd);
  void stepGarrisons();
  int32_t shelterPercent(uint32_t victim) const;
  void stepIncome();
  void stepPoints();
  void stepConvoys();
  void checkDefeat();

  // scripting (script.cpp)
  void stepScript();
  bool evaluate(const Condition& c, size_t trigger) const;
  void perform(const Action& a);
  std::vector<Entity> unitsIn(uint8_t player, int area, int unitType) const;

  // combat (combat.cpp)
  void stepTargeting();
  // Scouts fall back a few tiles from enemies that come close (9.3).
  void stepScouts();
  void stepWeapons();
  void stepProjectiles();
  void impact(int weapon, uint8_t owner, Vec2i at, Entity directTarget, Vec2i from, Entity shooter);
  void damage(uint32_t victim, int32_t amount, Entity shooter = kNullEntity);
  bool canSee(uint8_t player, int32_t x, int32_t y) const;
  void removeDead();

  Map& map_;
  const UnitTypeRegistry& types_;
  const WeaponRegistry& weapons_;
  mutable Registry reg_;  // pools are created on first use, even from const queries
  Rng rng_;
  uint32_t tick_ = 0;
  std::vector<Command> pending_;
  std::vector<SimEvent> events_;
  std::vector<SpawnRecord> spawnLog_;
  std::array<uint8_t, MAX_PLAYERS> team_{0, 1, 2, 3, 4, 5, 6, 7};
  std::array<PlayerState, MAX_PLAYERS> players_{};
  std::vector<uint32_t> tileStructure_;  // entity index per tile, UINT32_MAX = none
  std::vector<uint8_t> cover_;
  bool coverDirty_ = false;
  std::array<bool, MAX_PLAYERS> playedOnce_{};
  int32_t structureReach_ = 0;  // how far a structure's edge reaches beyond its centre (largest standing)
  std::vector<PointState> points_;  // one per map supply point
  struct Aura {
    uint8_t owner;
    Vec2i at;
    int32_t radius, fireRate;
  };
  std::vector<Aura> auras_;  // rebuilt every second

  struct TriggerState {
    bool enabled = true;
    uint32_t fires = 0;
    uint32_t lastFire = 0;
  };
  ScenarioScript script_;
  std::vector<TriggerState> triggerState_;
  std::array<bool, MAX_PLAYERS> scriptDefeated_{};
  struct RevealedArea {
    uint8_t player;
    int32_t area;
  };
  std::vector<RevealedArea> revealed_;  // areas a script has revealed for good
  // Tutorials and campaigns.
  std::array<std::string, MAX_PLAYERS> hints_, highlights_;
  std::array<std::vector<uint8_t>, MAX_PLAYERS> buildLocks_;  // by unit type: 1 = a script forbids it
  std::array<std::array<uint32_t, 32>, MAX_PLAYERS> ordersGiven_{};  // by CommandType, human players only
  std::map<std::string, int32_t> flags_;
  std::vector<Objective> objectives_;
  SkirmishAi ai_;
  bool recording_ = false;
  std::vector<LoggedCommand> log_;
  friend class SkirmishAi;
  int32_t effectiveDistance(uint32_t d, uint32_t target) const;
  Pathfinder pathfinder_;
  SpatialHash spatial_;
  Vision vision_;
  std::vector<PathRequest> pathQueue_;
  std::array<std::array<uint16_t, 6>, MAX_PLAYERS * 2> groupNumbers_{};  // last number used, per side, kind, echelon
  size_t pathQueueHead_ = 0;
  uint32_t nextRequest_ = 1;
  std::vector<uint32_t> dead_;
  std::vector<int32_t> pushX_, pushY_;
  SimStats stats_;
};

}  // namespace slave

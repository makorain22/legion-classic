#pragma once
// Slave Engine - the computer player.
//
// A skirmish opponent that plays by the same rules as a human: it only issues
// ordinary commands (produce, place, move, attack, bind...), pays for
// everything, and runs inside the simulation tick, so games against it replay
// exactly.
//
// Each side fights to its own doctrine. The Germans build fewer, better
// units, bind them into combined-arms battle groups, fortify in depth and
// attack when they are ready; the Soviets mass infantry and medium tanks,
// feed reinforcements into an attack as it goes and lean on rocket
// artillery.
//
// With several enemies (a free-for-all) it picks the nearest as its main
// enemy and sends the main force against that one, while its raiding parties
// take the others in turn, so nobody is left in peace. Whatever it fields it
// binds into units - battle groups, platoons, squads - for the firepower
// that brings, and binds again as reinforcements come up.
//
// Its forces:
//   - the main force gathers in front of the base, binds into a unit and
//     attacks, often by a flanking route; when it is badly hurt it falls
//     back a little to regroup and dig in rather than running home;
//   - raiders - fast cars and light tanks - hunt enemy logistics offices,
//     supply trucks and stragglers, hopping from target to target;
//   - a scout stands watch out in front and moves from post to post;
//   - a home guard mans the base's trenches and bunkers;
//   - damaged vehicles go home for repairs on their own.
// It keeps expanding: post offices, warehouses and a fuel depot behind its
// headquarters, parties sent out to take logistics points (with an MG nest
// beside each office), lost supply trucks replaced, a second construction
// yard later on, trench lines, wire and tank traps across the approach to
// its base. Difficulty
// changes how fast it thinks, how big its attacks are and how much it builds.

#include <cstdint>
#include <vector>

#include "slave/ecs/registry.hpp"
#include "slave/math/vec.hpp"

namespace slave {

class Simulation;
class ByteWriter;
class ByteReader;

enum class AiDifficulty : uint8_t { Easy = 0, Normal, Hard, Count };
// What a script may forbid the computer (AiBehaviour; bit numbers).
enum AiBehaviour : int { kAiBuild = 0, kAiTrain, kAiAttack, kAiRaid, kAiCapture, kAiFortify, kAiScout, kAiRepair };

struct AiPlayer {
  bool enabled = false;
  AiDifficulty difficulty = AiDifficulty::Normal;
  uint32_t nextThink = 0;
  Vec2i base{-1, -1};        // centre of its base (its first yard)
  Vec2i staging{-1, -1};     // where new troops gather
  Vec2i toward{0, 0};        // unit vector-ish (x 256) from base toward the enemy
  uint32_t waves = 0;        // attacks launched so far
  uint32_t lastWaveTick = 0;
  std::vector<Entity> wave;  // the main force, once it has set off
  Vec2i waveTarget{-1, -1};
  Entity waveTargetUnit;
  // Added with save version 3.
  uint8_t phase = 0;            // main force: 0 gathering, 1 attacking, 2 regrouping
  uint32_t phaseTick = 0;
  int32_t launchStrength = 0;   // strength when the attack set off
  Vec2i flank{-1, -1};          // waypoint of a flanking approach (x < 0: none)
  Vec2i forward{-1, -1};        // where the main force regroups
  std::vector<Entity> raiders;
  Vec2i raidTarget{-1, -1};
  uint32_t raidReady = 0;       // next raid not before this tick
  std::vector<Entity> guard;    // home guard
  uint32_t lastFortify = 0;
  Vec2i expandTo{-1, -1};       // where a construction truck is heading
  // Added with save version 6 (the 9.6 economy).
  std::vector<Entity> capture;  // the party sent to take a logistics point
  Vec2i captureTarget{-1, -1};  // that point (leptons)
  uint32_t captureReady = 0;    // next party not before this tick
  // Added with save version 7 (9.7: the main enemy, raids on the others, scouts).
  uint8_t mainEnemy = 0xFF;     // the side its main force goes for: the nearest one still standing (0xFF: none)
  uint8_t raidEnemy = 0xFF;     // the side its raiders are after this time
  uint32_t raids = 0;           // raiding parties sent out so far: they take the other enemies in turn
  Entity scout;                 // the scout posted out in front to watch
  uint8_t scoutPost = 0;        // which of its watch points it is on
  uint32_t scoutTick = 0;       // when it was sent there
  // Mission scripts' orders (9.9; saved with the mission state, SIM8).
  uint8_t order = 0;            // 0 its own choice, 1 attack `orderAt`, 2 defend `orderAt`
  Vec2i orderAt{-1, -1};        // leptons
  uint8_t disabled = 0;         // AiBehaviour bits it may not do (kAiBehaviourIds)
  int16_t waveScale = 0;        // percent of its usual attack size (0: usual)
  uint8_t orderFresh = 0;       // an attack order not yet acted on: go with what it has
  // Not saved: worked out afresh every time it thinks.
  Vec2i enemyAt{-1, -1};        // where the main enemy stands (the middle of its buildings)
  bool hasBase = false;         // it has a construction yard (a scripted force without one keeps no home guard)
};

class SkirmishAi {
 public:
  void setPlayer(uint8_t player, bool enabled, AiDifficulty difficulty);
  const AiPlayer& player(uint8_t p) const { return players_[p]; }
  bool any() const;
  // Called by the simulation at the start of each tick.
  void update(Simulation& sim);

  void save(ByteWriter& w) const;
  bool load(ByteReader& r, int version);
  // Mission orders (scripts).
  void setOrder(uint8_t player, uint8_t order, Vec2i at);
  void setBehaviour(uint8_t player, int bit, bool allowed);
  void setWaveScale(uint8_t player, int16_t percent);
  bool allowed(uint8_t player, int bit) const { return !(players_[player].disabled & (1u << bit)); }
  void saveOrders(ByteWriter& w) const;
  bool loadOrders(ByteReader& r);
  void clearOrders();

 private:
  void think(Simulation& sim, uint8_t p);
  bool manageEconomy(Simulation& sim, uint8_t p);  // true: expansion has first call on supplies
  void manageArmy(Simulation& sim, uint8_t p, bool saving);
  void fortify(Simulation& sim, uint8_t p);
  bool placeStructure(Simulation& sim, uint8_t p, int type, bool defensive);
  bool placeNear(Simulation& sim, uint8_t p, int type, Vec2i want, int radius);

  AiPlayer players_[8];
};

}  // namespace slave

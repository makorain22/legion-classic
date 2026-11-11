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
// Its forces:
//   - the main force gathers in front of the base, binds into a unit and
//     attacks, often by a flanking route; when it is badly hurt it falls
//     back a little to regroup and dig in rather than running home;
//   - raiders - fast cars and light tanks - hunt enemy logistics offices and
//     stragglers, hopping from target to target;
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

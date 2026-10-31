#pragma once
// Slave Engine - the computer player.
//
// A skirmish opponent that plays by the same rules as a human: it only issues
// ordinary commands (produce, place, move, attack...), pays for everything,
// and runs inside the simulation tick, so games against it replay exactly.
//
// Each think it: keeps its power up, follows a build order (economy first,
// then barracks, works, defences), keeps supply trucks working, trains a mixed
// army at a staging point in front of its base, sends the army to defend when
// the base is attacked, and launches attack waves at the nearest enemy
// structures once it has enough troops. Difficulty changes how fast it thinks,
// how big its waves are and how much it fortifies.

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
  std::vector<Entity> wave;  // units in the current attack
  Vec2i waveTarget{-1, -1};
  Entity waveTargetUnit;
};

class SkirmishAi {
 public:
  void setPlayer(uint8_t player, bool enabled, AiDifficulty difficulty);
  const AiPlayer& player(uint8_t p) const { return players_[p]; }
  bool any() const;
  // Called by the simulation at the start of each tick.
  void update(Simulation& sim);

  void save(ByteWriter& w) const;
  bool load(ByteReader& r);

 private:
  void think(Simulation& sim, uint8_t p);
  void manageEconomy(Simulation& sim, uint8_t p);
  void manageArmy(Simulation& sim, uint8_t p);
  bool placeStructure(Simulation& sim, uint8_t p, int type, bool defensive);

  AiPlayer players_[8];
};

}  // namespace slave

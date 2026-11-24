#pragma once
// Legion Classic - the battle as the lockstep sees it (no graphics).
//
// Orders travel as the engine's Command bytes (writeCommand). Every machine
// prepares an order the same way before the simulation gets it: the order
// is given the side its sender plays (seats, in the battle's save
// description), only orders a player may give in a multiplayer battle pass,
// and only the sender's own units take it. So nobody can order another
// side's units or spawn an army, whatever their game sends.
//
// A snapshot is a save game in memory (SaveFile: description, map, state),
// the same thing the game's own saves hold, so the dedicated server, the
// tests and the game all read each other's.
//
// NetWorld is the headless battle: the dedicated server runs one, the tests
// two or three. The game has its own adapter (net_game.cpp) over its scene,
// built from the same functions.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pioneer/map/map_io.hpp"
#include "pioneer/net/lockstep.hpp"
#include "pioneer/sim/simulation.hpp"

namespace legion {

inline constexpr const char* kNetGameId = "legion-classic";
// What must be the same on every machine of a game, besides the rules: the
// game's version and the engine's (session joining checks it).
std::string netVersion();

// The rules a dedicated server plays by: the base content and the mods
// enabled, merged as the game merges them. `hash` is what the session
// compares (the game's own rules hash).
struct NetRules {
  pioneer::WeaponRegistry weapons;
  pioneer::UnitTypeRegistry types;
  std::string hash;
  std::vector<std::string> mods;  // in load order
};
bool loadNetRules(const std::string& contentDir, const std::vector<std::string>& modDirs,
                  const std::vector<std::string>& enabledMods, const std::string& cacheDir, NetRules& out,
                  std::string& error, std::string* warnings = nullptr);

std::vector<uint8_t> encodeOrder(const pioneer::Command& c);
bool decodeOrder(const std::vector<uint8_t>& bytes, pioneer::Command& out);
// The orders a player may give in a multiplayer battle.
bool orderAllowed(pioneer::CommandType t);

// Who plays which side: the save description's "seats" ({"member": side}).
using Seats = std::map<uint8_t, uint8_t>;
Seats readSeats(const std::string& metaJson);
std::string writeSeats(const std::string& metaJson, const Seats& seats);

// Turns `member`'s order into the command the simulation gets (false: dropped).
bool prepareOrder(pioneer::Simulation& sim, const Seats& seats, uint8_t member, const std::vector<uint8_t>& bytes,
                  pioneer::Command& out);

// The simulation's per-system digest, for naming what differs after a desync.
std::vector<std::pair<std::string, uint64_t>> digestOf(pioneer::Simulation& sim);

class NetWorld final : public pioneer::net::LockstepAdapter {
 public:
  NetWorld(const pioneer::UnitTypeRegistry& types, const pioneer::WeaponRegistry& weapons)
      : types_(types), weapons_(weapons) {}

  // A battle from a map file, set up the way the editor's Play does (sides,
  // units, script). For tests and for a dedicated server started on a map.
  bool loadMapFile(const pioneer::MapFile& file, std::string& error);
  void setSeats(const Seats& s);
  const Seats& seats() const { return seats_; }

  void apply(uint8_t member, const std::vector<uint8_t>& order) override;
  void step() override { sim_->tick(); }
  uint32_t tick() const override { return sim_ ? sim_->tickCount() : 0; }
  uint64_t hash() const override { return sim_ ? sim_->stateHash() : 0; }
  std::vector<uint8_t> snapshot() const override;
  bool restore(const std::vector<uint8_t>& snap, std::string& error) override;
  std::vector<std::pair<std::string, uint64_t>> digest() const override {
    return sim_ ? digestOf(*sim_) : std::vector<std::pair<std::string, uint64_t>>{};
  }

  pioneer::Simulation* sim() { return sim_.get(); }
  pioneer::Map& map() { return file_.map; }
  const std::string& meta() const { return meta_; }
  uint32_t ordersApplied() const { return applied_; }
  uint32_t ordersDropped() const { return dropped_; }

 private:
  const pioneer::UnitTypeRegistry& types_;
  const pioneer::WeaponRegistry& weapons_;
  pioneer::MapFile file_;
  std::unique_ptr<pioneer::Simulation> sim_;
  std::string meta_ = "{}";
  Seats seats_;
  uint32_t applied_ = 0, dropped_ = 0;
};

}  // namespace legion

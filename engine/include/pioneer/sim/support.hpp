#pragma once
// Pioneer Engine - support from outside the battlefield (Milestone 9.14):
// call-ins. No aircraft or ships are units: a call-in is an order to people
// off the map - an air force, a fleet - who then deliver it. The game lists
// them as data (the "support" list of buildings.json, so mods merge them by
// id like any rule) and each side holds charges that recharge.
//
//   bombing    planes fly a line across the target and drop bombs along it
//   strafing   fighters rake a line with guns
//   attack     dive bombers or rocket planes hit one point hard
//   recon      a plane flies over and the ground round the target is seen
//              for a while (the enemy's units identified, not only chips)
//   supply     transports drop crates of supplies: whoever reaches one first
//              takes it
//   paradrop   transports drop men where the parachutes come down
//   naval      a ship's guns off the coast shell the target (only within
//              reach of the open sea)
//   landing    landing craft come in from the sea, beach, put men and
//              vehicles ashore and pull back (a set piece; in skirmish a
//              call-in on maps with a coast)
//   flyover, dogfight, crash   set pieces only, for scripts: planes that
//              fly over, a fight far off, a plane coming down
//
// Everything is worked out from ticks and the simulation's random numbers,
// so saves, replays and multiplayer keep in step; the planes are drawn
// along the same flight paths (Strike::planeAt). Flak guns (weapons with
// "antiAir": true) fire at planes overhead: a plane can be brought down,
// and a strike that has lost half its planes turns for home.

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "pioneer/math/vec.hpp"

namespace pioneer {

enum class SupportKind : uint8_t {
  Bombing, Strafing, Attack, Recon, Supply, Paradrop, Naval, Landing,
  Flyover, Dogfight, Crash,  // set pieces (scripts only)
  Count
};
const char* supportKindId(SupportKind k);  // "bombing"
bool supportKindFromId(const std::string& id, SupportKind& out);
bool supportUsesAircraft(SupportKind k);   // not naval or landing
bool supportIsSetPiece(SupportKind k);     // flyover, dogfight, crash

// Which aircraft a call-in sends (the model drawn; the look follows the faction).
enum class AircraftRole : uint8_t { Fighter, Attack, Bomber, Transport, Recon, Count };
const char* aircraftRoleId(AircraftRole r);
bool aircraftRoleFromId(const std::string& id, AircraftRole& out);

struct SupportPowerDef {
  std::string id;           // "ger_stuka"
  std::string name;         // "Stuka attack"
  std::string faction;      // "ger" (shown to that army; scripts may give it to anyone)
  std::string description;  // a line for the panel's tooltip
  SupportKind kind = SupportKind::Bombing;
  AircraftRole aircraft = AircraftRole::Bomber;
  std::string model;        // aircraft or ship model name (art); empty: by role and faction
  std::vector<std::string> requiresIds;  // in skirmish: the side needs one of these structures (any one)
  std::vector<int> prereqs;              // resolved type indices
  int32_t cost = 0;         // supplies each time
  int32_t charges = 1;      // most it can hold
  int32_t recharge = 180;   // seconds for one charge to come back
  int32_t cooldown = 20;    // seconds after a call before the next of the same
  int32_t planes = 3;       // aircraft (naval: salvos are counted in `shots`)
  int32_t shots = 8;        // bombs/bursts per plane; naval: shells in all
  std::string weaponId;     // what falls: a weapon of weapons.json
  int weapon = -1;
  int32_t length = 8 * 256;   // bombing/strafing: the line's length (leptons)
  int32_t radius = 3 * 256;   // attack/naval/recon/paradrop/supply: spread or reveal radius (leptons)
  int32_t speed = 110;        // leptons per tick in flight (110: about 6.4 tiles a second)
  int32_t delay = 4 * 15;     // ticks from the call until the first plane is over the map edge
  int32_t duration = 30;      // recon: seconds the ground stays seen; naval: seconds the shelling lasts
  std::vector<std::pair<std::string, int>> unitIds;  // paradrop/landing: unit types and how many
  std::vector<std::pair<int, int>> units;             // resolved
  int32_t crates = 3;         // supply: crates dropped...
  int32_t crateValue = 300;   // ...each worth this many supplies
  int32_t seaRange = 30;      // naval: the target must lie within this many tiles of the open sea
  int32_t aaChance = 12;      // percent a flak gun in range brings a plane down, each second
};

class UnitTypeRegistry;
class WeaponRegistry;

// "ger_rifleman*6, ger_halftrack" -> (type, count) pairs. False (and why) on an unknown type or a bad count.
bool parseUnitList(const std::string& text, const UnitTypeRegistry& types, std::vector<std::pair<int, int>>& out,
                   std::string* error);

class SupportRegistry {
 public:
  // The "support" list of a merged buildings.json (absent: no call-ins).
  bool loadJson(const nlohmann::json& list, std::string& error);
  bool resolve(const UnitTypeRegistry& types, const WeaponRegistry& weapons, std::string& error);
  int find(const std::string& id) const;
  const SupportPowerDef& get(int i) const { return defs_[static_cast<size_t>(i)]; }
  size_t size() const { return defs_.size(); }
  const std::vector<SupportPowerDef>& all() const { return defs_; }
  // Call-ins an army is offered (in rules order).
  std::vector<int> ofFaction(const std::string& faction) const;

 private:
  std::vector<SupportPowerDef> defs_;
};

// A side's hold on one call-in.
struct SupportState {
  enum class Gate : uint8_t { Auto = 0, On, Off };  // Auto: open while the side has a required structure
  Gate gate = Gate::Auto;
  int32_t charges = 0;      // ready now
  int32_t rechargeTicks = 0;  // progress towards the next charge
  uint32_t readyTick = 0;   // not before this tick (cooldown)
  uint32_t used = 0;        // how many times called this battle
};

// A call-in on its way or under way.
struct Strike {
  uint32_t id = 0;
  int16_t power = -1;       // SupportRegistry index (-1: a set piece from a script)
  SupportKind kind = SupportKind::Bombing;
  AircraftRole aircraft = AircraftRole::Bomber;
  uint8_t owner = 0;
  Vec2i target;             // leptons
  Vec2i from, to;           // flight: enters at `from` (off the map), leaves at `to`; naval/landing: ship or craft start, beach
  uint32_t startTick = 0;   // the first plane is at `from`
  int32_t speed = 110;      // leptons per tick
  uint8_t planes = 0;
  uint8_t alive = 0;        // bit per plane still flying
  uint8_t shots = 0;        // per plane (naval: in all)
  bool aborted = false;     // turned for home: drops nothing more
  bool done = false;
  uint32_t delivered = 0;   // drops made so far (bombs, crates, men...)
  int32_t value = 0;        // set pieces: seconds (dogfight); landing: unit count put ashore
  // Ticks from startTick until plane `i` is over point `along` leptons down its path.
  uint32_t length() const;  // `from` to `to`, leptons
  // Where plane `i` is at `tick` (may be fractional for drawing): ground position (leptons) and false if not on the map's way.
  bool planeAt(int i, double tick, int32_t& x, int32_t& y) const;
  // The spacing of the formation: plane `i`'s offset to the side of the line and behind the leader (leptons).
  static void formation(int i, int32_t& side, int32_t& behind);
  uint32_t endTick() const;  // when the last plane is off the map again (or the shelling/landing is over)
};

// A crate of supplies on the ground (supply drops).
struct SupplyCrate {
  Vec2i at;
  int32_t value = 0;
  uint32_t landTick = 0;  // on the ground (it falls till then)
  uint32_t until = 0;     // gone if nobody takes it by then
  uint8_t from = 0;       // the side that called it (anyone may take it)
};

// An aircraft brought down: where it falls, for the wreck and the smoke.
struct DownedPlane {
  Vec2i at;
  uint32_t tick = 0;
  uint8_t owner = 0;
  AircraftRole role = AircraftRole::Bomber;
};

}  // namespace pioneer

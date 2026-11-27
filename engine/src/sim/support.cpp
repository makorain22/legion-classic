// Pioneer Engine - call-ins: air support, naval guns, landings (Milestone
// 9.14). See sim/support.hpp.
#include "pioneer/sim/support.hpp"

#include <algorithm>
#include <cmath>
#include <deque>

#include <nlohmann/json.hpp>

#include "pioneer/core/fixed.hpp"
#include "pioneer/sim/simulation.hpp"
#include "pioneer/sim/unit_types.hpp"

namespace pioneer {

using nlohmann::json;

namespace {

const char* const kKindIds[] = {"bombing", "strafing", "attack", "recon",   "supply", "paradrop",
                                "naval",   "landing",  "flyover", "dogfight", "crash"};
static_assert(sizeof(kKindIds) / sizeof(kKindIds[0]) == static_cast<size_t>(SupportKind::Count));
const char* const kRoleIds[] = {"fighter", "attack", "bomber", "transport", "recon"};
static_assert(sizeof(kRoleIds) / sizeof(kRoleIds[0]) == static_cast<size_t>(AircraftRole::Count));

constexpr int32_t kMargin = 8 * LEPTONS_PER_TILE;  // flights start and end this far outside the map
constexpr uint32_t kFallTicks = 12;                // a bomb's fall
constexpr uint32_t kChuteTicks = 45;               // a parachute's descent
constexpr int kMaxPlanes = 8;

int32_t seconds(int32_t s) { return s * 15; }

// "ger_rifleman*6" or "ger_halftrack"
bool parseUnitEntry(const std::string& s, std::pair<std::string, int>& out) {
  const size_t star = s.find('*');
  out.first = s.substr(0, star);
  out.second = 1;
  if (star != std::string::npos) {
    try {
      out.second = std::stoi(s.substr(star + 1));
    } catch (...) {
      return false;
    }
  }
  return !out.first.empty() && out.second > 0 && out.second <= 50;
}

}  // namespace

const char* supportKindId(SupportKind k) { return k < SupportKind::Count ? kKindIds[static_cast<int>(k)] : "bombing"; }
bool supportKindFromId(const std::string& id, SupportKind& out) {
  for (int i = 0; i < static_cast<int>(SupportKind::Count); ++i)
    if (id == kKindIds[i]) {
      out = static_cast<SupportKind>(i);
      return true;
    }
  return false;
}
bool supportUsesAircraft(SupportKind k) { return k != SupportKind::Naval && k != SupportKind::Landing; }
bool supportIsSetPiece(SupportKind k) { return k == SupportKind::Flyover || k == SupportKind::Dogfight || k == SupportKind::Crash; }
const char* aircraftRoleId(AircraftRole r) { return r < AircraftRole::Count ? kRoleIds[static_cast<int>(r)] : "bomber"; }
bool aircraftRoleFromId(const std::string& id, AircraftRole& out) {
  for (int i = 0; i < static_cast<int>(AircraftRole::Count); ++i)
    if (id == kRoleIds[i]) {
      out = static_cast<AircraftRole>(i);
      return true;
    }
  return false;
}

bool parseUnitList(const std::string& text, const UnitTypeRegistry& types, std::vector<std::pair<int, int>>& out,
                   std::string* error) {
  out.clear();
  size_t at = 0;
  while (at <= text.size()) {
    size_t comma = text.find(',', at);
    if (comma == std::string::npos) comma = text.size();
    std::string item = text.substr(at, comma - at);
    item.erase(0, item.find_first_not_of(" \t"));
    if (const size_t e = item.find_last_not_of(" \t"); e != std::string::npos) item.erase(e + 1);
    item.erase(std::remove(item.begin(), item.end(), ' '), item.end());
    if (!item.empty()) {
      std::pair<std::string, int> e;
      if (!parseUnitEntry(item, e)) {
        if (error) *error = "'" + item + "' is not a unit and a count (like ger_rifleman*6)";
        return false;
      }
      const int t = types.find(e.first);
      if (t < 0 || types.get(t).isStructure()) {
        if (error) *error = "'" + e.first + "' is not a unit in the rules";
        return false;
      }
      out.push_back({t, e.second});
    }
    at = comma + 1;
  }
  if (out.empty() && error) *error = "no units";
  return !out.empty();
}

// ---------------------------------------------------------------------------
// The rules

bool SupportRegistry::loadJson(const json& list, std::string& error) {
  defs_.clear();
  if (list.is_null()) return true;
  if (!list.is_array()) {
    error = "\"support\" should be a list";
    return false;
  }
  try {
    for (const json& j : list) {
      SupportPowerDef d;
      d.id = j.at("id").get<std::string>();
      const std::string where = "support '" + d.id + "': ";
      d.name = j.value("name", d.id);
      d.faction = j.value("faction", std::string());
      d.description = j.value("description", std::string());
      const std::string kind = j.value("kind", std::string("bombing"));
      if (!supportKindFromId(kind, d.kind) || supportIsSetPiece(d.kind)) {
        error = where + "\"kind\" '" + kind + "' is not one of bombing, strafing, attack, recon, supply, paradrop, naval, landing";
        return false;
      }
      const std::string role = j.value("aircraft", std::string(d.kind == SupportKind::Strafing ? "fighter"
                                                               : d.kind == SupportKind::Attack ? "attack"
                                                               : d.kind == SupportKind::Recon  ? "recon"
                                                               : (d.kind == SupportKind::Supply || d.kind == SupportKind::Paradrop)
                                                                   ? "transport"
                                                                   : "bomber"));
      if (!aircraftRoleFromId(role, d.aircraft)) {
        error = where + "\"aircraft\" '" + role + "' is not one of fighter, attack, bomber, transport, recon";
        return false;
      }
      d.model = j.value("model", std::string());
      if (j.contains("requires")) d.requiresIds = j["requires"].get<std::vector<std::string>>();
      d.cost = std::max(0, j.value("cost", d.cost));
      d.charges = std::clamp(j.value("charges", d.charges), 1, 9);
      d.recharge = std::clamp(j.value("recharge", d.recharge), 1, 3600);
      d.cooldown = std::clamp(j.value("cooldown", d.cooldown), 0, 3600);
      d.planes = std::clamp(j.value("planes", d.planes), 1, kMaxPlanes);
      d.shots = std::clamp(j.value("shots", d.shots), 0, 200);
      d.weaponId = j.value("weapon", std::string());
      if (j.contains("length")) d.length = static_cast<int32_t>(j["length"].get<double>() * LEPTONS_PER_TILE);
      if (j.contains("radius")) d.radius = static_cast<int32_t>(j["radius"].get<double>() * LEPTONS_PER_TILE);
      d.speed = std::clamp(j.value("speed", d.speed), 8, 400);
      if (j.contains("delay")) d.delay = std::clamp(static_cast<int32_t>(j["delay"].get<double>() * 15), 0, 15 * 120);
      d.duration = std::clamp(j.value("duration", d.duration), 1, 600);
      for (const json& u : j.value("units", json::array())) {
        std::pair<std::string, int> e;
        if (!u.is_string() || !parseUnitEntry(u.get<std::string>(), e)) {
          error = where + "\"units\" has '" + u.dump() + "': a unit and a count, like \"ger_rifleman*6\"";
          return false;
        }
        d.unitIds.push_back(e);
      }
      d.crates = std::clamp(j.value("crates", d.crates), 1, 20);
      d.crateValue = std::max(0, j.value("crateValue", d.crateValue));
      d.seaRange = std::clamp(j.value("seaRange", d.seaRange), 1, 200);
      d.aaChance = std::clamp(j.value("aaChance", d.aaChance), 0, 100);
      for (const SupportPowerDef& have : defs_)
        if (have.id == d.id) {
          error = where + "given twice";
          return false;
        }
      defs_.push_back(std::move(d));
    }
  } catch (const std::exception& ex) {
    error = std::string("support: ") + ex.what();
    return false;
  }
  return true;
}

bool SupportRegistry::resolve(const UnitTypeRegistry& types, const WeaponRegistry& weapons, std::string& error) {
  for (SupportPowerDef& d : defs_) {
    const std::string where = "support '" + d.id + "': ";
    d.prereqs.clear();
    for (const std::string& r : d.requiresIds) {
      const int t = types.find(r);
      if (t < 0) {
        error = where + "requires '" + r + "', which is not a structure in the rules";
        return false;
      }
      d.prereqs.push_back(t);
    }
    d.weapon = -1;
    const bool needsWeapon = d.kind == SupportKind::Bombing || d.kind == SupportKind::Strafing ||
                             d.kind == SupportKind::Attack || d.kind == SupportKind::Naval;
    if (!d.weaponId.empty()) {
      d.weapon = weapons.find(d.weaponId);
      if (d.weapon < 0) {
        error = where + "weapon '" + d.weaponId + "' is not in weapons.json";
        return false;
      }
    } else if (needsWeapon) {
      error = where + "a " + supportKindId(d.kind) + " needs a \"weapon\"";
      return false;
    }
    d.units.clear();
    for (const auto& [id, n] : d.unitIds) {
      const int t = types.find(id);
      if (t < 0 || types.get(t).isStructure()) {
        error = where + "'" + id + "' is not a unit in the rules";
        return false;
      }
      d.units.push_back({t, n});
    }
    if ((d.kind == SupportKind::Paradrop || d.kind == SupportKind::Landing) && d.units.empty()) {
      error = where + "a " + std::string(supportKindId(d.kind)) + " needs \"units\"";
      return false;
    }
  }
  return true;
}

int SupportRegistry::find(const std::string& id) const {
  for (size_t i = 0; i < defs_.size(); ++i)
    if (defs_[i].id == id) return static_cast<int>(i);
  return -1;
}

std::vector<int> SupportRegistry::ofFaction(const std::string& faction) const {
  std::vector<int> out;
  for (size_t i = 0; i < defs_.size(); ++i)
    if (defs_[i].faction == faction) out.push_back(static_cast<int>(i));
  return out;
}

// ---------------------------------------------------------------------------
// Flight paths

void Strike::formation(int i, int32_t& side, int32_t& behind) {
  // A vee: the leader, then pairs either side, each pair further back.
  const int rank = (i + 1) / 2;
  side = (i == 0) ? 0 : ((i % 2) ? -1 : 1) * rank * 150;
  behind = rank * 220;
}

uint32_t Strike::length() const { return distance(to.x - from.x, to.y - from.y); }

bool Strike::planeAt(int i, double tick, int32_t& x, int32_t& y) const {
  const double len = static_cast<double>(length());
  if (len <= 0) return false;
  int32_t side = 0, behind = 0;
  formation(i, side, behind);
  const double s = (tick - static_cast<double>(startTick)) * speed - behind;
  if (s < 0 || s > len) return false;
  const double dx = (to.x - from.x) / len, dy = (to.y - from.y) / len;
  x = static_cast<int32_t>(std::lround(from.x + dx * s - dy * side));
  y = static_cast<int32_t>(std::lround(from.y + dy * s + dx * side));
  return true;
}

uint32_t Strike::endTick() const {
  switch (kind) {
    case SupportKind::Naval: return startTick + static_cast<uint32_t>(std::max(1, value)) * 15 + 60;
    case SupportKind::Landing: {
      const uint32_t run = length() / static_cast<uint32_t>(std::max(1, speed));
      return startTick + run * 2 + 15 * 12;  // in, ashore twelve seconds, out
    }
    case SupportKind::Dogfight: return startTick + static_cast<uint32_t>(std::max(1, value)) * 15;
    default: {
      int32_t side = 0, behind = 0;
      formation(std::max(0, planes - 1), side, behind);
      return startTick + (length() + static_cast<uint32_t>(behind)) / static_cast<uint32_t>(std::max(1, speed)) + 1;
    }
  }
}

// ---------------------------------------------------------------------------
// The simulation's side

void Simulation::resetSupport() {
  support_.assign(MAX_PLAYERS, std::vector<SupportState>(types_.support().size()));
  strikes_.clear();
  crates_.clear();
  downed_.clear();
  supportReveals_.clear();
  nextStrikeId_ = 1;
}

const SupportState& Simulation::supportState(uint8_t player, int power) const {
  static const SupportState none;
  if (player >= MAX_PLAYERS || power < 0 || static_cast<size_t>(player) >= support_.size() ||
      static_cast<size_t>(power) >= support_[player].size())
    return none;
  return support_[player][static_cast<size_t>(power)];
}

bool Simulation::supportOpen(uint8_t player, int power) const {
  if (player >= MAX_PLAYERS || power < 0 || static_cast<size_t>(power) >= types_.support().size()) return false;
  const SupportState& s = supportState(player, power);
  if (s.gate == SupportState::Gate::On) return true;
  if (s.gate == SupportState::Gate::Off) return false;
  const SupportPowerDef& d = types_.support().get(power);
  if (d.prereqs.empty()) return false;  // nothing opens it but a script
  for (int t : d.prereqs)
    if (static_cast<size_t>(t) < ownedTypes_[player].size() && ownedTypes_[player][static_cast<size_t>(t)] > 0) return true;
  return false;
}

int32_t Simulation::supportRechargeLeft(uint8_t player, int power) const {
  if (power < 0 || static_cast<size_t>(power) >= types_.support().size()) return 0;
  const SupportState& s = supportState(player, power);
  const SupportPowerDef& d = types_.support().get(power);
  if (s.charges >= d.charges) return 0;
  return std::max(0, seconds(d.recharge) - s.rechargeTicks);
}

void Simulation::setSupportGate(uint8_t player, int power, SupportState::Gate g) {
  if (player >= MAX_PLAYERS || power < 0 || static_cast<size_t>(power) >= types_.support().size()) return;
  if (support_.size() != MAX_PLAYERS) resetSupport();
  support_[player][static_cast<size_t>(power)].gate = g;
}

void Simulation::setSupportCharges(uint8_t player, int power, int32_t charges) {
  if (player >= MAX_PLAYERS || power < 0 || static_cast<size_t>(power) >= types_.support().size()) return;
  if (support_.size() != MAX_PLAYERS) resetSupport();
  SupportState& s = support_[player][static_cast<size_t>(power)];
  s.charges = std::clamp(charges, 0, types_.support().get(power).charges);
  s.rechargeTicks = 0;
}

// ---- the sea

void Simulation::rebuildSea() const {
  if (seaRevision_ == map_.revision() && !seaDist_.empty()) return;
  seaRevision_ = map_.revision();
  const int W = map_.width(), H = map_.height();
  seaDist_.assign(static_cast<size_t>(W) * H, 0xFFFF);
  std::vector<uint8_t> sea(static_cast<size_t>(W) * H, 0);
  // Open sea: deep water joined to the edge of the map.
  auto deep = [&](int x, int y) { return map_.info(x, y).waterDepth > 12; };
  std::deque<int> q;
  for (int x = 0; x < W; ++x)
    for (int y : {0, H - 1})
      if (deep(x, y) && !sea[static_cast<size_t>(y) * W + x]) {
        sea[static_cast<size_t>(y) * W + x] = 1;
        q.push_back(y * W + x);
      }
  for (int y = 0; y < H; ++y)
    for (int x : {0, W - 1})
      if (deep(x, y) && !sea[static_cast<size_t>(y) * W + x]) {
        sea[static_cast<size_t>(y) * W + x] = 1;
        q.push_back(y * W + x);
      }
  while (!q.empty()) {
    const int i = q.front();
    q.pop_front();
    const int x = i % W, y = i / W;
    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto& d : nb) {
      const int nx = x + d[0], ny = y + d[1];
      if (!map_.inBounds(nx, ny) || sea[static_cast<size_t>(ny) * W + nx] || !deep(nx, ny)) continue;
      sea[static_cast<size_t>(ny) * W + nx] = 1;
      q.push_back(ny * W + nx);
    }
  }
  // Distance in tiles from the open sea, everywhere (up to 255).
  for (int i = 0; i < W * H; ++i)
    if (sea[static_cast<size_t>(i)]) {
      seaDist_[static_cast<size_t>(i)] = 0;
      q.push_back(i);
    }
  while (!q.empty()) {
    const int i = q.front();
    q.pop_front();
    const uint16_t d0 = seaDist_[static_cast<size_t>(i)];
    if (d0 >= 255) continue;
    const int x = i % W, y = i / W;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int nx = x + dx, ny = y + dy;
        if (!map_.inBounds(nx, ny)) continue;
        uint16_t& n = seaDist_[static_cast<size_t>(ny) * W + nx];
        if (n <= d0 + 1) continue;
        n = static_cast<uint16_t>(d0 + 1);
        q.push_back(ny * W + nx);
      }
  }
}

int32_t Simulation::seaDistance(Vec2i at) const {
  rebuildSea();
  const int tx = at.x >> LEPTON_SHIFT, ty = at.y >> LEPTON_SHIFT;
  if (!map_.inBounds(tx, ty)) return -1;
  const uint16_t d = seaDist_[static_cast<size_t>(ty) * map_.width() + tx];
  return d == 0xFFFF ? -1 : d;
}

bool Simulation::findLanding(Vec2i at, int32_t rangeTiles, Vec2i& sea, Vec2i& beach) const {
  rebuildSea();
  const int W = map_.width();
  const int cx = at.x >> LEPTON_SHIFT, cy = at.y >> LEPTON_SHIFT;
  // The beach: land a man can stand on, beside the open sea, nearest the target.
  int best = -1;
  int64_t bestD = INT64_MAX;
  for (int y = std::max(0, cy - rangeTiles); y <= std::min(map_.height() - 1, cy + rangeTiles); ++y)
    for (int x = std::max(0, cx - rangeTiles); x <= std::min(W - 1, cx + rangeTiles); ++x) {
      if (seaDist_[static_cast<size_t>(y) * W + x] != 1 || !map_.passable(x, y, Locomotor::Foot)) continue;
      const int64_t d = static_cast<int64_t>(x - cx) * (x - cx) + static_cast<int64_t>(y - cy) * (y - cy);
      if (d < bestD) {
        bestD = d;
        best = y * W + x;
      }
    }
  if (best < 0) return false;
  const int bx = best % W, by = best / W;
  beach = {bx * LEPTONS_PER_TILE + 128, by * LEPTONS_PER_TILE + 128};
  // Out to sea: step away from the land, from sea tile to sea tile, a dozen tiles.
  int sx = bx, sy = by;
  for (int step = 0; step < 14; ++step) {
    int nx = sx, ny = sy, farthest = -1;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int x = sx + dx, y = sy + dy;
        if (!map_.inBounds(x, y) || seaDist_[static_cast<size_t>(y) * W + x] != 0) continue;
        // Farther from the beach, deeper.
        const int score = (x - bx) * (x - bx) + (y - by) * (y - by) + map_.info(x, y).waterDepth / 8;
        if (score > farthest) {
          farthest = score;
          nx = x;
          ny = y;
        }
      }
    if (farthest < 0) break;
    sx = nx;
    sy = ny;
  }
  if (sx == bx && sy == by) return false;
  sea = {sx * LEPTONS_PER_TILE + 128, sy * LEPTONS_PER_TILE + 128};
  return true;
}

// ---- calling

bool Simulation::canCallSupport(uint8_t player, int power, Vec2i at, std::string* why) const {
  auto no = [&](const char* w) {
    if (why) *why = w;
    return false;
  };
  if (player >= MAX_PLAYERS || power < 0 || static_cast<size_t>(power) >= types_.support().size()) return no("no such call-in");
  if (players_[player].defeated) return no("the side is out of the battle");
  const SupportPowerDef& d = types_.support().get(power);
  if (!supportOpen(player, power))
    return no(d.prereqs.empty() ? "not on this mission" : "needs its liaison post standing");
  const SupportState& s = supportState(player, power);
  if (s.charges <= 0) return no("not ready: recharging");
  if (tick_ < s.readyTick) return no("not ready: just called");
  if (players_[player].credits < d.cost) return no("not enough supplies");
  if (at.x < 0 || at.y < 0 || at.x >= map_.widthLeptons() || at.y >= map_.heightLeptons()) return no("off the map");
  if (d.kind == SupportKind::Naval) {
    const int32_t sd = seaDistance(at);
    if (sd < 0 || sd > d.seaRange) return no("out of the ships' reach: too far from the open sea");
  }
  if (d.kind == SupportKind::Landing) {
    Vec2i sea, beach;
    if (!findLanding(at, d.seaRange, sea, beach)) return no("no beach within reach of the open sea");
  }
  return true;
}

// Where the planes come from: from the caller's side of the map, unless told.
Vec2i Simulation::strikeHeading(uint8_t owner, Vec2i target) {
  int64_t sx = 0, sy = 0, n = 0;
  reg_.each<CUnit, CPosition>([&](uint32_t, const CUnit& u, const CPosition& p) {
    if (u.owner != owner || !types_.get(u.type).isStructure()) return;
    sx += p.x;
    sy += p.y;
    ++n;
  });
  Vec2i home;
  if (n > 0) home = {static_cast<int32_t>(sx / n), static_cast<int32_t>(sy / n)};
  else home = {map_.widthLeptons() / 2, map_.heightLeptons() / 2};
  Vec2i d = target - home;
  if (distance(d.x, d.y) < 4 * LEPTONS_PER_TILE) {
    // Too close to tell: from the nearest map edge.
    const int32_t l = target.x, r = map_.widthLeptons() - target.x, t = target.y, b = map_.heightLeptons() - target.y;
    const int32_t m = std::min({l, r, t, b});
    d = m == l ? Vec2i{1, 0} : m == r ? Vec2i{-1, 0} : m == t ? Vec2i{0, 1} : Vec2i{0, -1};
  }
  return d;
}

// The flight: in over the map edge along `dir` through `target`, and out the far side.
void Simulation::layFlight(Strike& s, Vec2i dir) const {
  const double len = std::max(1.0, std::hypot(static_cast<double>(dir.x), static_cast<double>(dir.y)));
  const double ux = dir.x / len, uy = dir.y / len;
  auto toEdge = [&](double sx, double sy) {
    // How far from the target to leave the map going (sx, sy).
    double best = 1e18;
    const double W = map_.widthLeptons(), H = map_.heightLeptons();
    if (sx > 1e-9) best = std::min(best, (W - s.target.x) / sx);
    if (sx < -1e-9) best = std::min(best, -s.target.x / sx);
    if (sy > 1e-9) best = std::min(best, (H - s.target.y) / sy);
    if (sy < -1e-9) best = std::min(best, -s.target.y / sy);
    return std::max(0.0, best) + kMargin;
  };
  const double back = toEdge(-ux, -uy), fwd = toEdge(ux, uy);
  s.from = {static_cast<int32_t>(std::lround(s.target.x - ux * back)), static_cast<int32_t>(std::lround(s.target.y - uy * back))};
  s.to = {static_cast<int32_t>(std::lround(s.target.x + ux * fwd)), static_cast<int32_t>(std::lround(s.target.y + uy * fwd))};
}

uint32_t Simulation::launchStrike(int power, SupportKind kind, AircraftRole role, uint8_t owner, Vec2i target, Vec2i dir,
                                  int planes, int shots, int32_t speed, int32_t delay, int32_t value) {
  Strike s;
  s.id = nextStrikeId_++;
  s.power = static_cast<int16_t>(power);
  s.kind = kind;
  s.aircraft = role;
  s.owner = owner;
  s.target = target;
  s.speed = std::max(1, speed);
  s.startTick = tick_ + static_cast<uint32_t>(std::max(0, delay));
  s.planes = static_cast<uint8_t>(std::clamp(planes, 0, kMaxPlanes));
  s.alive = static_cast<uint8_t>((1u << s.planes) - 1u);
  s.shots = static_cast<uint8_t>(std::clamp(shots, 0, 255));
  s.value = value;
  if (kind == SupportKind::Naval) {
    // The ship lies off the coast nearest the target.
    Vec2i sea, beach;
    s.from = findLanding(target, 255, sea, beach) ? sea : target;
    s.to = target;
  } else if (kind == SupportKind::Landing) {
    Vec2i sea, beach;
    if (!findLanding(target, 255, sea, beach)) return 0;
    s.from = sea;
    s.to = beach;
  } else {
    layFlight(s, dir);
  }
  strikes_.push_back(s);
  supportEvent(EventType::SupportCalled, owner, target, static_cast<int32_t>(s.id), static_cast<uint16_t>(std::max(0, power)));
  return s.id;
}

void Simulation::callSupport(const Command& cmd) {
  const int power = cmd.unitType;
  std::string why;
  if (!canCallSupport(cmd.player, power, cmd.target, &why)) {
    supportEvent(EventType::SupportCalled, cmd.player, cmd.target, 0, static_cast<uint16_t>(std::max(0, power)));  // refused
    return;
  }
  const SupportPowerDef& d = types_.support().get(power);
  SupportState& st = support_[cmd.player][static_cast<size_t>(power)];
  players_[cmd.player].credits -= d.cost;
  if (st.charges == d.charges) st.rechargeTicks = 0;  // the clock for the next charge starts now
  --st.charges;
  st.readyTick = tick_ + static_cast<uint32_t>(seconds(d.cooldown));
  ++st.used;
  Vec2i dir;
  if (cmd.amount == 1) dir = {cosQ16(cmd.facing) >> 4, sinQ16(cmd.facing) >> 4};
  else dir = strikeHeading(cmd.player, cmd.target);
  const int32_t value = d.kind == SupportKind::Naval ? d.duration : 0;
  launchStrike(power, d.kind, d.aircraft, cmd.player, cmd.target, dir, d.planes, d.shots, d.speed, d.delay, value);
}

uint32_t Simulation::startSetPiece(SupportKind kind, uint8_t owner, Vec2i at, Vec2i dir, int planes, int seconds_,
                                   AircraftRole role) {
  if (!supportIsSetPiece(kind) && kind != SupportKind::Landing) return 0;
  if (dir.x == 0 && dir.y == 0) dir = strikeHeading(owner, at);
  const int32_t speed = role == AircraftRole::Bomber || role == AircraftRole::Transport ? 95 : 130;
  return launchStrike(-1, kind, role, owner, at, dir, kind == SupportKind::Crash ? 1 : std::max(1, planes), 0, speed, 0,
                      std::max(1, seconds_));
}

uint32_t Simulation::scriptLanding(uint8_t owner, Vec2i at, const std::vector<std::pair<int, int>>& units) {
  Vec2i sea, beach;
  if (units.empty() || !findLanding(at, 255, sea, beach)) return 0;
  const uint32_t id = launchStrike(-1, SupportKind::Landing, AircraftRole::Transport, owner, at, {}, 1, 0, 28, 0, 0);
  if (id) landingUnits_[id] = units;
  return id;
}

void Simulation::supportEvent(EventType t, uint8_t owner, Vec2i at, int32_t amount, uint16_t unitType) {
  SimEvent e;
  e.type = t;
  e.owner = owner;
  e.at = at;
  e.amount = amount;
  e.unitType = unitType;
  events_.push_back(e);
}

// ---- each tick

void Simulation::stepSupport() {
  if (support_.size() != MAX_PLAYERS) resetSupport();
  const SupportRegistry& reg = types_.support();
  // Once a second: charges, and who owns what (for the liaison posts).
  if (tick_ % 15 == 0) {
    for (auto& v : ownedTypes_) v.assign(types_.size(), 0);
    reg_.each<CUnit>([&](uint32_t, const CUnit& u) {
      if (u.owner < MAX_PLAYERS && u.type < types_.size() && types_.get(u.type).isStructure())
        ++ownedTypes_[u.owner][u.type];
    });
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p) {
      if (support_[p].size() != reg.size()) support_[p].resize(reg.size());
      for (size_t i = 0; i < reg.size(); ++i) {
        SupportState& s = support_[p][i];
        const SupportPowerDef& d = reg.get(static_cast<int>(i));
        if (!supportOpen(p, static_cast<int>(i)) || s.charges >= d.charges) continue;
        s.rechargeTicks += 15;
        if (s.rechargeTicks >= seconds(d.recharge)) {
          s.rechargeTicks = 0;
          ++s.charges;
          supportEvent(EventType::SupportReady, p, {-1, -1}, s.charges, static_cast<uint16_t>(i));
        }
      }
    }
  }
  if (strikes_.empty() && crates_.empty() && downed_.empty()) return;

  // Flak, once a second: every enemy anti-aircraft gun in reach of a plane gets a chance at it.
  if (tick_ % 15 == 7) {
    struct Gun {
      Vec2i at;
      int32_t range;
      uint8_t owner;
      uint16_t weapon;
    };
    std::vector<Gun> guns;
    reg_.each<CUnit, CPosition>([&](uint32_t, const CUnit& u, const CPosition& p) {
      if (u.owner >= MAX_PLAYERS) return;
      for (int w : types_.get(u.type).weapons)
        if (w >= 0 && weapons_.get(w).antiAir) {
          guns.push_back({{p.x, p.y}, weapons_.get(w).range, u.owner, static_cast<uint16_t>(w)});
          break;
        }
    });
    if (!guns.empty())
      for (Strike& s : strikes_) {
        if (s.done || !supportUsesAircraft(s.kind) || supportIsSetPiece(s.kind) || s.power < 0) continue;
        const int chance = reg.get(s.power).aaChance;
        int lost = 0;
        for (int i = 0; i < s.planes; ++i) {
          if (!(s.alive & (1u << i))) {
            ++lost;
            continue;
          }
          int32_t x, y;
          if (!s.planeAt(i, tick_, x, y)) continue;
          for (const Gun& g : guns) {
            if (allied(g.owner, s.owner)) continue;
            if (distance(g.at.x - x, g.at.y - y) > static_cast<uint32_t>(g.range)) continue;
            SimEvent e;
            e.type = EventType::FlakFire;
            e.owner = g.owner;
            e.weapon = g.weapon;
            e.from = g.at;
            e.at = {x, y};
            events_.push_back(e);
            if (static_cast<int>(rng_.below(100)) < chance) {
              s.alive = static_cast<uint8_t>(s.alive & ~(1u << i));
              ++lost;
              // It comes down a little way on.
              const double len = std::max(1.0, static_cast<double>(s.length()));
              const Vec2i fall = {std::clamp(x + static_cast<int32_t>((s.to.x - s.from.x) / len * 900), 0, map_.widthLeptons() - 1),
                                  std::clamp(y + static_cast<int32_t>((s.to.y - s.from.y) / len * 900), 0, map_.heightLeptons() - 1)};
              downed_.push_back({fall, tick_ + 30, s.owner, s.aircraft});
              supportEvent(EventType::AircraftDown, s.owner, fall, static_cast<int32_t>(s.id));
              break;
            }
          }
        }
        // Half the flight gone: the rest turn for home.
        if (s.planes >= 2 && lost * 2 >= s.planes && !s.aborted) s.aborted = true;
        if (s.alive == 0) s.aborted = true;
      }
  }

  for (Strike& s : strikes_) {
    if (s.done) continue;
    if (tick_ >= s.endTick()) {
      s.done = true;
      continue;
    }
    if (tick_ < s.startTick) continue;
    const SupportPowerDef* d = s.power >= 0 ? &reg.get(s.power) : nullptr;
    const double len = std::max(1.0, static_cast<double>(s.length()));
    const double ux = (s.to.x - s.from.x) / len, uy = (s.to.y - s.from.y) / len;
    const double toTarget = std::hypot(static_cast<double>(s.target.x - s.from.x), static_cast<double>(s.target.y - s.from.y));
    // The tick plane `i` is over the point `along` leptons down the path.
    auto overTick = [&](int i, double along) {
      int32_t side, behind;
      Strike::formation(i, side, behind);
      return s.startTick + static_cast<uint32_t>(std::max(0.0, (along + behind) / s.speed));
    };
    auto drop = [&](Vec2i at, int weapon, uint16_t delay) {
      if (weapon < 0) return;
      at.x = std::clamp(at.x, 0, map_.widthLeptons() - 1);
      at.y = std::clamp(at.y, 0, map_.heightLeptons() - 1);
      const Entity pe = reg_.create();
      CProjectile pr;
      pr.from = pr.to = at;
      pr.x = at.x;
      pr.y = at.y;
      pr.total = 1;
      pr.weapon = static_cast<uint16_t>(weapon);
      pr.owner = s.owner;
      pr.delay = delay;
      reg_.add<CProjectile>(pe, pr);
      ++s.delivered;
    };
    switch (s.kind) {
      case SupportKind::Bombing:
      case SupportKind::Strafing: {
        if (s.aborted || !d) break;
        const int32_t L = d->length;
        for (int i = 0; i < s.planes; ++i) {
          if (!(s.alive & (1u << i))) continue;
          int32_t side, behind;
          Strike::formation(i, side, behind);
          for (int k = 0; k < s.shots; ++k) {
            const double along = toTarget - L / 2.0 + L * (k + 0.5) / s.shots;
            if (overTick(i, along) != tick_) continue;
            const int32_t jitter = s.kind == SupportKind::Bombing ? 96 : 48;
            const Vec2i at = {static_cast<int32_t>(std::lround(s.from.x + ux * along - uy * side)) + rng_.range(-jitter, jitter),
                              static_cast<int32_t>(std::lround(s.from.y + uy * along + ux * side)) + rng_.range(-jitter, jitter)};
            drop(at, d->weapon, s.kind == SupportKind::Bombing ? static_cast<uint16_t>(kFallTicks) : 0);
          }
        }
        break;
      }
      case SupportKind::Attack:
      case SupportKind::Supply:
      case SupportKind::Paradrop: {
        if (s.aborted || !d) break;
        for (int i = 0; i < s.planes; ++i) {
          if (!(s.alive & (1u << i)) || overTick(i, toTarget) != tick_) continue;
          if (s.kind == SupportKind::Attack) {
            for (int k = 0; k < s.shots; ++k)
              drop({s.target.x + rng_.range(-d->radius, d->radius), s.target.y + rng_.range(-d->radius, d->radius)}, d->weapon,
                   static_cast<uint16_t>(6 + k * 2));
          } else if (s.kind == SupportKind::Supply) {
            const int each = (d->crates + s.planes - 1) / s.planes;
            for (int k = 0; k < each && static_cast<int>(s.delivered) < d->crates; ++k) {
              SupplyCrate c;
              c.at = {std::clamp(s.target.x + rng_.range(-d->radius, d->radius), 0, map_.widthLeptons() - 1),
                      std::clamp(s.target.y + rng_.range(-d->radius, d->radius), 0, map_.heightLeptons() - 1)};
              c.value = d->crateValue;
              c.landTick = tick_ + kChuteTicks;
              c.until = c.landTick + static_cast<uint32_t>(seconds(180));
              c.from = s.owner;
              crates_.push_back(c);
              ++s.delivered;
            }
          } else {
            // Each plane carries its share of the stick; the men come down a little later.
            int total = 0;
            for (const auto& [t, n] : d->units) total += n;
            const int each = (total + s.planes - 1) / s.planes;
            pendingDrops_.push_back({tick_ + kChuteTicks, s.owner, s.target, d->radius, i * each, each, s.power, s.id});
            supportEvent(EventType::ChutesOpen, s.owner, s.target, std::min(each, std::max(0, total - i * each)));
            events_.back().weapon = static_cast<uint16_t>(std::clamp(d->radius, 0, 65535));
          }
        }
        break;
      }
      case SupportKind::Recon: {
        if (!d) break;
        for (int i = 0; i < s.planes; ++i)
          if ((s.alive & (1u << i)) && overTick(i, toTarget) == tick_) {
            supportReveals_.push_back({s.owner, s.target, static_cast<int16_t>(std::max(1, d->radius >> LEPTON_SHIFT)),
                                       tick_ + static_cast<uint32_t>(seconds(d->duration))});
            break;
          }
        break;
      }
      case SupportKind::Naval: {
        if (!d || s.shots == 0) break;
        const uint32_t span = static_cast<uint32_t>(std::max(1, s.value)) * 15;
        for (int k = 0; k < s.shots; ++k) {
          if (s.startTick + span * static_cast<uint32_t>(k) / s.shots != tick_) continue;
          drop({s.target.x + rng_.range(-d->radius, d->radius), s.target.y + rng_.range(-d->radius, d->radius)}, d->weapon, 24);
          SimEvent e;
          e.type = EventType::FlakFire;  // not flak: the ship's guns firing, drawn out at sea
          e.owner = s.owner;
          e.weapon = static_cast<uint16_t>(d->weapon);
          e.from = s.from;
          e.at = s.from;
          e.amount = 1;
          events_.push_back(e);
        }
        break;
      }
      case SupportKind::Landing: {
        const uint32_t run = s.length() / static_cast<uint32_t>(std::max(1, s.speed));
        if (tick_ != s.startTick + run) break;
        std::vector<std::pair<int, int>> units;
        if (d) units = d->units;
        else if (auto it = landingUnits_.find(s.id); it != landingUnits_.end()) units = it->second;
        int n = 0;
        for (const auto& [type, count] : units)
          for (int k = 0; k < count; ++k) {
            const int32_t sx = (n % 4 - 1) * 110, sy = (n / 4) * 110;
            // Up the beach, away from the sea.
            const Vec2i at = {std::clamp(s.to.x + static_cast<int32_t>(ux * (180 + sy)) - static_cast<int32_t>(uy * sx), 0,
                                         map_.widthLeptons() - 1),
                              std::clamp(s.to.y + static_cast<int32_t>(uy * (180 + sy)) + static_cast<int32_t>(ux * sx), 0,
                                         map_.heightLeptons() - 1)};
            const Entity e = spawnUnit(static_cast<uint16_t>(type), s.owner, at, atan2Angle(s.to.y - s.from.y, s.to.x - s.from.x));
            if (reg_.alive(e)) ++n;
          }
        s.value = n;
        s.delivered = static_cast<uint32_t>(n);
        landingUnits_.erase(s.id);
        supportEvent(EventType::TroopsLanded, s.owner, s.to, n);
        break;
      }
      case SupportKind::Crash: {
        if (overTick(0, toTarget) != tick_) break;
        s.alive = 0;
        downed_.push_back({s.target, tick_ + 20, s.owner, s.aircraft});
        supportEvent(EventType::AircraftDown, s.owner, s.target, static_cast<int32_t>(s.id));
        break;
      }
      default: break;  // flyover, dogfight: only the look
    }
  }

  // Men under their parachutes reach the ground.
  for (size_t i = 0; i < pendingDrops_.size();) {
    PendingDrop& pd = pendingDrops_[i];
    if (tick_ < pd.tick) {
      ++i;
      continue;
    }
    if (pd.power >= 0 && static_cast<size_t>(pd.power) < reg.size()) {
      const SupportPowerDef& d = reg.get(pd.power);
      int index = 0, made = 0;
      for (const auto& [type, count] : d.units)
        for (int k = 0; k < count; ++k, ++index) {
          if (index < pd.first || index >= pd.first + pd.count) continue;
          const Vec2i at = {std::clamp(pd.at.x + rng_.range(-pd.radius, pd.radius), 0, map_.widthLeptons() - 1),
                            std::clamp(pd.at.y + rng_.range(-pd.radius, pd.radius), 0, map_.heightLeptons() - 1)};
          if (reg_.alive(spawnUnit(static_cast<uint16_t>(type), pd.owner, at, static_cast<Angle>(rng_.below(ANGLE_FULL))))) ++made;
        }
      for (Strike& s : strikes_)
        if (s.id == pd.strike) s.delivered += static_cast<uint32_t>(made);
      if (made) supportEvent(EventType::TroopsLanded, pd.owner, pd.at, made);
    }
    pendingDrops_.erase(pendingDrops_.begin() + static_cast<long>(i));
  }

  // Crates: the first unit to reach one takes it, for its side.
  if (tick_ % 5 == 0 && !crates_.empty()) {
    for (size_t i = 0; i < crates_.size();) {
      SupplyCrate& c = crates_[i];
      if (tick_ >= c.until) {
        crates_.erase(crates_.begin() + static_cast<long>(i));
        continue;
      }
      bool taken = false;
      if (tick_ >= c.landTick) {
        uint8_t who = NEUTRAL_OWNER;
        reg_.each<CUnit, CPosition>([&](uint32_t, const CUnit& u, const CPosition& p) {
          if (who != NEUTRAL_OWNER || u.owner >= MAX_PLAYERS || types_.get(u.type).isStructure()) return;
          if (distance(p.x - c.at.x, p.y - c.at.y) <= 300) who = u.owner;
        });
        if (who != NEUTRAL_OWNER) {
          players_[who].credits += c.value;
          supportEvent(EventType::CrateTaken, who, c.at, c.value);
          taken = true;
        }
      }
      if (taken) crates_.erase(crates_.begin() + static_cast<long>(i));
      else ++i;
    }
  }
  // Wrecks of planes burn for a minute; finished strikes go.
  downed_.erase(std::remove_if(downed_.begin(), downed_.end(), [&](const DownedPlane& p) { return tick_ > p.tick + 900; }),
                downed_.end());
  supportReveals_.erase(std::remove_if(supportReveals_.begin(), supportReveals_.end(),
                                       [&](const SupportReveal& r) { return tick_ >= r.until; }),
                        supportReveals_.end());
  strikes_.erase(std::remove_if(strikes_.begin(), strikes_.end(), [&](const Strike& s) { return s.done; }), strikes_.end());
}

// Planes in the air see the ground under them; recon flights' targets stay seen a while.
void Simulation::stampSupportVision() {
  for (const SupportReveal& r : supportReveals_)
    for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
      if (team_[p] == team_[r.owner])
        vision_.stamp(p, r.at.x >> LEPTON_SHIFT, r.at.y >> LEPTON_SHIFT, r.radius, Vision::kVisible | Vision::kExplored | Vision::kScouted);
  for (const Strike& s : strikes_) {
    if (s.kind != SupportKind::Recon || s.done) continue;
    for (int i = 0; i < s.planes; ++i) {
      int32_t x, y;
      if (!(s.alive & (1u << i)) || !s.planeAt(i, tick_, x, y)) continue;
      if (x < 0 || y < 0 || x >= map_.widthLeptons() || y >= map_.heightLeptons()) continue;
      for (uint8_t p = 0; p < MAX_PLAYERS; ++p)
        if (team_[p] == team_[s.owner]) vision_.stamp(p, x >> LEPTON_SHIFT, y >> LEPTON_SHIFT, 5, Vision::kVisible | Vision::kExplored);
    }
  }
}

// ---- saving

void Simulation::saveSupport(ByteWriter& w) const {
  w.u32(static_cast<uint32_t>(support_.size()));
  for (const auto& side : support_) {
    w.u32(static_cast<uint32_t>(side.size()));
    for (const SupportState& s : side) {
      w.u8(static_cast<uint8_t>(s.gate));
      w.i32(s.charges);
      w.i32(s.rechargeTicks);
      w.u32(s.readyTick);
      w.u32(s.used);
    }
  }
  w.u32(nextStrikeId_);
  w.u32(static_cast<uint32_t>(strikes_.size()));
  for (const Strike& s : strikes_) {
    w.u32(s.id);
    w.i32(s.power);
    w.u8(static_cast<uint8_t>(s.kind));
    w.u8(static_cast<uint8_t>(s.aircraft));
    w.u8(s.owner);
    for (const Vec2i& v : {s.target, s.from, s.to}) {
      w.i32(v.x);
      w.i32(v.y);
    }
    w.u32(s.startTick);
    w.i32(s.speed);
    w.u8(s.planes);
    w.u8(s.alive);
    w.u8(s.shots);
    w.u8(static_cast<uint8_t>((s.aborted ? 1 : 0) | (s.done ? 2 : 0)));
    w.u32(s.delivered);
    w.i32(s.value);
    auto lu = landingUnits_.find(s.id);
    const uint32_t n = lu == landingUnits_.end() ? 0 : static_cast<uint32_t>(lu->second.size());
    w.u32(n);
    if (n)
      for (const auto& [t, c] : lu->second) {
        w.i32(t);
        w.i32(c);
      }
  }
  w.u32(static_cast<uint32_t>(crates_.size()));
  for (const SupplyCrate& c : crates_) {
    w.i32(c.at.x);
    w.i32(c.at.y);
    w.i32(c.value);
    w.u32(c.landTick);
    w.u32(c.until);
    w.u8(c.from);
  }
  w.u32(static_cast<uint32_t>(downed_.size()));
  for (const DownedPlane& d : downed_) {
    w.i32(d.at.x);
    w.i32(d.at.y);
    w.u32(d.tick);
    w.u8(d.owner);
    w.u8(static_cast<uint8_t>(d.role));
  }
  w.u32(static_cast<uint32_t>(supportReveals_.size()));
  for (const SupportReveal& r : supportReveals_) {
    w.u8(r.owner);
    w.i32(r.at.x);
    w.i32(r.at.y);
    w.i32(r.radius);
    w.u32(r.until);
  }
  w.u32(static_cast<uint32_t>(pendingDrops_.size()));
  for (const PendingDrop& p : pendingDrops_) {
    w.u32(p.tick);
    w.u8(p.owner);
    w.i32(p.at.x);
    w.i32(p.at.y);
    w.i32(p.radius);
    w.i32(p.first);
    w.i32(p.count);
    w.i32(p.power);
    w.u32(p.strike);
  }
}

bool Simulation::loadSupport(ByteReader& r) {
  resetSupport();
  const uint32_t sides = r.u32();
  if (sides > MAX_PLAYERS) return false;
  for (uint32_t p = 0; p < sides; ++p) {
    const uint32_t n = r.u32();
    if (n > 4096) return false;
    for (uint32_t i = 0; i < n; ++i) {
      SupportState s;
      s.gate = static_cast<SupportState::Gate>(std::min<uint8_t>(r.u8(), 2));
      s.charges = r.i32();
      s.rechargeTicks = r.i32();
      s.readyTick = r.u32();
      s.used = r.u32();
      // Call-ins the rules no longer have are dropped; new ones start empty.
      if (i < support_[p].size()) support_[p][i] = s;
    }
  }
  nextStrikeId_ = r.u32();
  const uint32_t ns = r.u32();
  if (ns > 10000) return false;
  for (uint32_t i = 0; i < ns; ++i) {
    Strike s;
    s.id = r.u32();
    s.power = static_cast<int16_t>(r.i32());
    s.kind = static_cast<SupportKind>(std::min<uint8_t>(r.u8(), static_cast<uint8_t>(SupportKind::Count) - 1));
    s.aircraft = static_cast<AircraftRole>(std::min<uint8_t>(r.u8(), static_cast<uint8_t>(AircraftRole::Count) - 1));
    s.owner = r.u8();
    for (Vec2i* v : {&s.target, &s.from, &s.to}) {
      v->x = r.i32();
      v->y = r.i32();
    }
    s.startTick = r.u32();
    s.speed = std::max(1, r.i32());
    s.planes = r.u8();
    s.alive = r.u8();
    s.shots = r.u8();
    const uint8_t flags = r.u8();
    s.aborted = flags & 1;
    s.done = flags & 2;
    s.delivered = r.u32();
    s.value = r.i32();
    const uint32_t nu = r.u32();
    if (nu > 256) return false;
    std::vector<std::pair<int, int>> units;
    for (uint32_t k = 0; k < nu; ++k) {
      const int t = r.i32();
      const int c = r.i32();
      units.push_back({t, c});
    }
    if (!units.empty()) landingUnits_[s.id] = units;
    if (s.power >= static_cast<int>(types_.support().size())) s.power = -1;
    strikes_.push_back(s);
  }
  const uint32_t nc = r.u32();
  if (nc > 10000) return false;
  for (uint32_t i = 0; i < nc; ++i) {
    SupplyCrate c;
    c.at.x = r.i32();
    c.at.y = r.i32();
    c.value = r.i32();
    c.landTick = r.u32();
    c.until = r.u32();
    c.from = r.u8();
    crates_.push_back(c);
  }
  const uint32_t nd = r.u32();
  if (nd > 10000) return false;
  for (uint32_t i = 0; i < nd; ++i) {
    DownedPlane d;
    d.at.x = r.i32();
    d.at.y = r.i32();
    d.tick = r.u32();
    d.owner = r.u8();
    d.role = static_cast<AircraftRole>(std::min<uint8_t>(r.u8(), static_cast<uint8_t>(AircraftRole::Count) - 1));
    downed_.push_back(d);
  }
  const uint32_t nr = r.u32();
  if (nr > 10000) return false;
  for (uint32_t i = 0; i < nr; ++i) {
    SupportReveal v;
    v.owner = r.u8();
    v.at.x = r.i32();
    v.at.y = r.i32();
    v.radius = static_cast<int16_t>(r.i32());
    v.until = r.u32();
    supportReveals_.push_back(v);
  }
  const uint32_t np = r.u32();
  if (np > 10000) return false;
  for (uint32_t i = 0; i < np; ++i) {
    PendingDrop p;
    p.tick = r.u32();
    p.owner = r.u8();
    p.at.x = r.i32();
    p.at.y = r.i32();
    p.radius = r.i32();
    p.first = r.i32();
    p.count = r.i32();
    p.power = r.i32();
    p.strike = r.u32();
    pendingDrops_.push_back(p);
  }
  return r.ok();
}

}  // namespace pioneer

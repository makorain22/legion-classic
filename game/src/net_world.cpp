// Legion Classic - the battle as the lockstep sees it (see net_world.hpp).
#include "net_world.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "pioneer/core/bytes.hpp"
#include "pioneer/sim/savefile.hpp"
#include "pioneer/sim/script.hpp"

using namespace pioneer;

namespace legion {

std::vector<uint8_t> encodeOrder(const Command& c) {
  ByteWriter w;
  writeCommand(w, c);
  return w.bytes();
}

bool decodeOrder(const std::vector<uint8_t>& bytes, Command& out) {
  ByteReader r(bytes);
  out = readCommand(r, true);
  return r.ok() && r.atEnd();
}

bool orderAllowed(CommandType t) {
  switch (t) {
    case CommandType::Move:
    case CommandType::AttackMove:
    case CommandType::Attack:
    case CommandType::Stop:
    case CommandType::SetStance:
    case CommandType::Produce:
    case CommandType::PlaceBuilding:
    case CommandType::PlaceWalls:
    case CommandType::Sell:
    case CommandType::Deploy:
    case CommandType::Harvest:
    case CommandType::SetRally:
    case CommandType::Repair:
    case CommandType::BindGroup:
    case CommandType::UnbindGroup:
    case CommandType::Occupy:
    case CommandType::Unload:
      return true;
    default:  // spawning, editing, credits, teams, the weather...: the editor's and the console's
      return false;
  }
}

Seats readSeats(const std::string& metaJson) {
  Seats out;
  const nlohmann::json j = nlohmann::json::parse(metaJson, nullptr, false);
  if (!j.is_object() || !j.contains("seats") || !j["seats"].is_object()) return out;
  for (const auto& [k, v] : j["seats"].items()) {
    if (!v.is_number_integer()) continue;
    const int m = std::atoi(k.c_str()), side = v.get<int>();
    if (m >= 0 && m < 255 && side >= 0 && side < MAX_PLAYERS) out[static_cast<uint8_t>(m)] = static_cast<uint8_t>(side);
  }
  return out;
}

std::string writeSeats(const std::string& metaJson, const Seats& seats) {
  nlohmann::json j = nlohmann::json::parse(metaJson, nullptr, false);
  if (!j.is_object()) j = nlohmann::json::object();
  nlohmann::json s = nlohmann::json::object();
  for (const auto& [m, side] : seats) s[std::to_string(m)] = side;
  j["seats"] = s;
  return j.dump();
}

bool prepareOrder(Simulation& sim, const Seats& seats, uint8_t member, const std::vector<uint8_t>& bytes, Command& out) {
  auto seat = seats.find(member);
  if (seat == seats.end() || !decodeOrder(bytes, out) || !orderAllowed(out.type)) return false;
  out.player = seat->second;
  // Only the sender's own units take it.
  Registry& reg = sim.registry();
  std::erase_if(out.units, [&](Entity e) {
    return !reg.alive(e) || !reg.has<CUnit>(e) || reg.get<CUnit>(e).owner != out.player;
  });
  const bool needsUnits = out.type != CommandType::Produce && out.type != CommandType::PlaceBuilding &&
                          out.type != CommandType::PlaceWalls;
  return !needsUnits || !out.units.empty();
}

std::vector<std::pair<std::string, uint64_t>> digestOf(Simulation& sim) {
  std::vector<std::pair<std::string, uint64_t>> out;
  const Simulation::StateDigest d = sim.digest();
  for (const auto& s : d.sections) {
    std::string name = std::to_string(s.tag);
    for (const Simulation::SectionInfo& i : Simulation::stateSections())
      if (i.tag == s.tag) name = i.name;
    out.emplace_back(name, s.hash);
  }
  return out;
}

bool NetWorld::loadMapFile(const MapFile& file, std::string& error) {
  file_ = file;
  file_.map.recomputeInfo();
  sim_ = std::make_unique<Simulation>(file_.map, types_, weapons_, 1940);
  for (uint8_t i = 0; i < MAX_PLAYERS && i < file_.players.size(); ++i) {
    Command c;
    c.type = CommandType::SetTeam;
    c.player = i;
    c.count = file_.players[i].team;
    sim_->queue(c);
  }
  constexpr uint32_t kPlacedTag = 0x504C4344;  // "PLCD", as the game
  Command spawns;
  spawns.type = CommandType::SpawnList;
  spawns.tag = kPlacedTag;
  for (const UnitPlacement& u : file_.units) {
    const int t = types_.find(u.type);
    if (t < 0) {
      error = "unknown unit " + u.type;
      return false;
    }
    spawns.spawns.push_back({static_cast<uint16_t>(t), u.owner, {u.x, u.y}, u.facing, u.health});
  }
  sim_->queue(spawns);
  for (size_t i = 0; i < file_.units.size(); ++i) {
    const UnitPlacement& u = file_.units[i];
    if (u.name.empty() && !u.flags && !u.stance) continue;
    Command c;
    c.type = CommandType::TagUnits;
    c.player = NEUTRAL_OWNER;
    c.tag = kPlacedTag;
    c.indexBase = static_cast<uint32_t>(i);
    c.text = u.name;
    c.count = 0xFF;
    c.amount = u.flags;
    c.radius = u.stance ? u.stance + 1 : 0;
    sim_->queue(c);
  }
  for (uint8_t i = 0; i < file_.players.size() && i < MAX_PLAYERS; ++i) {
    if (file_.players[i].active && !file_.players[i].human) {
      Command c;
      c.type = CommandType::SetController;
      c.player = i;
      c.amount = file_.players[i].difficulty;
      sim_->queue(c);
    }
    Command c;
    c.type = CommandType::SetCredits;
    c.player = i;
    c.amount = file_.players[i].credits;
    sim_->queue(c);
  }
  if (!file_.scriptJson.empty()) {
    const nlohmann::json sj = nlohmann::json::parse(file_.scriptJson, nullptr, false);
    if (!sj.is_discarded()) sim_->setScript(scriptFromJson(sj, &types_), false);
  }
  file_.units.clear();  // they are in the battle now
  sim_->tick();
  nlohmann::json meta;
  meta["title"] = file_.map.name;
  meta["map"] = file_.map.name;
  meta["types"] = sim_->typeIds();
  meta_ = writeSeats(meta.dump(), seats_);
  return true;
}

void NetWorld::setSeats(const Seats& s) {
  seats_ = s;
  meta_ = writeSeats(meta_, seats_);
}

void NetWorld::apply(uint8_t member, const std::vector<uint8_t>& order) {
  Command c;
  if (!sim_ || !prepareOrder(*sim_, seats_, member, order, c)) {
    ++dropped_;
    return;
  }
  ++applied_;
  sim_->queue(std::move(c));
}

std::vector<uint8_t> NetWorld::snapshot() const {
  SaveFile f;
  f.metaJson = meta_;
  f.mapBytes = encodeMap(file_.map, {}, file_.players, file_.scriptJson);
  ByteWriter w;
  sim_->saveState(w);
  f.stateBytes = std::move(w.bytes());
  return encodeSaveFile(f, false);
}

bool NetWorld::restore(const std::vector<uint8_t>& snap, std::string& error) {
  SaveFile f;
  if (!decodeSaveFile(snap, f, false, error)) return false;
  MapFile mf;
  if (!decodeMap(f.mapBytes, mf, error)) return false;
  sim_.reset();
  file_ = std::move(mf);
  file_.map.recomputeInfo();
  sim_ = std::make_unique<Simulation>(file_.map, types_, weapons_, 1940);
  if (!file_.scriptJson.empty()) {
    const nlohmann::json sj = nlohmann::json::parse(file_.scriptJson, nullptr, false);
    if (!sj.is_discarded()) sim_->setScript(scriptFromJson(sj, &types_), false);
  }
  ByteReader r(f.stateBytes);
  if (!sim_->loadState(r, error)) return false;
  meta_ = f.metaJson;
  seats_ = readSeats(meta_);
  return true;
}

}  // namespace legion

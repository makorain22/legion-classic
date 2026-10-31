#include "slave/map/map_io.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>
#include <nlohmann/json.hpp>

namespace slave {

namespace {
void putU32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (i * 8)));
}
uint32_t getU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}  // namespace

std::vector<uint8_t> encodeMap(const Map& map, const std::vector<UnitPlacement>& units,
                               const std::vector<PlayerSetup>& players, const std::string& scriptJson) {
  nlohmann::json j;
  j["engine"] = "Slave Engine 1.0";
  j["name"] = map.name;
  j["author"] = map.author;
  j["description"] = map.description;
  j["width"] = map.width();
  j["height"] = map.height();
  j["waterLevel"] = map.waterLevel();
  j["biome"] = biomeId(map.biome);
  auto& props = j["props"] = nlohmann::json::array();
  for (const Prop& p : map.props()) props.push_back({static_cast<int>(p.kind), p.variant, p.x, p.y});
  auto& us = j["units"] = nlohmann::json::array();
  for (const UnitPlacement& u : units)
    us.push_back({{"type", u.type}, {"owner", u.owner}, {"x", u.x}, {"y", u.y}, {"facing", u.facing}, {"health", u.health}});
  auto& ps = j["players"] = nlohmann::json::array();
  for (const PlayerSetup& p : players)
    ps.push_back({{"name", p.name}, {"faction", p.faction}, {"color", p.color}, {"team", p.team},
                  {"active", p.active}, {"human", p.human}, {"startX", p.startX}, {"startY", p.startY},
                  {"credits", p.credits}, {"difficulty", p.difficulty}});
  if (!scriptJson.empty()) {
    nlohmann::json sj = nlohmann::json::parse(scriptJson, nullptr, false);
    if (!sj.is_discarded()) j["script"] = std::move(sj);
  }
  const std::string header = j.dump();

  std::vector<uint8_t> raw;
  raw.reserve(header.size() + map.heights().size() * 2 + map.splatData().size() + 16);
  putU32(raw, static_cast<uint32_t>(header.size()));
  raw.insert(raw.end(), header.begin(), header.end());
  for (uint16_t h : map.heights()) {
    raw.push_back(static_cast<uint8_t>(h & 0xff));
    raw.push_back(static_cast<uint8_t>(h >> 8));
  }
  raw.insert(raw.end(), map.splatData().begin(), map.splatData().end());
  raw.insert(raw.end(), map.oilData().begin(), map.oilData().end());

  mz_ulong compSize = mz_compressBound(static_cast<mz_ulong>(raw.size()));
  std::vector<uint8_t> comp(compSize);
  mz_compress2(comp.data(), &compSize, raw.data(), static_cast<mz_ulong>(raw.size()), MZ_BEST_COMPRESSION);
  comp.resize(compSize);

  std::vector<uint8_t> out = {'L', 'M', 'A', 'P'};
  putU32(out, MAP_FILE_VERSION);
  putU32(out, static_cast<uint32_t>(raw.size()));
  putU32(out, static_cast<uint32_t>(comp.size()));
  out.insert(out.end(), comp.begin(), comp.end());
  return out;
}

bool decodeMap(const std::vector<uint8_t>& bytes, MapFile& out, std::string& error) {
  if (bytes.size() < 16 || std::memcmp(bytes.data(), "LMAP", 4) != 0) {
    error = "not a Legion Classic map file";
    return false;
  }
  const uint32_t version = getU32(&bytes[4]);
  if (version == 0 || version > MAP_FILE_VERSION) {
    error = "map was made by a newer version of the game (format " + std::to_string(version) + ")";
    return false;
  }
  const uint32_t rawSize = getU32(&bytes[8]);
  const uint32_t compSize = getU32(&bytes[12]);
  if (compSize != bytes.size() - 16 || rawSize > 512u * 1024 * 1024) {
    error = "map file is damaged (bad sizes)";
    return false;
  }
  std::vector<uint8_t> raw(rawSize);
  mz_ulong got = rawSize;
  if (mz_uncompress(raw.data(), &got, &bytes[16], compSize) != MZ_OK || got != rawSize) {
    error = "map file is damaged (cannot decompress)";
    return false;
  }

  if (raw.size() < 4) {
    error = "map file is damaged (no header)";
    return false;
  }
  const uint32_t headerLen = getU32(raw.data());
  if (4 + static_cast<size_t>(headerLen) > raw.size()) {
    error = "map file is damaged (header size)";
    return false;
  }
  nlohmann::json j = nlohmann::json::parse(raw.begin() + 4, raw.begin() + 4 + headerLen, nullptr, false);
  if (j.is_discarded()) {
    error = "map file is damaged (header JSON)";
    return false;
  }
  try {
    const int w = j.at("width").get<int>(), h = j.at("height").get<int>();
    if (w < MAP_MIN_SIZE || h < MAP_MIN_SIZE || w > MAP_MAX_SIZE || h > MAP_MAX_SIZE || w % MAP_SIZE_STEP ||
        h % MAP_SIZE_STEP) {
      error = "map has an unsupported size";
      return false;
    }
    Map& m = out.map;
    m.resize(w, h);
    const size_t heightBytes = m.heights().size() * 2;
    const size_t splatBytes = m.splatData().size();
    const size_t oilBytes = version >= 2 ? m.oilData().size() : 0;
    const size_t need = 4 + static_cast<size_t>(headerLen) + heightBytes + splatBytes + oilBytes;
    if (raw.size() != need) {
      error = "map file is damaged (terrain size)";
      return false;
    }
    const uint8_t* p = raw.data() + 4 + headerLen;
    for (size_t i = 0; i < m.heights().size(); ++i) m.heights()[i] = static_cast<uint16_t>(p[i * 2] | (p[i * 2 + 1] << 8));
    std::memcpy(m.splatData().data(), p + heightBytes, splatBytes);
    if (oilBytes) std::memcpy(m.oilData().data(), p + heightBytes + splatBytes, oilBytes);

    m.name = j.value("name", "Untitled");
    m.author = j.value("author", "");
    m.description = j.value("description", "");
    m.setWaterLevel(j.value("waterLevel", uint16_t{0}));
    if (!parseBiome(j.value("biome", std::string("temperate")), m.biome)) m.biome = Biome::Temperate;
    for (const auto& pj : j.value("props", nlohmann::json::array())) {
      const int kind = pj.at(0).get<int>();
      if (kind < 0 || kind >= static_cast<int>(PropKind::Count)) continue;
      m.props().push_back({static_cast<PropKind>(kind), pj.at(1).get<uint8_t>(), pj.at(2).get<int32_t>(),
                           pj.at(3).get<int32_t>()});
    }
    out.units.clear();
    for (const auto& uj : j.value("units", nlohmann::json::array())) {
      UnitPlacement u;
      u.type = uj.at("type").get<std::string>();
      u.owner = uj.value("owner", uint8_t{0});
      u.x = uj.at("x").get<int32_t>();
      u.y = uj.at("y").get<int32_t>();
      u.facing = uj.value("facing", uint16_t{0});
      u.health = uj.value("health", -1);
      out.units.push_back(std::move(u));
    }
    out.players.clear();
    for (const auto& pj : j.value("players", nlohmann::json::array())) {
      if (out.players.size() >= 8) break;
      PlayerSetup p;
      p.name = pj.value("name", "Player " + std::to_string(out.players.size() + 1));
      p.faction = pj.value("faction", "ger");
      p.color = pj.value("color", p.color);
      p.team = static_cast<uint8_t>(pj.value("team", 0) % 8);
      p.active = pj.value("active", false);
      p.human = pj.value("human", true);
      p.startX = pj.value("startX", -1);
      p.startY = pj.value("startY", -1);
      p.credits = pj.value("credits", 5000);
      p.difficulty = static_cast<uint8_t>(std::clamp(pj.value("difficulty", 1), 0, 2));
      out.players.push_back(std::move(p));
    }
    out.scriptJson = j.contains("script") ? j["script"].dump() : std::string();
    m.recomputeInfo();
  } catch (const nlohmann::json::exception& e) {
    error = std::string("map file is damaged: ") + e.what();
    return false;
  }
  return true;
}

bool saveMap(const std::string& path, const Map& map, const std::vector<UnitPlacement>& units, std::string& error) {
  return saveMap(path, map, units, {}, error);
}

bool saveMap(const std::string& path, const Map& map, const std::vector<UnitPlacement>& units,
             const std::vector<PlayerSetup>& players, std::string& error, const std::string& scriptJson) {
  const std::vector<uint8_t> bytes = encodeMap(map, units, players, scriptJson);
  // Write to a temporary file first, so a crash never leaves a half-written map.
  const std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) {
      error = "cannot write " + path;
      return false;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f) {
      error = "disk full or write error: " + path;
      return false;
    }
  }
  std::remove(path.c_str());
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    error = "cannot replace " + path;
    return false;
  }
  return true;
}

bool loadMap(const std::string& path, MapFile& out, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = "cannot open " + path;
    return false;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return decodeMap(bytes, out, error);
}

}  // namespace slave

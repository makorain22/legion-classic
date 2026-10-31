#include "slave/sim/savefile.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

namespace slave {

namespace {
const char* magic(bool replay) { return replay ? "LREP" : "LSAV"; }
void putU32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (i * 8)));
}
uint32_t getU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}  // namespace

std::vector<uint8_t> encodeSaveFile(const SaveFile& f, bool replay) {
  ByteWriter w;
  w.str(f.metaJson);
  w.podVector(f.mapBytes);
  w.podVector(f.stateBytes);
  if (replay) {
    w.u32(static_cast<uint32_t>(f.commands.size()));
    for (const auto& c : f.commands) {
      w.u32(c.tick);
      writeCommand(w, c.cmd);
    }
    w.u32(f.endTick);
  }
  const std::vector<uint8_t>& raw = w.bytes();
  mz_ulong compSize = mz_compressBound(static_cast<mz_ulong>(raw.size()));
  std::vector<uint8_t> comp(compSize);
  mz_compress2(comp.data(), &compSize, raw.data(), static_cast<mz_ulong>(raw.size()), MZ_DEFAULT_COMPRESSION);
  comp.resize(compSize);
  std::vector<uint8_t> out(magic(replay), magic(replay) + 4);
  putU32(out, SAVE_FILE_VERSION);
  putU32(out, static_cast<uint32_t>(raw.size()));
  putU32(out, static_cast<uint32_t>(comp.size()));
  putU32(out, static_cast<uint32_t>(f.metaJson.size()));  // uncompressed copy of the description...
  out.insert(out.end(), f.metaJson.begin(), f.metaJson.end());  // ...so lists can read it cheaply
  out.insert(out.end(), comp.begin(), comp.end());
  return out;
}

bool decodeSaveFile(const std::vector<uint8_t>& bytes, SaveFile& f, bool replay, std::string& error) {
  if (bytes.size() < 20 || std::memcmp(bytes.data(), magic(replay), 4) != 0) {
    error = replay ? "not a Legion Classic replay" : "not a Legion Classic saved game";
    return false;
  }
  if (getU32(&bytes[4]) > SAVE_FILE_VERSION) {
    error = "made by a newer version of the game";
    return false;
  }
  const uint32_t rawSize = getU32(&bytes[8]), compSize = getU32(&bytes[12]), metaSize = getU32(&bytes[16]);
  if (20ull + metaSize + compSize != bytes.size() || rawSize > 1024u * 1024 * 1024) {
    error = "the file is damaged (sizes)";
    return false;
  }
  std::vector<uint8_t> raw(rawSize);
  mz_ulong got = rawSize;
  if (mz_uncompress(raw.data(), &got, &bytes[20 + metaSize], compSize) != MZ_OK || got != rawSize) {
    error = "the file is damaged (can't decompress)";
    return false;
  }
  ByteReader r(raw);
  f.metaJson = r.str();
  f.mapBytes = r.podVector<uint8_t>();
  f.stateBytes = r.podVector<uint8_t>();
  f.commands.clear();
  if (replay) {
    const uint32_t n = r.u32();
    for (uint32_t i = 0; i < n && r.ok(); ++i) {
      Simulation::LoggedCommand c;
      c.tick = r.u32();
      c.cmd = readCommand(r);
      f.commands.push_back(std::move(c));
    }
    f.endTick = r.u32();
  }
  if (!r.ok() || !r.atEnd()) {
    error = "the file is damaged";
    return false;
  }
  return true;
}

bool writeSaveFile(const std::string& path, const SaveFile& f, bool replay, std::string& error) {
  const std::vector<uint8_t> bytes = encodeSaveFile(f, replay);
  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "can't write " + path;
      return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
      error = "disk full or write error";
      return false;
    }
  }
  std::remove(path.c_str());
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    error = "can't replace " + path;
    return false;
  }
  return true;
}

bool readSaveFile(const std::string& path, SaveFile& f, bool replay, std::string& error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "can't open " + path;
    return false;
  }
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return decodeSaveFile(bytes, f, replay, error);
}

bool readSaveMeta(const std::string& path, std::string& metaJson, bool replay) {
  std::ifstream in(path, std::ios::binary);
  uint8_t head[20];
  if (!in.read(reinterpret_cast<char*>(head), 20) || std::memcmp(head, magic(replay), 4) != 0) return false;
  const uint32_t metaSize = getU32(&head[16]);
  if (metaSize > 1u << 20) return false;
  metaJson.resize(metaSize);
  return static_cast<bool>(in.read(metaJson.data(), metaSize));
}

}  // namespace slave

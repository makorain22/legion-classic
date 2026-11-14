#pragma once
// Pioneer Engine - tagged chunks.
//
// A chunked stream is a list of sections, each with a four-letter tag, a
// version and a length:
//
//   u32 count
//   count x { u32 tag, u16 version, u16 flags, u32 size, size bytes }
//
// A reader takes the sections it knows and steps over the rest, so a system
// can be added (a new section) or grow (a higher version) without breaking
// what was written before, and an older game can still read what it
// understands of a newer file. Saved games use it (sim/serialize.cpp); packs
// and network snapshots can.

#include <cstdint>
#include <string>
#include <vector>

#include "pioneer/core/bytes.hpp"

namespace pioneer {

// 'ABCD' as a number that reads the same in a hex dump.
constexpr uint32_t fourcc(const char (&s)[5]) {
  return uint32_t(uint8_t(s[0])) | (uint32_t(uint8_t(s[1])) << 8) | (uint32_t(uint8_t(s[2])) << 16) |
         (uint32_t(uint8_t(s[3])) << 24);
}
inline std::string fourccName(uint32_t tag) {
  std::string s(4, ' ');
  for (int i = 0; i < 4; ++i) {
    const char c = static_cast<char>((tag >> (i * 8)) & 0xFF);
    s[static_cast<size_t>(i)] = (c >= 32 && c < 127) ? c : '?';
  }
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}

namespace chunk {
constexpr uint16_t kRequired = 1;  // a reader that doesn't know this section must refuse the file
}

// Writes sections one after another into a ByteWriter.
class ChunkWriter {
 public:
  explicit ChunkWriter(ByteWriter& out) : out_(out) {
    countAt_ = out_.bytes().size();
    out_.u32(0);
  }
  // Starts a section; write its contents to the same ByteWriter, then end().
  void begin(uint32_t tag, uint16_t version = 1, uint16_t flags = 0) {
    out_.u32(tag);
    out_.u16(version);
    out_.u16(flags);
    sizeAt_ = out_.bytes().size();
    out_.u32(0);
  }
  void end() {
    patch(sizeAt_, static_cast<uint32_t>(out_.bytes().size() - sizeAt_ - 4));
    patch(countAt_, ++count_);
  }
  uint32_t count() const { return count_; }

 private:
  void patch(size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) out_.bytes()[at + static_cast<size_t>(i)] = static_cast<uint8_t>(v >> (i * 8));
  }
  ByteWriter& out_;
  size_t countAt_ = 0, sizeAt_ = 0;
  uint32_t count_ = 0;
};

struct ChunkInfo {
  uint32_t tag = 0;
  uint16_t version = 0;
  uint16_t flags = 0;
  const uint8_t* data = nullptr;
  size_t size = 0;
  ByteReader reader() const { return ByteReader(data, size); }
  std::string name() const { return fourccName(tag); }
};

// Reads the table of a chunked stream. The sections point into the reader's
// buffer, which must outlive them. On return the reader stands after the
// last section.
class ChunkReader {
 public:
  bool parse(ByteReader& r, uint32_t maxChunks = 4096) {
    chunks_.clear();
    const uint32_t n = r.u32();
    if (!r.ok() || n > maxChunks) return false;
    for (uint32_t i = 0; i < n; ++i) {
      ChunkInfo c;
      c.tag = r.u32();
      c.version = r.u16();
      c.flags = r.u16();
      c.size = r.u32();
      c.data = r.cursor();
      if (!r.skip(c.size)) return false;
      chunks_.push_back(c);
    }
    return true;
  }
  const std::vector<ChunkInfo>& chunks() const { return chunks_; }
  // The first section with this tag, or null.
  const ChunkInfo* find(uint32_t tag) const {
    for (const ChunkInfo& c : chunks_)
      if (c.tag == tag) return &c;
    return nullptr;
  }

 private:
  std::vector<ChunkInfo> chunks_;
};

// FNV-1a over a run of bytes: the checksum the engine uses for state.
inline uint64_t hashBytes(const uint8_t* p, size_t n, uint64_t h = 1469598103934665603ULL) {
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 1099511628211ULL;
  }
  return h;
}

}  // namespace pioneer

#pragma once
// Pioneer Engine - zip archives, read and written (a thin layer over miniz).
// Internal to the core: the virtual file system and the pack format use it.

#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pioneer::zip {

struct Entry {
  std::string name;   // as stored, with '/' separators
  uint64_t size = 0;  // uncompressed
  uint32_t crc = 0;
  int64_t mtime = 0;  // seconds since 1970
  bool directory = false;
};

class Reader {
 public:
  Reader();
  ~Reader();
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;

  bool open(const std::string& file, std::string& error);
  void close();
  bool isOpen() const { return open_; }
  size_t count() const;
  bool entry(size_t index, Entry& out) const;
  // Decompresses one entry (checking its CRC).
  bool read(size_t index, std::vector<uint8_t>& out, std::string& error) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool open_ = false;
};

class Writer {
 public:
  Writer();
  ~Writer();
  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;

  bool open(const std::string& file, std::string& error);
  // level 0 stores, 1..9 deflates. mtime 0 = now.
  bool add(const std::string& name, const void* data, size_t size, int level, int64_t mtime, std::string& error);
  // Writes the directory and closes the file. Without it the archive is unreadable.
  bool finish(std::string& error);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool open_ = false;
};

// The files of an archive by clean name ('/' separators, nothing climbing out
// with ".."), each with its index in the archive. If every file sits in one
// top folder that holds a pack.json or mod.json (the way people zip a
// folder), that folder is left off the names and remembered in `strip`.
struct Index {
  std::map<std::string, std::pair<size_t, Entry>> files;
  std::string strip;
};
void index(const Reader& z, Index& out);

uint32_t crc32(const void* data, size_t size);

}  // namespace pioneer::zip

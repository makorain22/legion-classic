#include "zip_file.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include "pioneer/core/vfs.hpp"

namespace pioneer::zip {

namespace {
constexpr uint64_t kHoldInMemory = 96ull * 1024 * 1024;
}

struct Reader::Impl {
  mz_zip_archive zip;
  std::vector<uint8_t> bytes;  // the whole archive, when it was small enough to hold
};

Reader::Reader() : impl_(std::make_unique<Impl>()) { std::memset(&impl_->zip, 0, sizeof(impl_->zip)); }
Reader::~Reader() { close(); }

bool Reader::open(const std::string& file, std::string& error) {
  close();
  std::memset(&impl_->zip, 0, sizeof(impl_->zip));
  impl_->bytes.clear();
  // An ordinary pack is read into memory whole, so no file is held open: the
  // pack can be replaced on disk while the game runs (Windows refuses to
  // overwrite an open file). Very large archives are read from the file.
  std::error_code ec;
  const uint64_t size = std::filesystem::file_size(file, ec);
  bool ok = false;
  if (!ec && size > 0 && size <= kHoldInMemory) {
    std::ifstream in(file, std::ios::binary);
    impl_->bytes.resize(static_cast<size_t>(size));
    if (in && in.read(reinterpret_cast<char*>(impl_->bytes.data()), static_cast<std::streamsize>(size)))
      ok = mz_zip_reader_init_mem(&impl_->zip, impl_->bytes.data(), impl_->bytes.size(), 0) != 0;
    else
      impl_->bytes.clear();
  } else if (!ec) {
    ok = mz_zip_reader_init_file(&impl_->zip, file.c_str(), 0) != 0;
  }
  if (!ok) {
    error = file + ": not a readable zip archive";
    if (!ec && size > 0) error += std::string(" (") + mz_zip_get_error_string(mz_zip_get_last_error(&impl_->zip)) + ")";
    impl_->bytes.clear();
    return false;
  }
  open_ = true;
  return true;
}

void Reader::close() {
  if (open_) mz_zip_reader_end(&impl_->zip);
  open_ = false;
  impl_->bytes.clear();
  impl_->bytes.shrink_to_fit();
}

size_t Reader::count() const { return open_ ? mz_zip_reader_get_num_files(&impl_->zip) : 0; }

bool Reader::entry(size_t index, Entry& out) const {
  if (!open_) return false;
  mz_zip_archive_file_stat st;
  if (!mz_zip_reader_file_stat(&impl_->zip, static_cast<mz_uint>(index), &st)) return false;
  out.name = st.m_filename;
  for (char& c : out.name)
    if (c == '\\') c = '/';
  out.size = st.m_uncomp_size;
  out.crc = st.m_crc32;
#ifndef MINIZ_NO_TIME
  out.mtime = static_cast<int64_t>(st.m_time);
#else
  out.mtime = 0;
#endif
  out.directory = mz_zip_reader_is_file_a_directory(&impl_->zip, static_cast<mz_uint>(index)) != 0;
  return true;
}

bool Reader::read(size_t index, std::vector<uint8_t>& out, std::string& error) const {
  if (!open_) {
    error = "the archive is not open";
    return false;
  }
  size_t size = 0;
  void* p = mz_zip_reader_extract_to_heap(&impl_->zip, static_cast<mz_uint>(index), &size, 0);
  if (!p) {
    // An empty file extracts to nothing; that is not an error.
    mz_zip_archive_file_stat st;
    if (mz_zip_reader_file_stat(&impl_->zip, static_cast<mz_uint>(index), &st) && st.m_uncomp_size == 0) {
      out.clear();
      return true;
    }
    error = std::string("can't unpack an entry (") + mz_zip_get_error_string(mz_zip_get_last_error(&impl_->zip)) + ")";
    return false;
  }
  out.assign(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + size);
  mz_free(p);
  return true;
}

struct Writer::Impl {
  mz_zip_archive zip;
};

Writer::Writer() : impl_(std::make_unique<Impl>()) { std::memset(&impl_->zip, 0, sizeof(impl_->zip)); }
Writer::~Writer() {
  if (open_) mz_zip_writer_end(&impl_->zip);
}

bool Writer::open(const std::string& file, std::string& error) {
  std::memset(&impl_->zip, 0, sizeof(impl_->zip));
  if (!mz_zip_writer_init_file(&impl_->zip, file.c_str(), 0)) {
    error = "can't write " + file;
    return false;
  }
  open_ = true;
  return true;
}

bool Writer::add(const std::string& name, const void* data, size_t size, int level, int64_t mtime,
                 std::string& error) {
  if (!open_) {
    error = "the archive is not open";
    return false;
  }
  if (level < 0) level = 0;
  if (level > 9) level = 9;
  MZ_TIME_T when = mtime > 0 ? static_cast<MZ_TIME_T>(mtime) : time(nullptr);
  static const char kNothing = 0;
  if (!mz_zip_writer_add_mem_ex_v2(&impl_->zip, name.c_str(), size ? data : &kNothing, size, nullptr, 0,
                                   static_cast<mz_uint>(level), 0, 0, &when, nullptr, 0, nullptr, 0)) {
    error = "can't add " + name + " (" + mz_zip_get_error_string(mz_zip_get_last_error(&impl_->zip)) + ")";
    return false;
  }
  return true;
}

bool Writer::finish(std::string& error) {
  if (!open_) {
    error = "the archive is not open";
    return false;
  }
  const bool ok = mz_zip_writer_finalize_archive(&impl_->zip) != 0;
  if (!ok) error = "can't finish the archive";
  mz_zip_writer_end(&impl_->zip);
  open_ = false;
  return ok;
}

void index(const Reader& z, Index& out) {
  out.files.clear();
  out.strip.clear();
  std::set<std::string> tops;
  bool rootFiles = false;
  const size_t n = z.count();
  for (size_t i = 0; i < n; ++i) {
    Entry e;
    if (!z.entry(i, e) || e.directory) continue;
    std::string name;
    if (!vfs::normalise(e.name, name) || name.empty()) continue;  // "../evil" is simply not there
    const size_t slash = name.find('/');
    if (slash == std::string::npos) rootFiles = true;
    else tops.insert(name.substr(0, slash));
    e.name = name;
    out.files[name] = {i, e};
  }
  if (rootFiles || tops.size() != 1) return;
  const std::string top = *tops.begin() + "/";
  if (!out.files.count(top + "pack.json") && !out.files.count(top + "mod.json")) return;
  std::map<std::string, std::pair<size_t, Entry>> stripped;
  for (auto& [name, f] : out.files) {
    f.second.name = name.substr(top.size());
    stripped[f.second.name] = f;
  }
  out.files.swap(stripped);
  out.strip = top;
}

uint32_t crc32(const void* data, size_t size) {
  return static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, static_cast<const unsigned char*>(data), size));
}

}  // namespace pioneer::zip

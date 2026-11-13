#pragma once
// Pioneer Engine - little binary writer and reader for save games, replays and
// network messages. Values are written little-endian; the reader never reads
// past the end (it sets a failure flag instead), so damaged files can't crash
// the game.

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace pioneer {

class ByteWriter {
 public:
  std::vector<uint8_t>& bytes() { return buf_; }
  const std::vector<uint8_t>& bytes() const { return buf_; }

  void raw(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    buf_.insert(buf_.end(), b, b + n);
  }
  template <class T>
  void pod(const T& v) {
    static_assert(std::is_trivially_copyable_v<T>, "pod() needs a trivially copyable type");
    raw(&v, sizeof(T));
  }
  void u8(uint8_t v) { pod(v); }
  void u16(uint16_t v) { pod(v); }
  void u32(uint32_t v) { pod(v); }
  void u64(uint64_t v) { pod(v); }
  void i32(int32_t v) { pod(v); }
  void str(const std::string& s) {
    u32(static_cast<uint32_t>(s.size()));
    raw(s.data(), s.size());
  }
  template <class T>
  void podVector(const std::vector<T>& v) {
    static_assert(std::is_trivially_copyable_v<T>, "podVector() needs a trivially copyable type");
    u32(static_cast<uint32_t>(v.size()));
    if (!v.empty()) raw(v.data(), v.size() * sizeof(T));
  }
  void boolVector(const std::vector<bool>& v) {
    u32(static_cast<uint32_t>(v.size()));
    for (bool b : v) u8(b ? 1 : 0);
  }

 private:
  std::vector<uint8_t> buf_;
};

class ByteReader {
 public:
  ByteReader(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}
  explicit ByteReader(const std::vector<uint8_t>& v) : ByteReader(v.data(), v.size()) {}

  bool ok() const { return ok_; }
  bool atEnd() const { return p_ == end_; }
  size_t remaining() const { return static_cast<size_t>(end_ - p_); }
  void fail() { ok_ = false; }

  bool raw(void* out, size_t n) {
    if (!ok_ || remaining() < n) {
      ok_ = false;
      std::memset(out, 0, n);
      return false;
    }
    std::memcpy(out, p_, n);
    p_ += n;
    return true;
  }
  template <class T>
  T pod() {
    static_assert(std::is_trivially_copyable_v<T>, "pod() needs a trivially copyable type");
    T v{};
    raw(&v, sizeof(T));
    return v;
  }
  uint8_t u8() { return pod<uint8_t>(); }
  uint16_t u16() { return pod<uint16_t>(); }
  uint32_t u32() { return pod<uint32_t>(); }
  uint64_t u64() { return pod<uint64_t>(); }
  int32_t i32() { return pod<int32_t>(); }
  std::string str() {
    const uint32_t n = u32();
    if (!ok_ || n > remaining()) {
      ok_ = false;
      return {};
    }
    std::string s(reinterpret_cast<const char*>(p_), n);
    p_ += n;
    return s;
  }
  template <class T>
  std::vector<T> podVector(size_t maxCount = 1u << 28) {
    const uint32_t n = u32();
    std::vector<T> v;
    if (!ok_ || n > maxCount || static_cast<size_t>(n) * sizeof(T) > remaining()) {
      if (n) ok_ = false;
      return v;
    }
    v.resize(n);
    if (n) raw(v.data(), n * sizeof(T));
    return v;
  }
  std::vector<bool> boolVector() {
    const uint32_t n = u32();
    std::vector<bool> v;
    if (!ok_ || n > remaining()) {
      if (n) ok_ = false;
      return v;
    }
    v.resize(n);
    for (uint32_t i = 0; i < n; ++i) v[i] = u8() != 0;
    return v;
  }

 private:
  const uint8_t* p_;
  const uint8_t* end_;
  bool ok_ = true;
};

}  // namespace pioneer

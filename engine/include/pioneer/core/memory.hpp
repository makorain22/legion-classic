#pragma once
// Pioneer Engine - memory tracking: how many bytes each system holds.
//
// Three sources, shown together by mem::report():
//
//   * Tagged allocations. mem::Allocator<T, Tag> is a drop-in std allocator
//     that counts what it hands out (use it for a system's big containers);
//     mem::add() counts memory obtained some other way (GPU buffers, files).
//   * Reporters. A system that keeps its data in ordinary containers
//     registers a function that says how big it is right now. Asked only
//     when a report is made, so it costs nothing in play.
//   * The process: resident and peak memory as the operating system sees it.
//
// Counting is lock-free (atomics). Like the profiler, this only measures and
// never feeds back into the simulation.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <new>
#include <string>
#include <vector>

namespace pioneer::mem {

enum class Tag : uint8_t {
  General, Sim, Ecs, Path, Vision, Map, Script, Ai,
  Render, Mesh, Texture, Audio, Assets, Content, UI, Jobs, Net, Tools,
  Count
};
constexpr int kTagCount = static_cast<int>(Tag::Count);
const char* tagName(Tag t);

struct TagStats {
  int64_t bytes = 0;    // held now
  int64_t peak = 0;     // most ever held
  uint64_t allocs = 0;  // times memory was taken
  uint64_t frees = 0;
};

// bytes > 0: memory taken; bytes < 0: given back.
void add(Tag tag, int64_t bytes);
TagStats stats(Tag tag);
void resetPeaks();

// A std-compatible allocator that counts under `tag`:
//   std::vector<Node, mem::Allocator<Node, mem::Tag::Path>> nodes;
template <class T, Tag tag>
struct Allocator {
  using value_type = T;
  template <class U>
  struct rebind {
    using other = Allocator<U, tag>;
  };
  Allocator() = default;
  template <class U>
  Allocator(const Allocator<U, tag>&) {}
  T* allocate(size_t n) {
    add(tag, static_cast<int64_t>(n * sizeof(T)));
    return static_cast<T*>(::operator new(n * sizeof(T), std::align_val_t(alignof(T))));
  }
  void deallocate(T* p, size_t n) {
    add(tag, -static_cast<int64_t>(n * sizeof(T)));
    ::operator delete(p, std::align_val_t(alignof(T)));
  }
  template <class U>
  bool operator==(const Allocator<U, tag>&) const { return true; }
  template <class U>
  bool operator!=(const Allocator<U, tag>&) const { return false; }
};
template <class T, Tag tag>
using Vector = std::vector<T, Allocator<T, tag>>;

// Reporters: `fn` returns the bytes the named thing holds right now.
using Reporter = std::function<size_t()>;
int addReporter(Tag tag, std::string name, Reporter fn);
void removeReporter(int id);
// Removes its reporter when it goes out of scope (a member of the system).
class ScopedReporter {
 public:
  ScopedReporter() = default;
  ScopedReporter(Tag tag, std::string name, Reporter fn) : id_(addReporter(tag, std::move(name), std::move(fn))) {}
  ~ScopedReporter() { reset(); }
  ScopedReporter(const ScopedReporter&) = delete;
  ScopedReporter& operator=(const ScopedReporter&) = delete;
  ScopedReporter(ScopedReporter&& o) noexcept : id_(o.id_) { o.id_ = 0; }
  ScopedReporter& operator=(ScopedReporter&& o) noexcept {
    if (this != &o) {
      reset();
      id_ = o.id_;
      o.id_ = 0;
    }
    return *this;
  }
  void reset() {
    if (id_) removeReporter(id_);
    id_ = 0;
  }

 private:
  int id_ = 0;
};

struct ReportLine {
  Tag tag = Tag::General;
  std::string name;   // empty for the tag's own counted allocations
  size_t bytes = 0;
  size_t peak = 0;    // tagged allocations only
};
// Every tag with something counted, then every reporter, grouped by tag.
std::vector<ReportLine> report();
// Bytes per tag: counted allocations plus that tag's reporters.
std::vector<size_t> totals();

struct ProcessMemory {
  size_t resident = 0;      // physical memory in use
  size_t peakResident = 0;
  size_t virtualBytes = 0;  // address space reserved
};
// Zeroes where the platform doesn't tell.
ProcessMemory process();

// "1.5 MB", "312 KB", "88 B".
std::string formatBytes(size_t bytes);

// Size helpers for reporters.
template <class V, class A>
size_t bytesOf(const std::vector<V, A>& v) {
  return v.capacity() * sizeof(V);
}
inline size_t bytesOf(const std::string& s) { return s.capacity(); }

}  // namespace pioneer::mem

#include "pioneer/core/memory.hpp"

#include <algorithm>
#include <cstdio>
#include <mutex>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace pioneer::mem {

namespace {

struct Counter {
  std::atomic<int64_t> bytes{0}, peak{0};
  std::atomic<uint64_t> allocs{0}, frees{0};
};

struct ReporterEntry {
  int id;
  Tag tag;
  std::string name;
  Reporter fn;
};

struct Global {
  Counter counters[kTagCount];
  std::mutex m;
  std::vector<ReporterEntry> reporters;
  int nextId = 1;
};

Global& g() {
  static Global* instance = new Global;  // never destroyed: allocators may run during static destruction
  return *instance;
}

}  // namespace

const char* tagName(Tag t) {
  static const char* const kNames[kTagCount] = {"General", "Simulation", "ECS",    "Pathfinding", "Vision", "Map",
                                                "Script",  "AI",         "Render", "Meshes",      "Textures", "Audio",
                                                "Assets",  "Content",    "UI",     "Jobs",        "Network",  "Tools"};
  const int i = static_cast<int>(t);
  return i >= 0 && i < kTagCount ? kNames[i] : "?";
}

void add(Tag tag, int64_t bytes) {
  Counter& c = g().counters[static_cast<int>(tag)];
  if (bytes >= 0) {
    c.allocs.fetch_add(1, std::memory_order_relaxed);
    const int64_t now = c.bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    int64_t peak = c.peak.load(std::memory_order_relaxed);
    while (now > peak && !c.peak.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {
    }
  } else {
    c.frees.fetch_add(1, std::memory_order_relaxed);
    c.bytes.fetch_add(bytes, std::memory_order_relaxed);
  }
}

TagStats stats(Tag tag) {
  const Counter& c = g().counters[static_cast<int>(tag)];
  return {c.bytes.load(), c.peak.load(), c.allocs.load(), c.frees.load()};
}

void resetPeaks() {
  for (Counter& c : g().counters) c.peak.store(c.bytes.load());
}

int addReporter(Tag tag, std::string name, Reporter fn) {
  Global& G = g();
  std::lock_guard<std::mutex> lk(G.m);
  G.reporters.push_back({G.nextId, tag, std::move(name), std::move(fn)});
  return G.nextId++;
}

void removeReporter(int id) {
  Global& G = g();
  std::lock_guard<std::mutex> lk(G.m);
  G.reporters.erase(std::remove_if(G.reporters.begin(), G.reporters.end(),
                                   [id](const ReporterEntry& r) { return r.id == id; }),
                    G.reporters.end());
}

std::vector<ReportLine> report() {
  Global& G = g();
  std::vector<ReportLine> out;
  std::lock_guard<std::mutex> lk(G.m);
  for (int t = 0; t < kTagCount; ++t) {
    const Counter& c = G.counters[t];
    if (c.allocs.load() > 0)
      out.push_back({static_cast<Tag>(t), {}, static_cast<size_t>(std::max<int64_t>(0, c.bytes.load())),
                     static_cast<size_t>(std::max<int64_t>(0, c.peak.load()))});
    for (const ReporterEntry& r : G.reporters)
      if (static_cast<int>(r.tag) == t) out.push_back({r.tag, r.name, r.fn ? r.fn() : 0, 0});
  }
  return out;
}

std::vector<size_t> totals() {
  std::vector<size_t> out(static_cast<size_t>(kTagCount), 0);
  for (const ReportLine& l : report()) out[static_cast<size_t>(l.tag)] += l.bytes;
  return out;
}

ProcessMemory process() {
  ProcessMemory p;
#if defined(_WIN32)
  PROCESS_MEMORY_COUNTERS pmc;
  if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) {
    p.resident = pmc.WorkingSetSize;
    p.peakResident = pmc.PeakWorkingSetSize;
    p.virtualBytes = pmc.PagefileUsage;
  }
#elif defined(__APPLE__)
  mach_task_basic_info info;
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
    p.resident = info.resident_size;
    p.peakResident = info.resident_size_max;
    p.virtualBytes = info.virtual_size;
  }
#else
  if (std::FILE* f = std::fopen("/proc/self/statm", "r")) {
    unsigned long pages = 0, rss = 0;
    if (std::fscanf(f, "%lu %lu", &pages, &rss) == 2) {
      const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
      p.virtualBytes = pages * page;
      p.resident = rss * page;
    }
    std::fclose(f);
  }
  rusage ru{};
  if (getrusage(RUSAGE_SELF, &ru) == 0) p.peakResident = static_cast<size_t>(ru.ru_maxrss) * 1024;
#endif
  return p;
}

std::string formatBytes(size_t bytes) {
  char buf[32];
  const double b = static_cast<double>(bytes);
  if (bytes >= (1ull << 30)) std::snprintf(buf, sizeof buf, "%.2f GB", b / (1ull << 30));
  else if (bytes >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.1f MB", b / (1ull << 20));
  else if (bytes >= (1ull << 10)) std::snprintf(buf, sizeof buf, "%.0f KB", b / (1ull << 10));
  else std::snprintf(buf, sizeof buf, "%zu B", bytes);
  return buf;
}

}  // namespace pioneer::mem

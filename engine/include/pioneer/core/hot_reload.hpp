#pragma once
// Pioneer Engine - hot reload: files changed on disk reach the running game.
//
// Systems say what they care about ("rules/" + ".json", "audio/", "shaders/")
// and get called, on the main thread, with the files that changed:
//
//   hot.watch("rules", "rules/", ".json", [&](const std::vector<vfs::Change>& c) { reloadRules(); });
//
// update() is called every frame. Now and then it has the virtual file system
// look at the mounted folders - on a worker thread, so a frame never waits for
// the disk - and when the look is done it compares it with the one before. A
// changed file is held back until one further look finds it unchanged: editors
// and exporters write files in several steps, and nobody wants half a file.
//
// The asset registry, if given, is told first, so versions and "who uses
// this" are up to date when the handlers run.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "pioneer/core/jobs.hpp"
#include "pioneer/core/vfs.hpp"

namespace pioneer {

namespace assets {
class Registry;
}

class HotReload {
 public:
  using Handler = std::function<void(const std::vector<vfs::Change>& changes)>;

  struct Stats {
    uint64_t scans = 0;        // looks at the disk so far
    uint64_t changes = 0;      // changed files handed to handlers
    double lastScanMs = 0;     // how long the last look took (on its worker)
    size_t filesWatched = 0;
    size_t waiting = 0;        // changes held back until they settle
  };
  struct WatchInfo {
    int id = 0;
    std::string name, prefix, ext;
    uint64_t calls = 0;
  };

  // prefix: "" = everything, else a folder like "rules/". ext: "" = any, else
  // ".json" (case ignored). Returns a handle for unwatch().
  int watch(std::string name, std::string prefix, std::string ext, Handler fn);
  void unwatch(int handle);
  std::vector<WatchInfo> watches() const;

  void setEnabled(bool on) { enabled_ = on; }
  bool enabled() const { return enabled_; }
  // Seconds between looks at the disk (at least 0.1).
  void setInterval(double seconds) { interval_ = seconds < 0.1 ? 0.1 : seconds; }
  double interval() const { return interval_; }

  // Once a frame, on the main thread. Returns how many changes were handed out.
  size_t update(const std::shared_ptr<vfs::Vfs>& fs, double dtSeconds, assets::Registry* registry = nullptr);
  // Looks now and hands out everything at once, settled or not (the console's
  // "content.check", tests). Returns how many changes were handed out.
  size_t checkNow(const std::shared_ptr<vfs::Vfs>& fs, assets::Registry* registry = nullptr);

  const Stats& stats() const { return stats_; }
  // The changes handed out last, newest batch only (for the console and the log).
  const std::vector<vfs::Change>& lastChanges() const { return last_; }

 private:
  struct Watch {
    int id = 0;
    std::string name, prefix, ext;
    Handler fn;
    uint64_t calls = 0;
  };
  size_t dispatch(const std::vector<vfs::Change>& changes, assets::Registry* registry);
  void takeIn(const std::shared_ptr<vfs::Vfs>& fs, const vfs::Snapshot& snap, bool settle,
              std::vector<vfs::Change>& ready);

  std::vector<Watch> watches_;
  int nextWatch_ = 1;
  bool enabled_ = true;
  double interval_ = 1.0;
  double sinceScan_ = 0;
  // The look in flight.
  std::shared_ptr<vfs::Snapshot> pendingSnap_;
  std::shared_ptr<vfs::Vfs> pendingFs_;
  jobs::JobHandle pendingJob_;
  // Changes seen once, waiting to be seen unchanged.
  std::vector<vfs::Change> waiting_;
  const vfs::Vfs* waitingFs_ = nullptr;
  std::vector<vfs::Change> last_;
  Stats stats_;
};

}  // namespace pioneer

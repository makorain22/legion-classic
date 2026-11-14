// Pioneer Engine - hot reload (see hot_reload.hpp).
#include "pioneer/core/hot_reload.hpp"

#include <algorithm>
#include <cctype>

#include "pioneer/core/assets.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/core/profiler.hpp"

namespace pioneer {

namespace {
bool matches(const std::string& path, const std::string& prefix, const std::string& ext) {
  if (!prefix.empty() && path.compare(0, prefix.size(), prefix) != 0) return false;
  if (ext.empty()) return true;
  if (path.size() < ext.size()) return false;
  for (size_t i = 0; i < ext.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(path[path.size() - ext.size() + i])) !=
        std::tolower(static_cast<unsigned char>(ext[i])))
      return false;
  return true;
}
}  // namespace

int HotReload::watch(std::string name, std::string prefix, std::string ext, Handler fn) {
  Watch w;
  w.id = nextWatch_++;
  w.name = std::move(name);
  w.prefix = std::move(prefix);
  w.ext = std::move(ext);
  w.fn = std::move(fn);
  watches_.push_back(std::move(w));
  return watches_.back().id;
}

void HotReload::unwatch(int handle) {
  watches_.erase(std::remove_if(watches_.begin(), watches_.end(), [&](const Watch& w) { return w.id == handle; }),
                 watches_.end());
}

std::vector<HotReload::WatchInfo> HotReload::watches() const {
  std::vector<WatchInfo> out;
  for (const Watch& w : watches_) out.push_back({w.id, w.name, w.prefix, w.ext, w.calls});
  return out;
}

size_t HotReload::dispatch(const std::vector<vfs::Change>& changes, assets::Registry* registry) {
  if (changes.empty()) return 0;
  PNR_PROFILE("Hot reload: tell the systems");
  last_ = changes;
  stats_.changes += changes.size();
  for (const vfs::Change& c : changes)
    PNR_INFO_CH(Content, "%s on disk: %s", vfs::changeName(c.kind), c.path.c_str());
  if (registry) registry->apply(changes);
  // Handlers may add or remove watches: go by handle, over a copy of the list.
  std::vector<int> ids;
  for (const Watch& w : watches_) ids.push_back(w.id);
  for (int id : ids) {
    auto it = std::find_if(watches_.begin(), watches_.end(), [&](const Watch& w) { return w.id == id; });
    if (it == watches_.end() || !it->fn) continue;
    std::vector<vfs::Change> mine;
    for (const vfs::Change& c : changes)
      if (matches(c.path, it->prefix, it->ext)) mine.push_back(c);
    if (mine.empty()) continue;
    ++it->calls;
    const Handler fn = it->fn;  // the handler may unwatch itself
    fn(mine);
  }
  return changes.size();
}

// Compares a finished look with the one before and sorts the result into
// "ready to hand out" and "seen once, wait for the next look".
void HotReload::takeIn(const std::shared_ptr<vfs::Vfs>& fs, const vfs::Snapshot& snap, bool settle,
                       std::vector<vfs::Change>& ready) {
  ++stats_.scans;
  stats_.lastScanMs = snap.ms;
  stats_.filesWatched = snap.fileCount;
  if (waitingFs_ != fs.get()) waiting_.clear();  // another file system: what waited no longer applies
  waitingFs_ = fs.get();
  const std::vector<vfs::Change> fresh = fs->apply(snap);
  if (!settle) {
    ready = waiting_;
    waiting_.clear();
    for (const vfs::Change& c : fresh) {
      auto same = std::find_if(ready.begin(), ready.end(), [&](const vfs::Change& o) {
        return o.path == c.path && o.mount == c.mount;
      });
      if (same != ready.end()) same->kind = c.kind;
      else ready.push_back(c);
    }
    stats_.waiting = 0;
    return;
  }
  // What waited and did not change again has settled.
  std::vector<vfs::Change> still;
  for (const vfs::Change& w : waiting_) {
    const bool again = std::any_of(fresh.begin(), fresh.end(), [&](const vfs::Change& c) {
      return c.path == w.path && c.mount == w.mount;
    });
    if (!again) ready.push_back(w);
  }
  for (const vfs::Change& c : fresh) {
    // Added then modified while waiting is still "added" to whoever hears of it.
    vfs::Change keep = c;
    for (const vfs::Change& w : waiting_)
      if (w.path == c.path && w.mount == c.mount && w.kind == vfs::ChangeKind::Added &&
          c.kind == vfs::ChangeKind::Modified)
        keep.kind = vfs::ChangeKind::Added;
    still.push_back(std::move(keep));
  }
  waiting_.swap(still);
  stats_.waiting = waiting_.size();
}

size_t HotReload::update(const std::shared_ptr<vfs::Vfs>& fs, double dtSeconds, assets::Registry* registry) {
  if (!fs) return 0;
  std::vector<vfs::Change> ready;
  // A look that has come back?
  if (pendingSnap_ && pendingJob_.done()) {
    const std::shared_ptr<vfs::Snapshot> snap = std::move(pendingSnap_);
    const std::shared_ptr<vfs::Vfs> scanned = std::move(pendingFs_);
    pendingSnap_.reset();
    pendingFs_.reset();
    pendingJob_ = jobs::JobHandle();
    if (enabled_ && scanned == fs) takeIn(fs, *snap, true, ready);
  }
  if (!enabled_) {
    sinceScan_ = 0;
    return 0;
  }
  sinceScan_ += dtSeconds;
  if (!pendingSnap_ && sinceScan_ >= interval_) {
    sinceScan_ = 0;
    pendingSnap_ = std::make_shared<vfs::Snapshot>();
    pendingFs_ = fs;
    const std::shared_ptr<vfs::Snapshot> out = pendingSnap_;
    const std::shared_ptr<vfs::Vfs> keep = fs;
    pendingJob_ = jobs::system().submit([out, keep](int) { *out = keep->scan(); }, {}, "Hot reload: look at the disk");
  }
  return dispatch(ready, registry);
}

size_t HotReload::checkNow(const std::shared_ptr<vfs::Vfs>& fs, assets::Registry* registry) {
  if (!fs) return 0;
  if (pendingSnap_) {
    jobs::system().wait(pendingJob_);
    pendingSnap_.reset();
    pendingFs_.reset();
    pendingJob_ = jobs::JobHandle();
  }
  std::vector<vfs::Change> ready;
  takeIn(fs, fs->scan(), false, ready);
  sinceScan_ = 0;
  return dispatch(ready, registry);
}

}  // namespace pioneer

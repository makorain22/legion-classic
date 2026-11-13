#pragma once
// Pioneer Engine - the job system: every core put to work, the result the same
// as with one.
//
// A JobSystem owns worker threads, each with its own queue. A thread pushes
// work to its own queue and takes the newest job first (it is the one still
// warm in the cache); a thread with nothing to do steals the oldest job from
// another. The thread that started the system is worker 0 and works too
// whenever it waits, so nothing ever deadlocks waiting for itself.
//
// DETERMINISM. The simulation must give the same answer on every machine, so
// jobs never decide *what* is computed, only *when*:
//
//   * parallelFor cuts [0, count) into chunks whose borders depend on `count`
//     and `grain` alone, never on the number of threads.
//   * parallelMap / parallelChunks hand back results indexed by item or by
//     chunk; the caller merges them in index order.
//   * A job writes only to its own chunk's output. Shared state is read-only
//     while jobs run.
//
// Follow those three rules and a run with 1 thread and a run with 32 produce
// the same bytes (tests/test_core.cpp plays a battle both ways and compares
// the state hash).
//
// With one thread (old hardware, `--threads 1`, or PIONEER_THREADS=1) no
// worker is started and every job runs inline, at once, on the caller.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pioneer::jobs {

namespace detail {
struct Job;
}

// A job that was submitted. Copyable; keeps the job's record alive.
class JobHandle {
 public:
  JobHandle() = default;
  bool valid() const { return job_ != nullptr; }
  bool done() const;

 private:
  friend class JobSystem;
  explicit JobHandle(std::shared_ptr<detail::Job> j) : job_(std::move(j)) {}
  std::shared_ptr<detail::Job> job_;
};

// `worker` is the index of the thread running the job: 0 .. threadCount()-1.
// Use it to pick per-thread scratch space.
using JobFn = std::function<void(int worker)>;
// One chunk of a parallelFor: items [begin, end), chunk number `chunk`.
using RangeFn = std::function<void(size_t begin, size_t end, size_t chunk, int worker)>;

struct JobStats {
  int threads = 1;
  uint64_t executed = 0;     // jobs run since start
  uint64_t stolen = 0;       // of those, taken from another thread's queue
  uint64_t inlined = 0;      // run at once by submit (single-thread mode)
  uint32_t queued = 0;       // waiting right now
  uint32_t peakQueued = 0;
  std::vector<uint64_t> perThread;  // jobs run by each worker
};

class JobSystem {
 public:
  JobSystem() = default;
  ~JobSystem();
  JobSystem(const JobSystem&) = delete;
  JobSystem& operator=(const JobSystem&) = delete;

  // Starts `threads` workers in all, counting the calling thread (which
  // becomes worker 0). 0 = one per logical core (at most 32). 1 = no threads
  // at all: jobs run inline. Restarting with another count is allowed while
  // no job is running.
  void start(int threads = 0);
  void stop();
  bool running() const { return started_; }
  int threadCount() const { return threads_; }
  // Worker index of the calling thread, or -1 if it isn't one of ours.
  int currentWorker() const;

  // Queues `fn`. It starts once every job in `after` has finished.
  JobHandle submit(JobFn fn, const std::vector<JobHandle>& after = {}, const char* name = nullptr);
  // Blocks until the job is done, running other jobs meanwhile. Rethrows what
  // the job threw.
  void wait(const JobHandle& h);
  void wait(const std::vector<JobHandle>& hs);

  // Runs fn over [0, count) in chunks of `grain` items (the last may be
  // shorter) and returns when all are done. Chunk borders depend on count and
  // grain only. grain 0 = one item per chunk.
  void parallelFor(size_t count, size_t grain, const RangeFn& fn, const char* name = nullptr);

  JobStats stats() const;
  void resetStats();

 private:
  struct Impl;
  void enqueue(std::shared_ptr<detail::Job> j);
  bool runOne(int worker);
  void execute(const std::shared_ptr<detail::Job>& j, int worker);
  void workerLoop(int worker);

  Impl* impl_ = nullptr;
  int threads_ = 1;
  bool started_ = false;
};

// The engine's shared job system. Starts in single-thread mode until init().
JobSystem& system();
// Starts the shared system. threads: 0 = automatic; the environment variable
// PIONEER_THREADS overrides it. Returns the number of threads in use.
int init(int threads = 0);
void shutdown();
// How many chunks parallelFor makes for these arguments.
inline size_t chunkCount(size_t count, size_t grain) {
  if (grain == 0) grain = 1;
  return (count + grain - 1) / grain;
}

// out[i] = fn(i) for every i, computed on all cores, returned in index order.
template <class T, class Fn>
std::vector<T> parallelMap(JobSystem& js, size_t count, size_t grain, Fn&& fn) {
  std::vector<T> out(count);
  js.parallelFor(count, grain, [&](size_t b, size_t e, size_t, int) {
    for (size_t i = b; i < e; ++i) out[i] = fn(i);
  });
  return out;
}

// One T per chunk: fn(begin, end, T& out). Merge the result front to back and
// the outcome does not depend on the number of threads.
template <class T, class Fn>
std::vector<T> parallelChunks(JobSystem& js, size_t count, size_t grain, Fn&& fn) {
  std::vector<T> out(chunkCount(count, grain));
  js.parallelFor(count, grain, [&](size_t b, size_t e, size_t chunk, int) { fn(b, e, out[chunk]); });
  return out;
}

// A set of named tasks with dependencies, run as jobs. Tasks with no path
// between them run side by side; a task starts after everything it depends
// on. For frame phases ("vision" after "movement", "audio" beside both).
class TaskGraph {
 public:
  // Returns the task's id. `after`: ids of tasks that must finish first.
  int add(std::string name, JobFn fn, std::vector<int> after = {});
  int find(const std::string& name) const;
  size_t size() const { return tasks_.size(); }
  // False (and nothing run) if the dependencies form a cycle or name a
  // missing task; `error` says which.
  bool run(JobSystem& js, std::string* error = nullptr);
  // An order that respects the dependencies (ids), for display and tests.
  bool order(std::vector<int>& out, std::string* error = nullptr) const;
  const std::string& name(int id) const { return tasks_[static_cast<size_t>(id)].name; }

 private:
  struct Task {
    std::string name;
    JobFn fn;
    std::vector<int> after;
  };
  std::vector<Task> tasks_;
};

}  // namespace pioneer::jobs

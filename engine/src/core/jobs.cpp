#include "pioneer/core/jobs.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <thread>

#include "pioneer/core/profiler.hpp"

namespace pioneer::jobs {

namespace detail {
struct Job {
  JobFn fn;
  const char* name = nullptr;
  std::atomic<int> waiting{1};  // unfinished dependencies, +1 while submit() is still linking them
  std::atomic<bool> done{false};
  std::mutex m;  // guards `dependents` and the flip of `done`
  std::vector<std::shared_ptr<Job>> dependents;
  std::exception_ptr error;
};
}  // namespace detail

bool JobHandle::done() const { return !job_ || job_->done.load(std::memory_order_acquire); }

struct JobSystem::Impl {
  struct Queue {
    std::mutex m;
    std::deque<std::shared_ptr<detail::Job>> q;
  };
  std::vector<std::unique_ptr<Queue>> queues;  // one per worker
  std::vector<std::thread> threads;
  std::mutex sleepM;
  std::condition_variable sleepCv;
  std::mutex doneM;
  std::condition_variable doneCv;
  std::atomic<bool> stopping{false};
  std::atomic<uint32_t> queued{0}, peak{0}, nextQueue{0};
  std::atomic<uint64_t> executed{0}, stolen{0}, inlined{0};
  std::unique_ptr<std::atomic<uint64_t>[]> perThread;
};

namespace {
thread_local const JobSystem* tlsSystem = nullptr;
thread_local int tlsWorker = -1;
}  // namespace

JobSystem::~JobSystem() { stop(); }

int JobSystem::currentWorker() const { return tlsSystem == this ? tlsWorker : -1; }

void JobSystem::start(int threads) {
  stop();
  int n = threads;
  if (n <= 0) n = static_cast<int>(std::thread::hardware_concurrency());
  n = std::clamp(n, 1, 32);
  impl_ = new Impl;
  impl_->perThread = std::make_unique<std::atomic<uint64_t>[]>(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    impl_->queues.push_back(std::make_unique<Impl::Queue>());
    impl_->perThread[static_cast<size_t>(i)] = 0;
  }
  threads_ = n;
  started_ = true;
  tlsSystem = this;
  tlsWorker = 0;
  if (prof::threadNames().empty()) prof::setThreadName("Main");  // the starter is thread 0 in captures
  for (int i = 1; i < n; ++i) impl_->threads.emplace_back([this, i] { workerLoop(i); });
}

void JobSystem::stop() {
  if (!impl_) return;
  impl_->stopping = true;
  {
    std::lock_guard<std::mutex> lk(impl_->sleepM);
  }
  impl_->sleepCv.notify_all();
  for (std::thread& t : impl_->threads) t.join();
  delete impl_;
  impl_ = nullptr;
  threads_ = 1;
  started_ = false;
  if (tlsSystem == this) {
    tlsSystem = nullptr;
    tlsWorker = -1;
  }
}

void JobSystem::workerLoop(int worker) {
  tlsSystem = this;
  tlsWorker = worker;
  char name[32];
  std::snprintf(name, sizeof name, "Worker %d", worker);
  prof::setThreadName(name);
  for (;;) {
    if (runOne(worker)) continue;
    std::unique_lock<std::mutex> lk(impl_->sleepM);
    impl_->sleepCv.wait(lk, [this] { return impl_->stopping.load() || impl_->queued.load() > 0; });
    if (impl_->stopping.load()) break;
  }
}

void JobSystem::enqueue(std::shared_ptr<detail::Job> j) {
  if (!impl_ || threads_ <= 1) {
    if (impl_) impl_->inlined.fetch_add(1, std::memory_order_relaxed);
    execute(j, std::max(0, currentWorker()));
    return;
  }
  int w = currentWorker();
  if (w < 0) w = static_cast<int>(impl_->nextQueue.fetch_add(1) % static_cast<uint32_t>(threads_));
  {
    Impl::Queue& q = *impl_->queues[static_cast<size_t>(w)];
    std::lock_guard<std::mutex> lk(q.m);
    q.q.push_back(std::move(j));
  }
  const uint32_t now = impl_->queued.fetch_add(1) + 1;
  uint32_t peak = impl_->peak.load(std::memory_order_relaxed);
  while (now > peak && !impl_->peak.compare_exchange_weak(peak, now)) {
  }
  {
    std::lock_guard<std::mutex> lk(impl_->sleepM);
  }
  impl_->sleepCv.notify_one();
}

bool JobSystem::runOne(int worker) {
  std::shared_ptr<detail::Job> j;
  const size_t n = impl_->queues.size();
  {
    Impl::Queue& own = *impl_->queues[static_cast<size_t>(worker)];
    std::lock_guard<std::mutex> lk(own.m);
    if (!own.q.empty()) {
      j = std::move(own.q.back());  // newest first: still warm
      own.q.pop_back();
    }
  }
  for (size_t k = 1; !j && k < n; ++k) {
    Impl::Queue& victim = *impl_->queues[(static_cast<size_t>(worker) + k) % n];
    std::lock_guard<std::mutex> lk(victim.m);
    if (!victim.q.empty()) {
      j = std::move(victim.q.front());  // oldest first: the least likely to be wanted back soon
      victim.q.pop_front();
      impl_->stolen.fetch_add(1, std::memory_order_relaxed);
    }
  }
  if (!j) return false;
  impl_->queued.fetch_sub(1);
  execute(j, worker);
  return true;
}

void JobSystem::execute(const std::shared_ptr<detail::Job>& j, int worker) {
  {
    prof::Scope scope(j->name ? j->name : "Job");
    try {
      j->fn(worker);
    } catch (...) {
      j->error = std::current_exception();
    }
  }
  j->fn = nullptr;  // let go of what the job captured
  std::vector<std::shared_ptr<detail::Job>> next;
  {
    std::lock_guard<std::mutex> lk(j->m);
    j->done.store(true, std::memory_order_release);
    next.swap(j->dependents);
  }
  if (impl_) {
    impl_->executed.fetch_add(1, std::memory_order_relaxed);
    impl_->perThread[static_cast<size_t>(worker)].fetch_add(1, std::memory_order_relaxed);
  }
  for (std::shared_ptr<detail::Job>& d : next)
    if (d->waiting.fetch_sub(1) == 1) enqueue(std::move(d));
  if (impl_) {
    {
      std::lock_guard<std::mutex> lk(impl_->doneM);
    }
    impl_->doneCv.notify_all();
  }
}

JobHandle JobSystem::submit(JobFn fn, const std::vector<JobHandle>& after, const char* name) {
  auto j = std::make_shared<detail::Job>();
  j->fn = std::move(fn);
  j->name = name;
  for (const JobHandle& d : after) {
    if (!d.job_) continue;
    std::lock_guard<std::mutex> lk(d.job_->m);
    if (!d.job_->done.load(std::memory_order_acquire)) {
      d.job_->dependents.push_back(j);
      j->waiting.fetch_add(1);
    }
  }
  JobHandle h(j);
  if (j->waiting.fetch_sub(1) == 1) enqueue(std::move(j));
  return h;
}

void JobSystem::wait(const JobHandle& h) {
  if (!h.job_) return;
  detail::Job& j = *h.job_;
  const int w = currentWorker();
  while (!j.done.load(std::memory_order_acquire)) {
    if (!impl_) break;  // inline mode: the job ran inside submit()
    if (w >= 0 && runOne(w)) continue;
    std::unique_lock<std::mutex> lk(impl_->doneM);
    impl_->doneCv.wait_for(lk, std::chrono::microseconds(200),
                           [&j] { return j.done.load(std::memory_order_acquire); });
  }
  if (j.error) std::rethrow_exception(j.error);
}

void JobSystem::wait(const std::vector<JobHandle>& hs) {
  std::exception_ptr first;
  for (const JobHandle& h : hs) {
    try {
      wait(h);
    } catch (...) {
      if (!first) first = std::current_exception();
    }
  }
  if (first) std::rethrow_exception(first);
}

void JobSystem::parallelFor(size_t count, size_t grain, const RangeFn& fn, const char* name) {
  if (count == 0) return;
  if (grain == 0) grain = 1;
  const size_t chunks = chunkCount(count, grain);
  const int me = currentWorker();
  // One chunk, one thread, or a caller that isn't one of our workers: do it here.
  // A chunk that throws doesn't stop the others, here or below: what was
  // done is the same whichever way the work was shared out.
  if (chunks == 1 || threads_ <= 1 || !impl_ || me < 0) {
    std::exception_ptr error;
    for (size_t c = 0; c < chunks; ++c) {
      try {
        fn(c * grain, std::min(count, c * grain + grain), c, std::max(0, me));
      } catch (...) {
        if (!error) error = std::current_exception();
      }
    }
    if (error) std::rethrow_exception(error);
    return;
  }
  struct Shared {
    std::atomic<size_t> next{0};
    std::mutex m;
    std::exception_ptr error;
  } sh;
  auto body = [&sh, &fn, chunks, grain, count](int worker) {
    for (;;) {
      const size_t c = sh.next.fetch_add(1);
      if (c >= chunks) return;
      try {
        fn(c * grain, std::min(count, c * grain + grain), c, worker);
      } catch (...) {
        std::lock_guard<std::mutex> lk(sh.m);
        if (!sh.error) sh.error = std::current_exception();
      }
    }
  };
  const size_t helpers = std::min(chunks, static_cast<size_t>(threads_)) - 1;
  std::vector<JobHandle> hs;
  hs.reserve(helpers);
  for (size_t i = 0; i < helpers; ++i) hs.push_back(submit(body, {}, name ? name : "parallelFor"));
  body(me);
  wait(hs);  // `sh` and `fn` must outlive every helper
  if (sh.error) std::rethrow_exception(sh.error);
}

JobStats JobSystem::stats() const {
  JobStats s;
  s.threads = threads_;
  if (!impl_) return s;
  s.executed = impl_->executed.load();
  s.stolen = impl_->stolen.load();
  s.inlined = impl_->inlined.load();
  s.queued = impl_->queued.load();
  s.peakQueued = impl_->peak.load();
  for (int i = 0; i < threads_; ++i) s.perThread.push_back(impl_->perThread[static_cast<size_t>(i)].load());
  return s;
}

void JobSystem::resetStats() {
  if (!impl_) return;
  impl_->executed = 0;
  impl_->stolen = 0;
  impl_->inlined = 0;
  impl_->peak = 0;
  for (int i = 0; i < threads_; ++i) impl_->perThread[static_cast<size_t>(i)] = 0;
}

JobSystem& system() {
  static JobSystem s;
  return s;
}

int init(int threads) {
  if (const char* env = std::getenv("PIONEER_THREADS")) {
    const int v = std::atoi(env);
    if (v > 0) threads = v;
  }
  system().start(threads);
  return system().threadCount();
}

void shutdown() { system().stop(); }

// --- TaskGraph ------------------------------------------------------------------

int TaskGraph::add(std::string name, JobFn fn, std::vector<int> after) {
  tasks_.push_back({std::move(name), std::move(fn), std::move(after)});
  return static_cast<int>(tasks_.size()) - 1;
}

int TaskGraph::find(const std::string& name) const {
  for (size_t i = 0; i < tasks_.size(); ++i)
    if (tasks_[i].name == name) return static_cast<int>(i);
  return -1;
}

bool TaskGraph::order(std::vector<int>& out, std::string* error) const {
  out.clear();
  const int n = static_cast<int>(tasks_.size());
  std::vector<int> pending(static_cast<size_t>(n), 0);
  std::vector<std::vector<int>> users(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    for (int d : tasks_[static_cast<size_t>(i)].after) {
      if (d < 0 || d >= n) {
        if (error) *error = "task '" + tasks_[static_cast<size_t>(i)].name + "' depends on a task that does not exist";
        return false;
      }
      ++pending[static_cast<size_t>(i)];
      users[static_cast<size_t>(d)].push_back(i);
    }
  }
  std::vector<int> ready;
  for (int i = 0; i < n; ++i)
    if (pending[static_cast<size_t>(i)] == 0) ready.push_back(i);
  for (size_t k = 0; k < ready.size(); ++k) {  // in id order within each wave, so the order is stable
    const int t = ready[k];
    out.push_back(t);
    for (int u : users[static_cast<size_t>(t)])
      if (--pending[static_cast<size_t>(u)] == 0) ready.push_back(u);
  }
  if (static_cast<int>(out.size()) != n) {
    if (error) {
      *error = "tasks depend on each other in a circle:";
      for (int i = 0; i < n; ++i)
        if (pending[static_cast<size_t>(i)] > 0) *error += " " + tasks_[static_cast<size_t>(i)].name;
    }
    out.clear();
    return false;
  }
  return true;
}

bool TaskGraph::run(JobSystem& js, std::string* error) {
  std::vector<int> ord;
  if (!order(ord, error)) return false;
  std::vector<JobHandle> handles(tasks_.size());
  for (int t : ord) {
    const Task& task = tasks_[static_cast<size_t>(t)];
    std::vector<JobHandle> after;
    for (int d : task.after) after.push_back(handles[static_cast<size_t>(d)]);
    handles[static_cast<size_t>(t)] = js.submit(task.fn, after, nullptr);
  }
  js.wait(handles);
  return true;
}

}  // namespace pioneer::jobs

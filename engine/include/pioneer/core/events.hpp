#pragma once
// Pioneer Engine - the event bus.
//
// Systems that should not know each other talk through events: one publishes
// a value of some type, whoever subscribed to that type is told. Delivery
// order is fixed (priority, then order of subscribing), so the bus can be
// used where the order matters.
//
//   struct ContentReloaded { std::string path; };
//   auto sub = bus.subscribe<ContentReloaded>([](const ContentReloaded& e) { ... });
//   bus.publish(ContentReloaded{"rules/units.json"});   // now
//   bus.post(ContentReloaded{"audio/audio.json"});      // at the next dispatch()
//
// A Subscription ends when it goes out of scope. Handlers may subscribe,
// unsubscribe, publish and post while events are being delivered.
// Not thread-safe: post from other threads through the job system's main-thread queue.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pioneer {

class EventBus {
 public:
  using Handle = uint64_t;

  // Ends its subscription when destroyed.
  class Subscription {
   public:
    Subscription() = default;
    Subscription(EventBus* bus, Handle h) : bus_(bus), handle_(h) {}
    Subscription(Subscription&& o) noexcept : bus_(o.bus_), handle_(o.handle_) { o.bus_ = nullptr; }
    Subscription& operator=(Subscription&& o) noexcept {
      if (this != &o) {
        reset();
        bus_ = o.bus_;
        handle_ = o.handle_;
        o.bus_ = nullptr;
      }
      return *this;
    }
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    ~Subscription() { reset(); }
    void reset() {
      if (bus_) bus_->unsubscribe(handle_);
      bus_ = nullptr;
    }
    bool active() const { return bus_ != nullptr; }

   private:
    EventBus* bus_ = nullptr;
    Handle handle_ = 0;
  };

  // Higher priority hears first; equal priorities in the order they subscribed.
  template <class E, class Fn>
  [[nodiscard]] Subscription subscribe(Fn&& fn, int priority = 0) {
    return Subscription(this, listen<E>(std::forward<Fn>(fn), priority));
  }
  // The same without the guard: stays until unsubscribe(handle).
  template <class E, class Fn>
  Handle listen(Fn&& fn, int priority = 0) {
    auto& list = channels_[std::type_index(typeid(E))];
    Listener l;
    l.handle = ++next_;
    l.priority = priority;
    l.call = [f = std::function<void(const E&)>(std::forward<Fn>(fn))](const void* e) { f(*static_cast<const E*>(e)); };
    auto at = std::find_if(list.begin(), list.end(), [&](const Listener& o) { return o.priority < priority; });
    list.insert(at, std::move(l));
    return next_;
  }
  void unsubscribe(Handle h) {
    for (auto& [type, list] : channels_)
      for (Listener& l : list)
        if (l.handle == h) l.dead = true;  // taken out when nobody is walking the list
    if (depth_ == 0) sweep();
  }

  // Tells every subscriber of E now. Returns how many heard.
  template <class E>
  size_t publish(const E& event) {
    auto it = channels_.find(std::type_index(typeid(E)));
    if (it == channels_.end()) return 0;
    size_t heard = 0;
    ++depth_;
    // By index, and only those there when it began: handlers may subscribe meanwhile.
    const size_t n = it->second.size();
    for (size_t i = 0; i < n && i < it->second.size(); ++i) {
      if (it->second[i].dead) continue;
      const auto call = it->second[i].call;  // a copy: the list may grow under us
      call(&event);
      ++heard;
    }
    if (--depth_ == 0) sweep();
    return heard;
  }
  // Keeps the event for the next dispatch().
  template <class E>
  void post(E event) {
    queue_.push_back([this, e = std::move(event)]() { publish(e); });
  }
  // Delivers what was posted, in the order posted (events posted meanwhile too). Returns how many.
  size_t dispatch() {
    size_t n = 0;
    while (!queue_.empty()) {
      std::vector<std::function<void()>> batch;
      batch.swap(queue_);
      for (auto& f : batch) {
        f();
        ++n;
      }
    }
    return n;
  }
  size_t pending() const { return queue_.size(); }
  template <class E>
  size_t subscribers() const {
    auto it = channels_.find(std::type_index(typeid(E)));
    if (it == channels_.end()) return 0;
    return static_cast<size_t>(std::count_if(it->second.begin(), it->second.end(), [](const Listener& l) { return !l.dead; }));
  }

 private:
  struct Listener {
    Handle handle = 0;
    int priority = 0;
    bool dead = false;
    std::function<void(const void*)> call;
  };
  void sweep() {
    for (auto& [type, list] : channels_) std::erase_if(list, [](const Listener& l) { return l.dead; });
  }
  std::unordered_map<std::type_index, std::vector<Listener>> channels_;
  std::vector<std::function<void()>> queue_;
  Handle next_ = 0;
  int depth_ = 0;
};

}  // namespace pioneer

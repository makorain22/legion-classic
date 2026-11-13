#pragma once
// Pioneer Engine - entity/component storage.
//
// Entities are ids with a generation counter, so a stale handle to a deleted
// unit is detected instead of silently pointing at a new one. Components live
// in dense arrays (one "pool" per component type), which keeps iteration over
// thousands of units fast and cache-friendly. Iteration order depends only on
// the order of adds/removes, so it is deterministic.

#include <cassert>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

namespace pioneer {

struct Entity {
  uint32_t index = UINT32_MAX;
  uint32_t generation = 0;
  constexpr bool valid() const { return index != UINT32_MAX; }
  constexpr bool operator==(const Entity&) const = default;
};
constexpr Entity kNullEntity{};

namespace detail {
inline uint32_t nextPoolId() {
  static uint32_t counter = 0;
  return counter++;
}
template <class T>
uint32_t poolId() {
  static const uint32_t id = nextPoolId();
  return id;
}

struct PoolBase {
  virtual ~PoolBase() = default;
  virtual void remove(uint32_t entityIndex) = 0;
  virtual bool has(uint32_t entityIndex) const = 0;
  virtual void clear() = 0;
};
}  // namespace detail

// Sparse set: sparse[entityIndex] -> position in dense arrays.
template <class T>
class Pool final : public detail::PoolBase {
 public:
  static constexpr uint32_t kNone = UINT32_MAX;

  T& add(uint32_t e, T value = {}) {
    if (e >= sparse_.size()) sparse_.resize(static_cast<size_t>(e) + 1, kNone);
    if (sparse_[e] != kNone) {
      data_[sparse_[e]] = std::move(value);
      return data_[sparse_[e]];
    }
    sparse_[e] = static_cast<uint32_t>(dense_.size());
    dense_.push_back(e);
    data_.push_back(std::move(value));
    return data_.back();
  }

  void remove(uint32_t e) override {
    if (!has(e)) return;
    const uint32_t pos = sparse_[e];
    const uint32_t last = static_cast<uint32_t>(dense_.size() - 1);
    if (pos != last) {
      dense_[pos] = dense_[last];
      data_[pos] = std::move(data_[last]);
      sparse_[dense_[pos]] = pos;
    }
    dense_.pop_back();
    data_.pop_back();
    sparse_[e] = kNone;
  }

  bool has(uint32_t e) const override { return e < sparse_.size() && sparse_[e] != kNone; }
  void clear() override {
    sparse_.clear();
    dense_.clear();
    data_.clear();
  }

  // Restores a pool exactly as saved: same entities in the same order, so
  // iteration (and therefore the simulation) continues identically.
  void restore(std::vector<uint32_t> dense, std::vector<T> data) {
    sparse_.clear();
    dense_ = std::move(dense);
    data_ = std::move(data);
    for (size_t i = 0; i < dense_.size(); ++i) {
      if (dense_[i] >= sparse_.size()) sparse_.resize(static_cast<size_t>(dense_[i]) + 1, kNone);
      sparse_[dense_[i]] = static_cast<uint32_t>(i);
    }
  }

  T& get(uint32_t e) { return data_[sparse_[e]]; }
  const T& get(uint32_t e) const { return data_[sparse_[e]]; }
  T* tryGet(uint32_t e) { return has(e) ? &data_[sparse_[e]] : nullptr; }

  size_t size() const { return dense_.size(); }
  const std::vector<uint32_t>& entities() const { return dense_; }
  std::vector<T>& data() { return data_; }

 private:
  std::vector<uint32_t> sparse_;
  std::vector<uint32_t> dense_;
  std::vector<T> data_;
};

class Registry {
 public:
  Entity create() {
    uint32_t index;
    if (!freeList_.empty()) {
      index = freeList_.back();
      freeList_.pop_back();
    } else {
      index = static_cast<uint32_t>(generations_.size());
      generations_.push_back(0);
      used_.push_back(0);
    }
    used_[index] = 1;
    ++alive_;
    return {index, generations_[index]};
  }

  void destroy(Entity e) {
    if (!alive(e)) return;
    for (auto& p : pools_)
      if (p) p->remove(e.index);
    ++generations_[e.index];
    used_[e.index] = 0;
    freeList_.push_back(e.index);
    --alive_;
  }

  bool alive(Entity e) const {
    return e.index < generations_.size() && used_[e.index] && generations_[e.index] == e.generation;
  }
  Entity handle(uint32_t index) const { return {index, generations_[index]}; }

  // Raw entity bookkeeping, for save games.
  struct State {
    std::vector<uint32_t> generations;
    std::vector<uint8_t> used;
    std::vector<uint32_t> freeList;
    uint64_t alive = 0;
  };
  State state() const { return {generations_, used_, freeList_, alive_}; }
  void restoreState(State s) {
    generations_ = std::move(s.generations);
    used_ = std::move(s.used);
    freeList_ = std::move(s.freeList);
    alive_ = static_cast<size_t>(s.alive);
  }
  size_t aliveCount() const { return alive_; }
  size_t capacity() const { return generations_.size(); }  // highest entity index + 1

  void clear() {
    for (auto& p : pools_)
      if (p) p->clear();
    generations_.clear();
    used_.clear();
    freeList_.clear();
    alive_ = 0;
  }

  template <class T>
  Pool<T>& pool() {
    const uint32_t id = detail::poolId<T>();
    if (id >= pools_.size()) pools_.resize(static_cast<size_t>(id) + 1);
    if (!pools_[id]) pools_[id] = std::make_unique<Pool<T>>();
    return static_cast<Pool<T>&>(*pools_[id]);
  }

  template <class T>
  T& add(Entity e, T value = {}) { return pool<T>().add(e.index, std::move(value)); }
  template <class T>
  void remove(Entity e) { pool<T>().remove(e.index); }
  template <class T>
  bool has(Entity e) { return pool<T>().has(e.index); }
  template <class T>
  T& get(Entity e) { return pool<T>().get(e.index); }
  template <class T>
  T* tryGet(Entity e) { return alive(e) ? pool<T>().tryGet(e.index) : nullptr; }

  // Calls fn(entityIndex, A&, B&...) for every entity having all components.
  // Iterates the first component's pool; put the rarest component first.
  template <class A, class... Rest, class Fn>
  void each(Fn&& fn) {
    auto& first = pool<A>();
    [[maybe_unused]] std::tuple<Pool<Rest>&...> others(pool<Rest>()...);
    auto& ents = first.entities();
    auto& data = first.data();
    for (size_t i = 0; i < ents.size(); ++i) {
      const uint32_t e = ents[i];
      if ((std::get<Pool<Rest>&>(others).has(e) && ...))
        fn(e, data[i], std::get<Pool<Rest>&>(others).get(e)...);
    }
  }

 private:
  std::vector<std::unique_ptr<detail::PoolBase>> pools_;
  std::vector<uint32_t> generations_;
  std::vector<uint8_t> used_;
  std::vector<uint32_t> freeList_;
  size_t alive_ = 0;
};

}  // namespace pioneer

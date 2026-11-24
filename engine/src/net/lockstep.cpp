// Pioneer Engine - deterministic lockstep over a session (see lockstep.hpp).
#include "pioneer/net/lockstep.hpp"

#include <algorithm>
#include <cmath>

#include "pioneer/core/bytes.hpp"
#include "pioneer/core/log.hpp"

namespace pioneer::net {

namespace {
enum Msg : uint8_t {
  kOrder = kFirstAppMessage,  // player -> host: an order
  kTurn,                      // host -> players: tick, orders
  kSnapshot,                  // host -> player: why, tick, epoch, pause, the battle
  kHash,                      // player -> host: epoch, tick of the sample, hash, current tick
  kProgress,                  // player -> host (unreliable): epoch, current tick
  kPause,                     // player -> host: on; host -> players: on, by
  kDesync,                    // host -> players: member, tick, systems
  kUpload,                    // leader -> dedicated host: the battle to start with
  kDigestAsk,                 // host -> player: epoch, tick
  kDigest,                    // player -> host: epoch, tick, entries
  kWaiting,                   // host -> players: members everyone waits for
  kEnd,                       // host -> players: the game is over
};
constexpr int kProgressEveryMs = 300;
constexpr int kDigestTimeoutMs = 6000;
constexpr int kStarvedMs = 500;
}  // namespace

const char* phaseName(LockstepPhase p) {
  switch (p) {
    case LockstepPhase::Idle: return "idle";
    case LockstepPhase::Loading: return "loading";
    case LockstepPhase::Running: return "running";
    case LockstepPhase::Waiting: return "waiting";
    case LockstepPhase::Paused: return "paused";
    case LockstepPhase::Ended: return "ended";
  }
  return "?";
}

Lockstep::Lockstep(Session& session, LockstepAdapter& adapter, LockstepConfig cfg)
    : session_(session), adapter_(adapter), cfg_(cfg) {
  delay_ = std::max(1, cfg_.minDelay);
  listener_ = session_.addListener([this](const SessionEvent& e) { onSession(e); });
  session_.setHandler(kOrder, [this](uint8_t from, const std::vector<uint8_t>& d) {
    if (session_.isHost()) hostOrder(from, d);
  });
  session_.setHandler(kTurn, [this](uint8_t, const std::vector<uint8_t>& d) { onTurn(d); });
  session_.setHandler(kSnapshot, [this](uint8_t, const std::vector<uint8_t>& d) { onSnapshot(d); });
  session_.setHandler(kHash, [this](uint8_t from, const std::vector<uint8_t>& d) { onHash(from, d); });
  session_.setHandler(kProgress, [this](uint8_t from, const std::vector<uint8_t>& d) {
    if (!session_.isHost()) return;
    auto it = players_.find(from);
    ByteReader r(d);
    const uint16_t epoch = r.u16();
    const uint32_t t = r.u32();
    if (it == players_.end() || !r.ok() || epoch != it->second.epoch) return;
    it->second.tick = std::max(it->second.tick, t);
    if (it->second.syncing && t >= it->second.syncTick) it->second.syncing = false;
  });
  session_.setHandler(kPause, [this](uint8_t from, const std::vector<uint8_t>& d) {
    ByteReader r(d);
    const bool on = r.u8() != 0;
    if (session_.isHost()) {
      if (!started_ || ended_ || (!cfg_.anyonePauses && from != session_.leader()) || on == paused_) return;
      paused_ = on;
      pausedBy_ = from;
      session_.broadcast(kPause, {static_cast<uint8_t>(on ? 1 : 0), from});
      PNR_INFO_CH(Net, "%s by member %u", on ? "Paused" : "Going on", from);
    } else {
      const uint8_t by = r.u8();
      if (r.ok()) {
        paused_ = on;
        pausedBy_ = by;
      }
    }
  });
  session_.setHandler(kDesync, [this](uint8_t, const std::vector<uint8_t>& d) {
    ByteReader r(d);
    const uint8_t m = r.u8();
    const uint32_t t = r.u32();
    const std::string sys = r.str();
    if (!r.ok()) return;
    if (m == session_.localId()) {
      ++stats_.desyncs;
      stats_.lastDesyncTick = t;
      stats_.lastDesyncSystems = sys;
    }
    if (onDesync) onDesync(m, t, sys);
  });
  session_.setHandler(kUpload, [this](uint8_t from, const std::vector<uint8_t>& d) { onUpload(from, d); });
  session_.setHandler(kDigestAsk, [this](uint8_t, const std::vector<uint8_t>& d) {
    ByteReader r(d);
    const uint16_t epoch = r.u16();
    const uint32_t t = r.u32();
    if (r.ok() && epoch == epoch_) digestAsk_ = t;
  });
  session_.setHandler(kDigest, [this](uint8_t from, const std::vector<uint8_t>& d) { onDigest(from, d); });
  session_.setHandler(kWaiting, [this](uint8_t, const std::vector<uint8_t>& d) {
    if (session_.isHost()) return;
    waitingFor_.assign(d.begin(), d.end());
  });
  session_.setHandler(kEnd, [this](uint8_t, const std::vector<uint8_t>&) {
    if (!session_.isHost()) ended_ = true;
  });
}

Lockstep::~Lockstep() {
  session_.removeListener(listener_);
  for (uint8_t t = kOrder; t <= kEnd; ++t) session_.setHandler(t, nullptr);
}

LockstepPhase Lockstep::phase() const {
  if (ended_) return LockstepPhase::Ended;
  if (loading_) return LockstepPhase::Loading;
  if (!started_) return LockstepPhase::Idle;
  if (paused_) return LockstepPhase::Paused;
  if (!waitingFor_.empty()) return LockstepPhase::Waiting;
  if (!session_.isHost() && starvedSince_ && now_ - starvedSince_ > static_cast<uint64_t>(kStarvedMs))
    return LockstepPhase::Waiting;
  return LockstepPhase::Running;
}

std::vector<uint8_t> Lockstep::turnBytes(uint32_t t, const Turn& turn) const {
  ByteWriter w;
  w.u32(t);
  w.u16(static_cast<uint16_t>(turn.orders.size()));
  for (const Order& o : turn.orders) {
    w.u8(o.member);
    w.u32(static_cast<uint32_t>(o.bytes.size()));
    w.raw(o.bytes.data(), o.bytes.size());
  }
  return w.bytes();
}

std::vector<uint8_t> Lockstep::playersInGame() const {
  std::vector<uint8_t> out;
  for (const Member& m : session_.members())
    if (m.connected && !m.local) out.push_back(m.id);
  return out;
}

// --- Starting and ending ----------------------------------------------------------

bool Lockstep::start(std::string& error) {
  if (started_ && !ended_) {
    error = "The game is already running";
    return false;
  }
  if (!session_.active()) {
    error = "Not in a session";
    return false;
  }
  if (!session_.isHost()) {
    if (!session_.isLeader()) {
      error = "Only the leader starts the game";
      return false;
    }
    // A dedicated server: it takes our battle and sends it to everyone (us too).
    session_.sendToHost(kUpload, adapter_.snapshot());
    loading_ = true;
    return true;
  }
  started_ = true;
  ended_ = false;
  loading_ = false;
  paused_ = false;
  turns_.clear();
  open_ = {};
  hashes_.clear();
  pendingHashes_.clear();
  digests_.clear();
  players_.clear();
  waitingFor_.clear();
  lastTurn_ = adapter_.tick();
  clock_ = 0;
  lastAdvance_ = now_ = session_.now();
  session_.setInGame(true);
  for (uint8_t m : playersInGame()) sendSnapshot(m, 0);
  if (adapter_.tick() % static_cast<uint32_t>(cfg_.hashEvery) == 0) hashes_[adapter_.tick()] = adapter_.hash();
  PNR_INFO_CH(Net, "Game started at tick %u for %zu players", lastTurn_, players_.size());
  return true;
}

void Lockstep::end() {
  if (!session_.isHost() || !started_ || ended_) return;
  ended_ = true;
  session_.broadcast(kEnd, {});
}

void Lockstep::onUpload(uint8_t from, const std::vector<uint8_t>& d) {
  if (!session_.isHost() || (started_ && !ended_) || from != session_.leader()) return;
  std::string err;
  if (!adapter_.restore(d, err)) {
    PNR_WARN_CH(Net, "The leader's battle can't be loaded: %s", err.c_str());
    return;
  }
  if (onRestored) onRestored(0);
  start(err);
}

void Lockstep::sendSnapshot(uint8_t member, int why) {
  PlayerInfo& p = players_[member];
  ++p.epoch;
  p.syncing = true;
  p.syncTick = p.tick = adapter_.tick();
  p.digestTick = 0;
  p.digestIn = false;
  const std::vector<uint8_t> snap = adapter_.snapshot();
  ByteWriter w;
  w.u8(static_cast<uint8_t>(why));
  w.u32(adapter_.tick());
  w.u16(p.epoch);
  w.u8(paused_ ? 1 : 0);
  w.u8(pausedBy_);
  w.raw(snap.data(), snap.size());
  session_.sendTo(member, kSnapshot, w.bytes());
  for (uint32_t t = adapter_.tick() + 1; t <= lastTurn_; ++t) {
    auto it = turns_.find(t);
    session_.sendTo(member, kTurn, turnBytes(t, it == turns_.end() ? Turn{} : it->second));
  }
  ++stats_.snapshotsSent;
  stats_.lastSnapshotBytes = snap.size();
  PNR_INFO_CH(Net, "Battle sent to member %u at tick %u (%zu bytes, %s)", member, adapter_.tick(), snap.size(),
              why == 0 ? "start" : why == 1 ? "rejoin" : "resync");
}

void Lockstep::onSession(const SessionEvent& e) {
  using T = SessionEvent::Type;
  if (session_.isHost()) {
    if (!started_ || ended_) return;
    if (e.type == T::MemberLost || e.type == T::MemberLeft) {
      players_.erase(e.member);
      updateWaiting();
    } else if (e.type == T::MemberBack) {
      sendSnapshot(e.member, 1);
    }
    return;
  }
  if (e.type == T::Rejoined) {
    // Our battle may have gone on without us: the host sends it again.
    loading_ = true;
    turns_.clear();
    starvedSince_ = 0;
  } else if (e.type == T::Closed) {
    if (started_) ended_ = true;
    loading_ = false;
  }
}

// --- Orders and turns -------------------------------------------------------------

void Lockstep::submit(std::vector<uint8_t> order) {
  if (!started_ || ended_ || loading_) return;
  ++stats_.ordersSent;
  if (session_.isHost()) {
    if (session_.localId() != kNoMember) hostOrder(session_.localId(), std::move(order));
    return;
  }
  session_.sendToHost(kOrder, order);
}

void Lockstep::hostOrder(uint8_t from, std::vector<uint8_t> order) {
  if (!started_ || ended_) return;
  if (filter_ && !filter_(from, order)) return;
  open_.orders.push_back({from, std::move(order)});
}

void Lockstep::setPaused(bool on) {
  if (!started_ || ended_) return;
  session_.sendToHost(kPause, {static_cast<uint8_t>(on ? 1 : 0)});
}

void Lockstep::onTurn(const std::vector<uint8_t>& d) {
  if (session_.isHost() || !started_ || loading_) return;
  ByteReader r(d);
  const uint32_t t = r.u32();
  const uint16_t n = r.u16();
  Turn turn;
  for (uint16_t i = 0; i < n && r.ok(); ++i) {
    Order o;
    o.member = r.u8();
    const uint32_t len = r.u32();
    if (!r.ok() || len > r.remaining()) return;
    o.bytes.assign(r.cursor(), r.cursor() + len);
    r.skip(len);
    turn.orders.push_back(std::move(o));
  }
  if (!r.ok() || t <= adapter_.tick()) return;
  turns_[t] = std::move(turn);
  lastTurn_ = std::max(lastTurn_, t);
  ++stats_.turnsReceived;
}

void Lockstep::onSnapshot(const std::vector<uint8_t>& d) {
  if (session_.isHost()) return;
  ByteReader r(d);
  const int why = r.u8();
  const uint32_t t = r.u32();
  const uint16_t epoch = r.u16();
  const bool paused = r.u8() != 0;
  const uint8_t by = r.u8();
  if (!r.ok()) return;
  std::string err;
  if (!adapter_.restore(std::vector<uint8_t>(r.cursor(), r.cursor() + r.remaining()), err)) {
    PNR_ERROR_CH(Net, "The host's battle can't be loaded: %s", err.c_str());
    session_.leave("Could not load the host's battle: " + err);
    return;
  }
  if (adapter_.tick() != t) PNR_WARN_CH(Net, "Snapshot said tick %u, the battle is at %u", t, adapter_.tick());
  turns_.clear();
  lastTurn_ = adapter_.tick();
  epoch_ = epoch;
  started_ = true;
  ended_ = false;
  loading_ = false;
  paused_ = paused;
  pausedBy_ = by;
  digestAsk_ = 0;
  clock_ = 0;
  starvedSince_ = 0;
  if (why == 2) ++stats_.resyncs;
  ByteWriter w;
  w.u16(epoch_);
  w.u32(adapter_.tick());
  session_.sendToHost(kProgress, w.bytes(), Channel::Reliable);
  lastProgress_ = now_;
  PNR_INFO_CH(Net, "Battle %s at tick %u", why == 0 ? "received" : why == 1 ? "received again" : "resynced", t);
  if (onRestored) onRestored(why);
}

// --- Running ----------------------------------------------------------------------

void Lockstep::stepOnce() {
  const uint32_t t = adapter_.tick() + 1;
  auto it = turns_.find(t);
  if (it != turns_.end()) {
    for (const Order& o : it->second.orders) adapter_.apply(o.member, o.bytes);
    turns_.erase(it);
  }
  adapter_.step();
  const uint32_t now = adapter_.tick();
  const bool hashTick = now % static_cast<uint32_t>(cfg_.hashEvery) == 0;
  if (session_.isHost()) {
    if (hashTick) {
      const uint64_t h = adapter_.hash();
      hashes_[now] = h;
      while (hashes_.size() > 120) hashes_.erase(hashes_.begin());
      auto p = pendingHashes_.find(now);
      if (p != pendingHashes_.end()) {
        const auto list = std::move(p->second);
        pendingHashes_.erase(p);
        for (const auto& [m, ph] : list) checkHash(m, now, ph);
      }
      std::erase_if(pendingHashes_, [&](const auto& kv) { return kv.first < now; });
    }
    for (auto& [m, p] : players_)
      if (p.digestTick == now && !digests_.count(now)) digests_[now] = adapter_.digest();
    std::vector<uint8_t> ready;
    for (auto& [m, p] : players_)
      if (p.digestTick && p.digestTick <= now && p.digestIn) ready.push_back(m);
    for (uint8_t m : ready) finishDiagnosis(m);
  } else {
    if (hashTick) {
      ByteWriter w;
      w.u16(epoch_);
      w.u32(now);
      w.u64(adapter_.hash());
      w.u32(now);
      session_.sendToHost(kHash, w.bytes());
    }
    if (digestAsk_ && now == digestAsk_) {
      ByteWriter w;
      w.u16(epoch_);
      w.u32(now);
      const auto dg = adapter_.digest();
      w.u16(static_cast<uint16_t>(dg.size()));
      for (const auto& [name, h] : dg) {
        w.str(name);
        w.u64(h);
      }
      session_.sendToHost(kDigest, w.bytes());
      digestAsk_ = 0;
    }
  }
}

void Lockstep::hostClose() {
  const uint32_t t = ++lastTurn_;
  Turn& turn = turns_[t];
  turn = std::move(open_);
  open_ = {};
  session_.broadcast(kTurn, turnBytes(t, turn));
}

void Lockstep::updateWaiting() {
  if (!session_.isHost()) return;
  std::vector<uint8_t> waiting;
  const uint32_t mine = adapter_.tick();
  for (const auto& [m, p] : players_) {
    const Member* mem = session_.member(m);
    if (!mem || !mem->connected) continue;
    // Silent for a while: probably a broken line, not a slow computer. It
    // catches up from the turns it gets when the line is back; until then
    // nobody waits for it.
    if (session_.stats(m).idleMs > 1000) continue;
    if (mine > p.tick && mine - p.tick > static_cast<uint32_t>(cfg_.maxBehind)) waiting.push_back(m);
  }
  if (waiting != waitingFor_) {
    waitingFor_ = waiting;
    session_.broadcast(kWaiting, waitingFor_);
    if (!waiting.empty()) PNR_INFO_CH(Net, "Waiting for %zu player(s)", waiting.size());
  }
}

int Lockstep::advance(uint64_t nowMs) {
  now_ = nowMs;
  if (lastAdvance_ == 0 || nowMs < lastAdvance_) lastAdvance_ = nowMs;
  const double elapsed = static_cast<double>(nowMs - lastAdvance_);
  lastAdvance_ = nowMs;
  if (!started_ || ended_ || loading_) return 0;
  const double tickMs = 1000.0 / cfg_.ticksPerSecond;
  int ran = 0;

  if (session_.isHost()) {
    // The delay follows the slowest round trip: up at once, down slowly.
    if (nowMs - lastDelayCheck_ >= 500) {
      lastDelayCheck_ = nowMs;
      float rtt = 0;
      for (const auto& [m, p] : players_) rtt = std::max(rtt, session_.stats(m).rttMs);
      const int want = std::clamp(static_cast<int>(std::ceil(rtt * 0.5 / tickMs)) + cfg_.keepInHand, cfg_.minDelay,
                                  cfg_.maxDelay);
      if (want > delay_) delay_ = want;
      else if (want < delay_ && nowMs - lastDelayDrop_ > 4000) {
        --delay_;
        lastDelayDrop_ = nowMs;
      }
    }
    updateWaiting();
    clock_ += elapsed;
    const bool hold = paused_ || !waitingFor_.empty();
    int closed = 0;
    while (clock_ >= tickMs && closed < 8) {
      clock_ -= tickMs;
      if (hold) continue;
      hostClose();
      ++closed;
    }
    if (hold || clock_ > tickMs * 4) clock_ = std::min(clock_, tickMs);
    // Our own battle runs `delay` ticks behind the turns.
    const uint32_t target = lastTurn_ > static_cast<uint32_t>(delay_) ? lastTurn_ - static_cast<uint32_t>(delay_) : 0;
    while (adapter_.tick() < target && ran < 1 + cfg_.catchUp) {
      stepOnce();
      ++ran;
    }
    // Players who were sent their digest request and never answered.
    std::vector<uint8_t> late;
    for (auto& [m, p] : players_)
      if (p.digestTick && nowMs - p.digestAskedAt > static_cast<uint64_t>(kDigestTimeoutMs)) late.push_back(m);
    for (uint8_t m : late) finishDiagnosis(m);
    return ran;
  }

  // A player: run the turns we have, at the clock's pace, catching up when behind.
  clock_ += elapsed;
  int due = static_cast<int>(clock_ / tickMs);
  clock_ -= due * tickMs;
  if (clock_ > tickMs) clock_ = tickMs;
  const auto inHand = [&]() -> int {
    return static_cast<int>(lastTurn_) - static_cast<int>(adapter_.tick()) - cfg_.keepInHand;
  };
  if (paused_) due = 0;
  int can = inHand();
  int run = std::min(due, std::max(0, can));
  if (can - run > cfg_.keepInHand + 2) run += std::min(can - run - cfg_.keepInHand, cfg_.catchUp);
  if (due > 0 && can <= 0 && !paused_) {
    if (!starvedSince_) starvedSince_ = nowMs;
  } else if (can > 0) {
    starvedSince_ = 0;
  }
  for (; ran < run && adapter_.tick() < lastTurn_; ++ran) stepOnce();
  if (nowMs - lastProgress_ >= static_cast<uint64_t>(kProgressEveryMs)) {
    lastProgress_ = nowMs;
    ByteWriter w;
    w.u16(epoch_);
    w.u32(adapter_.tick());
    session_.sendToHost(kProgress, w.bytes(), Channel::Unreliable);
  }
  return ran;
}

// --- Checks -----------------------------------------------------------------------

void Lockstep::onHash(uint8_t from, const std::vector<uint8_t>& d) {
  if (!session_.isHost() || !started_) return;
  ByteReader r(d);
  const uint16_t epoch = r.u16();
  const uint32_t t = r.u32();
  const uint64_t h = r.u64();
  const uint32_t cur = r.u32();
  auto it = players_.find(from);
  if (!r.ok() || it == players_.end() || epoch != it->second.epoch) return;
  it->second.tick = std::max(it->second.tick, cur);
  if (it->second.syncing && cur >= it->second.syncTick) it->second.syncing = false;
  if (adapter_.tick() >= t) checkHash(from, t, h);
  else pendingHashes_[t].push_back({from, h});
}

void Lockstep::checkHash(uint8_t member, uint32_t t, uint64_t h) {
  auto mine = hashes_.find(t);
  if (mine == hashes_.end() || mine->second == h) return;
  desyncFound(member, t);
}

void Lockstep::desyncFound(uint8_t member, uint32_t t) {
  auto it = players_.find(member);
  if (it == players_.end()) return;
  PlayerInfo& p = it->second;
  if (p.digestTick || (p.syncing && t <= p.syncTick)) return;
  ++stats_.desyncs;
  stats_.lastDesyncTick = t;
  p.desyncTick = t;
  PNR_WARN_CH(Net, "Member %u is out of step at tick %u", member, t);
  if (adapter_.digest().empty()) {
    p.digestTick = std::max<uint32_t>(1, adapter_.tick());
    p.digestAskedAt = 0;  // nothing to ask: finish at once
    finishDiagnosis(member);
    return;
  }
  // Both sides take a digest at a tick neither has run yet.
  p.digestTick = lastTurn_ + 2;
  p.digestAskedAt = now_;
  p.digestIn = false;
  p.digest.clear();
  ByteWriter w;
  w.u16(p.epoch);
  w.u32(p.digestTick);
  session_.sendTo(member, kDigestAsk, w.bytes());
}

void Lockstep::onDigest(uint8_t from, const std::vector<uint8_t>& d) {
  if (!session_.isHost()) return;
  auto it = players_.find(from);
  if (it == players_.end()) return;
  ByteReader r(d);
  const uint16_t epoch = r.u16();
  const uint32_t t = r.u32();
  const uint16_t n = r.u16();
  std::vector<std::pair<std::string, uint64_t>> dg;
  for (uint16_t i = 0; i < n && r.ok(); ++i) {
    std::string name = r.str();
    const uint64_t h = r.u64();
    dg.emplace_back(std::move(name), h);
  }
  PlayerInfo& p = it->second;
  if (!r.ok() || epoch != p.epoch || t != p.digestTick) return;
  p.digest = std::move(dg);
  p.digestIn = true;
  if (adapter_.tick() >= t) finishDiagnosis(from);
}

void Lockstep::finishDiagnosis(uint8_t member) {
  auto it = players_.find(member);
  if (it == players_.end() || !it->second.digestTick) return;
  PlayerInfo& p = it->second;
  std::string systems;
  auto mine = digests_.find(p.digestTick);
  if (p.digestIn && mine != digests_.end()) {
    std::map<std::string, uint64_t> theirs(p.digest.begin(), p.digest.end());
    for (const auto& [name, h] : mine->second) {
      auto t = theirs.find(name);
      if (t == theirs.end() || t->second != h) systems += (systems.empty() ? "" : ", ") + name;
    }
    if (systems.empty()) systems = "(the same by then)";
  } else if (p.digestAskedAt) {
    systems = "(no digest)";
  }
  const uint32_t t = p.desyncTick;
  const uint32_t dt = p.digestTick;
  p.digestTick = 0;
  p.digestIn = false;
  p.digest.clear();
  bool stillNeeded = false;
  for (const auto& [m, q] : players_) stillNeeded = stillNeeded || q.digestTick == dt;
  if (!stillNeeded) digests_.erase(dt);
  stats_.lastDesyncSystems = systems;
  PNR_WARN_CH(Net, "Member %u out of step from tick %u%s%s", member, t, systems.empty() ? "" : "; differs in: ",
              systems.c_str());
  ByteWriter w;
  w.u8(member);
  w.u32(t);
  w.str(systems);
  session_.broadcast(kDesync, w.bytes());
  if (onDesync) onDesync(member, t, systems);
  if (cfg_.resyncOnDesync) {
    ++stats_.resyncs;
    sendSnapshot(member, 2);
  }
}

}  // namespace pioneer::net

#pragma once
// Pioneer Engine - deterministic lockstep over a session.
//
// Every machine runs the same simulation; only orders travel. The host (a
// player's game or a dedicated server) is the relay and the referee:
//
//   - Turns. Players send their orders to the host. The host closes one turn
//     per tick on its own clock - the orders that reached it, in the order
//     they came - and sends it to everyone. Every machine runs tick t once
//     it has turn t, applying that turn's orders first. Everyone applies the
//     same orders at the same tick, so everyone's battle stays the same.
//   - Delay. The host runs its own battle a few ticks behind the turns it
//     has closed (the delay), so turns reach the players before they are
//     needed. The delay follows the slowest round trip (one tick at least),
//     so a LAN game feels immediate and an internet game doesn't stutter.
//     Players keep a turn or two in hand and catch up when they fall behind.
//   - Waiting. When a player falls too far behind (a slow computer, a
//     stalled line), the host stops closing turns until it catches up, and
//     everyone sees who they are waiting for. A player whose line goes
//     silent doesn't hold the game: the battle goes on without its orders,
//     and it catches up (or rejoins, when the connection is lost) later.
//   - Checks. Every player sends the state hash once a second; the host
//     compares it with its own battle's. A difference is a desync: the host
//     asks for the player's per-system digest at a coming tick, names the
//     systems that differ (log, onDesync) and then sends the player its own
//     state (a resync), so the game goes on.
//   - Snapshots. The host's battle, saved (the adapter's snapshot), with
//     the turns after it, is what starts a game for everyone, brings back a
//     player who rejoins and repairs a desync. A dedicated server builds no
//     battle itself: the leader's game builds it and sends it up.
//   - Pause: anyone may pause or go on; the host stops or restarts the turns.
//
// The game plugs in through LockstepAdapter (orders are bytes here) and
// filters orders on the host (setFilter: e.g. no cheats in multiplayer).

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "pioneer/net/session.hpp"

namespace pioneer::net {

class LockstepAdapter {
 public:
  virtual ~LockstepAdapter() = default;
  // An order from `member` for the coming tick.
  virtual void apply(uint8_t member, const std::vector<uint8_t>& order) = 0;
  virtual void step() = 0;  // run one tick
  virtual uint32_t tick() const = 0;
  virtual uint64_t hash() const = 0;
  virtual std::vector<uint8_t> snapshot() const = 0;
  virtual bool restore(const std::vector<uint8_t>& snap, std::string& error) = 0;
  // Optional: a hash per system, to name what differs after a desync.
  virtual std::vector<std::pair<std::string, uint64_t>> digest() const { return {}; }
};

struct LockstepConfig {
  int ticksPerSecond = 15;
  int hashEvery = 15;        // ticks between state checks
  int maxBehind = 30;        // ticks a player may trail the host's battle before everyone waits
  int minDelay = 1, maxDelay = 12;
  int keepInHand = 1;        // turns a player keeps before running them (absorbs jitter)
  int catchUp = 8;           // extra ticks per advance() when behind
  bool resyncOnDesync = true;
  bool anyonePauses = true;
};

enum class LockstepPhase : uint8_t {
  Idle,      // no battle yet
  Loading,   // a snapshot is on its way
  Running,
  Waiting,   // for a player (see waitingFor()) or for turns
  Paused,
  Ended,
};
const char* phaseName(LockstepPhase p);

class Lockstep {
 public:
  Lockstep(Session& session, LockstepAdapter& adapter, LockstepConfig cfg = {});
  ~Lockstep();
  Lockstep(const Lockstep&) = delete;
  Lockstep& operator=(const Lockstep&) = delete;

  // The host, or the leader on a dedicated server: the adapter's battle as it
  // stands is the game. Everyone is sent it and the turns begin.
  bool start(std::string& error);
  // The host: the game is over (everyone stops).
  void end();
  // The local player's order (sent to the host; run a few ticks later everywhere).
  void submit(std::vector<uint8_t> order);
  void setPaused(bool on);

  // Runs the ticks the clock and the turns allow; returns how many ran.
  int advance(uint64_t nowMs);

  LockstepPhase phase() const;
  uint32_t tick() const { return adapter_.tick(); }
  uint32_t lastTurn() const { return lastTurn_; }
  int delay() const { return delay_; }
  bool paused() const { return paused_; }
  uint8_t pausedBy() const { return pausedBy_; }
  const std::vector<uint8_t>& waitingFor() const { return waitingFor_; }

  // Host: decides whether an order from `member` is taken (it may change it).
  using Filter = std::function<bool(uint8_t member, std::vector<uint8_t>& order)>;
  void setFilter(Filter f) { filter_ = std::move(f); }
  // The battle was replaced from a snapshot: 0 start, 1 rejoin, 2 resync.
  std::function<void(int why)> onRestored;
  // A player was out of step (on the host; and the notice everyone gets).
  std::function<void(uint8_t member, uint32_t tick, const std::string& systems)> onDesync;

  struct Stats {
    uint32_t desyncs = 0, resyncs = 0, snapshotsSent = 0;
    uint32_t lastDesyncTick = 0;
    std::string lastDesyncSystems;
    size_t lastSnapshotBytes = 0;
    uint32_t ordersSent = 0, turnsReceived = 0;
  };
  const Stats& stats() const { return stats_; }

 private:
  struct Order {
    uint8_t member;
    std::vector<uint8_t> bytes;
  };
  struct Turn {
    std::vector<Order> orders;
  };
  void onSession(const SessionEvent& e);
  void hostOrder(uint8_t from, std::vector<uint8_t> order);
  void onTurn(const std::vector<uint8_t>& d);
  void onSnapshot(const std::vector<uint8_t>& d);
  void onHash(uint8_t from, const std::vector<uint8_t>& d);
  void onDigest(uint8_t from, const std::vector<uint8_t>& d);
  void onUpload(uint8_t from, const std::vector<uint8_t>& d);
  std::vector<uint8_t> turnBytes(uint32_t t, const Turn& turn) const;
  void sendSnapshot(uint8_t member, int why);
  void stepOnce();
  void hostClose();
  void checkHash(uint8_t member, uint32_t t, uint64_t h);
  void desyncFound(uint8_t member, uint32_t t);
  void finishDiagnosis(uint8_t member);
  void updateWaiting();
  std::vector<uint8_t> playersInGame() const;

  Session& session_;
  LockstepAdapter& adapter_;
  LockstepConfig cfg_;
  int listener_ = 0;
  bool started_ = false, ended_ = false, loading_ = false;
  bool paused_ = false;
  uint8_t pausedBy_ = kNoMember;
  uint64_t now_ = 0, lastAdvance_ = 0;
  double clock_ = 0;           // ms owed to the tick clock
  int delay_ = 2;
  // Turns: on the host, every closed turn still needed; on players, those not yet run.
  std::map<uint32_t, Turn> turns_;
  uint32_t lastTurn_ = 0;      // the newest turn closed (host) or received (player)
  Turn open_;                  // host: orders for the next turn
  std::vector<uint8_t> waitingFor_;
  uint64_t starvedSince_ = 0;
  // Host: what each player reports.
  struct PlayerInfo {
    uint32_t tick = 0;
    bool syncing = false;      // a snapshot is on its way to it
    uint32_t syncTick = 0;
    uint32_t digestTick = 0;   // asked for its digest at this tick (0: not)
    uint64_t digestAskedAt = 0;
    std::vector<std::pair<std::string, uint64_t>> digest;
    bool digestIn = false;
    uint32_t desyncTick = 0;
    uint16_t epoch = 0;        // which snapshot its reports belong to
  };
  std::map<uint8_t, PlayerInfo> players_;
  std::map<uint32_t, uint64_t> hashes_;                                       // host: its own, by tick
  std::map<uint32_t, std::vector<std::pair<uint8_t, uint64_t>>> pendingHashes_;  // players' hashes ahead of the host
  std::map<uint32_t, std::vector<std::pair<std::string, uint64_t>>> digests_;   // host: its own, by tick
  // Player: digest asked for at a tick.
  uint32_t digestAsk_ = 0;
  uint16_t epoch_ = 0;
  uint64_t lastDelayCheck_ = 0, lastDelayDrop_ = 0;
  uint64_t lastProgress_ = 0;
  Filter filter_;
  Stats stats_;
};

}  // namespace pioneer::net

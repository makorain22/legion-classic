#pragma once
// Pioneer Engine - a network in memory, for tests.
//
// Sockets made by a LoopbackNetwork send to each other through the network,
// which holds every datagram back for a latency (plus a random jitter, so
// datagrams overtake each other), loses some, doubles some and can cut a
// computer off entirely. Time is the test's: `advance(ms)` moves the clock
// and lets due datagrams arrive. The dice are seeded, so a test that fails
// fails the same way every time.

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "pioneer/core/rng.hpp"
#include "pioneer/net/socket.hpp"

namespace pioneer::net {

struct LinkConditions {
  int latencyMs = 0;      // one way
  int jitterMs = 0;       // up to this much more, at random (reorders datagrams)
  float loss = 0.0f;      // share of datagrams lost, 0..1
  float duplicate = 0.0f; // share sent twice
};

class LoopbackNetwork {
 public:
  explicit LoopbackNetwork(uint64_t seed = 1) : rng_(seed) {}

  // A socket for the computer at `ip` (10.0.0.x by default), bound to `port`
  // (0: a free one). The network must outlive its sockets.
  std::unique_ptr<DatagramSocket> open(uint32_t ip, uint16_t port);

  // Conditions for every datagram, or only between two computers (both ways).
  void setConditions(const LinkConditions& c) { all_ = c; }
  void setConditions(uint32_t ipA, uint32_t ipB, const LinkConditions& c);
  // A computer off the network (cable out): nothing to or from it arrives.
  void setOffline(uint32_t ip, bool off);

  uint64_t now() const { return now_; }
  void advance(uint64_t ms);

  struct Stats {
    uint64_t sent = 0, delivered = 0, lost = 0, bytes = 0;
  };
  const Stats& stats() const { return stats_; }

 private:
  class Socket;
  friend class Socket;
  struct Datagram {
    uint64_t due;
    uint64_t order;
    Address from, to;
    std::vector<uint8_t> data;
  };
  bool post(const Address& from, const Address& to, const uint8_t* data, size_t size);
  void deliver(Datagram&& d);
  const LinkConditions& conditions(uint32_t a, uint32_t b) const;
  void detach(Socket* s);

  Rng rng_;
  uint64_t now_ = 0, order_ = 0;
  uint16_t nextPort_ = 50000;
  LinkConditions all_;
  std::map<std::pair<uint32_t, uint32_t>, LinkConditions> links_;
  std::map<uint32_t, bool> offline_;
  std::map<Address, Socket*> sockets_;
  std::vector<Datagram> inFlight_;
  Stats stats_;
};

// 10.0.0.n
constexpr uint32_t loopbackHost(uint8_t n) { return 0x0A000000u | n; }

}  // namespace pioneer::net

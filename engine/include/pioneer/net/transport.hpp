#pragma once
// Pioneer Engine - connections over datagrams.
//
// A Transport turns one DatagramSocket into any number of connections, each
// carrying messages of any size on two channels:
//
//   - Reliable: every message arrives, once, in the order it was sent. Lost
//     datagrams are sent again; a message too big for one datagram is cut
//     into pieces and put back together.
//   - Unreliable: may be lost, never repeated; for things that are stale by
//     the time a resend would arrive.
//
// How it works: every datagram carries a sequence number and acknowledges
// the last 33 datagrams the other side sent (number + 32-bit field), so one
// lost acknowledgement costs nothing. A reliable message stays queued until
// a datagram carrying it is acknowledged, and goes again when that takes
// longer than the round trip (with some margin). The round trip is measured
// from the acknowledgements. Sending is limited to a byte rate per
// connection, so a big message (a battle's state for a player who rejoins)
// doesn't flood a slow line.
//
// A connection starts with a handshake (Connect / Accept, with a random
// number from each side that every later datagram must carry), ends with
// Disconnect, or times out when nothing arrives for a while. Datagrams from
// another game or another protocol version (`protocolId`) are ignored.
//
// Time is passed in (`update(nowMs)`): the game gives it the clock, tests
// a LoopbackNetwork's. Nothing here blocks or makes threads. No encryption:
// it is for LAN games and friends' servers.

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pioneer/net/socket.hpp"

namespace pioneer::net {

using ConnId = uint32_t;
enum class Channel : uint8_t { Unreliable = 0, Reliable = 1 };

enum class DisconnectReason : uint8_t {
  None,
  Local,     // this side closed it
  Remote,    // the other side closed it
  TimedOut,  // nothing arrived for timeoutMs
  Refused,   // the other side doesn't take connections (or is full)
  NoAnswer,  // connecting got no answer within connectTimeoutMs
  Replaced,  // the same computer connected again (the game restarted there)
};
const char* reasonText(DisconnectReason r);

struct TransportConfig {
  uint32_t protocolId = 0x504E5231;  // 'PNR1': give each game and protocol version its own
  int maxConnections = 32;
  int timeoutMs = 8000;
  int connectTimeoutMs = 6000;
  int keepAliveMs = 100;           // an empty datagram at least this often (it carries acknowledgements)
  size_t sendRate = 512 * 1024;    // bytes per second per connection
  size_t window = 512;             // reliable messages (pieces) in flight per connection
};

struct TransportEvent {
  enum class Type : uint8_t { Connected, Disconnected, Message } type = Type::Message;
  ConnId conn = 0;
  Channel channel = Channel::Reliable;
  DisconnectReason reason = DisconnectReason::None;
  std::vector<uint8_t> data;
};

struct ConnectionStats {
  float rttMs = 0;
  uint64_t packetsSent = 0, packetsReceived = 0, packetsLost = 0;
  uint64_t bytesSent = 0, bytesReceived = 0;
  uint64_t resent = 0;          // reliable pieces sent again
  size_t queued = 0;            // reliable pieces not yet acknowledged
  size_t queuedBytes = 0;
  uint64_t idleMs = 0;          // since anything last arrived
};

class Transport {
 public:
  Transport(DatagramSocket& socket, TransportConfig cfg = {});
  ~Transport();
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  // Take connections from others (a host or server).
  void listen(bool on) { listening_ = on; }
  bool listening() const { return listening_; }
  // Connect to a listening transport. Connected or Disconnected (NoAnswer,
  // Refused) follows as an event.
  ConnId connect(const Address& to);
  // Queue a message (sent at the next update).
  bool send(ConnId c, Channel ch, const std::vector<uint8_t>& data) { return send(c, ch, data.data(), data.size()); }
  bool send(ConnId c, Channel ch, const uint8_t* data, size_t size);
  // Close a connection (the other side hears it, best effort); no event follows here.
  void disconnect(ConnId c);
  void disconnectAll();

  // Receive, resend, acknowledge, time out. Call often (every frame).
  void update(uint64_t nowMs);
  bool poll(TransportEvent& out);

  bool connected(ConnId c) const;
  Address address(ConnId c) const;
  ConnectionStats stats(ConnId c) const;
  std::vector<ConnId> connections() const;  // connected ones
  DatagramSocket& socket() { return socket_; }
  const TransportConfig& config() const { return cfg_; }

  // Datagrams that came from an address with no connection and weren't a
  // handshake (other uses of the same port could read these). Kept for tests.
  uint64_t strayDatagrams() const { return stray_; }

 private:
  struct Conn;
  Conn* find(ConnId c) const;
  Conn* findByAddress(const Address& a) const;
  void handle(const Address& from, const std::vector<uint8_t>& d);
  void handleData(Conn& c, const uint8_t* p, size_t n);
  void flush(Conn& c);
  void sendRaw(const Address& to, const std::vector<uint8_t>& d);
  void close(Conn& c, DisconnectReason why, bool tell);
  uint32_t salt();

  DatagramSocket& socket_;
  TransportConfig cfg_;
  bool listening_ = false;
  uint64_t now_ = 0;
  ConnId nextId_ = 1;
  std::map<ConnId, std::unique_ptr<Conn>> conns_;
  std::deque<TransportEvent> events_;
  uint64_t saltState_ = 0;
  uint64_t stray_ = 0;
};

// Wrap-around comparison of 16-bit sequence numbers: is a after b?
inline bool seqNewer(uint16_t a, uint16_t b) { return a != b && static_cast<uint16_t>(a - b) < 0x8000; }

}  // namespace pioneer::net

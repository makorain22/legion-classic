#pragma once
// Pioneer Engine - addresses and datagram sockets.
//
// Everything above this file talks to a `DatagramSocket`: send a datagram to
// an address, take the next one that arrived (never blocking). Two kinds
// exist: `UdpSocket`, the real thing (Windows and POSIX), and the sockets of
// a `LoopbackNetwork` (net/loopback.hpp), a network in memory with latency,
// loss and broken links that tests drive with their own clock.
//
// IPv4 only for now: LAN games and direct IP need nothing else, and the
// address type is small enough to grow later.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pioneer::net {

struct Address {
  uint32_t ip = 0;    // host byte order: 127.0.0.1 = 0x7F000001
  uint16_t port = 0;

  static Address loopback(uint16_t port) { return {0x7F000001u, port}; }
  static Address broadcast(uint16_t port) { return {0xFFFFFFFFu, port}; }
  bool valid() const { return port != 0; }
  bool isLoopback() const { return (ip >> 24) == 127; }
  bool isBroadcast() const { return ip == 0xFFFFFFFFu; }
  std::string toString() const;      // "192.168.1.20:47800"
  std::string ipString() const;      // "192.168.1.20"
  bool operator==(const Address& o) const { return ip == o.ip && port == o.port; }
  bool operator!=(const Address& o) const { return !(*this == o); }
  bool operator<(const Address& o) const { return ip != o.ip ? ip < o.ip : port < o.port; }
};

// "1.2.3.4", "1.2.3.4:5000", "localhost", "myserver.example.org:5000". A
// name is looked up (this can take a moment); no port given = `defaultPort`.
bool parseAddress(const std::string& text, uint16_t defaultPort, Address& out, std::string& error);

class DatagramSocket {
 public:
  virtual ~DatagramSocket() = default;
  // False when the datagram certainly didn't go (closed socket, too big...).
  // True says nothing about arriving: datagrams get lost.
  virtual bool send(const Address& to, const uint8_t* data, size_t size) = 0;
  // The next datagram that arrived, or false when there is none.
  virtual bool receive(Address& from, std::vector<uint8_t>& out) = 0;
  virtual Address localAddress() const = 0;
};

// Sockets need the system's network library started (Windows); this is
// counted, so every UdpSocket calls it and nobody else has to.
bool netStartup();
void netShutdown();

class UdpSocket final : public DatagramSocket {
 public:
  UdpSocket() = default;
  ~UdpSocket() override;
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  // port 0: any free port. `broadcast`: may send to 255.255.255.255.
  // `shared`: other sockets may bind the same port (LAN browsing from two
  // copies of the game on one computer).
  bool open(uint16_t port, bool broadcast, std::string& error, bool shared = false);
  void close();
  bool isOpen() const { return handle_ != kInvalid; }

  bool send(const Address& to, const uint8_t* data, size_t size) override;
  bool receive(Address& from, std::vector<uint8_t>& out) override;
  Address localAddress() const override { return local_; }

 private:
  static constexpr intptr_t kInvalid = -1;
  intptr_t handle_ = kInvalid;
  Address local_;
  bool started_ = false;
};

// The largest datagram anything in the engine sends: fits in one Ethernet
// frame with room for IP/UDP headers and the odd VPN tunnel.
constexpr size_t kMaxDatagram = 1200;

}  // namespace pioneer::net

// Pioneer Engine - finding games on the local network (see discovery.hpp).
#include "pioneer/net/discovery.hpp"

#include <algorithm>

#include "pioneer/core/bytes.hpp"

namespace pioneer::net {

namespace {
constexpr uint32_t kBeaconMagic = 0x424C4E50;  // 'PNLB'
constexpr uint8_t kBeaconVersion = 1;
}  // namespace

std::vector<uint8_t> encodeBeacon(const GameBeacon& b) {
  ByteWriter w;
  w.u32(kBeaconMagic);
  w.u8(kBeaconVersion);
  w.str(b.gameId.substr(0, 64));
  w.str(b.version.substr(0, 64));
  w.str(b.name.substr(0, 64));
  w.str(b.description.substr(0, 256));
  w.u16(b.port);
  w.u8(b.members);
  w.u8(b.maxMembers);
  w.u8(static_cast<uint8_t>((b.inGame ? 1 : 0) | (b.password ? 2 : 0) | (b.dedicated ? 4 : 0)));
  w.u64(b.instance);
  return w.bytes();
}

bool decodeBeacon(const uint8_t* data, size_t size, GameBeacon& out) {
  ByteReader r(data, size);
  if (r.u32() != kBeaconMagic || r.u8() != kBeaconVersion) return false;
  out.gameId = r.str();
  out.version = r.str();
  out.name = r.str();
  out.description = r.str();
  out.port = r.u16();
  out.members = r.u8();
  out.maxMembers = r.u8();
  const uint8_t f = r.u8();
  out.inGame = f & 1;
  out.password = (f & 2) != 0;
  out.dedicated = (f & 4) != 0;
  out.instance = r.u64();
  return r.ok() && out.port != 0;
}

DiscoveryAnnouncer::DiscoveryAnnouncer(DatagramSocket& socket, uint16_t discoveryPort, int everyMs)
    : socket_(socket), port_(discoveryPort), every_(everyMs) {}

void DiscoveryAnnouncer::update(uint64_t nowMs, const GameBeacon& beacon) {
  if (sent_ && nowMs - last_ < static_cast<uint64_t>(every_)) return;
  sent_ = true;
  last_ = nowMs;
  const std::vector<uint8_t> b = encodeBeacon(beacon);
  socket_.send(Address::broadcast(port_), b.data(), b.size());
  socket_.send(Address::loopback(port_), b.data(), b.size());
}

DiscoveryBrowser::DiscoveryBrowser(DatagramSocket& socket, std::string gameId, int forgetAfterMs)
    : socket_(socket), gameId_(std::move(gameId)), forget_(forgetAfterMs) {}

void DiscoveryBrowser::update(uint64_t nowMs) {
  Address from;
  std::vector<uint8_t> d;
  while (socket_.receive(from, d)) {
    GameBeacon b;
    if (!decodeBeacon(d.data(), d.size(), b) || b.gameId != gameId_) continue;
    b.address = {from.ip, b.port};
    b.lastSeen = nowMs;
    auto it = std::find_if(games_.begin(), games_.end(), [&](const GameBeacon& g) { return g.instance == b.instance; });
    if (it == games_.end()) {
      games_.push_back(b);
    } else {
      // Heard on this computer and over the network: the network address
      // works from anywhere, so keep that one.
      const Address keep = (b.address.isLoopback() && !it->address.isLoopback()) ? it->address : b.address;
      *it = b;
      it->address = keep;
    }
  }
  std::erase_if(games_, [&](const GameBeacon& g) { return nowMs - g.lastSeen > static_cast<uint64_t>(forget_); });
  std::sort(games_.begin(), games_.end(), [](const GameBeacon& a, const GameBeacon& b) {
    return a.name != b.name ? a.name < b.name : a.instance < b.instance;
  });
}

}  // namespace pioneer::net

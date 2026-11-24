#pragma once
// Pioneer Engine - finding games on the local network.
//
// A host announces itself once a second with a beacon broadcast to the
// discovery port (and to the same port on this computer, for a second copy
// of the game running here). Anyone browsing listens on that port and keeps
// a list of the games heard from, forgetting one silent for a few seconds.
// Beacons carry the game's id: another game using the engine is never
// listed. Hosts don't listen for anything, so browsing costs them nothing.

#include <cstdint>
#include <string>
#include <vector>

#include "pioneer/net/socket.hpp"

namespace pioneer::net {

struct GameBeacon {
  std::string gameId;
  std::string version;
  std::string name;          // the host's player or the server's name
  std::string description;   // the game's own (Legion: the map)
  uint16_t port = 0;         // where to join
  uint8_t members = 0, maxMembers = 0;
  bool inGame = false;
  bool password = false;
  bool dedicated = false;
  uint64_t instance = 0;     // tells two hosts on one computer apart
  // Filled in by the browser:
  Address address;           // the host, at its game port
  uint64_t lastSeen = 0;
};

std::vector<uint8_t> encodeBeacon(const GameBeacon& b);
bool decodeBeacon(const uint8_t* data, size_t size, GameBeacon& out);

class DiscoveryAnnouncer {
 public:
  // `socket` may be the game's own (it must be allowed to broadcast).
  DiscoveryAnnouncer(DatagramSocket& socket, uint16_t discoveryPort = 47801, int everyMs = 1000);
  void update(uint64_t nowMs, const GameBeacon& beacon);

 private:
  DatagramSocket& socket_;
  uint16_t port_;
  int every_;
  uint64_t last_ = 0;
  bool sent_ = false;
};

class DiscoveryBrowser {
 public:
  // `socket`: bound to the discovery port (shared, so two browsers on one
  // computer both hear). Only games with `gameId` are kept.
  DiscoveryBrowser(DatagramSocket& socket, std::string gameId, int forgetAfterMs = 3500);
  void update(uint64_t nowMs);
  const std::vector<GameBeacon>& games() const { return games_; }
  void clear() { games_.clear(); }

 private:
  DatagramSocket& socket_;
  std::string gameId_;
  int forget_;
  std::vector<GameBeacon> games_;
};

}  // namespace pioneer::net

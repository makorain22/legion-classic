// Networking: addresses, the reliable transport, LAN discovery, sessions
// (lobby, chat, rejoining) - over the in-memory network, and once over real
// UDP on this computer.
#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <memory>

#include "pioneer/net/discovery.hpp"
#include "pioneer/net/loopback.hpp"
#include "pioneer/net/session.hpp"
#include "pioneer/net/transport.hpp"

using namespace pioneer::net;

namespace {
std::vector<uint8_t> bytesOf(size_t n, uint8_t seed) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 7 + (i >> 8));
  return v;
}

// Runs `ms` of network time in 10 ms steps, calling `each` with the clock.
void run(LoopbackNetwork& net, int ms, const std::function<void(uint64_t)>& each) {
  for (int t = 0; t < ms; t += 10) {
    net.advance(10);
    each(net.now());
  }
}

struct TransportPair {
  LoopbackNetwork net{42};
  std::unique_ptr<DatagramSocket> hs = net.open(loopbackHost(1), 47800);
  std::unique_ptr<DatagramSocket> cs = net.open(loopbackHost(2), 0);
  Transport host{*hs}, client{*cs};
  ConnId toHost = 0, toClient = 0;
  std::vector<TransportEvent> atHost, atClient;
  void pump(int ms) {
    run(net, ms, [&](uint64_t now) {
      host.update(now);
      client.update(now);
      TransportEvent e;
      while (host.poll(e)) {
        if (e.type == TransportEvent::Type::Connected) toClient = e.conn;
        atHost.push_back(e);
      }
      while (client.poll(e)) atClient.push_back(e);
    });
  }
};
}  // namespace

TEST_CASE("net: addresses are read and written") {
  Address a;
  std::string err;
  REQUIRE(parseAddress("192.168.1.20", 47800, a, err));
  CHECK(a.ip == 0xC0A80114u);
  CHECK(a.port == 47800);
  CHECK(a.toString() == "192.168.1.20:47800");
  REQUIRE(parseAddress(" 10.0.0.5:5000 ", 47800, a, err));
  CHECK(a.toString() == "10.0.0.5:5000");
  CHECK_FALSE(parseAddress("10.0.0.5:99999", 47800, a, err));
  CHECK_FALSE(parseAddress("300.1.1.1", 47800, a, err));
  CHECK_FALSE(parseAddress("", 47800, a, err));
  REQUIRE(parseAddress("localhost:4000", 47800, a, err));
  CHECK(a.isLoopback());
  CHECK(Address::broadcast(1).isBroadcast());
  CHECK(cleanText("  hi\x01 there\n ", 100) == "hi there");
  CHECK(cleanText("\xc3\xa9\xc3\xa9\xc3\xa9", 3) == "\xc3\xa9");  // never half a character
}

TEST_CASE("net: the transport delivers reliable messages once and in order through a bad line") {
  TransportPair p;
  p.net.setConditions({40, 30, 0.2f, 0.05f});  // 40-70 ms, 20% lost, 5% doubled
  p.host.listen(true);
  p.toHost = p.client.connect(p.hs->localAddress());
  p.pump(2000);
  REQUIRE(p.client.connected(p.toHost));
  REQUIRE(p.toClient != 0);
  // 300 small messages, one big one (cut into pieces), both ways.
  for (int i = 0; i < 300; ++i) p.client.send(p.toHost, Channel::Reliable, bytesOf(1 + i % 50, static_cast<uint8_t>(i)));
  const std::vector<uint8_t> big = bytesOf(150000, 9);
  p.host.send(p.toClient, Channel::Reliable, big);
  p.host.send(p.toClient, Channel::Reliable, {1, 2, 3});
  p.pump(8000);
  std::vector<std::vector<uint8_t>> got;
  for (const TransportEvent& e : p.atHost)
    if (e.type == TransportEvent::Type::Message) got.push_back(e.data);
  REQUIRE(got.size() == 300);
  for (int i = 0; i < 300; ++i) CHECK(got[static_cast<size_t>(i)] == bytesOf(1 + i % 50, static_cast<uint8_t>(i)));
  std::vector<std::vector<uint8_t>> back;
  for (const TransportEvent& e : p.atClient)
    if (e.type == TransportEvent::Type::Message) back.push_back(e.data);
  REQUIRE(back.size() == 2);
  CHECK(back[0] == big);
  CHECK(back[1] == std::vector<uint8_t>{1, 2, 3});
  const ConnectionStats s = p.host.stats(p.toClient);
  CHECK(s.resent > 0);
  CHECK(s.queued == 0);
  CHECK(s.rttMs > 60);
  CHECK(s.rttMs < 400);
}

TEST_CASE("net: unreliable messages may be lost but never repeat") {
  TransportPair p;
  p.net.setConditions({10, 0, 0.3f, 0.3f});
  p.host.listen(true);
  p.toHost = p.client.connect(p.hs->localAddress());
  p.pump(1500);
  REQUIRE(p.client.connected(p.toHost));
  for (int i = 0; i < 100; ++i) {
    p.client.send(p.toHost, Channel::Unreliable, {static_cast<uint8_t>(i)});
    p.pump(20);
  }
  p.pump(200);
  std::vector<int> seen;
  for (const TransportEvent& e : p.atHost)
    if (e.type == TransportEvent::Type::Message && e.channel == Channel::Unreliable) seen.push_back(e.data[0]);
  CHECK(seen.size() < 100);
  CHECK(seen.size() > 40);
  std::sort(seen.begin(), seen.end());
  CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
  CHECK_FALSE(p.client.send(p.toHost, Channel::Unreliable, bytesOf(5000, 1)));  // too big for one datagram
}

TEST_CASE("net: connections are refused, time out and end") {
  SUBCASE("nobody listening") {
    TransportPair p;
    p.toHost = p.client.connect(p.hs->localAddress());
    p.pump(500);
    REQUIRE(p.atClient.size() == 1);
    CHECK(p.atClient[0].reason == DisconnectReason::Refused);
  }
  SUBCASE("nobody there") {
    TransportPair p;
    p.toHost = p.client.connect({loopbackHost(9), 47800});
    p.pump(7000);
    REQUIRE(p.atClient.size() == 1);
    CHECK(p.atClient[0].reason == DisconnectReason::NoAnswer);
  }
  SUBCASE("the cable comes out") {
    TransportPair p;
    p.host.listen(true);
    p.toHost = p.client.connect(p.hs->localAddress());
    p.pump(500);
    REQUIRE(p.client.connected(p.toHost));
    p.net.setOffline(loopbackHost(2), true);
    p.pump(9000);
    CHECK_FALSE(p.client.connected(p.toHost));
    CHECK(p.host.connections().empty());
    bool timedOut = false;
    for (const TransportEvent& e : p.atHost) timedOut = timedOut || e.reason == DisconnectReason::TimedOut;
    CHECK(timedOut);
  }
  SUBCASE("closed politely") {
    TransportPair p;
    p.host.listen(true);
    p.toHost = p.client.connect(p.hs->localAddress());
    p.pump(500);
    p.client.disconnect(p.toHost);
    p.pump(200);
    CHECK(p.host.connections().empty());
    CHECK(p.atHost.back().reason == DisconnectReason::Remote);
  }
  SUBCASE("another game's datagrams are ignored") {
    TransportPair p;
    p.host.listen(true);
    TransportConfig other;
    other.protocolId = 0x12345678;
    Transport stranger(*p.cs, other);
    stranger.connect(p.hs->localAddress());
    stranger.update(p.net.now());
    p.pump(500);
    CHECK(p.host.connections().empty());
    CHECK(p.host.strayDatagrams() > 0);
  }
}

TEST_CASE("net: real UDP on this computer") {
  UdpSocket a, b;
  std::string err;
  if (!a.open(0, false, err) || !b.open(0, false, err)) {
    MESSAGE("no UDP here: " << err);
    return;
  }
  Transport host(a), client(b);
  host.listen(true);
  const ConnId c = client.connect(Address::loopback(a.localAddress().port));
  std::vector<uint8_t> got;
  const std::vector<uint8_t> msg = bytesOf(20000, 3);
  bool sent = false;
  for (int i = 0; i < 400 && got.empty(); ++i) {
    const uint64_t now = static_cast<uint64_t>(i) * 10;
    host.update(now);
    client.update(now);
    TransportEvent e;
    while (host.poll(e))
      if (e.type == TransportEvent::Type::Message) got = e.data;
    while (client.poll(e)) {
    }
    if (client.connected(c) && !sent) sent = client.send(c, Channel::Reliable, msg);
  }
  CHECK(got == msg);
}

TEST_CASE("net: games on the local network are found") {
  LoopbackNetwork net(3);
  auto hostSock = net.open(loopbackHost(1), 47800);
  auto otherSock = net.open(loopbackHost(4), 47800);
  auto browseSock = net.open(loopbackHost(2), kDefaultDiscoveryPort);
  DiscoveryAnnouncer ann(*hostSock), annOther(*otherSock);
  DiscoveryBrowser browser(*browseSock, "legion");
  GameBeacon b;
  b.gameId = "legion";
  b.version = "0.9.2";
  b.name = "Erich's game";
  b.description = "Ardennes, 2 of 4";
  b.port = 47800;
  b.members = 2;
  b.maxMembers = 4;
  b.instance = 77;
  GameBeacon other = b;
  other.gameId = "street-racer";
  other.instance = 78;
  run(net, 1500, [&](uint64_t now) {
    ann.update(now, b);
    annOther.update(now, other);
    browser.update(now);
  });
  REQUIRE(browser.games().size() == 1);
  const GameBeacon& g = browser.games()[0];
  CHECK(g.name == "Erich's game");
  CHECK(g.description == "Ardennes, 2 of 4");
  CHECK(g.address == Address{loopbackHost(1), 47800});
  CHECK(g.members == 2);
  // The host goes away: forgotten after a few seconds.
  run(net, 4000, [&](uint64_t now) { browser.update(now); });
  CHECK(browser.games().empty());
}

// --- Sessions ----------------------------------------------------------------------

namespace {
struct Lan {
  LoopbackNetwork net{11};
  SessionConfig cfg;
  std::unique_ptr<DatagramSocket> hostSock = net.open(loopbackHost(1), kDefaultGamePort);
  std::vector<std::unique_ptr<DatagramSocket>> socks;
  Session host;
  std::vector<std::unique_ptr<Session>> clients;
  Lan() {
    cfg.gameId = "test";
    cfg.gameVersion = "1.0";
    cfg.contentHash = "abc";
    cfg.maxMembers = 4;
  }
  Session& join(const std::string& name, SessionConfig c, uint8_t hostNo) {
    socks.push_back(net.open(loopbackHost(hostNo), 0));
    clients.push_back(std::make_unique<Session>());
    std::string err;
    REQUIRE(clients.back()->join(*socks.back(), c, hostSock->localAddress(), name, err));
    return *clients.back();
  }
  void pump(int ms) {
    run(net, ms, [&](uint64_t now) {
      host.update(now);
      for (auto& c : clients) c->update(now);
    });
  }
};
}  // namespace

TEST_CASE("net: a lobby with names, settings, chat and refusals") {
  Lan lan;
  std::string err;
  REQUIRE(lan.host.host(*lan.hostSock, lan.cfg, "Erich", err));
  Session& a = lan.join("Ivan", lan.cfg, 2);
  Session& b = lan.join("Ivan", lan.cfg, 3);  // the same name twice
  SessionConfig old = lan.cfg;
  old.gameVersion = "0.9";
  Session& c = lan.join("Old", old, 4);
  SessionConfig mod = lan.cfg;
  mod.contentHash = "xyz";
  Session& d = lan.join("Modded", mod, 5);
  lan.pump(1500);
  CHECK(a.state() == SessionState::Lobby);
  CHECK(b.state() == SessionState::Lobby);
  CHECK(c.state() == SessionState::Closed);
  CHECK(c.closeReason().find("version 1.0") != std::string::npos);
  CHECK(d.state() == SessionState::Closed);
  CHECK(d.closeReason().find("rules or mods") != std::string::npos);
  REQUIRE(lan.host.members().size() == 3);
  CHECK(a.members().size() == 3);
  CHECK(b.localMember()->name == "Ivan (2)");
  CHECK(a.leader() == 0);
  CHECK(lan.host.isLeader());

  lan.host.setSettings("{\"map\":\"Ardennes\"}");
  a.setSettings("{\"map\":\"hacked\"}");  // not the leader: ignored
  a.setReady(true);
  a.setMemberData("{\"side\":1}");
  a.chat("Hello\x07 everyone");
  lan.host.chat("Welcome");
  lan.pump(500);
  CHECK(b.settings() == "{\"map\":\"Ardennes\"}");
  CHECK(lan.host.member(a.localId())->ready);
  CHECK(b.member(a.localId())->data == "{\"side\":1}");
  bool sawHello = false, sawWelcome = false, sawJoin = false;
  for (const ChatLine& l : b.chatLog()) {
    sawHello = sawHello || (l.text == "Hello everyone" && l.name == "Ivan");
    sawWelcome = sawWelcome || (l.text == "Welcome" && l.from == 0);
    sawJoin = sawJoin || (l.from == kNoMember && l.text.find("joined") != std::string::npos);
  }
  CHECK(sawHello);
  CHECK(sawWelcome);
  CHECK(sawJoin);

  // Leaving and being removed.
  b.leave();
  lan.pump(300);
  CHECK(lan.host.members().size() == 2);
  lan.host.kick(a.localId(), "testing");
  lan.pump(1500);
  CHECK(a.state() == SessionState::Closed);
  CHECK(a.closeReason().find("testing") != std::string::npos);
  CHECK(lan.host.members().size() == 1);
}

TEST_CASE("net: a password, a full game and a game already started") {
  Lan lan;
  lan.cfg.password = "tiger";
  lan.cfg.maxMembers = 2;  // the host and one more
  std::string err;
  REQUIRE(lan.host.host(*lan.hostSock, lan.cfg, "Host", err));
  SessionConfig wrong = lan.cfg;
  wrong.password = "panther";
  Session& a = lan.join("A", wrong, 2);
  Session& b = lan.join("B", lan.cfg, 3);
  lan.pump(1000);
  CHECK(a.closeReason() == "Wrong password");
  CHECK(b.state() == SessionState::Lobby);
  Session& c = lan.join("C", lan.cfg, 4);
  lan.pump(1000);
  CHECK(c.closeReason() == "The game is full");
  lan.host.setInGame(true);
  lan.pump(100);
  CHECK(b.state() == SessionState::InGame);
}

TEST_CASE("net: in a game, a lost player comes back to its own place") {
  Lan lan;
  lan.cfg.rejoinSeconds = 60;
  std::string err;
  REQUIRE(lan.host.host(*lan.hostSock, lan.cfg, "Host", err));
  Session& a = lan.join("A", lan.cfg, 2);
  lan.pump(500);
  const uint8_t id = a.localId();
  a.setMemberData("side 1");
  lan.host.setInGame(true);
  lan.pump(300);
  REQUIRE(a.state() == SessionState::InGame);
  std::vector<SessionEvent> clientEvents, hostEvents;
  a.addListener([&](const SessionEvent& e) { clientEvents.push_back(e); });
  lan.host.addListener([&](const SessionEvent& e) { hostEvents.push_back(e); });

  lan.net.setOffline(loopbackHost(2), true);
  lan.pump(12000);
  CHECK(a.state() == SessionState::Reconnecting);
  REQUIRE(lan.host.member(id));
  CHECK_FALSE(lan.host.member(id)->connected);
  lan.net.setOffline(loopbackHost(2), false);
  lan.pump(8000);
  CHECK(a.state() == SessionState::InGame);
  CHECK(a.localId() == id);
  CHECK(lan.host.member(id)->connected);
  CHECK(lan.host.member(id)->data == "side 1");
  auto has = [](const std::vector<SessionEvent>& v, SessionEvent::Type t) {
    return std::any_of(v.begin(), v.end(), [&](const SessionEvent& e) { return e.type == t; });
  };
  CHECK(has(clientEvents, SessionEvent::Type::Reconnecting));
  CHECK(has(clientEvents, SessionEvent::Type::Rejoined));
  CHECK(has(hostEvents, SessionEvent::Type::MemberLost));
  CHECK(has(hostEvents, SessionEvent::Type::MemberBack));

  // Gone for longer than the place is kept: removed.
  lan.cfg.rejoinSeconds = 60;
  lan.net.setOffline(loopbackHost(2), true);
  lan.pump(75000);
  CHECK(lan.host.member(id) == nullptr);
  CHECK(a.state() == SessionState::Closed);
}

TEST_CASE("net: a dedicated server has no player; the first to join leads") {
  Lan lan;
  std::string err;
  REQUIRE(lan.host.host(*lan.hostSock, lan.cfg, "", err));
  CHECK(lan.host.dedicated());
  Session& a = lan.join("A", lan.cfg, 2);
  Session& b = lan.join("B", lan.cfg, 3);
  lan.pump(1000);
  CHECK(lan.host.members().size() == 2);
  CHECK(a.isLeader());
  CHECK_FALSE(b.isLeader());
  a.setSettings("leader's map");
  b.setSettings("not mine");
  lan.pump(300);
  CHECK(b.settings() == "leader's map");
  a.leave();
  lan.pump(300);
  CHECK(b.isLeader());
}

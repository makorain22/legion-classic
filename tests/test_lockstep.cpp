// Lockstep: turns, delay, waiting, pausing, rejoining, desync detection and
// repair, a dedicated server - first with a toy battle, then a real Legion
// battle between two players over a lossy network.
#include <doctest/doctest.h>

#include <functional>
#include <map>
#include <memory>

#include "mission_battle.hpp"
#include "net_world.hpp"
#include "pioneer/core/bytes.hpp"
#include "pioneer/net/lockstep.hpp"
#include "pioneer/net/loopback.hpp"

using namespace pioneer;
using namespace pioneer::net;

namespace {
// A tiny deterministic "battle": two numbers mixed every tick and by every order.
struct Toy final : LockstepAdapter {
  uint32_t t = 0;
  uint64_t a = 1, b = 2;
  uint32_t corruptAt = 0;   // at this tick, b goes wrong here only
  int restored = 0;
  std::map<uint32_t, uint64_t> history;
  void apply(uint8_t m, const std::vector<uint8_t>& o) override {
    a = a * 1099511628211ull + m + 1;
    for (uint8_t x : o) a = (a ^ x) * 1099511628211ull;
  }
  void step() override {
    ++t;
    a ^= a >> 29;
    a *= 0xBF58476D1CE4E5B9ull;
    b += (a >> 7) | 1;
    if (t == corruptAt) b += 1;
    history[t] = hash();
  }
  uint32_t tick() const override { return t; }
  uint64_t hash() const override { return a ^ (b * 31) ^ t; }
  std::vector<uint8_t> snapshot() const override {
    ByteWriter w;
    w.u32(t);
    w.u64(a);
    w.u64(b);
    return w.bytes();
  }
  bool restore(const std::vector<uint8_t>& s, std::string& err) override {
    ByteReader r(s);
    t = r.u32();
    a = r.u64();
    b = r.u64();
    ++restored;
    if (!r.ok()) err = "short";
    return r.ok();
  }
  std::vector<std::pair<std::string, uint64_t>> digest() const override { return {{"A", a}, {"B", b}}; }
};

// The ticks two histories both have, and whether they agree on all of them.
size_t agree(const std::map<uint32_t, uint64_t>& x, const std::map<uint32_t, uint64_t>& y, uint32_t from = 0,
             bool* allSame = nullptr) {
  size_t common = 0;
  bool same = true;
  for (const auto& [t, h] : x) {
    if (t < from) continue;
    auto it = y.find(t);
    if (it == y.end()) continue;
    ++common;
    if (it->second != h) same = false;
  }
  if (allSame) *allSame = same;
  return common;
}

template <class Adapter>
struct Machine {
  std::unique_ptr<DatagramSocket> sock;
  Session session;
  std::unique_ptr<Adapter> world;
  std::unique_ptr<Lockstep> lock;
  bool frozen = false;  // a computer that stops running the game (but keeps its connection)
};

template <class Adapter>
struct Game {
  LoopbackNetwork net{5};
  SessionConfig cfg;
  LockstepConfig lcfg;
  Machine<Adapter> host;
  std::vector<std::unique_ptr<Machine<Adapter>>> players;
  std::function<std::unique_ptr<Adapter>()> make;

  explicit Game(std::function<std::unique_ptr<Adapter>()> mk) : make(std::move(mk)) {
    cfg.gameId = "test";
    cfg.gameVersion = "1";
    cfg.rejoinSeconds = 60;
  }
  void hostAs(const std::string& name) {
    host.sock = net.open(loopbackHost(1), kDefaultGamePort);
    host.world = make();
    host.lock = std::make_unique<Lockstep>(host.session, *host.world, lcfg);
    std::string err;
    REQUIRE(host.session.host(*host.sock, cfg, name, err));
  }
  Machine<Adapter>& join(const std::string& name) {
    auto m = std::make_unique<Machine<Adapter>>();
    m->sock = net.open(loopbackHost(static_cast<uint8_t>(2 + players.size())), 0);
    m->world = make();
    m->lock = std::make_unique<Lockstep>(m->session, *m->world, lcfg);
    std::string err;
    REQUIRE(m->session.join(*m->sock, cfg, host.sock->localAddress(), name, err));
    players.push_back(std::move(m));
    return *players.back();
  }
  void pump(int ms, const std::function<void(uint64_t)>& each = {}) {
    for (int t = 0; t < ms; t += 10) {
      net.advance(10);
      const uint64_t now = net.now();
      host.session.update(now);
      host.lock->advance(now);
      for (auto& p : players) {
        p->session.update(now);
        if (!p->frozen) p->lock->advance(now);
      }
      if (each) each(now);
    }
  }
};

std::vector<uint8_t> order(uint8_t x) { return {x, static_cast<uint8_t>(x * 3)}; }
}  // namespace

TEST_CASE("lockstep: three machines run the same battle through a bad line") {
  Game<Toy> g([] { return std::make_unique<Toy>(); });
  g.net.setConditions({35, 25, 0.05f, 0.01f});
  g.hostAs("Host");
  auto& a = g.join("A");
  auto& b = g.join("B");
  g.pump(1500);
  REQUIRE(a.session.state() == SessionState::Lobby);
  std::string err;
  REQUIRE(g.host.lock->start(err));
  int n = 0;
  g.pump(20000, [&](uint64_t now) {
    if (now % 230 == 0) a.lock->submit(order(static_cast<uint8_t>(++n)));
    if (now % 370 == 0) b.lock->submit(order(static_cast<uint8_t>(++n)));
    if (now % 510 == 0) g.host.lock->submit(order(static_cast<uint8_t>(++n)));
  });
  CHECK(a.world->restored == 1);
  CHECK(a.lock->phase() == LockstepPhase::Running);
  // About fifteen ticks a second for everyone.
  CHECK(g.host.world->t > 280);
  CHECK(g.host.world->t < 310);
  CHECK(a.world->t + 20 > g.host.world->t);
  CHECK(b.world->t + 20 > g.host.world->t);
  bool same = false;
  CHECK(agree(g.host.world->history, a.world->history, 0, &same) > 250);
  CHECK(same);
  CHECK(agree(g.host.world->history, b.world->history, 0, &same) > 250);
  CHECK(same);
  CHECK(g.host.lock->delay() >= 2);  // a 60-120 ms round trip needs a turn or two in hand
  CHECK(g.host.lock->stats().desyncs == 0);
  CHECK(a.lock->stats().ordersSent > 50);
}

TEST_CASE("lockstep: on a LAN the delay is one tick") {
  Game<Toy> g([] { return std::make_unique<Toy>(); });
  g.net.setConditions({1, 1, 0, 0});
  g.hostAs("Host");
  auto& a = g.join("A");
  g.pump(500);
  std::string err;
  REQUIRE(g.host.lock->start(err));
  g.pump(5000);
  CHECK(g.host.lock->delay() <= 2);
  bool same = false;
  CHECK(agree(g.host.world->history, a.world->history, 0, &same) > 60);
  CHECK(same);
}

TEST_CASE("lockstep: a slow player holds everyone; a lost one doesn't, and comes back") {
  Game<Toy> g([] { return std::make_unique<Toy>(); });
  g.net.setConditions({20, 5, 0, 0});
  g.hostAs("Host");
  auto& a = g.join("A");
  auto& b = g.join("B");
  g.pump(800);
  std::string err;
  REQUIRE(g.host.lock->start(err));
  g.pump(3000);

  // A's computer stops running the game: after two seconds, everyone waits for it.
  a.frozen = true;
  g.pump(4000);
  CHECK(g.host.lock->phase() == LockstepPhase::Waiting);
  REQUIRE(g.host.lock->waitingFor().size() == 1);
  CHECK(g.host.lock->waitingFor()[0] == a.session.localId());
  CHECK(b.lock->phase() == LockstepPhase::Waiting);
  const uint32_t held = g.host.world->t;
  g.pump(2000);
  CHECK(g.host.world->t <= held + 1);
  a.frozen = false;
  g.pump(3000);
  CHECK(g.host.lock->phase() == LockstepPhase::Running);
  CHECK(g.host.world->t > held + 20);

  // B's cable comes out: the game goes on without it...
  g.net.setOffline(loopbackHost(3), true);
  const uint32_t before = g.host.world->t;
  g.pump(15000);
  CHECK(g.host.world->t > before + 180);
  CHECK(b.session.state() == SessionState::Reconnecting);
  // ...and when it is back, it gets the battle as it stands and runs with the rest.
  g.net.setOffline(loopbackHost(3), false);
  g.pump(8000);
  CHECK(b.session.state() == SessionState::InGame);
  CHECK(b.world->restored == 2);
  CHECK(b.lock->phase() == LockstepPhase::Running);
  bool same = false;
  CHECK(agree(g.host.world->history, b.world->history, g.host.world->t - 60, &same) > 30);
  CHECK(same);
  CHECK(agree(g.host.world->history, a.world->history, 0, &same) > 300);
  CHECK(same);
}

TEST_CASE("lockstep: a desync is found, named and repaired") {
  Game<Toy> g([] { return std::make_unique<Toy>(); });
  g.net.setConditions({15, 5, 0.02f, 0});
  g.hostAs("Host");
  auto& a = g.join("A");
  g.pump(500);
  a.world->corruptAt = 70;
  std::vector<std::string> notices;
  a.lock->onDesync = [&](uint8_t m, uint32_t, const std::string& sys) {
    if (m == a.session.localId()) notices.push_back(sys);
  };
  std::string err;
  REQUIRE(g.host.lock->start(err));
  g.pump(15000);
  CHECK(g.host.lock->stats().desyncs == 1);
  CHECK(g.host.lock->stats().resyncs == 1);
  CHECK(g.host.lock->stats().lastDesyncTick == 75);  // the first check after it went wrong
  CHECK(g.host.lock->stats().lastDesyncSystems == "B");
  REQUIRE(notices.size() == 1);
  CHECK(notices[0] == "B");
  CHECK(a.world->restored == 2);
  bool same = false;
  CHECK(agree(g.host.world->history, a.world->history, 120, &same) > 50);
  CHECK(same);
}

TEST_CASE("lockstep: anyone pauses, anyone goes on") {
  Game<Toy> g([] { return std::make_unique<Toy>(); });
  g.hostAs("Host");
  auto& a = g.join("A");
  g.pump(500);
  std::string err;
  REQUIRE(g.host.lock->start(err));
  g.pump(2000);
  a.lock->setPaused(true);
  g.pump(300);
  CHECK(g.host.lock->phase() == LockstepPhase::Paused);
  CHECK(a.lock->phase() == LockstepPhase::Paused);
  CHECK(a.lock->pausedBy() == a.session.localId());
  const uint32_t t = g.host.world->t, ta = a.world->t;
  g.pump(3000);
  CHECK(g.host.world->t <= t + 2);
  CHECK(a.world->t <= ta + 2);
  g.host.lock->setPaused(false);
  g.pump(2000);
  CHECK(a.lock->phase() == LockstepPhase::Running);
  CHECK(a.world->t > ta + 20);
}

TEST_CASE("lockstep: a dedicated server takes the leader's battle") {
  Game<Toy> g([] { return std::make_unique<Toy>(); });
  g.net.setConditions({10, 5, 0.01f, 0});
  g.hostAs("");
  auto* a0 = &g.join("A");
  auto* b0 = &g.join("B");
  g.pump(800);
  if (b0->session.isLeader()) std::swap(a0, b0);
  auto& a = *a0;
  auto& b = *b0;
  REQUIRE(a.session.isLeader());
  std::string err;
  CHECK_FALSE(b.lock->start(err));  // only the leader
  a.world->t = 500;
  a.world->a = 12345;
  REQUIRE(a.lock->start(err));
  g.pump(6000, [&](uint64_t now) {
    if (now % 300 == 0) b.lock->submit(order(static_cast<uint8_t>(now / 300)));
  });
  CHECK(g.host.world->t > 560);
  CHECK(b.world->restored == 1);
  bool same = false;
  CHECK(agree(g.host.world->history, a.world->history, 0, &same) > 50);
  CHECK(same);
  CHECK(agree(g.host.world->history, b.world->history, 0, &same) > 50);
  CHECK(same);
}

// --- A real battle ---------------------------------------------------------------------

namespace {
const testkit::Rules& rules() {
  static testkit::Rules r;
  return r;
}

MapFile skirmishField() {
  MapFile f;
  f.map = Map(64, 64);
  f.map.name = "Test field";
  for (uint8_t side = 0; side < 2; ++side) {
    PlayerSetup p;
    p.name = side ? "Ivan" : "Erich";
    p.faction = side ? "rus" : "ger";
    p.team = side;
    p.active = true;
    f.players.push_back(p);
    const int x = side ? 50 : 14;
    for (int i = 0; i < 8; ++i)
      f.units.push_back({side ? "rus_rifleman" : "ger_rifleman", side, (x + i % 4) * 256 + 128, (28 + i / 4) * 256 + 128});
    f.units.push_back({side ? "rus_medium_tank" : "ger_medium_tank", side, (x + 1) * 256 + 128, 34 * 256 + 128});
  }
  return f;
}

std::vector<Entity> unitsOf(legion::NetWorld& w, uint8_t owner) {
  std::vector<Entity> out;
  Registry& reg = w.sim()->registry();
  for (uint32_t e : reg.pool<CUnit>().entities())
    if (reg.pool<CUnit>().get(e).owner == owner) out.push_back(reg.handle(e));
  return out;
}

// Records the state hash once a second after each tick.
struct Recorded final : LockstepAdapter {
  legion::NetWorld world{rules().types, rules().weapons};
  std::map<uint32_t, uint64_t> history;
  int restored = 0;
  void apply(uint8_t m, const std::vector<uint8_t>& o) override { world.apply(m, o); }
  void step() override {
    world.step();
    if (world.tick() % 15 == 0) history[world.tick()] = world.hash();
  }
  uint32_t tick() const override { return world.tick(); }
  uint64_t hash() const override { return world.hash(); }
  std::vector<uint8_t> snapshot() const override { return world.snapshot(); }
  bool restore(const std::vector<uint8_t>& s, std::string& e) override {
    ++restored;
    return world.restore(s, e);
  }
  std::vector<std::pair<std::string, uint64_t>> digest() const override { return world.digest(); }
};
}  // namespace

TEST_CASE("lockstep: two players fight a Legion battle over a lossy line, and a desync is repaired") {
  Game<Recorded> g([] { return std::make_unique<Recorded>(); });
  g.net.setConditions({30, 20, 0.04f, 0.01f});
  g.hostAs("Erich");
  auto& ivan = g.join("Ivan");
  g.pump(1000);
  REQUIRE(ivan.session.localId() == 1);
  std::string err;
  REQUIRE(g.host.world->world.loadMapFile(skirmishField(), err));
  g.host.world->world.setSeats({{0, 0}, {1, 1}});
  REQUIRE(g.host.lock->start(err));
  g.pump(1000);
  REQUIRE(ivan.world->restored == 1);
  CHECK(ivan.world->world.seats().at(1) == 1);
  REQUIRE(ivan.world->world.sim());

  auto attack = [](legion::NetWorld& w, uint8_t side) {
    Command c;
    c.type = CommandType::AttackMove;
    c.units = unitsOf(w, side);
    c.target = {(side ? 14 : 50) * 256, 30 * 256};
    return legion::encodeOrder(c);
  };
  g.host.lock->submit(attack(g.host.world->world, 0));
  ivan.lock->submit(attack(ivan.world->world, 1));
  // Ivan tries to cheat: spawn tanks, and order Erich's men away. Both are dropped everywhere.
  Command spawn;
  spawn.type = CommandType::Spawn;
  spawn.unitType = static_cast<uint16_t>(rules().types.find("rus_heavy_tank") >= 0 ? rules().types.find("rus_heavy_tank")
                                                                                     : rules().types.find("rus_medium_tank"));
  spawn.count = 10;
  spawn.target = {50 * 256, 30 * 256};
  ivan.lock->submit(legion::encodeOrder(spawn));
  Command steal;
  steal.type = CommandType::Move;
  steal.units = unitsOf(ivan.world->world, 0);
  steal.target = {60 * 256, 60 * 256};
  ivan.lock->submit(legion::encodeOrder(steal));

  g.pump(40000);
  CHECK(g.host.world->world.ordersApplied() == 2);
  CHECK(g.host.world->world.ordersDropped() == 2);
  CHECK(ivan.world->world.ordersDropped() == 2);
  bool same = false;
  CHECK(agree(g.host.world->history, ivan.world->history, 0, &same) > 30);
  CHECK(same);
  // The fight happened.
  CHECK(unitsOf(g.host.world->world, 0).size() + unitsOf(g.host.world->world, 1).size() < 18);
  CHECK(g.host.lock->stats().desyncs == 0);

  // A broken machine: one of Ivan's units loses health on his computer only.
  std::vector<Entity> his = unitsOf(ivan.world->world, 1);
  if (his.empty()) his = unitsOf(ivan.world->world, 0);
  REQUIRE_FALSE(his.empty());
  ivan.world->world.sim()->registry().get<CUnit>(his.front()).health -= 1;
  const uint32_t at = ivan.world->world.tick();
  g.pump(14000);
  CHECK(g.host.lock->stats().desyncs == 1);
  CHECK(g.host.lock->stats().resyncs == 1);
  CHECK(g.host.lock->stats().lastDesyncSystems.find("ECS") != std::string::npos);  // units' health lives there
  MESSAGE("desync named: " << g.host.lock->stats().lastDesyncSystems);
  CHECK(ivan.world->restored == 2);
  CHECK(agree(g.host.world->history, ivan.world->history, at + 90, &same) > 5);
  CHECK(same);
}

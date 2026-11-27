// Call-ins from outside the battlefield (Milestone 9.14): air strikes, recon,
// supply and paratroop drops, naval guns and landings.
#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include "pioneer/core/bytes.hpp"
#include "pioneer/sim/script.hpp"
#include "pioneer/sim/simulation.hpp"

using namespace pioneer;

namespace {
struct World {
  Map map{64, 64, 300};
  WeaponRegistry weapons;
  UnitTypeRegistry types;
  explicit World(bool sea = false) {
    std::string err;
    REQUIRE(weapons.loadJson(nlohmann::json::parse(R"({
      "warheads": {"bullet": {"infantry":100,"light":30,"medium":6,"heavy":3,"structure":5},
                   "he": {"infantry":100,"light":80,"medium":40,"heavy":25,"structure":60}},
      "weapons": [
        {"id":"rifle","warhead":"bullet","damage":14,"range":4.5,"cooldown":22,"accuracy":100},
        {"id":"bomb","warhead":"he","damage":300,"range":1,"cooldown":1,"projectile":"shell","speed":120,"splash":1.5},
        {"id":"shell","warhead":"he","damage":300,"range":60,"cooldown":1,"projectile":"shell","speed":160,"splash":1.5},
        {"id":"flak","warhead":"bullet","damage":10,"range":10,"cooldown":5,"accuracy":60,"antiAir":true}
      ]})"), err));
    REQUIRE(types.loadJson(nlohmann::json::parse(R"({"units":[
      {"id":"rifle","model":"rifleman","speed":12,"turnTicks":0,"radius":40,"health":100,"armor":"infantry",
       "sight":6,"weapons":["rifle"],"locomotor":"foot"},
      {"id":"post","model":"post","armor":"structure","footprint":[2,2],"health":500,"sight":4},
      {"id":"flakgun","model":"flak","armor":"structure","footprint":[1,1],"health":300,"sight":10,"weapons":["flak"]}
    ]})"), err));
    REQUIRE(types.resolveWeapons(weapons, err));
    REQUIRE(types.resolveLinks(err));
    REQUIRE_MESSAGE(types.support().loadJson(nlohmann::json::parse(R"([
      {"id":"bombs","name":"Bombing raid","kind":"bombing","requires":["post"],"cost":500,"charges":1,"recharge":10,
       "cooldown":2,"planes":3,"shots":4,"weapon":"bomb","length":8,"speed":100,"aaChance":100},
      {"id":"recon","kind":"recon","requires":["post"],"charges":2,"recharge":5,"radius":6,"duration":20},
      {"id":"drop","kind":"paradrop","requires":["post"],"recharge":5,"planes":2,"radius":1.5,"units":["rifle*6"]},
      {"id":"crates","kind":"supply","requires":["post"],"recharge":5,"planes":2,"crates":3,"crateValue":250,"radius":0.5},
      {"id":"ship","kind":"naval","requires":["post"],"recharge":5,"shots":6,"weapon":"shell","radius":2,"duration":6,
       "seaRange":12},
      {"id":"boats","kind":"landing","recharge":5,"speed":40,"seaRange":20,"units":["rifle*5"]}
    ])"), err), err);
    REQUIRE_MESSAGE(types.support().resolve(types, weapons, err), err);
    if (sea) {
      // The west quarter of the map is open sea, deep, with a sandy shore.
      map.setWaterLevel(260);
      for (int cy = 0; cy < map.cornersY(); ++cy)
        for (int cx = 0; cx <= 18; ++cx) map.setCorner(cx, cy, static_cast<uint16_t>(cx <= 13 ? 150 : 225 + (cx - 13) * 15));
      map.recomputeInfo();
    }
  }
  int power(const char* id) const {
    const int i = types.support().find(id);
    REQUIRE(i >= 0);
    return i;
  }
};

Vec2i tile(int x, int y) { return {x * 256 + 128, y * 256 + 128}; }

void spawn(Simulation& sim, int type, uint8_t player, Vec2i at, uint16_t count = 1) {
  Command c;
  c.type = CommandType::Spawn;
  c.unitType = static_cast<uint16_t>(type);
  c.player = player;
  c.target = at;
  c.count = count;
  sim.queue(c);
}
void credits(Simulation& sim, uint8_t player, int32_t amount) {
  Command c;
  c.type = CommandType::SetCredits;
  c.player = player;
  c.amount = amount;
  sim.queue(c);
}
void call(Simulation& sim, uint8_t player, int power, Vec2i at) {
  Command c;
  c.type = CommandType::CallSupport;
  c.player = player;
  c.unitType = static_cast<uint16_t>(power);
  c.target = at;
  sim.queue(c);
}
int countOwner(Simulation& sim, uint8_t owner, int type = -1) {
  int n = 0;
  for (uint32_t e : sim.registry().pool<CUnit>().entities()) {
    const CUnit& u = sim.registry().pool<CUnit>().get(e);
    if (u.owner == owner && (type < 0 || u.type == type)) ++n;
  }
  return n;
}
void run(Simulation& sim, int ticks) {
  for (int i = 0; i < ticks; ++i) sim.tick();
}
// A side with a post at its home corner, rich, and every call-in charged.
void setUp(Simulation& sim, World& w, uint8_t player, Vec2i home) {
  spawn(sim, w.types.find("post"), player, home);
  credits(sim, player, 10000);
  run(sim, 2);
}
}  // namespace

TEST_CASE("call-ins load from the rules; bad ones are caught") {
  World w;
  CHECK(w.types.support().size() == 6);
  const SupportPowerDef& b = w.types.support().get(w.power("bombs"));
  CHECK(b.kind == SupportKind::Bombing);
  CHECK(b.aircraft == AircraftRole::Bomber);
  CHECK(b.length == 8 * 256);
  CHECK(b.weapon == w.weapons.find("bomb"));
  CHECK(w.types.support().get(w.power("drop")).aircraft == AircraftRole::Transport);
  CHECK(w.types.support().get(w.power("drop")).units.size() == 1);

  std::string err;
  SupportRegistry bad;
  CHECK_FALSE(bad.loadJson(nlohmann::json::parse(R"([{"id":"x","kind":"flyover"}])"), err));  // set pieces: scripts only
  REQUIRE(bad.loadJson(nlohmann::json::parse(R"([{"id":"x","kind":"bombing"}])"), err));
  CHECK_FALSE(bad.resolve(w.types, w.weapons, err));
  CHECK(err.find("weapon") != std::string::npos);
  REQUIRE(bad.loadJson(nlohmann::json::parse(R"([{"id":"x","kind":"paradrop","units":["tank*2"]}])"), err));
  CHECK_FALSE(bad.resolve(w.types, w.weapons, err));

  std::vector<std::pair<int, int>> list;
  CHECK(parseUnitList("rifle*3, rifle", w.types, list, nullptr));
  CHECK(list.size() == 2);
  CHECK(list[0].second == 3);
  CHECK_FALSE(parseUnitList("post*2", w.types, list, &err));  // structures don't land
  CHECK_FALSE(parseUnitList("rifle*x", w.types, list, &err));
}

TEST_CASE("call-ins open with their liaison post, recharge, and cost supplies") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 3);
  const int bombs = w.power("bombs");
  credits(sim, 0, 10000);
  run(sim, 2);
  std::string why;
  CHECK_FALSE(sim.supportOpen(0, bombs));
  CHECK_FALSE(sim.canCallSupport(0, bombs, tile(40, 40), &why));
  CHECK(why.find("liaison") != std::string::npos);
  CHECK_FALSE(sim.supportOpen(0, w.power("boats")));  // no requirement: only a script opens it

  spawn(sim, w.types.find("post"), 0, tile(5, 5));
  run(sim, 16);
  CHECK(sim.supportOpen(0, bombs));
  CHECK(sim.supportState(0, bombs).charges == 0);
  CHECK_FALSE(sim.canCallSupport(0, bombs, tile(40, 40), &why));
  CHECK(why.find("recharging") != std::string::npos);
  bool ready = false;
  for (int i = 0; i < 15 * 11 && !ready; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events()) ready = ready || (e.type == EventType::SupportReady && e.unitType == bombs);
  }
  CHECK(ready);
  CHECK(sim.supportState(0, bombs).charges == 1);
  CHECK(sim.canCallSupport(0, bombs, tile(40, 40)));
  CHECK_FALSE(sim.canCallSupport(0, bombs, {-5, 40}, &why));

  call(sim, 0, bombs, tile(40, 40));
  sim.tick();
  CHECK(sim.playerState(0).credits == 10000 - 500);
  CHECK(sim.supportState(0, bombs).charges == 0);
  CHECK(sim.supportState(0, bombs).used == 1);
  CHECK(sim.strikes().size() == 1);

  // A script can close it, or open one with no post.
  sim.setSupportGate(0, bombs, SupportState::Gate::Off);
  CHECK_FALSE(sim.supportOpen(0, bombs));
  sim.setSupportGate(0, w.power("boats"), SupportState::Gate::On);
  CHECK(sim.supportOpen(0, w.power("boats")));
  sim.setSupportCharges(0, bombs, 9);
  CHECK(sim.supportState(0, bombs).charges == 1);  // capped at the call-in's charges
}

TEST_CASE("bombers fly in from the caller's side and wreck what is under the line") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 5);
  setUp(sim, w, 0, tile(4, 32));
  spawn(sim, w.types.find("rifle"), 1, tile(40, 32), 6);
  run(sim, 2);
  const int bombs = w.power("bombs");
  sim.setSupportCharges(0, bombs, 1);
  run(sim, 1);
  REQUIRE(sim.canCallSupport(0, bombs, tile(40, 32)));
  call(sim, 0, bombs, tile(40, 32));
  sim.tick();
  REQUIRE(sim.strikes().size() == 1);
  const Strike s = sim.strikes()[0];
  CHECK(s.from.x < 0);        // from the west, where the caller's post is
  CHECK(s.to.x > 64 * 256);   // and out the far side
  CHECK(s.planes == 3);
  int32_t x, y;
  CHECK_FALSE(s.planeAt(0, s.startTick - 1, x, y));
  CHECK(s.planeAt(0, s.startTick + 10, x, y));
  const int before = countOwner(sim, 1);
  bool done = false;
  for (int i = 0; i < 15 * 40 && !done; ++i) {
    sim.tick();
    done = sim.strikes().empty();
  }
  CHECK(done);
  run(sim, 40);  // the last bombs fall
  CHECK(countOwner(sim, 1) < before);
}

TEST_CASE("flak brings planes down and drives the raid off") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 7);
  setUp(sim, w, 0, tile(4, 32));
  // A ring of guns round the target, all with a sure shot (aaChance 100).
  for (int k = 0; k < 4; ++k) spawn(sim, w.types.find("flakgun"), 1, tile(26 + k * 2, 30));
  run(sim, 2);
  const int bombs = w.power("bombs");
  sim.setSupportCharges(0, bombs, 1);
  run(sim, 1);
  call(sim, 0, bombs, tile(40, 32));
  sim.tick();
  bool fired = false, down = false, aborted = false;
  for (int i = 0; i < 15 * 40 && !sim.strikes().empty(); ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events()) {
      fired = fired || e.type == EventType::FlakFire;
      down = down || e.type == EventType::AircraftDown;
    }
    if (!sim.strikes().empty()) aborted = aborted || sim.strikes()[0].aborted;
  }
  CHECK(fired);
  CHECK(down);
  CHECK(aborted);
  CHECK_FALSE(sim.downedPlanes().empty());
}

TEST_CASE("recon shows the target; paratroopers land; crates go to whoever gets there first") {
  World w;
  Simulation sim(w.map, w.types, w.weapons, 11);
  setUp(sim, w, 0, tile(4, 4));
  spawn(sim, w.types.find("rifle"), 1, tile(50, 50));
  run(sim, 2);
  const int recon = w.power("recon"), drop = w.power("drop"), crates = w.power("crates");
  for (int p : {recon, drop, crates}) sim.setSupportCharges(0, p, 1);
  run(sim, 1);
  const int rifle = w.types.find("rifle");

  CHECK_FALSE(sim.vision().visible(0, 50, 50));
  call(sim, 0, recon, tile(50, 50));
  bool seen = false;
  for (int i = 0; i < 15 * 30 && !seen; ++i) {
    sim.tick();
    seen = sim.vision().visible(0, 50, 50);
  }
  CHECK(seen);

  const int before = countOwner(sim, 0, rifle);
  call(sim, 0, drop, tile(30, 30));
  bool landed = false;
  for (int i = 0; i < 15 * 40 && !landed; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events()) landed = landed || e.type == EventType::TroopsLanded;
  }
  CHECK(landed);
  run(sim, 15 * 5);
  CHECK(countOwner(sim, 0, rifle) == before + 6);

  // The enemy's man stands where the crates come down: he takes them.
  spawn(sim, rifle, 1, tile(20, 44), 4);
  run(sim, 2);
  const int32_t enemy = sim.playerState(1).credits;
  call(sim, 0, crates, tile(20, 44));
  bool taken = false;
  size_t most = 0;
  for (int i = 0; i < 15 * 40; ++i) {
    sim.tick();
    most = std::max(most, sim.crates().size());
    for (const SimEvent& e : sim.events()) taken = taken || (e.type == EventType::CrateTaken && e.owner == 1);
  }
  CHECK(most == 3);
  CHECK(taken);
  CHECK(sim.playerState(1).credits > enemy);
}

TEST_CASE("ships only reach targets near the open sea; landing craft put men on the beach") {
  World w(true);
  Simulation sim(w.map, w.types, w.weapons, 13);
  setUp(sim, w, 0, tile(40, 4));
  const int ship = w.power("ship"), boats = w.power("boats");
  sim.setSupportCharges(0, ship, 1);
  sim.setSupportGate(0, boats, SupportState::Gate::On);
  sim.setSupportCharges(0, boats, 1);
  run(sim, 1);
  CHECK(sim.seaDistance(tile(2, 30)) == 0);
  CHECK(sim.seaDistance(tile(20, 30)) > 0);
  CHECK(sim.seaDistance(tile(20, 30)) <= 12);
  std::string why;
  CHECK(sim.canCallSupport(0, ship, tile(20, 30)));
  CHECK_FALSE(sim.canCallSupport(0, ship, tile(60, 30), &why));
  CHECK(why.find("sea") != std::string::npos);

  Vec2i sea, beach;
  REQUIRE(sim.findLanding(tile(24, 30), 20, sea, beach));
  CHECK(sea.x < beach.x);
  CHECK(sim.seaDistance(beach) == 1);

  call(sim, 0, ship, tile(20, 30));
  sim.tick();
  REQUIRE(sim.strikes().size() == 1);
  CHECK(sim.seaDistance(sim.strikes()[0].from) == 0);  // the ship lies at sea
  const int rifle = w.types.find("rifle");
  const int before = countOwner(sim, 0, rifle);
  call(sim, 0, boats, tile(24, 30));
  int landed = -1;
  for (int i = 0; i < 15 * 60 && landed < 0; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events())
      if (e.type == EventType::TroopsLanded) landed = e.amount;
  }
  CHECK(landed == 5);
  CHECK(countOwner(sim, 0, rifle) == before + 5);
}

TEST_CASE("a save made mid-strike plays on the same; call-ins are deterministic") {
  auto play = [](bool reload) {
    World w(true);
    Simulation sim(w.map, w.types, w.weapons, 17);
    setUp(sim, w, 0, tile(40, 4));
    spawn(sim, w.types.find("rifle"), 1, tile(30, 40), 8);
    spawn(sim, w.types.find("flakgun"), 1, tile(34, 44));
    run(sim, 2);
    for (const char* id : {"bombs", "drop", "crates", "ship"}) sim.setSupportCharges(0, w.power(id), 1);
    run(sim, 1);
    call(sim, 0, w.power("bombs"), tile(30, 40));
    call(sim, 0, w.power("drop"), tile(36, 30));
    call(sim, 0, w.power("crates"), tile(28, 20));
    call(sim, 0, w.power("ship"), tile(22, 40));
    run(sim, 15 * 6);
    if (reload) {
      ByteWriter bw;
      sim.saveState(bw);
      Map copy = w.map;
      Simulation other(copy, w.types, w.weapons, 99);
      ByteReader br(bw.bytes());
      std::string err;
      REQUIRE_MESSAGE(other.loadState(br, err), err);
      CHECK(other.strikes().size() == sim.strikes().size());
      CHECK(other.stateHash() == sim.stateHash());
      run(other, 15 * 30);
      return other.stateHash();
    }
    run(sim, 15 * 30);
    return sim.stateHash();
  };
  const uint64_t a = play(false);
  CHECK(a == play(false));
  CHECK(a == play(true));
}

TEST_CASE("scripts open call-ins, call them free, and stage landings and fly-overs") {
  World w(true);
  Simulation sim(w.map, w.types, w.weapons, 19);
  setUp(sim, w, 0, tile(40, 4));
  const int rifle = w.types.find("rifle");
  // A landing straight from a script: no call-in needed.
  const uint32_t id = sim.scriptLanding(0, tile(24, 40), {{rifle, 3}});
  CHECK(id != 0);
  const uint32_t fly = sim.startSetPiece(SupportKind::Flyover, 1, tile(32, 32), {1, 0}, 4, 0, AircraftRole::Bomber);
  CHECK(fly != 0);
  const uint32_t crash = sim.startSetPiece(SupportKind::Crash, 1, tile(32, 20), {0, 1}, 1, 0, AircraftRole::Fighter);
  CHECK(crash != 0);
  const int before = countOwner(sim, 0, rifle);
  bool down = false;
  for (int i = 0; i < 15 * 60; ++i) {
    sim.tick();
    for (const SimEvent& e : sim.events()) down = down || e.type == EventType::AircraftDown;
  }
  CHECK(down);
  CHECK(countOwner(sim, 0, rifle) == before + 3);
  CHECK(sim.strikes().empty());

  // The action and condition names stay put: missions store them.
  CHECK(std::string(actionId(ActionType::SupportCall)) == "support_call");
  CHECK(std::string(actionId(ActionType::Landing)) == "landing");
  CHECK(std::string(condId(CondType::SupportReady)) == "support_ready");
}

#pragma once
// Legion Classic - multiplayer: hosting and joining, the lobby, the battle
// over the lockstep, chat and what the player sees of the network.
//
// The lobby works with the skirmish setup. The leader (the host's player, or
// the first player on a dedicated server) owns the settings: the map and the
// four sides (closed, open for a player, or a computer). Players choose an
// open side and its army, and say when they are ready. When everyone is,
// the leader starts: its game builds the battle the way a skirmish starts
// and the battle goes to everyone (net/lockstep.hpp). From then on every
// order a player gives goes through the network (GameScene::issue).
//
// Rules in a multiplayer battle: no pausing alone (pause is for everyone),
// no game speed, no editor, no saving, no cheats from the console; orders
// are checked on every machine (net_world.hpp).

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game_scene.hpp"
#include "net_world.hpp"
#include "pioneer/net/discovery.hpp"
#include "pioneer/net/lockstep.hpp"
#include "pioneer/net/session.hpp"

namespace legion {

// A member's choice in the lobby (its session data, as JSON).
struct SeatChoice {
  int side = -1;  // -1: watching
  std::string faction = "ger";
};

class NetGame {
 public:
  explicit NetGame(GameScene& scene);
  ~NetGame();

  bool host(const std::string& name, uint16_t port, const std::string& password, std::string& error);
  bool join(const std::string& address, const std::string& name, const std::string& password, std::string& error);
  void leave(const std::string& why = "left");
  // The LAN list (listening for beacons) while the multiplayer page is open.
  void browse(bool on);
  const std::vector<pioneer::net::GameBeacon>& lanGames() const;

  // Every frame: the network. Every tick: the battle (true when the lockstep drives it).
  void update();
  bool tick();

  pioneer::net::Session& session() { return session_; }
  pioneer::net::Lockstep* lockstep() { return lock_.get(); }
  bool active() const;     // in a session (connecting, lobby, game, reconnecting)
  bool inLobby() const;
  bool inBattle() const;   // the lockstep runs the scene's battle
  const std::string& lastError() const { return lastError_; }

  // --- The lobby -------------------------------------------------------------
  SkirmishSetup setup() const;           // the leader's settings
  void setSetup(const SkirmishSetup& s); // leader only
  SeatChoice choiceOf(uint8_t member) const;
  void choose(const SeatChoice& c);
  bool canStart(std::string& why) const;
  bool start(std::string& error);        // leader only

  // --- In the battle ---------------------------------------------------------
  void submit(const pioneer::Command& c);
  void togglePause();
  void openChat();
  bool chatOpen() const { return chatOpen_; }
  void drawOverlay();
  // The seats in this battle and this machine's side (-1: watching).
  const Seats& seats() const { return seats_; }

  // --net-selftest: a second player inside this process (headless, over real UDP).
  void startSelfTest(bool dedicated = false);

 private:
  class Adapter;
  friend class Adapter;
  // The adapter's work, on the scene.
  void applyOrder(uint8_t member, const std::vector<uint8_t>& order);
  void stepBattle();
  std::vector<uint8_t> snapshotBattle() const;
  bool restoreBattle(const std::vector<uint8_t>& bytes, std::string& error);
  std::string memberName(uint8_t m) const;
  void makeLockstep();
  void onEvent(const pioneer::net::SessionEvent& e);
  void restored(int why);
  void say(const std::string& text, float seconds = 4.0f);
  pioneer::net::SessionConfig config(const std::string& password) const;
  void selfTestStep();

  GameScene& scene_;
  pioneer::net::UdpSocket sock_;
  pioneer::net::Session session_;
  std::unique_ptr<Adapter> adapter_;
  std::unique_ptr<pioneer::net::Lockstep> lock_;
  std::unique_ptr<pioneer::net::UdpSocket> browseSock_;
  std::unique_ptr<pioneer::net::DiscoveryBrowser> browser_;
  std::unique_ptr<pioneer::net::DiscoveryAnnouncer> announcer_;
  uint64_t instance_ = 0;
  Seats seats_;
  bool battle_ = false;
  bool chatOpen_ = false, chatFocus_ = false;
  char chatBuf_[256] = "";
  struct Line {
    std::string text;
    float shown;
  };
  std::vector<Line> lines_;
  std::string lastError_;
  int desyncs_ = 0;
  std::map<uint32_t, uint64_t> history_;  // the state hash once a second (self-test)

  // The self-test's second player.
  struct Bot;
  std::unique_ptr<Bot> bot_;
};

// Monotonic milliseconds, for the network clock.
uint64_t netClockMs();

}  // namespace legion

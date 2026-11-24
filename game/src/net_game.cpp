// Legion Classic - multiplayer (see net_game.hpp).
#include "net_game.hpp"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>
#include <random>

#include "menus.hpp"
#include "pioneer/core/bytes.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/sim/savefile.hpp"

using namespace pioneer;
using namespace pioneer::net;

namespace legion {

uint64_t netClockMs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// --- The scene as the lockstep sees it ----------------------------------------------

class NetGame::Adapter final : public LockstepAdapter {
 public:
  explicit Adapter(NetGame& n) : n_(n) {}
  void apply(uint8_t m, const std::vector<uint8_t>& o) override { n_.applyOrder(m, o); }
  void step() override { n_.stepBattle(); }
  uint32_t tick() const override { return n_.scene_.sim_ ? n_.scene_.sim_->tickCount() : 0; }
  uint64_t hash() const override { return n_.scene_.sim_ ? n_.scene_.sim_->stateHash() : 0; }
  std::vector<uint8_t> snapshot() const override { return n_.snapshotBattle(); }
  bool restore(const std::vector<uint8_t>& b, std::string& e) override { return n_.restoreBattle(b, e); }
  std::vector<std::pair<std::string, uint64_t>> digest() const override { return digestOf(*n_.scene_.sim_); }

 private:
  NetGame& n_;
};

void NetGame::applyOrder(uint8_t member, const std::vector<uint8_t>& order) {
  Command c;
  if (prepareOrder(*scene_.sim_, seats_, member, order, c)) scene_.sim_->queue(std::move(c));
}

void NetGame::stepBattle() {
  scene_.simTick();
  const uint32_t t = scene_.sim_->tickCount();
  if (t % 15 == 0) {
    history_[t] = scene_.sim_->stateHash();
    while (history_.size() > 600) history_.erase(history_.begin());
  }
}

std::vector<uint8_t> GameScene::netSnapshot() const {
  SaveFile f;
  f.metaJson = writeSeats(metaJson(map_.name + " (multiplayer)"), net_ ? net_->seats() : Seats{});
  f.mapBytes = encodeMap(map_, {}, playerList(), scenarioJson());
  ByteWriter w;
  sim_->saveState(w);
  f.stateBytes = std::move(w.bytes());
  return encodeSaveFile(f, false);
}

bool GameScene::netRestore(const std::vector<uint8_t>& bytes, std::string& error) {
  SaveFile f;
  if (!decodeSaveFile(bytes, f, false, error)) return false;
  autosaveReplay();
  if (!loadFromSave(f, false, error)) return false;
  menus_->close();
  paused_ = false;
  speed_ = 1.0f;
  revealMap_ = false;
  return true;
}

std::vector<uint8_t> NetGame::snapshotBattle() const { return scene_.netSnapshot(); }

bool NetGame::restoreBattle(const std::vector<uint8_t>& bytes, std::string& error) {
  SaveFile f;
  if (!decodeSaveFile(bytes, f, false, error)) return false;
  seats_ = readSeats(f.metaJson);
  if (!scene_.netRestore(bytes, error)) return false;
  auto mine = seats_.find(session_.localId());
  if (mine != seats_.end()) {
    scene_.player_ = mine->second;
  } else {
    scene_.revealMap_ = true;  // watching: see everything
  }
  scene_.centerOn({scene_.players_[scene_.player_].startX, scene_.players_[scene_.player_].startY});
  return true;
}

bool GameScene::netKey(const SDL_KeyboardEvent& k) {
  if (k.key == SDLK_RETURN || k.key == SDLK_KP_ENTER) {
    net_->openChat();
    return true;
  }
  if (settings_.pressed(InputAction::Pause, k)) {
    net_->togglePause();
    return true;
  }
  if (settings_.pressed(InputAction::SpeedUp, k) || settings_.pressed(InputAction::SpeedDown, k)) {
    showStatus("The game speed is fixed in a multiplayer game", 3);
    return true;
  }
  if (settings_.pressed(InputAction::Editor, k)) {
    showStatus("The editor is closed in a multiplayer game", 3);
    return true;
  }
  return settings_.pressed(InputAction::NextSide, k);  // no looking at the other sides' forces
}

// --- Sessions --------------------------------------------------------------------------

NetGame::NetGame(GameScene& scene) : scene_(scene) {
  std::random_device rd;
  instance_ = (static_cast<uint64_t>(rd()) << 32) ^ rd();
  adapter_ = std::make_unique<Adapter>(*this);
  makeLockstep();
  session_.addListener([this](const SessionEvent& e) {
    if (e.type == SessionEvent::Type::Closed) battle_ = false;
  });
}

NetGame::~NetGame() {
  lock_.reset();
  session_.leave("closed the game");
}

void NetGame::makeLockstep() {
  lock_.reset();  // first: the old one takes its message handlers with it
  lock_ = std::make_unique<Lockstep>(session_, *adapter_);
  lock_->setFilter([this](uint8_t member, std::vector<uint8_t>& order) {
    Command c;
    return decodeOrder(order, c) && orderAllowed(c.type) && seats_.count(member) > 0;
  });
  lock_->onRestored = [this](int why) { restored(why); };
  lock_->onDesync = [this](uint8_t m, uint32_t, const std::string& systems) {
    ++desyncs_;
    say("Out of step with " + memberName(m) + (systems.empty() ? "" : " (" + systems + ")") +
            (session_.isHost() ? ": sending the battle again" : ""),
        6);
  };
}

SessionConfig NetGame::config(const std::string& password) const {
  SessionConfig c;
  c.gameId = kNetGameId;
  c.gameVersion = netVersion();
  c.contentHash = scene_.rulesHash_;
  c.password = password;
  c.maxMembers = 8;
  return c;
}

bool NetGame::host(const std::string& name, uint16_t port, const std::string& password, std::string& error) {
  leave("left");
  if (!sock_.open(port, true, error)) return false;
  if (!session_.host(sock_, config(password), name, error)) return false;
  announcer_ = std::make_unique<DiscoveryAnnouncer>(sock_, kDefaultDiscoveryPort);
  lastError_.clear();
  lines_.clear();
  // The host plays the first open side to begin with.
  choose({0, "ger"});
  setSetup(setup());
  return true;
}

bool NetGame::join(const std::string& address, const std::string& name, const std::string& password,
                   std::string& error) {
  leave("left");
  Address to;
  if (!parseAddress(address, kDefaultGamePort, to, error)) return false;
  if (!sock_.open(0, false, error)) return false;
  if (!session_.join(sock_, config(password), to, name, error)) return false;
  lastError_.clear();
  lines_.clear();
  return true;
}

void NetGame::leave(const std::string& why) {
  if (session_.state() != SessionState::Idle && session_.state() != SessionState::Closed) session_.leave(why);
  announcer_.reset();
  sock_.close();
  battle_ = false;
  seats_.clear();
  chatOpen_ = false;
  makeLockstep();
}

void NetGame::browse(bool on) {
  if (!on) {
    browser_.reset();
    browseSock_.reset();
    return;
  }
  if (browser_) return;
  browseSock_ = std::make_unique<UdpSocket>();
  std::string err;
  if (!browseSock_->open(kDefaultDiscoveryPort, false, err, true)) {
    PNR_WARN_CH(Net, "Can't listen for LAN games: %s", err.c_str());
    browseSock_.reset();
    return;
  }
  browser_ = std::make_unique<DiscoveryBrowser>(*browseSock_, kNetGameId);
}

const std::vector<GameBeacon>& NetGame::lanGames() const {
  static const std::vector<GameBeacon> none;
  return browser_ ? browser_->games() : none;
}

bool NetGame::active() const {
  const SessionState s = session_.state();
  return s != SessionState::Idle && s != SessionState::Closed;
}

bool NetGame::inLobby() const { return session_.state() == SessionState::Lobby; }

bool NetGame::inBattle() const {
  return battle_ && lock_ && lock_->phase() != LockstepPhase::Idle && lock_->phase() != LockstepPhase::Ended;
}

std::string NetGame::memberName(uint8_t m) const {
  const Member* mem = session_.member(m);
  return mem ? mem->name : std::string("a player");
}

void NetGame::say(const std::string& text, float seconds) {
  lines_.push_back({text, static_cast<float>(ImGui::GetTime()) + seconds - 12.0f});
  if (lines_.size() > 60) lines_.erase(lines_.begin());
}

void NetGame::update() {
  const uint64_t now = netClockMs();
  if (browser_) browser_->update(now);
  session_.update(now);
  if (announcer_ && session_.isHost() && session_.active()) {
    GameBeacon b;
    b.gameId = kNetGameId;
    b.version = netVersion();
    b.name = session_.localMember() ? session_.localMember()->name + "'s game" : "Legion";
    b.description = battle_ ? scene_.map_.name : "In the lobby";
    b.port = sock_.localAddress().port;
    b.members = static_cast<uint8_t>(session_.members().size());
    b.maxMembers = static_cast<uint8_t>(session_.config().maxMembers);
    b.inGame = session_.state() == SessionState::InGame;
    b.password = !session_.config().password.empty();
    b.instance = instance_;
    announcer_->update(now, b);
  }
  SessionEvent e;
  while (session_.pollEvent(e)) onEvent(e);
  if (bot_) selfTestStep();
}

bool NetGame::tick() {
  if (!battle_ || !lock_) return false;
  if (lock_->phase() == LockstepPhase::Ended) return false;
  lock_->advance(netClockMs());
  return true;
}

void NetGame::onEvent(const SessionEvent& e) {
  using T = SessionEvent::Type;
  switch (e.type) {
    case T::Chat: {
      if (e.member == kNoMember) say(e.text);
      else say(memberName(e.member) + ": " + e.text, 8);
      break;
    }
    case T::Reconnecting:
      say("The connection to the host was lost: reconnecting...", 8);
      break;
    case T::Rejoined:
      say("Back in: loading the battle", 4);
      break;
    case T::Closed:
      lastError_ = e.text;
      if (scene_.mode_ == Mode::Game && seats_.size()) {
        scene_.showStatus("Multiplayer game over: " + e.text, 8);
        scene_.toMainMenu();
      }
      seats_.clear();
      break;
    default:
      break;
  }
}

void NetGame::restored(int why) {
  battle_ = true;
  scene_.menus_->close();
  if (why == 0) say("The battle begins. Enter: chat, P: pause for everyone", 8);
  else if (why == 1) say("Back in the battle", 5);
  else say("The battle was sent again after a desync", 6);
}

// --- The lobby -------------------------------------------------------------------------

namespace {
nlohmann::json setupToJson(const SkirmishSetup& s) {
  nlohmann::json j;
  j["mapSize"] = s.mapSize;
  j["seed"] = s.seed;
  j["biome"] = s.biome;
  j["layout"] = s.layout;
  j["credits"] = s.credits;
  j["startLayout"] = s.startLayout;
  j["mapFile"] = s.mapFile;
  auto& sides = j["sides"] = nlohmann::json::array();
  for (const auto& d : s.sides)
    sides.push_back({{"open", d.open}, {"faction", d.faction}, {"human", d.human}, {"difficulty", d.difficulty},
                     {"team", d.team}, {"color", d.color}});
  return j;
}

SkirmishSetup setupFromJson(const nlohmann::json& j) {
  SkirmishSetup s;
  const uint32_t colors[4] = {rgba8(196, 58, 44), rgba8(52, 96, 196), rgba8(66, 160, 72), rgba8(214, 184, 62)};
  for (int i = 0; i < 4; ++i) {
    s.sides[static_cast<size_t>(i)] = {i < 2, i % 2 ? "rus" : "ger", true, 1, i, colors[i]};
  }
  if (!j.is_object()) return s;
  s.mapSize = std::clamp(j.value("mapSize", s.mapSize), 64, 512);
  s.seed = j.value("seed", s.seed);
  s.biome = j.value("biome", s.biome);
  s.layout = j.value("layout", s.layout);
  s.credits = std::clamp(j.value("credits", s.credits), 0, 1000000);
  s.startLayout = j.value("startLayout", s.startLayout);
  s.mapFile = j.value("mapFile", std::string());
  if (j.contains("sides") && j["sides"].is_array())
    for (size_t i = 0; i < 4 && i < j["sides"].size(); ++i) {
      const auto& d = j["sides"][i];
      auto& side = s.sides[i];
      side.open = d.value("open", side.open);
      side.faction = d.value("faction", side.faction);
      side.human = d.value("human", side.human);
      side.difficulty = std::clamp(d.value("difficulty", side.difficulty), 0, 2);
      side.team = std::clamp(d.value("team", side.team), 0, 7);
      side.color = d.value("color", side.color);
    }
  return s;
}
}  // namespace

SkirmishSetup NetGame::setup() const {
  return setupFromJson(nlohmann::json::parse(session_.settings(), nullptr, false));
}

void NetGame::setSetup(const SkirmishSetup& s) { session_.setSettings(setupToJson(s).dump()); }

SeatChoice NetGame::choiceOf(uint8_t member) const {
  SeatChoice c;
  const Member* m = session_.member(member);
  if (!m) return c;
  const nlohmann::json j = nlohmann::json::parse(m->data, nullptr, false);
  if (!j.is_object()) return c;
  c.side = std::clamp(j.value("side", -1), -1, 3);
  c.faction = j.value("faction", c.faction);
  return c;
}

void NetGame::choose(const SeatChoice& c) {
  nlohmann::json j;
  j["side"] = c.side;
  j["faction"] = c.faction;
  session_.setMemberData(j.dump());
}

bool NetGame::canStart(std::string& why) const {
  if (!session_.isLeader()) {
    why = "The leader starts the game";
    return false;
  }
  const SkirmishSetup s = setup();
  int taken[4] = {-1, -1, -1, -1};
  int seated = 0;
  for (const Member& m : session_.members()) {
    if (!m.connected) continue;
    const SeatChoice c = choiceOf(m.id);
    if (m.id != session_.localId() && !m.ready) {
      why = m.name + " is not ready";
      return false;
    }
    if (c.side < 0) continue;
    const auto& side = s.sides[static_cast<size_t>(c.side)];
    if (!side.open || !side.human) {
      why = m.name + " chose a side that isn't open for players";
      return false;
    }
    if (taken[c.side] >= 0) {
      why = "Two players chose side " + std::to_string(c.side + 1);
      return false;
    }
    taken[c.side] = m.id;
    ++seated;
  }
  int sides = 0;
  for (int i = 0; i < 4; ++i)
    if (s.sides[static_cast<size_t>(i)].open && (!s.sides[static_cast<size_t>(i)].human || taken[i] >= 0)) ++sides;
  if (seated == 0) {
    why = "Nobody has chosen a side";
    return false;
  }
  if (sides < 2) {
    why = "A battle needs two sides";
    return false;
  }
  return true;
}

bool NetGame::start(std::string& error) {
  if (!canStart(error)) return false;
  SkirmishSetup s = setup();
  Seats seats;
  std::string names[4];
  for (int i = 0; i < 4; ++i) {
    auto& side = s.sides[static_cast<size_t>(i)];
    if (!side.open || !side.human) continue;
    bool sat = false;
    for (const Member& m : session_.members()) {
      const SeatChoice c = choiceOf(m.id);
      if (!m.connected || c.side != i) continue;
      side.faction = c.faction;
      seats[m.id] = static_cast<uint8_t>(i);
      names[i] = m.name;
      sat = true;
    }
    if (!sat) side.open = false;  // a player's side nobody took
  }
  seats_ = seats;
  scene_.menus_->close();
  scene_.startSkirmish(s);
  for (int i = 0; i < 4; ++i)
    if (!names[i].empty()) scene_.players_[static_cast<size_t>(i)].name = names[i];
  auto mine = seats_.find(session_.localId());
  if (mine != seats_.end()) scene_.player_ = mine->second;
  if (!lock_->start(error)) return false;
  if (session_.isHost()) {
    battle_ = true;
    restored(0);
  } else {
    scene_.paused_ = true;  // a dedicated server sends the battle back to everyone, us too: wait for it
  }
  return true;
}

// --- In the battle ---------------------------------------------------------------------

void NetGame::submit(const Command& c) {
  if (lock_) lock_->submit(encodeOrder(c));
}

void NetGame::togglePause() {
  if (lock_) lock_->setPaused(!lock_->paused());
}

void NetGame::openChat() {
  chatOpen_ = true;
  chatFocus_ = true;
  chatBuf_[0] = 0;
}

void NetGame::drawOverlay() {
  if (!battle_) return;
  const ImGuiIO& io = ImGui::GetIO();
  const float now = static_cast<float>(ImGui::GetTime());
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
  // What everyone is waiting for.
  std::string banner;
  if (session_.state() == SessionState::Reconnecting) {
    banner = "The connection to the host was lost - reconnecting...";
  } else if (lock_) {
    switch (lock_->phase()) {
      case LockstepPhase::Loading: banner = "Loading the battle..."; break;
      case LockstepPhase::Paused:
        banner = "Paused by " + memberName(lock_->pausedBy()) + " - P to go on";
        break;
      case LockstepPhase::Waiting: {
        std::string who;
        for (uint8_t m : lock_->waitingFor()) who += (who.empty() ? "" : ", ") + memberName(m);
        banner = who.empty() ? "Waiting for the host..." : "Waiting for " + who + "...";
        break;
      }
      default: break;
    }
  }
  if (!banner.empty()) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 90), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowBgAlpha(0.8f);
    ImGui::Begin("##netbanner", nullptr, flags | ImGuiWindowFlags_NoInputs);
    ImGui::TextColored(ImVec4(0.93f, 0.78f, 0.38f, 1), "%s", banner.c_str());
    ImGui::End();
  }
  // Ping, bottom left.
  {
    ImGui::SetNextWindowPos(ImVec2(12, io.DisplaySize.y - 12), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("##netping", nullptr, flags | ImGuiWindowFlags_NoInputs);
    if (session_.isHost()) ImGui::Text("Hosting - %zu players - delay %d", session_.members().size(), lock_ ? lock_->delay() : 0);
    else ImGui::Text("Ping %d ms", static_cast<int>(session_.stats(0).rttMs + 0.5f));
    ImGui::End();
  }
  // Chat lines, bottom left, and the line being written.
  std::erase_if(lines_, [&](const Line& l) { return !chatOpen_ && now - l.shown > 60.0f; });
  std::vector<const Line*> recent;
  for (const Line& l : lines_)
    if (chatOpen_ || now - l.shown < 12.0f) recent.push_back(&l);
  if (recent.size() > 8) recent.erase(recent.begin(), recent.end() - 8);
  if (!recent.empty() || chatOpen_) {
    ImGui::SetNextWindowPos(ImVec2(12, io.DisplaySize.y - 200), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowBgAlpha(chatOpen_ ? 0.75f : 0.3f);
    ImGui::Begin("##netchat", nullptr, (flags & ~ImGuiWindowFlags_NoFocusOnAppearing) | (chatOpen_ ? 0 : ImGuiWindowFlags_NoInputs));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460);
    for (const Line* l : recent) ImGui::TextUnformatted(l->text.c_str());
    ImGui::PopTextWrapPos();
    if (chatOpen_) {
      ImGui::SetNextItemWidth(460);
      if (chatFocus_) {
        ImGui::SetKeyboardFocusHere();
        chatFocus_ = false;
      }
      if (ImGui::InputText("##say", chatBuf_, sizeof chatBuf_, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (chatBuf_[0]) session_.chat(chatBuf_);
        chatOpen_ = false;
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Escape)) chatOpen_ = false;
    }
    ImGui::End();
  }
}

// --- --net-selftest ------------------------------------------------------------------------
//
// A second player in this process: its own socket, session, headless battle
// and lockstep, joining the scene's game over real UDP on this computer. The
// scene hosts, both choose sides, the scene starts, both give orders, the
// bot pauses and goes on, and their battles must stay the same.

struct NetGame::Bot {
  struct Recorder final : LockstepAdapter {
    NetWorld world;
    std::map<uint32_t, uint64_t> history;
    int restored = 0;
    Recorder(const UnitTypeRegistry& t, const WeaponRegistry& w) : world(t, w) {}
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
  UdpSocket sock;
  Session session;
  std::unique_ptr<Recorder> rec;
  std::unique_ptr<Lockstep> lock;
  // --net-selftest dedicated: the server, in this process too.
  struct Server {
    UdpSocket sock;
    Session session;
    std::unique_ptr<Recorder> rec;
    std::unique_ptr<Lockstep> lock;
  };
  std::unique_ptr<Server> server;
  bool joined = false;
  uint64_t started = 0;
  int stage = 0;
  int failures = 0;
  uint64_t stageAt = 0;
};

void NetGame::startSelfTest(bool dedicated) {
  auto check = [&](bool ok, const char* what) {
    if (!ok) ++scene_.selfTestFailures_;
    PNR_INFO("NET SELFTEST %s: %s", ok ? "ok  " : "FAIL", what);
  };
  std::string err;
  bot_ = std::make_unique<Bot>();
  uint16_t port = 0;
  if (dedicated) {
    // A dedicated server as LegionServer runs it, and the scene joins it first (so it leads).
    auto sv = std::make_unique<Bot::Server>();
    sv->rec = std::make_unique<Bot::Recorder>(scene_.types_, scene_.weapons_);
    sv->lock = std::make_unique<Lockstep>(sv->session, *sv->rec);
    Bot::Server* raw = sv.get();
    sv->lock->setFilter([raw](uint8_t member, std::vector<uint8_t>& order) {
      Command c;
      return decodeOrder(order, c) && orderAllowed(c.type) && raw->rec->world.seats().count(member) > 0;
    });
    const bool up = sv->sock.open(0, true, err) && sv->session.host(sv->sock, config(""), "", err);
    check(up, "start a dedicated server");
    port = sv->sock.localAddress().port;
    bot_->server = std::move(sv);
    check(join("127.0.0.1:" + std::to_string(port), "Host", "", err), "join the server");
  } else {
    check(host("Host", 0, "", err), "host a game");
    port = sock_.localAddress().port;
  }
  if (!err.empty()) {
    PNR_INFO("NET SELFTEST FAILED: %s", err.c_str());
    bot_.reset();
    scene_.quit_ = true;
    return;
  }
  bot_->rec = std::make_unique<Bot::Recorder>(scene_.types_, scene_.weapons_);
  bot_->lock = std::make_unique<Lockstep>(bot_->session, *bot_->rec);
  check(bot_->sock.open(0, false, err), "open the second player's socket");
  bot_->started = bot_->stageAt = netClockMs();
  bot_->joined = !dedicated;
  if (!dedicated)
    check(bot_->session.join(bot_->sock, config(""), Address::loopback(port), "Bot", err), "the second player joins");
}

void NetGame::selfTestStep() {
  Bot& b = *bot_;
  const uint64_t now = netClockMs();
  if (b.server) {
    b.server->session.update(now);
    b.server->lock->advance(now);
    // The bot joins once the scene is in, so the scene leads.
    if (!b.joined && session_.state() == SessionState::Lobby) {
      std::string err;
      b.joined = true;
      b.session.join(b.sock, config(""), Address::loopback(b.server->sock.localAddress().port), "Bot", err);
    }
  }
  b.session.update(now);
  b.lock->advance(now);
  auto check = [&](bool ok, const std::string& what) {
    if (!ok) ++scene_.selfTestFailures_;
    PNR_INFO("NET SELFTEST %s: %s", ok ? "ok  " : "FAIL", what.c_str());
  };
  auto next = [&] {
    ++b.stage;
    b.stageAt = now;
  };
  const uint64_t inStage = now - b.stageAt;
  if (now - b.started > 120000) {
    check(false, "finish within two minutes (stuck at stage " + std::to_string(b.stage) + "; the second player is " +
                     stateName(b.session.state()) + " " + b.session.closeReason() + ", the host has " +
                     std::to_string(session_.members().size()) + " members, " + stateName(session_.state()) + ")");
    b.stage = 99;
  }
  switch (b.stage) {
    case 0:  // in the lobby: choose, talk, get ready
      if (b.session.state() == SessionState::Lobby && session_.members().size() == 2) {
        if (b.server) {
          check(session_.isLeader(), "the first to join a dedicated server leads");
          choose({0, "ger"});
          setSetup(setup());
        }
        nlohmann::json j;
        j["side"] = 1;
        j["faction"] = "rus";
        b.session.setMemberData(j.dump());
        b.session.chat("Good luck");
        b.session.setReady(true);
        next();
      }
      break;
    case 1:
      if (inStage > 1500) {
        std::string why;
        const bool can = canStart(why);
        check(can, "everyone ready, the game can start" + (can ? std::string() : ": " + why));
        bool heard = false;
        for (const ChatLine& l : session_.chatLog()) heard = heard || l.text == "Good luck";
        check(heard, "the host heard the second player's chat");
        std::string err;
        check(start(err), "start the battle" + (err.empty() ? std::string() : ": " + err));
        session_.chat("Have fun");
        next();
      }
      break;
    case 2:  // the bot has the battle: orders from both sides
      if (b.rec->restored == 1 && inBattle() && inStage > 1000) {
        check(b.rec->world.seats().size() == 2, "the battle carries both seats");
        check(scene_.players_[1].name == "Bot", "the second side carries the player's name");
        auto own = [](Simulation& sim, uint8_t side) {
          std::vector<Entity> out;
          Registry& reg = sim.registry();
          for (uint32_t e : reg.pool<CUnit>().entities())
            if (reg.pool<CUnit>().get(e).owner == side && !reg.has<CStructure>(reg.handle(e))) out.push_back(reg.handle(e));
          return out;
        };
        Command mine;
        mine.type = CommandType::AttackMove;
        mine.units = own(*scene_.sim_, 0);
        mine.target = {scene_.map_.widthLeptons() / 2, scene_.map_.heightLeptons() / 2};
        scene_.issue(mine);
        Command theirs;
        theirs.type = CommandType::AttackMove;
        theirs.units = own(*b.rec->world.sim(), 1);
        theirs.target = mine.target;
        b.lock->submit(encodeOrder(theirs));
        Command cheat;
        cheat.type = CommandType::SetCredits;
        cheat.amount = 1000000;
        b.lock->submit(encodeOrder(cheat));
        next();
      }
      break;
    case 3:  // pause from the second player
      if (inStage > 5000) {
        b.lock->setPaused(true);
        next();
      }
      break;
    case 4:
      if (inStage > 1500) {
        check(lock_->phase() == LockstepPhase::Paused &&
                  (!b.server || b.server->lock->phase() == LockstepPhase::Paused),
              "the second player's pause holds everyone");
        scene_.app_.requestScreenshot(scene_.app_.userDir() + "net_selftest.png");  // the banner and the chat, for a look
        b.lock->setPaused(false);
        next();
      }
      break;
    case 5:
      if (inStage > 8000) {
        size_t common = 0;
        bool same = true;
        for (const auto& [t, h] : history_) {
          auto it = b.rec->history.find(t);
          if (it == b.rec->history.end()) continue;
          ++common;
          same = same && it->second == h;
        }
        check(common >= 8, "both battles ran (" + std::to_string(common) + " checks in common)");
        check(same, "both battles are the same, tick for tick");
        if (b.server) {
          size_t withServer = 0;
          bool sameServer = true;
          for (const auto& [t, h] : history_) {
            auto it = b.server->rec->history.find(t);
            if (it == b.server->rec->history.end()) continue;
            ++withServer;
            sameServer = sameServer && it->second == h;
          }
          check(withServer >= 8 && sameServer, "the server's battle is the same too");
          check(b.server->lock->stats().desyncs == 0, "no desync on the server");
        }
        check(lock_->stats().desyncs == 0, "no desync");
        check(scene_.sim_->playerState(1).credits < 1000000, "a cheat from the console of a player is dropped");
        bool heard = false;
        for (const ChatLine& l : b.session.chatLog()) heard = heard || l.text == "Have fun";
        check(heard, "the second player heard the host's chat");
        check(lock_->phase() == LockstepPhase::Running, "the battle runs after the pause");
        next();
      }
      break;
    case 6:
      b.session.leave("done");
      if (b.server) b.server->session.leave("done");
      PNR_INFO("NET SELFTEST %s (%d failures)", scene_.selfTestFailures_ ? "FAILED" : "PASSED",
               scene_.selfTestFailures_);
      scene_.quit_ = true;
      next();
      break;
    default:
      if (b.stage == 99) {
        PNR_INFO("NET SELFTEST FAILED (%d failures)", scene_.selfTestFailures_);
        scene_.quit_ = true;
        b.stage = 100;
      }
      break;
  }
}

}  // namespace legion

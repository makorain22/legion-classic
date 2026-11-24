// Legion Classic - the dedicated server.
//
// Runs without graphics or sound (it links only the engine core): hosts a
// session, shows itself on the LAN, takes the battle the leader's game builds
// and runs it as the referee - turns, state checks, resyncs, players
// rejoining. When everyone has left, it goes back to the lobby and waits for
// the next game.
//
//   LegionServer [--port 47800] [--name "Legion server"] [--password secret]
//                [--max 8] [--mods a,b] [--content DIR] [--no-lan]
//                [--quit-after SECONDS] [--verbose]
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "net_world.hpp"
#include "pioneer/core/log.hpp"
#include "pioneer/net/discovery.hpp"
#include "pioneer/net/lockstep.hpp"
#include "pioneer/net/session.hpp"

using namespace pioneer;
using namespace pioneer::net;

namespace {
std::atomic<bool> gQuit{false};
void onSignal(int) { gQuit = true; }

uint64_t nowMs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at <= s.size()) {
    const size_t comma = s.find(',', at);
    const std::string part = s.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
    if (!part.empty()) out.push_back(part);
    if (comma == std::string::npos) break;
    at = comma + 1;
  }
  return out;
}

void usage() {
  std::printf(
      "Legion Classic dedicated server\n"
      "  --port N          game port (default %u; LAN beacons go to %u)\n"
      "  --name TEXT       the server's name in game lists\n"
      "  --password TEXT   players must give it to join\n"
      "  --max N           players (default 8)\n"
      "  --mods a,b        mods to play with (players need the same)\n"
      "  --content DIR     the game's content folder (default: next to this program)\n"
      "  --no-lan          don't announce the server on the local network\n"
      "  --quit-after S    stop after S seconds (tests)\n"
      "  --verbose         log network details\n",
      kDefaultGamePort, kDefaultDiscoveryPort);
}
}  // namespace

int main(int argc, char** argv) {
  uint16_t port = kDefaultGamePort;
  std::string name = "Legion server", password, contentDir, modsArg;
  int maxPlayers = 8;
  bool lan = true, verbose = false;
  double quitAfter = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
    if (a == "--port") port = static_cast<uint16_t>(std::atoi(next().c_str()));
    else if (a == "--name") name = next();
    else if (a == "--password") password = next();
    else if (a == "--max") maxPlayers = std::max(1, std::min(8, std::atoi(next().c_str())));
    else if (a == "--mods") modsArg = next();
    else if (a == "--content") contentDir = next();
    else if (a == "--no-lan") lan = false;
    else if (a == "--quit-after") quitAfter = std::atof(next().c_str());
    else if (a == "--verbose") verbose = true;
    else if (a == "--help" || a == "-h") {
      usage();
      return 0;
    } else {
      std::fprintf(stderr, "Unknown option %s\n", a.c_str());
      usage();
      return 2;
    }
  }
  log::setChannelLevel(log::Channel::Net, verbose ? log::Level::Debug : log::Level::Info);

  namespace fs = std::filesystem;
  const fs::path exeDir = fs::absolute(fs::path(argv[0])).parent_path();
  if (contentDir.empty()) {
    for (const fs::path& p : {exeDir / "content", exeDir / ".." / "content", fs::path("content")})
      if (fs::exists(p / "rules" / "units.json")) {
        contentDir = p.string();
        break;
      }
  }
  if (contentDir.empty()) {
    std::fprintf(stderr, "The game's content folder wasn't found: give it with --content\n");
    return 1;
  }
  if (contentDir.back() != '/' && contentDir.back() != '\\') contentDir += '/';
  legion::NetRules rules;
  std::string err, warnings;
  const std::vector<std::string> modDirs = {contentDir + "../mods/"};
  if (!legion::loadNetRules(contentDir, modDirs, splitList(modsArg), "", rules, err, &warnings)) {
    std::fprintf(stderr, "The rules can't be loaded: %s\n", err.c_str());
    return 1;
  }
  if (!warnings.empty()) std::fprintf(stderr, "%s", warnings.c_str());

  UdpSocket sock;
  if (!sock.open(port, true, err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  SessionConfig cfg;
  cfg.gameId = legion::kNetGameId;
  cfg.gameVersion = legion::netVersion();
  cfg.contentHash = rules.hash;
  cfg.password = password;
  cfg.serverName = name;
  cfg.maxMembers = maxPlayers;
  Session session;
  if (!session.host(sock, cfg, "", err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  std::printf("%s on port %u (rules %s%s%s)%s\n", name.c_str(), sock.localAddress().port, rules.hash.c_str(),
              rules.mods.empty() ? "" : ", mods ", modsArg.c_str(), lan ? ", announced on the LAN" : "");
  std::fflush(stdout);

  auto world = std::make_unique<legion::NetWorld>(rules.types, rules.weapons);
  auto lock = std::make_unique<Lockstep>(session, *world);
  lock->setFilter([&](uint8_t member, std::vector<uint8_t>& order) {
    Command c;
    return legion::decodeOrder(order, c) && legion::orderAllowed(c.type) && world->seats().count(member);
  });
  DiscoveryAnnouncer announcer(sock, kDefaultDiscoveryPort);
  const uint64_t start = nowMs();
  uint64_t lastStatus = start;
  while (!gQuit) {
    const uint64_t now = nowMs();
    if (quitAfter > 0 && static_cast<double>(now - start) >= quitAfter * 1000.0) break;
    session.update(now);
    lock->advance(now);
    SessionEvent e;
    while (session.pollEvent(e)) {
      using T = SessionEvent::Type;
      if (e.type == T::MemberJoined) std::printf("+ %s joined\n", e.text.c_str());
      else if (e.type == T::MemberLeft) std::printf("- %s left\n", e.text.c_str());
      else if (e.type == T::MemberLost) std::printf("! %s lost the connection\n", e.text.c_str());
      else if (e.type == T::MemberBack) std::printf("+ %s is back\n", e.text.c_str());
      else if (e.type == T::Chat && e.member != kNoMember) {
        const Member* m = session.member(e.member);
        std::printf("<%s> %s\n", m ? m->name.c_str() : "?", e.text.c_str());
      }
      std::fflush(stdout);
    }
    // Everyone gone from a running game: back to the lobby for the next one.
    if (session.state() == SessionState::InGame) {
      bool anyone = false;
      for (const Member& m : session.members()) anyone = anyone || m.connected;
      if (!anyone && session.members().empty()) {
        std::printf("Everyone has left: back to the lobby\n");
        lock.reset();
        session.setInGame(false);
        world = std::make_unique<legion::NetWorld>(rules.types, rules.weapons);
        lock = std::make_unique<Lockstep>(session, *world);
      }
    }
    if (lan) {
      GameBeacon b;
      b.gameId = cfg.gameId;
      b.version = cfg.gameVersion;
      b.name = name;
      b.description = session.state() == SessionState::InGame ? world->map().name : "Waiting for players";
      b.port = sock.localAddress().port;
      b.members = static_cast<uint8_t>(session.members().size());
      b.maxMembers = static_cast<uint8_t>(maxPlayers);
      b.inGame = session.state() == SessionState::InGame;
      b.password = !password.empty();
      b.dedicated = true;
      b.instance = (static_cast<uint64_t>(sock.localAddress().port) << 32) ^ start;
      announcer.update(now, b);
    }
    if (now - lastStatus >= 60000) {
      lastStatus = now;
      std::printf("[%s] %zu player(s), tick %u, %s\n", stateName(session.state()), session.members().size(),
                  lock->tick(), phaseName(lock->phase()));
      std::fflush(stdout);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  session.leave("The server is shutting down");
  std::printf("Server stopped\n");
  return 0;
}

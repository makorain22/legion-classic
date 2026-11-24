#pragma once
// Pioneer Engine - sessions: hosting, joining, the lobby and chat.
//
// A session is one host and the members connected to it. The host is either
// a player's own game (a listen server: the host is also member 0) or a
// dedicated server (no member of its own; the first member to join, the
// leader, chooses the settings and starts).
//
// What the session keeps, for every game built on it:
//   - members: id, name, ready, connected, ping and a text the game fills
//     in (Legion: side, faction, team, colour, as JSON);
//   - settings: one text the host (or leader) owns (Legion: the map and the
//     sides, as JSON);
//   - chat, with a log for the interface;
//   - joining checks: same game, same version, same content (rules and mods
//     hash), the password; a refusal says why;
//   - rejoining: in a game, a member whose connection is lost keeps its
//     place for `rejoinSeconds`; its session tries to come back on its own
//     (with the token the host gave it) and gets its old id back;
//   - messages for systems built on top (the lockstep, net/lockstep.hpp):
//     types from kFirstAppMessage up, to the host, to one member or to all,
//     handled by registered handlers.
//
// Nothing here knows about any particular game.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pioneer/net/transport.hpp"

namespace pioneer::net {

constexpr uint16_t kDefaultGamePort = 47800;
constexpr uint16_t kDefaultDiscoveryPort = 47801;
constexpr uint8_t kNoMember = 0xFF;
constexpr uint8_t kFirstAppMessage = 32;

struct SessionConfig {
  std::string gameId = "pioneer";  // a host of another game refuses (and isn't listed)
  std::string gameVersion;         // must be the same
  std::string contentHash;         // must be the same (rules, mods)
  std::string password;            // the host's ("" none); members send theirs
  std::string serverName;          // dedicated server: its name in lists
  int maxMembers = 8;
  int rejoinSeconds = 120;
  TransportConfig transport;
};

struct Member {
  uint8_t id = 0;
  std::string name;
  bool ready = false;
  bool connected = true;
  bool local = false;     // the host's own player (listen server)
  int pingMs = 0;
  std::string data;       // the game's (side, colour...)
};

struct ChatLine {
  uint8_t from = kNoMember;  // kNoMember: the session itself (joins, leaves)
  std::string name;
  std::string text;
};

enum class SessionState : uint8_t { Idle, Connecting, Lobby, InGame, Reconnecting, Closed };
const char* stateName(SessionState s);

struct SessionEvent {
  enum class Type : uint8_t {
    Joined,          // this side is in (client: welcomed; host: hosting)
    MemberJoined, MemberLeft,
    MemberLost,      // in a game: connection lost, place kept
    MemberBack,      // ...and back
    Changed,         // members or settings changed
    Chat,
    Reconnecting,    // this side lost the host and is trying again
    Rejoined,        // ...and is back in (the game must be sent again)
    Closed,          // the session is over (see closeReason())
  } type = Type::Changed;
  uint8_t member = kNoMember;
  std::string text;
};

class Session {
 public:
  Session();
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Host on `socket` (already open on the game port). `localName` empty:
  // a dedicated server.
  bool host(DatagramSocket& socket, const SessionConfig& cfg, const std::string& localName, std::string& error);
  // Join the host at `to`. `rejoinToken` from an earlier session gets the old place back.
  bool join(DatagramSocket& socket, const SessionConfig& cfg, const Address& to, const std::string& name,
            std::string& error, uint64_t rejoinToken = 0);
  // Leave (members hear it); the session is Closed.
  void leave(const std::string& why = "left");
  void update(uint64_t nowMs);
  uint64_t now() const { return now_; }

  SessionState state() const { return state_; }
  bool active() const { return state_ == SessionState::Lobby || state_ == SessionState::InGame; }
  bool isHost() const { return hosting_; }
  bool dedicated() const { return hosting_ && localId_ == kNoMember; }
  uint8_t localId() const { return localId_; }
  // Who chooses the settings and starts: the host's player, or on a
  // dedicated server the connected member with the lowest id.
  uint8_t leader() const;
  bool isLeader() const { return localId_ != kNoMember && leader() == localId_; }
  const std::vector<Member>& members() const { return members_; }
  const Member* member(uint8_t id) const;
  const Member* localMember() const { return member(localId_); }
  const std::string& settings() const { return settings_; }
  const SessionConfig& config() const { return cfg_; }
  const std::string& closeReason() const { return closeReason_; }
  uint64_t rejoinToken() const { return token_; }
  Address hostAddress() const { return hostAddr_; }

  // The leader's settings (others are ignored by the host).
  void setSettings(const std::string& s);
  void setName(const std::string& name);
  void setReady(bool r);
  void setMemberData(const std::string& data);
  void setMemberData(uint8_t id, const std::string& data);  // host only
  void chat(const std::string& text);
  const std::deque<ChatLine>& chatLog() const { return chat_; }
  // Host: in a game, lost members keep their places; in the lobby they leave.
  void setInGame(bool on);
  void kick(uint8_t id, const std::string& why);

  // --- Messages for the systems on top --------------------------------------
  using Handler = std::function<void(uint8_t from, const std::vector<uint8_t>& data)>;
  void setHandler(uint8_t type, Handler h) { handlers_[type] = std::move(h); }
  // Client: to the host. On the host itself: handled at once as from the local member.
  void sendToHost(uint8_t type, const std::vector<uint8_t>& data, Channel ch = Channel::Reliable);
  // Host: to one member / to every connected member but its own.
  void sendTo(uint8_t member, uint8_t type, const std::vector<uint8_t>& data, Channel ch = Channel::Reliable);
  void broadcast(uint8_t type, const std::vector<uint8_t>& data, Channel ch = Channel::Reliable);

  // Events, for listeners (the lockstep) and for the interface (pollEvent).
  using Listener = std::function<void(const SessionEvent&)>;
  int addListener(Listener l);
  void removeListener(int id);
  bool pollEvent(SessionEvent& out);

  Transport* transport() { return transport_.get(); }
  ConnectionStats stats(uint8_t member) const;  // client: of the link to the host (member ignored)

 private:
  struct Remote {
    ConnId conn = 0;
    uint8_t member = kNoMember;  // kNoMember until welcomed
    uint64_t closeAt = 0;        // refused: close once the refusal is out
    std::deque<uint64_t> chatTimes;
  };
  void onTransport(const TransportEvent& e);
  void onMessage(ConnId c, const std::vector<uint8_t>& d);
  void hostMessage(Remote& r, uint8_t type, const uint8_t* p, size_t n);
  void clientMessage(uint8_t type, const uint8_t* p, size_t n);
  void refuse(Remote& r, const std::string& why);
  void sendLobby();
  std::vector<uint8_t> lobbyBytes() const;
  void readLobby(const uint8_t* p, size_t n);
  void emit(SessionEvent::Type t, uint8_t m = kNoMember, std::string text = {});
  void addChat(uint8_t from, const std::string& text);
  void systemChat(const std::string& text);  // host: a line from the session itself, to everyone
  void reset();
  void close(const std::string& why);
  void rawSend(ConnId c, uint8_t type, const std::vector<uint8_t>& data, Channel ch);
  Remote* remoteOf(uint8_t member);
  Member* memberMut(uint8_t id);
  uint8_t freeId() const;

  SessionConfig cfg_;
  SessionState state_ = SessionState::Idle;
  bool hosting_ = false;
  std::unique_ptr<Transport> transport_;
  DatagramSocket* socket_ = nullptr;
  uint64_t now_ = 0;
  uint8_t localId_ = kNoMember;
  std::vector<Member> members_;
  std::string settings_;
  std::deque<ChatLine> chat_;
  std::string closeReason_;
  // Host side
  std::map<ConnId, Remote> remotes_;
  std::map<uint8_t, uint64_t> tokens_;      // member -> rejoin token
  std::map<uint8_t, uint64_t> lostAt_;      // member -> when its connection went
  uint64_t lastPings_ = 0;
  bool dirty_ = false;
  // Client side
  ConnId hostConn_ = 0;
  Address hostAddr_;
  std::string name_;
  uint64_t token_ = 0;
  uint64_t reconnectUntil_ = 0;
  bool wasInGame_ = false;
  std::map<uint8_t, Handler> handlers_;
  std::map<int, Listener> listeners_;
  int nextListener_ = 1;
  std::deque<SessionEvent> events_;
};

// Cleans a name or chat line from the network: no control characters, at
// most `maxBytes` (cut on a whole UTF-8 character), spaces trimmed.
std::string cleanText(const std::string& s, size_t maxBytes);

}  // namespace pioneer::net

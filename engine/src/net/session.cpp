// Pioneer Engine - sessions: hosting, joining, the lobby and chat (see session.hpp).
#include "pioneer/net/session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <random>

#include "pioneer/core/bytes.hpp"
#include "pioneer/core/log.hpp"

namespace pioneer::net {

namespace {
// Session messages (the first byte of every reliable message).
enum Msg : uint8_t {
  kHello = 1,     // client: game id, version, content hash, password, name, rejoin token
  kWelcome = 2,   // host: your id, your token, then the lobby
  kRefuse = 3,    // host: why
  kLobby = 4,     // host: settings, in game, members
  kSetName = 5,
  kSetReady = 6,
  kSetData = 7,
  kChat = 8,      // client: text; host: from, name, text
  kKick = 9,      // host: why
  kSetSettings = 10,
  kBye = 11,      // either side: leaving, why
};
constexpr size_t kMaxName = 24;
constexpr size_t kMaxChat = 200;
constexpr size_t kMaxChatLog = 200;

uint64_t randomToken() {
  std::random_device rd;
  uint64_t t = (static_cast<uint64_t>(rd()) << 32) ^ rd() ^
               static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  return t ? t : 1;
}
}  // namespace

const char* stateName(SessionState s) {
  switch (s) {
    case SessionState::Idle: return "idle";
    case SessionState::Connecting: return "connecting";
    case SessionState::Lobby: return "lobby";
    case SessionState::InGame: return "in game";
    case SessionState::Reconnecting: return "reconnecting";
    case SessionState::Closed: return "closed";
  }
  return "?";
}

std::string cleanText(const std::string& s, size_t maxBytes) {
  std::string out;
  for (unsigned char ch : s) {
    if (ch < 32 || ch == 127) {
      if (ch == '\t') out += ' ';
      continue;
    }
    out += static_cast<char>(ch);
  }
  if (out.size() > maxBytes) {
    size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n;  // not inside a character
    out.resize(n);
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  size_t lead = 0;
  while (lead < out.size() && out[lead] == ' ') ++lead;
  return out.substr(lead);
}

Session::Session() = default;
Session::~Session() {
  if (active() || state_ == SessionState::Connecting || state_ == SessionState::Reconnecting) leave("closed");
}

bool Session::host(DatagramSocket& socket, const SessionConfig& cfg, const std::string& localName, std::string& error) {
  if (state_ != SessionState::Idle && state_ != SessionState::Closed) {
    error = "Already in a session";
    return false;
  }
  reset();
  cfg_ = cfg;
  socket_ = &socket;
  transport_ = std::make_unique<Transport>(socket, cfg.transport);
  transport_->listen(true);
  hosting_ = true;
  state_ = SessionState::Lobby;
  if (!localName.empty()) {
    Member m;
    m.id = 0;
    m.name = cleanText(localName, kMaxName);
    if (m.name.empty()) m.name = "Host";
    m.local = true;
    members_.push_back(m);
    localId_ = 0;
    addChat(kNoMember, m.name + " is hosting");
  }
  PNR_INFO_CH(Net, "Hosting on port %u%s", socket.localAddress().port, localName.empty() ? " (dedicated)" : "");
  emit(SessionEvent::Type::Joined, localId_);
  return true;
}

bool Session::join(DatagramSocket& socket, const SessionConfig& cfg, const Address& to, const std::string& name,
                   std::string& error, uint64_t rejoinToken) {
  if (state_ != SessionState::Idle && state_ != SessionState::Closed) {
    error = "Already in a session";
    return false;
  }
  if (!to.valid()) {
    error = "No address";
    return false;
  }
  reset();
  cfg_ = cfg;
  socket_ = &socket;
  transport_ = std::make_unique<Transport>(socket, cfg.transport);
  hostAddr_ = to;
  name_ = cleanText(name, kMaxName);
  if (name_.empty()) name_ = "Player";
  token_ = rejoinToken;
  state_ = SessionState::Connecting;
  hostConn_ = transport_->connect(to);
  PNR_INFO_CH(Net, "Joining %s as %s", to.toString().c_str(), name_.c_str());
  return true;
}

// Everything but the handlers and listeners (systems on top stay attached).
void Session::reset() {
  if (transport_) transport_->disconnectAll();
  transport_.reset();
  cfg_ = {};
  state_ = SessionState::Idle;
  hosting_ = false;
  socket_ = nullptr;
  localId_ = kNoMember;
  members_.clear();
  settings_.clear();
  chat_.clear();
  closeReason_.clear();
  remotes_.clear();
  tokens_.clear();
  lostAt_.clear();
  lastPings_ = 0;
  dirty_ = false;
  hostConn_ = 0;
  hostAddr_ = {};
  name_.clear();
  token_ = 0;
  reconnectUntil_ = 0;
  wasInGame_ = false;
  events_.clear();
}

void Session::systemChat(const std::string& text) {
  addChat(kNoMember, text);
  ByteWriter w;
  w.u8(kNoMember);
  w.str("");
  w.str(text);
  broadcast(kChat, w.bytes());
  emit(SessionEvent::Type::Chat, kNoMember, text);
}

void Session::leave(const std::string& why) {
  if (state_ == SessionState::Idle || state_ == SessionState::Closed) return;
  ByteWriter w;
  w.str(why);
  if (hosting_) {
    for (auto& [c, r] : remotes_) rawSend(c, kBye, w.bytes(), Channel::Reliable);
  } else if (hostConn_) {
    rawSend(hostConn_, kBye, w.bytes(), Channel::Reliable);
  }
  // Give the goodbye a moment to go out; the transport's Disconnect follows.
  if (transport_) {
    for (int i = 0; i < 3; ++i) transport_->update(now_ + static_cast<uint64_t>(i) * 20);
    transport_->disconnectAll();
  }
  close(why == "left" ? "You left" : why);
}

void Session::close(const std::string& why) {
  if (state_ == SessionState::Closed) return;
  state_ = SessionState::Closed;
  closeReason_ = why;
  if (transport_) transport_->disconnectAll();
  PNR_INFO_CH(Net, "Session closed: %s", why.c_str());
  emit(SessionEvent::Type::Closed, kNoMember, why);
}

uint8_t Session::leader() const {
  if (hosting_ && localId_ != kNoMember) return localId_;
  uint8_t best = kNoMember;
  for (const Member& m : members_)
    if (m.connected && (best == kNoMember || m.id < best)) best = m.id;
  // A client of a listen server: the host's player (id 0) leads.
  return best;
}

const Member* Session::member(uint8_t id) const {
  for (const Member& m : members_)
    if (m.id == id) return &m;
  return nullptr;
}

Member* Session::memberMut(uint8_t id) {
  for (Member& m : members_)
    if (m.id == id) return &m;
  return nullptr;
}

Session::Remote* Session::remoteOf(uint8_t member) {
  for (auto& [c, r] : remotes_)
    if (r.member == member) return &r;
  return nullptr;
}

uint8_t Session::freeId() const {
  for (uint8_t id = 1; id < 250; ++id)
    if (!member(id)) return id;
  return kNoMember;
}

int Session::addListener(Listener l) {
  listeners_[nextListener_] = std::move(l);
  return nextListener_++;
}

void Session::removeListener(int id) { listeners_.erase(id); }

bool Session::pollEvent(SessionEvent& out) {
  if (events_.empty()) return false;
  out = std::move(events_.front());
  events_.pop_front();
  return true;
}

void Session::emit(SessionEvent::Type t, uint8_t m, std::string text) {
  SessionEvent e{t, m, std::move(text)};
  // Copy: a listener may add or remove listeners.
  auto ls = listeners_;
  for (auto& [id, l] : ls) l(e);
  events_.push_back(std::move(e));
  if (events_.size() > 500) events_.pop_front();
}

void Session::addChat(uint8_t from, const std::string& text) {
  ChatLine l;
  l.from = from;
  if (const Member* m = member(from)) l.name = m->name;
  l.text = text;
  chat_.push_back(l);
  if (chat_.size() > kMaxChatLog) chat_.pop_front();
}

ConnectionStats Session::stats(uint8_t m) const {
  if (!transport_) return {};
  if (!hosting_) return transport_->stats(hostConn_);
  for (const auto& [c, r] : remotes_)
    if (r.member == m) return transport_->stats(c);
  return {};
}

void Session::rawSend(ConnId c, uint8_t type, const std::vector<uint8_t>& data, Channel ch) {
  if (!transport_) return;
  std::vector<uint8_t> b;
  b.reserve(data.size() + 1);
  b.push_back(type);
  b.insert(b.end(), data.begin(), data.end());
  transport_->send(c, ch, b);
}

void Session::sendToHost(uint8_t type, const std::vector<uint8_t>& data, Channel ch) {
  if (hosting_) {
    auto it = handlers_.find(type);
    if (it != handlers_.end() && localId_ != kNoMember) it->second(localId_, data);
    return;
  }
  if (hostConn_ && state_ != SessionState::Connecting && state_ != SessionState::Reconnecting)
    rawSend(hostConn_, type, data, ch);
}

void Session::sendTo(uint8_t m, uint8_t type, const std::vector<uint8_t>& data, Channel ch) {
  if (!hosting_) return;
  if (Remote* r = remoteOf(m)) rawSend(r->conn, type, data, ch);
}

void Session::broadcast(uint8_t type, const std::vector<uint8_t>& data, Channel ch) {
  if (!hosting_) return;
  for (auto& [c, r] : remotes_)
    if (r.member != kNoMember && !r.closeAt) rawSend(c, type, data, ch);
}

// --- The lobby ------------------------------------------------------------------

std::vector<uint8_t> Session::lobbyBytes() const {
  ByteWriter w;
  w.str(settings_);
  w.u8(state_ == SessionState::InGame ? 1 : 0);
  w.u8(static_cast<uint8_t>(members_.size()));
  for (const Member& m : members_) {
    w.u8(m.id);
    w.str(m.name);
    w.u8(static_cast<uint8_t>((m.ready ? 1 : 0) | (m.connected ? 2 : 0) | (m.local ? 4 : 0)));
    w.u16(static_cast<uint16_t>(std::clamp(m.pingMs, 0, 65535)));
    w.str(m.data);
  }
  return w.bytes();
}

void Session::readLobby(const uint8_t* p, size_t n) {
  ByteReader r(p, n);
  std::string settings = r.str();
  const bool inGame = r.u8() != 0;
  const uint8_t count = r.u8();
  std::vector<Member> ms;
  for (uint8_t i = 0; i < count && r.ok(); ++i) {
    Member m;
    m.id = r.u8();
    m.name = r.str();
    const uint8_t f = r.u8();
    m.ready = f & 1;
    m.connected = (f & 2) != 0;
    m.pingMs = r.u16();
    m.data = r.str();
    m.local = m.id == localId_;
    ms.push_back(std::move(m));
  }
  if (!r.ok()) return;
  settings_ = std::move(settings);
  members_ = std::move(ms);
  if (state_ == SessionState::Lobby || state_ == SessionState::InGame)
    state_ = inGame ? SessionState::InGame : SessionState::Lobby;
  wasInGame_ = inGame;
  emit(SessionEvent::Type::Changed);
}

void Session::sendLobby() {
  dirty_ = false;
  broadcast(kLobby, lobbyBytes());
  emit(SessionEvent::Type::Changed);
}

void Session::setSettings(const std::string& s) {
  if (hosting_) {
    if (settings_ == s) return;
    settings_ = s;
    dirty_ = true;
  } else if (isLeader()) {
    ByteWriter w;
    w.str(s);
    sendToHost(kSetSettings, w.bytes());
  }
}

void Session::setName(const std::string& nameIn) {
  const std::string name = cleanText(nameIn, kMaxName);
  if (name.empty()) return;
  if (hosting_) {
    if (Member* m = memberMut(localId_)) {
      m->name = name;
      dirty_ = true;
    }
    return;
  }
  name_ = name;
  ByteWriter w;
  w.str(name);
  sendToHost(kSetName, w.bytes());
}

void Session::setReady(bool on) {
  if (hosting_) {
    if (Member* m = memberMut(localId_)) {
      m->ready = on;
      dirty_ = true;
    }
    return;
  }
  sendToHost(kSetReady, {static_cast<uint8_t>(on ? 1 : 0)});
}

void Session::setMemberData(const std::string& data) {
  if (hosting_) {
    setMemberData(localId_, data);
    return;
  }
  ByteWriter w;
  w.str(data);
  sendToHost(kSetData, w.bytes());
}

void Session::setMemberData(uint8_t id, const std::string& data) {
  if (!hosting_) return;
  if (Member* m = memberMut(id))
    if (m->data != data) {
      m->data = data;
      dirty_ = true;
    }
}

void Session::chat(const std::string& textIn) {
  const std::string text = cleanText(textIn, kMaxChat);
  if (text.empty()) return;
  if (hosting_) {
    if (localId_ == kNoMember) return;
    addChat(localId_, text);
    ByteWriter w;
    w.u8(localId_);
    w.str(member(localId_)->name);
    w.str(text);
    broadcast(kChat, w.bytes());
    emit(SessionEvent::Type::Chat, localId_, text);
    return;
  }
  ByteWriter w;
  w.str(text);
  sendToHost(kChat, w.bytes());
}

void Session::setInGame(bool on) {
  if (!hosting_ || !active()) return;
  const SessionState want = on ? SessionState::InGame : SessionState::Lobby;
  if (state_ == want) return;
  state_ = want;
  if (!on) {
    // Back in the lobby: places kept for lost members go.
    for (auto& [id, t] : lostAt_) std::erase_if(members_, [&](const Member& m) { return m.id == id; });
    lostAt_.clear();
    for (Member& m : members_) m.ready = false;
  }
  dirty_ = true;
}

void Session::kick(uint8_t id, const std::string& why) {
  if (!hosting_ || id == localId_) return;
  if (Remote* r = remoteOf(id)) {
    ByteWriter w;
    w.str(why);
    rawSend(r->conn, kKick, w.bytes(), Channel::Reliable);
    r->closeAt = now_ + 1000;
  }
  const Member* m = member(id);
  const std::string name = m ? m->name : "?";
  std::erase_if(members_, [&](const Member& k) { return k.id == id; });
  tokens_.erase(id);
  lostAt_.erase(id);
  systemChat(name + " was removed: " + why);
  emit(SessionEvent::Type::MemberLeft, id, name);
  dirty_ = true;
}

void Session::refuse(Remote& r, const std::string& why) {
  ByteWriter w;
  w.str(why);
  rawSend(r.conn, kRefuse, w.bytes(), Channel::Reliable);
  r.closeAt = now_ + 1500;
  PNR_INFO_CH(Net, "Refused %s: %s", transport_->address(r.conn).toString().c_str(), why.c_str());
}

// --- Running -------------------------------------------------------------------

void Session::update(uint64_t nowMs) {
  now_ = nowMs;
  if (!transport_ || state_ == SessionState::Idle || state_ == SessionState::Closed) return;
  transport_->update(nowMs);
  TransportEvent e;
  while (transport_ && transport_->poll(e)) onTransport(e);
  if (state_ == SessionState::Closed) return;

  if (hosting_) {
    // Refused or kicked connections go once their message is out.
    std::vector<ConnId> done;
    for (auto& [c, r] : remotes_)
      if (r.closeAt && (now_ >= r.closeAt || transport_->stats(c).queued == 0)) done.push_back(c);
    for (ConnId c : done) {
      transport_->disconnect(c);
      remotes_.erase(c);
    }
    // Places kept for lost members run out.
    std::vector<uint8_t> gone;
    for (auto& [id, t] : lostAt_)
      if (now_ - t > static_cast<uint64_t>(cfg_.rejoinSeconds) * 1000) gone.push_back(id);
    for (uint8_t id : gone) {
      const Member* m = member(id);
      const std::string name = m ? m->name : "?";
      lostAt_.erase(id);
      tokens_.erase(id);
      std::erase_if(members_, [&](const Member& k) { return k.id == id; });
      systemChat(name + " did not come back");
      emit(SessionEvent::Type::MemberLeft, id, name);
      dirty_ = true;
    }
    // Pings, every two seconds.
    if (now_ - lastPings_ >= 2000) {
      lastPings_ = now_;
      for (auto& [c, r] : remotes_)
        if (Member* m = memberMut(r.member)) {
          const int ping = static_cast<int>(transport_->stats(c).rttMs + 0.5f);
          if (std::abs(ping - m->pingMs) > 2) {
            m->pingMs = ping;
            dirty_ = true;
          }
        }
    }
    if (dirty_) sendLobby();
  } else if (state_ == SessionState::Reconnecting && !transport_->connected(hostConn_) && now_ > reconnectUntil_) {
    close("The host could not be reached again");
  }
}

void Session::onTransport(const TransportEvent& e) {
  using T = TransportEvent::Type;
  if (hosting_) {
    if (e.type == T::Connected) {
      Remote r;
      r.conn = e.conn;
      remotes_[e.conn] = r;
    } else if (e.type == T::Disconnected) {
      auto it = remotes_.find(e.conn);
      if (it == remotes_.end()) return;
      const uint8_t id = it->second.member;
      remotes_.erase(it);
      Member* m = memberMut(id);
      if (!m) return;
      const std::string name = m->name;
      if (state_ == SessionState::InGame) {
        m->connected = false;
        m->ready = false;
        lostAt_[id] = now_;
        systemChat(name + " lost the connection");
        emit(SessionEvent::Type::MemberLost, id, name);
      } else {
        std::erase_if(members_, [&](const Member& k) { return k.id == id; });
        tokens_.erase(id);
        systemChat(name + " left");
        emit(SessionEvent::Type::MemberLeft, id, name);
      }
      dirty_ = true;
    } else {
      onMessage(e.conn, e.data);
    }
    return;
  }
  // Client
  if (e.conn != hostConn_) return;
  if (e.type == T::Connected) {
    ByteWriter w;
    w.str(cfg_.gameId);
    w.str(cfg_.gameVersion);
    w.str(cfg_.contentHash);
    w.str(cfg_.password);
    w.str(name_);
    w.u64(token_);
    rawSend(hostConn_, kHello, w.bytes(), Channel::Reliable);
  } else if (e.type == T::Disconnected) {
    const bool lost = e.reason == DisconnectReason::TimedOut || e.reason == DisconnectReason::NoAnswer;
    if (lost && (state_ == SessionState::InGame || state_ == SessionState::Reconnecting) && token_) {
      if (state_ != SessionState::Reconnecting) {
        state_ = SessionState::Reconnecting;
        reconnectUntil_ = now_ + static_cast<uint64_t>(cfg_.rejoinSeconds) * 1000;
        PNR_WARN_CH(Net, "Lost the host, trying to reconnect");
        emit(SessionEvent::Type::Reconnecting);
      }
      if (now_ < reconnectUntil_) hostConn_ = transport_->connect(hostAddr_);
      else close("The host could not be reached again");
      return;
    }
    if (state_ == SessionState::Connecting && e.reason == DisconnectReason::NoAnswer)
      close("No answer from " + hostAddr_.toString());
    else if (e.reason == DisconnectReason::Refused) close("The host is full or not taking players");
    else if (closeReason_.empty()) close(std::string("Connection ") + reasonText(e.reason));
    else close(closeReason_);
  } else {
    onMessage(e.conn, e.data);
  }
}

void Session::onMessage(ConnId c, const std::vector<uint8_t>& d) {
  if (d.empty()) return;
  const uint8_t type = d[0];
  if (hosting_) {
    auto it = remotes_.find(c);
    if (it == remotes_.end() || it->second.closeAt) return;
    hostMessage(it->second, type, d.data() + 1, d.size() - 1);
  } else {
    clientMessage(type, d.data() + 1, d.size() - 1);
  }
}

void Session::hostMessage(Remote& rem, uint8_t type, const uint8_t* p, size_t n) {
  ByteReader r(p, n);
  if (rem.member == kNoMember) {
    if (type != kHello) return;
    const std::string game = r.str(), version = r.str(), content = r.str(), password = r.str();
    const std::string name = cleanText(r.str(), kMaxName);
    const uint64_t token = r.u64();
    if (!r.ok()) return refuse(rem, "Garbled request");
    if (game != cfg_.gameId) return refuse(rem, "This is a different game");
    if (version != cfg_.gameVersion)
      return refuse(rem, "The host runs version " + cfg_.gameVersion + ", you run " + version);
    if (content != cfg_.contentHash) return refuse(rem, "Your rules or mods are not the same as the host's");
    if (!cfg_.password.empty() && password != cfg_.password) return refuse(rem, "Wrong password");
    uint8_t id = kNoMember;
    bool back = false;
    if (token) {
      for (const auto& [mid, t] : tokens_)
        if (t == token) id = mid;
      if (id != kNoMember) {
        if (Remote* old = remoteOf(id)) {  // its old connection hasn't timed out yet
          transport_->disconnect(old->conn);
          remotes_.erase(old->conn);
        }
        back = true;
      }
    }
    if (id == kNoMember) {
      if (state_ == SessionState::InGame) return refuse(rem, "The game has already started");
      int count = 0;
      for (const Member& m : members_) count += m.local ? 0 : 1;
      if (count >= cfg_.maxMembers - (localId_ == kNoMember ? 0 : 1)) return refuse(rem, "The game is full");
      id = freeId();
      if (id == kNoMember) return refuse(rem, "The game is full");
    }
    rem.member = id;
    if (back) {
      Member* m = memberMut(id);
      m->connected = true;
      lostAt_.erase(id);
      systemChat(m->name + " is back");
    } else {
      Member m;
      m.id = id;
      m.name = name.empty() ? "Player" : name;
      // The same name twice gets a number.
      for (int k = 2; std::any_of(members_.begin(), members_.end(), [&](const Member& o) { return o.name == m.name; }); ++k)
        m.name = (name.empty() ? "Player" : name) + " (" + std::to_string(k) + ")";
      members_.push_back(m);
      tokens_[id] = randomToken();
      systemChat(m.name + " joined");
    }
    ByteWriter w;
    w.u8(id);
    w.u64(tokens_[id]);
    const std::vector<uint8_t> lobby = lobbyBytes();
    w.raw(lobby.data(), lobby.size());
    rawSend(rem.conn, kWelcome, w.bytes(), Channel::Reliable);
    dirty_ = true;
    sendLobby();
    emit(back ? SessionEvent::Type::MemberBack : SessionEvent::Type::MemberJoined, id, member(id)->name);
    return;
  }
  Member* m = memberMut(rem.member);
  if (!m) return;
  switch (type) {
    case kSetName: {
      const std::string name = cleanText(r.str(), kMaxName);
      if (r.ok() && !name.empty() && name != m->name) {
        m->name = name;
        dirty_ = true;
      }
      return;
    }
    case kSetReady:
      m->ready = r.u8() != 0;
      dirty_ = true;
      return;
    case kSetData: {
      std::string data = r.str();
      if (r.ok() && data.size() <= 4096 && data != m->data) {
        m->data = std::move(data);
        dirty_ = true;
      }
      return;
    }
    case kSetSettings: {
      std::string s = r.str();
      if (r.ok() && rem.member == leader() && s.size() <= 65536 && s != settings_) {
        settings_ = std::move(s);
        dirty_ = true;
      }
      return;
    }
    case kChat: {
      const std::string text = cleanText(r.str(), kMaxChat);
      if (!r.ok() || text.empty()) return;
      // At most five lines in three seconds.
      while (!rem.chatTimes.empty() && now_ - rem.chatTimes.front() > 3000) rem.chatTimes.pop_front();
      if (rem.chatTimes.size() >= 5) return;
      rem.chatTimes.push_back(now_);
      addChat(m->id, text);
      ByteWriter w;
      w.u8(m->id);
      w.str(m->name);
      w.str(text);
      broadcast(kChat, w.bytes());
      emit(SessionEvent::Type::Chat, m->id, text);
      return;
    }
    case kBye: {
      const std::string why = r.str();
      const uint8_t id = m->id;
      const std::string name = m->name;
      transport_->disconnect(rem.conn);
      remotes_.erase(rem.conn);
      std::erase_if(members_, [&](const Member& k) { return k.id == id; });
      tokens_.erase(id);
      lostAt_.erase(id);
      systemChat(name + " left");
      emit(SessionEvent::Type::MemberLeft, id, name);
      dirty_ = true;
      return;
    }
    default: {
      if (type < kFirstAppMessage) return;
      auto it = handlers_.find(type);
      if (it != handlers_.end()) it->second(rem.member, std::vector<uint8_t>(p, p + n));
    }
  }
}

void Session::clientMessage(uint8_t type, const uint8_t* p, size_t n) {
  ByteReader r(p, n);
  switch (type) {
    case kWelcome: {
      localId_ = r.u8();
      token_ = r.u64();
      if (!r.ok()) return;
      const bool rejoined = state_ == SessionState::Reconnecting || (state_ == SessionState::Connecting && wasInGame_);
      state_ = SessionState::Lobby;
      readLobby(r.cursor(), r.remaining());
      PNR_INFO_CH(Net, "%s %s as member %u", rejoined ? "Back in" : "Joined", hostAddr_.toString().c_str(), localId_);
      emit(rejoined ? SessionEvent::Type::Rejoined : SessionEvent::Type::Joined, localId_);
      return;
    }
    case kRefuse:
      closeReason_ = r.str();
      if (closeReason_.empty()) closeReason_ = "Refused";
      close(closeReason_);
      return;
    case kKick:
      closeReason_ = "Removed by the host: " + r.str();
      close(closeReason_);
      return;
    case kBye:
      closeReason_ = "The host ended the session";
      close(closeReason_);
      return;
    case kLobby:
      readLobby(p, n);
      return;
    case kChat: {
      const uint8_t from = r.u8();
      const std::string name = r.str(), text = r.str();
      if (!r.ok()) return;
      ChatLine l{from, name, text};
      chat_.push_back(l);
      if (chat_.size() > kMaxChatLog) chat_.pop_front();
      emit(SessionEvent::Type::Chat, from, text);
      return;
    }
    default: {
      if (type < kFirstAppMessage) return;
      auto it = handlers_.find(type);
      if (it != handlers_.end()) it->second(0, std::vector<uint8_t>(p, p + n));
    }
  }
}

}  // namespace pioneer::net

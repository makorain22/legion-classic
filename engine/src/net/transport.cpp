// Pioneer Engine - connections over datagrams (see transport.hpp).
#include "pioneer/net/transport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <random>

#include "pioneer/core/bytes.hpp"
#include "pioneer/core/log.hpp"

namespace pioneer::net {

namespace {
enum Kind : uint8_t { kConnect = 1, kAccept = 2, kRefuse = 3, kData = 4, kDisconnect = 5 };
enum MsgFlags : uint8_t { kReliable = 1, kMore = 2 };
constexpr size_t kPiece = 1100;        // payload of one reliable piece
constexpr size_t kDataHeader = 4 + 1 + 4 + 2 + 2 + 4;
constexpr size_t kRing = 512;          // datagrams remembered for acknowledgement
constexpr int kConnectEveryMs = 200;
}  // namespace

const char* reasonText(DisconnectReason r) {
  switch (r) {
    case DisconnectReason::None: return "none";
    case DisconnectReason::Local: return "closed here";
    case DisconnectReason::Remote: return "closed by the other side";
    case DisconnectReason::TimedOut: return "timed out";
    case DisconnectReason::Refused: return "refused";
    case DisconnectReason::NoAnswer: return "no answer";
    case DisconnectReason::Replaced: return "replaced by a new connection";
  }
  return "?";
}

struct Transport::Conn {
  ConnId id = 0;
  Address addr;
  bool connected = false, outgoing = false;
  bool fresh = false;  // made before the first update: its clock starts there
  uint32_t clientSalt = 0, serverSalt = 0;
  uint32_t token() const { return clientSalt ^ serverSalt ^ 0x9E3779B9u; }
  uint64_t started = 0, lastReceived = 0, lastSent = 0, lastConnectSent = 0, lastFlush = 0;
  uint16_t nextSeq = 0, remoteSeq = 0;
  uint32_t remoteBits = 0;
  bool anyReceived = false, ackPending = false;
  int receivedSinceAck = 0;
  struct Sent {
    uint16_t seq = 0;
    bool live = false;
    uint64_t time = 0;
    std::vector<uint16_t> msgs;
  };
  std::array<Sent, kRing> sent;
  struct Out {
    uint16_t id = 0;
    std::vector<uint8_t> data;
    bool more = false, acked = false;
    uint64_t lastSent = 0;
    uint32_t sends = 0;
  };
  std::deque<Out> outbox;
  uint16_t nextMsgId = 0;
  std::deque<std::vector<uint8_t>> unreliable;
  uint16_t expected = 0;
  std::map<uint16_t, std::pair<std::vector<uint8_t>, bool>> early;
  std::vector<uint8_t> partial;
  double allowance = 0;
  float rtt = 0;
  bool rttKnown = false;
  ConnectionStats st;
};

Transport::Transport(DatagramSocket& socket, TransportConfig cfg) : socket_(socket), cfg_(cfg) {
  std::random_device rd;
  saltState_ = (static_cast<uint64_t>(rd()) << 32) ^ rd() ^
               static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
               reinterpret_cast<uintptr_t>(this);
}

Transport::~Transport() { disconnectAll(); }

uint32_t Transport::salt() {
  // splitmix64
  uint64_t z = (saltState_ += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  const uint32_t s = static_cast<uint32_t>(z ^ (z >> 31));
  return s ? s : 1;
}

Transport::Conn* Transport::find(ConnId c) const {
  auto it = conns_.find(c);
  return it == conns_.end() ? nullptr : it->second.get();
}

Transport::Conn* Transport::findByAddress(const Address& a) const {
  for (const auto& [id, c] : conns_)
    if (c->addr == a) return c.get();
  return nullptr;
}

ConnId Transport::connect(const Address& to) {
  if (Conn* old = findByAddress(to)) close(*old, DisconnectReason::Local, true);
  auto c = std::make_unique<Conn>();
  c->id = nextId_++;
  c->addr = to;
  c->outgoing = true;
  c->clientSalt = salt();
  c->started = now_;
  c->lastFlush = now_;
  c->fresh = true;
  const ConnId id = c->id;
  conns_[id] = std::move(c);
  return id;
}

bool Transport::send(ConnId id, Channel ch, const uint8_t* data, size_t size) {
  Conn* c = find(id);
  if (!c) return false;
  if (ch == Channel::Unreliable) {
    if (size > kPiece) return false;
    c->unreliable.emplace_back(data, data + size);
    return true;
  }
  size_t at = 0;
  do {
    const size_t n = std::min(kPiece, size - at);
    Conn::Out o;
    o.id = c->nextMsgId++;
    o.data.assign(data + at, data + at + n);
    at += n;
    o.more = at < size;
    c->outbox.push_back(std::move(o));
  } while (at < size);
  return true;
}

void Transport::sendRaw(const Address& to, const std::vector<uint8_t>& d) { socket_.send(to, d.data(), d.size()); }

void Transport::close(Conn& c, DisconnectReason why, bool tell) {
  if (tell && c.connected) {
    ByteWriter w;
    w.u32(cfg_.protocolId);
    w.u8(kDisconnect);
    w.u32(c.token());
    for (int i = 0; i < 3; ++i) sendRaw(c.addr, w.bytes());
  }
  if (why != DisconnectReason::Local) {
    TransportEvent e;
    e.type = TransportEvent::Type::Disconnected;
    e.conn = c.id;
    e.reason = why;
    events_.push_back(std::move(e));
  }
  PNR_DEBUG_CH(Net, "Connection %u with %s ended: %s", c.id, c.addr.toString().c_str(), reasonText(why));
  conns_.erase(c.id);
}

void Transport::disconnect(ConnId id) {
  if (Conn* c = find(id)) close(*c, DisconnectReason::Local, true);
}

void Transport::disconnectAll() {
  while (!conns_.empty()) close(*conns_.begin()->second, DisconnectReason::Local, true);
}

bool Transport::connected(ConnId id) const {
  const Conn* c = find(id);
  return c && c->connected;
}

Address Transport::address(ConnId id) const {
  const Conn* c = find(id);
  return c ? c->addr : Address{};
}

ConnectionStats Transport::stats(ConnId id) const {
  const Conn* c = find(id);
  if (!c) return {};
  ConnectionStats s = c->st;
  s.rttMs = c->rtt;
  s.idleMs = now_ > c->lastReceived ? now_ - c->lastReceived : 0;
  s.queued = c->outbox.size();
  s.queuedBytes = 0;
  for (const Conn::Out& o : c->outbox) s.queuedBytes += o.data.size();
  return s;
}

std::vector<ConnId> Transport::connections() const {
  std::vector<ConnId> out;
  for (const auto& [id, c] : conns_)
    if (c->connected) out.push_back(id);
  return out;
}

bool Transport::poll(TransportEvent& out) {
  if (events_.empty()) return false;
  out = std::move(events_.front());
  events_.pop_front();
  return true;
}

void Transport::update(uint64_t nowMs) {
  now_ = std::max(now_, nowMs);
  Address from;
  std::vector<uint8_t> d;
  while (socket_.receive(from, d)) handle(from, d);

  std::vector<ConnId> ids;
  for (const auto& [id, c] : conns_) ids.push_back(id);
  for (ConnId id : ids) {
    Conn* c = find(id);
    if (!c) continue;
    if (c->fresh) {
      c->started = c->lastFlush = now_;
      c->fresh = false;
    }
    if (!c->connected) {
      if (now_ - c->started > static_cast<uint64_t>(cfg_.connectTimeoutMs)) {
        close(*c, DisconnectReason::NoAnswer, false);
        continue;
      }
      if (c->lastConnectSent == 0 || now_ - c->lastConnectSent >= static_cast<uint64_t>(kConnectEveryMs)) {
        ByteWriter w;
        w.u32(cfg_.protocolId);
        w.u8(kConnect);
        w.u32(c->clientSalt);
        // Padded, so an answer is never bigger than the question (no amplification).
        w.bytes().resize(64, 0);
        sendRaw(c->addr, w.bytes());
        c->lastConnectSent = now_ ? now_ : 1;
      }
      continue;
    }
    if (now_ - c->lastReceived > static_cast<uint64_t>(cfg_.timeoutMs)) {
      close(*c, DisconnectReason::TimedOut, false);
      continue;
    }
    flush(*c);
  }
}

void Transport::handle(const Address& from, const std::vector<uint8_t>& d) {
  ByteReader r(d);
  if (d.size() < 5 || r.u32() != cfg_.protocolId) {
    ++stray_;
    return;
  }
  const uint8_t kind = r.u8();
  Conn* c = findByAddress(from);
  switch (kind) {
    case kConnect: {
      const uint32_t cs = r.u32();
      if (!r.ok() || cs == 0) return;
      if (c && c->outgoing) return;
      auto accept = [&](const Conn& k) {
        ByteWriter w;
        w.u32(cfg_.protocolId);
        w.u8(kAccept);
        w.u32(k.clientSalt);
        w.u32(k.serverSalt);
        sendRaw(from, w.bytes());
      };
      if (c && c->clientSalt == cs) {
        accept(*c);  // our Accept was lost
        return;
      }
      if (c) close(*c, DisconnectReason::Replaced, false);
      if (!listening_ || static_cast<int>(conns_.size()) >= cfg_.maxConnections) {
        ByteWriter w;
        w.u32(cfg_.protocolId);
        w.u8(kRefuse);
        w.u32(cs);
        sendRaw(from, w.bytes());
        return;
      }
      auto n = std::make_unique<Conn>();
      n->id = nextId_++;
      n->addr = from;
      n->connected = true;
      n->clientSalt = cs;
      n->serverSalt = salt();
      n->started = n->lastReceived = n->lastFlush = now_;
      accept(*n);
      TransportEvent e;
      e.type = TransportEvent::Type::Connected;
      e.conn = n->id;
      events_.push_back(std::move(e));
      PNR_DEBUG_CH(Net, "Connection %u from %s", n->id, from.toString().c_str());
      conns_[n->id] = std::move(n);
      return;
    }
    case kAccept: {
      const uint32_t cs = r.u32(), ss = r.u32();
      if (!r.ok() || !c || !c->outgoing || c->connected || c->clientSalt != cs) return;
      c->connected = true;
      c->serverSalt = ss;
      c->lastReceived = c->lastFlush = now_;
      TransportEvent e;
      e.type = TransportEvent::Type::Connected;
      e.conn = c->id;
      events_.push_back(std::move(e));
      return;
    }
    case kRefuse: {
      const uint32_t cs = r.u32();
      if (r.ok() && c && c->outgoing && !c->connected && c->clientSalt == cs) close(*c, DisconnectReason::Refused, false);
      return;
    }
    case kDisconnect: {
      const uint32_t t = r.u32();
      if (r.ok() && c && c->connected && c->token() == t) close(*c, DisconnectReason::Remote, false);
      return;
    }
    case kData: {
      const uint32_t t = r.u32();
      if (!r.ok() || !c || !c->connected || c->token() != t) {
        ++stray_;
        return;
      }
      c->st.bytesReceived += d.size();
      handleData(*c, d.data() + 9, d.size() - 9);
      return;
    }
    default:
      ++stray_;
  }
}

void Transport::handleData(Conn& c, const uint8_t* p, size_t n) {
  ByteReader r(p, n);
  const uint16_t seq = r.u16(), ack = r.u16();
  const uint32_t bits = r.u32();
  if (!r.ok()) return;
  c.lastReceived = now_;
  ++c.st.packetsReceived;
  bool dup = false;
  if (!c.anyReceived) {
    c.anyReceived = true;
    c.remoteSeq = seq;
    c.remoteBits = 0;
  } else if (seqNewer(seq, c.remoteSeq)) {
    const uint32_t shift = static_cast<uint16_t>(seq - c.remoteSeq);
    if (shift > 32) c.remoteBits = 0;
    else if (shift == 32) c.remoteBits = 1u << 31;
    else c.remoteBits = (c.remoteBits << shift) | (1u << (shift - 1));
    c.remoteSeq = seq;
  } else {
    const uint32_t back = static_cast<uint16_t>(c.remoteSeq - seq);
    if (back == 0 || back > 32) dup = true;
    else if (c.remoteBits & (1u << (back - 1))) dup = true;
    else c.remoteBits |= 1u << (back - 1);
  }
  c.ackPending = true;
  // A burst bigger than the acknowledgement field: acknowledge part of it now,
  // or the start of the burst would never be acknowledged and go again.
  if (++c.receivedSinceAck >= 24) {
    ByteWriter w;
    w.u32(cfg_.protocolId);
    w.u8(kData);
    w.u32(c.token());
    Conn::Sent& s = c.sent[c.nextSeq % kRing];
    if (s.live) ++c.st.packetsLost;
    s.seq = c.nextSeq;
    s.live = true;
    s.time = now_;
    s.msgs.clear();
    w.u16(c.nextSeq++);
    w.u16(c.remoteSeq);
    w.u32(c.remoteBits);
    sendRaw(c.addr, w.bytes());
    ++c.st.packetsSent;
    c.st.bytesSent += w.bytes().size();
    c.receivedSinceAck = 0;
  }

  auto acked = [&](uint16_t s) {
    Conn::Sent& e = c.sent[s % kRing];
    if (!e.live || e.seq != s) return;
    e.live = false;
    const float sample = static_cast<float>(now_ - e.time);
    c.rtt = c.rttKnown ? c.rtt * 0.875f + sample * 0.125f : sample;
    c.rttKnown = true;
    if (c.outbox.empty()) return;
    const uint16_t front = c.outbox.front().id;
    for (uint16_t id : e.msgs) {
      const uint16_t at = static_cast<uint16_t>(id - front);
      if (at < c.outbox.size() && c.outbox[at].id == id) c.outbox[at].acked = true;
    }
  };
  acked(ack);
  for (uint16_t i = 0; i < 32; ++i)
    if (bits & (1u << i)) acked(static_cast<uint16_t>(ack - i - 1));
  while (!c.outbox.empty() && c.outbox.front().acked) c.outbox.pop_front();
  if (dup) return;

  while (r.ok() && !r.atEnd()) {
    const uint8_t flags = r.u8();
    const uint16_t id = (flags & kReliable) ? r.u16() : 0;
    const uint16_t len = r.u16();
    if (!r.ok() || r.remaining() < len) return;
    std::vector<uint8_t> data(r.cursor(), r.cursor() + len);
    r.skip(len);
    if (!(flags & kReliable)) {
      TransportEvent e;
      e.conn = c.id;
      e.channel = Channel::Unreliable;
      e.data = std::move(data);
      events_.push_back(std::move(e));
      continue;
    }
    const uint16_t ahead = static_cast<uint16_t>(id - c.expected);
    if (ahead >= 0x4000) continue;  // delivered already
    c.early.emplace(id, std::make_pair(std::move(data), (flags & kMore) != 0));
    for (auto it = c.early.find(c.expected); it != c.early.end(); it = c.early.find(c.expected)) {
      c.partial.insert(c.partial.end(), it->second.first.begin(), it->second.first.end());
      const bool more = it->second.second;
      c.early.erase(it);
      ++c.expected;
      if (more) continue;
      TransportEvent e;
      e.conn = c.id;
      e.channel = Channel::Reliable;
      e.data = std::move(c.partial);
      c.partial.clear();
      events_.push_back(std::move(e));
    }
  }
}

void Transport::flush(Conn& c) {
  const double rate = static_cast<double>(cfg_.sendRate);
  c.allowance = std::min(c.allowance + static_cast<double>(now_ - c.lastFlush) * rate / 1000.0,
                         rate / 8.0 + static_cast<double>(kMaxDatagram) * 4.0);
  c.lastFlush = now_;
  const uint64_t resendAfter =
      c.rttKnown ? static_cast<uint64_t>(std::clamp(c.rtt * 1.5f + 25.0f, 50.0f, 1000.0f)) : 250;
  for (int packets = 0; packets < 1024; ++packets) {
    ByteWriter w;
    w.u32(cfg_.protocolId);
    w.u8(kData);
    w.u32(c.token());
    const uint16_t seq = c.nextSeq;
    w.u16(seq);
    w.u16(c.remoteSeq);
    w.u32(c.remoteBits);
    const size_t limit = std::min(kMaxDatagram, static_cast<size_t>(std::max(0.0, c.allowance)));
    std::vector<uint16_t> ids;
    bool any = false;
    if (limit > kDataHeader + 16) {
      while (!c.unreliable.empty() && w.bytes().size() + 3 + c.unreliable.front().size() <= limit) {
        const auto& m = c.unreliable.front();
        w.u8(0);
        w.u16(static_cast<uint16_t>(m.size()));
        w.raw(m.data(), m.size());
        c.unreliable.pop_front();
        any = true;
      }
      const size_t n = std::min(c.outbox.size(), cfg_.window);
      for (size_t i = 0; i < n; ++i) {
        Conn::Out& o = c.outbox[i];
        if (o.acked || (o.sends && now_ - o.lastSent < resendAfter)) continue;
        if (w.bytes().size() + 5 + o.data.size() > limit) break;
        w.u8(static_cast<uint8_t>(kReliable | (o.more ? kMore : 0)));
        w.u16(o.id);
        w.u16(static_cast<uint16_t>(o.data.size()));
        w.raw(o.data.data(), o.data.size());
        if (o.sends++) ++c.st.resent;
        o.lastSent = now_;
        ids.push_back(o.id);
        any = true;
      }
    }
    if (!any && !c.ackPending && now_ - c.lastSent < static_cast<uint64_t>(cfg_.keepAliveMs)) break;
    Conn::Sent& s = c.sent[seq % kRing];
    if (s.live) ++c.st.packetsLost;  // never acknowledged
    s.seq = seq;
    s.live = true;
    s.time = now_;
    s.msgs = std::move(ids);
    ++c.nextSeq;
    sendRaw(c.addr, w.bytes());
    ++c.st.packetsSent;
    c.st.bytesSent += w.bytes().size();
    c.allowance -= static_cast<double>(w.bytes().size());
    c.lastSent = now_;
    c.ackPending = false;
    c.receivedSinceAck = 0;
    if (!any) break;
  }
}

}  // namespace pioneer::net

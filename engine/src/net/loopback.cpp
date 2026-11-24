// Pioneer Engine - the network in memory (tests).
#include "pioneer/net/loopback.hpp"

#include <algorithm>
#include <deque>

namespace pioneer::net {

class LoopbackNetwork::Socket final : public DatagramSocket {
 public:
  Socket(LoopbackNetwork& n, Address a) : net_(&n), addr_(a) {}
  ~Socket() override {
    if (net_) net_->detach(this);
  }
  bool send(const Address& to, const uint8_t* data, size_t size) override {
    return net_ && net_->post(addr_, to, data, size);
  }
  bool receive(Address& from, std::vector<uint8_t>& out) override {
    if (inbox_.empty()) return false;
    from = inbox_.front().first;
    out = std::move(inbox_.front().second);
    inbox_.pop_front();
    return true;
  }
  Address localAddress() const override { return addr_; }

  LoopbackNetwork* net_;
  Address addr_;
  std::deque<std::pair<Address, std::vector<uint8_t>>> inbox_;
};

std::unique_ptr<DatagramSocket> LoopbackNetwork::open(uint32_t ip, uint16_t port) {
  if (port == 0)
    while (sockets_.count({ip, nextPort_})) ++nextPort_;
  const Address a{ip, port ? port : nextPort_++};
  if (sockets_.count(a)) return nullptr;
  auto s = std::make_unique<Socket>(*this, a);
  sockets_[a] = s.get();
  return s;
}

void LoopbackNetwork::detach(Socket* s) {
  for (auto it = sockets_.begin(); it != sockets_.end(); ++it)
    if (it->second == s) {
      sockets_.erase(it);
      return;
    }
}

void LoopbackNetwork::setConditions(uint32_t a, uint32_t b, const LinkConditions& c) {
  links_[{std::min(a, b), std::max(a, b)}] = c;
}

void LoopbackNetwork::setOffline(uint32_t ip, bool off) { offline_[ip] = off; }

const LinkConditions& LoopbackNetwork::conditions(uint32_t a, uint32_t b) const {
  auto it = links_.find({std::min(a, b), std::max(a, b)});
  return it == links_.end() ? all_ : it->second;
}

bool LoopbackNetwork::post(const Address& from, const Address& to, const uint8_t* data, size_t size) {
  if (size > 65000) return false;
  ++stats_.sent;
  stats_.bytes += size;
  auto isOff = [&](uint32_t ip) {
    auto it = offline_.find(ip);
    return it != offline_.end() && it->second;
  };
  std::vector<Address> targets;
  if (to.isBroadcast()) {
    for (const auto& [a, s] : sockets_)
      if (a.port == to.port && a != from) targets.push_back(a);
  } else {
    targets.push_back(to);
  }
  for (const Address& t : targets) {
    const LinkConditions& c = conditions(from.ip, t.ip);
    if (isOff(from.ip) || isOff(t.ip) || (c.loss > 0 && rng_.below(1000000) < static_cast<uint32_t>(c.loss * 1e6f))) {
      ++stats_.lost;
      continue;
    }
    const int copies = (c.duplicate > 0 && rng_.below(1000000) < static_cast<uint32_t>(c.duplicate * 1e6f)) ? 2 : 1;
    for (int i = 0; i < copies; ++i) {
      const uint64_t jitter = c.jitterMs > 0 ? rng_.below(static_cast<uint32_t>(c.jitterMs) + 1) : 0;
      inFlight_.push_back({now_ + static_cast<uint64_t>(c.latencyMs) + jitter, order_++, from, t,
                           std::vector<uint8_t>(data, data + size)});
    }
  }
  if (inFlight_.size() && all_.latencyMs == 0 && all_.jitterMs == 0 && links_.empty()) advance(0);
  return true;
}

void LoopbackNetwork::deliver(Datagram&& d) {
  auto it = sockets_.find(d.to);
  if (it == sockets_.end()) {
    ++stats_.lost;
    return;
  }
  ++stats_.delivered;
  it->second->inbox_.emplace_back(d.from, std::move(d.data));
}

void LoopbackNetwork::advance(uint64_t ms) {
  now_ += ms;
  std::vector<Datagram> due;
  for (size_t i = 0; i < inFlight_.size();) {
    if (inFlight_[i].due <= now_) {
      due.push_back(std::move(inFlight_[i]));
      inFlight_[i] = std::move(inFlight_.back());
      inFlight_.pop_back();
    } else {
      ++i;
    }
  }
  std::sort(due.begin(), due.end(), [](const Datagram& a, const Datagram& b) {
    return a.due != b.due ? a.due < b.due : a.order < b.order;
  });
  for (Datagram& d : due) deliver(std::move(d));
}

}  // namespace pioneer::net

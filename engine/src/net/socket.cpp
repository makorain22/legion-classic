// Pioneer Engine - UDP sockets on Windows (Winsock) and POSIX.
#include "pioneer/net/socket.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using SockLen = int;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SockLen = socklen_t;
#endif

namespace pioneer::net {

namespace {
std::atomic<int> gStarted{0};

#ifdef _WIN32
using Native = SOCKET;
inline Native native(intptr_t h) { return static_cast<Native>(h); }
inline int lastError() { return WSAGetLastError(); }
inline bool wouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAECONNRESET; }
#else
using Native = int;
inline Native native(intptr_t h) { return static_cast<Native>(h); }
inline int lastError() { return errno; }
inline bool wouldBlock(int e) { return e == EAGAIN || e == EWOULDBLOCK || e == ECONNREFUSED || e == EINTR; }
#endif

sockaddr_in toSockaddr(const Address& a) {
  sockaddr_in s{};
  s.sin_family = AF_INET;
  s.sin_addr.s_addr = htonl(a.ip);
  s.sin_port = htons(a.port);
  return s;
}
}  // namespace

std::string Address::ipString() const {
  char buf[24];
  std::snprintf(buf, sizeof buf, "%u.%u.%u.%u", (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u);
  return buf;
}

std::string Address::toString() const { return ipString() + ":" + std::to_string(port); }

bool parseAddress(const std::string& textIn, uint16_t defaultPort, Address& out, std::string& error) {
  std::string text = textIn;
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.erase(text.begin());
  if (text.empty()) {
    error = "No address given";
    return false;
  }
  std::string host = text;
  uint16_t port = defaultPort;
  const size_t colon = text.rfind(':');
  if (colon != std::string::npos) {
    host = text.substr(0, colon);
    const std::string p = text.substr(colon + 1);
    char* end = nullptr;
    const long v = std::strtol(p.c_str(), &end, 10);
    if (p.empty() || *end != 0 || v <= 0 || v > 65535) {
      error = "'" + p + "' is not a port number";
      return false;
    }
    port = static_cast<uint16_t>(v);
  }
  // Four numbers: no lookup needed.
  unsigned a, b, c, d;
  char tail;
  if (std::sscanf(host.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) == 4) {
    if (a > 255 || b > 255 || c > 255 || d > 255) {
      error = "'" + host + "' is not an address";
      return false;
    }
    out = {(a << 24) | (b << 16) | (c << 8) | d, port};
    return true;
  }
  if (!netStartup()) {
    error = "The network can't be started";
    return false;
  }
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* res = nullptr;
  const int r = getaddrinfo(host.c_str(), nullptr, &hints, &res);
  bool found = false;
  if (r == 0) {
    for (addrinfo* i = res; i; i = i->ai_next)
      if (i->ai_family == AF_INET) {
        const auto* s = reinterpret_cast<const sockaddr_in*>(i->ai_addr);
        out = {ntohl(s->sin_addr.s_addr), port};
        found = true;
        break;
      }
    freeaddrinfo(res);
  }
  netShutdown();
  if (!found) error = "Can't find the computer '" + host + "'";
  return found;
}

bool netStartup() {
#ifdef _WIN32
  if (gStarted.fetch_add(1) == 0) {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      gStarted.fetch_sub(1);
      return false;
    }
  }
#else
  gStarted.fetch_add(1);
#endif
  return true;
}

void netShutdown() {
#ifdef _WIN32
  if (gStarted.fetch_sub(1) == 1) WSACleanup();
#else
  gStarted.fetch_sub(1);
#endif
}

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(uint16_t port, bool broadcast, std::string& error, bool shared) {
  close();
  if (!netStartup()) {
    error = "The network can't be started";
    return false;
  }
  started_ = true;
  const Native s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
  if (s == INVALID_SOCKET) {
#else
  if (s < 0) {
#endif
    error = "Can't make a network socket (" + std::to_string(lastError()) + ")";
    close();
    return false;
  }
  handle_ = static_cast<intptr_t>(s);
  const int one = 1;
  if (broadcast) setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof one);
  if (shared) {
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, reinterpret_cast<const char*>(&one), sizeof one);
#endif
  }
  // Big buffers: a burst of state for a player who rejoins mustn't overflow them.
  const int buf = 1 << 20;
  setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buf), sizeof buf);
  setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buf), sizeof buf);
#ifdef _WIN32
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
  // A datagram to a closed port makes Windows fail the next receive with
  // WSAECONNRESET; turn that off (SIO_UDP_CONNRESET).
  BOOL off = FALSE;
  DWORD ret = 0;
  WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12), &off, sizeof off, nullptr, 0, &ret, nullptr, nullptr);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  sockaddr_in addr = toSockaddr({0, port});
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {
    error = "Port " + std::to_string(port) + " is in use or not allowed (" + std::to_string(lastError()) + ")";
    close();
    return false;
  }
  sockaddr_in bound{};
  SockLen len = sizeof bound;
  getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
  local_ = {0x7F000001u, ntohs(bound.sin_port)};
  return true;
}

void UdpSocket::close() {
  if (handle_ != kInvalid) {
#ifdef _WIN32
    closesocket(native(handle_));
#else
    ::close(native(handle_));
#endif
    handle_ = kInvalid;
  }
  if (started_) {
    netShutdown();
    started_ = false;
  }
  local_ = {};
}

bool UdpSocket::send(const Address& to, const uint8_t* data, size_t size) {
  if (handle_ == kInvalid || size > 65000) return false;
  const sockaddr_in a = toSockaddr(to);
  const auto n = ::sendto(native(handle_), reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                          reinterpret_cast<const sockaddr*>(&a), sizeof a);
  return n >= 0 && static_cast<size_t>(n) == size;
}

bool UdpSocket::receive(Address& from, std::vector<uint8_t>& out) {
  if (handle_ == kInvalid) return false;
  uint8_t buf[2048];
  for (;;) {
    sockaddr_in a{};
    SockLen len = sizeof a;
    const auto n = ::recvfrom(native(handle_), reinterpret_cast<char*>(buf), sizeof buf, 0,
                              reinterpret_cast<sockaddr*>(&a), &len);
    if (n < 0) {
      if (wouldBlock(lastError())) {
#ifdef _WIN32
        if (lastError() == WSAECONNRESET) continue;
#else
        if (lastError() == ECONNREFUSED || lastError() == EINTR) continue;
#endif
      }
      return false;
    }
    from = {ntohl(a.sin_addr.s_addr), ntohs(a.sin_port)};
    out.assign(buf, buf + n);
    return true;
  }
}

}  // namespace pioneer::net

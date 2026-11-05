// pc_net.h - the PC's real network for RITA's emulator: her web calls are real HTTP, her web page and
// (while she hosts) her fleet list are real servers, and "joining" a network on the ladder means the
// PC really reaching that network's hub. Nothing here touches the PC's own WiFi: she rides along on
// whatever network the PC is on, like the fleet's PC programs do when another one drives the card.
//
// --shots (ritaPretend) skips all of this: main.cpp's pretend fleet answers instead, so the README's
// screenshots always look the same.
#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOGDI
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
// windows.h's macros that collide with LovyanGFX's names
#undef DOUBLE_CLICK
#undef far
#undef near
#undef FAR
#undef NEAR
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define closesocket close
#define ioctlsocket ioctl
#endif
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "WString.h"

extern bool ritaPretend;   // main.cpp: true for --shots (the pretend fleet), false in the window

namespace pcnet {

inline void startup() {
#ifdef _WIN32
  static bool done = false;
  if (!done) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); done = true; }
#endif
}

inline void blocking(SOCKET s, bool on) {
#ifdef _WIN32
  u_long nb = on ? 0 : 1;
  ioctlsocket(s, FIONBIO, &nb);
#else
  const int f = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, on ? f & ~O_NONBLOCK : f | O_NONBLOCK);
#endif
}

inline void timeouts(SOCKET s, int ms) {
#ifdef _WIN32
  DWORD t = ms;
#else
  timeval t = { ms / 1000, (ms % 1000) * 1000 };
#endif
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&t, sizeof(t));
}

// A TCP connection to host:port within ms, or INVALID_SOCKET
inline SOCKET dial(const std::string& host, int port, int ms) {
  startup();
  addrinfo hints = {}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) || !res) return INVALID_SOCKET;
  SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == INVALID_SOCKET) { freeaddrinfo(res); return s; }
  blocking(s, false);
  connect(s, res->ai_addr, (int)res->ai_addrlen);
  freeaddrinfo(res);
  fd_set w, e;
  FD_ZERO(&w); FD_SET(s, &w);
  FD_ZERO(&e); FD_SET(s, &e);
  timeval tv = { ms / 1000, (ms % 1000) * 1000 };
  int err = 0;
  socklen_t len = sizeof(err);
  if (select((int)s + 1, nullptr, &w, &e, &tv) <= 0 || FD_ISSET(s, &e) ||
      getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len) || err) {
    closesocket(s);
    return INVALID_SOCKET;
  }
  blocking(s, true);
  timeouts(s, ms);
  return s;
}

inline bool sendAll(SOCKET s, const std::string& data) {
  for (size_t at = 0; at < data.size();) {
    const int n = send(s, data.data() + at, (int)(data.size() - at), 0);
    if (n <= 0) return false;
    at += n;
  }
  return true;
}

inline bool splitUrl(const std::string& url, std::string* host, int* port, std::string* path) {
  if (url.compare(0, 7, "http://")) return false;   // the fleet speaks plain HTTP
  const size_t start = 7, slash = url.find('/', start);
  const std::string hp = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
  *path = slash == std::string::npos ? "/" : url.substr(slash);
  const size_t colon = hp.find(':');
  *host = hp.substr(0, colon);
  *port = colon == std::string::npos ? 80 : atoi(hp.c_str() + colon + 1);
  return host->size() > 0;
}

inline std::string lower(std::string s) {
  for (char& c : s) c = (char)tolower((unsigned char)c);
  return s;
}

// What the PC's own address is toward host (the one robots and RIFT see), "" if there's no route
inline std::string localAddressToward(const std::string& host) {
  startup();
  SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == INVALID_SOCKET) return "";
  sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(5000);
  inet_pton(AF_INET, host.c_str(), &to.sin_addr);
  std::string out;
  if (!connect(s, (sockaddr*)&to, sizeof(to))) {
    sockaddr_in me = {};
    socklen_t len = sizeof(me);
    if (!getsockname(s, (sockaddr*)&me, &len)) {
      char buf[INET_ADDRSTRLEN] = {0};
      inet_ntop(AF_INET, &me.sin_addr, buf, sizeof(buf));
      out = buf;
    }
  }
  closesocket(s);
  return out == "0.0.0.0" ? "" : out;
}

// The PC's default gateways (where a hub on the network it's on would be)
inline std::vector<std::string> gateways() {
  std::vector<std::string> out;
#ifdef _WIN32
  ULONG size = 16 * 1024;
  std::vector<uint8_t> buf(size);
  auto* a = (IP_ADAPTER_ADDRESSES*)buf.data();
  if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, a, &size) == ERROR_BUFFER_OVERFLOW) {
    buf.resize(size);
    a = (IP_ADAPTER_ADDRESSES*)buf.data();
  }
  if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr, a, &size) != NO_ERROR) return out;
  for (; a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp) continue;
    for (auto* g = a->FirstGatewayAddress; g; g = g->Next) {
      char ip[INET_ADDRSTRLEN] = {0};
      inet_ntop(AF_INET, &((sockaddr_in*)g->Address.lpSockaddr)->sin_addr, ip, sizeof(ip));
      if (strcmp(ip, "0.0.0.0")) out.push_back(ip);
    }
  }
#endif
  return out;
}

// ---------------------------------------------------------------------------
// An HTTP response being read: the headers are in, the body comes off the socket as it's asked for
// ---------------------------------------------------------------------------
struct Response {
  SOCKET s = INVALID_SOCKET;
  std::string pending;   // body bytes already read with the headers
  long left = -1;        // Content-Length still to come (-1: until the connection closes)
  bool chunked = false, eof = false;
  int code = 0;

  void close() {
    if (s != INVALID_SOCKET) closesocket(s);
    s = INVALID_SOCKET;
    eof = true;
  }
  // Up to n bytes, waiting for at least one (the socket's timeout). 0: the body is over.
  int read(uint8_t* out, int n) {
    if (!pending.empty()) {
      const int k = std::min<int>(n, (int)pending.size());
      memcpy(out, pending.data(), k);
      pending.erase(0, k);
      return k;
    }
    if (eof || s == INVALID_SOCKET || left == 0) return 0;
    const int want = left > 0 ? (int)std::min<long>(n, left) : n;
    const int k = recv(s, (char*)out, want, 0);
    if (k <= 0) { eof = true; return 0; }
    if (left > 0) left -= k;
    return k;
  }
  // Bytes ready now, without waiting (-1: the stream is over)
  int ready() {
    if (!pending.empty()) return (int)pending.size();
    if (eof || s == INVALID_SOCKET || left == 0) return -1;
    fd_set r;
    FD_ZERO(&r); FD_SET(s, &r);
    timeval tv = { 0, 0 };
    if (select((int)s + 1, &r, nullptr, nullptr, &tv) <= 0) return 0;
    char buf[4096];
    const int want = left > 0 ? (int)std::min<long>(sizeof(buf), left) : (int)sizeof(buf);
    const int k = recv(s, buf, want, 0);
    if (k <= 0) { eof = true; return -1; }
    if (left > 0) left -= k;
    pending.append(buf, k);
    return k;
  }
  std::string rest() {   // the whole remaining body (chunked bodies decoded)
    std::string all = pending;
    pending.clear();
    char buf[4096];
    while (!eof && s != INVALID_SOCKET && left != 0) {
      const int want = left > 0 ? (int)std::min<long>(sizeof(buf), left) : (int)sizeof(buf);
      const int k = recv(s, buf, want, 0);
      if (k <= 0) { eof = true; break; }
      if (left > 0) left -= k;
      all.append(buf, k);
    }
    if (!chunked) return all;
    std::string body;
    for (size_t at = 0; at < all.size();) {
      const size_t eol = all.find("\r\n", at);
      if (eol == std::string::npos) break;
      const long n = strtol(all.c_str() + at, nullptr, 16);
      if (n <= 0) break;
      body += all.substr(eol + 2, n);
      at = eol + 2 + n + 2;
    }
    return body;
  }
};

// One request; the response's headers read. code <= 0: no answer (-1 can't connect, -11 timed out).
inline int request(const std::string& method, const std::string& url, const std::vector<std::string>& headers,
                   const std::string& body, int ms, Response* r) {
  std::string host, path;
  int port;
  r->close();
  *r = Response();
  if (!splitUrl(url, &host, &port, &path)) return r->code = -1;
  r->s = dial(host, port, ms);
  if (r->s == INVALID_SOCKET) return r->code = -1;
  std::string req = method + " " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) +
                    "\r\nUser-Agent: ESP32HTTPClient\r\nConnection: close\r\n";
  for (const std::string& h : headers) req += h + "\r\n";
  if (method == "POST") req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  req += "\r\n" + body;
  if (!sendAll(r->s, req)) { r->close(); return r->code = -1; }
  std::string head;
  char buf[2048];
  size_t end;
  while ((end = head.find("\r\n\r\n")) == std::string::npos) {
    const int k = recv(r->s, buf, sizeof(buf), 0);
    if (k <= 0) { r->close(); return r->code = -11; }
    head.append(buf, k);
    if (head.size() > 64 * 1024) { r->close(); return r->code = -7; }
  }
  r->pending = head.substr(end + 4);
  head.resize(end);
  const size_t sp = head.find(' ');
  r->code = sp == std::string::npos ? -7 : atoi(head.c_str() + sp + 1);
  const std::string low = lower(head);
  const size_t cl = low.find("\r\ncontent-length:");
  if (cl != std::string::npos) {
    r->left = strtol(low.c_str() + cl + 17, nullptr, 10) - (long)r->pending.size();
    if (r->left < 0) { r->pending.resize(r->pending.size() + r->left); r->left = 0; }
  }
  r->chunked = low.find("transfer-encoding: chunked") != std::string::npos;
  return r->code;
}

// ---------------------------------------------------------------------------
// A tiny HTTP server: one request per connection, answered from the loop (like the ESP32's WebServer)
// ---------------------------------------------------------------------------
struct Server {
  SOCKET listener = INVALID_SOCKET;
  int port = 0;
  bool warned = false;

  bool listen(int p) {
    startup();
    port = p;
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) return false;
#ifdef _WIN32
    BOOL excl = TRUE;   // never share a port another program (RIFT on :5000) already has
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
#endif
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons(p);
    a.sin_addr.s_addr = INADDR_ANY;
    if (bind(listener, (sockaddr*)&a, sizeof(a)) || ::listen(listener, 8)) {
      closesocket(listener);
      listener = INVALID_SOCKET;
      if (!warned) printf("(RITA can't serve on port %d here: something on this PC already has it)\n", p);
      warned = true;
      return false;
    }
    blocking(listener, false);
    return true;
  }
  void stop() {
    if (listener != INVALID_SOCKET) closesocket(listener);
    listener = INVALID_SOCKET;
  }
  // A connection waiting, with its request read (method, path, query, body), or INVALID_SOCKET
  SOCKET next(std::string* method, std::string* path, std::string* query, std::string* body, std::string* peer) {
    if (listener == INVALID_SOCKET) return INVALID_SOCKET;
    sockaddr_in from = {};
    socklen_t len = sizeof(from);
    SOCKET c = accept(listener, (sockaddr*)&from, &len);
    if (c == INVALID_SOCKET) return c;
    blocking(c, true);
    timeouts(c, 1500);
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
    *peer = ip;
    std::string got;
    char buf[2048];
    size_t end;
    while ((end = got.find("\r\n\r\n")) == std::string::npos) {
      const int k = recv(c, buf, sizeof(buf), 0);
      if (k <= 0 || got.size() > 64 * 1024) { closesocket(c); return INVALID_SOCKET; }
      got.append(buf, k);
    }
    const std::string head = got.substr(0, end);
    *body = got.substr(end + 4);
    const size_t cl = lower(head).find("\r\ncontent-length:");
    const long want = cl == std::string::npos ? 0 : strtol(head.c_str() + cl + 17, nullptr, 10);
    while ((long)body->size() < want) {
      const int k = recv(c, buf, sizeof(buf), 0);
      if (k <= 0) break;
      body->append(buf, k);
    }
    const size_t sp1 = head.find(' '), sp2 = head.find(' ', sp1 + 1);
    *method = head.substr(0, sp1);
    const std::string target = head.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t q = target.find('?');
    *path = target.substr(0, q);
    *query = q == std::string::npos ? "" : target.substr(q + 1);
    return c;
  }
};

inline std::string urlDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') out += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) { out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
    else out += s[i];
  }
  return out;
}

inline std::string formArg(const std::string& form, const std::string& name, bool* found) {
  for (size_t at = 0; at <= form.size();) {
    size_t amp = form.find('&', at);
    if (amp == std::string::npos) amp = form.size();
    const std::string pair = form.substr(at, amp - at);
    const size_t eq = pair.find('=');
    if (urlDecode(pair.substr(0, eq)) == name) {
      *found = true;
      return eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
    }
    at = amp + 1;
  }
  return "";
}

// ---------------------------------------------------------------------------
// The ladder, seen from the PC: which fleet networks are around, and which hub answers for each
// ---------------------------------------------------------------------------
// A network is "around" if the PC's WiFi can see its name, or if its hub answers on the network the
// PC is on (a hub's /ping says whose it is: "RIFT alive"). Joining one works when its hub answers,
// or when the PC itself is on that WiFi; the PC's WiFi is never switched.
struct Ladder {
  std::mutex lock;
  std::vector<std::string> seen;                 // SSIDs from the PC's own WiFi scan
  std::vector<std::pair<std::string, std::string>> hubs;   // (network name, hub address) answering now
  std::string pcSsid;                            // the WiFi the PC itself is on
  std::string extraHub;                          // --hub HOST or RITA_HUB
  std::atomic<bool> running{false};

  static std::string run(const char* cmd) {
    std::string out;
#ifdef _WIN32
    FILE* p = _popen(cmd, "r");
#else
    FILE* p = popen(cmd, "r");
#endif
    if (!p) return out;
    char buf[512];
    while (fgets(buf, sizeof(buf), p)) out += buf;
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    return out;
  }
  static std::string after(const std::string& line) {
    const size_t c = line.find(':');
    if (c == std::string::npos) return "";
    std::string v = line.substr(c + 1);
    while (!v.empty() && (v[0] == ' ')) v.erase(0, 1);
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
    return v;
  }

  void scanWifi() {
#ifdef _WIN32
    std::vector<std::string> ssids;
    std::string mine;
    const std::string nets = run("netsh wlan show networks 2>nul");
    for (size_t at = 0; at < nets.size();) {
      size_t eol = nets.find('\n', at);
      if (eol == std::string::npos) eol = nets.size();
      const std::string line = nets.substr(at, eol - at);
      if (line.compare(0, 5, "SSID ") == 0) ssids.push_back(after(line));
      at = eol + 1;
    }
    const std::string ifs = run("netsh wlan show interfaces 2>nul");
    for (size_t at = 0; at < ifs.size();) {
      size_t eol = ifs.find('\n', at);
      if (eol == std::string::npos) eol = ifs.size();
      std::string line = ifs.substr(at, eol - at);
      while (!line.empty() && line[0] == ' ') line.erase(0, 1);
      if (line.compare(0, 4, "SSID") == 0 && line.compare(0, 5, "SSID ") == 0 && line.find("BSSID") == std::string::npos) mine = after(line);
      at = eol + 1;
    }
    std::lock_guard<std::mutex> g(lock);
    seen = ssids;
    pcSsid = mine;
#endif
  }

  void probeHubs() {
    std::vector<std::string> tries;
    if (!extraHub.empty()) tries.push_back(extraHub);
    tries.push_back("127.0.0.1");
    for (const std::string& g : gateways()) tries.push_back(g);
    std::vector<std::pair<std::string, std::string>> found;
    for (const std::string& host : tries) {
      Response r;
      if (request("GET", "http://" + host + ":5000/ping", {}, "", 800, &r) == 200) {
        const std::string text = r.rest();
        const size_t sp = text.find(' ');
        const std::string name = text.substr(0, sp);
        bool dup = false;
        for (auto& f : found) dup |= f.first == name;
        if (!name.empty() && !dup) found.push_back({ name, host });
      }
      r.close();
    }
    std::lock_guard<std::mutex> g(lock);
    hubs = found;
  }

  void start() {
    if (running.exchange(true)) return;
    if (const char* h = getenv("RITA_HUB")) if (!extraHub.size()) extraHub = h;
    probeHubs();
    scanWifi();
    std::thread([this]() {
      for (int n = 1;; n++) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        probeHubs();
        if (n % 5 == 0) scanWifi();   // the PC's WiFi scan is slow: every 15 s
      }
    }).detach();
  }

  std::vector<std::string> around() {
    std::lock_guard<std::mutex> g(lock);
    std::vector<std::string> out = seen;
    for (auto& h : hubs) out.push_back(h.first);
    if (!pcSsid.empty()) out.push_back(pcSsid);
    return out;
  }
  // The hub's address for network ssid, "" if it can't be reached from this PC
  std::string hubFor(const std::string& ssid) {
    std::lock_guard<std::mutex> g(lock);
    for (auto& h : hubs) if (h.first == ssid) return h.second;
    if (pcSsid == ssid) {
      const std::vector<std::string> gw = gateways();
      if (!gw.empty()) return gw[0];
    }
    return "";
  }
};

inline Ladder& ladder() { static Ladder l; return l; }

}  // namespace pcnet

// WiFi.h - RITA's WiFi on the PC. Her own ladder code runs unchanged on top of it:
//   window   the real thing, seen from the PC (pc_net.h): a scan finds the fleet networks the PC's
//            WiFi can see and the hubs that answer on the network the PC is on; joining one works
//            when its hub answers. Her addresses are the PC's. The PC's WiFi is never switched.
//   --shots  a pretend WiFi: one scan finds RIFT and joining it always works.
#pragma once
#include <Arduino.h>
#include "pc_net.h"

#define WIFI_STA 1
#define WIFI_AP_STA 3
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6

struct IPAddress {
  uint8_t b[4] = { 0, 0, 0, 0 };
  IPAddress() {}
  IPAddress(int a, int c, int d, int e) { b[0] = a; b[1] = c; b[2] = d; b[3] = e; }
  static IPAddress parse(const std::string& s) {
    IPAddress ip;
    unsigned a, c, d, e;
    if (sscanf(s.c_str(), "%u.%u.%u.%u", &a, &c, &d, &e) == 4) ip = IPAddress(a, c, d, e);
    return ip;
  }
  String toString() const {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return buf;
  }
};

// A stream off the network (an MJPEG camera, DREAM's voice): what HTTPClient::getStreamPtr() gives
class WiFiClient {
 public:
  pcnet::Response* r = nullptr;
  std::string fake;                         // --shots: main.cpp's pretend stream
  bool (*refill)(std::string*) = nullptr;

  int available() {
    if (refill) { if (fake.empty()) refill(&fake); return (int)fake.size(); }
    if (!r) return 0;
    const int n = r->ready();
    return n < 0 ? 0 : n;
  }
  bool connected() { return refill ? true : r && !r->eof; }
  int read(uint8_t* buf, size_t n) {
    if (refill) {
      const size_t k = std::min(n, fake.size());
      memcpy(buf, fake.data(), k);
      fake.erase(0, k);
      return (int)k;
    }
    return r ? r->read(buf, (int)n) : 0;
  }
  int readBytes(uint8_t* buf, size_t n) {   // waits (the socket's timeout) until n or the end
    size_t got = 0;
    while (got < n) {
      const int k = read(buf + got, n - got);
      if (k <= 0) break;
      got += k;
    }
    return (int)got;
  }
};

class WiFiClass {
  bool up = false;
  int found = -2;   // -2: no scan running; otherwise what scanComplete() gives
  std::vector<std::string> nets;
  std::string joining, hub, me;

 public:
  void mode(int) {}
  void setAutoReconnect(bool) {}
  int status() {
    if (ritaPretend) return up ? WL_CONNECTED : WL_DISCONNECTED;
    if (!joining.empty()) {               // on it while its hub answers, like a WiFi link that's up
      hub = pcnet::ladder().hubFor(joining);
      if (!hub.empty() && me.empty()) me = pcnet::localAddressToward(hub);
      up = !hub.empty();
    }
    return up ? WL_CONNECTED : WL_DISCONNECTED;
  }
  IPAddress gatewayIP() { return ritaPretend ? IPAddress(192, 168, 4, 1) : IPAddress::parse(hub); }
  IPAddress localIP() {
    if (ritaPretend) return IPAddress(192, 168, 4, 1);
    if (me == "127.0.0.1" || me.empty()) {   // a hub on this PC: the address the rest of the fleet sees
      const std::vector<std::string> gw = pcnet::gateways();
      const std::string out = gw.empty() ? "" : pcnet::localAddressToward(gw[0]);
      if (!out.empty()) return IPAddress::parse(out);
    }
    return IPAddress::parse(me);
  }
  void scanNetworks(bool) {
    if (ritaPretend) { nets = { "RIFT" }; found = 1; return; }
    pcnet::ladder().start();
    nets = pcnet::ladder().around();
    found = (int)nets.size();
  }
  int scanComplete() const { return found; }
  String SSID(int i) const { return i >= 0 && i < (int)nets.size() ? String(nets[i]) : String(); }
  void scanDelete() { found = -2; nets.clear(); }
  void begin(const char* ssid, const char*) {
    if (ritaPretend) { up = true; return; }
    joining = ssid;
    hub = me = "";
    up = false;
    printf("(RITA joining %s: %s)\n", ssid, pcnet::ladder().hubFor(ssid).empty() ? "no hub of it answers from this PC" : "its hub answers");
    fflush(stdout);
  }
  void disconnect() { up = false; joining = hub = me = ""; }
  // Hosting her own network: a PC can't be a WiFi access point for her, so the robots can't join it,
  // but her fleet list on :5000 still runs, for anything on the PC's network.
  bool softAPConfig(IPAddress, IPAddress, IPAddress) { return true; }
  bool softAP(const char*, const char*, int = 1, int = 0, int = 4) { return true; }
  bool softAPdisconnect(bool) { return true; }
};
static WiFiClass WiFi;

// The board sets its clock from NTP; a PC's clock is already right (time() reads it).
#include <ctime>
inline void configTime(long, int, const char*, const char* = nullptr, const char* = nullptr) {}

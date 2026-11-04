// WiFi.h - a pretend WiFi: one scan finds RIFT's network and joining it always works, so RITA's own
// ladder code runs as it does on the board.
#pragma once
#include <Arduino.h>

#define WIFI_STA 1
#define WIFI_AP_STA 3
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6

struct IPAddress {
  IPAddress() {}
  IPAddress(int, int, int, int) {}
  String toString() const { return "192.168.4.1"; }
};

class WiFiClass {
  bool up = false;
  int found = -2;   // -2: no scan running; otherwise what scanComplete() gives

 public:
  void mode(int) {}
  void setAutoReconnect(bool) {}
  int status() const { return up ? WL_CONNECTED : WL_DISCONNECTED; }
  IPAddress gatewayIP() const { return IPAddress(); }
  IPAddress localIP() const { return IPAddress(); }
  void scanNetworks(bool) { found = 1; }
  int scanComplete() const { return found; }
  String SSID(int) const { return "RIFT"; }
  void scanDelete() { found = -2; }
  void begin(const char*, const char*) { up = true; }
  void disconnect() { up = false; }
  bool softAPConfig(IPAddress, IPAddress, IPAddress) { return true; }
  bool softAP(const char*, const char*, int = 1, int = 0, int = 4) { return true; }
  bool softAPdisconnect(bool) { return true; }
};
static WiFiClass WiFi;

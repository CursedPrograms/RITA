// WebServer.h for the PC preview: RITA's fleet list while she hosts. The pretend WiFi always finds
// RIFT, so she never hosts here; this only has to build.
#pragma once
#include <Arduino.h>
#include <functional>
#include "WiFi.h"

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };

struct WebClient { IPAddress remoteIP() const { return IPAddress(); } };

class WebServer {
 public:
  explicit WebServer(int) {}
  void on(const char*, std::function<void()>) {}
  void on(const char*, HTTPMethod, std::function<void()>) {}
  void begin() {}
  void stop() {}
  void handleClient() {}
  void send(int, const char*, const String&) {}
  void send_P(int, const char*, const char*, size_t = 0) {}
  String arg(const char*) { return ""; }
  bool hasArg(const char*) { return false; }
  WebClient client() { return WebClient(); }
};

// WebServer.h - RITA's servers on the PC: her web page (:80, which RIFT also calls for /chirp) and,
// while she hosts, the fleet list on :5000. Real sockets in the window (pc_net.h), answered from her
// loop one request at a time like the ESP32's WebServer. With --shots nothing listens.
#pragma once
#include <Arduino.h>
#include <functional>
#include "WiFi.h"

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };

struct WebClient {
  std::string peer;
  IPAddress remoteIP() const { return IPAddress::parse(peer); }
};

class WebServer {
  struct Route { std::string path; HTTPMethod method; std::function<void()> fn; };
  std::vector<Route> routes;
  pcnet::Server server;
  int port;
  SOCKET c = INVALID_SOCKET;
  std::string method, path, query, body;
  WebClient who;

  void reply(int code, const char* type, const char* data, size_t len) {
    if (c == INVALID_SOCKET) return;
    const char* why = code == 200 ? "OK" : code == 404 ? "Not Found" : code == 400 ? "Bad Request" : code == 409 ? "Conflict" : "Error";
    const std::string head = "HTTP/1.1 " + std::to_string(code) + " " + why + "\r\nContent-Type: " + type +
                             "\r\nContent-Length: " + std::to_string(len) + "\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n";
    pcnet::sendAll(c, head);
    pcnet::sendAll(c, std::string(data, len));
    closesocket(c);
    c = INVALID_SOCKET;
  }

 public:
  explicit WebServer(int p) : port(p) {}
  void on(const char* p, std::function<void()> fn) { routes.push_back({ p, HTTP_ANY, fn }); }
  void on(const char* p, HTTPMethod m, std::function<void()> fn) { routes.push_back({ p, m, fn }); }
  void begin() {
    if (ritaPretend) return;
    if (server.listen(port)) printf("(RITA serving on port %d)\n", port);
  }
  void stop() { server.stop(); }
  void handleClient() {
    if (ritaPretend) return;
    c = server.next(&method, &path, &query, &body, &who.peer);
    if (c == INVALID_SOCKET) return;
    for (const Route& r : routes) {
      if (r.path != path) continue;
      if (r.method == HTTP_POST && method != "POST") continue;
      if (r.method == HTTP_GET && method != "GET") continue;
      r.fn();
      if (c != INVALID_SOCKET) reply(500, "text/plain", "no answer", 9);
      return;
    }
    reply(404, "text/plain", "not found", 9);
  }
  void send(int code, const char* type, const String& content) { reply(code, type, content.c_str(), content.length()); }
  void send_P(int code, const char* type, const char* data, size_t len = 0) { reply(code, type, data, len ? len : strlen(data)); }
  String arg(const char* name) {
    if (!strcmp(name, "plain")) return String(body);
    bool found = false;
    std::string v = pcnet::formArg(query, name, &found);
    if (!found && method == "POST") v = pcnet::formArg(body, name, &found);
    return String(v);
  }
  bool hasArg(const char* name) {
    if (!strcmp(name, "plain")) return method == "POST" && !body.empty();
    bool found = false;
    pcnet::formArg(query, name, &found);
    if (!found && method == "POST") pcnet::formArg(body, name, &found);
    return found;
  }
  WebClient client() { return who; }
};

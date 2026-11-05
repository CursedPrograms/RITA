// HTTPClient.h - RITA's web calls on the PC. In the window they're real HTTP to the real fleet
// (pc_net.h), timeouts and all, and they hold her loop up the way they do on the board; with --shots
// they go to fakeHttp() in main.cpp, the pretend fleet.
#pragma once
#include <Arduino.h>
#include "WiFi.h"

int fakeHttp(const String& url, const String& body, String* answer);
bool fakeStream(const String& url, WiFiClient* into);   // main.cpp: --shots' pretend camera

class HTTPClient {
  String url, answer;
  int timeoutMs = 5000;
  std::vector<std::string> headers;
  pcnet::Response r;
  WiFiClient stream;

  int real(const char* method, const String& body) {
    const int code = pcnet::request(method, url.c_str(), headers, body.c_str(), timeoutMs, &r);
    // /status and the fleet list are polled all the time; everything else is her doing something
    if (url.indexOf("/status") < 0 && url.indexOf("/fleet") < 0 && url.indexOf("/register") < 0 && url.indexOf("/speech/") < 0)
      printf("%s %s %s -> %d\n", method, url.c_str(), body.c_str(), code);
    fflush(stdout);
    return code;
  }

 public:
  ~HTTPClient() { end(); }
  void begin(const String& u) { end(); url = u; headers.clear(); }
  void setTimeout(int ms) { timeoutMs = ms; }
  void addHeader(const char* k, const char* v) { headers.push_back(std::string(k) + ": " + v); }
  int GET() {
    if (!ritaPretend) return real("GET", "");
    return fakeStream(url, &stream) ? 200 : fakeHttp(url, "", &answer);
  }
  int POST(const String& body) { return ritaPretend ? fakeHttp(url, body, &answer) : real("POST", body); }
  String getString() { return ritaPretend ? answer : String(r.rest()); }
  int getSize() { return ritaPretend ? (int)answer.length() : r.left < 0 ? -1 : (int)(r.left + r.pending.size()); }
  WiFiClient* getStreamPtr() {
    if (!ritaPretend) stream.r = &r;
    return &stream;
  }
  bool connected() { return ritaPretend || !r.eof; }
  void end() {
    r.close();
    stream.r = nullptr;
    stream.refill = nullptr;
    stream.fake.clear();
  }
};

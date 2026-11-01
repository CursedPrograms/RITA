// HTTPClient.h - RITA's web calls go to fakeHttp() in main.cpp, the pretend fleet.
#pragma once
#include <Arduino.h>

int fakeHttp(const String& url, const String& body, String* answer);

class HTTPClient {
  String url, answer;

 public:
  void begin(const String& u) { url = u; }
  void setTimeout(int) {}
  void addHeader(const char*, const char*) {}
  int GET() { return fakeHttp(url, "", &answer); }
  int POST(const String& body) { return fakeHttp(url, body, &answer); }
  String getString() { return answer; }
  void end() {}
};

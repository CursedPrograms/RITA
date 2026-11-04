// ArduinoOTA.h for the PC preview: updates over WiFi are the board's business; here they do nothing.
#pragma once
#include <functional>

class ArduinoOTAClass {
 public:
  void setHostname(const char*) {}
  void setPassword(const char*) {}
  void onStart(std::function<void()>) {}
  void begin() {}
  void handle() {}
};
inline ArduinoOTAClass ArduinoOTA;

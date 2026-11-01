// Preferences.h - the flash store. It answers "already calibrated": there's no touch panel to calibrate.
#pragma once
#include <Arduino.h>

class Preferences {
 public:
  void begin(const char*, bool) {}
  size_t getBytes(const char*, void*, size_t len) { return len; }
  size_t putBytes(const char*, const void*, size_t len) { return len; }
  void end() {}
};

// NimBLEDevice.h - on the PC: Bluetooth LE (NimBLE, as on her board) that never hears anyone. The
// emulator has no BLE radio of hers, so DRIVE says a robot isn't heard over Bluetooth, like the board
// does with nobody in range.
#pragma once
#include <Arduino.h>
#include <string>

class NimBLEAdvertisedDevice {
 public:
  bool haveManufacturerData() const { return false; }
  std::string getManufacturerData() const { return std::string(); }
  int getRSSI() const { return -100; }
};
class NimBLEScanCallbacks {
 public:
  virtual ~NimBLEScanCallbacks() {}
  virtual void onResult(const NimBLEAdvertisedDevice*) {}
};
class NimBLEAdvertisementData {
 public:
  bool setFlags(uint8_t) { return true; }
  bool setManufacturerData(const std::string&) { return true; }
};
class NimBLEAdvertising {
 public:
  bool setAdvertisementData(const NimBLEAdvertisementData&) { return true; }
  void setMinInterval(uint16_t) {}
  void setMaxInterval(uint16_t) {}
  bool start(uint32_t = 0) { return true; }
};
class NimBLEScan {
 public:
  void setScanCallbacks(NimBLEScanCallbacks*, bool = false) {}
  void setActiveScan(bool) {}
  void setInterval(uint16_t) {}
  void setWindow(uint16_t) {}
  void setMaxResults(uint8_t) {}
  bool start(uint32_t, bool = false, bool = true) { return true; }
};
class NimBLEDevice {
 public:
  static bool init(const std::string&) { return true; }
  static NimBLEAdvertising* getAdvertising() { static NimBLEAdvertising a; return &a; }
  static NimBLEScan* getScan() { static NimBLEScan s; return &s; }
};

// BLEDevice.h - on the PC: Bluetooth LE that never hears anyone (the emulator has no BLE radio of hers).
#pragma once
#include <Arduino.h>

class BLEAdvertisedDevice {
 public:
  bool haveManufacturerData() { return false; }
  String getManufacturerData() { return String(); }
  int getRSSI() { return -100; }
};
class BLEAdvertisedDeviceCallbacks {
 public:
  virtual ~BLEAdvertisedDeviceCallbacks() {}
  virtual void onResult(BLEAdvertisedDevice d) = 0;
};
class BLEAdvertisementData {
 public:
  void setFlags(uint8_t) {}
  void setManufacturerData(const String&) {}
};
class BLEAdvertising {
 public:
  void setAdvertisementData(BLEAdvertisementData&) {}
  void setMinInterval(uint16_t) {}
  void setMaxInterval(uint16_t) {}
  void start() {}
};
class BLEScan {
 public:
  void setAdvertisedDeviceCallbacks(BLEAdvertisedDeviceCallbacks*, bool = false) {}
  void setActiveScan(bool) {}
  void setInterval(uint16_t) {}
  void setWindow(uint16_t) {}
  bool start(uint32_t, void*, bool) { return true; }
  void clearResults() {}
};
class BLEDevice {
 public:
  static void init(const char*) {}
  static BLEAdvertising* getAdvertising() { static BLEAdvertising a; return &a; }
  static BLEScan* getScan() { static BLEScan s; return &s; }
};

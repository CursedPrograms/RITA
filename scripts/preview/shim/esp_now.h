// esp_now.h - on the PC. A PC has no ESP-NOW radio, so in the window her beacons go nowhere and
// nobody answers: DRIVE says a robot isn't heard, as it does on the board when none is in range.
// Only --shots has a pretend NORA, WHIP and MILA answering, so the screenshots show the distance.
#pragma once
#include <Arduino.h>
#include <cmath>
extern bool ritaPretend;

#define ESP_OK 0
typedef int wifi_interface_t;
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1

struct esp_now_peer_info_t {
  uint8_t peer_addr[6];
  uint8_t channel;
  int ifidx;
  bool encrypt;
};
struct wifi_pkt_rx_ctrl_t { int rssi; };
struct esp_now_recv_info_t { const uint8_t* src_addr; wifi_pkt_rx_ctrl_t* rx_ctrl; };
typedef void (*esp_now_recv_cb_t)(const esp_now_recv_info_t*, const uint8_t*, int);

// FreeRTOS's spinlock, for fleet_near.h's table: one thread here, so nothing to do
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))

inline esp_now_recv_cb_t& esp_now_shim_cb() { static esp_now_recv_cb_t cb = nullptr; return cb; }
inline int esp_now_init() { return ESP_OK; }
inline int esp_now_deinit() { return ESP_OK; }
inline int esp_now_register_recv_cb(esp_now_recv_cb_t cb) { esp_now_shim_cb() = cb; return ESP_OK; }
inline bool esp_now_is_peer_exist(const uint8_t*) { return false; }
inline int esp_now_add_peer(const esp_now_peer_info_t*) { return ESP_OK; }
inline int esp_now_send(const uint8_t*, const uint8_t* data, size_t len) {
  esp_now_recv_cb_t cb = esp_now_shim_cb();
  if (!ritaPretend || !cb || len < 20) return ESP_OK;
  struct Fake { const char* name; int rssi; } fakes[] = {
    { "NORA", -52 },
    { "WHIP", -60 - (int)(12 * (0.5 + 0.5 * std::sin(millis() / 8000.0))) },
    { "MILA", -74 },
  };
  for (const Fake& f : fakes) {
    uint8_t b[256] = {0};
    memcpy(b, data, len);                         // her beacon's layout, with their name and "park"
    memset(b + 5, 0, 12);
    strncpy((char*)b + 5, f.name, 11);
    static const char PARK[] = "++++++++[>++++++++++++++<-]>.---------------.<++++[>++++<-]>+.-------.";   // "park", as fleet_near.h says it
    memset(b + 19, 0, len - 19);
    strncpy((char*)b + 19, PARK, len - 20);
    wifi_pkt_rx_ctrl_t rx = { f.rssi };
    esp_now_recv_info_t info = { nullptr, &rx };
    cb(&info, b, (int)len);
  }
  return ESP_OK;
}

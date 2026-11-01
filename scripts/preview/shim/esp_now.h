// esp_now.h - her beacon has nobody to reach on the PC, so every send just works.
#pragma once
#include <Arduino.h>

#define ESP_OK 0
#define WIFI_IF_STA 0

struct esp_now_peer_info_t {
  uint8_t peer_addr[6];
  int ifidx;
};
inline int esp_now_init() { return ESP_OK; }
inline bool esp_now_is_peer_exist(const uint8_t*) { return true; }
inline int esp_now_add_peer(const esp_now_peer_info_t*) { return ESP_OK; }
inline int esp_now_send(const uint8_t*, const uint8_t*, size_t) { return ESP_OK; }

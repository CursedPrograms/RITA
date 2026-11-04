// esp_wifi.h for the PC preview: her hotspot's list of who's on it (empty here: the pretend WiFi
// always finds RIFT, so she never hosts).
#pragma once
#include <cstdint>
#include "esp_now.h"
#define ESP_WIFI_MAX_CONN_NUM 10
struct wifi_sta_info_t { uint8_t mac[6]; int8_t rssi; };
struct wifi_sta_list_t { wifi_sta_info_t sta[ESP_WIFI_MAX_CONN_NUM]; int num; };
inline int esp_wifi_ap_get_sta_list(wifi_sta_list_t* l) { l->num = 0; return -1; }

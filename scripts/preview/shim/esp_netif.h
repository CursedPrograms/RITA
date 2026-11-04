// esp_netif.h for the PC preview: the hotspot's DHCP table (unused here).
#pragma once
#include <cstdint>
#include <cstdio>
struct esp_netif_t {};
struct esp_ip4_addr_t { uint32_t addr; };
struct esp_netif_pair_mac_ip_t { uint8_t mac[6]; esp_ip4_addr_t ip; };
inline esp_netif_t* esp_netif_get_handle_from_ifkey(const char*) { return nullptr; }
inline int esp_netif_dhcps_get_clients_by_mac(esp_netif_t*, int, esp_netif_pair_mac_ip_t*) { return -1; }
inline char* esp_ip4addr_ntoa(const esp_ip4_addr_t* a, char* buf, int len) {
  snprintf(buf, len, "%u.%u.%u.%u", a->addr & 255, (a->addr >> 8) & 255, (a->addr >> 16) & 255, a->addr >> 24);
  return buf;
}

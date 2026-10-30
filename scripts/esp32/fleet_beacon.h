// fleet_beacon.h - RITA's "I'm here", the fleet's ESP-NOW beacon, byte for byte (fleet_near.h on NORA
// and WHIP). She's in your hand, so this is how STAY CLOSE knows how near you are.
#pragma once

#include <WiFi.h>
#include <esp_now.h>

#define FLEET_BEACON_MS 500

struct __attribute__((packed)) FleetBeacon {
  char magic[4];      // "DRFL"
  uint8_t version;    // 1
  char name[12];
  uint16_t seq;
  char bf[72];        // Brainfuck that prints what she's doing: "hand"
};

static const char FLEET_HAND_BF[] = "++++++++[>+++++++++++++<-]>.-------.+++++++++++++.----------.";
static const uint8_t FLEET_EVERYONE[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Every loop. It only sends once she's on a fleet network: that's what puts her on the robots' channel.
static void fleetBeaconLoop() {
  static bool ready = false;
  static unsigned long nextMs = 0;
  static uint16_t seq = 0;
  if (WiFi.status() != WL_CONNECTED || (long)(millis() - nextMs) < 0) return;
  nextMs = millis() + FLEET_BEACON_MS + random(0, 60);
  if (!ready) {
    if (esp_now_init() != ESP_OK) return;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, FLEET_EVERYONE, 6);
    peer.ifidx = WIFI_IF_STA;
    if (!esp_now_is_peer_exist(FLEET_EVERYONE) && esp_now_add_peer(&peer) != ESP_OK) return;
    ready = true;
  }
  FleetBeacon b = {};
  memcpy(b.magic, "DRFL", 4);
  b.version = 1;
  strncpy(b.name, "RITA", sizeof(b.name) - 1);
  strncpy(b.bf, FLEET_HAND_BF, sizeof(b.bf) - 1);
  b.seq = seq++;
  esp_now_send(FLEET_EVERYONE, (const uint8_t *)&b, sizeof(b));
}

// dream_mirror.h - DREAM on RITA's screen: the clip DREAM is showing, from her SD card, in step with
// DREAM's own screen, and DREAM's voice on RITA's speaker connector. All optional: without an SD
// card RITA shows DREAM's avatar and what she's doing instead, and nothing else changes.
//
// The card (FAT32), filled from RITA's sdcard/ folder (make_sdcard.bat converts DREAM's videos):
//   /dream/<clip>.mjpeg   the clip as back-to-back JPEGs, 480 x 270 (what an ESP32 can decode)
//   /dream/<clip>.idx     "RIDX", fps (u16), width (u16), height (u16), frames (u32), then
//                         frames + 1 offsets (u32) into the .mjpeg: frame k is [off k, off k+1)
// <clip> is the name DREAM reports ("idle2" for videos/idle2.mp4). A frame is chosen by time:
// (now - when DREAM started the clip) x fps, wrapped for a looping clip, so RITA shows the frame
// DREAM is showing, however late she joined.
//
// Voice: DREAM's mirror server (port 5015) serves the line she's speaking as unsigned 8-bit mono
// PCM at 16 kHz; a task on the other core streams it into the DAC (GPIO26, the board's speaker
// amplifier), starting as far in as DREAM already is.
//
// Board: ESP32-3248S035R. The SD slot is on its own SPI bus (VSPI: SCK 18, MISO 19, MOSI 23, CS 5),
// separate from the screen and touch (HSPI), and the speaker connector is on GPIO26 (DAC 2).
#pragma once
#include <SD.h>
#include <SPI.h>
#include <HTTPClient.h>
#include "driver/dac_continuous.h"
#include "talk_bf.h"   // the fleet's Brainfuck phrasebook (RIFT/Fleet/brainfuck_talk.py)

#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23
#define SD_CS    5

namespace mirror {

SPIClass sdSpi(VSPI);
bool card = false;                 // an SD card with a /dream folder
uint8_t* buf = nullptr;            // one JPEG frame: only while DREAM's screen is open, as big as her
size_t bufSize = 0;                // biggest frame so far (10-40 KB). Held from boot, it left WiFi too
                                   // little RAM next to Bluetooth, and she rebooted over and over.

void freeBuf() { free(buf); buf = nullptr; bufSize = 0; }

// ---- video ----
File vid, idx;
String clip;                       // the clip open now ("" = none, or not on the card)
uint16_t fps = 15, vw = 480, vh = 270;
uint32_t frames = 0;
unsigned long clipStart = 0;       // millis() when DREAM started it
bool clipLoop = true;
int32_t shown = -1;                // the frame on the screen

bool begin() {
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  card = SD.begin(SD_CS, sdSpi, 25000000) && SD.exists("/dream");
  return card;
}

uint32_t rd32(File& f) { uint8_t b[4]; f.read(b, 4); return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
uint16_t rd16(File& f) { uint8_t b[2]; f.read(b, 2); return b[0] | b[1] << 8; }

// DREAM is showing `name`, `ms` into it. True if RITA has it on her card.
bool show(const String& name, uint32_t ms, bool loop) {
  clipStart = millis() - ms;
  clipLoop = loop;
  if (name == clip) return frames > 0;
  if (vid) vid.close();
  if (idx) idx.close();
  clip = name; frames = 0; shown = -1;
  if (!name.length()) freeBuf();   // off DREAM's screen: the RAM goes back
  if (!card || !name.length()) return false;
  idx = SD.open("/dream/" + name + ".idx");
  vid = SD.open("/dream/" + name + ".mjpeg");
  if (!idx || !vid) { if (idx) idx.close(); if (vid) vid.close(); return false; }
  char magic[4];
  idx.read((uint8_t*)magic, 4);
  if (memcmp(magic, "RIDX", 4)) { idx.close(); vid.close(); return false; }
  fps = rd16(idx); vw = rd16(idx); vh = rd16(idx); frames = rd32(idx);
  if (!fps) fps = 15;
  return frames > 0;
}
bool has() { return frames > 0; }
void redraw() { shown = -1; }     // the screen was wiped: draw the frame again

// Draw the frame that's due, at (x, y). Call it often.
template <class GFX> void frame(GFX& lcd, int x, int y) {
  if (!frames) return;
  uint32_t k = (uint32_t)((millis() - clipStart) * (uint64_t)fps / 1000);
  k = clipLoop ? k % frames : min(k, frames - 1);
  if ((int32_t)k == shown) return;
  idx.seek(14 + 4 * k);
  const uint32_t a = rd32(idx), b = rd32(idx);
  if (b <= a || b - a > 64 * 1024) { shown = k; return; }
  if (b - a > bufSize) {             // a bigger frame than any so far: grow, in 4 KB steps
    const size_t want = ((b - a) + 4095) & ~(size_t)4095;
    uint8_t* more = (uint8_t*)realloc(buf, want);
    if (!more) { shown = k; return; }   // no room right now: skip this frame, not her
    buf = more; bufSize = want;
  }
  vid.seek(a);
  if (vid.read(buf, b - a) != (int)(b - a)) return;
  lcd.drawJpg(buf, b - a, x, y);
  shown = k;
}

// ---- voice ----
dac_continuous_handle_t dac = nullptr;
TaskHandle_t voiceTask = nullptr;
SemaphoreHandle_t voiceLock = nullptr;
String voiceUrl;                   // the next line to play (set by say(), taken by the task)
volatile bool voiceStop = false;
String voiceId;                    // the line playing / last played
bool sound = true;                 // the SOUND button on DREAM's screen
volatile bool playing = false;     // her speaker is busy (DREAM's voice, or a phrase)
volatile int chirpU = -1;          // a Brainfuck phrase to beep next (-1: none)
const uint8_t TALK_VOICE_PCT = 118;   // her voice: a little higher than the fleet's

// One phrase: each symbol of its program a square-wave tone (bfToneHz/bfToneMs), 16 kHz into the DAC
void beepPhrase(int u) {
  static uint8_t buf[512];
  for (uint16_t n = 0;; n++) {
    const char c = talkSymbol(u, n);
    if (!c) break;
    const uint32_t hz = (uint32_t)bfToneHz(c) * TALK_VOICE_PCT / 100;
    uint32_t samples = 16000UL * bfToneMs(c) / 1000, gap = 16000UL * TALK_GAP_MS / 1000, i = 0;
    const uint32_t half = hz ? 8000 / hz : 0;
    while (samples + gap > 0) {
      size_t k = 0;
      for (; k < sizeof(buf) && samples + gap > 0; k++) {
        if (samples) { buf[k] = half && (i++ / half) % 2 ? 170 : 86; samples--; }
        else { buf[k] = 128; gap--; }
      }
      size_t loaded = 0;
      dac_continuous_write(dac, buf, k, &loaded, 200);
    }
  }
}

void voiceLoop(void*) {
  static uint8_t chunk[1024];
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (chirpU >= 0) {                     // a Brainfuck phrase, when she isn't speaking for DREAM
      playing = true;
      dac_continuous_enable(dac);
      beepPhrase(chirpU);
      dac_continuous_disable(dac);
      chirpU = -1;
      playing = false;
      continue;
    }
    xSemaphoreTake(voiceLock, portMAX_DELAY);
    const String url = voiceUrl;
    xSemaphoreGive(voiceLock);
    voiceStop = false;
    HTTPClient http;
    http.begin(url);
    http.setTimeout(3000);
    if (http.GET() == 200) {
      WiFiClient* s = http.getStreamPtr();
      int left = http.getSize();          // -1: until the connection closes
      dac_continuous_enable(dac);
      playing = true;
      while (!voiceStop && (left > 0 || left == -1) && (http.connected() || s->available())) {
        const int n = s->readBytes(chunk, left > 0 ? min(left, (int)sizeof(chunk)) : sizeof(chunk));
        if (n <= 0) break;
        size_t loaded = 0;
        dac_continuous_write(dac, chunk, n, &loaded, 500);
        if (left > 0) left -= n;
      }
      memset(chunk, 128, sizeof(chunk));  // ease back to the middle: no click
      size_t loaded = 0;
      dac_continuous_write(dac, chunk, sizeof(chunk), &loaded, 200);
      dac_continuous_disable(dac);
      playing = false;
    }
    http.end();
  }
}

bool voiceBegin() {
  dac_continuous_config_t cfg = {};
  cfg.chan_mask = DAC_CHANNEL_MASK_CH1;   // GPIO26: the speaker connector
  cfg.desc_num = 4;
  cfg.buf_size = 1024;
  cfg.freq_hz = 16000;
  cfg.offset = 0;
  cfg.clk_src = DAC_DIGI_CLK_SRC_APLL;    // the default clock can't go as low as 16 kHz
  cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
  if (dac_continuous_new_channels(&cfg, &dac) != ESP_OK) { dac = nullptr; return false; }
  voiceLock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(voiceLoop, "dream-voice", 6144, nullptr, 2, &voiceTask, 0);
  return true;
}

// DREAM started line `id` (`ms` ago): play it from there. base = "http://ip:5015"
void say(const String& base, const String& id, uint32_t ms) {
  if (!dac || !sound || !id.length() || id == voiceId) return;
  voiceId = id;
  voiceStop = true;                       // a line still playing gives way
  xSemaphoreTake(voiceLock, portMAX_DELAY);
  voiceUrl = base + "/speech/" + id + "?from_ms=" + String(ms + 150);   // + the time it takes to arrive
  xSemaphoreGive(voiceLock);
  xTaskNotifyGive(voiceTask);
}
void hush() { voiceStop = true; voiceId = ""; }

// A Brainfuck phrase on her speaker. False if it's busy (DREAM's voice) or there's no DAC.
bool chirp(int u) {
  if (!dac || playing || chirpU >= 0 || u < 0 || u >= TALK_UTTERANCES) return false;
  chirpU = u;
  xTaskNotifyGive(voiceTask);
  return true;
}

}  // namespace mirror

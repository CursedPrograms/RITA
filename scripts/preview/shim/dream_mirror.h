// dream_mirror.h for the PC preview: RITA's repo sdcard/ folder is her SD card (preview.py passes its
// path as RITA_SDCARD), so DREAM's clips play here the way they do on the board. Her speaker (the
// board's DAC) is the PC's: DREAM's voice is fetched from DREAM and played, and RITA's Brainfuck
// phrases beep, as on the board. --shots stays silent and only prints them.
#pragma once
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <SDL2/SDL.h>
#include "pc_net.h"
#ifndef pgm_read_byte
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#endif
inline void tone(int, unsigned int, unsigned long = 0) {}
inline void noTone(int) {}
#include "talk_bf.h"   // the fleet's phrasebook, as on the board

#ifndef RITA_SDCARD
#define RITA_SDCARD "sdcard"
#endif
std::string ritaSdcard();   // main.cpp: next to the exe, else RITA_SDCARD

namespace mirror {

bool card = false, sound = true;
String voiceId;
std::string clip;
uint16_t fps = 15;
uint32_t frames = 0;
std::vector<uint32_t> offs;
std::vector<uint8_t> data;
unsigned long clipStart = 0;
bool clipLoop = true;
int32_t shown = -1;

static bool readFile(const std::string& path, std::vector<uint8_t>* out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  fseek(f, 0, SEEK_END);
  out->resize(ftell(f));
  fseek(f, 0, SEEK_SET);
  const bool ok = fread(out->data(), 1, out->size(), f) == out->size();
  fclose(f);
  return ok;
}

bool begin() {
  FILE* f = fopen((ritaSdcard() + "/dream/idle0.idx").c_str(), "rb");
  card = f != nullptr;
  if (f) fclose(f);
  return card;
}

bool show(const String& name, uint32_t ms, bool loop) {
  clipStart = millis() - ms;
  clipLoop = loop;
  if (std::string(name.c_str()) == clip) return frames > 0;
  clip = name.c_str(); frames = 0; shown = -1; offs.clear(); data.clear();
  if (!card || clip.empty()) return false;
  std::vector<uint8_t> ix;
  const std::string base = ritaSdcard() + "/dream/" + clip;
  if (!readFile(base + ".idx", &ix) || ix.size() < 14 || memcmp(ix.data(), "RIDX", 4) || !readFile(base + ".mjpeg", &data)) return false;
  fps = ix[4] | ix[5] << 8;
  const uint32_t n = ix[10] | ix[11] << 8 | ix[12] << 16 | (uint32_t)ix[13] << 24;
  for (uint32_t k = 0; k <= n && 14 + 4 * k + 3 < ix.size(); k++) {
    const uint8_t* p = &ix[14 + 4 * k];
    offs.push_back(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
  }
  frames = offs.size() > 1 ? offs.size() - 1 : 0;
  if (!fps) fps = 15;
  return frames > 0;
}
bool has() { return frames > 0; }
void redraw() { shown = -1; }

template <class GFX> void frame(GFX& lcd, int x, int y) {
  if (!frames) return;
  uint32_t k = (uint32_t)((millis() - clipStart) * (uint64_t)fps / 1000);
  k = clipLoop ? k % frames : (k < frames ? k : frames - 1);
  if ((int32_t)k == shown) return;
  lcd.drawJpg(data.data() + offs[k], offs[k + 1] - offs[k], x, y);
  shown = k;
}

// ---- voice: the board's DAC is the PC's speakers here (16 kHz, unsigned 8-bit, like the DAC) ----
SDL_AudioDeviceID dac = 0;
std::atomic<unsigned> voiceGen{0};           // a new line (or hush) bumps it: the one playing stops
std::atomic<bool> fetching{false};
const uint8_t TALK_VOICE_PCT = 118;          // her voice: a little higher than the fleet's

bool voiceBegin() {
  if (ritaPretend) return false;
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
  SDL_AudioSpec want = {}, got = {};
  want.freq = 16000;
  want.format = AUDIO_U8;
  want.channels = 1;
  want.samples = 512;
  dac = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
  if (dac) SDL_PauseAudioDevice(dac, 0);
  return dac != 0;
}
static bool busy() { return dac && (fetching || SDL_GetQueuedAudioSize(dac) > 0); }

void say(const String& base, const String& id, uint32_t ms) {
  if (!sound || !id.length() || id == voiceId) return;
  voiceId = id;
  if (!dac) { printf("(DREAM's voice: %s/speech/%s from %u ms - no speakers)\n", base.c_str(), id.c_str(), (unsigned)ms); return; }
  const unsigned gen = ++voiceGen;
  SDL_ClearQueuedAudio(dac);                 // a line still playing gives way
  const std::string url = std::string(base.c_str()) + "/speech/" + id.c_str() + "?from_ms=" + std::to_string(ms + 150);
  fetching = true;
  std::thread([url, gen]() {
    pcnet::Response r;
    if (pcnet::request("GET", url, {}, "", 3000, &r) == 200) {
      uint8_t chunk[1024];
      int n;
      while (voiceGen == gen && (n = r.read(chunk, sizeof(chunk))) > 0) {
        while (voiceGen == gen && SDL_GetQueuedAudioSize(dac) > 16000) SDL_Delay(20);   // half a second ahead, like the DAC's buffer
        if (voiceGen == gen) SDL_QueueAudio(dac, chunk, n);
      }
    }
    r.close();
    if (voiceGen == gen) fetching = false;
  }).detach();
}
void hush() {
  voiceId = "";
  ++voiceGen;
  fetching = false;
  if (dac) SDL_ClearQueuedAudio(dac);
}

// A Brainfuck phrase on her speaker: each symbol a square wave (bfToneHz/bfToneMs), as on the board.
// False if it's busy (DREAM's voice) or there are no speakers.
bool chirp(int u) {
  if (ritaPretend) { printf("(RITA beeps Brainfuck phrase %d)\n", u); return true; }
  if (!dac || busy() || u < 0 || u >= TALK_UTTERANCES) return false;
  std::vector<uint8_t> pcm;
  for (uint16_t n = 0;; n++) {
    const char c = talkSymbol(u, n);
    if (!c) break;
    const uint32_t hz = (uint32_t)bfToneHz(c) * TALK_VOICE_PCT / 100;
    const uint32_t samples = 16000UL * bfToneMs(c) / 1000, gap = 16000UL * TALK_GAP_MS / 1000, half = hz ? 8000 / hz : 0;
    for (uint32_t i = 0; i < samples; i++) pcm.push_back(half && (i / half) % 2 ? 170 : 86);
    pcm.insert(pcm.end(), gap, 128);
  }
  SDL_QueueAudio(dac, pcm.data(), (Uint32)pcm.size());
  return true;
}

}  // namespace mirror

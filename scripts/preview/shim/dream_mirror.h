// dream_mirror.h for the PC preview: RITA's repo sdcard/ folder is her SD card (preview.py passes its
// path as RITA_SDCARD), so DREAM's clips play here the way they do on the board. Her voice is the
// board's (the DAC): here it's only printed.
#pragma once
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef RITA_SDCARD
#define RITA_SDCARD "sdcard"
#endif

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
  FILE* f = fopen((std::string(RITA_SDCARD) + "/dream/idle0.idx").c_str(), "rb");
  card = f != nullptr;
  if (f) fclose(f);
  return card;
}
bool voiceBegin() { return true; }

bool show(const String& name, uint32_t ms, bool loop) {
  clipStart = millis() - ms;
  clipLoop = loop;
  if (std::string(name.c_str()) == clip) return frames > 0;
  clip = name.c_str(); frames = 0; shown = -1; offs.clear(); data.clear();
  if (!card || clip.empty()) return false;
  std::vector<uint8_t> ix;
  const std::string base = std::string(RITA_SDCARD) + "/dream/" + clip;
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

void say(const String& base, const String& id, uint32_t ms) {
  if (!sound || !id.length() || id == voiceId) return;
  voiceId = id;
  printf("(DREAM's voice on the speaker: %s/speech/%s from %u ms)\n", base.c_str(), id.c_str(), (unsigned)ms);
}
void hush() { voiceId = ""; }

}  // namespace mirror

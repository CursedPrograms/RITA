// Preferences.h - her flash store, on the PC: a file (%APPDATA%\DREAM Robotics\RITA\prefs.bin, else
// rita_prefs.bin next to the exe), so SOUND, TALK and the rest are remembered between runs like on
// the board. The touch panel is the mouse: it never needs calibrating, so "touch" always reads back.
// --shots never reads or writes it: the screenshots always start from the defaults.
#pragma once
#include <Arduino.h>
#include <map>
#include <string>
#include <vector>

class Preferences {
  std::string ns;

  static std::string file() {
    const char* app = getenv("APPDATA");
    if (!app) return "rita_prefs.bin";
    const std::string dir = std::string(app) + "\\DREAM Robotics";
    std::string cmd = "mkdir \"" + dir + "\\RITA\" 2>nul";
    static bool made = false;
    if (!made) { system(cmd.c_str()); made = true; }
    return dir + "\\RITA\\prefs.bin";
  }
  static std::map<std::string, std::vector<uint8_t>>& store() {
    static std::map<std::string, std::vector<uint8_t>> s;
    static bool loaded = false;
    if (!loaded && !ritaPretend) {
      loaded = true;
      if (FILE* f = fopen(file().c_str(), "rb")) {   // key\0 len(4) bytes, again and again
        for (;;) {
          std::string k;
          int ch;
          while ((ch = fgetc(f)) > 0) k += (char)ch;
          uint32_t n;
          if (ch < 0 || fread(&n, 4, 1, f) != 1) break;
          std::vector<uint8_t> v(n);
          if (n && fread(v.data(), 1, n, f) != n) break;
          s[k] = v;
        }
        fclose(f);
      }
    }
    return s;
  }
  static void save() {
    if (ritaPretend) return;
    FILE* f = fopen(file().c_str(), "wb");
    if (!f) return;
    for (auto& kv : store()) {
      fwrite(kv.first.c_str(), 1, kv.first.size() + 1, f);
      const uint32_t n = (uint32_t)kv.second.size();
      fwrite(&n, 4, 1, f);
      fwrite(kv.second.data(), 1, n, f);
    }
    fclose(f);
  }
  std::string key(const char* k) const { return ns + "/" + k; }

 public:
  void begin(const char* name, bool) { ns = name; }
  size_t getBytes(const char* k, void* out, size_t len) {
    if (!strcmp(k, "touch")) return len;   // the mouse is always calibrated
    auto it = store().find(key(k));
    if (it == store().end() || it->second.size() != len) return 0;
    memcpy(out, it->second.data(), len);
    return len;
  }
  size_t putBytes(const char* k, const void* v, size_t len) {
    if (!strcmp(k, "touch")) return len;
    store()[key(k)] = std::vector<uint8_t>((const uint8_t*)v, (const uint8_t*)v + len);
    save();
    return len;
  }
  bool getBool(const char* k, bool def) {
    auto it = store().find(key(k));
    return it == store().end() || it->second.empty() ? def : it->second[0] != 0;
  }
  size_t putBool(const char* k, bool v) {
    store()[key(k)] = { (uint8_t)(v ? 1 : 0) };
    save();
    return 1;
  }
  void end() {}
};

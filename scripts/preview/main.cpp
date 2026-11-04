// main.cpp - RITA on the PC: her real sketch, unchanged, against a pretend fleet.
//
//   rita_preview            a window: the mouse is your finger, Esc quits, the console shows her web calls
//   rita_preview --shots D  her screens as D/<name>.ppm (preview.py turns them into images/screens/*.png)
//
// preview.py copies esp32.ino in as sketch.inc, so the display.h it finds is shim/display.h.
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <string>

void draw();
#include <cstdio>
#include "sketch.inc"

// Her SD card: an sdcard folder next to the exe or above it (the repo's emulator/ has ../sdcard),
// else the repo's sdcard/ it was built from
std::string ritaSdcard() {
  static std::string found;
  if (!found.empty()) return found;
  char* b = SDL_GetBasePath();
  const std::string here = b ? b : "";
  if (b) SDL_free(b);
  for (const char* up : {"sdcard", "../sdcard", "../../sdcard"}) {
    const std::string p = here + up;
    if (FILE* f = fopen((p + "/dream/idle0.idx").c_str(), "rb")) { fclose(f); return found = p; }
  }
  return found = RITA_SDCARD;
}

// ---------------------------------------------------------------------------
// The clock and the touch panel
// ---------------------------------------------------------------------------
bool touchDown = false;
int touchX = 0, touchY = 0;

static bool shots = false;            // shots run on a pretend clock, so they don't wait
static unsigned long fakeNow = 0;
static std::string shotDir;
static void (*onLongDelay)(unsigned long) = nullptr;

unsigned long millis() { return shots ? fakeNow : SDL_GetTicks(); }
void delay(unsigned long ms) {
  if (ms >= 1000 && onLongDelay) onLongDelay(ms);
  else if (shots) fakeNow += ms;
  else SDL_Delay(ms);
}

// ---------------------------------------------------------------------------
// The pretend fleet: WHIP, NORA, MILA, KIDA-01 and ARIA are on the network, NIKA and KIDA-00 aren't
// ---------------------------------------------------------------------------
static const char FLEET_JSON[] =
    "{\"robots\":["
    "{\"name\":\"WHIP\",\"type\":\"hexapod\",\"ip\":\"192.168.4.20\"},"
    "{\"name\":\"NORA\",\"type\":\"mecanum\",\"ip\":\"192.168.4.21\"},"
    "{\"name\":\"MILA\",\"type\":\"tank\",\"ip\":\"192.168.4.22\"},"
    "{\"name\":\"KIDA01\",\"type\":\"tank\",\"ip\":\"192.168.4.23\"},"
    "{\"name\":\"ARIA\",\"type\":\"drone\",\"ip\":\"192.168.4.24\"},"
    "{\"name\":\"DREAM\",\"type\":\"companion\",\"ip\":\"192.168.4.25\"},"
    "{\"name\":\"KIDA00\",\"type\":\"tank\",\"ip\":\"192.168.4.26\"}]}";

// DREAM's mirror: talking, a line every 8 s, then idle and asleep in turn (the clips from sdcard/)
static String dreamStatus() {
  const unsigned long t = millis() % 24000;
  const char* clip = t < 8000 ? "talking0" : t < 16000 ? "idle0" : "sleeping";
  const char* state = t < 8000 ? "talking" : t < 16000 ? "idle" : "sleeping";
  return String("{\"state\":\"") + state + "\",\"sleeping\":" + (t >= 16000 ? "true" : "false") +
         ",\"clip\":\"" + clip + "\",\"clip_ms\":" + String((unsigned)(t % 8000)) + ",\"loop\":true,\"speech\":" +
         (t < 8000 ? String("\"line") + String((unsigned)(millis() / 24000)) + "\"" : String("null")) + ",\"speech_ms\":" + String((unsigned)(t % 8000)) + "}";
}

static const char ASK_JSON[] =
    "{\"ok\":true,\"core\":{\"mode\":\"MANUAL\",\"moving\":false,\"music\":\"stopped\","
    "\"distances_cm\":{\"front\":62,\"back\":140}},"
    "\"does\":[\"stop\",\"forward\",\"back\",\"turn_left\",\"turn_right\",\"left\",\"right\","
    "\"music_play\",\"music_next\",\"music_stop\",\"uv_on\",\"uv_off\",\"lock_wheels\"]}";

static const char ARIA_JSON[] = "{\"name\":\"ARIA\",\"mode\":\"ground\",\"battery_v\":3.92,\"network\":\"RIFT\"}";

static String heard;   // every call since the last check, for STOP's check below

int fakeHttp(const String& url, const String& body, String* answer) {
  *answer = "{\"ok\":true}";
  if (url.indexOf("/status") < 0 && url.indexOf("/fleet") < 0 && url.indexOf("/register") < 0) heard = heard + url + body + "\n";
  if (url.indexOf("/fleet") >= 0) *answer = FLEET_JSON;
  else if (url.indexOf("/talk") >= 0 && body.indexOf("\"ask\"") >= 0) *answer = ASK_JSON;
  else if (url.indexOf(".24:") >= 0 && url.indexOf("/status") >= 0) *answer = ARIA_JSON;
  else if (url.indexOf(":5015/status") >= 0) *answer = dreamStatus();
  else if (url.indexOf(".26:5003/status") >= 0)
    *answer = "{\"mode\":\"USER\",\"camera_servo\":{\"angle\":120.0,\"mode\":\"manual\",\"face\":null},\"distance_cm\":74,\"wifi_rssi\":-63}";
  else if (url.indexOf(".21:5002/status") >= 0) *answer = "{\"mode\":\"MANUAL\",\"temp\":23.4,\"hum\":51,\"distances_cm\":{\"front\":62,\"back\":140},\"battery_pct\":78,\"wifi_rssi\":-58}";
  else if (url.indexOf(".23:5004/status") >= 0) *answer = "{\"sensors\":{\"temperature\":24.1,\"humidity\":48,\"distance_cm\":35},\"power\":{\"battery_v\":7.62},\"mode\":1,\"wifi_rssi\":-71}";
  else if (url.indexOf(".20:5005/status") >= 0) *answer = "{\"mode\":\"web\",\"battery_v\":7.9,\"range_cm\":88,\"tilt\":2,\"wifi_rssi\":-82}";
  else if (url.indexOf(".22:5010/status") >= 0) *answer = "{\"mode\":\"wasd\",\"dist\":41,\"bat_pct\":64}";
  // /status is polled all the time; everything else is her doing something, so it's worth reading
  if (!shots && url.indexOf("/status") < 0 && url.indexOf("/fleet") < 0 && url.indexOf("/register") < 0)
    printf("%s %s %s\n", body.length() ? "POST" : "GET ", url.c_str(), body.c_str());
  fflush(stdout);
  return 200;
}

// ---------------------------------------------------------------------------
// Her screen as plain RGB
// ---------------------------------------------------------------------------
static inline void pixel(int i, uint8_t* r, uint8_t* g, uint8_t* b) {
  const uint8_t* p = (const uint8_t*)lcd.getBuffer() + i * 2;   // RGB565, high byte first
  const uint16_t c = (p[0] << 8) | p[1];
  *r = (c >> 8 & 0xF8) | (c >> 13);
  *g = (c >> 3 & 0xFC) | (c >> 9 & 3);
  *b = (c << 3 & 0xF8) | (c >> 2 & 7);
}

// ---------------------------------------------------------------------------
// --shots
// ---------------------------------------------------------------------------
static void shot(const char* name) {
  const std::string path = shotDir + "/" + name + ".ppm";
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { printf("can't write %s\n", path.c_str()); exit(1); }
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  for (int i = 0; i < W * H; i++) {
    uint8_t c[3];
    pixel(i, &c[0], &c[1], &c[2]);
    fwrite(c, 1, 3, f);
  }
  fclose(f);
  printf("%s\n", name);
}

static void run(unsigned long ms) {   // her main loop, for this long on the pretend clock
  const unsigned long until = fakeNow + ms;
  while (fakeNow < until) loop();
}
static void settled(const char* name) {   // let the toast go and the polls come back first
  run(3000);
  shot(name);
}
// A finger on a button, through her real touch code. Off the screen's button list, it says so.
static void press(const char* id) {
  for (int i = btnCount - 1; i >= 0; i--) {
    if (btns[i].id != id) continue;
    touchX = btns[i].x + btns[i].w / 2;
    touchY = btns[i].y + btns[i].h / 2;
    touchDown = true;
    run(100);
    return;
  }
  printf("no button %s on this screen\n", id);
  exit(1);
}
static void lift() {
  touchDown = false;
  run(300);
}
static void onRobot(int r) {
  run(100);   // HOME has to be on the screen before its buttons exist
  press((String("go:") + r).c_str());
  lift();
  run(3000);
}

// The big STOP has to leave a mode the robot drives itself in: a plain stop there does nothing
static bool stops(int r, const char* idFromMode, const char* expected) {
  onRobot(r);
  run(100);
  heard = "";
  press(idFromMode);
  lift();
  const bool ok = heard.indexOf(expected) >= 0;
  if (!ok) printf("%s's STOP sent:\n%s(wanted %s)\n", ROSTER[r].label, heard.c_str(), expected);
  tap("back");
  return ok;
}

static int takeShots() {
  shots = true;
  onLongDelay = [](unsigned long ms) { shot("01_splash"); fakeNow += ms; };
  setup();
  onLongDelay = nullptr;
  run(1000);
  while (netState != NET_UP || !botCount) loop();
  settled("02_home");

  onRobot(0);
  press("p:f");
  if (heldPad != "p:f") { printf("holding W didn't drive\n"); return 1; }
  shot("03_whip");
  lift();
  if (heldPad.length()) { printf("letting go didn't stop\n"); return 1; }
  tap("back");

  onRobot(1);
  settled("04_nora");
  // every button a robot has must fit on its DRIVE screen (NORA's LOCK once didn't)
  for (int r = 0; r < ROSTER_N; r++) {
    int n = 0;
    extrasOf(r, &n);
    if (n > 8) { printf("%s has %d buttons, DRIVE shows 8\n", ROSTER[r].label, n); return 1; }
  }
  tap("rift");
  settled("08_rift_ask_do");
  tap("do:forward");
  run(100);
  shot("09_let_rita_move");
  tap("no");
  tap("back");
  tap("back");

  onRobot(2);
  settled("05_mila_wasd");
  tap("scheme:tank");
  tap("t:L+");
  tap("t:R-");
  settled("06_mila_tank");
  tap("stop");
  tap("back");

  onRobot(5);
  settled("07_kida01");
  tap("back");
  onRobot(3);
  settled("10_nika_away");
  tap("back");
  onRobot(6);
  settled("11_ida");
  tap("back");
  onRobot(7);
  settled("12_arm");
  tap("back");
  onRobot(8);
  settled("13_aria");
  tap("back");
  onRobot(9);
  settled("14_dream");
  tap("cam");
  settled("16_dream_cam");
  tap("cam");
  tap("back");
  onRobot(4);
  settled("20_kida00_servo");
  tap("cam");
  settled("15_kida00_cam");
  tap("cam");
  tap("back");

  tap("qr");
  settled("19_web_qr");
  tap("back");
  tap("stats");
  settled("17_stats");
  run(4000);
  shot("17_stats");
  tap("st:1");
  settled("18_stats_nora");
  tap("back");
  tap("back");

  if (!stops(0, "stop", "/mode?m=web") || !stops(1, "stop", "/modeManual") || !stops(2, "stop", "/mode?v=wasd") ||
      !stops(5, "stop", "stop_anywhere")) return 1;
  onRobot(2);
  run(100);
  tap("x:chirp");
  if (heard.indexOf("/chirp?u=") < 0) { printf("CHIRP needs a phrase\n"); return 1; }
  return 0;
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------
static const int SCALE = 2;
static SDL_Renderer* renderer;
static SDL_Texture* texture;

static void present() {
  static uint8_t rgb[480 * 320 * 3];
  for (int i = 0; i < W * H; i++) pixel(i, &rgb[i * 3], &rgb[i * 3 + 1], &rgb[i * 3 + 2]);
  SDL_UpdateTexture(texture, nullptr, rgb, W * 3);
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);
  SDL_RenderPresent(renderer);
}

// The keyboard, as a finger on her buttons:
//   1-9, 0      HOME: open WHIP .. ARIA, DREAM          W A S D / arrows   hold to drive (the pad)
//   Q E         crab (WHIP) / turn on the spot (NORA)    Space   STOP
//   Backspace   back                                     T       WASD <-> TANK
//   R           RIFT: ask and do                         M       DREAM's sound on / off
//   C           CAM: the camera (KIDA-00, KIDA-01, DREAM) V       KIDA-01: cam 0 / cam 1
//   Esc         quit
static bool onScreen(const char* id) {
  for (int i = 0; i < btnCount; i++) if (btns[i].id == id) return true;
  return false;
}
static void key(SDL_Keycode k, bool down) {
  const char* pad = nullptr;
  switch (k) {
    case SDLK_w: case SDLK_UP: pad = "p:f"; break;
    case SDLK_s: case SDLK_DOWN: pad = "p:b"; break;
    case SDLK_a: case SDLK_LEFT: pad = "p:l"; break;
    case SDLK_d: case SDLK_RIGHT: pad = "p:r"; break;
    case SDLK_q: pad = onScreen("p:sl") ? "p:sl" : "p:tl"; break;
    case SDLK_e: pad = onScreen("p:sr") ? "p:sr" : "p:tr"; break;
  }
  if (pad) {   // held like a finger on the pad: drives while down, stops when let go
    if (down && screen == SCR_CTRL && onScreen(pad) && heldPad != pad) padPress(pad);
    else if (!down && heldPad == pad) padRelease();
    return;
  }
  if (!down) return;
  if (k >= SDLK_1 && k <= SDLK_9 && screen == SCR_HOME) { if (k - SDLK_1 < ROSTER_N) tap(String("go:") + (int)(k - SDLK_1)); }
  else if (k == SDLK_0 && screen == SCR_HOME && ROSTER_N >= 10) tap("go:9");
  else if (k == SDLK_BACKSPACE && onScreen("back")) tap("back");
  else if (k == SDLK_SPACE && onScreen("stop")) tap("stop");
  else if (k == SDLK_t && onScreen("scheme:tank")) tap(tankUi ? "scheme:wasd" : "scheme:tank");
  else if (k == SDLK_r && onScreen("rift")) tap("rift");
  else if (k == SDLK_m && onScreen("sound")) tap("sound");
  else if (k == SDLK_c && onScreen("cam")) tap("cam");
  else if (k == SDLK_v && onScreen("camsw")) tap("camsw");
}

static bool pump() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) return false;
    if ((e.type == SDL_KEYDOWN && !e.key.repeat) || e.type == SDL_KEYUP) key(e.key.keysym.sym, e.type == SDL_KEYDOWN);
    if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) touchDown = e.type == SDL_MOUSEBUTTONDOWN;
    if (e.type == SDL_MOUSEBUTTONDOWN) { touchX = e.button.x / SCALE; touchY = e.button.y / SCALE; }
    if (e.type == SDL_MOUSEMOTION) { touchX = e.motion.x / SCALE; touchY = e.motion.y / SCALE; }
  }
  return true;
}

static int window() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL: %s\n", SDL_GetError()); return 1; }
  printf("RITA: the mouse is your finger. Keys: 1-9/0 open a robot, WASD/arrows drive (hold), Q/E crab or turn,\n"
         "Space STOP, Backspace back, T WASD/TANK, R RIFT, M DREAM's sound, C camera, V KIDA-01's other camera, Esc quits.\n");
  SDL_Window* win = SDL_CreateWindow("RITA preview (a pretend fleet)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     W * SCALE, H * SCALE, 0);
  renderer = SDL_CreateRenderer(win, -1, 0);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, W, H);
  onLongDelay = [](unsigned long ms) {   // her splash: stay on screen and keep the window alive
    const unsigned long until = SDL_GetTicks() + ms;
    while (SDL_GetTicks() < until && pump()) { present(); SDL_Delay(30); }
  };
  setup();
  onLongDelay = nullptr;
  while (pump()) {
    loop();
    present();
  }
  SDL_Quit();
  return 0;
}

int main(int argc, char** argv) {
  if (argc == 3 && !strcmp(argv[1], "--shots")) {
    shotDir = argv[2];
    return takeShots();
  }
  return window();
}

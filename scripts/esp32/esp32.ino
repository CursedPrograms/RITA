/*
  RITA - Remote Interface for Total Autonomy
  ---------------------------------------------------------------------------
  The fleet's universal controller: a 3.5" ESP32 "Cheap Yellow Display" (ESP32-3248S035R,
  480x320, resistive touch) that drives every DREAM Robotics robot herself, and talks to the
  rest of the fleet through RIFT.

    SPLASH  her avatar, 5 s (splash.h)
    HOME    every robot she can drive, its round avatar with its name under it (avatars.h);
            the ones on the network ringed green, the rest dimmed
    DRIVE   tap one: its own controller, talking to the robot directly (its web API, the same
            its PC controller uses). Hold a pad button to drive, let go to stop; the robot's own
            stops (walls, tilt, its wheel lock) still hold. The tracked ones (MILA, NIKA, KIDA-00,
            KIDA-01) switch between WASD and TANK: the controls change, and so does the robot's
            own mode. One finger at a time (resistive touch), so TANK's tracks latch: set the left,
            then the right, and each keeps going until you change it; LINK moves both together.
    RIFT    from a controller: what the robot says about itself through RIFT (ask) and everything
            RIFT can do with it (do), as timed 1 s moves that ask "let RITA move it?" first.

  WIFI: the fleet's ladder, like the controllers. She joins the highest network she can see
  (RIFT, DREAM, NINA, TINA, the controllers, NORA, then the robots that host when
  nobody above them is around), looks again every minute while she isn't on
  RIFT's, and starts over when hers goes quiet. She never hosts one. The hub is the network's
  host, at its gateway address on :5000; ask and do need RIFT (the others only list robots).
  She registers there as RITA (type "remote") every 10 s, so the fleet sees her too.

  Touch: calibrated on the first boot (tap the four corners), kept in flash. Hold the screen
  while she starts to calibrate again. Pins and panel options: display.h.

  RGB LED: green on RIFT's network, amber on another, red with no network.

  Libraries: LovyanGFX, ArduinoJson 7 (and the esp32 core's WiFi / HTTPClient / Preferences).
  Board: ESP32 Dev Module (esp32:esp32:esp32).
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "display.h"
#include "splash.h"   // her avatar for the boot screen (scripts/make_splash.py)
#include "avatars.h"  // the fleet's round avatars for HOME (scripts/make_avatars.py)

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "No secrets.h: building with secrets.example.h's placeholders (copy it to secrets.h and fill it in)"
#include "secrets.example.h"
#endif

// A robot's own button on DRIVE. Up here, before any function: Arduino puts its function
// prototypes (extrasOf()'s returns one) in front of the first function in the file.
struct Extra { const char* label; const char* id; };

LGFX lcd;
Preferences prefs;

// ---------------------------------------------------------------------------
// Look
// ---------------------------------------------------------------------------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
const uint16_t C_BG     = rgb(7, 9, 15);
const uint16_t C_PANEL  = rgb(16, 22, 36);
const uint16_t C_LINE   = rgb(44, 58, 88);
const uint16_t C_TEXT   = rgb(215, 230, 255);
const uint16_t C_DIM    = rgb(110, 128, 160);
const uint16_t C_ACCENT = rgb(100, 150, 230);   // the fleet's blue
const uint16_t C_OK     = rgb(80, 210, 130);
const uint16_t C_WARN   = rgb(240, 170, 60);
const uint16_t C_BAD    = rgb(235, 80, 90);

const int W = 480, H = 320, BAR = 36;

// ---------------------------------------------------------------------------
// The fleet's WiFi ladder (highest first) and the hub
// ---------------------------------------------------------------------------
const char* const LADDER[] = { FLEET_SSID, "DREAM", "NINA", "TINA",
                               "NORACTL", "WHIPCTL", "KIDA01CTL", "KIDA00CTL", "ARMCTL",
                               "NORA", "WHIP", "KIDA01", "KIDA00", "ARM", "MILA", "NIKA" };   // the whole ladder: whoever hosts
const int LADDER_N = sizeof(LADDER) / sizeof(LADDER[0]);
const unsigned long JOIN_MS      = 15000;   // per network
const unsigned long LOOK_MS      = 60000;   // not on RIFT's: look for a higher one this often
const unsigned long LOST_MS      = 10000;   // no WiFi this long: start over
const unsigned long REGISTER_MS  = 10000;
const int HUB_PORT = 5000;

enum NetState { NET_SCAN, NET_JOINING, NET_UP };
NetState netState = NET_SCAN;
bool seen[LADDER_N];
int tryRank = -1, rank = -1;          // the network being joined / joined (index in LADDER)
unsigned long tryStart = 0, lastLook = 0, lostSince = 0, lastRegister = 0;
bool lookScan = false;                // the scan running is the once-a-minute look, not a start-over
bool hubIsRift = false;               // the hub answers /fleet (RIFT): ask and do work

String hubUrl() { return String("http://") + WiFi.gatewayIP().toString() + ":" + HUB_PORT; }

// ---------------------------------------------------------------------------
// What RITA knows
// ---------------------------------------------------------------------------
struct Bot { String name, type, ip; bool busy; };
const int MAX_BOTS = 16;
Bot bots[MAX_BOTS];
int botCount = 0, fleetPage = 0;
const int ROWS = 6;

String sel;                       // the robot on the ROBOT screen
String statusLines[8];
int statusCount = 0;
String note;
const int MAX_ACTIONS = 32;
String actions[MAX_ACTIONS];
int actionCount = 0, actionPage = 0;
bool moveOk = false;              // a person said yes to moving `sel` (this visit)
String pendingAction;             // waiting for that yes

String toast;
uint16_t toastColor = C_TEXT;
unsigned long toastUntil = 0;

// The relay's harmless actions (RIFT/Fleet/relay.py HARMLESS): everything else moves the robot
const char* const HARMLESS[] = { "stop", "status", "scan", "lock_wheels", "photo", "sleep", "wake", "dance_stop",
                                 "manual_mode", "music_play", "music_next", "music_prev", "music_stop", "music_pause",
                                 "music_resume", "uv_on", "uv_off", "uv_blink", "lights_on", "lights_off", "sound", "chirp" };
bool harmless(const String& a) {
  for (auto h : HARMLESS) if (a == h) return true;
  return false;
}
// The drive pad's actions (RIFT's names for timed moves)
const char* const PAD[] = { "forward", "back", "turn_left", "turn_right", "left", "right" };
bool onPad(const String& a) {
  for (auto p : PAD) if (a == p) return true;
  return a == "stop";
}
bool has(const String& a) {
  for (int i = 0; i < actionCount; i++) if (actions[i] == a) return true;
  return false;
}

// ---------------------------------------------------------------------------
// Screens and buttons
// ---------------------------------------------------------------------------
enum Screen { SCR_HOME, SCR_CTRL, SCR_FLEET, SCR_ROBOT, SCR_CONFIRM };
Screen screen = SCR_HOME;
bool dirty = true;
unsigned long lastRefresh = 0;
const unsigned long REFRESH_MS = 3000;

struct Btn { int16_t x, y, w, h; String id; };
const int MAX_BTNS = 48;
Btn btns[MAX_BTNS];
int btnCount = 0;

void addBtn(int x, int y, int w, int h, const String& id) {
  if (btnCount < MAX_BTNS) btns[btnCount++] = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, id };
}

void button(int x, int y, int w, int h, const String& label, uint16_t edge, const String& id, bool filled = false) {
  lcd.fillRoundRect(x, y, w, h, 8, filled ? edge : C_PANEL);
  lcd.drawRoundRect(x, y, w, h, 8, edge);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(filled ? C_BG : C_TEXT);
  lcd.setTextDatum(middle_center);
  lcd.drawString(label, x + w / 2, y + h / 2);
  lcd.setTextDatum(top_left);
  addBtn(x, y, w, h, id);
}

void say(const String& text, uint16_t color = C_TEXT, unsigned long ms = 2500) {
  toast = text;
  toastColor = color;
  toastUntil = millis() + ms;
  dirty = true;
}

// "turn_left" -> "TURN LEFT"
String label(String a) {
  a.replace("_", " ");
  a.toUpperCase();
  return a;
}

// ---------------------------------------------------------------------------
// LED
// ---------------------------------------------------------------------------
void led(bool r, bool g, bool b) {   // active low
  digitalWrite(PIN_LED_R, !r);
  digitalWrite(PIN_LED_G, !g);
  digitalWrite(PIN_LED_B, !b);
}
void showNetOnLed() {
  if (netState != NET_UP) led(true, false, false);
  else if (rank == 0) led(false, true, false);
  else led(true, true, false);
}

// ---------------------------------------------------------------------------
// Talking to the hub
// ---------------------------------------------------------------------------
// GET or POST (JSON) to the hub; the answer into doc. The HTTP code, or <= 0 with no answer.
int hub(const String& path, JsonDocument& doc, const String& body = "") {
  if (netState != NET_UP) return -1;
  HTTPClient http;
  http.begin(hubUrl() + path);
  http.setTimeout(body.length() ? 8000 : 2500);   // a do waits for the robot (and a timed move)
  int code;
  if (body.length()) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
  } else {
    code = http.GET();
  }
  if (code > 0) {
    DeserializationError e = deserializeJson(doc, http.getString());
    if (e) doc.clear();
  }
  http.end();
  return code;
}

void registerWithHub() {
  HTTPClient http;
  http.begin(hubUrl() + "/register");
  http.setTimeout(2000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.POST("name=RITA&type=remote&capabilities=remote");
  http.end();
}

void fetchFleet() {
  JsonDocument doc;
  int code = hub("/fleet", doc);   // RIFT: everyone, merged with NORA's list
  hubIsRift = code == 200;
  if (!hubIsRift) code = hub("/robots", doc);   // NORA or another host: its own list
  if (code != 200) { botCount = 0; return; }
  botCount = 0;
  for (JsonObject r : doc["robots"].as<JsonArray>()) {
    String name = r["name"] | "";
    if (!name.length() || name == "RITA" || botCount >= MAX_BOTS) continue;
    bots[botCount++] = { name, String(r["type"] | "robot"), String(r["ip"] | ""), r["busy"] | false };
  }
  if (fleetPage * ROWS >= botCount) fleetPage = 0;
}

void askRobot() {
  statusCount = 0;
  note = "";
  if (!hubIsRift) { note = "Ask and do need RIFT's network."; return; }
  JsonDocument doc;
  String body = "{\"to\":\"" + sel + "\",\"verb\":\"ask\",\"from\":\"RITA\"}";
  int code = hub("/talk", doc, body);
  if (code <= 0) { note = "RIFT didn't answer."; return; }
  if (!(doc["ok"] | false)) { note = String(doc["error"] | "no answer"); }
  JsonObject core = doc["core"].as<JsonObject>();
  for (JsonPair kv : core) {
    if (statusCount >= 8) break;
    String v;
    if (kv.value().is<JsonObject>() || kv.value().is<JsonArray>()) serializeJson(kv.value(), v);
    else v = kv.value().as<String>();
    if (v == "null" || !v.length()) continue;
    String k = String(kv.key().c_str());
    k.replace("_", " ");
    statusLines[statusCount++] = k + ": " + v;
  }
  if (doc["note"].is<const char*>()) note = String(doc["note"].as<const char*>());
  actionCount = 0;
  for (JsonVariant a : doc["does"].as<JsonArray>()) {
    if (actionCount < MAX_ACTIONS) actions[actionCount++] = a.as<String>();
  }
}

void doAction(const String& a) {
  if (!hubIsRift) { say("Ask and do need RIFT", C_WARN); return; }
  JsonDocument doc;
  String body = "{\"to\":\"" + sel + "\",\"verb\":\"do\",\"action\":\"" + a + "\",\"from\":\"RITA\"";
  if (!harmless(a)) body += ",\"confirmed\":true,\"seconds\":1";
  body += "}";
  say(label(a) + "...", C_DIM, 8000);
  draw();
  int code = hub("/talk", doc, body);
  if (code <= 0) say("RIFT didn't answer", C_BAD);
  else if (doc["ok"] | false) say(sel + ": " + label(a) + " done", C_OK);
  else say(String(doc["error"] | "refused"), C_BAD, 4000);
}

// ---------------------------------------------------------------------------
// The roster: every robot she drives herself, in HOME's order (scripts/make_avatars.py has the
// same list for their avatars). Each talks its own web API, the one its PC controller uses.
// ---------------------------------------------------------------------------
enum Kind : uint8_t {
  K_WHIP,     // hexapod, :5005  /unlock, /mode?m=web, /web?cmd=drive&f&l&t (resent; stops 500 ms after)
  K_NORA,     // mecanum, :5002  /modeManual, /drive?f&s&t (resent; stops 500 ms after), /stop
  K_TANK,     // MILA :5010, NIKA :5013 (ESP8266 bridge)  /unlock, /mode?v=wasd|tank, /cmd?v=FORWARD..,
              //   L_FWD.. R_BWD, STOP; /status every 400 ms or the bridge stops her (its watchdog)
  K_KIDA00,   // Pi, :5003  POST /command {"command": forward | drive:l:r | tank:l:r | stop} (resent: 0.8 s)
  K_KIDA01,   // Pi, :5004  POST /action {"command": joy_drive, left, right} (resent: 0.8 s), move_stop
  K_IDA,      // no WiFi: NORA's IR link, NORA's /ida?c=fw|bw|left|right|stop
  K_ARM,      // no WiFi: her USB port, from her PC
};
struct Robot { const char* name; const char* label; Kind kind; uint16_t port; };
const Robot ROSTER[] = {
  { "WHIP", "WHIP", K_WHIP, 5005 },     { "NORA", "NORA", K_NORA, 5002 },
  { "MILA", "MILA", K_TANK, 5010 },     { "NIKA", "NIKA", K_TANK, 5013 },
  { "KIDA00", "KIDA-00", K_KIDA00, 5003 }, { "KIDA01", "KIDA-01", K_KIDA01, 5004 },
  { "IDA", "IDA", K_IDA, 5002 },        { "ARM", "ARM", K_ARM, 0 },
};
const int ROSTER_N = sizeof(ROSTER) / sizeof(ROSTER[0]);

// Where a robot is, from the fleet list (IDA: through NORA). "" = not on the network.
String ipOf(const char* name) {
  for (int i = 0; i < botCount; i++) if (bots[i].name == name) return bots[i].ip;
  return "";
}
String robotIp(int r) {
  if (ROSTER[r].kind == K_ARM) return "";
  return ipOf(ROSTER[r].kind == K_IDA ? "NORA" : ROSTER[r].name);
}
bool tracked(int r) { return ROSTER[r].kind == K_TANK || ROSTER[r].kind == K_KIDA00 || ROSTER[r].kind == K_KIDA01; }

const uint8_t* avatarOf(const char* name, size_t* len) {
  for (int i = 0; i < AVATAR_COUNT; i++) if (!strcmp(AVATARS[i].name, name)) { *len = AVATARS[i].len; return AVATARS[i].jpg; }
  return nullptr;
}

// ---- the controller: which robot, and what it's doing ----
int cur = -1;                      // ROSTER index on DRIVE
String curBase;                    // "http://ip:port"
bool tankUi = false;               // tracked robots: TANK (true) or WASD
int8_t trackL = 0, trackR = 0;     // TANK: each track -1 / 0 / 1, latched
bool linkTracks = false;           // TANK: one tap sets both
String heldPad;                    // the pad button held now ("" = none)
float heldF = 0, heldS = 0, heldT = 0;   // ... as forward, sideways (+ left), turn (+ anticlockwise)
unsigned long lastBeat = 0, lastPoll = 0;
int lastCode = 0;                  // the robot's last answer (HTTP code; <= 0: none)

int robotGet(const String& path, uint16_t timeout = 600) {
  if (!curBase.length() || netState != NET_UP) return -1;
  HTTPClient http;
  http.begin(curBase + path);
  http.setTimeout(timeout);
  int code = http.GET();
  http.end();
  return code;
}
int robotPost(const String& path, const String& json, uint16_t timeout = 600) {
  if (!curBase.length() || netState != NET_UP) return -1;
  HTTPClient http;
  http.begin(curBase + path);
  http.setTimeout(timeout);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  http.end();
  return code;
}
String num(float v) { return String(v, 2); }
int kida00(const String& c) { return robotPost("/command", "{\"command\":\"" + c + "\"}"); }
int kida01(const String& c, const String& more = "") {
  return robotPost("/action", "{\"command\":\"" + c + "\"" + more + "}");
}
int kida01Tracks(float l, float r) { return kida01("joy_drive", ",\"left\":" + num(l) + ",\"right\":" + num(r)); }
// Arcade to tracks: + turn (anticlockwise) slows the left track
void mixTracks(float f, float t, float* l, float* r) {
  *l = constrain(f - t, -1.0f, 1.0f);
  *r = constrain(f + t, -1.0f, 1.0f);
}

// Into the robot's controller: sign in where it has one, and its manual mode, so the pad drives it
void ctlEnter(int r) {
  cur = r;
  const String ip = robotIp(r);
  curBase = ip.length() ? "http://" + ip + ":" + String(ROSTER[r].port) : "";
  trackL = trackR = 0; linkTracks = false; heldPad = ""; lastCode = 0;
  tankUi = false;
  if (!curBase.length()) return;
  switch (ROSTER[r].kind) {
    case K_WHIP: robotGet("/unlock?pw=" ROBOT_PIN); lastCode = robotGet("/mode?m=web"); break;
    case K_NORA: lastCode = robotGet("/modeManual"); break;
    case K_TANK: robotGet("/unlock?pw=" ROBOT_PIN); lastCode = robotGet("/mode?v=wasd"); break;
    case K_KIDA00: lastCode = kida00("_mode_user"); kida00("scheme_wasd"); break;
    case K_KIDA01: lastCode = kida01("mode_1"); kida01("scheme_wasd"); break;
    case K_IDA: lastCode = robotGet("/ida?c=manual"); break;
    default: break;
  }
}

// One drive command (pad held): f forward, s sideways (+ left), t turn (+ anticlockwise)
void ctlDrive(float f, float s, float t, bool first) {
  float l, r;
  switch (ROSTER[cur].kind) {
    case K_WHIP: lastCode = robotGet("/web?cmd=drive&f=" + num(f) + "&l=" + num(s) + "&t=" + num(t)); break;
    case K_NORA: lastCode = robotGet("/drive?f=" + num(f) + "&s=" + num(s) + "&t=" + num(t)); break;
    case K_TANK:   // one command per press: the bridge holds it until STOP
      if (first) lastCode = robotGet(String("/cmd?v=") + (f > 0 ? "FORWARD" : f < 0 ? "BACKWARD" : t > 0 ? "LEFT" : "RIGHT"));
      break;
    case K_KIDA00:
      if (t == 0) lastCode = kida00(f > 0 ? "forward" : "backward");
      else if (f == 0) lastCode = kida00(t > 0 ? "left" : "right");
      else { mixTracks(f, t, &l, &r); lastCode = kida00("drive:" + num(l) + ":" + num(r)); }
      break;
    case K_KIDA01: mixTracks(f, t, &l, &r); lastCode = kida01Tracks(l, r); break;
    case K_IDA:
      lastCode = robotGet(String("/ida?c=") + (f > 0 ? "fw" : f < 0 ? "bw" : t > 0 ? "left" : "right"));
      break;
    default: break;
  }
}

void ctlStop() {
  switch (ROSTER[cur].kind) {
    case K_WHIP: lastCode = robotGet("/web?cmd=stop"); break;
    case K_NORA: lastCode = robotGet("/stop"); break;
    case K_TANK: lastCode = robotGet("/cmd?v=STOP"); break;
    case K_KIDA00: lastCode = kida00("stop"); break;
    case K_KIDA01: lastCode = kida01("move_stop"); break;
    case K_IDA: lastCode = robotGet("/ida?c=stop"); break;
    default: break;
  }
}

// TANK: the tracks as latched now. MILA / NIKA take one track at a time (L_FWD...): a track
// going to 0 stops both, then the other one starts again.
void ctlTracks(int8_t oldL, int8_t oldR) {
  switch (ROSTER[cur].kind) {
    case K_TANK: {
      if (!trackL && !trackR) { lastCode = robotGet("/cmd?v=STOP"); break; }
      if ((oldL && !trackL) || (oldR && !trackR)) robotGet("/cmd?v=STOP");
      if (trackL && (trackL != oldL || (oldR && !trackR))) lastCode = robotGet(trackL > 0 ? "/cmd?v=L_FWD" : "/cmd?v=L_BWD");
      if (trackR && (trackR != oldR || (oldL && !trackL))) lastCode = robotGet(trackR > 0 ? "/cmd?v=R_FWD" : "/cmd?v=R_BWD");
      break;
    }
    case K_KIDA00:
      lastCode = (trackL || trackR) ? kida00("tank:" + String(trackL) + ":" + String(trackR)) : kida00("stop");
      break;
    case K_KIDA01: lastCode = (trackL || trackR) ? kida01Tracks(trackL, trackR) : kida01("move_stop"); break;
    default: break;
  }
}

// WASD <-> TANK: everything stops, and the robot's own scheme changes with the controls
void ctlScheme(bool tank) {
  if (heldPad.length()) { heldPad = ""; ctlStop(); }
  trackL = trackR = 0;
  ctlStop();
  tankUi = tank;
  switch (ROSTER[cur].kind) {
    case K_TANK: lastCode = robotGet(tank ? "/mode?v=tank" : "/mode?v=wasd"); break;
    case K_KIDA00: lastCode = kida00(tank ? "scheme_tank" : "scheme_wasd"); break;
    case K_KIDA01: lastCode = kida01(tank ? "scheme_qaws" : "scheme_wasd"); break;
    default: break;
  }
  say(tank ? "TANK: tap a track to set it, it keeps going" : "WASD: hold a direction", C_ACCENT);
}

// Leaving the controller: nothing keeps moving
void ctlLeave() {
  if (cur >= 0 && curBase.length()) {
    if (heldPad.length() || trackL || trackR) ctlStop();
    if (ROSTER[cur].kind == K_TANK && tankUi) robotGet("/mode?v=wasd");
  }
  heldPad = ""; trackL = trackR = 0; cur = -1; curBase = "";
}

// While on DRIVE: resend a held drive (they stop by themselves when it stops coming), keep TANK's
// latched tracks alive on the KIDAs, and poll MILA / NIKA's /status for their bridge's watchdog
void ctlLoop() {
  if (cur < 0 || !curBase.length()) return;
  const Kind k = ROSTER[cur].kind;
  const unsigned long now = millis();
  if (heldPad.length() && k != K_TANK && now - lastBeat > 200) { lastBeat = now; ctlDrive(heldF, heldS, heldT, false); }
  if (!heldPad.length() && (trackL || trackR) && (k == K_KIDA00 || k == K_KIDA01) && now - lastBeat > 300) {
    lastBeat = now;
    ctlTracks(trackL, trackR);
  }
  const unsigned long pollMs = k == K_TANK ? 400 : 1500;
  if (now - lastPoll > pollMs && k != K_IDA) {
    lastPoll = now;
    const int code = robotGet("/status", 500);
    if ((code > 0) != (lastCode > 0)) dirty = true;
    if (!heldPad.length() || code <= 0) lastCode = code;
  }
}

// The pad's buttons: what each means for this robot (forward, sideways, turn)
bool padVec(const String& id, float* f, float* s, float* t) {
  const Kind k = ROSTER[cur].kind;
  const bool slides = k == K_NORA;            // A / D strafe: mecanum
  *f = *s = *t = 0;
  if      (id == "p:f")  *f = 1;
  else if (id == "p:b")  *f = -1;
  else if (id == "p:l")  { if (slides) *s = 1; else *t = 1; }
  else if (id == "p:r")  { if (slides) *s = -1; else *t = -1; }
  else if (id == "p:fl") { *f = 1;  if (slides) *s = 1;  else *t = 0.5f; }
  else if (id == "p:fr") { *f = 1;  if (slides) *s = -1; else *t = -0.5f; }
  else if (id == "p:bl") { *f = -1; if (slides) *s = 1;  else *t = -0.5f; }
  else if (id == "p:br") { *f = -1; if (slides) *s = -1; else *t = 0.5f; }
  else if (id == "p:sl") *s = 1;               // WHIP: crab
  else if (id == "p:sr") *s = -1;
  else if (id == "p:tl") *t = 1;               // NORA: turn on the spot
  else if (id == "p:tr") *t = -1;
  else return false;
  return true;
}

void padPress(const String& id) {
  if (!curBase.length()) { say(String(ROSTER[cur].label) + " isn't on the network", C_WARN); return; }
  if (!padVec(id, &heldF, &heldS, &heldT)) return;
  heldPad = id;
  lastBeat = millis();
  ctlDrive(heldF, heldS, heldT, true);
  dirty = true;
}
void padRelease() {
  if (!heldPad.length()) return;
  heldPad = "";
  ctlStop();
  dirty = true;
}

// The buttons beside the pad: each robot's own (id "x:<what>")
const Extra EX_WHIP[]   = { {"HELLO", "hello"}, {"DANCE", "dance"}, {"REST", "rest"}, {"AUTO", "auto"}, {"MANUAL", "manual"} };
const Extra EX_NORA[]   = { {"AUTO", "auto"}, {"MANUAL", "manual"}, {"UV ON", "uvon"}, {"UV OFF", "uvoff"},
                            {"MUSIC", "music"}, {"NEXT", "next"}, {"UNLOCK", "unlock"}, {"LOCK", "lock"} };
const Extra EX_TANK[]   = { {"AUTO", "auto"}, {"MANUAL", "manual"}, {"SPEED", "speed"}, {"CHIRP", "chirp"},
                            {"UNLOCK", "unlock"}, {"LOCK", "lock"} };
const Extra EX_KIDA00[] = { {"AUTO", "auto"}, {"MANUAL", "manual"}, {"PHOTO", "photo"}, {"MUSIC", "music"},
                            {"SKIP", "skip"}, {"UNLOCK", "unlock"}, {"LOCK", "lock"} };
const Extra EX_KIDA01[] = { {"MANUAL", "manual"}, {"SPEED", "speed"}, {"PHOTO", "photo"}, {"MUSIC", "music"},
                            {"SKIP", "skip"}, {"UNLOCK", "unlock"}, {"LOCK", "lock"} };
const Extra EX_IDA[]    = { {"AUTO", "auto"}, {"MANUAL", "manual"}, {"SPEED", "speed"} };
const Extra* extrasOf(int r, int* n) {
  switch (ROSTER[r].kind) {
    case K_WHIP:   *n = sizeof(EX_WHIP) / sizeof(Extra);   return EX_WHIP;
    case K_NORA:   *n = sizeof(EX_NORA) / sizeof(Extra);   return EX_NORA;
    case K_TANK:   *n = sizeof(EX_TANK) / sizeof(Extra);   return EX_TANK;
    case K_KIDA00: *n = sizeof(EX_KIDA00) / sizeof(Extra); return EX_KIDA00;
    case K_KIDA01: *n = sizeof(EX_KIDA01) / sizeof(Extra); return EX_KIDA01;
    case K_IDA:    *n = sizeof(EX_IDA) / sizeof(Extra);    return EX_IDA;
    default:       *n = 0; return nullptr;
  }
}

void ctlExtra(const String& x) {
  if (!curBase.length()) { say(String(ROSTER[cur].label) + " isn't on the network", C_WARN); return; }
  const Kind k = ROSTER[cur].kind;
  int code = -1;
  switch (k) {
    case K_WHIP:
      if (x == "auto") code = robotGet("/mode?m=obstacle");
      else if (x == "manual") code = robotGet("/mode?m=web");
      else code = robotGet("/act?do=" + x);
      break;
    case K_NORA:
      if (x == "auto") code = robotGet("/modeAuto");
      else if (x == "manual") code = robotGet("/modeManual");
      else if (x == "uvon") code = robotGet("/uvOn");
      else if (x == "uvoff") code = robotGet("/uvOff");
      else if (x == "music") code = robotGet("/muPlay");
      else if (x == "next") code = robotGet("/muNext");
      else if (x == "unlock") code = robotGet("/motorlockOff?pw=" ROBOT_PIN);
      else if (x == "lock") code = robotGet("/motorlockOn");
      break;
    case K_TANK:
      if (x == "auto") code = robotGet("/mode?v=obstacle");
      else if (x == "manual") code = robotGet(tankUi ? "/mode?v=tank" : "/mode?v=wasd");
      else if (x == "speed") code = robotGet("/cmd?v=SPEEDCYCLE");
      else if (x == "chirp") code = robotGet("/chirp");
      else if (x == "unlock") code = robotGet("/motorlockOff?pw=" ROBOT_PIN);
      else if (x == "lock") code = robotGet("/motorlockOn");
      break;
    case K_KIDA00:
      if (x == "auto") code = kida00("_mode_autonomous");
      else if (x == "manual") code = kida00("_mode_user");
      else if (x == "photo") code = kida00("photo");
      else if (x == "music") code = kida00("music_toggle");
      else if (x == "skip") code = kida00("skip");
      else if (x == "unlock") code = robotGet("/motorlockOff?pw=" ROBOT_PIN);
      else if (x == "lock") code = robotGet("/motorlockOn");
      break;
    case K_KIDA01:
      if (x == "manual") code = kida01("mode_1");
      else if (x == "speed") code = kida01("speed_cycle");
      else if (x == "photo") code = kida01("photo");
      else if (x == "music") code = kida01("music_play");
      else if (x == "skip") code = kida01("music_skip");
      else if (x == "unlock") code = robotGet("/unlock?pw=" ROBOT_PIN);
      else if (x == "lock") code = kida01("motor_lock_on");
      break;
    case K_IDA: code = robotGet("/ida?c=" + x); break;
    default: break;
  }
  lastCode = code;
  say(code == 200 ? String(ROSTER[cur].label) + ": " + label(x) : String(ROSTER[cur].label) + " said no (" + code + ")",
      code == 200 ? C_OK : C_BAD);
}

// ---------------------------------------------------------------------------
// WiFi: the ladder
// ---------------------------------------------------------------------------
void startScan(bool look) {
  lookScan = look;
  WiFi.scanNetworks(true);   // async
  if (!look) netState = NET_SCAN;
}

void joinNext() {
  for (int r = tryRank + 1; r < LADDER_N; r++) {
    if (!seen[r]) continue;
    tryRank = r;
    tryStart = millis();
    WiFi.begin(LADDER[r], r == 0 ? FLEET_PASSWORD : NORA_PASSWORD);
    netState = NET_JOINING;
    say(String("Joining ") + LADDER[r], C_DIM, JOIN_MS);
    return;
  }
  startScan(false);   // nothing (left) to join: look again
}

void netLoop() {
  int n = WiFi.scanComplete();
  if (n >= 0) {          // a scan finished
    bool any = false, higher = false;
    for (int r = 0; r < LADDER_N; r++) seen[r] = false;
    for (int i = 0; i < n; i++) {
      String s = WiFi.SSID(i);
      for (int r = 0; r < LADDER_N; r++) {
        if (s == LADDER[r]) { seen[r] = true; any = true; if (netState == NET_UP && r < rank) higher = true; }
      }
    }
    WiFi.scanDelete();
    if (lookScan) {
      lookScan = false;
      if (higher) { WiFi.disconnect(); tryRank = -1; joinNext(); }
    } else if (any) {
      tryRank = -1;
      joinNext();
    } else {
      startScan(false);
    }
    dirty = true;
  }

  if (netState == NET_JOINING) {
    if (WiFi.status() == WL_CONNECTED) {
      netState = NET_UP;
      rank = tryRank;
      lostSince = 0;
      lastLook = millis();
      lastRegister = 0;
      say(String("On ") + LADDER[rank], C_OK);
      lastRefresh = 0;
      showNetOnLed();
    } else if (millis() - tryStart > JOIN_MS) {
      WiFi.disconnect();
      joinNext();
    }
  } else if (netState == NET_UP) {
    if (WiFi.status() != WL_CONNECTED) {
      if (!lostSince) lostSince = millis();
      if (millis() - lostSince > LOST_MS) {
        rank = -1;
        botCount = 0;
        hubIsRift = false;
        say("Network lost: looking again", C_WARN);
        showNetOnLed();
        startScan(false);
      }
    } else {
      lostSince = 0;
      if (millis() - lastRegister > REGISTER_MS) { lastRegister = millis(); registerWithHub(); }
      if (rank > 0 && !lookScan && millis() - lastLook > LOOK_MS) { lastLook = millis(); startScan(true); }
    }
  }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void drawBar(const String& title, bool back) {
  lcd.fillRect(0, 0, W, BAR, C_PANEL);
  lcd.drawFastHLine(0, BAR, W, C_LINE);
  if (back) {
    button(4, 4, 88, BAR - 8, screen == SCR_CTRL ? "< HOME" : "< BACK", C_ACCENT, "back");
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_TEXT);
    lcd.drawString(title, 104, 6);
  } else {
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_ACCENT);
    lcd.drawString(title, 10, 6);
  }
  String net;
  uint16_t c;
  if (netState == NET_UP) { net = String(LADDER[rank]) + (hubIsRift ? " / RIFT" : " / list only"); c = rank == 0 ? C_OK : C_WARN; }
  else if (netState == NET_JOINING) { net = String("joining ") + LADDER[tryRank]; c = C_DIM; }
  else { net = "looking for the fleet"; c = C_BAD; }
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(c);
  lcd.setTextDatum(top_right);
  lcd.drawString(net, W - 8, 11);
  lcd.setTextDatum(top_left);
}

void drawToast() {
  if (!toast.length() || millis() > toastUntil) return;
  lcd.fillRoundRect(60, H - 34, W - 120, 28, 8, C_PANEL);
  lcd.drawRoundRect(60, H - 34, W - 120, 28, 8, toastColor);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(toastColor);
  lcd.setTextDatum(middle_center);
  lcd.drawString(toast, W / 2, H - 20);
  lcd.setTextDatum(top_left);
}

void drawFleet() {
  drawBar("RITA", false);
  if (netState != NET_UP) {
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DIM);
    lcd.drawString("Looking for RIFT, DREAM, NINA, TINA, the controllers or NORA...", 14, 60);
    return;
  }
  if (!botCount) {
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DIM);
    lcd.drawString("No robots on the list yet.", 14, 60);
    return;
  }
  int first = fleetPage * ROWS;
  for (int i = 0; i < ROWS && first + i < botCount; i++) {
    const Bot& b = bots[first + i];
    int y = BAR + 6 + i * 46;
    lcd.fillRoundRect(6, y, 420, 40, 8, C_PANEL);
    lcd.drawRoundRect(6, y, 420, 40, 8, b.busy ? C_WARN : C_LINE);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_TEXT);
    lcd.drawString(b.name, 16, y + 8);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DIM);
    lcd.drawString(b.type + "  " + b.ip + (b.busy ? "  (busy)" : ""), 170, y + 13);
    addBtn(6, y, 420, 40, "bot:" + String(first + i));
  }
  if (botCount > ROWS) {
    button(432, BAR + 6, 42, 130, "^", C_ACCENT, "up");
    button(432, BAR + 142, 42, 130, "v", C_ACCENT, "down");
  }
}

void drawRobot() {
  drawBar(sel, true);
  // what it says about itself
  lcd.fillRoundRect(4, BAR + 6, 228, 150, 8, C_PANEL);
  lcd.setFont(&fonts::Font2);
  int y = BAR + 12;
  for (int i = 0; i < statusCount && y < BAR + 146; i++, y += 17) {
    lcd.setTextColor(C_TEXT);
    lcd.drawString(statusLines[i].substring(0, 34), 12, y);
  }
  if (note.length()) {
    lcd.setTextColor(C_DIM);
    lcd.drawString(note.substring(0, 34), 12, y);
    if (note.length() > 34) lcd.drawString(note.substring(34, 68), 12, y + 17);
  }

  // the drive pad: forward / turns / back / stop, and strafes when it has them
  int px = 118, py = 236;   // centre of the pad
  if (has("forward"))    button(px - 30, py - 74, 60, 44, "^", C_WARN, "do:forward");
  if (has("back"))       button(px - 30, py + 30, 60, 44, "v", C_WARN, "do:back");
  if (has("turn_left"))  button(px - 96, py - 22, 60, 44, "<", C_WARN, "do:turn_left");
  if (has("turn_right")) button(px + 36, py - 22, 60, 44, ">", C_WARN, "do:turn_right");
  if (has("left"))       button(4, py - 74, 50, 36, "<<", C_WARN, "do:left");
  if (has("right"))      button(182, py - 74, 50, 36, ">>", C_WARN, "do:right");
  if (has("stop"))       button(px - 30, py - 22, 60, 44, "STOP", C_BAD, "do:stop", true);

  // everything else it can do
  String rest[MAX_ACTIONS];
  int n = 0;
  for (int i = 0; i < actionCount; i++) if (!onPad(actions[i])) rest[n++] = actions[i];
  const int COLS = 2, PER = 12;
  if (actionPage * PER >= n) actionPage = 0;
  for (int i = 0; i < PER && actionPage * PER + i < n; i++) {
    const String& a = rest[actionPage * PER + i];
    int bx = 240 + (i % COLS) * 118, by = BAR + 6 + (i / COLS) * 44;
    button(bx, by, 112, 38, label(a), harmless(a) ? C_OK : C_WARN, "do:" + a);
  }
  if (n > PER) button(240, H - 40, 232, 34, "MORE", C_ACCENT, "more");
  if (!actionCount && hubIsRift) {
    lcd.setTextColor(C_DIM);
    lcd.drawString("Nothing it can do through RIFT yet.", 244, BAR + 14);
  }
}

void drawConfirm() {
  lcd.fillRoundRect(40, 60, W - 80, H - 120, 12, C_PANEL);
  lcd.drawRoundRect(40, 60, W - 80, H - 120, 12, C_WARN);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(C_TEXT);
  lcd.setTextDatum(top_center);
  lcd.drawString("Let RITA move " + sel + "?", W / 2, 84);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_DIM);
  lcd.drawString("Each tap moves it for 1 second.", W / 2, 124);
  lcd.drawString("Its own stops (walls, tilt, locks) still hold.", W / 2, 144);
  lcd.setTextDatum(top_left);
  button(80, H - 116, 140, 48, "YES", C_OK, "yes", true);
  button(W - 220, H - 116, 140, 48, "NO", C_BAD, "no");
}

// HOME: the roster in a 4 x 2 grid, each robot's round avatar with its name under it. On the
// network: a green ring; not: dimmed, a grey ring. (Online = in the fleet list.)
const int GRID_COLS = 4, CELL_W = W / GRID_COLS, CELL_H = (H - BAR) / 2;
void drawHome() {
  drawBar("RITA", false);
  for (int r = 0; r < ROSTER_N && r < GRID_COLS * 2; r++) {
    const int cx = (r % GRID_COLS) * CELL_W + CELL_W / 2;
    const int top = BAR + (r / GRID_COLS) * CELL_H + 10;
    const int cy = top + AVATAR_SIZE / 2;
    const bool on = robotIp(r).length() > 0;
    size_t len = 0;
    const uint8_t* jpg = avatarOf(ROSTER[r].name, &len);
    if (jpg) lcd.drawJpg(jpg, len, cx - AVATAR_SIZE / 2, top);
    if (!on) {   // dimmed: a dark veil of dots over the picture
      for (int y = top; y < top + AVATAR_SIZE; y += 2)
        for (int x = cx - AVATAR_SIZE / 2 + (y & 2 ? 1 : 0); x < cx + AVATAR_SIZE / 2; x += 2) lcd.drawPixel(x, y, C_BG);
    }
    const uint16_t ring = on ? C_OK : C_LINE;
    lcd.drawCircle(cx, cy, AVATAR_SIZE / 2 + 2, ring);
    lcd.drawCircle(cx, cy, AVATAR_SIZE / 2 + 3, ring);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(on ? C_TEXT : C_DIM);
    lcd.setTextDatum(top_center);
    lcd.drawString(ROSTER[r].label, cx, top + AVATAR_SIZE + 8);
    lcd.setTextDatum(top_left);
    addBtn(cx - CELL_W / 2, top - 6, CELL_W, CELL_H, "go:" + String(r));
  }
}

// DRIVE: the pad (WASD) or the two tracks (TANK) on the left, the robot's own buttons on the right
void padButton(int x, int y, int w, int h, const String& txt, const String& id) {
  const bool held = heldPad == id;
  button(x, y, w, h, txt, held ? C_OK : C_WARN, id, held);
}
void trackButton(int x, int y, int w, int h, const String& txt, const String& id, bool on, uint16_t c) {
  button(x, y, w, h, txt, c, id, on);
}

void drawCtrl() {
  const Robot& R = ROSTER[cur];
  drawBar(R.label, true);
  const bool tank = tracked(cur) && tankUi;
  if (tracked(cur)) {   // WASD / TANK, where the network name usually is
    lcd.fillRect(250, 2, W - 252, BAR - 4, C_PANEL);
    button(W - 176, 4, 84, BAR - 8, "WASD", C_ACCENT, "scheme:wasd", !tankUi);
    button(W - 88, 4, 84, BAR - 8, "TANK", C_ACCENT, "scheme:tank", tankUi);
  }

  if (R.kind == K_ARM) {
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DIM);
    lcd.drawString("ARM has no WiFi: she's driven from her PC,", 14, BAR + 20);
    lcd.drawString("over her USB port (ARM-Robot-v01/scripts).", 14, BAR + 40);
    return;
  }

  // ---- left: the pad or the tracks (x 0-300) ----
  const int bx = 12, by = BAR + 8, bw = 88, bh = 70, g = 6;
  if (tank) {
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DIM);
    lcd.setTextDatum(top_center);
    lcd.drawString("LEFT", bx + bw / 2, by);
    lcd.drawString("RIGHT", bx + 2 * (bw + g) + bw / 2, by);
    lcd.setTextDatum(top_left);
    const int ty = by + 18;
    for (int side = 0; side < 2; side++) {
      const int x = bx + side * 2 * (bw + g);
      const int8_t v = side ? trackR : trackL;
      const String s = side ? "R" : "L";
      trackButton(x, ty, bw, 84, "FWD", "t:" + s + "+", v > 0, C_OK);
      trackButton(x, ty + 90, bw, 54, "STOP", "t:" + s + "0", v == 0, C_DIM);
      trackButton(x, ty + 150, bw, 84, "BACK", "t:" + s + "-", v < 0, C_WARN);
    }
    trackButton(bx + bw + g, ty, bw, 60, "LINK", "t:link", linkTracks, C_ACCENT);
    button(bx + bw + g, ty + 66, bw, 168, "STOP", C_BAD, "t:stop", true);
  } else {
    const Kind k = R.kind;
    const bool diag = k == K_WHIP || k == K_NORA || k == K_KIDA00 || k == K_KIDA01;   // MILA / NIKA, IDA: 4 ways
    const char* ids[9]  = { "p:fl", "p:f", "p:fr", "p:l", "stop", "p:r", "p:bl", "p:b", "p:br" };
    const char* txts[9] = { "W+A", "W", "W+D", "A", "STOP", "D", "S+A", "S", "S+D" };
    for (int i = 0; i < 9; i++) {
      const int x = bx + (i % 3) * (bw + g), y = by + (i / 3) * (bh + g);
      if (i == 4) { button(x, y, bw, bh, "STOP", C_BAD, "stop", true); continue; }
      if (!diag && (i == 0 || i == 2 || i == 6 || i == 8)) continue;
      padButton(x, y, bw, bh, txts[i], ids[i]);
    }
    const int ey = by + 3 * (bh + g), ew = (3 * bw + 2 * g - g) / 2;
    if (k == K_WHIP) {         // crab
      padButton(bx, ey, ew, 40, "<< CRAB", "p:sl");
      padButton(bx + ew + g, ey, ew, 40, "CRAB >>", "p:sr");
    } else if (k == K_NORA) {  // A / D slide, so turning goes here
      padButton(bx, ey, ew, 40, "TURN L", "p:tl");
      padButton(bx + ew + g, ey, ew, 40, "TURN R", "p:tr");
    } else {
      lcd.setFont(&fonts::Font2);
      lcd.setTextColor(C_DIM);
      lcd.drawString(k == K_IDA ? "through NORA's IR link" : "A / D turn on the spot", bx, ey + 12);
    }
  }

  // ---- right: the avatar, how it's answering, its own buttons (x 300-480) ----
  const int rx = 306;
  size_t len = 0;
  const uint8_t* jpg = avatarOf(R.name, &len);
  if (jpg) lcd.drawJpg(jpg, len, rx, BAR + 8);
  lcd.setFont(&fonts::Font2);
  String state;
  uint16_t c;
  if (!curBase.length()) { state = "not on the network"; c = C_BAD; }
  else if (lastCode == 200) { state = "answering"; c = C_OK; }
  else if (lastCode == 403) { state = "locked (PIN)"; c = C_WARN; }
  else if (lastCode <= 0) { state = "no answer"; c = C_BAD; }
  else { state = "said " + String(lastCode); c = C_WARN; }
  lcd.setTextColor(c);
  lcd.drawString(state, rx, BAR + AVATAR_SIZE + 12);   // under the avatar: 166 px across
  int n = 0;
  const Extra* ex = extrasOf(cur, &n);
  const int ew2 = 80, eh = 30, ey0 = BAR + AVATAR_SIZE + 32;   // 4 rows of 2, then RIFT's
  for (int i = 0; i < n && i < 8; i++) {
    button(rx + (i % 2) * (ew2 + 6), ey0 + (i / 2) * (eh + 4), ew2, eh, ex[i].label,
           !strcmp(ex[i].id, "unlock") ? C_WARN : C_ACCENT, String("x:") + ex[i].id);
  }
  if (hubIsRift) button(rx, ey0 + 4 * (eh + 4), 2 * ew2 + 6, eh, "RIFT: ASK / DO", C_DIM, "rift");
}

void draw() {
  btnCount = 0;
  lcd.startWrite();
  lcd.fillScreen(C_BG);
  if (screen == SCR_HOME) drawHome();
  else if (screen == SCR_CTRL) drawCtrl();
  else if (screen == SCR_FLEET) drawFleet();
  else drawRobot();
  if (screen == SCR_CONFIRM) { btnCount = 0; drawConfirm(); }
  drawToast();
  lcd.endWrite();
  dirty = false;
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------
// TANK: a track button. With LINK on, the other track goes the same way.
void tankTap(const String& id) {
  const int8_t oldL = trackL, oldR = trackR;
  if (id == "t:link") { linkTracks = !linkTracks; if (linkTracks) trackR = trackL; }
  else if (id == "t:stop") { trackL = trackR = 0; }
  else {
    const int8_t v = id.endsWith("+") ? 1 : id.endsWith("-") ? -1 : 0;
    if (id[2] == 'L' || linkTracks) trackL = v;
    if (id[2] == 'R' || linkTracks) trackR = v;
  }
  if (!curBase.length()) { say(String(ROSTER[cur].label) + " isn't on the network", C_WARN); trackL = trackR = 0; return; }
  if (trackL != oldL || trackR != oldR) { ctlTracks(oldL, oldR); lastBeat = millis(); }
}

void tap(const String& id) {
  if (id == "back" && screen == SCR_CTRL) { ctlLeave(); screen = SCR_HOME; lastRefresh = 0; }
  else if (id == "back" && screen == SCR_ROBOT && cur >= 0) { screen = SCR_CTRL; sel = ""; moveOk = false; }
  else if (id == "back") { screen = SCR_HOME; sel = ""; moveOk = false; lastRefresh = 0; }
  else if (id.startsWith("go:")) {
    const int r = id.substring(3).toInt();
    screen = SCR_CTRL;
    say(String("Connecting to ") + ROSTER[r].label + "...", C_DIM, 3000);
    cur = r;
    curBase = "";
    draw();
    ctlEnter(r);
    if (ROSTER[r].kind != K_ARM && !curBase.length()) say(String(ROSTER[r].label) + " isn't on the network", C_WARN);
  }
  else if (id == "stop") { trackL = trackR = 0; heldPad = ""; if (curBase.length()) ctlStop(); }
  else if (id.startsWith("scheme:")) { if (curBase.length()) ctlScheme(id == "scheme:tank"); else tankUi = id == "scheme:tank"; }
  else if (id.startsWith("t:")) tankTap(id);
  else if (id.startsWith("x:")) ctlExtra(id.substring(2));
  else if (id == "rift") {
    sel = ROSTER[cur].name;
    screen = SCR_ROBOT;
    moveOk = false; actionPage = 0; actionCount = 0; statusCount = 0;
    note = "Asking...";
    draw();
    askRobot();
    lastRefresh = millis();
  }
  else if (id == "up")   { if (fleetPage > 0) fleetPage--; }
  else if (id == "down") { if ((fleetPage + 1) * ROWS < botCount) fleetPage++; }
  else if (id == "more") { actionPage++; }
  else if (id.startsWith("bot:")) {
    sel = bots[id.substring(4).toInt()].name;
    screen = SCR_ROBOT;
    moveOk = false;
    actionPage = 0;
    actionCount = 0;
    statusCount = 0;
    note = "Asking...";
    draw();
    askRobot();
    lastRefresh = millis();
  }
  else if (id.startsWith("do:")) {
    String a = id.substring(3);
    if (!harmless(a) && !moveOk) { pendingAction = a; screen = SCR_CONFIRM; }
    else doAction(a);
  }
  else if (id == "yes") { moveOk = true; screen = SCR_ROBOT; draw(); doAction(pendingAction); }
  else if (id == "no")  { screen = SCR_ROBOT; pendingAction = ""; }
  dirty = true;
}

// A press taps a button; a pad button (DRIVE's "p:...") drives while it's held and stops when the
// finger lifts. Resistive touch drops out for a moment now and then, so "lifted" means no touch for
// RELEASE_MS; a finger sliding off the pad button lets go too.
const unsigned long RELEASE_MS = 120;
void touchLoop() {
  static bool wasDown = false;
  static unsigned long lastDown = 0;
  int32_t x, y;
  bool down = lcd.getTouch(&x, &y);
  if (down) {
    lastDown = millis();
    if (!wasDown) {
      for (int i = btnCount - 1; i >= 0; i--) {
        const Btn& b = btns[i];
        if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
          if (screen == SCR_CTRL && b.id.startsWith("p:")) padPress(b.id);
          else tap(b.id);
          break;
        }
      }
    } else if (heldPad.length()) {
      for (int i = 0; i < btnCount; i++) {
        const Btn& b = btns[i];
        if (b.id == heldPad && !(x >= b.x - 10 && x < b.x + b.w + 10 && y >= b.y - 10 && y < b.y + b.h + 10)) padRelease();
      }
    }
    wasDown = true;
  } else if (wasDown && millis() - lastDown > RELEASE_MS) {
    wasDown = false;
    padRelease();
  }
}

void calibrate(bool force) {
  uint16_t cal[8];
  prefs.begin("rita", false);
  bool have = prefs.getBytes("touch", cal, sizeof(cal)) == sizeof(cal);
  if (have && !force) {
    lcd.setTouchCalibrate(cal);
  } else {
    lcd.fillScreen(C_BG);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_TEXT);
    lcd.setTextDatum(middle_center);
    lcd.drawString("Tap each corner as it lights up", W / 2, H / 2);
    lcd.setTextDatum(top_left);
    lcd.calibrateTouch(cal, C_ACCENT, C_BG, 24);
    prefs.putBytes("touch", cal, sizeof(cal));
  }
  prefs.end();
}

// ---------------------------------------------------------------------------
// Setup & loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  led(true, false, false);

  lcd.init();
  lcd.setRotation(1);   // landscape, 480 x 320
  lcd.setBrightness(200);
  lcd.fillScreen(C_BG);
  // boot screen: her avatar on the left (splash.h), her name on the right
  const int px = 24, py = (H - SPLASH_SIZE) / 2, tx = px + SPLASH_SIZE + (W - px - SPLASH_SIZE) / 2;
  lcd.drawJpg(SPLASH_JPG, sizeof(SPLASH_JPG), px, py);
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(C_ACCENT);
  lcd.setTextDatum(middle_center);
  lcd.drawString("RITA", tx, H / 2 - 26);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_DIM);
  lcd.drawString("Remote Interface", tx, H / 2 + 6);
  lcd.drawString("for Total Autonomy", tx, H / 2 + 24);
  lcd.setTextDatum(top_left);
  delay(5000);   // her splash: 5 s
  int32_t x, y;
  calibrate(lcd.getTouch(&x, &y));   // held at boot: calibrate again

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  startScan(false);
  dirty = true;
}

void loop() {
  netLoop();
  touchLoop();

  // HOME and FLEET: who's on the network; RIFT's ROBOT screen: what it says. DRIVE talks to its
  // robot itself (ctlLoop), so the fleet list isn't fetched while you drive.
  if (netState == NET_UP && millis() - lastRefresh > REFRESH_MS && screen != SCR_CONFIRM && screen != SCR_CTRL) {
    lastRefresh = millis();
    if (screen == SCR_HOME || screen == SCR_FLEET) fetchFleet();
    else askRobot();
    dirty = true;
  }
  if (screen == SCR_CTRL) ctlLoop();
  static bool toastShown = false;
  bool toastNow = toast.length() && millis() <= toastUntil;
  if (toastShown && !toastNow) dirty = true;   // take it away
  toastShown = toastNow;

  if (dirty) draw();
  delay(10);
}

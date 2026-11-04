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

  WIFI: the fleet's ladder. Her rung is under the controllers and above NORA:
    RIFT > DREAM > NINA > TINA > NORACTL > WHIPCTL > KIDA01CTL > KIDA00CTL > ARMCTL > RITA > NORA > WHIP ...
  She joins the highest network above her she can see. With none, she hosts "RITA" herself
  (192.168.4.1, channel 1, the fleet's fallback password, like every robot's own) with the fleet
  list on :5000, so NORA, WHIP, the KIDAs, ARM, MILA and NIKA join her and find each other. Every
  minute she looks again, and moves up (handing hers over) when a higher one is back. The hub is the network's
  host, at its gateway address on :5000; ask and do need RIFT (the others only list robots).
  She registers there as RITA (type "remote") every 10 s, so the fleet sees her too.

  Touch: calibrated on the first boot (tap the four corners), kept in flash. Hold the screen
  while she starts to calibrate again. Pins and panel options: display.h.

  RGB LED: green on RIFT's network, amber on another, red with no network.

  Libraries: LovyanGFX, ArduinoJson 7 (and the esp32 core's WiFi / HTTPClient / Preferences).
  Board: ESP32 Dev Module (esp32:esp32:esp32).
*/

#include <WiFi.h>
#include "fleet_beacon.h"
#include <HTTPClient.h>
#include <WebServer.h>   // the fleet list on :5000 while she hosts
#include "esp_wifi.h"
#include "esp_netif.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include "display.h"
#include "splash.h"   // her avatar for the boot screen (scripts/make_splash.py)
#include "avatars.h"  // the fleet's round avatars for HOME (scripts/make_avatars.py)
#include "dream_mirror.h"   // DREAM's clips from the SD card and her voice: optional (sdcard/)
#include "camera.h"         // the KIDAs' and DREAM's cameras (MJPEG, with their detections)
#include "web_page.h"       // her own web page (scripts/esp32/web, embedded by scripts/make_web.py)
#include <ArduinoOTA.h>     // new firmware over WiFi: flash.bat -Ota (no BOOT button)

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "No secrets.h: building with secrets.example.h's placeholders (copy it to secrets.h and fill it in)"
#include "secrets.example.h"
#endif

// A robot's own button on DRIVE. Up here, before any function: Arduino puts its function
// prototypes (extrasOf()'s returns one) in front of the first function in the file.
struct Extra { const char* label; const char* id; };
struct Stat { String v[5]; bool on = false; bool any = false; };   // a robot's STATS row: TEMP, HUMIDITY, DISTANCE, BATTERY, WIFI

LGFX lcd;
Preferences prefs;

// ---------------------------------------------------------------------------
// Look
// ---------------------------------------------------------------------------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
const uint16_t C_BG     = rgb(16, 21, 29);
const uint16_t C_PANEL  = rgb(25, 33, 46);
const uint16_t C_LINE   = rgb(46, 59, 78);
const uint16_t C_TEXT   = rgb(239, 233, 210);
const uint16_t C_DIM    = rgb(124, 122, 104);
const uint16_t C_ACCENT = rgb(224, 112, 42);   // her shirt and TV: colour_scheme.xml
const uint16_t C_OK     = rgb(80, 210, 130);
const uint16_t C_WARN   = rgb(240, 170, 60);
const uint16_t C_BAD    = rgb(235, 80, 90);

const int W = 480, H = 320, BAR = 36;

// ---------------------------------------------------------------------------
// The fleet's WiFi ladder (highest first) and the hub
// ---------------------------------------------------------------------------
const char* const LADDER[] = { FLEET_SSID, "DREAM", "NINA", "TINA",
                               "NORACTL", "WHIPCTL", "KIDA01CTL", "KIDA00CTL", "ARMCTL" };   // above her, highest first
const char* AP_SSID = "RITA";       // hers, when none of those is around: the robots below join it
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
bool hosting = false;                 // she's the network ("RITA") and its fleet list

String hubUrl() { return String("http://") + WiFi.gatewayIP().toString() + ":" + HUB_PORT; }
String netName() { return hosting ? String(AP_SSID) : rank >= 0 && rank < LADDER_N ? String(LADDER[rank]) : String(""); }

// ---- while she hosts: the fleet list on :5000 (RIFT's wire format: /ping, /register, /robots) ----
WebServer fleetServer(HUB_PORT);
struct Reg { String name, type, ip, caps; unsigned long seen; };
const int MAX_REG = 16;
Reg regs[MAX_REG];
int regCount = 0;
const unsigned long REG_TTL_MS = 20000;   // a robot that stops checking in drops off, like on RIFT

void regPrune() {
  for (int i = 0; i < regCount;) {
    if (millis() - regs[i].seen > REG_TTL_MS) regs[i] = regs[--regCount];
    else i++;
  }
}
void fleetRegister() {
  const String name = fleetServer.arg("name");
  if (!name.length()) { fleetServer.send(400, "text/plain", "missing name"); return; }
  int i = 0;
  while (i < regCount && regs[i].name != name) i++;
  if (i == regCount) { if (regCount == MAX_REG) { fleetServer.send(503, "text/plain", "full"); return; } regCount++; }
  regs[i] = { name, fleetServer.hasArg("type") ? fleetServer.arg("type") : String("unknown"),
              fleetServer.client().remoteIP().toString(), fleetServer.arg("capabilities"), millis() };
  fleetServer.send(200, "text/plain", "OK");
}
void fleetRobots() {
  regPrune();
  JsonDocument doc;
  doc["authority"] = "RITA";
  JsonArray list = doc["robots"].to<JsonArray>();
  for (int i = 0; i < regCount; i++) {
    JsonObject r = list.add<JsonObject>();
    r["name"] = regs[i].name; r["ip"] = regs[i].ip; r["type"] = regs[i].type;
    JsonArray caps = r["capabilities"].to<JsonArray>();
    int a = 0;
    while (a < (int)regs[i].caps.length()) {
      int b = regs[i].caps.indexOf(',', a);
      if (b < 0) b = regs[i].caps.length();
      if (b > a) caps.add(regs[i].caps.substring(a, b));
      a = b + 1;
    }
  }
  String out;
  serializeJson(doc, out);
  fleetServer.send(200, "application/json", out);
}

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
enum Screen { SCR_HOME, SCR_CTRL, SCR_FLEET, SCR_ROBOT, SCR_CONFIRM, SCR_STATS, SCR_STAT1, SCR_QR };
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

// The robots are he or she, never "it": WHIP and RIFT are he, everyone else she
bool isHe(const String& name) { return name == "WHIP" || name == "RIFT"; }
String heShe(const String& name) { return isHe(name) ? "he" : "she"; }
String himHer(const String& name) { return isHe(name) ? "him" : "her"; }
String hisHer(const String& name) { return isHe(name) ? "his" : "her"; }
String HisHer(const String& name) { return isHe(name) ? "His" : "Her"; }

// "turn_left" -> "TURN LEFT"
String label(String a) {
  a.replace("_", " ");
  a.toUpperCase();
  return a;
}

// ---------------------------------------------------------------------------
// LED
// ---------------------------------------------------------------------------
// Her RGB LED (active low), in colour: PWM on fixed channels 1-3 (the screen's backlight has 7).
// ledTick() decides what it shows, most important first:
//   updating over WiFi       white, flashing
//   driving a robot that doesn't answer   red, blinking
//   on DREAM's screen        her: orange pulse talking, teal listening, violet thinking, dim blue asleep
//   otherwise, the network   a slow breath: green on RIFT, amber on another, blue hosting, red none
bool ledReady = false, otaRunning = false;
void ledRgb(uint8_t r, uint8_t g, uint8_t b) {
  if (!ledReady) {
    ledcAttachChannel(PIN_LED_R, 5000, 8, 1);
    ledcAttachChannel(PIN_LED_G, 5000, 8, 2);
    ledcAttachChannel(PIN_LED_B, 5000, 8, 3);
    ledReady = true;
  }
  ledcWrite(PIN_LED_R, 255 - r);
  ledcWrite(PIN_LED_G, 255 - g);
  ledcWrite(PIN_LED_B, 255 - b);
}
void led(bool r, bool g, bool b) { ledRgb(r ? 255 : 0, g ? 255 : 0, b ? 255 : 0); }
void showNetOnLed() {}   // ledTick() keeps it up to date now
// 0..1, a smooth rise and fall every `periodMs`
float breath(unsigned long periodMs) { return 0.5f - 0.5f * cosf(2 * PI * (millis() % periodMs) / (float)periodMs); }

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
  if (hosting) return;   // she's the hub
  HTTPClient http;
  http.begin(hubUrl() + "/register");
  http.setTimeout(2000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.POST("name=RITA&type=remote&capabilities=remote");
  http.end();
}

void fetchFleet() {
  if (hosting) {   // she is the list
    regPrune();
    hubIsRift = false;
    botCount = 0;
    for (int i = 0; i < regCount && botCount < MAX_BOTS; i++) bots[botCount++] = { regs[i].name, regs[i].type, regs[i].ip, false };
    if (fleetPage * ROWS >= botCount) fleetPage = 0;
    return;
  }
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
  K_ARIA,     // drone, :5014  /status, /chirp?u=: to look and talk. Nothing here flies her: a resistive
              //   screen has no sticks, and a slipped finger is a crash. She flies from her app or a gamepad
  K_DREAM,    // DREAM's mirror, :5015 (plain HTTP, read-only)  /status, /speech/<id>: RITA plays the clip
              //   she's showing from the SD card, in step, and her voice on the speaker (dream_mirror.h)
};
struct Robot { const char* name; const char* label; Kind kind; uint16_t port; };
const Robot ROSTER[] = {
  { "WHIP", "WHIP", K_WHIP, 5005 },     { "NORA", "NORA", K_NORA, 5002 },
  { "MILA", "MILA", K_TANK, 5010 },     { "NIKA", "NIKA", K_TANK, 5013 },
  { "KIDA00", "KIDA-00", K_KIDA00, 5003 }, { "KIDA01", "KIDA-01", K_KIDA01, 5004 },
  { "IDA", "IDA", K_IDA, 5002 },        { "ARM", "ARM", K_ARM, 0 },
  { "ARIA", "ARIA", K_ARIA, 5014 },     { "DREAM", "DREAM", K_DREAM, 5015 },
};
const int ROSTER_N = sizeof(ROSTER) / sizeof(ROSTER[0]);

// Where a robot is, from the fleet list (IDA: through NORA). "" = not on the network.
String ipOf(const char* name) {
  for (int i = 0; i < botCount; i++) if (bots[i].name == name) return bots[i].ip;
  return "";
}
String robotIp(int r) {
  if (ROSTER[r].kind == K_ARM) return "";
  String ip = ipOf(ROSTER[r].kind == K_IDA ? "NORA" : ROSTER[r].name);
  // on DREAM's own WiFi she's the network's host, so she may not be in her own list
  if (!ip.length() && ROSTER[r].kind == K_DREAM && netState == NET_UP && !hosting && netName() == "DREAM")
    ip = WiFi.gatewayIP().toString();
  return ip;
}
// ---------------------------------------------------------------------------
// STATS: every robot's sensors from its own /status. Each names them its own way (temp, temperature,
// temp_c; hum, humidity; distance_cm, range_cm, distances_cm.front; battery_pct, battery_v ...), so
// the fields are recognised by name: whatever a robot reports, and anything it reports later, shows.
// ---------------------------------------------------------------------------
Stat stats[ROSTER_N];
const char* const STAT_NAMES[5] = { "TEMP", "HUMID", "DIST", "BATTERY", "WIFI" };
int statNext = 0, statSel = -1, statCount = 0, statPage = 0;
unsigned long lastStat = 0;
const int MAX_STAT_FIELDS = 48;
String statKey[MAX_STAT_FIELDS], statVal[MAX_STAT_FIELDS];

int hostedRssi(const String& ip);   // further down, with her hosting
bool hasStats(int r) { return ROSTER[r].kind != K_ARM && ROSTER[r].kind != K_IDA; }
String statusUrl(int r) {
  const String ip = robotIp(r);
  return ip.length() ? "http://" + ip + ":" + String(ROSTER[r].port) + "/status" : "";
}

// Which of the four a field is (-1: none), from its name
int statKind(String k) {
  k.toLowerCase();
  if (k == "wifi_rssi" || k == "rssi" || k == "wifi_dbm" || k == "signal_dbm") return 4;   // its own WiFi signal
  if (k.indexOf("hum") >= 0) return 1;
  if (k.indexOf("temp") >= 0 && k.indexOf("temperament") < 0 && !k.endsWith("temper") && k.indexOf("templ") < 0) return 0;   // not her mood's "temper"
  if (k.indexOf("dist") >= 0 || k.indexOf("range") >= 0 || k.indexOf("sonar") >= 0 || k.indexOf("ultra") >= 0) return 2;
  if (k.indexOf("bat") >= 0 || k.indexOf("volt") >= 0 || k.endsWith("soc")) return 3;
  return -1;
}
String statUnit(String k, int kind) {
  k.toLowerCase();
  if (kind == 0) return "C";
  if (kind == 1) return "%";
  if (kind == 2) return k.indexOf("_mm") >= 0 ? "mm" : k.indexOf("cm") >= 0 ? "cm" : k.endsWith("_m") ? "m" : "";
  if (kind == 4) return "dBm";
  if (kind == 3) return (k.indexOf("pct") >= 0 || k.indexOf("percent") >= 0) ? "%" : (k.endsWith("_v") || k.indexOf("volt") >= 0) ? "V" : "";
  return "";
}
String statText(JsonVariantConst v) {
  if (v.is<const char*>()) return String(v.as<const char*>());
  if (v.is<bool>()) return v.as<bool>() ? "yes" : "no";
  if (v.is<long>()) return String(v.as<long>());
  if (v.is<float>()) { const float f = v.as<float>(); return String(f, fabsf(f) < 10 ? 2 : 1); }
  String out; serializeJson(v, out); return out;
}
// Every leaf of the answer as "a.b: value"; the four summaries filled as they're met
void statFlatten(JsonVariantConst v, const String& key, Stat* st, bool keep) {
  if (v.is<JsonObjectConst>()) {
    for (JsonPairConst kv : v.as<JsonObjectConst>()) statFlatten(kv.value(), key.length() ? key + "." + kv.key().c_str() : String(kv.key().c_str()), st, keep);
    return;
  }
  if (v.is<JsonArrayConst>()) {
    int i = 0;
    for (JsonVariantConst e : v.as<JsonArrayConst>()) { if (i >= 6) break; statFlatten(e, key + "[" + i++ + "]", st, keep); }
    return;
  }
  const String val = statText(v);
  if (keep && statCount < MAX_STAT_FIELDS) { statKey[statCount] = key; statVal[statCount++] = val; }
  const int kind = statKind(key);
  if (kind < 0 || v.isNull() || !(v.is<float>() || v.is<long>())) return;
  const String u = statUnit(key, kind);
  String& slot = st->v[kind];
  if (kind == 2 && slot.length()) { if (slot.length() < 14) slot = slot.substring(0, slot.lastIndexOf(' ') > 0 ? slot.lastIndexOf(' ') : slot.length()) + "/" + val + (u.length() ? " " + u : ""); }   // front/back
  else if (!slot.length() || (kind == 0 && key.indexOf("cpu") < 0)) slot = val + (u.length() ? " " + u : "");
  st->any = true;
}
// Ask robot r; keep = also every field, for its own page
void statPoll(int r, bool keep) {
  Stat fresh;
  const String url = statusUrl(r);
  if (keep) statCount = 0;
  if (url.length() && netState == NET_UP) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(450);
    const int code = http.GET();
    if (code == 200) {
      JsonDocument doc;
      if (!deserializeJson(doc, http.getString())) { fresh.on = true; statFlatten(doc.as<JsonVariantConst>(), "", &fresh, keep); }
    }
    http.end();
  }
  const int heard = hostedRssi(robotIp(r));   // hosting: her own measure beats what it says
  if (heard) fresh.v[4] = String(heard) + " dBm";
  stats[r] = fresh;
}
// While she hosts: each robot's signal as her hotspot hears it (dBm), by its address; 0 = not on hers
int hostedRssi(const String& ip) {
  if (!hosting || !ip.length()) return 0;
  wifi_sta_list_t list;
  if (esp_wifi_ap_get_sta_list(&list) != ESP_OK || !list.num) return 0;
  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!ap) return 0;
  esp_netif_pair_mac_ip_t pairs[ESP_WIFI_MAX_CONN_NUM];
  for (int i = 0; i < list.num; i++) { memcpy(pairs[i].mac, list.sta[i].mac, 6); pairs[i].ip.addr = 0; }
  if (esp_netif_dhcps_get_clients_by_mac(ap, list.num, pairs) != ESP_OK) return 0;
  for (int i = 0; i < list.num; i++) {
    char buf[16];
    esp_ip4addr_ntoa(&pairs[i].ip, buf, sizeof(buf));
    if (ip == buf) return list.sta[i].rssi;
  }
  return 0;
}

// On STATS: one robot per tick, so RITA never waits on the whole fleet
void statsLoop() {
  if (millis() - lastStat < (screen == SCR_STAT1 ? 1500UL : 350UL)) return;
  lastStat = millis();
  if (screen == SCR_STAT1) { statPoll(statSel, true); return; }
  for (int tries = 0; tries < ROSTER_N; tries++) {
    statNext = (statNext + 1) % ROSTER_N;
    if (hasStats(statNext)) { statPoll(statNext, false); return; }
  }
}

// ---------------------------------------------------------------------------
// Her own web page: http://rita.local or her IP (192.168.4.1 while she hosts), port 80. It shows
// what's on her screen and what she's doing as it happens (/api/status, asked every second), her
// network and the fleet with its sensors. Read-only: driving stays on her touchscreen.
// ---------------------------------------------------------------------------
WebServer web(80);
unsigned long lastWebView = 0;            // the page asked lately: keep the fleet's stats fresh for it
bool webUp = false;
String screenName();
String doingNow();
String webUrl();

String robotPage(int r) {
  const String ip = robotIp(r);
  if (!ip.length()) return "";
  if (ROSTER[r].kind == K_DREAM) return "https://" + ip + ":5009/";
  return "http://" + ip + ":" + String(ROSTER[r].port) + "/";
}

void webStatus() {
  lastWebView = millis();
  JsonDocument d;
  d["name"] = "RITA";
  d["screen"] = screenName();
  d["doing"] = doingNow();
  d["toast"] = toast.length() && millis() <= toastUntil ? toast : String("");
  d["hosting"] = hosting;
  d["rift"] = hubIsRift;
  d["net"] = netState == NET_UP ? netName() : String("");
  d["ip"] = hosting ? String("192.168.4.1") : WiFi.localIP().toString();
  d["uptime_s"] = millis() / 1000;
  d["heap"] = ESP.getFreeHeap();
  d["sd"] = mirror::card;
  d["sound"] = mirror::sound;
  JsonArray rs = d["robots"].to<JsonArray>();
  for (int r = 0; r < ROSTER_N; r++) {
    JsonObject o = rs.add<JsonObject>();
    o["name"] = ROSTER[r].name;
    o["label"] = ROSTER[r].label;
    o["ip"] = robotIp(r);
    o["page"] = robotPage(r);
    o["answered"] = stats[r].on;
    o["temp"] = stats[r].v[0]; o["hum"] = stats[r].v[1]; o["dist"] = stats[r].v[2]; o["batt"] = stats[r].v[3]; o["wifi"] = stats[r].v[4];
  }
  JsonArray hs = d["hosted"].to<JsonArray>();
  if (hosting) for (int i = 0; i < regCount; i++) { JsonObject h = hs.add<JsonObject>(); h["name"] = regs[i].name; h["type"] = regs[i].type; h["ip"] = regs[i].ip; }
  String out;
  serializeJson(d, out);
  web.send(200, "application/json", out);
}

void webBegin() {
  web.on("/", []() { web.send_P(200, "text/html", WEB_INDEX); });
  web.on("/style.css", []() { web.send_P(200, "text/css", WEB_STYLE); });
  web.on("/app.js", []() { web.send_P(200, "application/javascript", WEB_APP); });
  web.on("/avatar.jpg", []() { web.send_P(200, "image/jpeg", (const char*)SPLASH_JPG, sizeof(SPLASH_JPG)); });
  web.on("/api/status", webStatus);
  web.begin();
  webUp = true;
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
bool camView = false;              // CAM: its camera on the screen (KIDA-00, KIDA-01, DREAM)
int servoAngle = 90;               // KIDA-00's camera servo, 0-180 (her /status camera_servo.angle)
bool servoHeld = false;            // a finger on its slider
int servoSent = -1;                // the angle last sent
unsigned long servoSentAt = 0;
struct Slider { int x, y, w, h; } servoSlider = { 0, 0, 0, 0 };
int camIdx = 0;                    // KIDA-01: cam 0 (ahead, YOLO) or cam 1
bool hasCam(int r) { const Kind k = ROSTER[r].kind; return k == K_KIDA00 || k == K_KIDA01 || k == K_DREAM; }
String camUrl() {
  switch (ROSTER[cur].kind) {
    case K_KIDA00: return curBase + "/video_feed";
    case K_KIDA01: return curBase + "/video_feed/" + String(camIdx) + "?w=320";
    case K_DREAM:  return curBase + "/camera.mjpeg?w=320";
    default:       return "";
  }
}
// Where the camera goes: beside the pad on the KIDAs, the whole video area on DREAM's screen
void camBox(int* x, int* y, int* w, int* h) {
  if (ROSTER[cur].kind == K_DREAM) { *x = 0; *y = BAR + 8; *w = W; *h = 270; }
  else { *x = 2; *y = BAR + 4; *w = 320; *h = 240; }
}

int robotGet(const String& path, uint16_t timeout = 600) {
  if (!curBase.length() || netState != NET_UP) return -1;
  HTTPClient http;
  http.begin(curBase + path);
  http.setTimeout(timeout);
  int code = http.GET();
  http.end();
  return code;
}
// A GET whose answer we want: a whole number out of its JSON, -1 if it isn't there
int robotGetInt(const String& path, const char* key, uint16_t timeout = 600) {
  if (!curBase.length() || netState != NET_UP) return -1;
  HTTPClient http;
  http.begin(curBase + path);
  http.setTimeout(timeout);
  lastCode = http.GET();
  const String body = lastCode == 200 ? http.getString() : "";
  http.end();
  const String find = String("\"") + key + "\":";
  const int at = body.indexOf(find);
  return at < 0 ? -1 : body.substring(at + find.length()).toInt();
}
// A word out of a JSON answer: "key":"word" or "key":12.34, "" if it isn't there
String jsonField(const String& body, const char* key) {
  const String find = String("\"") + key + "\":";
  int at = body.indexOf(find);
  if (at < 0) return "";
  at += find.length();
  if (body[at] == '"') return body.substring(at + 1, body.indexOf('"', at + 1));
  int end = at;
  while (end < (int)body.length() && body[end] != ',' && body[end] != '}') end++;
  return body.substring(at, end);
}
String dreamState, dreamClip;          // DREAM's last /status: what she's doing, the clip she shows
bool dreamAsleep = false, dreamClipHere = false;
void dreamPoll() {
  if (!curBase.length() || netState != NET_UP) return;
  HTTPClient http;
  http.begin(curBase + "/status");
  http.setTimeout(400);
  lastCode = http.GET();
  const String body = lastCode == 200 ? http.getString() : "";
  http.end();
  if (!body.length()) return;
  auto field = [&](const char* k) { String v = jsonField(body, k); return v == "null" ? String("") : v; };
  const String state = field("state"), clip = field("clip"), speech = field("speech");
  const bool asleep = field("sleeping") == "true";
  const bool here = mirror::show(clip, field("clip_ms").toInt(), field("loop") != "false");
  if (speech.length()) mirror::say(curBase, speech, field("speech_ms").toInt());
  else mirror::voiceId = "";            // she's quiet: the same line (a cached "Yes?") can play again
  if (state != dreamState || asleep != dreamAsleep || here != dreamClipHere || clip != dreamClip) dirty = true;
  dreamState = state; dreamAsleep = asleep; dreamClipHere = here; dreamClip = clip;
}

String ariaMode, ariaBattery, ariaNetwork;   // her last /status
void ariaPoll() {
  if (!curBase.length() || netState != NET_UP) return;
  HTTPClient http;
  http.begin(curBase + "/status");
  http.setTimeout(500);
  lastCode = http.GET();
  const String body = lastCode == 200 ? http.getString() : "";
  http.end();
  const String mode = jsonField(body, "mode"), volts = jsonField(body, "battery_v"), net = jsonField(body, "network");
  if (mode != ariaMode || volts != ariaBattery || net != ariaNetwork) dirty = true;
  ariaMode = mode;
  ariaBattery = volts;
  ariaNetwork = net;
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

// The STOP buttons: stopped in any mode. ctlStop() alone does nothing while a robot drives itself
// (AUTO, FOLLOW, STAY: each ignores its plain stop, and still answers OK), so out of that first.
void ctlHalt() {
  switch (ROSTER[cur].kind) {
    case K_WHIP: robotGet("/mode?m=web"); break;
    case K_NORA: robotGet("/modeManual"); break;
    case K_TANK: robotGet(tankUi ? "/mode?v=tank" : "/mode?v=wasd"); break;
    case K_KIDA00: robotPost("/do", "{\"action\":\"stop\"}"); break;
    case K_KIDA01: kida01("stop_anywhere"); break;
    default: break;   // IDA's stop already leaves OBSTACLE
  }
  ctlStop();
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
  if (camView) { cam::stop(); camView = false; }
  if (cur >= 0 && ROSTER[cur].kind == K_DREAM) { mirror::hush(); mirror::show("", 0, true); dreamState = dreamClip = ""; }
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
  const unsigned long pollMs = k == K_TANK ? 400 : k == K_DREAM ? 500 : 1500;
  if (now - lastPoll > pollMs && k != K_IDA) {
    lastPoll = now;
    if (k == K_ARIA) { ariaPoll(); return; }
    if (k == K_DREAM) { dreamPoll(); return; }
    if (k == K_KIDA00) {   // her /status, for the camera servo's angle too
      HTTPClient http;
      http.begin(curBase + "/status");
      http.setTimeout(500);
      const int code = http.GET();
      const String body = code == 200 ? http.getString() : "";
      http.end();
      if ((code > 0) != (lastCode > 0)) dirty = true;
      if (!heldPad.length() || code <= 0) lastCode = code;
      const int at = body.indexOf("\"camera_servo\"");
      if (at >= 0 && !servoHeld && millis() - servoSentAt > 1500) {
        const String a = jsonField(body.substring(at), "angle");
        if (a.length() && a.toInt() != servoAngle) { servoAngle = a.toInt(); servoSent = servoAngle; dirty = true; }
      }
      return;
    }
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
const Extra EX_WHIP[]   = { {"HELLO", "hello"}, {"DANCE", "dance"}, {"REST", "rest"}, {"STAY", "stay"}, {"AUTO", "auto"}, {"MANUAL", "manual"},
                            {"CRAB -", "crabin"}, {"CRAB +", "crabout"} };
const Extra EX_NORA[]   = { {"STAY", "stay"}, {"AUTO", "auto"}, {"MANUAL", "manual"}, {"UV ON", "uvon"}, {"UV OFF", "uvoff"},
                            {"MUSIC", "music"}, {"UNLOCK", "unlock"}, {"LOCK", "lock"} };   // 8 at most; next song: RIFT: ASK / DO
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
  if (ROSTER[cur].kind == K_ARIA) {   // a phrase: x is its number; she says no while she's flying
    const int got = robotGet("/chirp?u=" + x);
    say(got == 200 ? "ARIA: talking" : got == 409 ? "ARIA's flying" : "ARIA said no", got == 200 ? C_OK : C_WARN);
    return;
  }
  const Kind k = ROSTER[cur].kind;
  int code = -1;
  switch (k) {
    case K_WHIP:
      if (x == "stay") code = robotGet("/mode?m=stay");
      else if (x == "auto") code = robotGet("/mode?m=obstacle");
      else if (x == "manual") code = robotGet("/mode?m=web");
      else if (x == "crabin" || x == "crabout") {   // his crab offset (the slider on his web page), a degree a press
        const int deg = robotGetInt(String("/gait?crabstep=") + (x == "crabin" ? "-1" : "1"), "crabBack");
        say(deg < 0 ? String("WHIP has no crab offset") : "Crab offset " + String(deg) + " deg", deg < 0 ? C_WARN : C_OK);
        return;   // the degrees are the answer, not "said no"
      }
      else code = robotGet("/act?do=" + x);
      break;
    case K_NORA:
      if (x == "stay") code = robotGet("/modeStay");
      else if (x == "auto") code = robotGet("/modeAuto");
      else if (x == "manual") code = robotGet("/modeManual");
      else if (x == "uvon") code = robotGet("/uvOn");
      else if (x == "uvoff") code = robotGet("/uvOff");
      else if (x == "music") code = robotGet("/muPlay");
      else if (x == "unlock") code = robotGet("/motorlockOff?pw=" ROBOT_PIN);
      else if (x == "lock") code = robotGet("/motorlockOn");
      break;
    case K_TANK:
      if (x == "auto") code = robotGet("/mode?v=obstacle");
      else if (x == "manual") code = robotGet(tankUi ? "/mode?v=tank" : "/mode?v=wasd");
      else if (x == "speed") code = robotGet("/cmd?v=SPEEDCYCLE");
      else if (x == "chirp") code = robotGet("/chirp?u=0");   // a phrase is required: 0 is hello
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
void startHost() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);                  // the station half keeps looking for a higher network
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, NORA_PASSWORD, 1, 0, 10);   // the fleet's fallback password, channel 1 like the robots', up to 10
  static bool routes = false;
  if (!routes) {
    fleetServer.on("/ping", []() { fleetServer.send(200, "text/plain", "RITA alive"); });
    fleetServer.on("/register", HTTP_POST, fleetRegister);
    fleetServer.on("/robots", fleetRobots);
    routes = true;
  }
  fleetServer.begin();
  hosting = true;
  regCount = 0;
  netState = NET_UP;
  rank = LADDER_N;                         // below everything she can join: any of them is higher
  lastLook = millis();
  lastRefresh = 0;
  hubIsRift = false;
  say("No fleet network: hosting RITA", C_WARN, 3000);
  showNetOnLed();
}
void stopHost() {
  if (!hosting) return;
  fleetServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  hosting = false;
  regCount = 0;
}

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
  startHost();   // nothing (left) to join: she's the network now, and keeps looking every minute
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
      if (higher) { stopHost(); WiFi.disconnect(); tryRank = -1; joinNext(); }   // a higher one is back: move up
    } else if (any) {
      tryRank = -1;
      joinNext();
    } else {
      startHost();   // nobody above her: she hosts
    }
    // no dirty here: the screen only changes if what it shows did (viewSig, in loop)
  }

  if (netState == NET_JOINING) {
    if (WiFi.status() == WL_CONNECTED) {
      netState = NET_UP;
      rank = tryRank;
      lostSince = 0;
      lastLook = millis();
      lastRegister = 0;
      say(String("On ") + netName(), C_OK);
      lastRefresh = 0;
      showNetOnLed();
    } else if (millis() - tryStart > JOIN_MS) {
      WiFi.disconnect();
      joinNext();
    }
  } else if (netState == NET_UP && hosting) {
    fleetServer.handleClient();
    if (!lookScan && millis() - lastLook > LOOK_MS) { lastLook = millis(); startScan(true); }
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
  const int tx = back ? 104 : 10;
  if (back) button(4, 4, 88, BAR - 8, screen == SCR_CTRL ? "< HOME" : "< BACK", C_ACCENT, "back");
  lcd.setFont(&fonts::Font4);
  lcd.setTextColor(back ? C_TEXT : C_ACCENT);
  lcd.drawString(title, tx, 6);
  // RIFT: green while its hub answers, red when it doesn't (and while she hosts: that's why she does)
  const int dx = tx + lcd.textWidth(title.c_str()) + 12;
  lcd.fillCircle(dx, BAR / 2, 5, hubIsRift ? C_OK : C_BAD);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_DIM);
  lcd.drawString("RIFT", dx + 9, 11);
  String net;
  uint16_t c;
  if (netState == NET_UP) {
    net = hosting ? String("hosting RITA (") + regCount + ")" : netName();
    if (!hosting && (rank > 0 || !hubIsRift)) net = net + (hubIsRift ? " / RIFT" : " / list only");
    c = rank == 0 ? C_OK : C_WARN;
  }
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
  lcd.fillRoundRect(4, BAR + 6, 228, 114, 8, C_PANEL);   // ends above the pad
  lcd.setFont(&fonts::Font2);
  int y = BAR + 12;
  for (int i = 0; i < statusCount && y < BAR + 95; i++, y += 17) {
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
    lcd.drawString("Nothing " + heShe(sel) + " can do through RIFT yet.", 244, BAR + 14);
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
  lcd.drawString("Each tap moves " + himHer(sel) + " for 1 second.", W / 2, 124);
  lcd.drawString(HisHer(sel) + " own stops (walls, tilt, locks) still hold.", W / 2, 144);
  lcd.setTextDatum(top_left);
  button(80, H - 116, 140, 48, "YES", C_OK, "yes", true);
  button(W - 220, H - 116, 140, 48, "NO", C_BAD, "no");
}

// HOME: the roster in a 5 x 2 grid, each robot's round avatar with its name under it. On the
// network: a green dot on the avatar; not: dimmed, a red dot. (Online = in the fleet list.)
const int GRID_COLS = 5, CELL_W = W / GRID_COLS, CELL_H = (H - BAR) / 2;
void drawHome() {
  drawBar("RITA", false);
  button(128, 4, 84, BAR - 8, "STATS", C_ACCENT, "stats");
  button(218, 4, 64, BAR - 8, "WEB", C_ACCENT, "qr");
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
    lcd.drawCircle(cx, cy, AVATAR_SIZE / 2 + 2, C_LINE);
    lcd.drawCircle(cx, cy, AVATAR_SIZE / 2 + 3, C_LINE);
    // online dot, like a chatroom: bottom right on the ring, green = on the network, red = not
    const int dr = AVATAR_SIZE / 2 + 2, dx = cx + dr * 707 / 1000, dy = cy + dr * 707 / 1000;
    lcd.fillCircle(dx, dy, 9, C_BG);                 // a gap round the dot so it stands out
    lcd.fillCircle(dx, dy, 7, on ? C_OK : C_BAD);
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

// KIDA-00's camera servo: a slider, 0 (her left) .. 180. Drag it and the camera follows.
void drawServoSlider(int x, int y, int w, int h) {
  servoSlider = { x, y, w, h };
  lcd.fillRoundRect(x, y, w, h, 6, C_PANEL);
  lcd.drawRoundRect(x, y, w, h, 6, C_LINE);
  const int tx = x + 58, tw = w - 70, ty = y + h / 2;
  lcd.drawFastHLine(tx, ty, tw, C_DIM);
  lcd.drawFastVLine(tx + tw / 2, ty - 5, 11, C_DIM);   // centre
  const int kx = tx + (long)servoAngle * tw / 180;
  lcd.fillRoundRect(kx - 7, y + 4, 14, h - 8, 4, servoHeld ? C_ACCENT : C_TEXT);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_TEXT);
  lcd.setTextDatum(middle_left);
  lcd.drawString("CAM " + String(servoAngle), x + 6, ty);
  lcd.setTextDatum(top_left);
  addBtn(x, y, w, h, "sl:servo");
}
int sliderAngle(int touchX) {
  const int tx = servoSlider.x + 58, tw = servoSlider.w - 70;
  return constrain((int)((long)(touchX - tx) * 180 / tw), 0, 180);
}
// Sent at most every 150 ms while dragging, and once more on letting go
void servoSend(bool final) {
  if (!curBase.length() || servoAngle == servoSent || (!final && millis() - servoSentAt < 150)) return;
  servoSent = servoAngle;
  servoSentAt = millis();
  robotPost("/camera_servo", "{\"action\":\"angle\",\"angle\":" + String(servoAngle) + "}", 400);
}

// Under the camera, until its first frame: what's happening
void drawCamNote() {
  int x, y, w, h;
  camBox(&x, &y, &w, &h);
  lcd.drawRect(x, y, w, h, C_LINE);
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_DIM);
  lcd.setTextDatum(middle_center);
  lcd.drawString(curBase.length() ? (cam::status.length() ? cam::status : String("connecting...")) : String("not on the network"), x + w / 2, y + h / 2);
  lcd.setTextDatum(top_left);
}

// A KIDA's camera with a small pad beside it: drive while you watch
void drawCamCtrl() {
  lcd.fillRect(216, 2, W - 218, BAR - 4, C_PANEL);
  button(W - 88, 4, 84, BAR - 8, "CAM", C_ACCENT, "cam", true);
  if (ROSTER[cur].kind == K_KIDA01) button(W - 176, 4, 84, BAR - 8, camIdx ? "CAM 1" : "CAM 0", C_ACCENT, "camsw");
  drawCamNote();
  const int px = 330, py = BAR + 10, b = 46, g = 4;   // a 3 x 3 pad, 150 wide
  padButton(px + b + g, py, b, b, "^", "p:f");
  padButton(px, py + b + g, b, b, "<", "p:l");
  button(px + b + g, py + b + g, b, b, "STOP", C_BAD, "stop", true);
  padButton(px + 2 * (b + g), py + b + g, b, b, ">", "p:r");
  padButton(px + b + g, py + 2 * (b + g), b, b, "v", "p:b");
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(C_DIM);
  lcd.drawString(cam::fps > 0.5f ? String(cam::fps, 1) + " fps" : String(""), px, py + 3 * (b + g) + 4);
  lcd.drawString(ROSTER[cur].kind == K_KIDA00 ? "her HUD: what she sees" : camIdx ? "cam 1" : "cam 0: YOLO when on", px, py + 3 * (b + g) + 22);
  if (ROSTER[cur].kind == K_KIDA00) drawServoSlider(2, BAR + 248, 320, 32);   // her camera is on a servo
}

void drawCtrl() {
  const Robot& R = ROSTER[cur];
  drawBar(R.label, true);
  if (camView && R.kind != K_DREAM) { drawCamCtrl(); return; }
  const bool tank = tracked(cur) && tankUi;
  if (tracked(cur)) {   // WASD / TANK, where the network name usually is
    lcd.fillRect(250, 2, W - 252, BAR - 4, C_PANEL);
    button(W - 176, 4, 84, BAR - 8, "WASD", C_ACCENT, "scheme:wasd", !tankUi);
    button(W - 88, 4, 84, BAR - 8, "TANK", C_ACCENT, "scheme:tank", tankUi);
  }
  if (hasCam(cur) && R.kind != K_DREAM) button(W - 264, 4, 84, BAR - 8, "CAM", C_ACCENT, "cam", false);   // its camera

  if (R.kind == K_ARM) {
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DIM);
    lcd.drawString("ARM has no WiFi: she's driven from her PC,", 14, BAR + 20);
    lcd.drawString("over her USB port (ARM-Robot-v01/scripts).", 14, BAR + 40);
    return;
  }

  if (R.kind == K_DREAM) {   // the clip she's showing (from the SD card, in step), or her avatar
    lcd.fillRect(250, 2, W - 252, BAR - 4, C_PANEL);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(dreamAsleep ? C_DIM : C_ACCENT);
    lcd.setTextDatum(middle_right);
    const String doing = dreamAsleep ? "asleep" : dreamState.length() ? dreamState : String("--");
    lcd.drawString(doing, W - 172, BAR / 2);
    lcd.setTextDatum(top_left);
    button(W - 164, 4, 64, BAR - 8, "CAM", C_ACCENT, "cam", camView);
    button(W - 96, 4, 92, BAR - 8, mirror::sound ? "SOUND ON" : "SOUND OFF", C_ACCENT, "sound", mirror::sound);
    if (camView) { drawCamNote(); return; }           // loop() draws her camera under the bar
    if (mirror::has()) { mirror::redraw(); return; }   // loop() draws her frames under the bar
    size_t len = 0;
    const uint8_t* jpg = avatarOf("DREAM", &len);
    const int cx = W / 2, top = BAR + 30;
    if (jpg) lcd.drawJpg(jpg, len, cx - AVATAR_SIZE / 2, top);
    lcd.drawCircle(cx, top + AVATAR_SIZE / 2, AVATAR_SIZE / 2 + 2, C_LINE);
    lcd.setFont(&fonts::Font4);
    lcd.setTextColor(C_TEXT);
    lcd.setTextDatum(top_center);
    lcd.drawString(curBase.length() ? (dreamAsleep ? "She's asleep" : "DREAM is " + doing) : String("DREAM isn't on the network"), cx, top + AVATAR_SIZE + 16);
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_DIM);
    lcd.drawString(!mirror::card ? "No SD card: copy RITA's sdcard folder onto one to see her clips."
                   : dreamClip.length() ? "This clip (" + dreamClip + ") isn't on the SD card yet."
                   : "Waiting for her screen...", cx, top + AVATAR_SIZE + 52);
    lcd.setTextDatum(top_left);
    return;
  }

  if (R.kind == K_ARIA) {
    lcd.setFont(&fonts::Font2);
    lcd.setTextColor(C_TEXT);
    lcd.drawString("State:    " + (ariaMode.length() ? ariaMode : String("--")), 14, BAR + 14);
    lcd.drawString("Battery:  " + (ariaBattery.length() ? ariaBattery + " V" : String("--")), 14, BAR + 36);
    lcd.drawString("Network:  " + (ariaNetwork.length() ? ariaNetwork : String("--")), 14, BAR + 58);
    lcd.setTextColor(C_DIM);
    lcd.drawString("She flies from her app or a gamepad, not from here.", 14, BAR + 84);
    static const char* const WORDS[] = { "HELLO", "HAPPY", "PLAY", "BYE" };
    static const char* const IDS[] = { "x:0", "x:2", "x:5", "x:6" };
    for (int i = 0; i < 4; i++) button(14 + i * 112, BAR + 116, 104, 60, WORDS[i], C_ACCENT, IDS[i]);
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
    } else if (k == K_KIDA00) {   // her camera's servo
      drawServoSlider(bx, ey, 3 * bw + 2 * g, 40);
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

// Everything the screens show that changes on its own (network, fleet list, a robot's answer).
// draw() wipes the whole screen, which shows as a black blink, so the periodic scans and refreshes
// only redraw when this changes - not every few seconds.
String viewSig() {
  String s = String(netState) + "|" + rank + "|" + tryRank + "|" + hubIsRift + "|" + botCount + "|" + hosting + regCount;
  for (int i = 0; i < botCount; i++) s += "|" + bots[i].name + "," + bots[i].type + "," + bots[i].ip + "," + bots[i].busy;
  if (screen == SCR_CTRL && cur >= 0 && ROSTER[cur].kind == K_DREAM) s += "|" + dreamState + dreamAsleep + dreamClipHere + dreamClip;
  if (screen == SCR_QR) s += "|qr" + webUrl();
  if (screen == SCR_STATS || screen == SCR_STAT1) {
    for (int r = 0; r < ROSTER_N; r++) s += "|" + String(stats[r].on) + stats[r].v[0] + stats[r].v[1] + stats[r].v[2] + stats[r].v[3] + stats[r].v[4];
    if (screen == SCR_STAT1) { s += statCount; for (int i = 0; i < statCount; i++) s += statVal[i]; }
  }
  if (screen == SCR_CTRL && camView) s += "|cam" + String(camIdx) + (cam::status.length() ? cam::status : String("")) + (int)(cam::fps + 0.5f);
  if (screen == SCR_ROBOT || screen == SCR_CONFIRM) {
    s += "|" + note;
    for (int i = 0; i < statusCount; i++) s += "|" + statusLines[i];
    for (int i = 0; i < actionCount; i++) s += "|" + actions[i];
  }
  return s;
}

void drawStats() {
  drawBar("STATS", true);
  const int rowH = 30, y0 = BAR + 6;
  lcd.setFont(&fonts::Font2);
  int row = 0;
  const int cx0 = 112, cw = 73;   // five columns after the name
  for (int i = 0; i < 5; i++) {   // the column heads
    lcd.setTextColor(C_DIM);
    lcd.setTextDatum(top_center);
    lcd.drawString(STAT_NAMES[i], cx0 + i * cw + cw / 2, y0);
  }
  lcd.setTextDatum(top_left);
  for (int r = 0; r < ROSTER_N; r++) {
    if (!hasStats(r)) continue;
    const int y = y0 + 18 + row++ * rowH;
    const Stat& st = stats[r];
    const bool on = st.on;
    lcd.fillRoundRect(4, y, W - 8, rowH - 3, 6, C_PANEL);
    lcd.fillCircle(16, y + rowH / 2 - 1, 5, on ? C_OK : (robotIp(r).length() ? C_WARN : C_BAD));
    lcd.setTextColor(on ? C_TEXT : C_DIM);
    lcd.drawString(ROSTER[r].label, 28, y + 6);
    lcd.setTextDatum(top_center);
    for (int i = 0; i < 5; i++) {
      const int dbm = i == 4 ? st.v[4].toInt() : 0;   // WIFI: green strong, amber fair, red weak
      lcd.setTextColor(!st.v[i].length() ? C_LINE : dbm == 0 ? C_TEXT : dbm > -67 ? C_OK : dbm > -78 ? C_WARN : C_BAD);
      lcd.drawString(on ? (st.v[i].length() ? st.v[i] : String("--")) : String(""), cx0 + i * cw + cw / 2, y + 6);
    }
    lcd.setTextDatum(top_left);
    if (!on) { lcd.setTextColor(C_DIM); lcd.drawString(robotIp(r).length() ? "no answer" : "offline", cx0 + 10, y + 6); }
    addBtn(4, y, W - 8, rowH - 3, "st:" + String(r));
  }
}

void drawStat1() {
  drawBar(String(ROSTER[statSel].label) + " stats", true);
  lcd.setFont(&fonts::Font2);
  if (!stats[statSel].on) {
    lcd.setTextColor(C_DIM);
    lcd.drawString(robotIp(statSel).length() ? "Asking..." : "Not on the network.", 14, BAR + 14);
    return;
  }
  const String page = robotPage(statSel);   // its own site: a QR to it, on the right
  if (page.length()) {
    const int q = 150, qx = W - q - 14, qy = BAR + 12;
    lcd.fillRect(qx - 4, qy - 4, q + 8, q + 8, TFT_WHITE);
    lcd.qrcode(page.c_str(), qx, qy, q);
    lcd.setTextColor(C_DIM);
    lcd.setTextDatum(top_center);
    lcd.drawString(hisHer(ROSTER[statSel].name) + (ROSTER[statSel].kind == K_DREAM ? " dashboard" : " page"), qx + q / 2, qy + q + 10);
    lcd.drawString(page.substring(page.indexOf("//") + 2), qx + q / 2, qy + q + 28);
    lcd.setTextDatum(top_left);
  }
  const int PER = 14;
  if (statPage * PER >= statCount) statPage = 0;
  for (int i = 0; i < PER && statPage * PER + i < statCount; i++) {
    const int k = statPage * PER + i, x = 8, y = BAR + 8 + i * 18;
    String key = statKey[k];
    if (key.length() > 18) key = ".." + key.substring(key.length() - 16);
    lcd.setTextColor(statKind(statKey[k]) >= 0 ? C_ACCENT : C_DIM);
    lcd.drawString(key, x, y);
    lcd.setTextColor(C_TEXT);
    lcd.drawString(statVal[k].substring(0, 14), x + 130, y);
  }
  if (statCount > PER) button(8, H - 34, 92, 30, "MORE", C_ACCENT, "stmore");
}

String screenName() {
  switch (screen) {
    case SCR_HOME:    return "HOME: the fleet";
    case SCR_STATS:   return "STATS: the fleet's sensors";
    case SCR_STAT1:   return "STATS: one robot";
    case SCR_QR:      return "WEB: the QR code to this page";
    case SCR_FLEET:   return "The fleet list";
    case SCR_ROBOT:   return "RIFT: ask and do";
    case SCR_CONFIRM: return "Asking: let RITA move " + himHer(sel) + "?";
    case SCR_CTRL:    return cur >= 0 && ROSTER[cur].kind == K_DREAM ? "DREAM" : "Driving";
  }
  return "";
}
String doingNow() {
  if (screen == SCR_STAT1 && statSel >= 0) return String(ROSTER[statSel].label) + "'s sensors";
  if ((screen == SCR_ROBOT || screen == SCR_CONFIRM) && sel.length()) return sel;
  if (screen != SCR_CTRL || cur < 0) return "";
  const Robot& R = ROSTER[cur];
  if (R.kind == K_DREAM) {
    String s = camView ? "DREAM's camera" : dreamClipHere ? "DREAM's screen, in step (" + dreamClip + ")" : "DREAM";
    if (dreamState.length()) s += dreamAsleep ? ": asleep" : ": " + dreamState;
    return s;
  }
  String s = String(R.label);
  if (!curBase.length()) return s + " (not on the network)";
  if (camView) s += String(", camera ") + (R.kind == K_KIDA01 ? String(camIdx) : String("on"));
  if (tracked(cur)) s += tankUi ? ", TANK" : ", WASD";
  if (heldPad.length()) s += ", driving: " + heldPad.substring(2);
  else if (trackL || trackR) s += String(", tracks L ") + trackL + " R " + trackR;
  else s += ", stopped";
  return s;
}

// WEB: a QR code to her own page; while she hosts, a second one that joins her WiFi first
String webUrl() { return "http://" + (hosting ? String("192.168.4.1") : WiFi.localIP().toString()) + "/"; }
void drawQr() {
  drawBar("Her web page", true);
  lcd.setFont(&fonts::Font2);
  lcd.setTextDatum(top_center);
  if (netState != NET_UP) {
    lcd.setTextColor(C_DIM);
    lcd.drawString("She isn't on a network yet.", W / 2, H / 2 - 8);
    lcd.setTextDatum(top_left);
    return;
  }
  const int size = 196, y = BAR + 18;
  if (hosting) {   // a phone has to be on her WiFi before the page opens
    const String wifi = String("WIFI:T:WPA;S:") + AP_SSID + ";P:" + NORA_PASSWORD + ";;";
    lcd.fillRect(26, y, size + 8, size + 8, TFT_WHITE);
    lcd.qrcode(wifi.c_str(), 30, y + 4, size);
    lcd.fillRect(W - 34 - size, y, size + 8, size + 8, TFT_WHITE);
    lcd.qrcode(webUrl().c_str(), W - 30 - size, y + 4, size);
    lcd.setTextColor(C_TEXT);
    lcd.drawString("1. join her WiFi \"RITA\"", 30 + size / 2, y + size + 16);
    lcd.drawString("2. open " + webUrl(), W - 30 - size / 2, y + size + 16);
  } else {
    lcd.fillRect(W / 2 - size / 2 - 4, y, size + 8, size + 8, TFT_WHITE);
    lcd.qrcode(webUrl().c_str(), W / 2 - size / 2, y + 4, size);
    lcd.setTextColor(C_TEXT);
    lcd.drawString(webUrl() + "  (or rita.local)", W / 2, y + size + 16);
    lcd.setTextColor(C_DIM);
    lcd.drawString("On " + netName() + ": the phone has to be on it too", W / 2, y + size + 34);
  }
  lcd.setTextDatum(top_left);
}

void ledTick() {
  static unsigned long last = 0;
  if (millis() - last < 25) return;
  last = millis();
  auto scaled = [](uint8_t r, uint8_t g, uint8_t b, float k) { ledRgb(r * k, g * k, b * k); };
  if (otaRunning) { const bool on = (millis() / 120) % 2; scaled(255, 255, 255, on ? 1.0f : 0.1f); return; }
  const bool driving = screen == SCR_CTRL && cur >= 0;
  if (driving && curBase.length() && lastCode <= 0 && ROSTER[cur].kind != K_ARM) {   // it isn't answering
    scaled(255, 0, 0, (millis() / 250) % 2 ? 1.0f : 0.05f);
    return;
  }
  if (driving && ROSTER[cur].kind == K_DREAM && dreamState.length()) {
    if (dreamAsleep)                     scaled(20, 40, 255, 0.05f + 0.2f * breath(5000));
    else if (dreamState == "talking")    scaled(255, 90, 10, 0.3f + 0.7f * breath(500));
    else if (dreamState == "listening")  scaled(20, 230, 200, 0.8f);
    else if (dreamState == "thinking")   scaled(170, 40, 255, 0.2f + 0.6f * breath(1600));
    else                                 scaled(255, 110, 40, 0.15f + 0.25f * breath(3000));
    return;
  }
  const float k = 0.12f + 0.45f * breath(3500);   // the network, breathing
  if (netState != NET_UP)  scaled(255, 0, 0, k);
  else if (hosting)        scaled(0, 60, 255, k);
  else if (rank == 0)      scaled(0, 255, 40, k);
  else                     scaled(255, 150, 0, k);
}

void draw() {
  btnCount = 0;
  lcd.startWrite();
  lcd.fillScreen(C_BG);
  if (screen == SCR_HOME) drawHome();
  else if (screen == SCR_STATS) drawStats();
  else if (screen == SCR_QR) drawQr();
  else if (screen == SCR_STAT1) drawStat1();
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
  if (id == "back" && screen == SCR_STAT1) { screen = SCR_STATS; statSel = -1; }
  else if (id == "back" && screen == SCR_STATS) { screen = SCR_HOME; lastRefresh = 0; }
  else if (id == "back" && screen == SCR_QR) { screen = SCR_HOME; lastRefresh = 0; }
  else if (id == "qr") { screen = SCR_QR; }
  else if (id == "stats") { screen = SCR_STATS; lastStat = 0; }
  else if (id.startsWith("st:")) { statSel = id.substring(3).toInt(); statPage = 0; statCount = 0; screen = SCR_STAT1; draw(); statPoll(statSel, true); lastStat = millis(); }
  else if (id == "stmore") { statPage++; }
  else if (id == "back" && screen == SCR_CTRL) { ctlLeave(); screen = SCR_HOME; lastRefresh = 0; }
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
  else if (id == "cam") {     // its camera on the screen, or back
    camView = !camView;
    if (camView && curBase.length()) cam::start(camUrl()); else cam::stop();
    if (!camView && ROSTER[cur].kind == K_DREAM) mirror::redraw();
  }
  else if (id == "camsw") { camIdx ^= 1; if (camView && curBase.length()) cam::start(camUrl()); }
  else if (id == "sound") {   // DREAM's voice on the speaker (or the headphones)
    mirror::sound = !mirror::sound;
    if (!mirror::sound) mirror::hush();
    prefs.begin("rita", false); prefs.putBool("sound", mirror::sound); prefs.end();
  }
  else if (id == "stop" || id == "t:stop") { trackL = trackR = 0; heldPad = ""; if (curBase.length()) ctlHalt(); }
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
          else if (b.id == "sl:servo") { servoHeld = true; servoAngle = sliderAngle(x); servoSend(false); dirty = true; }
          else tap(b.id);
          break;
        }
      }
    } else if (servoHeld) {        // dragging KIDA-00's camera servo
      const int a = sliderAngle(x);
      if (a != servoAngle) { servoAngle = a; servoSend(false); drawServoSlider(servoSlider.x, servoSlider.y, servoSlider.w, servoSlider.h); }
    } else if (heldPad.length()) {
      for (int i = 0; i < btnCount; i++) {
        const Btn& b = btns[i];
        if (b.id == heldPad && !(x >= b.x - 10 && x < b.x + b.w + 10 && y >= b.y - 10 && y < b.y + b.h + 10)) padRelease();
      }
    }
    wasDown = true;
  } else if (wasDown && millis() - lastDown > RELEASE_MS) {
    wasDown = false;
    if (servoHeld) { servoHeld = false; servoSend(true); dirty = true; }
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
  mirror::begin();                    // an SD card with DREAM's clips, if there is one
  mirror::voiceBegin();               // her voice on the speaker connector
  prefs.begin("rita", true); mirror::sound = prefs.getBool("sound", true); prefs.end();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  startScan(false);
  dirty = true;
}

void loop() {
  netLoop();
  static bool otaUp = false;          // new firmware over WiFi (flash.bat -Ota), once she has a network
  if (!otaUp && netState == NET_UP) {
    ArduinoOTA.setHostname("rita");
    ArduinoOTA.setPassword(ROBOT_PIN);
    ArduinoOTA.onStart([]() { otaRunning = true; ledRgb(255, 255, 255); cam::stop(); mirror::hush(); lcd.fillScreen(C_BG); lcd.setTextColor(C_ACCENT); lcd.setFont(&fonts::Font4);
                              lcd.setTextDatum(middle_center); lcd.drawString("Updating over WiFi...", W / 2, H / 2); });
    ArduinoOTA.begin();
    otaUp = true;
  }
  if (otaUp) ArduinoOTA.handle();
  if (!webUp && netState == NET_UP) webBegin();   // her web page, once she has a network
  if (webUp) web.handleClient();
  ledTick();
  if (screen != SCR_STATS && screen != SCR_STAT1 && millis() - lastWebView < 15000 && millis() - lastStat > 700) {
    lastStat = millis();   // the page is open: its sensors stay fresh, one robot at a time
    for (int tries = 0; tries < ROSTER_N; tries++) {
      statNext = (statNext + 1) % ROSTER_N;
      if (hasStats(statNext)) { statPoll(statNext, false); break; }
    }
  }
  fleetBeaconLoop();
  touchLoop();

  // HOME and FLEET: who's on the network; RIFT's ROBOT screen: what it says. DRIVE talks to its
  // robot itself (ctlLoop), so the fleet list isn't fetched while you drive.
  if (netState == NET_UP && millis() - lastRefresh > REFRESH_MS && screen != SCR_CONFIRM && screen != SCR_CTRL && screen != SCR_STAT1) {
    lastRefresh = millis();
    if (screen == SCR_HOME || screen == SCR_FLEET || screen == SCR_STATS) fetchFleet();
    else askRobot();
  }
  static String shownSig;
  static unsigned long lastSigCheck = 0;
  if (dirty || millis() - lastSigCheck > 250) {   // redraw only when what she shows changed
    lastSigCheck = millis();
    String s = viewSig();
    if (s != shownSig) { shownSig = s; dirty = true; }
  }
  if (screen == SCR_CTRL) ctlLoop();
  static unsigned long lastRift = 0;
  if (millis() - lastRift > 5000 && screen != SCR_HOME && screen != SCR_FLEET && screen != SCR_STATS) {
    lastRift = millis();
    bool rift = false;
    if (netState == NET_UP && !hosting) {
      HTTPClient http;
      http.begin(hubUrl() + "/clock");
      http.setTimeout(400);
      rift = http.GET() == 200;
      http.end();
    }
    if (rift != hubIsRift) { hubIsRift = rift; dirty = true; }
  }
  if (screen == SCR_STATS || screen == SCR_STAT1) statsLoop();
  static bool toastShown = false;
  bool toastNow = toast.length() && millis() <= toastUntil;
  if (toastShown && !toastNow) dirty = true;   // take it away
  toastShown = toastNow;

  if (dirty) draw();
  const bool video = screen == SCR_CTRL && cur >= 0 && (ROSTER[cur].kind == K_DREAM || camView);
  if (screen == SCR_CTRL && camView) { int x, y, w, h; camBox(&x, &y, &w, &h); cam::poll(lcd, x, y, w, h); }
  else if (video) mirror::frame(lcd, 0, BAR + 8);   // DREAM's clip, 480 x 270
  delay(video ? 1 : 10);                       // her frames come as fast as the card and decoder allow
}

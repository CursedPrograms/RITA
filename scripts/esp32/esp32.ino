/*
  RITA - Remote Interface for Total Autonomy
  ---------------------------------------------------------------------------
  RIFT in your hand: a 3.5" ESP32 "Cheap Yellow Display" (ESP32-3248S035R, 480x320, resistive
  touch) that shows the DREAM Robotics fleet and talks to it through RIFT.

    FLEET   every robot online (RIFT's /fleet, or the /robots list of whoever hosts the network)
    ROBOT   tap one: what it says about itself (RIFT's ask: mode, moving, music, distances) and a
            button for everything it can do (RIFT's do). Moving actions ask "let RITA move it?"
            first, once per visit, then run as 1 s timed moves (RIFT caps them at 3 s); a drive
            pad appears for robots that drive. Locks and PINs stay the robots' own: RIFT never
            passes on anything that unlocks one.

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

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "No secrets.h: building with secrets.example.h's placeholders (copy it to secrets.h and fill it in)"
#include "secrets.example.h"
#endif

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
enum Screen { SCR_FLEET, SCR_ROBOT, SCR_CONFIRM };
Screen screen = SCR_FLEET;
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
    button(4, 4, 88, BAR - 8, "< FLEET", C_ACCENT, "back");
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

void draw() {
  btnCount = 0;
  lcd.startWrite();
  lcd.fillScreen(C_BG);
  if (screen == SCR_FLEET) drawFleet();
  else drawRobot();
  if (screen == SCR_CONFIRM) { btnCount = 0; drawConfirm(); }
  drawToast();
  lcd.endWrite();
  dirty = false;
}

// ---------------------------------------------------------------------------
// Touch
// ---------------------------------------------------------------------------
void tap(const String& id) {
  if (id == "back") { screen = SCR_FLEET; sel = ""; moveOk = false; lastRefresh = 0; }
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

void touchLoop() {
  static bool wasDown = false;
  int32_t x, y;
  bool down = lcd.getTouch(&x, &y);
  if (down && !wasDown) {
    for (int i = btnCount - 1; i >= 0; i--) {
      const Btn& b = btns[i];
      if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) { tap(b.id); break; }
    }
  }
  wasDown = down;
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
  delay(1800);   // long enough to see her
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

  if (netState == NET_UP && millis() - lastRefresh > REFRESH_MS && screen != SCR_CONFIRM) {
    lastRefresh = millis();
    if (screen == SCR_FLEET) fetchFleet();
    else askRobot();
    dirty = true;
  }
  static bool toastShown = false;
  bool toastNow = toast.length() && millis() <= toastUntil;
  if (toastShown && !toastNow) dirty = true;   // take it away
  toastShown = toastNow;

  if (dirty) draw();
  delay(10);
}

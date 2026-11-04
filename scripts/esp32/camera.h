// camera.h - a robot's camera on RITA's screen: an MJPEG stream (multipart/x-mixed-replace, a JPEG
// after a JPEG), read a bit at a time so her loop never waits on it, newest whole frame drawn fitted
// to a box. Where the robot draws its detections on its frames, they come along:
//   KIDA-00  :5003/video_feed           her HUD's camera panel, 320 x 240, with what she sees marked
//   KIDA-01  :5004/video_feed/0|1?w=320 cam 0 (YOLO boxes while her inference is on), cam 1
//   DREAM    :5015/camera.mjpeg?w=320   her camera with her detections (people, objects, faces)
#pragma once
#include <HTTPClient.h>

namespace cam {

HTTPClient http;
WiFiClient* in = nullptr;
uint8_t* buf = nullptr;
const size_t SIZE = 64 * 1024;      // a 320 x 240 frame is 7-20 KB
size_t have = 0;
String url;
bool on = false;
unsigned long lastTry = 0, lastByte = 0, lastFrame = 0;
float fps = 0;
String status = "connecting...";

void close() {
  if (in) { http.end(); in = nullptr; }
  have = 0;
}
void stop() {
  close();
  on = false;
  fps = 0;
  if (buf) { free(buf); buf = nullptr; }
}
bool start(const String& u) {
  stop();
  url = u;
  buf = (uint8_t*)malloc(SIZE);
  on = buf != nullptr;
  lastTry = 0; fps = 0;
  status = on ? "connecting..." : "not enough memory";
  return on;
}

static int find(const uint8_t* b, size_t n, uint8_t second, size_t from) {
  for (size_t i = from; i + 1 < n; i++) if (b[i] == 0xFF && b[i + 1] == second) return (int)i;
  return -1;
}

// Read what's arrived; draw the newest whole frame fitted into (x, y, w, h). True if it drew one.
template <class GFX> bool poll(GFX& lcd, int x, int y, int w, int h) {
  if (!on) return false;
  if (!in) {
    if (millis() - lastTry < 1500) return false;
    lastTry = millis();
    http.begin(url);
    http.setTimeout(2000);
    const int code = http.GET();
    if (code != 200) { http.end(); status = code == 403 ? "its camera is off" : code > 0 ? "no camera (" + String(code) + ")" : "can't reach it"; return false; }
    in = http.getStreamPtr();
    have = 0;
    lastByte = millis();
    status = "";
  }
  int n = in->available();
  if (n <= 0) {
    if (!in->connected() || millis() - lastByte > 5000) { close(); status = "the stream stopped: trying again"; }
    return false;
  }
  lastByte = millis();
  if (have + n > SIZE) n = SIZE - have;
  have += in->read(buf + have, n);

  int s = -1, e = -1, cut = 0;      // the newest whole frame, and where the unread rest starts
  for (int from = 0;;) {
    const int a = find(buf, have, 0xD8, from);
    if (a < 0) break;
    const int b = find(buf, have, 0xD9, a + 2);
    if (b < 0) { cut = a; break; }
    s = a; e = b + 2; cut = e; from = e;
  }
  bool drew = false;
  if (s >= 0) {
    lcd.drawJpg(buf + s, e - s, x, y, w, h, 0, 0, 0.0f, 0.0f, middle_center);
    const unsigned long now = millis();
    if (lastFrame) fps = fps * 0.8f + 0.2f * (1000.0f / max(1UL, now - lastFrame));
    lastFrame = now;
    drew = true;
  }
  if (cut > 0) { memmove(buf, buf + cut, have - cut); have -= cut; }
  if (have >= SIZE) have = 0;       // no frame end in 64 KB: start over
  return drew;
}

}  // namespace cam

// camera.h for the PC preview: a pretend camera. It plays DREAM's idle clip from the sdcard folder
// (or a grey test card) with a pretend detection box, so the CAM screens can be seen and tried.
#pragma once
#include <Arduino.h>

namespace cam {

bool on = false;
float fps = 0;
String status = "connecting...";
String url;
unsigned long lastFrame = 0;

void stop() { on = false; fps = 0; }
bool start(const String& u) { url = u; on = true; status = ""; fps = 12; return true; }

template <class GFX> bool poll(GFX& lcd, int x, int y, int w, int h) {
  if (!on || millis() - lastFrame < 80) return false;
  lastFrame = millis();
  const int fw = w * 3 / 4 > h ? h * 4 / 3 : w, fh = fw * 3 / 4;   // a 4:3 picture, fitted
  const int fx = x + (w - fw) / 2, fy = y + (h - fh) / 2;
  const uint8_t t = (millis() / 40) % 64;
  lcd.fillRect(fx, fy, fw, fh, lcd.color565(40 + t / 2, 44 + t / 3, 52));
  lcd.drawRect(fx + fw / 3, fy + fh / 5, fw / 3, fh * 3 / 5, lcd.color565(80, 210, 130));
  lcd.setFont(&fonts::Font2);
  lcd.setTextColor(lcd.color565(80, 210, 130));
  lcd.drawString("person 0.91", fx + fw / 3 + 3, fy + fh / 5 + 3);
  lcd.setTextColor(lcd.color565(239, 233, 210));
  lcd.drawString(url.length() > 40 ? url.substring(url.length() - 40) : url, fx + 4, fy + fh - 18);
  return true;
}

}  // namespace cam

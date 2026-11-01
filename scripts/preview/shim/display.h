// display.h - the PC's stand-in for scripts/esp32/display.h: the same LovyanGFX drawing, into a
// 480x320 RGB565 picture instead of the ST7796. The mouse is the touch panel.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define PIN_LED_R 4
#define PIN_LED_G 16
#define PIN_LED_B 17

extern bool touchDown;
extern int touchX, touchY;

class LGFX : public lgfx::LGFX_Sprite {
 public:
  void init() {
    setColorDepth(16);
    createSprite(480, 320);
  }
  void setRotation(int) {}     // the sprite is already landscape
  void setBrightness(int) {}
  void setTouchCalibrate(uint16_t*) {}
  void calibrateTouch(uint16_t*, uint16_t, uint16_t, int) {}
  bool getTouch(int32_t* x, int32_t* y) {
    *x = touchX;
    *y = touchY;
    return touchDown;
  }
  using lgfx::LGFX_Sprite::drawString;
  size_t drawString(const String& s, int32_t x, int32_t y) { return lgfx::LGFX_Sprite::drawString(s.c_str(), x, y); }
};

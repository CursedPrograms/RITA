// display.h - the ESP32-3248S035R "Cheap Yellow Display" (3.5", 480x320): ST7796 screen and
// XPT2046 resistive touch, sharing the HSPI bus (SCLK 14, MOSI 13, MISO 12), driven with LovyanGFX.
//
// If the picture comes out in negative colours, flip PANEL_INVERT; red and blue swapped, PANEL_BGR.
// Touch is calibrated on the first boot (four corners) and kept in flash; hold the screen while
// RITA starts to calibrate again.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define PANEL_INVERT false
#define PANEL_BGR    false

#define PIN_SCLK  14
#define PIN_MOSI  13
#define PIN_MISO  12
#define PIN_TFT_DC 2
#define PIN_TFT_CS 15
#define PIN_TFT_BL 27
#define PIN_TOUCH_CS  33
#define PIN_TOUCH_IRQ 36
#define PIN_LED_R  4    // the RGB LED, active low
#define PIN_LED_G 16
#define PIN_LED_B 17

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796  _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;

 public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host   = SPI2_HOST;   // HSPI
      c.spi_mode   = 0;
      c.freq_write = 40000000;
      c.freq_read  = 16000000;
      c.spi_3wire  = false;
      c.use_lock   = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = PIN_SCLK;
      c.pin_mosi = PIN_MOSI;
      c.pin_miso = PIN_MISO;
      c.pin_dc   = PIN_TFT_DC;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs   = PIN_TFT_CS;
      c.pin_rst  = -1;
      c.pin_busy = -1;
      c.panel_width  = 320;
      c.panel_height = 480;
      c.offset_rotation = 0;
      c.readable   = true;
      c.invert     = PANEL_INVERT;
      c.rgb_order  = PANEL_BGR;
      c.dlen_16bit = false;
      c.bus_shared = true;   // the touch is on the same bus
      _panel.config(c);
    }
    {
      auto c = _light.config();
      c.pin_bl = PIN_TFT_BL;
      c.invert = false;
      c.freq   = 44100;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    {
      auto c = _touch.config();
      c.x_min = 0;
      c.x_max = 319;
      c.y_min = 0;
      c.y_max = 479;
      c.pin_int  = PIN_TOUCH_IRQ;
      c.bus_shared = true;
      c.offset_rotation = 0;
      c.spi_host = SPI2_HOST;
      c.freq     = 1000000;
      c.pin_sclk = PIN_SCLK;
      c.pin_mosi = PIN_MOSI;
      c.pin_miso = PIN_MISO;
      c.pin_cs   = PIN_TOUCH_CS;
      _touch.config(c);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

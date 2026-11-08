// Arduino.h - the little of the Arduino core RITA's sketch needs on the PC. main.cpp runs the clock.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "WString.h"

#define PROGMEM
#define OUTPUT 1
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 0x05
#endif
#ifndef OUTPUT_OPEN_DRAIN
#define OUTPUT_OPEN_DRAIN 0x13
#endif
#ifndef HIGH
#define HIGH 1
#define LOW 0
#endif

unsigned long millis();
void delay(unsigned long ms);
template <class A, class B> auto max(A a, B b) -> decltype(a + b) { return a > b ? a : b; }
template <class A, class B> auto min(A a, B b) -> decltype(a + b) { return a < b ? a : b; }
inline long random(long from, long to) { return from + rand() % (to - from); }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void delayMicroseconds(unsigned) {}
inline int digitalRead(int) { return 1; }   // a free pin, pulled up
template <class T> T constrain(T v, T low, T high) { return v < low ? low : v > high ? high : v; }

struct SerialPort {   // her serial monitor: the console
  void begin(unsigned long) {}
  template <class... A> int printf(const char* f, A... a) { const int n = ::printf(f, a...); fflush(stdout); return n; }
  void println(const char* s) { ::printf("%s\n", s); }
};
static SerialPort Serial;

struct EspClass { uint32_t getFreeHeap() const { return 180000; } uint32_t getMaxAllocHeap() const { return 110000; } };   // a PC has no ESP32 heap to report
inline EspClass ESP;

inline bool ledcAttachChannel(int, int, int, int) { return true; }
inline bool ledcWrite(int, int) { return true; }
#ifndef PI
#define PI 3.14159265f
#endif

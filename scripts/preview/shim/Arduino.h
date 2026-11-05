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

unsigned long millis();
void delay(unsigned long ms);
template <class A, class B> auto max(A a, B b) -> decltype(a + b) { return a > b ? a : b; }
template <class A, class B> auto min(A a, B b) -> decltype(a + b) { return a < b ? a : b; }
inline long random(long from, long to) { return from + rand() % (to - from); }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
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

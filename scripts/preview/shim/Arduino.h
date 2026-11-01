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
inline long random(long from, long to) { return from + rand() % (to - from); }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
template <class T> T constrain(T v, T low, T high) { return v < low ? low : v > high ? high : v; }

struct SerialPort {
  void begin(unsigned long) {}
};
static SerialPort Serial;

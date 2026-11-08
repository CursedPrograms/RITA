// WString.h - Arduino's String on the PC, as much of it as RITA and ArduinoJson use.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

class String {
  std::string s;

 public:
  String() {}
  bool equalsIgnoreCase(const String& o) const {
    return s.size() == o.s.size() && std::equal(s.begin(), s.end(), o.s.begin(),
                                                [](char a, char b) { return tolower((unsigned char)a) == tolower((unsigned char)b); });
  }
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& t) : s(t) {}
  String(char c) : s(1, c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(double v, int places = 2) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%.*f", places, v);
    s = buf;
  }

  const char* c_str() const { return s.c_str(); }
  void toCharArray(char* buf, unsigned size) const { if (!size) return; strncpy(buf, s.c_str(), size - 1); buf[size - 1] = 0; }
  unsigned length() const { return (unsigned)s.size(); }
  char operator[](unsigned i) const { return i < s.size() ? s[i] : 0; }
  bool concat(const char* c) { s += c; return true; }
  bool concat(char c) { s += c; return true; }
  bool reserve(unsigned n) { s.reserve(n); return true; }
  String& operator+=(const String& o) { s += o.s; return *this; }

  int indexOf(const String& o, unsigned from = 0) const {
    const size_t at = s.find(o.s, from);
    return at == std::string::npos ? -1 : (int)at;
  }
  String substring(unsigned from) const { return from < s.size() ? String(s.substr(from)) : String(); }
  String substring(unsigned from, unsigned to) const {
    return from < s.size() && to > from ? String(s.substr(from, to - from)) : String();
  }
  bool startsWith(const String& o) const { return s.compare(0, o.s.size(), o.s) == 0; }
  bool endsWith(const String& o) const {
    return s.size() >= o.s.size() && s.compare(s.size() - o.s.size(), o.s.size(), o.s) == 0;
  }
  void replace(const String& a, const String& b) {
    for (size_t at = 0; (at = s.find(a.s, at)) != std::string::npos; at += b.s.size()) s.replace(at, a.s.size(), b.s);
  }
  void toLowerCase() { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); }); }
  int lastIndexOf(char c) const { size_t i = s.rfind(c); return i == std::string::npos ? -1 : (int)i; }
  void trim() {
    const char* ws = " \t\r\n";
    const size_t a = s.find_first_not_of(ws);
    if (a == std::string::npos) { s.clear(); return; }
    s = s.substr(a, s.find_last_not_of(ws) - a + 1);
  }
  void toUpperCase() { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)toupper(c); }); }
  long toInt() const { return strtol(s.c_str(), nullptr, 10); }

  friend String operator+(const String& a, const String& b) { return String(a.s + b.s); }
  friend bool operator==(const String& a, const String& b) { return a.s == b.s; }
  friend bool operator!=(const String& a, const String& b) { return a.s != b.s; }
};

#pragma once
// Minimal Arduino core stand-in, so the firmware's *drawing* code can be
// compiled and run on a normal computer. Only what firmware/ actually
// uses is here - this is not an Arduino emulator, and nothing that talks
// to real hardware is implemented (see the fakes in host_fakes.cpp).

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <string>

// PROGMEM is a no-op on a machine with one flat address space. Note the
// deliberate absence of pgm_read_dword: on a 64-bit host it would
// silently truncate a pointer, so anything needing one reads the struct
// member directly instead.
#define PROGMEM
#define IRAM_ATTR
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif

#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 2
#define OUTPUT 1
#define FALLING 2

// Plain overloaded functions rather than Arduino's min/max macros: a
// macro would textually rewrite every "max"/"min" token, including ones
// used as ordinary identifiers or member functions inside <vector> and
// other standard headers pulled in for this host build (real Arduino
// cores get away with the macro because nothing in their toolchain
// includes those headers). Firmware code calling bare min(a, b)/max(a, b)
// still resolves to these via normal unqualified lookup.
template <typename T> inline T min(T a, T b) { return a < b ? a : b; }
template <typename T> inline T max(T a, T b) { return a > b ? a : b; }

// ---- String -------------------------------------------------------------
// Arduino's String over std::string, covering the operations the firmware
// uses. Indexes follow Arduino's convention: -1 for "not found".
class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) { char b[24]; snprintf(b, sizeof(b), "%d", v); s_ = b; }
  String(unsigned int v) { char b[24]; snprintf(b, sizeof(b), "%u", v); s_ = b; }
  String(long v) { char b[32]; snprintf(b, sizeof(b), "%ld", v); s_ = b; }
  String(unsigned long v) { char b[32]; snprintf(b, sizeof(b), "%lu", v); s_ = b; }
  String(float v, int dp = 2) { char b[40]; snprintf(b, sizeof(b), "%.*f", dp, (double)v); s_ = b; }
  String(double v, int dp = 2) { char b[40]; snprintf(b, sizeof(b), "%.*f", dp, v); s_ = b; }

  const char *c_str() const { return s_.c_str(); }
  unsigned length() const { return (unsigned)s_.size(); }
  void reserve(unsigned n) { s_.reserve(n); }

  char operator[](int i) const { return (i >= 0 && i < (int)s_.size()) ? s_[i] : '\0'; }

  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *o) { s_ += o ? o : ""; return *this; }
  String &operator+=(char c) { s_ += c; return *this; }

  friend String operator+(String a, const String &b) { a.s_ += b.s_; return a; }
  friend String operator+(String a, const char *b) { a.s_ += (b ? b : ""); return a; }
  friend String operator+(const char *a, const String &b) { return String(std::string(a ? a : "") + b.s_); }
  friend String operator+(String a, char b) { a.s_ += b; return a; }

  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *o) const { return s_ == (o ? o : ""); }
  bool operator!=(const String &o) const { return !(*this == o); }
  bool operator!=(const char *o) const { return !(*this == o); }

  int indexOf(char c, int from = 0) const { return idx(s_.find(c, clampFrom(from))); }
  int indexOf(const String &t, int from = 0) const { return idx(s_.find(t.s_, clampFrom(from))); }
  int indexOf(const char *t, int from = 0) const { return idx(s_.find(t ? t : "", clampFrom(from))); }
  int lastIndexOf(char c) const { return idx(s_.rfind(c)); }

  String substring(int a) const {
    if (a < 0) a = 0;
    if (a >= (int)s_.size()) return String();
    return String(s_.substr(a));
  }
  String substring(int a, int b) const {
    if (a < 0) a = 0;
    if (b > (int)s_.size()) b = (int)s_.size();
    if (a >= b) return String();
    return String(s_.substr(a, b - a));
  }

  bool startsWith(const char *p) const { return s_.rfind(p ? p : "", 0) == 0; }
  bool startsWith(const String &p, int from) const {
    if (from < 0 || from > (int)s_.size()) return false;
    return s_.compare(from, p.s_.size(), p.s_) == 0;
  }
  bool startsWith(const char *p, int from) const { return startsWith(String(p), from); }

  void remove(int i) { if (i >= 0 && i < (int)s_.size()) s_.erase(i); }
  void remove(int i, int n) { if (i >= 0 && i < (int)s_.size()) s_.erase(i, n); }

  void trim() {
    size_t b = s_.find_first_not_of(" \t\r\n");
    size_t e = s_.find_last_not_of(" \t\r\n");
    s_ = (b == std::string::npos) ? "" : s_.substr(b, e - b + 1);
  }
  void toUpperCase() { for (auto &c : s_) c = (char)toupper((unsigned char)c); }
  int toInt() const { return atoi(s_.c_str()); }

 private:
  static int clampFrom(int f) { return f < 0 ? 0 : f; }
  static int idx(size_t p) { return p == std::string::npos ? -1 : (int)p; }
  std::string s_;
};

// ---- timing / IO --------------------------------------------------------
unsigned long millis();
void delay(unsigned long ms);
void pinMode(int pin, int mode);
int digitalRead(int pin);
void digitalWrite(int pin, int value);
int digitalPinToInterrupt(int pin);
void attachInterrupt(int irq, void (*fn)(), int mode);
void noInterrupts();
void interrupts();

// The host clock the drawing code sees. Fixed, not the wall clock, so a
// rendered screen is byte-identical run to run - otherwise every render
// would differ and diffing two of them would be useless.
bool getLocalTime(struct tm *info, uint32_t ms = 5000);
void hostSetFakeNow(int year, int month, int day, int hour, int minute, int second);

// PSRAM allocator - plain malloc here.
inline void *ps_malloc(size_t n) { return malloc(n); }

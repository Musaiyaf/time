#pragma once
#include <Arduino.h>

// Just enough JSON reading for the two fixed, flat API responses this
// firmware consumes (see weather.cpp and calendar_events.cpp): pick a
// value out by key, optionally scoped to a byte range so the same key
// name appearing in two different objects can't be confused.
//
// Deliberately not a JSON library. Both payloads have a known shape and
// only a handful of fields are wanted from each, so this is a few lines
// of indexOf/atof against the response String rather than a parse tree
// and the RAM to hold it - the same reasoning as the key=value config
// parsing elsewhere in this firmware.
//
// Note that a key searched for *with* its quotes and colon is already
// unambiguous against a longer key sharing its prefix: "country": can't
// match "country_code": because they differ before the closing quote.
namespace JsonLite {

// Byte range of the object that follows "key":{ ... }, as [start, end).
// Only valid for objects with no nested objects of their own - the next
// '}' is taken as the end.
inline bool objectRange(const String &s, const char *keyWithQuotes, int &start, int &end) {
  int k = s.indexOf(keyWithQuotes);
  if (k < 0) return false;
  int brace = s.indexOf('{', k + (int)strlen(keyWithQuotes) - 1);
  if (brace < 0) return false;
  int close = s.indexOf('}', brace);
  if (close < 0) return false;
  start = brace;
  end = close;
  return true;
}

// Numeric value of "key": within [from, to), or fallback if it's absent
// or JSON null.
inline float numberIn(const String &s, int from, int to, const char *keyWithQuotes, float fallback) {
  int k = s.indexOf(keyWithQuotes, from);
  if (k < 0 || k >= to) return fallback;
  int v = k + strlen(keyWithQuotes);
  while (v < to && (s[v] == ' ' || s[v] == ':')) v++;
  if (v >= to) return fallback;
  if (s.startsWith("null", v)) return fallback;
  return atof(s.c_str() + v);
}

// Reads the numeric array at "key":[ ... ] within [from, to) into out,
// stopping at maxOut values. A JSON null becomes nullValue. Returns how
// many were read.
inline int numberArrayIn(const String &s, int from, int to, const char *keyWithQuotes,
                          float *out, int maxOut, float nullValue) {
  int k = s.indexOf(keyWithQuotes, from);
  if (k < 0 || k >= to) return 0;
  int i = s.indexOf('[', k + (int)strlen(keyWithQuotes) - 1);
  if (i < 0 || i >= to) return 0;
  i++; // past '['
  int n = 0;
  while (i < to && n < maxOut) {
    while (i < to && (s[i] == ' ' || s[i] == ',')) i++;
    if (i >= to || s[i] == ']') break;
    if (s.startsWith("null", i)) {
      out[n++] = nullValue;
      i += 4;
    } else {
      out[n++] = atof(s.c_str() + i);
      while (i < to && s[i] != ',' && s[i] != ']') i++;
    }
  }
  return n;
}

// Text value of "key":"..." within [from, to), or "" if absent.
inline String stringIn(const String &s, int from, int to, const char *keyWithQuotes) {
  int k = s.indexOf(keyWithQuotes, from);
  if (k < 0 || k >= to) return "";
  int q = s.indexOf('"', k + strlen(keyWithQuotes)); // opening quote of the value
  if (q < 0 || q >= to) return "";
  int endq = s.indexOf('"', q + 1);
  if (endq < 0 || endq >= to) return "";
  return s.substring(q + 1, endq);
}

} // namespace JsonLite

// Verbatim from Megaton's wallclock.cpp; kept identical.
#include <windows.h>
#include "wallclock.h"

// QueryPerformanceCounter, not GetTickCount (a 10-16 ms tick would step the 750 ms damage fades). A zero frequency
// falls back to raw ticks; the multiply is split so a TSC-backed counter cannot overflow after ~35 days.
unsigned wallclock_ms() {
  static LARGE_INTEGER freq = { 0 };
  static bool asked = false;
  if (!asked) {
    asked = true;
    QueryPerformanceFrequency(&freq);
    if (freq.QuadPart == 0) freq.QuadPart = 1;  // still monotone
  }
  LARGE_INTEGER now; QueryPerformanceCounter(&now);
  const long long whole = now.QuadPart / freq.QuadPart;
  const long long rem   = now.QuadPart % freq.QuadPart;
  return (unsigned)(whole * 1000 + (rem * 1000) / freq.QuadPart);  // wraps by design
}

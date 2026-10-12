#pragma once
#include "common.h"   // rva::NOCULL_CALL_SITE_RVA/BYTES, rva::GOTSECTOR

// "Disable culling": draw every sector while flying. drawrooms seeds only the player's `globalcursectnum` into
// `gotsector` via polymost_scansector, so a floating camera leaves unreached sectors undrawn. Fix: call the same body
// for every sector still unmarked, once per frame after the seed call. Costs frame time on big maps, hence opt-in.

// May the extra sweep run? cameraActive is the render-clock snapshot, not camera_active()'s tic flag; bracketArmed is
// camera_render_armed(), since the camera can fly while displayrooms refuses to arm; inMirror stops doubling on a
// mirror's second drawrooms.
inline bool nocull_should_scan(bool cfgOn, bool cameraActive, bool bracketArmed, bool inMirror) {
  return cfgOn && cameraActive && bracketArmed && !inMirror;
}

// The sweep's engine-array budget: polymost_scansector appends to numscans/numbunches with NO bound, and the sweep
// removes the vanilla BFS's natural one, so a large map would overflow the 6144-entry arrays (rva::MAXWALLSB).
// ALLOWANCE (256) covers scansector's own BFS continuation.
const int NOCULL_CALL_ALLOWANCE = 256;
const int NOCULL_FLOOR_MARGIN   = 1024;

inline bool nocull_budget_allows(int numscansEntry, int numbunchesEntry,
                                 int numscansNow, int numbunchesNow,
                                 int wallnum) {
  if (numscansEntry < 0 || numbunchesEntry < 0 ||
      numscansNow   < 0 || numbunchesNow   < 0 || wallnum < 0) return false;

  const int floorAt = rva::MAXWALLSB - NOCULL_FLOOR_MARGIN;
  if (numscansNow > floorAt || numbunchesNow > floorAt) return false;

  const int entryMax = numscansEntry > numbunchesEntry ? numscansEntry : numbunchesEntry;
  const int free = rva::MAXWALLSB - entryMax;
  const int budget = free / 2;

  const int nowMax = numscansNow > numbunchesNow ? numscansNow : numbunchesNow;
  const int usedSinceEntry = nowMax - entryMax;

  if (usedSinceEntry + wallnum + NOCULL_CALL_ALLOWANCE > budget) return false;
  return true;
}

inline int nocull_next_unscanned(const unsigned char* gotsector, int numsectors, int from) {
  if (!gotsector || numsectors <= 0) return -1;
  const int start = from < 0 ? 0 : from + 1;
  for (int s = start; s < numsectors; s++) {
    if ((gotsector[s >> 3] & (unsigned char)(1 << (s & 7))) == 0) return s;
  }
  return -1;
}

inline bool& nocull_available_slot() { static bool avail = true; return avail; }
inline bool nocull_available() { return nocull_available_slot(); }
inline void nocull_mark_unavailable() { nocull_available_slot() = false; }

inline void nocull_reset_available() { nocull_available_slot() = true; }

inline bool nocull_row_unavailable() { return !nocull_available(); }


// Redirects polymost_drawrooms' seed call to polymost_scansector through hooked_polymost_scansector. Never fatal: a
// failed install logs and marks the option unavailable.
bool nocull_install();

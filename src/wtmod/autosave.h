// From Megaton's autosave.h.
#pragma once
#include "common.h"

// Autosave as a pure rule: every decision about WHEN a save is due is here.

const int AUTOSAVE_MINS_MIN = 0, AUTOSAVE_MINS_MAX = 30;
// 0 is off and the default: saving over a slot unasked has to be asked for.
const int AUTOSAVE_MINS_DEFAULT = AUTOSAVE_MINS_MIN;
// Slot 9: furthest from hand-saved slots.
const int AUTOSAVE_SLOT_MIN = 0, AUTOSAVE_SLOT_MAX = 9;
const int AUTOSAVE_SLOT_DEFAULT = AUTOSAVE_SLOT_MAX;
const unsigned AUTOSAVE_MS_PER_MIN = 60000u;

inline int autosave_mins_clamped(int v) { return clamp_int(v, AUTOSAVE_MINS_MIN, AUTOSAVE_MINS_MAX); }
inline int autosave_slot_clamped(int v) { return clamp_int(v, AUTOSAVE_SLOT_MIN, AUTOSAVE_SLOT_MAX); }

// int flags only, never bool: MSVC 19.50 /O2 loses a bool member of a struct an inline function returns.
struct AutosaveState { unsigned lastMs; unsigned levelSig; int armed; };

// Returns 1 when a save is due. `sig` is the map signature; `inPlay` is gm == 4 and none of paused / the game's menu /
// this panel / the free camera / dead.
inline int autosave_step(AutosaveState* s, int intervalMins, unsigned nowMs, unsigned sig, int inPlay) {
  if (!s) return 0;
  // A new map (and the first call) restarts the timer and never saves: a level-start save duplicates the arrival.
  if (!s->armed || sig != s->levelSig) {
    s->armed = 1;
    s->levelSig = sig;
    s->lastMs = nowMs;
    return 0;
  }
  // Off holds the clock at now, so turning it up gives a full interval.
  const int mins = autosave_mins_clamped(intervalMins);
  if (mins <= 0) { s->lastMs = nowMs; return 0; }
  // Out of play only the save waits, so one due during a pause lands on the first tic back.
  if (!inPlay) return 0;
  // Unsigned, so an interval straddling wallclock_ms()'s wrap is exact.
  if ((unsigned)(nowMs - s->lastMs) < (unsigned)mins * AUTOSAVE_MS_PER_MIN) return 0;
  s->lastMs = nowMs;
  return 1;
}

void autosave_frame();

// Saves through FUN_0071cc30 (rva 0x6CC30). autosave_frame is NOT called: Present runs on the render thread and would
// read player state mid-tic, so autosave_tic runs at end of tic.
bool autosave_install();
void autosave_tic(unsigned char* playerBase, bool cameraActive);

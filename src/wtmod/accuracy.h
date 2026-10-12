#pragma once
#include "common.h"   // rva::AccSite and the three ACC_* site pairs

// Perfect accuracy: which spread sites this one shot gets. `shoot` adds krand spread for the pistol, chaingun, shotgun
// and expander at six sites; `B8 <centre> 00 00 00` makes the subtraction 0. ONE PAIR PER SHOT, NEVER A UNION (chaingun
// and shotgun share ACC_HITSCAN; patch_bracket_arm refuses an overlap). The pistol's sites sit inside `aim() == -1`.

enum AccPair {
  ACC_PAIR_NONE     = -1,
  ACC_PAIR_PISTOL   = 0,
  ACC_PAIR_HITSCAN  = 1,   // chaingun AND shotgun: one shared branch
  ACC_PAIR_EXPANDER = 2,
  ACC_PAIR_COUNT    = 3
};

const int ACC_PAIR_SITES = 2;

// The pair this shot needs, from `atwith` and the four toggles: pistol 0xa23, chaingun 0x9e8, shotgun 0xa35, expander
// 0x990, shared with LIZTROOP/PIGCOP/ENFORCER/GROWSPARK's enemy spread, so accuracy_should_arm holds the shooter gate.
// Everything else is NONE.
inline int accuracy_pair_for(short atwith, bool pistol, bool chaingun,
                             bool shotgun, bool expander) {
  switch (atwith) {
    case 0x0a23: return pistol   ? ACC_PAIR_PISTOL   : ACC_PAIR_NONE;
    case 0x09e8: return chaingun ? ACC_PAIR_HITSCAN  : ACC_PAIR_NONE;
    case 0x0a35: return shotgun  ? ACC_PAIR_HITSCAN  : ACC_PAIR_NONE;
    case 0x0990: return expander ? ACC_PAIR_EXPANDER : ACC_PAIR_NONE;
    default:     return ACC_PAIR_NONE;
  }
}

inline const rva::AccSite* accuracy_pair_sites(int pair) {
  switch (pair) {
    case ACC_PAIR_PISTOL:   return rva::ACC_PISTOL;
    case ACC_PAIR_HITSCAN:  return rva::ACC_HITSCAN;
    case ACC_PAIR_EXPANDER: return rva::ACC_EXPANDER;
    default:                return 0;
  }
}

// How many sites this shot patches (0 or ACC_PAIR_SITES). `outSites` is set null when the count is 0, so a caller
// ignoring the count cannot arm over a stale pointer.
inline int accuracy_sites_for(short atwith, bool pistol, bool chaingun, bool shotgun,
                              bool expander, const rva::AccSite** outSites) {
  if (outSites) *outSites = 0;
  const rva::AccSite* sites =
      accuracy_pair_sites(accuracy_pair_for(atwith, pistol, chaingun, shotgun, expander));
  if (!sites) return 0;
  if (outSites) *outSites = sites;
  return ACC_PAIR_SITES;
}

// The per-pair session latch: patch_bracket_arm fingerprints every site on every arm and refuses the whole bracket if
// one byte differs (common.h). A refusal will not change on retry, so it LATCHES THAT PAIR OFF and logs once. One
// program-wide instance.
inline bool* accuracy_latch_slots() {
  static bool slots[ACC_PAIR_COUNT] = { false, false, false };
  return slots;
}

inline bool accuracy_pair_latched(int pair) {
  if (pair < 0 || pair >= ACC_PAIR_COUNT) return true;
  return accuracy_latch_slots()[pair];
}

inline void accuracy_latch_pair(int pair) {
  if (pair < 0 || pair >= ACC_PAIR_COUNT) return;
  accuracy_latch_slots()[pair] = true;
}

inline void accuracy_latch_reset_all() {
  for (int i = 0; i < ACC_PAIR_COUNT; i++) accuracy_latch_slots()[i] = false;
}

// Whether this shot may arm: the LOCAL PLAYER'S own shot, a real pair, one not latched off. accuracy_pair_for cannot
// tell shooters apart: an enemy's hitscan shot would arm the patch, and a transient VirtualProtect refusal there would
// latch a weapon off for the session.
inline bool accuracy_should_arm(int pair, bool isPlayerShot) {
  return isPlayerShot && pair != ACC_PAIR_NONE && !accuracy_pair_latched(pair);
}

inline bool accuracy_note_arm_result(int pair, bool armed) {
  if (!armed) accuracy_latch_pair(pair);
  return armed;
}

// A latched pair must not read as live in the panel: config.cpp never writes the latch back into the switch (a saved
// `accpistol=1` must survive onto a binary where the sites are fine), so the hazard strip and "(Unavailable)" both use
// `value && !latched`.
inline bool accuracy_row_live(int value, bool latched) {
  return value != 0 && !latched;
}
inline bool accuracy_row_unavailable(bool latched) { return latched; }

// Whether `hooked_shoot` may attempt to arm, given whether an OUTER call's bracket is still armed. `shoot` re-enters
// itself (checkhitsprite per blood hit, atwith 0x8f8..0x8fb); a nested arm on the same site would find the outer's `B8`
// patch, refuse, and FALSELY LATCH a pair.
inline bool accuracy_may_attempt_arm(bool alreadyArmed) {
  return !alreadyArmed;
}

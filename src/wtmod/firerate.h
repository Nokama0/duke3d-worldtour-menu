#pragma once
#include <math.h>

// Fire rate, as pure rules. The engine advances a per-weapon counter (kickback_pic) one step per tic, mostly just
// counting; advancing it past those steps and never past a real event shortens the cadence without skipping a shot,
// sound, shell or reset.

const int FIRERATE_MIN = 100, FIRERATE_MAX = 400, FIRERATE_STEP = 50;
// Default for both rows this range drives (fire rate and weapon switch speed).
const int FIRERATE_DEFAULT = FIRERATE_MIN;
const int FIRERATE_EVENTS_MAX = 8;
struct FireRateWeapon { int weapon; int nEvents; int events[FIRERATE_EVENTS_MAX]; };

// Pipebomb throw, tripbomb placement and hand remote are not cooldown counters, so have no entry.
inline const FireRateWeapon* firerate_weapon(int weapon) {
  // kickback_pic values where the next tic does something other than kb = v + 1.
  static const FireRateWeapon FIRERATE_TABLE[9] = {
    { 0,  2, { 6, 13 } },                       // mighty foot: shot at 7, end at 14
    { 1,  5, { 1, 2, 4, 7, 26 } },  // pistol: shot 1, shell 2, clip decision 5, sounds, end 27
    { 2,  7, { 3, 12, 14, 16, 19, 23, 30 } },   // shotgun: shot at 4, pump events, end at 31
    { 3,  5, { 2, 5, 8, 11, 12 } },             // chaingun: shots at 3, 6, 9, 12; held check at 13
    { 4,  2, { 3, 19 } },                       // RPG: shot at 4, end at 20
    { 6,  1, { 11 } },                          // shrinker: shot and reset at 11
    { 7,  3, { 2, 4, 5 } },                     // devastator: shots at 3 and 5, end at 6
    { 9,  3, { 2, 3, 4 } },                     // freezer: shot at 3, checks at 4, held check at 4
    { 11, 1, { 4 } },                           // expander: shot and reset at 4
  };
  for (int i = 0; i < 9; i++) if (FIRERATE_TABLE[i].weapon == weapon) return &FIRERATE_TABLE[i];
  return 0;
}

// A hand-edited ini never holds a rate the knob cannot sit on. Nearest stop, not truncation.
inline int firerate_rate_clamped(int v) {
  if (v < FIRERATE_MIN) v = FIRERATE_MIN;
  if (v > FIRERATE_MAX) v = FIRERATE_MAX;
  return ((v - FIRERATE_MIN + FIRERATE_STEP / 2) / FIRERATE_STEP) * FIRERATE_STEP + FIRERATE_MIN;
}

// A counter already on an event is held, since the engine performs that event next tic.
inline int firerate_next_event(const FireRateWeapon& t, int k) {
  for (int i = 0; i < t.nEvents; i++) if (t.events[i] >= k) return t.events[i];
  return -1;
}

// Never from rest: k <= 0 means not firing, and advancing from there would fire the weapon itself.
inline int firerate_advance(const FireRateWeapon& t, int k, int steps) {
  if (k <= 0 || steps <= 0) return k;
  const int nxt = firerate_next_event(t, k);
  if (nxt < 0) return k;
  const int target = k + steps;
  return target > nxt ? nxt : target;
}

// Carry in [0, 1): 150 is one extra every other tic, 400 three every tic.
inline int firerate_steps(int ratePct, double* carry) {
  const double extra = (firerate_rate_clamped(ratePct) - 100) / 100.0;
  if (!carry) return (int)floor(extra);
  *carry += extra;
  const int n = (int)floor(*carry);
  *carry -= n;
  if (*carry < 0.0) *carry = 0.0;
  return n;
}

// Every condition the one player-state write needs.
inline bool firerate_gate(bool verified, bool localPlayer, bool freezeActive, bool cameraActive,
                          int ratePct, int kickback, bool hasTable) {
  return verified && localPlayer && !freezeActive && !cameraActive
      && firerate_rate_clamped(ratePct) > 100 && kickback > 0 && hasTable;
}

// One-word reason firerate_gate came out as it did, for the throttled `firerate:` diagnostic.
inline const char* firerate_gate_reason(bool verified, bool localPlayer, bool freezeActive, bool cameraActive,
                                        int ratePct, int kickback, bool hasTable) {
  if (!verified || !localPlayer) return "unverified";
  if (freezeActive)               return "frozen";
  if (cameraActive)               return "camera";
  if (firerate_rate_clamped(ratePct) <= 100) return "off";
  if (kickback <= 0)              return "rest";
  if (!hasTable)                  return "notable";
  return "on";
}

// The carry is shared by every weapon; a switch must not carry a fraction into the new cadence.
inline double firerate_carry_for_weapon(double carry, int lastWeapon, int weapon) {
  return weapon != lastWeapon ? 0.0 : carry;
}

// What the last tic did, for the aim ledger. Filled whether the gate passed or refused.
struct FireRateLast {
  int weapon;       // curr_weapon this tic
  int kickbackIn;   // what the engine's own per-tic step left
  int kickbackOut;  // what this feature wrote back (equal means it did nothing)
  int steps;        // extra steps it asked for
  int carryMilli;   // the fractional carry x1000, so the log needs no float
  int acted;        // 0 when the gate refused
  const char* why;  // the gate's reason when it refused, else "ok"
};
const FireRateLast& firerate_last();

void firerate_tic(int snum, bool freezeActive);
void firerate_install(const unsigned char* base, unsigned imageSize);

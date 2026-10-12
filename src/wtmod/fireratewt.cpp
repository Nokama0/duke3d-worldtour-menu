// Runs from the processinput tail AFTER the original. Safe because firerate_advance never moves a counter at rest or
// past the next non-counting value: no shot or reload is skipped.
#include <windows.h>
#include "firerate.h"
#include "config.h"
#include "freecam.h"
#include "hook.h"

// WORLD TOUR'S OWN OFFSETS (Megaton's ang is at 0x28C, here 0x2CC). curr_weapon: FUN_0074fd60 compares and writes it
// (10 for weapon 10). kickback_pic: processinput subtracts 13, bounds at 18 and jumps a table for the shotgun pump.
#define P_CURR_WEAPON 0x2FA   // int16
#define P_KICKBACK    0x31C   // int16
#define P_STRIDE      0x408

static const unsigned kRvaPlayers = 0x5FD5C0;   // player[0]
static const unsigned char* s_base = 0;
static int s_ready = 0;
// MUST persist across tics (a per-tic reset floors the half step at 150). Reset on every refusal and weapon change: a
// fraction of one weapon's cadence is not another's.
static double s_carry = 0.0;

// Written on every path so "gate said no" and "never ran" differ.
static FireRateLast s_last = { 0, 0, 0, 0, 0, 0, "never ran" };
const FireRateLast& firerate_last() { return s_last; }
static int s_lastWeapon = -1;   // the first tic reads as a switch

// WORLD TOUR HAS A THIRTEENTH WEAPON (processinput jump table 0x0072dba0, arms 0..12). DERIVED, NOT VERIFIED: its arm
// at 0x0072d9b2 increments the counter and tests `CMP AX,0x2` (shot) and `CMP AX,0x10` (end), so the events are 1 and
// 15. If ever wrong, DELETE this entry, not adjust it: no speed-up beats a skipped shot.
static const FireRateWeapon kWeapon12 = { 12, 2, { 1, 15, 0, 0, 0, 0, 0, 0 } };

static const FireRateWeapon* wt_firerate_weapon(int weapon) {
  const FireRateWeapon* t = firerate_weapon(weapon);
  if (t) return t;
  return weapon == kWeapon12.weapon ? &kWeapon12 : 0;
}

void firerate_install(const unsigned char* base, unsigned imageSize) {
  if (!base) return;
  const unsigned last = kRvaPlayers + 15u * P_STRIDE + P_KICKBACK + 2u;
  if (last > imageSize) {
    wt_log("firerate: player[15] + 0x%X does not fit the image; fire rate stays off", P_KICKBACK);
    return;
  }
  s_base = base;
  s_ready = 1;
}

void firerate_tic(int snum, bool freezeActive) {
  if (!s_ready || snum < 0 || snum > 15) return;
  unsigned char* p = (unsigned char*)(s_base + kRvaPlayers + (unsigned)snum * P_STRIDE);
  const int weapon   = *(const short*)(p + P_CURR_WEAPON);
  const int kickback = *(const short*)(p + P_KICKBACK);
  const FireRateWeapon* t = wt_firerate_weapon(weapon);
  // localPlayer is not a parameter: the caller already gated on it.
  if (!firerate_gate(s_ready != 0, true, freezeActive, camera_active(),
                     g_cfg.firerate, kickback, t != 0)) {
    s_carry = 0.0;
    s_lastWeapon = weapon;
    s_last.weapon = weapon; s_last.kickbackIn = kickback; s_last.kickbackOut = kickback;
    s_last.steps = 0; s_last.carryMilli = 0; s_last.acted = 0;
    s_last.why = firerate_gate_reason(s_ready != 0, true, freezeActive, camera_active(),
                                      g_cfg.firerate, kickback, t != 0);
    return;
  }
  s_carry = firerate_carry_for_weapon(s_carry, s_lastWeapon, weapon);
  s_lastWeapon = weapon;
  const int steps = firerate_steps(g_cfg.firerate, &s_carry);
  const int next = firerate_advance(*t, kickback, steps);
  if (next != kickback) *(short*)(p + P_KICKBACK) = (short)next;
  s_last.weapon = weapon; s_last.kickbackIn = kickback; s_last.kickbackOut = next;
  s_last.steps = steps; s_last.carryMilli = (int)(s_carry * 1000.0);
  s_last.acted = 1; s_last.why = "ok";
}

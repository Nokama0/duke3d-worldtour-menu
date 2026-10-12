// Mouse aiming arithmetic, free of Windows and engine types (hooks: mouseaim.cpp). The game polls the mouse ~13 times a
// second, so raw input is accumulated at the device's rate and added to `ang` and `horiz` after the engine's step.
#ifndef WTMOD_MOUSEAIM_H
#define WTMOD_MOUSEAIM_H

#include "common.h"   // clamp_double_to_int: an out-of-range (int) cast is UB
#include <math.h>

// `mouseaimsens` is degrees of YAW per raw count times 1000 (menu.cpp prints "%d.%03d").
const int MOUSEAIM_SENS_MIN = 5, MOUSEAIM_SENS_MAX = 200, MOUSEAIM_SENS_DEFAULT = 40;
const int MOUSEAIM_VSCALE_MIN = 50, MOUSEAIM_VSCALE_MAX = 150, MOUSEAIM_VSCALE_DEFAULT = 100;

const double MOUSEAIM_PI = 3.14159265358979323846;
// tan(57 deg) * 128 = 197, so the horizon stays inside the engine's -99..299 clamp at both ends.
const double MOUSEAIM_PITCH_LIMIT_DEG = 57.0;
// The engine's clamp on `horiz`, from processinput's tail.
const int MOUSEAIM_HORIZ_MIN = -99, MOUSEAIM_HORIZ_MAX = 299;

// A stuck mouse or a refusing clamp would grow the pending value without bound; (int) casts are UB once unrepresentable.
const double MOUSEAIM_YAW_PENDING_MAX   = 4096.0;   // angle units: two full turns
const double MOUSEAIM_HORIZ_PENDING_MAX = 400.0;    // horizon units: the whole engine range

// The strafe modifier spends mouse x sideways at the ENGINE's rate: getinput divides by 8 into svel or by 64 into avel,
// and processinput doubles avel, so one yaw unit is 32 and 32/8 = 4 svel. The sign is the engine's too.
const double MOUSEAIM_SVEL_PER_YAW_UNIT = -4.0;
// getinput's own clamp on svel.
const int MOUSEAIM_SVEL_MAX = 90;
// Two full strafe ranges: the same guard as above.
const double MOUSEAIM_SVEL_PENDING_MAX = 180.0;

inline int mouseaim_sens_clamped(int v)   { return clamp_int(v, MOUSEAIM_SENS_MIN, MOUSEAIM_SENS_MAX); }
inline int mouseaim_vscale_clamped(int v) { return clamp_int(v, MOUSEAIM_VSCALE_MIN, MOUSEAIM_VSCALE_MAX); }

// Build units of yaw per raw count: degrees per count, at 2048 units to the turn.
inline double mouseaim_yaw_units_per_count(int sens) {
  return (mouseaim_sens_clamped(sens) / 1000.0) * (2048.0 / 360.0);
}
// Degrees of PITCH per raw count. This game's camera is a real rotate-X by getangle(128, horiz-100) degrees
// (docs/re/engine-facts.md), so the default vscale of 100 already matches the axes.
inline double mouseaim_pitch_deg_per_count(int sens, int vscale) {
  return (mouseaim_sens_clamped(sens) / 1000.0) * (mouseaim_vscale_clamped(vscale) / 100.0);
}

// `horiz` is a tangent, not an angle; these convert to degrees and back so the pitch rate stays constant.
inline double mouseaim_pitch_deg_of_horiz(double horizUnits) {
  return atan((horizUnits - 100.0) / 128.0) * 180.0 / MOUSEAIM_PI;
}
inline double mouseaim_horiz_of_pitch_deg(double deg) {
  if (deg >  MOUSEAIM_PITCH_LIMIT_DEG) deg =  MOUSEAIM_PITCH_LIMIT_DEG;
  if (deg < -MOUSEAIM_PITCH_LIMIT_DEG) deg = -MOUSEAIM_PITCH_LIMIT_DEG;
  return 100.0 + 128.0 * tan(deg * MOUSEAIM_PI / 180.0);
}

// A horizon outside +-57 degrees would clamp at BOTH ends of the difference and return zero, a dead zone; clamped into
// the band first, a small movement still moves.
inline double mouseaim_horiz_in_band(double horizUnits) {
  const double hi = mouseaim_horiz_of_pitch_deg( MOUSEAIM_PITCH_LIMIT_DEG);
  const double lo = mouseaim_horiz_of_pitch_deg(-MOUSEAIM_PITCH_LIMIT_DEG);
  if (horizUnits > hi) return hi;
  if (horizUnits < lo) return lo;
  return horizUnits;
}

inline int mouseaim_horiz_clamped(int horiz) {
  return clamp_int(horiz, MOUSEAIM_HORIZ_MIN, MOUSEAIM_HORIZ_MAX);
}

// NaN restarts from zero: one bad live read would otherwise poison the accumulator.
inline double mouseaim_clamp_pending(double v, double maxAbs) {
  if (!(v == v)) return 0.0;
  if (v >  maxAbs) return  maxAbs;
  if (v < -maxAbs) return -maxAbs;
  return v;
}

// Taken around the CURRENT pitch so angular sensitivity is the same at every pitch.
inline double mouseaim_pitch_units_delta(int countsY, int sens, int vscale, bool flip,
                                         double horizNowUnits) {
  const double dir = flip ? 1.0 : -1.0;   // not flipped: mouse forward (dy < 0) looks up
  const double ddeg = dir * countsY * mouseaim_pitch_deg_per_count(sens, vscale);
  const double p0 = mouseaim_pitch_deg_of_horiz(mouseaim_horiz_in_band(horizNowUnits));
  return mouseaim_horiz_of_pitch_deg(p0 + ddeg) - mouseaim_horiz_of_pitch_deg(p0);
}

// A FRACTION, never a queue: everything whole goes out in its own tic.
struct MouseAimState { double yawPending; double horizPending; double svelPending; };
inline void mouseaim_reset(MouseAimState* s) {
  if (s) { s->yawPending = 0.0; s->horizPending = 0.0; s->svelPending = 0.0; }
}

struct MouseAimTic { int angAdd; int horizAdd; };

// `horizNow` is the horizon as the engine's step left it; horizAdd is limited so the sum lands inside the engine's
// clamp.
inline MouseAimTic mouseaim_tic(MouseAimState* s, int countsX, int countsY,
                                int sens, int vscale, bool flip, bool aimMode, int horizNow) {
  MouseAimTic t = { 0, 0 };
  if (!s) return t;
  // No ceiling of our own: a large count in one tic turns all the way, unlike getinput's +-127.
  s->yawPending = mouseaim_clamp_pending(s->yawPending + countsX * mouseaim_yaw_units_per_count(sens),
                                         MOUSEAIM_YAW_PENDING_MAX);
  t.angAdd = clamp_double_to_int(s->yawPending);      // toward zero, never UB
  s->yawPending -= (double)t.angAdd;

  // Off, processinput snaps the horizon back toward 100 every tic and would fight any write: counts are discarded.
  if (!aimMode) { s->horizPending = 0.0; return t; }
  s->horizPending = mouseaim_clamp_pending(
      s->horizPending + mouseaim_pitch_units_delta(countsY, sens, vscale, flip, (double)horizNow),
      MOUSEAIM_HORIZ_PENDING_MAX);
  const int want = clamp_double_to_int(s->horizPending);
  const int limited = mouseaim_horiz_clamped(horizNow + want) - horizNow;
  // At the clamp the rest is dropped, not banked, or it would unwind as a lurch when the player looks back.
  if (limited != want) s->horizPending = 0.0;
  else                 s->horizPending -= (double)want;
  t.horizAdd = limited;
  return t;
}

inline double mouseaim_svel_units_per_count(int sens) {
  return MOUSEAIM_SVEL_PER_YAW_UNIT * mouseaim_yaw_units_per_count(sens);
}
inline int mouseaim_svel_clamped(int svel) {
  return clamp_int(svel, -MOUSEAIM_SVEL_MAX, MOUSEAIM_SVEL_MAX);
}

// `svelNow` is the strafe the engine already put in its global, so mouse and keyboard share its ceiling. The result is
// BEFORE the rotation into world axes. Spending the counts takes the yaw carry too, else it would turn the view later.
inline int mouseaim_strafe_tic(MouseAimState* s, int countsX, int sens, int svelNow) {
  if (!s) return 0;
  s->yawPending = 0.0;
  s->svelPending = mouseaim_clamp_pending(
      s->svelPending + countsX * mouseaim_svel_units_per_count(sens), MOUSEAIM_SVEL_PENDING_MAX);
  const int want = clamp_double_to_int(s->svelPending);
  const int limited = mouseaim_svel_clamped(svelNow + want) - svelNow;
  if (limited != want) s->svelPending = 0.0;
  else                 s->svelPending -= (double)want;
  return limited;
}

// The engine's mulscale9 and the 16-bit add it stores through.
inline int mouseaim_mulscale9(int a, int b) {
  return (int)(((long long)a * (long long)b) >> 9);
}
inline short mouseaim_add16(int now, int add) {
  return (short)(unsigned short)((unsigned)now + (unsigned)add);
}

// getinput writes the record's velocity fields in WORLD axes, so a strafe added afterwards must use the same rotation:
// `sinAng` is sintable[ang & 2047], `sinAngBack` sintable[(ang + 1536) & 2047]. Added, never assigned, so the
// keyboard's strafe survives.
inline void mouseaim_strafe_into_record(short* recFvel, short* recSvel, int svelAdd,
                                        int sinAng, int sinAngBack) {
  if (!recFvel || !recSvel) return;
  *recFvel = mouseaim_add16(*recFvel, mouseaim_mulscale9(svelAdd, sinAng));
  *recSvel = mouseaim_add16(*recSvel, mouseaim_mulscale9(svelAdd, sinAngBack));
}

// Hooks: CONTROL_GetMouseDelta (hands the game nothing), getinput (strafe), processinput (aim). Inputs arrive clamped.
void mouseaim_install(int sens, int vscale);

// 0 when the ini did not ask for the feature at startup: no hooks, so the switches below do nothing this run.
int mouseaim_installed();

// Patches are never removed: off, the replacement calls the trampoline and the tic hook adds nothing.
void mouseaim_set_enabled(int on);
void mouseaim_set_sens(int sens);
void mouseaim_set_vscale(int vscale);

// From the WINDOW thread. Discarded (and the accumulator emptied) while the feature cannot use them (panel open,
// paused, a camera owning the view), so nothing banks up to lurch out later.
void mouseaim_raw_counts(int dx, int dy);

// Motion not yet applied, for the drawn view's fraction (`yawUnits` build units, `horizUnits` horizon units). Returns 0
// and writes zeroes when this feature is not driving the view, so smoothview.cpp falls back to the engine's.
int mouseaim_view_pending(double horizNowUnits, double* yawUnits, double* horizUnits);

// Both rising together means the old pipeline is gone.
long mouseaim_poll_count();
long mouseaim_zeroed_count();

#endif

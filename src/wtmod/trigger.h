#pragma once

#include "esp_layout.h"
#include "common.h"
#include "esp_tint.h"    // WorldXY
#include <math.h>

// Secret trigger ESP rules, pure. A hidden trigger and what it operates share only a LOTAG: a touchplate (statnum 6,
// picnum 3) calls operateactivators(lotag); picnum 4 locks.
const int TRIG_MAX_CHAINS  = 32;

const int TRIG_SECRET_LOTAG = 32767;

enum { TRIG_TOUCHPLATE = 0, TRIG_SWITCH = 1 };

enum { TRIG_EFFECT_SECRET = 0, TRIG_EFFECT_OPENS = 1, TRIG_EFFECT_UNLOCKS = 2 };

// One sprite as scanned. `kind` is TRIG_TOUCHPLATE/TRIG_SWITCH for a trigger, -1 for an activator.
struct TrigSprite { int index, kind, isActivator, isLocked, lotag, hitag, sectnum, x, y, z; bool exhausted; };

struct TrigSector { int lotag, wallptr, wallnum; };

struct TrigChain  { int kind, trigger, activator, sector, effect; int tx, ty, tz, ax, ay, az; };

inline bool trigger_sector_is_secret(int lotag) { return lotag == TRIG_SECRET_LOTAG; }

// Is sector `s` the secret itself or sharing a wall with one? A trigger often opens a door into the secret, but an
// effector lowering a floor inside it is as common. Found secrets drop out: checksectors zeroes the lotag on entry.
inline bool trigger_sector_near_secret(int s, const TrigSector* sec, int nsec, const short* wallNext, int nwall) {
  if (!sec || !wallNext || s < 0 || s >= nsec) return false;
  if (trigger_sector_is_secret(sec[s].lotag)) return true;
  const int w0 = sec[s].wallptr, n = sec[s].wallnum;
  if (w0 < 0 || n < 0 || w0 + n > nwall) return false;       // a malformed loop is refused, not clamped
  for (int w = w0; w < w0 + n; w++) {
    const int nx = wallNext[w];
    if (nx >= 0 && nx < nsec && trigger_sector_is_secret(sec[nx].lotag)) return true;
  }
  return false;
}

// Range is measured in the WORLD: project_world zeroes every field on a refusal, so an end behind the player measured
// "zero away". A line is clipped to the near plane, not dropped.

inline int trigger_world_dist2d(int cx, int cy, int x, int y) {
  const double dx = (double)x - (double)cx;
  const double dy = (double)y - (double)cy;
  const double d = sqrt(dx * dx + dy * dy);
  return (d > 2147483647.0) ? 0x7FFFFFFF : (int)d;
}

inline bool trigger_end_in_range(int dist2d, int drawdist) {
  return esp_within_draw_distance(dist2d, drawdist);
}
inline bool trigger_end_in_range(int dist2d, int drawdist, int noLimit) {
  return esp_within_draw_distance_or_unlimited(dist2d, drawdist, noLimit);
}

// Segment parameter where A->B crosses `nearPlane`: -1.0 both ends behind, 0.0 both in front.
inline double trigger_segment_clip_t(double depthA, double depthB, double nearPlane) {
  const bool aBehind = !(depthA >= nearPlane);
  const bool bBehind = !(depthB >= nearPlane);
  if (aBehind && bBehind) return -1.0;    // nothing of it is in front
  if (!aBehind && !bBehind) return 0.0;   // nothing to clip
  const double span = depthB - depthA;
  if (!(span > 0.0) && !(span < 0.0)) return -1.0;
  double t = (nearPlane - depthA) / span;
  if (!(t >= 0.0)) t = 0.0;
  if (t > 1.0) t = 1.0;
  return t;
}

inline int trigger_lerp_coord(int a, int b, double t) {
  const double v = (double)a + ((double)b - (double)a) * t;
  return clamp_double_to_int(v < 0.0 ? v - 0.5 : v + 0.5);
}

inline void trigger_lerp_point(int ax, int ay, int az, int bx, int by, int bz,
                               double t, int* x, int* y, int* z) {
  if (x) *x = trigger_lerp_coord(ax, bx, t);
  if (y) *y = trigger_lerp_coord(ay, by, t);
  if (z) *z = trigger_lerp_coord(az, bz, t);
}

// project_world also refuses on the pitched view plane, which the depth clip misses, so the draw asks the projection:
// both project, draw; neither, nothing; one, bisect the refused end.
const int TRIG_CLIP_STEPS = 8;   // within 1/256 of the segment, and bounds the loop

inline double trigger_bisect_toward(double lo, double hi) { return (lo + hi) * 0.5; }

// ENGINE-FORCED DIVERGENCE: World Tour refuses on clip w, so the caller supplies the plane in ScreenPt::depth units plus
// this margin.
const double TRIG_CLIP_MARGIN = 0.01;

inline int trigger_effect(int operatedLotag, bool operatedIsSecret, bool activatorLocked) {
  if (activatorLocked) return TRIG_EFFECT_UNLOCKS;
  return (operatedIsSecret || operatedLotag == TRIG_SECRET_LOTAG) ? TRIG_EFFECT_SECRET : TRIG_EFFECT_OPENS;
}

inline const char* trigger_effect_label(int e) {
  return e == TRIG_EFFECT_SECRET ? "Secret" : e == TRIG_EFFECT_UNLOCKS ? "Locked" : "Opens secret";
}
inline const char* trigger_step_label() { return "Step here"; }

// Same-map check: sector/wall counts plus standables length; a hint, not a hash.
const unsigned TRIG_SCAN_PERIOD_MS = 1000;
inline unsigned trigger_map_signature(int nsectors, int nwalls, int standables) {
  return ((unsigned)nsectors * 2654435761u) ^ ((unsigned)nwalls * 40503u) ^ ((unsigned)standables * 97u);
}

// One chain per (trigger, activator) pair whose operated sector is a secret or borders one. Triggers: unused
// touchplates (hittype temp_data[5] = 1 marks used up) and switches. Lotag 0 is not a channel (it matches every
// untagged activator).
inline int trigger_resolve(const TrigSprite* sp, int nsp, const TrigSector* sec, int nsec,
                           const short* wallNext, int nwall, TrigChain* out, int outMax) {
  if (!sp || !sec || !wallNext || !out || outMax <= 0) return 0;
  int n = 0;
  for (int t = 0; t < nsp && n < outMax; t++) {
    if (sp[t].isActivator) continue;
    if (sp[t].kind != TRIG_TOUCHPLATE && sp[t].kind != TRIG_SWITCH) continue;
    if (sp[t].kind == TRIG_TOUCHPLATE && sp[t].exhausted) continue;
    if (sp[t].lotag == 0) continue;
    for (int a = 0; a < nsp && n < outMax; a++) {
      if (!sp[a].isActivator || sp[a].lotag != sp[t].lotag) continue;
      const int s = sp[a].sectnum;
      if (s < 0 || s >= nsec) continue;
      if (!trigger_sector_near_secret(s, sec, nsec, wallNext, nwall)) continue;
      TrigChain c = { sp[t].kind, sp[t].index, sp[a].index, s,
                      trigger_effect(sec[s].lotag, trigger_sector_is_secret(sec[s].lotag), sp[a].isLocked != 0),
                      sp[t].x, sp[t].y, sp[t].z, sp[a].x, sp[a].y, sp[a].z };
      out[n++] = c;
    }
  }
  return n;
}

// trigger.cpp: tic-clock scan and render-clock draw; no-ops unless secrettriggers is on.
void trigger_tic();
void trigger_draw(int screenW, int screenH);

// The secret volume: undiscovered secret sectors as translucent panels, one per wall. Caps refuse rather than clamp: an
// over-cap sector gets one panel at the room's centre, not a truncated room.
const int TRIGVOL_WALLS_PER_SECTOR = 64, TRIGVOL_WALLS_MAX = 512;

inline bool secretvol_wanted(int lotag) { return trigger_sector_is_secret(lotag); }

inline int secretvol_curtain_angle(int wallAng) { return (wallAng + 512) & 2047; }

struct TrigWall { int sector, x0, y0, z0top, z0bot, x1, y1, z1top, z1bot; };

// How many of a sector's walls to stage: all if they fit both caps, none otherwise; wallnum <= 0 is malformed.
inline int trigvol_stage_plan(int wallnum, int perSectorCap, int staged, int totalCap, bool* fallback) {
  const bool fits = wallnum > 0 && wallnum <= perSectorCap && staged + wallnum <= totalCap;
  if (fallback) *fallback = !fits;
  return fits ? wallnum : 0;
}

// One panel at a point, across the angle: sintable[(ang+512)&2047] is the cosine at 16384 scale; rounded half up.
inline void trigvol_curtain(int px, int py, int ang, int halfWidth, int ztop, int zbot, const short* sintable, TrigWall* out) {
  if (!out || !sintable) return;
  const double rx = (double)sintable[ang & 2047] / 16384.0, ry = -(double)sintable[(ang + 512) & 2047] / 16384.0;
  out->sector = -1;
  out->x0 = (int)floor(px + rx * halfWidth + 0.5); out->y0 = (int)floor(py + ry * halfWidth + 0.5);
  out->x1 = (int)floor(px - rx * halfWidth + 0.5); out->y1 = (int)floor(py - ry * halfWidth + 0.5);
  out->z0top = ztop; out->z0bot = zbot; out->z1top = ztop; out->z1bot = zbot;
}

inline bool trigvol_quad_corners(const TrigWall& w, WorldXY* a, WorldXY* b, int* zaTop, int* zaBot, int* zbTop, int* zbBot) {
  if (!a || !b || !zaTop || !zaBot || !zbTop || !zbBot) return false;
  if (w.z0top > w.z0bot || w.z1top > w.z1bot) return false;             // z grows downward: top is the smaller
  if (w.x0 == w.x1 && w.y0 == w.y1) return false;                       // zero length: no wall to draw
  a->x = w.x0; a->y = w.y0; b->x = w.x1; b->y = w.y1;
  *zaTop = w.z0top; *zaBot = w.z0bot; *zbTop = w.z1top; *zbBot = w.z1bot;
  return true;
}

void secret_volume_tic();

// Render-clock producer, only from world_draw_frame's producer slot after esp_tint_collect().
void secret_volume_collect();

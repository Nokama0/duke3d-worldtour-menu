#include "target.h"
#include "common.h"
#include <math.h>

// The cstat half of aim()'s test (VA 0x004f3010), shared so one mutated mask breaks both rules.
static bool cstat_shootable(int cstat) {
  return (cstat & SPR_CSTAT_SHOOT_MASK) == SPR_CSTAT_SHOOT_WANT;
}

// The engine's rule (aim()) minus the health term: xrepeat != 0 && (cstat & 0x8101) == 0x101.
bool target_hittable_rule(int xrepeat, int cstat) {
  return xrepeat != 0 && cstat_shootable(cstat);
}

// No health term, by design (live actors keep the death-state bits that would clear).
bool target_alive_rule(int statnum, bool badguy, int cstat) {
  const bool onActorList = (statnum == TARGET_STAT_ACTOR ||
                            statnum == TARGET_STAT_ZOMBIEACTOR);
  return onActorList && badguy && cstat_shootable(cstat);
}

int target_hp_for_display(int extra) { return extra > 0 ? extra : 0; }

// bx/by/bz: hittype[i]'s previous-tic position, so the difference is the per-tic velocity.
bool target_velocity_from(int x, int y, int z, int bx, int by, int bz, int* vx, int* vy, int* vz) {
  if (!vx || !vy || !vz) return false;
  *vx = x - bx;
  *vy = y - by;
  *vz = z - bz;
  return true;
}

static const int    kUnitsFullCircle = 2048;
static const double kPi              = 3.14159265358979323846;

// atan2 as a Build angle 0..2047 (0 = +x, 512 = +y); `& 2047` on a negative operand is the engine's own wrap.
static int build_angle_from(long long dx, long long dy) {
  double angRad = atan2((double)dy, (double)dx);
  long long araw = (long long)llround(angRad * (kUnitsFullCircle / 2) / kPi);
  return clamp_ll_to_int(araw & (long long)(kUnitsFullCircle - 1));
}

// vel: engine xy-units per tic; horizMul: the pitch-formula divisor.
WeaponInfo target_weapon_info(int currWeapon) {
  static const WeaponInfo table[] = {
    { 0,  WC_MELEE,      0,   0 },
    { 1,  WC_HITSCAN,    0,  32 },
    { 2,  WC_HITSCAN,    0,  32 },
    { 3,  WC_HITSCAN,    0,  32 },
    { 4,  WC_PROJECTILE, 644, 81 },  // rpg (vel confirmed VA 0x004f5105)
    { 5,  WC_NONE,       0,   0 },   // pipebomb: lobbed ordnance, no aim assist
    // Shrinker: fixed vel = 768 (handler 0x9fc), pitch multiplier unread; WC_NONE keeps aim_tic from publishing a
    // target, so silent aim's 0x9fc row stays unreachable until it is read.
    { 6,  WC_NONE,       0,   0 },
    { 7,  WC_PROJECTILE, 644, 81 },  // devastator fires RPG-family projectiles
    { 8,  WC_NONE,       0,   0 },   // tripbomb: wall placement, not a targeted shot
    { 9,  WC_PROJECTILE, 644, 81 },  // freezer (FREEZEBLAST shares RPG's handler)
    { 10, WC_NONE,       0,   0 },   // handremote: no projectile at all
    // Expander: hitscan, atwith GROWSPARK 0x990 (VA 0x004fe956); the seven shoot(i, 0xa35) calls at
    // 0x004fe58c-0x004fe602 are the SHOTGUN's pellets.
    { 11, WC_HITSCAN,    0,  32 },
  };
  const int n = (int)(sizeof(table) / sizeof(table[0]));
  if (currWeapon < 0 || currWeapon >= n) {
    WeaponInfo unknown = { currWeapon, WC_NONE, 0, 0 };
    return unknown;
  }
  return table[currWeapon];
}

// Sprite height is 4h (h = yrepeat*tilesizy): centre (k=2) is mid-body, upper (k=3) three-quarters up.
int target_aim_z(int spriteZ, int yrepeat, int tilesizy, AimPoint ap) {
  long long h = (long long)yrepeat * (long long)tilesizy;
  long long k = (ap == AP_UPPER) ? 3 : 2;
  return clamp_ll_to_int((long long)spriteZ - k * h);
}

int target_angular_offset(int viewAng, int dx, int dy) {
  int a = build_angle_from(dx, dy);
  int d = (a - viewAng) & (kUnitsFullCircle - 1);
  return d > kUnitsFullCircle / 2 ? kUnitsFullCircle - d : d;
}

bool target_candidate_healthy(int extra) { return extra > 0; }

// One tier of the scan. The cone test is INSIDE the tier: a healthy candidate outside the cone must not block an
// unhealthy one inside it.
static int pick_in_tier(const TargetCandidate* cands, int count, int viewAng,
                        int eyex, int eyey, TargetMode mode, int crosshairConeUnits,
                        bool healthyOnly) {
  int best = -1;
  int bestOffset = 0;
  int bestDist = 0;
  for (int i = 0; i < count; ++i) {
    const TargetCandidate& c = cands[i];
    if (healthyOnly && !target_candidate_healthy(c.extra)) continue;
    if (mode == TM_CROSSHAIR) {
      int off = target_angular_offset(viewAng, c.x - eyex, c.y - eyey);
      if (off > crosshairConeUnits) continue;
      if (best == -1 || off < bestOffset || (off == bestOffset && c.dist2d < bestDist)) {
        best = c.index;
        bestOffset = off;
        bestDist = c.dist2d;
      }
    } else {
      if (best == -1 || c.dist2d < bestDist) {
        best = c.index;
        bestDist = c.dist2d;
      }
    }
  }
  return best;
}

// Healthy first, everything second: a lone dying enemy is still a target.
int target_pick(const TargetCandidate* cands, int count, int viewAng, int eyex, int eyey, TargetMode mode, int crosshairConeUnits) {
  if (!cands || count <= 0) return -1;
  const int healthy = pick_in_tier(cands, count, viewAng, eyex, eyey, mode,
                                   crosshairConeUnits, true);
  // `>= 0`, not `> 0`: sprite index 0 is a real answer.
  if (healthy >= 0) return healthy;
  return pick_in_tier(cands, count, viewAng, eyex, eyey, mode,
                      crosshairConeUnits, false);
}

// Two fixed iterations of t = dist2d(P + V*t, S) / vel; vel <= 0 (hitscan) returns P. Distance is 2D: vel is xy-units
// per tic and Build z is 16x finer (shoot picks zvel as dz*vel/dist2d).
Point3 target_lead(Point3 shooter, Point3 p, int vx, int vy, int vz, int vel) {
  if (vel <= 0) return p;

  double sx = (double)shooter.x, sy = (double)shooter.y;
  double px = (double)p.x, py = (double)p.y, pz = (double)p.z;
  double v = (double)vel;

  double dx0 = px - sx, dy0 = py - sy;
  double t0 = sqrt(dx0 * dx0 + dy0 * dy0) / v;

  double lx0 = px + vx * t0, ly0 = py + vy * t0;
  double dx1 = lx0 - sx, dy1 = ly0 - sy;
  double t1 = sqrt(dx1 * dx1 + dy1 * dy1) / v;

  Point3 out;
  out.x = clamp_double_to_int(px + vx * t1);
  out.y = clamp_double_to_int(py + vy * t1);
  out.z = clamp_double_to_int(pz + vz * t1);
  return out;
}

// Clamped here too, not just at config load (else it would extrapolate). long long: `led - raw` can overflow int.
Point3 target_lead_scaled(Point3 raw, Point3 led, int scalePct) {
  const long long pct = (long long)aim_leadscale_clamped(scalePct);
  Point3 out;
  out.x = clamp_ll_to_int((long long)raw.x + ((long long)led.x - raw.x) * pct / 100);
  out.y = clamp_ll_to_int((long long)raw.y + ((long long)led.y - raw.y) * pct / 100);
  out.z = clamp_ll_to_int((long long)raw.z + ((long long)led.z - raw.z) * pct / 100);
  return out;
}

// The one-tic advance (target.h): the shot leaves from a position the solve hasn't seen.

// long long before the subtraction: pos and opos are independent live reads and INT_MAX - INT_MIN would wrap to -1.
// The cap is inclusive: x/y use AIM_AHEAD_MAX_STEP, z (16x finer) AIM_AHEAD_MAX_STEP_Z.
int aim_ahead_step(int pos, int opos, int cap) {
  const long long step = (long long)pos - (long long)opos;
  if (step > (long long)cap) return 0;
  if (step < -(long long)cap) return 0;
  return (int)step;
}

Point3 aim_ahead_origin(Point3 eye, int dx, int dy, int dz) {
  Point3 out;
  out.x = clamp_ll_to_int((long long)eye.x + (long long)dx);
  out.y = clamp_ll_to_int((long long)eye.y + (long long)dy);
  out.z = clamp_ll_to_int((long long)eye.z + (long long)dz);
  return out;
}

// Aim point plus one tic of smoothed velocity (already per-tic: one addition, not a second lead).
Point3 aim_ahead_target(Point3 raw, int svx, int svy, int svz) {
  Point3 out;
  out.x = clamp_ll_to_int((long long)raw.x + (long long)svx);
  out.y = clamp_ll_to_int((long long)raw.y + (long long)svy);
  out.z = clamp_ll_to_int((long long)raw.z + (long long)svz);
  return out;
}

// Wrapped signed yaw error in [-1024, +1023]. `ang` is live memory and `%` keeps the sign of its left operand, so it is
// corrected into range.
int aim_shot_angerr(int ang, int exact) {
  long long m = ((long long)ang - (long long)exact + 1024) % 2048;
  if (m < 0) m += 2048;
  return (int)(m - 1024);
}

static int ray_sin(int ang, const short* sintable) { return sintable[ang & 2047]; }
static int ray_cos(int ang, const short* sintable) { return sintable[(ang + 512) & 2047]; }

// Widened: the difference can overflow int and the 16384 multiply adds 14 bits.
int shot_lateral(int ox, int oy, int ang, int sx, int sy, const short* sintable) {
  if (!sintable) return 0;
  const long long dx = (long long)sx - (long long)ox;
  const long long dy = (long long)sy - (long long)oy;
  return clamp_ll_to_int((dx * ray_sin(ang, sintable) - dy * ray_cos(ang, sintable)) / 16384);
}

int shot_forward(int ox, int oy, int ang, int sx, int sy, const short* sintable) {
  if (!sintable) return 0;
  const long long dx = (long long)sx - (long long)ox;
  const long long dy = (long long)sy - (long long)oy;
  return clamp_ll_to_int((dx * ray_cos(ang, sintable) + dy * ray_sin(ang, sintable)) / 16384);
}

// fwd*vz can overflow alone (vz ~3.3 million looking straight down).
int shot_vertical(int sz, int vz, int fwd, int targetZ) {
  const long long rayZ = (long long)sz + (long long)fwd * (long long)vz / 16384;
  return clamp_ll_to_int((long long)targetZ - rayZ);
}

// engine.c's hitscan accepts a face sprite within tilesizx*xrepeat/sqrt(128) of the ray (1000/11314 ~ 1/sqrt(128)); the
// product is clamped so a hostile pair saturates.
int shot_hitscan_halfw(int tilesizx, int xrepeat) {
  long long prod = (long long)tilesizx * (long long)xrepeat;
  const long long safeMax = 9223372036854775807LL / 1000;
  const long long safeMin = (-9223372036854775807LL - 1LL) / 1000;
  if (prod > safeMax) prod = safeMax;
  if (prod < safeMin) prod = safeMin;
  return clamp_ll_to_int(prod * 1000 / 11314);
}

// Integer arithmetic (VA 0x004f424f hitscan, 0x004f520f projectile): 644/81 truncates to 7. `dist` is the EXACT 2D
// distance, not `shoot`'s octagonal `ldist` (~3.9% short on-axis), so this is not a byte-exact mirror.
AimSolution target_solve(Point3 eye, Point3 aim, int horizoff, const WeaponInfo& w) {
  AimSolution r;
  r.ang = 0;
  r.horiz = 0;
  r.valid = false;

  long long dx = (long long)aim.x - (long long)eye.x;
  long long dy = (long long)aim.y - (long long)eye.y;
  long long dz = (long long)aim.z - (long long)eye.z;

  long long dist = (long long)clamp_double_to_int(sqrt((double)(dx * dx + dy * dy)));
  if (dist < 1) return r;

  r.ang = build_angle_from(dx, dy);

  long long zvel, horiz;
  if (w.cls == WC_PROJECTILE) {
    if (w.horizMul == 0) return r;
    zvel = (dz * (long long)w.vel) / dist;
    horiz = 100 - horizoff - zvel / w.horizMul;
  } else {
    // Hitscan (and the default): horiz = 100 - horizoff - 8*dz/dist as the engine stages it. `dz * 256`, not `<< 8`
    // (UB on a negative before C++20).
    zvel = (dz * 256) / dist;
    horiz = 100 - horizoff - zvel / 32;
  }
  r.horiz = clamp_ll_to_int(horiz);
  r.valid = true;
  return r;
}

// step is the fraction of the wrapped delta NOT damped by smoothPct (0 snaps, 100 applies none). It truncates toward
// zero, so a nonzero delta moves at least 1 unit, except at pct == 100: contractually "never moves" (target.h).
int target_smooth_ang(int current, int desired, int smoothPct) {
  int pct = clamp_int(smoothPct, 0, 100);
  int delta = ((desired - current + kUnitsFullCircle / 2) & (kUnitsFullCircle - 1)) - kUnitsFullCircle / 2;
  int step = delta * (100 - pct) / 100;
  if (step == 0 && delta != 0 && pct < 100) step = (delta > 0) ? 1 : -1;
  return (current + step) & (kUnitsFullCircle - 1);
}

// Same rule as target_smooth_ang; horiz has no wraparound.
int target_smooth_horiz(int current, int desired, int smoothPct) {
  int pct = clamp_int(smoothPct, 0, 100);
  int delta = desired - current;
  int step = delta * (100 - pct) / 100;
  if (step == 0 && delta != 0 && pct < 100) step = (delta > 0) ? 1 : -1;
  return current + step;
}

bool target_within_redirect(int currentAng, int desiredAng, int maxUnits) {
  int delta = ((desiredAng - currentAng + kUnitsFullCircle / 2) & (kUnitsFullCircle - 1)) - kUnitsFullCircle / 2;
  int absDelta = delta < 0 ? -delta : delta;
  return absDelta <= maxUnits;
}

// Rounded via the +180 bias; long long so a hostile degrees value cannot overflow.
int target_degrees_to_units(int degrees) {
  long long v = ((long long)degrees * kUnitsFullCircle + 180) / 360;
  return clamp_ll_to_int(v);
}

// The four absolute refusals first (panel owning input, dead player, untrusted read, viewscreen camera); only then hold
// mode. buttonDown is ignored in always-on mode.
bool aim_should_run(bool on, bool hold, bool buttonDown, bool menuOpen, bool dead, bool valid,
                    bool cameraOn) {
  if (!on || menuOpen || dead || !valid || cameraOn) return false;
  if (hold) return buttonDown;
  return true;
}

// Plain conjunction so the suite proves which six terms are consulted. `los` keeps a wall-hidden enemy from getting the
// marker; `targetable` keeps a dying body from stealing the aim point.
bool aim_candidate_ok(bool badguy, bool alive, bool inRange, bool los, bool reachable,
                      bool targetable) {
  return badguy && alive && inRange && los && reachable && targetable;
}

// Widened to double before the squares (int dx*dx overflows within a legal map); saturates instead of wrapping negative.
int aim_dist2d(int dx, int dy) {
  const double x = (double)dx, y = (double)dy;
  return clamp_double_to_int(sqrt(x * x + y * y));
}

// The seqlock. Both writers RESTORE the parity (`(v + 1) | 1` stays odd from odd, `& ~1` even), so a counter left odd
// by an unfinished writer is repaired by the next complete cycle, which a bare `++` would invert.
void snap_begin(unsigned* seq) { if (seq) *seq = (*seq + 1u) | 1u; }
void snap_end(unsigned* seq)   { if (seq) *seq = (*seq + 1u) & ~1u; }

// A reader accepts only a copy bracketed by the SAME even counter.
bool snap_read_ok(unsigned before, unsigned after) {
  return before == after && (before & 1u) == 0u;
}

// Room: the next free slot. Full: the slot holding the largest dist2d, if the newcomer is strictly nearer (a tie never
// churns). `count >= cap` so an over-counted caller still replaces.
int aim_keep_index(const TargetCandidate* cands, int count, int cap, int dist2d) {
  if (!cands || cap <= 0 || count < 0) return -1;
  if (count < cap) return count;
  int worst = 0;
  for (int k = 1; k < cap; k++)
    if (cands[k].dist2d > cands[worst].dist2d) worst = k;
  return (dist2d < cands[worst].dist2d) ? worst : -1;
}

bool aim_weapon_usable(WeaponClass cls) {
  return cls == WC_HITSCAN || cls == WC_PROJECTILE;
}

// Compared against the one value naming the alternative, not zero: an unrecognised setting is not evidence for it.
TargetMode aim_mode_from_config(int aimtarget) {
  return (aimtarget == 1) ? TM_CLOSEST : TM_CROSSHAIR;
}
AimPoint aim_point_from_config(int aimpoint) {
  return (aimpoint == 1) ? AP_UPPER : AP_CENTRE;
}

// The engine's envelope from processinput (four instructions above AIM_HORIZ_MIN in target.h).
int aim_horiz_clamped(int horiz) { return clamp_int(horiz, AIM_HORIZ_MIN, AIM_HORIZ_MAX); }

// One tic of the view move. The envelope applies to both pitch endpoints before any subtraction (`desired - current`
// could overflow on a garbage read); a clamped wantHoriz eases an unreachable solve to the range edge.
bool aim_next_view(int curAng, int curHoriz, int wantAng, int wantHoriz, int smoothPct,
                   int* outAng, int* outHoriz) {
  if (!outAng || !outHoriz) return false;
  *outAng = target_smooth_ang(curAng, wantAng, smoothPct);
  *outHoriz = target_smooth_horiz(aim_horiz_clamped(curHoriz), aim_horiz_clamped(wantHoriz), smoothPct);
  return true;
}

// Two absolute caps, one per axis the engine moves differently (target.h); long long so a garbage INT_MIN spikes.
bool aim_vel_spike(int vx, int vy, int vz) {
  const double x = (double)vx, y = (double)vy;
  const double step = sqrt(x * x + y * y);
  if (step > (double)AIM_VEL_MAX_STEP) return true;
  const long long z = (long long)vz;
  return (z > (long long)AIM_VEL_MAX_FALL) || (z < -(long long)AIM_VEL_MAX_FALL);
}

void aim_vel_reset(VelSmooth* s) {
  if (!s) return;
  s->index = -1;
  s->picnum = 0;
  s->samples = 0;
  s->qx = 0; s->qy = 0; s->qz = 0;
}

// One EMA axis in the scaled domain: q moves NUM/DEN of the way to the sample; the scale stops a small delta truncating
// to zero short of the mean. int suffices (samples bounded by AIM_VEL_MAX_FALL, 6144).
static int vel_fold(int q, int sample) {
  return q + ((sample * AIM_VEL_SCALE - q) * AIM_VEL_EMA_NUM) / AIM_VEL_EMA_DEN;
}

bool aim_vel_update(VelSmooth* s, int index, int picnum, int vx, int vy, int vz,
                    int* outvx, int* outvy, int* outvz) {
  if (!s || !outvx || !outvy || !outvz) return false;

  const bool spike = aim_vel_spike(vx, vy, vz);

  if (s->index != index || s->picnum != picnum) {
    // A different actor: seed with the sample so the first tic leads by its own motion; a spike seeds zero, not a teleport.
    aim_vel_reset(s);
    s->index = index;
    s->picnum = picnum;
    if (!spike) {
      s->qx = vx * AIM_VEL_SCALE;
      s->qy = vy * AIM_VEL_SCALE;
      s->qz = vz * AIM_VEL_SCALE;
      s->samples = 1;
    }
  } else if (!spike) {
    s->qx = vel_fold(s->qx, vx);
    s->qy = vel_fold(s->qy, vy);
    s->qz = vel_fold(s->qz, vz);
    // Saturates at the window: past it the count only says "settled", and an unbounded one could overflow in a hook.
    if (s->samples < AIM_VEL_EMA_TICS) s->samples++;
  }
  // A spike on an established target changes nothing, so teleports cannot age the state.

  *outvx = s->qx / AIM_VEL_SCALE;
  *outvy = s->qy / AIM_VEL_SCALE;
  *outvz = s->qz / AIM_VEL_SCALE;
  return true;
}

// Read-only half of aim_vel_update: same identity and division, so a mid-tic reader sees the same average.
bool vel_smooth_read(const VelSmooth* s, int index, int picnum,
                     int* outvx, int* outvy, int* outvz) {
  if (!s || !outvx || !outvy || !outvz) return false;
  if (s->index != index || s->picnum != picnum) return false;
  *outvx = s->qx / AIM_VEL_SCALE;
  *outvy = s->qy / AIM_VEL_SCALE;
  *outvz = s->qz / AIM_VEL_SCALE;
  return true;
}

int aim_lead_vel(int weaponVel, int aimlead) { return aimlead != 0 ? weaponVel : 0; }

// shoot's own sz (VA 0x004f36c0-0x004f36d3): posz + pyoff + 0x400, widened and saturated.
int aim_shot_origin_z(int posz, int pyoff) {
  return clamp_ll_to_int((long long)posz + (long long)pyoff + (long long)AIM_SHOT_Z_BIAS);
}

// One rung of the ladder, preference first: configured aim point, upper body, head, centre last (target.h has the order
// and the head's quarter-h margin). Indexed so silent.cpp's re-solve need not re-walk cansee.
int target_probe_z(int spriteZ, int yrepeat, int tilesizy, AimPoint ap, int rung) {
  const long long h = (long long)yrepeat * (long long)tilesizy;
  switch (rung) {
    case 1: return clamp_ll_to_int((long long)spriteZ - 3 * h);
    case 2: return clamp_ll_to_int((long long)spriteZ - 4 * h + h / 4);
    // The centre, last resort for an enemy under an overhang (target.h).
    case 3: return clamp_ll_to_int((long long)spriteZ - 2 * h);
    // Rung 0 and anything out of range (including the -1 "no probe visible" sentinel): the configured aim point.
    default: return target_aim_z(spriteZ, yrepeat, tilesizy, ap);
  }
}

// Duplicates dropped, not probed twice: AP_CENTRE and AP_UPPER both come out at three distinct probes.
int aim_probe_zs(int spriteZ, int yrepeat, int tilesizy, AimPoint ap, int* out, int cap) {
  if (!out || cap < 1) return 0;
  const int ladder[AIM_PROBE_MAX] = {
    target_probe_z(spriteZ, yrepeat, tilesizy, ap, 0),
    target_probe_z(spriteZ, yrepeat, tilesizy, ap, 1),
    target_probe_z(spriteZ, yrepeat, tilesizy, ap, 2),
    target_probe_z(spriteZ, yrepeat, tilesizy, ap, 3),
  };
  int n = 0;
  for (int i = 0; i < AIM_PROBE_MAX && n < cap; i++) {
    bool dup = false;
    for (int k = 0; k < n; k++) if (out[k] == ladder[i]) { dup = true; break; }
    if (!dup) out[n++] = ladder[i];
  }
  return n;
}

// First, not best: aim_probe_zs orders lowest-first, so the first visible probe exposes the most body.
int aim_first_visible(const bool* visible, int count) {
  if (!visible || count <= 0) return -1;
  for (int i = 0; i < count; i++) if (visible[i]) return i;
  return -1;
}

bool aim_horiz_reachable(int horiz) {
  return horiz >= AIM_HORIZ_MIN && horiz <= AIM_HORIZ_MAX;
}

// Post-lead fallback: led first, raw only when led is unusable; ASC_NONE when neither works, never a clamped pitch.
AimSolutionChoice aim_choose_solution(bool ledValid, int ledHoriz, bool rawValid, int rawHoriz) {
  if (ledValid && aim_horiz_reachable(ledHoriz)) return ASC_LED;
  if (rawValid && aim_horiz_reachable(rawHoriz)) return ASC_RAW;
  return ASC_NONE;
}

// The same four absolute refusals as aim_should_run, restated because silentOn carries none of them.
bool aim_acquire_should_run(bool aimbotWants, bool silentOn, bool menuOpen,
                            bool dead, bool valid, bool cameraOn) {
  if (menuOpen || dead || !valid || cameraOn) return false;
  return aimbotWants || silentOn;
}


// A set, matched exactly, never a range: 0x9d9 (knee) sits just below 0x9e8, so a range would pull melee in with the
// chaingun. One entry per player weapon that fires an aimed shot.
bool silent_atwith_is_player_shot(int atwith) {
  switch (atwith) {
    case 0xa23:   // SHOTSPARK1,  VA 0x004fe458            pistol (kb == 1)
    case 0xa35:   // SHOTGUN,     VA 0x004fe58c x7         shotgun (kb == 4)
    case 0x9e8:   // CHAINGUN,    VA 0x004fe876            chaingun
    case 0xa2d:   // RPG,         VA 0x004fea65, 0x004fecf8  rpg and devastator
    // Inert while the shrinker is WC_NONE (no published target); live once its pitch multiplier is read.
    case 0x9fc:   // SHRINKSPARK, VA 0x004fe9cf            shrinker (ammo[6])
    case 0x669:   // FREEZEBLAST, VA 0x004feb2b            freezer
    case 0x990:   // GROWSPARK,   VA 0x004fe956            expander (ammo[11])
      return true;
    default:
      return false;
  }
}

bool silent_snapshot_usable(int targetIndex, int solvedAng, int solvedHoriz) {
  if (targetIndex < 0) return false;
  if (solvedAng < 0 || solvedAng >= kUnitsFullCircle) return false;
  // Through the function the walk and fallback gate on, not a second pair of pitch literals.
  return aim_horiz_reachable(solvedHoriz);
}

// Six terms, one conjunction so no edit can slip a term ahead of the refusals. The cone (the only term costing an
// arithmetic wrap) is last, so it is skipped on a refused call.
bool silent_decide(bool on, bool isLocalPlayerSprite, bool atwithIsPlayerShot,
                   bool haveTarget, bool los, int curAng, int wantAng, int maxUnits) {
  return on
      && isLocalPlayerSprite
      && atwithIsPlayerShot
      && haveTarget
      && los
      && target_within_redirect(curAng, wantAng, maxUnits);
}

// hooked_shoot runs before aim_tic publishes this tic's solve, so the snapshot is one tic old. Acquisition stays the
// snapshot's; only the final angle is recomputed from the target's position at the instant of the shot.
SilentResolve silent_resolve(Point3 eye, int horizoff, const WeaponInfo& w,
                             int snapAng, int snapHoriz,
                             Point3 liveRaw, int vx, int vy, int vz,
                             int leadVel, int leadScalePct,
                             int curAng, int maxRedirectUnits) {
  SilentResolve r;
  r.ang = snapAng; r.horiz = snapHoriz; r.valid = true; r.live = false;
  r.coneRejected = false;
  // Same calls, order and scale as the aimbot, so the redirect and the calibration marker agree on the lead point.
  const Point3 led = target_lead(eye, liveRaw, vx, vy, vz, leadVel);
  const Point3 aim = target_lead_scaled(liveRaw, led, leadScalePct);
  const AimSolution sol = target_solve(eye, aim, horizoff, w);
  // A degenerate solve or a pitch outside the envelope is no improvement on a one-tic-old, reach-checked answer.
  if (!sol.valid || !aim_horiz_reachable(sol.horiz)) return r;
  // Re-apply the redirect cone silent_decide gated on, or a close, fast target's live angle could exceed Max redirect.
  if (!target_within_redirect(curAng, sol.ang, maxRedirectUnits)) {
    r.coneRejected = true;
    return r;
  }
  r.ang = sol.ang; r.horiz = sol.horiz; r.live = true;
  return r;
}

// The engine's not-falling test, widened (AIM_FALL_FLOOR_OFFSET). z points down: at or below floorz-margin is on the
// floor.
bool aim_actor_grounded(int z, int floorz, int margin) {
  return (long long)z >= (long long)floorz - (long long)margin;
}

// A grounded actor contributes no vertical velocity (what was measured was its gravity-and-snap cycle); anything above
// its floor keeps it, so a flying RECON keeps its vz.
int aim_grounded_vz(int z, int floorz, int vz, int margin) {
  return aim_actor_grounded(z, floorz, margin) ? 0 : vz;
}

// Unsigned throughout, so a GetTickCount wrap is a small difference, not a stall. `first` forces the first call.
bool aim_throttle_due(unsigned nowMs, unsigned lastMs, bool first, unsigned periodMs) {
  if (first) return true;
  return (unsigned)(nowMs - lastMs) >= periodMs;
}

bool aim_lost_should_log(bool presentLast, bool presentNow, bool picnumSame, int xrepeat,
                         bool alreadyLoggedThisWindow) {
  return presentLast && !presentNow && picnumSame && xrepeat != 0 && !alreadyLoggedThisWindow;
}

// The presence set must not outlive its level. OR, not AND: either signal alone suffices (target.h); the signature goes
// first as two integer compares settle the common case without the clock.
bool aim_presence_reset(int prevPlayerSprite, int prevNumsectors,
                        int playerSprite, int numsectors,
                        unsigned nowMs, unsigned lastMs, unsigned maxGapMs) {
  if (prevPlayerSprite != playerSprite) return true;
  if (prevNumsectors != numsectors) return true;
  // Through aim_throttle_due so a GetTickCount wrap behaves as there.
  return aim_throttle_due(nowMs, lastMs, false, maxGapMs);
}

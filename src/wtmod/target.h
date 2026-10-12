// Megaton's target.h except AIM_PROBE_MAX 3 -> 4 (centre rung appended: aimpoint=upper collapses rungs 0 and 1).
#pragma once
// Targeting core. Pure: links into test_host; engine.cpp feeds it engine reads.

// LIVENESS is the engine's own aim() (VA 0x004f3010) minus its health term, not `extra > 0` (live actors sit at extra
// -9 with cstat 0x0101): status lists {1, 2}, badguy(), `xrepeat != 0 && (cstat & 0x8101) == 0x101`. An actor keeps its
// box through its death animation until the CON clears cstat.
const int SPR_CSTAT_SHOOT_MASK = 0x8101;
const int SPR_CSTAT_SHOOT_WANT = 0x0101;

// engine.h's STAT_ACTOR/STAT_ZOMBIEACTOR, restated since this header is pure; test_host pins the pairs.
const int TARGET_STAT_ACTOR       = 1;
const int TARGET_STAT_ZOMBIEACTOR = 2;

bool target_hittable_rule(int xrepeat, int cstat);

// `badguy` is passed in since it is native code this pure module cannot call.
bool target_alive_rule(int statnum, bool badguy, int cstat);

// Clamped at 0 for display; an actor kept alive by the rule above can sit at -7.
int target_hp_for_display(int extra);

// Per-tic velocity = position minus previous-tic position (hittype[].bpos*).
bool target_velocity_from(int x, int y, int z, int bx, int by, int bz, int* vx, int* vy, int* vz);

// Speeds and multipliers are binary reads.
enum WeaponClass { WC_NONE = 0, WC_HITSCAN = 1, WC_PROJECTILE = 2, WC_MELEE = 3 };
struct WeaponInfo { int weaponIndex; WeaponClass cls; int vel; int horizMul; };
// Duke curr_weapon indices: 0 knee, 1 pistol, 2 shotgun, 3 chaingun, 4 rpg, 5 pipebomb,
// 6 shrinker, 7 devastator, 8 tripbomb, 9 freezer, 10 handremote, 11 expander (Atomic).
WeaponInfo target_weapon_info(int currWeapon);
// h = yrepeat * tilesizy from the sprite's feet z: centre = z - 2h, upper = z - 3h.
enum AimPoint { AP_CENTRE = 0, AP_UPPER = 1 };
int target_aim_z(int spriteZ, int yrepeat, int tilesizy, AimPoint ap);

// `picnum` rides alongside `index`: a Build sprite slot is recycled the instant its actor dies.
struct TargetCandidate {
  int index; int picnum; int x, y, z; int vx, vy, vz; int aimz; int sectnum; int dist2d;
  int aimzRung;       // which of target_probe_z's rungs produced `aimz` (-> AimSnapshot::targetRung)
  int floorz; bool grounded;   // hittype[].floorz and grounded verdict, for the diagnostic
  int extra;          // health: target_pick's tiebreak, not liveness
};

// Health is a preference tier for target_pick, never a filter: a dying actor keeps cstat 0x0101 until its CON clears it
// and looks like "low health", so filtering on `extra > 0` brings back vanishing enemies.
bool target_candidate_healthy(int extra);

enum TargetMode { TM_CROSSHAIR = 0, TM_CLOSEST = 1 };
// Build units 0..1024 (wrapped, absolute).
int target_angular_offset(int viewAng, int dx, int dy);
// Best index in cands[] by mode, -1 when none. Two tiers: the healthy set first, all candidates only if it is empty.
// Within a tier: smallest angular offset inside the cone (ties by distance), or smallest distance for TM_CLOSEST.
int target_pick(const TargetCandidate* cands, int count, int viewAng, int eyex, int eyey, TargetMode mode, int crosshairConeUnits);

// Two fixed iterations of t = dist(P + V*t, S) / vel; P unchanged for vel <= 0 (hitscan).
struct Point3 { int x, y, z; };
Point3 target_lead(Point3 shooter, Point3 p, int vx, int vy, int vz, int vel);

// A full lead misses because the CON stops and restarts actors constantly. `scalePct` moves the aim point that fraction
// from raw to fully led, all axes alike so a climbing target is not hit at the wrong height.
Point3 target_lead_scaled(Point3 raw, Point3 led, int scalePct);

// Bounds shared with config.cpp's load clamp, so a hand-edited ini cannot bypass them.
const int AIM_LEADSCALE_MIN     = 0;
const int AIM_LEADSCALE_MAX     = 100;
const int AIM_LEADSCALE_DEFAULT = 70;
int aim_leadscale_clamped(int pct);

// The aimbot writes after processinput calls shoot, so the shot flies along last tic's solve: the solve runs ONE TIC
// AHEAD. Silent aim re-solves at shot time.

// Largest per-tic step predicted through, Build XY units (the _Z cap is 16x finer). Measured steps reached ~230/tic;
// 1024 (one grid square) is never legitimate, so a step at or above it is dropped.
const int AIM_AHEAD_MAX_STEP = 1024;

// The player's own motion `pos - opos`, widened to long long, 0 above `cap`. z callers pass _Z: an xy cap rejects a
// falling player's largest steps.
int aim_ahead_step(int pos, int opos, int cap);

// Eye plus this tic's step: where the player stands when `shoot` runs.
Point3 aim_ahead_origin(Point3 eye, int dx, int dy, int dz);

// The point read this tic is the enemy after moveactors(T-2) and the shot meets it after T-1, so add one tic of
// smoothed (already per-tic) velocity.
Point3 aim_ahead_target(Point3 raw, int svx, int svy, int svz);

// Signed diff between yaw faced and yaw that hits the published aim point, wrapped into [-1024, +1023] (5 vs 2040 is
// +13). long long, immune to hostile `ang`.
int aim_shot_angerr(int ang, int exact);

// `angerr` cannot say left/right/over; these decompose the shot ray (tbot_ray, 16384-scaled sintable) against the
// target's body.

// ((sx-ox)*sin - (sy-oy)*cos) / 16384. Positive = left of the ray (ang 0 faces +x, +y is right). Null sintable yields 0
// ("no offset measured").
int shot_lateral(int ox, int oy, int ang, int sx, int sy, const short* sintable);

// ((sx-ox)*cos + (sy-oy)*sin) / 16384. Negative behind the shooter is a real answer.
int shot_forward(int ox, int oy, int ang, int sx, int sy, const short* sintable);

// targetZ - (sz + fwd * vz / 16384): positive = target below the ray, i.e. the shot went over it.
int shot_vertical(int sz, int vz, int fwd, int targetZ);

// The engine's own hitscan half-width: Build accepts a face sprite when squared perpendicular distance <=
// (tilesizx*xrepeat)^2 >> 7; 1000/11314 approximates 1/sqrt(128) in integers.
int shot_hitscan_halfw(int tilesizx, int xrepeat);

// Decoupled from the ESP draw distance.
const int AIM_RANGE_MIN     = 512;
const int AIM_RANGE_MAX     = 32768;
const int AIM_RANGE_DEFAULT = AIM_RANGE_MAX;
int aim_range_clamped(int units);

inline bool aim_within_range(int dist2d, int aimrange) { return dist2d <= aimrange; }

// Pitch: hitscan horiz = 100 - horizoff - 8*dz/dist; projectile horiz = 100 - horizoff - (dz*vel/dist)/horizMul. dist
// is 2D and never 0: a zero-distance solve is invalid.
struct AimSolution { int ang; int horiz; bool valid; };
AimSolution target_solve(Point3 eye, Point3 aim, int horizoff, const WeaponInfo& w);

// Fraction of the remaining delta applied this tic: smoothPct 0 = snap, 100 = never moves.
int target_smooth_ang(int current, int desired, int smoothPct);      // wraps through the short way
int target_smooth_horiz(int current, int desired, int smoothPct);
// Silent aim gate: true when the wrapped |desired - current| <= maxUnits.
bool target_within_redirect(int currentAng, int desiredAng, int maxUnits);
int target_degrees_to_units(int degrees);    // 360 degrees = 2048 units, rounded

// menuOpen, dead, !valid and cameraOn (newowner >= 0) refuse absolutely; with a camera, processinput overwrites
// pos/ang from sprite[newowner] every tic anyway.
bool aim_should_run(bool on, bool hold, bool buttonDown, bool menuOpen, bool dead, bool valid,
                    bool cameraOn);

// Silent aim needs a target without a held button, when aim_should_run refuses. The absolute refusals are repeated.
bool aim_acquire_should_run(bool aimbotWants, bool silentOn, bool menuOpen,
                            bool dead, bool valid, bool cameraOn);

// HOLD VIEW: the engine adds the mouse after the aimbot's write and before `shoot` (10 of 30 moving misses in a 183-tic
// run), so the turn is removed as a full lock. wroteLastTic = aim_view_held().
inline bool aim_hold_view_rule(bool wroteLastTic, bool menuOpen, bool lockout) {
  return wroteLastTic && !menuOpen && !lockout;
}

// All SIX required; dropping los looks correct until an enemy behind a wall gets the marker. `alive` and `targetable`
// stay separate so a widened signature breaks callers.
bool aim_candidate_ok(bool badguy, bool alive, bool inRange, bool los, bool reachable,
                      bool targetable);

// `shoot` builds sz = posz + pyoff + 0x400 (VA 0x004f36c0), 64 xy-units below the eye. LOS runs from here (the eye
// passed a turret the muzzle cannot see); the SOLVE keeps the eye.
const int AIM_SHOT_Z_BIAS = 0x400;   // the 0x400 in shoot's own sz, VA 0x004f36cc
// Widened and saturated (pyoff is a live read).
int aim_shot_origin_z(int posz, int pyoff);

// Rungs: 0 the CHOSEN aim point, 1 upper body z-3h, 2 head z-4h+h/4, 3 centre z-2h. Silent aim re-solves at the rung
// acquisition chose, or it would aim at a point cansee rejected. Out of range (incl. -1, "none visible") answers like 0.
int target_probe_z(int spriteZ, int yrepeat, int tilesizy, AimPoint ap, int rung);

// Body points LOS is tested against (at most `cap` written, count returned; duplicates drop). ORDER IS THE DECISION:
// lowest visible point first, toward the head only when the body is covered; reversed it heads every visible enemy.
// The centre is last: cover blocks bottom-up, and it rescues aimpoint=upper.
const int AIM_PROBE_MAX = 4;
int aim_probe_zs(int spriteZ, int yrepeat, int tilesizy, AimPoint ap, int* out, int cap);

// First, not lowest: the ladder is PREFERENCE-ordered (at aimpoint=upper the last entry is the lowest). -1 for none.
int aim_first_visible(const bool* visible, int count);

// processinput clamps horiz to AIM_HORIZ_MIN..MAX, so an out-of-range solution (jetpack-height turret) is dropped as a
// candidate; clamping it sat the assist aiming at the wall below.
bool aim_horiz_reachable(int horiz);

// The lead can push a near-edge target out of reach; falling back to the raw point keeps the assist on the enemy
// without re-picking (which would reset the velocity smoother).
enum AimSolutionChoice { ASC_NONE = 0, ASC_LED = 1, ASC_RAW = 2 };
AimSolutionChoice aim_choose_solution(bool ledValid, int ledHoriz, bool rawValid, int rawHoriz);

// Widened before the squares: dx*dx overflows int inside a legal map.
int aim_dist2d(int dx, int dy);

// Degrees; config.cpp's load clamp calls the same function. 30: wide enough to acquire, narrow enough to stay what the
// player looks at.
const int AIM_CONE_MIN     = 1;
const int AIM_CONE_MAX     = 90;
const int AIM_CONE_DEFAULT = 30;
int aim_cone_clamped(int degrees);

const int AIM_SMOOTH_MIN = 0;
const int AIM_SMOOTH_MAX = 100;
// Beside its bounds because the Combat tab's reset glyph needs the same number config.cpp loads.
const int AIM_SMOOTH_DEFAULT = AIM_SMOOTH_MIN;
int aim_smooth_clamped(int pct);

// Seqlock from the TIC phase to the RENDER phase (one thread, different rates). Writer: snap_begin (odd), stores,
// snap_end (even). Reader: sample, copy, sample, accept via snap_read_ok if unchanged and even.
void snap_begin(unsigned* seq);
void snap_end(unsigned* seq);
bool snap_read_ok(unsigned before, unsigned after);

// A 16384-entry array would be 400 KB of stack in a hook.
const int AIM_MAX_CANDIDATES = 64;

// Keep-the-nearest: when full, replace the farthest slot only if the newcomer is strictly nearer, so overflow never
// drops the enemy in the player's face for one across the level. -1 rejects (null array, bad cap or count).
int aim_keep_index(const TargetCandidate* cands, int count, int cap, int dist2d);

// WC_NONE covers ordnance/placement weapons and the half-identified shrinker; WC_MELEE (the knee) has no aim point.
bool aim_weapon_usable(WeaponClass cls);

// An unknown value falls back to the default (crosshair, mid-body); config.cpp clamps both to 0/1.
TargetMode aim_mode_from_config(int aimtarget);
AimPoint   aim_point_from_config(int aimpoint);

// The engine clamps horiz to 299 at VA 0x004fd559 and -99 at 0x004fd577 (0x220 is rva::PS_OFFSET_HORIZ); the aimbot
// writes later, so needs its own clamp. Do NOT model the snap of 96..104 to 100 with ps[]+0x280 off (VA 0x004fd4da).
const int AIM_HORIZ_MIN = -99;
const int AIM_HORIZ_MAX = 299;
int aim_horiz_clamped(int horiz);

// Both pitch endpoints clamp first: `wantHoriz` eases an unreachable solution to the edge, `curHoriz` (a live read)
// cannot overflow the subtraction.
bool aim_next_view(int curAng, int curHoriz, int wantAng, int wantHoriz, int smoothPct,
                   int* outAng, int* outHoriz);

// Lead-velocity smoother: Duke's actors move in bursts, so `position - bpos` swings wildly even on a straight run.

// Horizontal per-tic step no real sprite exceeds: `ssp` (VA 0x00498b80) bounds steps by xvel, and the largest xvel in
// the weapon dispatch is 840 (SPIT/FIRELASER, VA 0x004f4d58). A larger step is a teleport, respawn or bad read.
const int AIM_VEL_MAX_STEP = 840;

// Vertical counterpart; `ssp` passes zvel unscaled and makeitfall clamps it at 0x1800 (VA 0x004dd179). Reusing the xy
// cap would reject every falling enemy, and a rejected sample drops the horizontal velocity too.
const int AIM_VEL_MAX_FALL = 6144;

// The same number as AIM_VEL_MAX_FALL, not a second literal: makeitfall's clamp bounds the player's fall as well.
const int AIM_AHEAD_MAX_STEP_Z = AIM_VEL_MAX_FALL;

// EMA window in tics, alpha = 2/(N+1). A 10-tic average still describes a run that ended a third of a second ago.
const int AIM_VEL_EMA_TICS = 6;
const int AIM_VEL_EMA_NUM  = 2;
const int AIM_VEL_EMA_DEN  = AIM_VEL_EMA_TICS + 1;

// The average is kept multiplied by this so a small step cannot truncate to zero. Applied with * and /: shifting a
// negative signed operand is UB before C++20.
const int AIM_VEL_SCALE = 256;

// Absolute, not relative: the bursty motion IS made of samples several times the mean ((0,0,0,300) has mean 75), so a
// relative test would reject the signal and converge to zero.
bool aim_vel_spike(int vx, int vy, int vz);

// Keyed by sprite index AND picnum so a dead trooper's velocity does not leak into whatever respawns in its slot.
// `samples` saturates at AIM_VEL_EMA_TICS.
struct VelSmooth { int index; int picnum; int samples; int qx, qy, qz; };

// Called on target loss so re-acquisition starts from the new enemy's motion.
void aim_vel_reset(VelSmooth* s);

// A DIFFERENT (index, picnum) resets and seeds with the sample (a spike as first sample seeds ZERO); a SPIKE is
// dropped, state unchanged; else folded at alpha = NUM/DEN. Division truncates, so a converging average settles one
// unit short (deliberate). False if null.
bool aim_vel_update(VelSmooth* s, int index, int picnum, int vx, int vy, int vz,
                    int* outvx, int* outvy, int* outvz);

// The pure half of aim.h's aim_smoothed_velocity: a READ, not a fold. False for null args or an identity mismatch.
bool vel_smooth_read(const VelSmooth* s, int index, int picnum,
                     int* outvx, int* outvy, int* outvz);

// 0 when aimlead is off (the hitscan path), so aimlead=0 collapses the marker's two squares for a stationary target.
int aim_lead_vel(int weaponVel, int aimlead);

// Silent aim: the pure decisions behind the `shoot` thunk hook.

// Half-angle of the redirect cone in degrees; config.cpp calls the same clamp. 45: a redirect the player can plausibly
// claim as their own aim.
const int SILENT_MAXANGLE_MIN     = 1;
const int SILENT_MAXANGLE_MAX     = 180;
const int SILENT_MAXANGLE_DEFAULT = 45;
int silent_maxangle_clamped(int degrees);
// The `atwith` picnums of the player's own aimed shots (processinput's `PUSH atwith; PUSH ps[].i; CALL 0x0040316b`):
// 0xa23 pistol, 0xa35 shotgun (x7), 0x9e8 chaingun, 0x990 expander, 0x9fc shrinker, 0xa2d rpg/devastator, 0x669
// freezer. Not tripbomb or knee; enemy shots and checkhitsprite's decal hitscans (0x8f8-0x8fb) must not match.
bool silent_atwith_is_player_shot(int atwith);

// The values go straight into live game memory: solvedAng must be a real Build angle, solvedHoriz inside the pitch
// envelope (else `(100 - horiz - horizoff) * 0x62` overflows in the engine).
bool silent_snapshot_usable(int targetIndex, int solvedAng, int solvedHoriz);

// Does this shoot call get the substituted angle and pitch? All must hold: on; isLocalPlayerSprite (never an enemy's or
// co-op partner's); atwithIsPlayerShot; haveTarget; los (a redirect through a wall just hits the wall); and
// target_within_redirect.
bool silent_decide(bool on, bool isLocalPlayerSprite, bool atwithIsPlayerShot,
                   bool haveTarget, bool los, int curAng, int wantAng, int maxUnits);

// hooked_shoot runs inside processinput BEFORE aim_tic publishes this tic's snapshot, so the substituted pair was one
// tic stale; it re-solves from the target's LIVE position. `coneRejected` is diagnostic-only.
struct SilentResolve { int ang; int horiz; bool valid; bool live; bool coneRejected; };

// `snapAng/snapHoriz` are the published fallback, `liveRaw` the target now. Same lead, scale and solve as the marker, so
// they cannot disagree. Falls back to the published pair on a degenerate solve, an unreachable pitch, or an angle
// outside the redirect cone.
SilentResolve silent_resolve(Point3 eye, int horizoff, const WeaponInfo& w,
                             int snapAng, int snapHoriz,
                             Point3 liveRaw, int vx, int vy, int vz,
                             int leadVel, int leadScalePct,
                             int curAng, int maxRedirectUnits);

// A grounded actor gets vz = 0: `position - bpos` is then the mover's gravity-and-snap cycle (troopers read vz
// +272..+320/tic standing still).

// makeitfall snaps sprite->z to floorz - 0x100 and zvel = 0 (tests at VA 0x004dd12d-0x004dd13f, snap at 0x004dd1da),
// so `z >= floorz - 256` IS "not falling".
const int AIM_FALL_FLOOR_OFFSET = 256;    // 0x100

// The global `gc` (VA 0x005ceb38, init 176); makeitfall adds it to zvel (VA 0x004dd187).
const int AIM_FALL_GRAVITY = 176;

// The engine's offset plus two gravity steps, covering a one-think lag, sloping floors and a measured artefact up to
// 320. A flyer 608 up is not caught.
const int AIM_GROUND_MARGIN = AIM_FALL_FLOOR_OFFSET + 2 * AIM_FALL_GRAVITY;   // 608

// Widened before the subtraction: floorz - margin must not wrap.
bool aim_actor_grounded(int z, int floorz, int margin);

// 0 for a grounded actor. Applied BEFORE the smoother, so the average is never poisoned by the artefact.
int aim_grounded_vz(int z, int floorz, int vz, int margin);

// All three windows here share one wrap-safe implementation (aim_throttle_due).
const unsigned AIM_DIAG_WINDOW_MS = 3000;

// A whole window per index, giving once-per-index-per-window.
const unsigned AIM_LOST_WINDOW_MS = 3000;

// `first` forces the first call through. Unsigned subtraction makes a GetTickCount wrap a small difference.
bool aim_throttle_due(unsigned nowMs, unsigned lastMs, bool first, unsigned periodMs);

// presentNow false = "vanished"; picnumSame else a recycled slot, the ordinary way an index stops being an enemy;
// xrepeat zero = the engine deleting it.
bool aim_lost_should_log(bool presentLast, bool presentNow, bool picnumSame, int xrepeat,
                         bool alreadyLoggedThisWindow);

// A level load skips the walk, so the first tic compares against the old map's indices. The signature (playerSprite,
// numsectors) is exact; a gap over AIM_PRESENCE_MAX_GAP_MS covers the rest.
const unsigned AIM_PRESENCE_MAX_GAP_MS = 1000;
bool aim_presence_reset(int prevPlayerSprite, int prevNumsectors,
                        int playerSprite, int numsectors,
                        unsigned nowMs, unsigned lastMs, unsigned maxGapMs);

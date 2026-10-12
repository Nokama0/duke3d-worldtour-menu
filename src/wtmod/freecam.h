// Verbatim from Megaton's src/duke3dmod/freecam.h; kept identical on purpose -- do not edit here.
#pragma once
// Free camera: pure state and integration; the DLL wiring is camera.cpp. The camera never moves the player: camera.cpp
// substitutes this state into ps[] for one displayrooms call.
#include "project.h"
#include <stdio.h>    // sscanf_s: freecam_parse_readout
#include <string.h>   // strstr

// Percent of FREECAM_BASE_SPEED; 500% because the big expansion maps take too long to cross at 100%.
const int FREECAM_SPEED_MIN     = 1;
const int FREECAM_SPEED_MAX     = 500;
const int FREECAM_SPEED_DEFAULT = 100;

// 100% = a quarter metre/tic, ~6.5 m/s at 26 Hz: a running player.
const int FREECAM_BASE_SPEED = BUILD_UNITS_PER_METRE / 4;

// Milli angle units per mouse count: 0.23 units/count (the aim's own rate) does not survive an integer without the
// thousandth; freecam_step's yawRem/pitchRem carry the remainder.
const int FREECAM_SENS_MILLI_UNIT = 1000;
const int FREECAM_SENS_MILLI_MIN  = 1;       // 0 would freeze the camera
const int FREECAM_SENS_MILLI_MAX  = 10000;   // ten units a count, far past any real mouse
struct FreecamSens { int yawMilli, pitchMilli; };
inline int freecam_sens_milli_clamped(int m) { return clamp_int(m, FREECAM_SENS_MILLI_MIN, FREECAM_SENS_MILLI_MAX); }
inline FreecamSens freecam_sens_default() { FreecamSens s = { FREECAM_SENS_MILLI_UNIT, FREECAM_SENS_MILLI_UNIT }; return s; }
// From mouse aim's own rates when it is on, else the default.
FreecamSens freecam_sens_from_mouseaim(bool mouseaimOn, int sens, int vscale);   // freecam.cpp, which includes mouseaim.h

// Build's z axis is 16x x/y; vertical travel is scaled to match (PHEIGHT 0x2800 / 16 = 640 units = eye height).
const int FREECAM_Z_PER_XY = 16;

// 2048 to the full turn, as sintable indexes.
const int FREECAM_ANG_UNITS = 2048;

// sintable entries span +-16384; travel divides back down by this.
const int FREECAM_SIN_SCALE = 16384;

// Free pitch, 0 level, positive up; short of vertical (500 =~ 87.9 degrees) so horiz never divides by a zero cosine.
const int FREECAM_PITCH_MAX = 500;

// The engine's `horiz` for a pitch: 100 + 128*sin/cos, integer division as displayrooms draws it. Null table: level.
inline int freecam_pitch_to_horiz(int pitch, const short* sintable) {
  if (!sintable) return 100;
  const int sinP = sintable[pitch & (FREECAM_ANG_UNITS - 1)];
  const int cosP = sintable[(pitch + FREECAM_ANG_UNITS / 4) & (FREECAM_ANG_UNITS - 1)];
  // freecam_horiz_to_pitch's search probes this directly, so it must not divide by zero at the quarter turn.
  if (cosP == 0) return sinP >= 0 ? 32767 : -32768;
  return 100 + (128 * sinP) / cosP;
}

// Inverse of the above, once on entry. A linear search, not binary: truncation flattens pitches near 0 to one horiz
// and a binary search lands on the plateau's edge.
int freecam_horiz_to_pitch(int horiz, const short* sintable);

// Steepest pitch Polymost can draw: past ghalfx^2/(ydimen/2) a screen corner falls behind the camera; held to 90%.
// The floor keeps a bad measurement from pinning the camera level.
const int FREECAM_PITCH_CAP_MIN = 256;
int freecam_pitch_cap(double ghalfx, int ydimen, int xdimenscale, const short* sintable);

// `active` is the camera's own switch, not g_cfg.freecamon. p* are the PREVIOUS tic's pose, for interpolation.
// yawRem/pitchRem carry the sub-unit sensitivity remainder.
struct FreecamState { int x, y, z; int ang; int pitch; bool active;
                      int px, py, pz, pang, ppitch;
                      int yawRem, pitchRem; };

struct FreecamInput { int dx, dy; bool fwd, back, left, right, up, down, fast, slow; };

// config.cpp loads through this too, so a hand-edited ini cannot smuggle in an out-of-range speed.
int freecam_speed_clamped(int speedPct);

// base * percent / 100, doubled by fast, halved by slow (both cancel), never below 1.
int freecam_speed(int speedPct, bool fast, bool slow);

// One tic: yaw takes dx (wraps), pitch -dy (clamped); travel follows the full look direction (xy = v*cos(pitch),
// z = -16*v*sin(pitch)), strafe stays horizontal, up is -z. `sens` is milli units/count, widened (int overflows at
// 100000).
FreecamState freecam_step(const FreecamState& s, const FreecamInput& in,
                          int speedPct, const FreecamSens& sens, const short* sintable);

// Entry, exit and the dead-man's switch are three rules: one symmetric rule could stick the camera on with the input
// lockout engaged forever.

// Needs the feature on (a bound key alone is not enough), a key edge, no panel, not dead (the death slide has its own
// view) and a valid player state.
bool freecam_enter_rule(bool on, bool keyEdge, bool menuOpen, bool dead, bool valid);

// `on`, `dead` and `valid` are absent on purpose: none may hold the camera engaged.
bool freecam_exit_rule(bool active, bool keyEdge, bool menuOpen);

// Forced down regardless of keys: death (a frozen body would stay un-respawnable), an unreadable local player, or
// freecamon switched off while flying.
bool freecam_force_off_rule(bool active, bool on, bool dead, bool valid);

// Starts at the player's eye and view so switching on is invisible. A null sintable yields pitch 0.
FreecamState freecam_enter(const PlayerPos& p, int ang, int horiz, const short* sintable);

// Needs a key edge and no panel (the camera is paused, so its aim is stale). `on` is absent as in freecam_exit_rule.
bool freecam_teleport_rule(bool active, bool keyEdge, bool menuOpen);

// Look direction in the hitscan vx/vy/vz convention: vx/vy from yaw scaled to +-16384, vz `((100 - horiz) << 5) << 6`
// (the camera has no horizoff). A null sintable yields zero.
struct HitscanDir { int vx, vy, vz; };

inline HitscanDir freecam_hitscan_direction(int ang, int horiz, const short* sintable) {
  HitscanDir d = { 0, 0, 0 };
  if (!sintable) return d;
  d.vx = sintable[(ang + FREECAM_ANG_UNITS / 4) & (FREECAM_ANG_UNITS - 1)];
  d.vy = sintable[ang & (FREECAM_ANG_UNITS - 1)];
  d.vz = ((100 - horiz) << 5) << 6;
  return d;
}

// Step back from a wall/sprite hit (xy units), clear of the player's 164-unit clip radius; floors/ceilings need none.
const int TELEPORT_CLEARANCE = 256;

// vx/vy are the cast direction scaled by FREECAM_SIN_SCALE, not a unit vector.
inline void freecam_teleport_step_back(int hitx, int hity, int vx, int vy,
                                       bool stepBack, int* outX, int* outY) {
  if (!outX || !outY) return;
  if (!stepBack) { *outX = hitx; *outY = hity; return; }
  *outX = clamp_ll_to_int((long long)hitx - (long long)vx * TELEPORT_CLEARANCE / FREECAM_SIN_SCALE);
  *outY = clamp_ll_to_int((long long)hity - (long long)vy * TELEPORT_CLEARANCE / FREECAM_SIN_SCALE);
}

// Stage one: a raw hitscan result to the point the engine resolves a sector for (the caller runs engine_updatesectorz
// on it before florz/ceilz exist). hitsect < 0 is the one refusal; a floor/ceiling hit (wall and sprite < 0) steps
// nothing back.
struct TeleportAim { bool ok; int x, y, z; const char* reason; };

TeleportAim freecam_teleport_aim(int hitsect, int hitwall, int hitsprite,
                                 int hitx, int hity, int hitz,
                                 int vx, int vy);

// Stage two: may the player stand at stage one's point. sectFromEngine < 0 is outside any readable sector. Tight
// space refuses rather than clamps: the player must not be silently moved from where they looked.
struct TeleportPlan { bool ok; int x, y, z; int sect; const char* reason; };

// Named so the suite pins the exact words. "" means no line (a clean teleport is its own feedback).
const char* const TELEPORT_REASON_NOTHING   = "Nothing to teleport to";
const char* const TELEPORT_REASON_NO_SECTOR = "Camera is outside the map";
const char* const TELEPORT_REASON_TIGHT     = "Not enough room to stand";
const char* const TELEPORT_REASON_NONE      = "";
// The destination was fine but the write failed: an engine fact, not a map fact.
const char* const TELEPORT_REASON_NO_WRITE  = "The player could not be moved";

TeleportPlan freecam_teleport_plan(int x, int y, int sectFromEngine,
                                   int florz, int ceilz, int eyeHeight);

inline bool freecam_teleport_has_notice(const TeleportPlan& p) {
  return p.reason != 0 && p.reason[0] != '\0';
}

const unsigned FREECAM_NOTICE_MS = 2000;

// UNSIGNED SUBTRACTION, deliberately: GetTickCount wraps every 49.7 days. `have` keeps a zero timestamp from showing a
// notice nobody set.
inline bool freecam_notice_visible(bool have, unsigned setMs, unsigned nowMs) {
  return have && (unsigned)(nowMs - setMs) < FREECAM_NOTICE_MS;
}

// Landing marker: a player-sized column read as too big, so half a metre across and 1.5 m tall (xy unit 1/512 m, z
// unit 1/8192 m).
const int FREECAM_TPMARK_HALF   = 224;      // half width, world units
const int FREECAM_TPMARK_HEIGHT = 0x3000;   // z is up-negative in Build
struct FreecamMarkCorner { int x, y; };
struct FreecamMarkBox { FreecamMarkCorner c[4]; int zTop, zBottom; };
// Widened and saturated: the point comes from engine memory and a near-INT_MAX coordinate would wrap a corner.
inline FreecamMarkBox freecam_teleport_box(int x, int y, int z) {
  const long long h = FREECAM_TPMARK_HALF;
  const int lx = clamp_ll_to_int((long long)x - h), hx = clamp_ll_to_int((long long)x + h);
  const int ly = clamp_ll_to_int((long long)y - h), hy = clamp_ll_to_int((long long)y + h);
  FreecamMarkBox b;
  // One turn around the square, so consecutive corners are always a side, never the diagonal.
  b.c[0].x = lx; b.c[0].y = ly;
  b.c[1].x = hx; b.c[1].y = ly;
  b.c[2].x = hx; b.c[2].y = hy;
  b.c[3].x = lx; b.c[3].y = hy;
  b.zTop    = clamp_ll_to_int((long long)z - (long long)FREECAM_TPMARK_HEIGHT);
  b.zBottom = z;
  return b;
}

// camera.cpp, DLL only: draws on the render clock, after the secret volume.
void camera_draw_teleport_marker();

// Transports: the camera follows a paired sector effector 7 (teleporter or water surface) by movetransports' rule
// (VA 0x0049F260).

const int FREECAM_TRANSPORT_STAT = 9;      // statnum of the SE 7 sprites
const int FREECAM_TRANSPORT_ZBAND = 0x1800; // lotag 0's z window, and its landing offset
const int FREECAM_TRANSPORT_SURFACE = 0x1000;   // lotag 1's band below the floor
const int FREECAM_TRANSPORT_CEILBAND = 0x600;   // lotag 2's band below the ceiling
const int FREECAM_TRANSPORT_LAND = 0x700;       // lotag 1/2's landing offset
// The engine's transporter_hold: tics before an effector may carry the camera again, so it cannot bounce back.
const int FREECAM_TRANSPORT_HOLD_TICS = 13;

// lotag is the EFFECTOR'S SECTOR's (0 teleporter, 1 water surface, 2 underwater), not the sprite's; owner* come from
// the paired effector and its sector.
struct FreecamTransport { int lotag, seX, seY, seZ, ownerX, ownerY, ownerZ,
                          ownerCeilZ, ownerFloorZ, secFloorZ, secCeilZ, tempData4; };
struct FreecamJump { int hit, x, y, z; };   // int flags only (the MSVC fold defect)

// Mirrors movetransports' three cases; lotag 1/2 use their z bands alone (ground, crouch and fall terms have no camera
// equivalent).
inline FreecamJump freecam_transport_jump(const FreecamTransport& t, int x, int y, int z) {
  FreecamJump j = { 0, x, y, z };
  // The engine skips an unpaired effector (owner == self); read by index that is an owner sitting on the effector.
  if (t.seX == t.ownerX && t.seY == t.ownerY && t.seZ == t.ownerZ) return j;
  int hit = 0, zTo = z;
  if (t.lotag == 0) {
    // A floor teleporter (temp_data[4] == 0; non-zero is half of a water pair) with the camera inside the z window.
    const long long dz = (long long)t.seZ - z;
    hit = (t.tempData4 == 0 && (dz < 0 ? -dz : dz) < FREECAM_TRANSPORT_ZBAND) ? 1 : 0;
    zTo = t.ownerZ + FREECAM_TRANSPORT_ZBAND;
  } else if (t.lotag == 1) {
    // Down through a water surface: near the floor of the sector above it.
    hit = (t.tempData4 != 0 && z > t.secFloorZ - FREECAM_TRANSPORT_SURFACE) ? 1 : 0;
    zTo = t.ownerCeilZ + FREECAM_TRANSPORT_LAND;
  } else if (t.lotag == 2) {
    // Back up: near the ceiling of the sector below it.
    hit = (t.tempData4 != 0 && z < t.secCeilZ + FREECAM_TRANSPORT_CEILBAND) ? 1 : 0;
    zTo = t.ownerFloorZ - FREECAM_TRANSPORT_LAND;
  }
  if (!hit) return j;
  j.hit = 1;
  // The pair's separation, not the owner's position, as the engine does: the camera keeps where it stood.
  j.x = clamp_ll_to_int((long long)x + t.ownerX - t.seX);
  j.y = clamp_ll_to_int((long long)y + t.ownerY - t.seY);
  j.z = zTo;
  return j;
}

// camera.cpp is the DLL half and does not link into test_host.

// Hooks the displayrooms thunk and feeds engine_mark_hook_result. Never fatal: a failed hook leaves the camera unable
// to draw, everything else running.
bool camera_install();

// Toggle and dead-man's switch, once per tic from hooked_getinput right after the real getinput and before the
// lockout clears `loc` (later lets a tic of real input through). Leaving only sets a flag; see camera_release_tic().
void camera_toggle_tic(int player);

// Applies the deferred deactivation: drops the camera, discards the stale mouse accumulator, releases the lockout. Runs
// after the original processinput because it writes the player struct; also called from hooked_getinput, as
// domovethings gates processinput on pause.
void camera_release_tic();

// From hooked_processinput after the original, only while active: drains the input feed (so a key released while
// paused is not missed), steps, publishes a snapshot.
void camera_tic();

bool camera_active();

// True only inside hooked_displayrooms, not camera_active(): the camera can fly while displayrooms refuses to arm
// (unverified engine, unreadable snapshot). nocull.cpp's sweep gates on this.
bool camera_render_armed();
int camera_pitch_cap_last();

// Debug: a pose copied from the readout ("cam x= y= z= ang= pitch=" anywhere in the text), applied on the next tic.
struct FreecamPose { int x, y, z, ang, pitch; };
inline int freecam_parse_readout(const char* text, FreecamPose* out) {
  if (!text || !out) return 0;
  const char* p = text;
  while ((p = strstr(p, "cam x=")) != 0) {
    FreecamPose r;
    if (sscanf_s(p, "cam x=%d y=%d z=%d ang=%d pitch=%d", &r.x, &r.y, &r.z, &r.ang, &r.pitch) == 5) {
      *out = r;
      return 1;
    }
    p += 6;
  }
  return 0;
}
void camera_request_pose(const FreecamPose& pose);   // camera.cpp, tic clock applies it

// smoothratio of the last hooked_displayrooms call, 16.16 fixed point (0 at tic start, 65536 fully shown); 0 before the
// first frame. The game's own number: callers clamp it.
long camera_last_smoothratio();

// Render-clock read of what camera_tic published, through the seqlock AimSnapshot uses (target.h). False on a torn
// copy, so the caller draws nothing rather than half of two tics.
bool camera_snapshot(FreecamState* out);

// The teleport's one line of feedback, or null; window is freecam_notice_visible. Not gated on the camera: a refusal
// must survive switching the camera off right after.
const char* camera_notice();

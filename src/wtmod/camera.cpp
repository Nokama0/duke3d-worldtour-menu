// Free camera, DLL half. The bracket wraps drawrooms (FUN_00780860), not the matrix builder FUN_007b9870: the scene
// pass re-resolves globalcursectnum, which seeds the portal walk (a negative sector draws nothing). Eight globals are
// restored in a __finally.
#include "camera.h"
#include "freecam.h"
#include "config.h"
#include "hook.h"
#include "input.h"
#include "mouseaim.h"
#include "project.h"
#include "esp_layout.h"
#include "world.h"
#include "esp_world.h"
#include "wallclock.h"
#include "raycast.h"
#include "worldwt.h"
#include "light.h"
#include "aim.h"
#include "canvas.h"
#include "font.h"
#include "ui.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "shot.h"
#include "autosave.h"

// RVAs, never absolute addresses: the game loads at a randomised base.
static const unsigned kRvaHwDrawrooms = 0xD0860;    // FUN_00780860, the hardware drawrooms body

static const unsigned kRvaGlobalPosX   = 0xE07180;  // int
static const unsigned kRvaGlobalPosY   = 0xE07174;  // int
static const unsigned kRvaGlobalPosZ   = 0xE07170;  // int
static const unsigned kRvaGlobalAng    = 0xE07164;  // int16 (drawrooms stores AX here)
static const unsigned kRvaGlobalHoriz  = 0xE07178;  // int
static const unsigned kRvaGlobalCurSec = 0xE07160;  // int16, the portal walk's seed
static const unsigned kRvaSinGlobalAng = 0xE07154;  // int, sintable[ang], 16384-scaled
static const unsigned kRvaCosGlobalAng = 0xE07168;  // int, sintable[ang+512]

static const unsigned kRvaSinTable   = 0xD4B230;    // 2048 int16
static const unsigned kRvaPlayers    = 0x5FD5C0;    // player[0], stride 0x408
static const unsigned kRvaSprites    = 0xC86810;    // sprite[0], stride 0x2C
static const unsigned kRvaLocalPlyr  = 0x7824CC;    // int
static const unsigned kRvaCameraSprt = 0xEDFC7A;    // int16; >= 0 means a viewscreen owns the view
static const unsigned kRvaNumSectors = 0xEC0B60;    // int16
static const unsigned kRvaButtonBits = 0x5DB314;    // uint32, the gamefunc bitfield
static const unsigned kRvaTotalClock = 0xD46A28;    // int
static const unsigned kRvaOTotalClock = 0x73E5C0;   // int

// The velocity globals and the 12-byte input record getinput fills.
static const unsigned kRvaFvel   = 0x5F612C;   // int, pre-rotation forward
static const unsigned kRvaSvel   = 0x5F74BC;   // int, pre-rotation strafe
static const unsigned kRvaAvel   = 0x5F6128;   // int
static const unsigned kRvaHorz   = 0x5F6120;   // int
static const unsigned kRvaRecord = 0x73E1B0;   // 12 bytes

#define P_POSX      0x214
#define P_POSY      0x218
#define P_POSZ      0x21C
// The previous tic's position, kept by processinput for interpolation; pos - opos is one tic of the player's movement.
#define P_OPOSX     0x238
#define P_OPOSY     0x23C
#define P_OPOSZ     0x240
#define P_HORIZ     0x220
#define P_ANG       0x2CC
// ang and horiz's interpolation partners; silent aim substitutes them with the pair.
#define P_OHORIZ    0x224   // int32
#define P_OANG      0x2CE   // int16
// GameMouseAiming (int32): aim() (FUN_00724ac0) returns -1 without searching for any value above 1, which is silent
// aim's only way to hold the engine's autoaim off for one shot (it would drag a redirected shot back).
#define P_AIMMODE   0x288
#define AIMMODE_NO_AUTOAIM 2
// The player's visibility, a SHORT holding 512 ("Remove fog" is its only writer). A short here but a long in Megaton,
// so a dword scan reads 0x02000200 and finds nothing.
#define P_VISIBILITY 0x268
#define P_GM        0x398   // int8; getinput's own menu test is gm & 0x11
// The view tilt, an int16 READ SIGNED, the second half of every shot's vertical: shoot computes (100 - horizoff -
// horiz) * K. Nonzero while the player MOVES (processinput adds (floorz change * 0xA0) >> 16, decays it by
// (horizoff>>3)+1); passing 0 made the aimbot miss on the move.
#define P_HORIZOFF  0x300
#define P_CURSECT   0x2D2
#define P_CURR_WEAPON 0x2FA   // int16, fireratewt.cpp's own reading
// The view bob shoot adds to posz when it builds a shot's origin.
#define P_PYOFF     0x244   // int32: shoot reads it whole
#define P_NEWOWNER  0x306   // int16, >= 0 means the player is looking through something
#define P_SPRITE    0x328   // int16, the player's own sprite index
// THE HELD KEYCARDS (player base 0xCAD5C0 + 0x31E), found from the CON quotes 70/71/72: blue is bit 1 (pal 0), red 2
// (pal 21), yellow 4 (pal 23), the mapping esp_switch.h's switch_classify assumes.
#define P_GOT_ACCESS 0x31E   // uint8, bits 1/2/4 = blue/red/yellow
#define P_STRIDE    0x408

#define SPR_STRIDE  0x2C
#define SPR_EXTRA   0x2A    // int16, an actor's health

// The gamefunc bit numbers, read off getinput (FUN_00729820): 4 is the strafe modifier, 9/10 become the record's jump
// and crouch bits, 7 is the run key behind its 0x50-versus-0x28 speed cap.
#define GF_FORWARD  0
#define GF_BACK     1
#define GF_TURNL    2
#define GF_TURNR    3
#define GF_STRAFE   4
#define GF_RUN      7
#define GF_JUMP     9
#define GF_CROUCH  10
#define GF_STRAFEL 15
#define GF_STRAFER 16

// push ebp; mov ebp,esp; and esp,-8: the first instruction boundary at or past a five-byte JMP, none position
// dependent, eight-byte aligned so hook.cpp patches it atomically.
static const unsigned char kFpHwDrawrooms[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8 };
static const unsigned kHwDrawroomsPrologue = 6;

// THE SECOND BRACKET: FUN_007ba920, the per-sprite geometry update. Billboard yaw is baked here, before drawrooms
// writes globalang. ONLY globalang is substituted: it is the one of the eight read in this subtree.
static const unsigned kRvaSpriteUpdate = 0x10A920;
static const unsigned char kFpSpriteUpdate[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8 };
static const unsigned kSpriteUpdatePrologue = 6;
static const unsigned char kFpSpriteUpdateTail[6] = { 0x8B, 0x45, 0x08, 0x56, 0x57, 0x8B };
// Those six bytes start many MSVC functions, so the tail is checked too: sub esp,8; push esi; push edi.
static const unsigned kHwDrawroomsTailAt = 6;
static const unsigned char kHwDrawroomsTail[] = { 0x83, 0xEC, 0x08, 0x56, 0x57 };

typedef void (__cdecl* HwDrawRoomsFn)(void);

static unsigned char* s_base = 0;
static DWORD          s_imageSize = 0;
static int  s_ok = 0;          // every address and the fingerprint checked out

static int*   s_pPosX = 0;
static int*   s_pPosY = 0;
static int*   s_pPosZ = 0;
static short* s_pAng = 0;
static int*   s_pHoriz = 0;
static short* s_pCurSec = 0;
static int*   s_pSinAng = 0;
static int*   s_pCosAng = 0;
static const short* s_pSin = 0;
static const int*   s_pLocal = 0;
static const short* s_pCameraSprite = 0;
static const short* s_pNumSectors = 0;
static const unsigned* s_pButtons = 0;
static const int* s_pTotalClock = 0;
static const int* s_pOTotalClock = 0;
static int* s_pFvel = 0;
static int* s_pSvel = 0;
static int* s_pAvel = 0;
static int* s_pHorz = 0;
static unsigned char* s_pRecord = 0;
// Volatile and spun on: hook_install patches before it hands the trampoline back, so a call can arrive in between.
static HwDrawRoomsFn volatile s_orig = 0;

// Tic and render are one thread on this game (FUN_006e9da0 tic, displayrooms draw), so no seqlock here.
static FreecamState s_cam = { 0, 0, 0, 0, 0, false, 0, 0, 0, 0, 0, 0, 0 };
static int s_armed = 0;        // the render bracket substituted the camera THIS frame
static int s_pendingOff = 0;   // a deactivation asked for this tic, applied after processinput
static int s_mouseDx = 0, s_mouseDy = 0;
static int s_seedSect = -1;    // what the engine last resolved for the camera
// Tics left before a transport may carry the camera again (the engine's transporter_hold), else the far half of a pair
// carries it straight back.
static int s_transportHold = 0;
// The teleport marker's plan, written on the tic and read by the render clock.
static TeleportPlan s_markPlan = { false, 0, 0, 0, -1, TELEPORT_REASON_NONE };
static int s_markValid = 0;
static int s_lastPx = 0, s_lastPy = 0, s_lastPz = 0, s_haveLast = 0;
static int s_loggedOn = 0, s_loggedOff = 0, s_loggedDraw = 0;

// A level change or load moves the player further in one tic than play can, so a jump this size proves the world is not
// the one the camera left. 4096 xy is ~45 running steps; z is 16:1 and 65536 is past a fall.
static const int kJumpXY = 4096;
static const int kJumpZ  = 65536;

bool camera_active() { return s_cam.active; }
int  camera_installed() { return s_ok != 0; }
bool camera_render_armed() { return s_armed != 0; }

// THE ONLY PART TWO THREADS TOUCH: the copy and paste chords run on the window's thread, the rest on the game's. So the
// pose goes out through a seqlock and back through a latch; a half-read pose is a viewpoint that never existed.
static volatile LONG s_poseSeq = 0;        // odd while the copy below is being written
static FreecamPose s_posePub = { 0, 0, 0, 0, 0 };
static volatile LONG s_poseLive = 0;       // the camera was flying when this was published
static FreecamPose s_pendingPose = { 0, 0, 0, 0, 0 };
static volatile LONG s_pendingPoseSet = 0;

static void publish_pose() {
  InterlockedIncrement(&s_poseSeq);        // odd: readers refuse
  s_posePub.x = s_cam.x;
  s_posePub.y = s_cam.y;
  s_posePub.z = s_cam.z;
  s_posePub.ang = s_cam.ang;
  s_posePub.pitch = s_cam.pitch;
  s_poseLive = s_cam.active ? 1 : 0;
  InterlockedIncrement(&s_poseSeq);        // even again
}

int camera_pose_read(FreecamPose* out) {
  if (!out) return 0;
// Bounded, not a spin: this runs on the window's thread.
  for (int tries = 0; tries < 8; tries++) {
    const LONG before = s_poseSeq;
    if (before & 1) continue;
    const FreecamPose p = s_posePub;
    const int live = (int)s_poseLive;
    if (s_poseSeq != before) continue;
    if (!live) return 0;
    *out = p;
    return 1;
  }
  return 0;
}

void camera_request_pose(const FreecamPose& pose) {
  s_pendingPose = pose;
  InterlockedExchange(&s_pendingPoseSet, 1);   // written last
}

// Checked against the image's extent, so a wrong address is a refusal and not a crash in the game's thread.
static void* engine_ptr(unsigned rva, unsigned size) {
  if (!s_base || rva > s_imageSize || size > s_imageSize - rva) {
    wt_log("camera: rva 0x%X (%u bytes) is outside the image", rva, size);
    return 0;
  }
  return s_base + rva;
}

static int local_player() {
  if (!s_pLocal) return -1;
  const int n = *s_pLocal;
  return (n >= 0 && n <= 15) ? n : -1;
}

static unsigned char* player_ptr(int snum) {
  if (!s_ok || snum < 0 || snum > 15) return 0;
  return s_base + kRvaPlayers + (unsigned)snum * P_STRIDE;
}

// gm == 4 exactly, not a bit test: MODE_GAME alone (the menu and the end-of-level tally set other bits).
bool camera_local_in_play() {
  const unsigned char* q = player_ptr(local_player());
  return q && *(const unsigned char*)(q + P_GM) == 4;
}

// -1 when there is no player to read: unknown, not none, so it must not turn every line off.
int camera_local_got_access() {
  const unsigned char* q = player_ptr(local_player());
  return q ? (int)*(const unsigned char*)(q + P_GOT_ACCESS) : -1;
}

// `valid` is false when the local player has no sector this DLL may read (displayrooms' own "not in play": 0xFFFF, or
// past MAXSECTORS).
static PlayerPos player_pos(int snum) {
  PlayerPos p = { 0, 0, 0, 0, 0, 0, -1, false };
  const unsigned char* q = player_ptr(snum);
  if (!q || !s_pNumSectors) return p;
  const int sect = (int)*(const unsigned short*)(q + P_CURSECT);
  const int ns = (int)*s_pNumSectors;
  if (sect < 0 || sect >= ns || ns <= 0 || ns > 4096) return p;
  p.posx = *(const int*)(q + P_POSX);
  p.posy = *(const int*)(q + P_POSY);
  p.posz = *(const int*)(q + P_POSZ);
  p.oposx = p.posx; p.oposy = p.posy; p.oposz = p.posz;
  p.cursectnum = sect;
  p.valid = true;
  return p;
}

bool camera_local_player_pos(PlayerPos* out) {
  if (!out) return false;
  const PlayerPos zero = { 0, 0, 0, 0, 0, 0, -1, false };
  *out = zero;
  const int snum = local_player();
  if (snum < 0 || snum > 15) return false;
  const PlayerPos p = player_pos(snum);
  if (!p.valid) return false;
  *out = p;
  return true;
}

// Health through the player's own sprite. An unreadable index answers "not dead", which leaves the camera up rather
// than stranding a corpse in the air.
static bool player_dead(int snum) {
  const unsigned char* q = player_ptr(snum);
  if (!q) return false;
  const int i = (int)*(const short*)(q + P_SPRITE);
  if (i < 0 || i >= 16384) return false;
  const short* extra = (const short*)(s_base + kRvaSprites + (unsigned)i * SPR_STRIDE + SPR_EXTRA);
  return *extra <= 0;
}

void camera_lock_input() {
  if (!s_ok) return;
// getinput's own lockout (the `gm & 0x11` path), store for store, with the action bits reduced to the one it keeps so
// no unknown key leaks an action.
  *s_pFvel = 0;
  *s_pSvel = 0;
  *s_pAvel = 0;
  *s_pHorz = 0;
  *(short*)(s_pRecord + 0) = 0;          // avel (+0) and horz (+1)
  *(int*)(s_pRecord + 2) = 0;            // fvel (+2) and svel (+4), the engine's own 32-bit store
  *(unsigned*)(s_pRecord + 8) &= (1u << 26);
}

void camera_feed_mouse(int dx, int dy) {
// Bounded: a stuck device must not grow a value freecam_step then multiplies.
  const int kMax = 1 << 20;
  if (s_mouseDx > -kMax && s_mouseDx < kMax) s_mouseDx += dx;
  if (s_mouseDy > -kMax && s_mouseDy < kMax) s_mouseDy += dy;
}

// The player as the aimbot and triggerbot read it: they run on different hooks (processinput, getinput) and neither
// should learn the struct's layout. opos* is the struct's own triple, valid through a free camera flight.
bool camera_build_player_aim(PlayerAim* out) {
  if (!out) return false;
  if (!s_ok) return false;
  const int snum = local_player();
  if (snum < 0 || snum > 15) return false;
  unsigned char* q = player_ptr(snum);
  if (!q) return false;
  PlayerAim p;
  p.index = snum;
  p.spriteIndex = (int)*(const short*)(q + P_SPRITE);
  p.eyex = *(const int*)(q + P_POSX);
  p.eyey = *(const int*)(q + P_POSY);
  p.eyez = *(const int*)(q + P_POSZ);
  p.oposx = *(const int*)(q + P_OPOSX);
  p.oposy = *(const int*)(q + P_OPOSY);
  p.oposz = *(const int*)(q + P_OPOSZ);
  p.ang = (int)*(const unsigned short*)(q + P_ANG);
  p.horiz = *(const int*)(q + P_HORIZ);
  p.horizoff = (int)*(const short*)(q + P_HORIZOFF);
  p.pyoff = *(const int*)(q + P_PYOFF);
  p.cursectnum = (int)*(const unsigned short*)(q + P_CURSECT);
  p.currWeapon = (int)*(const short*)(q + P_CURR_WEAPON);
  p.autoAim = 0;
  p.dead = player_dead(snum);
  if (!s_pNumSectors || p.cursectnum < 0 || p.cursectnum >= (int)*s_pNumSectors) return false;
  *out = p;
  return true;
}

// Index-free accessors so silent aim can substitute the pair around one call. Both refuse rather than guess.
bool camera_player_read_view(short* ang, int* horiz) {
  if (!ang || !horiz) return false;
  const int snum = local_player();
  if (snum < 0 || snum > 15) return false;
  const unsigned char* q = player_ptr(snum);
  if (!q) return false;
  *ang = *(const short*)(q + P_ANG);
  *horiz = *(const int*)(q + P_HORIZ);
  return true;
}

bool camera_player_write_view(short ang, int horiz) {
  const int snum = local_player();
  if (snum < 0 || snum > 15) return false;
  unsigned char* q = player_ptr(snum);
  if (!q) return false;
  *(short*)(q + P_ANG) = ang;
  *(int*)(q + P_HORIZ) = horiz;
  return true;
}

// What silent aim substitutes around one shoot call: the pair plus its interpolation partners (no reader sees them
// disagree) and the autoaim hold-off. Three calls so the ORDER shows: save first, substitute, restore unconditionally.
bool camera_player_read_shot_view(ShotView* out) {
  if (!out) return false;
  const int snum = local_player();
  if (snum < 0 || snum > 15) return false;
  const unsigned char* q = player_ptr(snum);
  if (!q) return false;
  out->ang = *(const short*)(q + P_ANG);
  out->oang = *(const short*)(q + P_OANG);
  out->horiz = *(const int*)(q + P_HORIZ);
  out->ohoriz = *(const int*)(q + P_OHORIZ);
  out->aimmode = *(const int*)(q + P_AIMMODE);
  return true;
}

bool camera_player_substitute_shot_view(short ang, int horiz) {
  const int snum = local_player();
  if (snum < 0 || snum > 15) return false;
  unsigned char* q = player_ptr(snum);
  if (!q) return false;
  *(short*)(q + P_ANG) = ang;
  *(short*)(q + P_OANG) = ang;
  *(int*)(q + P_HORIZ) = horiz;
  *(int*)(q + P_OHORIZ) = horiz;
  *(int*)(q + P_AIMMODE) = AIMMODE_NO_AUTOAIM;
  return true;
}

bool camera_player_restore_shot_view(const ShotView& v) {
  const int snum = local_player();
  if (snum < 0 || snum > 15) return false;
  unsigned char* q = player_ptr(snum);
  if (!q) return false;
  *(short*)(q + P_ANG) = v.ang;
  *(short*)(q + P_OANG) = v.oang;
  *(int*)(q + P_HORIZ) = v.horiz;
  *(int*)(q + P_OHORIZ) = v.ohoriz;
  *(int*)(q + P_AIMMODE) = v.aimmode;
  return true;
}

void aim_run_tic() {
  if (!s_ok) return;
  const int snum = local_player();
  if (snum < 0 || snum > 15) return;
  unsigned char* q = player_ptr(snum);
  if (!q) return;

  PlayerAim p;
  p.index = snum;
  p.spriteIndex = (int)*(const short*)(q + P_SPRITE);
  p.eyex = *(const int*)(q + P_POSX);
  p.eyey = *(const int*)(q + P_POSY);
  p.eyez = *(const int*)(q + P_POSZ);
  p.oposx = *(const int*)(q + P_OPOSX);
  p.oposy = *(const int*)(q + P_OPOSY);
  p.oposz = *(const int*)(q + P_OPOSZ);
  p.ang = (int)*(const unsigned short*)(q + P_ANG);
  p.horiz = *(const int*)(q + P_HORIZ);
  p.horizoff = (int)*(const short*)(q + P_HORIZOFF);
  p.pyoff = *(const int*)(q + P_PYOFF);
  p.cursectnum = (int)*(const unsigned short*)(q + P_CURSECT);
  p.currWeapon = (int)*(const short*)(q + P_CURR_WEAPON);
  p.autoAim = 0;
  p.dead = player_dead(snum);

// A sector the engine treats as "not in play" is no place to cast a sightline from.
  if (!s_pNumSectors || p.cursectnum < 0 || p.cursectnum >= (int)*s_pNumSectors) {
    aim_clear();
    return;
  }

  aim_tic(p);
  short ang = *(short*)(q + P_ANG);
  int horiz = *(int*)(q + P_HORIZ);
  if (aim_apply(p, &ang, &horiz)) {
    *(short*)(q + P_ANG) = ang;
    *(int*)(q + P_HORIZ) = horiz;
  }
}

// THE VISIBILITY HOLD, ON EVERY TIC, not inside camera_tic (which runs only while the camera flies): FULLBRIGHT rides
// the same field during ordinary play.
void camera_light_tic() {
  unsigned char* pq = player_ptr(local_player());
  fog_hold_tic(camera_active(), pq ? (short*)(pq + P_VISIBILITY) : 0);
// Screenshot mode may call into the game's UI (game-thread work). The menu test is getinput's own.
  const bool menuOpen = pq && (*(const unsigned char*)(pq + P_GM) & 0x11) != 0;
  shot_tic(camera_active(), menuOpen);
// Autosave last: it reads the whole game's state, and the end of the tic is the only game-thread boundary.
  autosave_tic(pq, camera_active());
}

void camera_release_tic() {
  if (!s_pendingOff) return;
  s_pendingOff = 0;
  s_cam.active = false;
  s_armed = 0;
// The teleport key goes back to the game; a press still waiting is dropped.
  input_set_teleport_key(0);
// fog_tic will not run again to undo its substitution once the camera is down.
  fog_drop();
  shot_drop();
  s_markValid = 0;
  s_markPlan.ok = false;
// Discarded, not banked: they would turn the player once aim got them back.
  s_mouseDx = 0;
  s_mouseDy = 0;
  if (!s_loggedOff) {
    s_loggedOff = 1;
    wt_log("camera: down. no engine global is left changed (the teleport, if it fired, moved the player deliberately)");
  }
  publish_pose();   // live 0: the copy chord has nothing to copy once it is down
}

void camera_toggle_tic(int player) {
  if (!s_ok) return;
  if (player < 0 || player > 15 || player != local_player()) return;

  const PlayerPos p = player_pos(player);
  const bool dead = player_dead(player);
  const bool menuOpen = input_menu_open();
  const bool on = g_cfg.freecamon != 0;
  const bool edge = input_take_freecam_edge() != 0;

// A step this big in the player's position is a level change, load or teleporter, and the camera's world is gone. The
// teleport records its landing as the last position so the next tic measures no step.
  bool jumped = false;
  if (p.valid) {
    if (s_haveLast) {
      const long long dx = (long long)p.posx - s_lastPx;
      const long long dy = (long long)p.posy - s_lastPy;
      const long long dz = (long long)p.posz - s_lastPz;
      jumped = (dx > kJumpXY || dx < -kJumpXY || dy > kJumpXY || dy < -kJumpXY ||
                dz > kJumpZ  || dz < -kJumpZ);
    }
    s_lastPx = p.posx; s_lastPy = p.posy; s_lastPz = p.posz;
    s_haveLast = 1;
  } else {
    s_haveLast = 0;   // nothing to measure the next tic against
  }

  if (s_cam.active && jumped) {
    s_pendingOff = 1;
  } else if (freecam_force_off_rule(s_cam.active, on, dead, p.valid)) {
    s_pendingOff = 1;
  } else if (s_cam.active) {
    if (freecam_exit_rule(s_cam.active, edge, menuOpen)) s_pendingOff = 1;
  } else if (freecam_enter_rule(on, edge, menuOpen, dead, p.valid)) {
    const unsigned char* q = player_ptr(player);
    const int ang = (int)*(const unsigned short*)(q + P_ANG);
    const int horiz = *(const int*)(q + P_HORIZ);
    s_cam = freecam_enter(p, ang, horiz, s_pSin);
    s_seedSect = p.cursectnum;
    s_transportHold = 0;
    input_set_teleport_key(g_cfg.freecamteleportkey);
    s_mouseDx = 0;
    s_mouseDy = 0;
// A paste that arrived while the camera was down belongs to no flight.
    InterlockedExchange(&s_pendingPoseSet, 0);
    if (!s_loggedOn) {
      s_loggedOn = 1;
      wt_log("camera: up at %d,%d,%d ang %d pitch %d, sector %d; the tic is untouched "
             "and the player's input is held at getinput's own menu lockout",
             s_cam.x, s_cam.y, s_cam.z, s_cam.ang, s_cam.pitch, s_seedSect);
    }
  }

  // Held through the exit tic too, so the flying keys cannot leak out.
  if (s_cam.active) camera_lock_input();
  publish_pose();
}

static FreecamInput read_keys() {
  FreecamInput in = { 0, 0, false, false, false, false, false, false, false, false };
  in.dx = s_mouseDx;
  in.dy = s_mouseDy;
  if (!s_pButtons) return in;
  const unsigned b = *s_pButtons;
// THE UNION, everywhere but descend: the camera's own keys are OR'd with the game's bindings, so nothing anyone bound
// stops moving.
  const unsigned k = input_freecam_move_keys();
  const bool strafeMod = ((b >> GF_STRAFE) & 1u) != 0;
  in.fwd   = ((b >> GF_FORWARD) & 1u) != 0 || (k & (1u << WTFK_FWD)) != 0;
  in.back  = ((b >> GF_BACK) & 1u) != 0 || (k & (1u << WTFK_BACK)) != 0;
  // With the strafe modifier held the turn keys strafe, as in getinput.
  in.left  = ((b >> GF_STRAFEL) & 1u) != 0 || (strafeMod && ((b >> GF_TURNL) & 1u) != 0)
             || (k & (1u << WTFK_LEFT)) != 0;
  in.right = ((b >> GF_STRAFER) & 1u) != 0 || (strafeMod && ((b >> GF_TURNR) & 1u) != 0)
             || (k & (1u << WTFK_RIGHT)) != 0;
  in.up    = ((b >> GF_JUMP) & 1u) != 0 || (k & (1u << WTFK_UP)) != 0;
// DESCEND IS THE ONE EXCEPTION to the union: Duke binds Crouch to Left Ctrl, which is the SLOW key, so taking both
// would slow and sink at once.
  in.down  = (k & (1u << WTFK_DOWN)) != 0;
// The run key itself, not the engine's autorun xor, which would leave it fast.
  in.fast  = ((b >> GF_RUN) & 1u) != 0 || (k & (1u << WTFK_FAST)) != 0;
// The game has no "walk" bit to read.
  in.slow  = (k & (1u << WTFK_SLOW)) != 0;
  return in;
}

// The teleport drops the player where the camera's crosshair ray lands. THE ONLY PLAYER-STATE WRITE not the engine's
// own, so it runs from camera_tic (processinput's tail, after the original) and writes only position and sector.
const unsigned TELEPORT_NOTICE_MS = 2500;
const int TELEPORT_NOTICE_X = 24, TELEPORT_NOTICE_Y = 96;
static const char* s_teleportNotice = TELEPORT_REASON_NONE;
static unsigned    s_teleportNoticeMs = 0;

// The plan the key acts on AND the marker draws, so the two never disagree. Reads only.
static TeleportPlan camera_teleport_compute(int* outVx, int* outVy) {
  TeleportPlan p = { false, 0, 0, 0, -1, TELEPORT_REASON_NOTHING };
  if (outVx) *outVx = 0;
  if (outVy) *outVy = 0;
  if (!world_ready()) return p;

  const int horiz = freecam_pitch_to_horiz(s_cam.pitch, s_pSin);
  const HitscanDir d = freecam_hitscan_direction(s_cam.ang, horiz, s_pSin);
  if (outVx) *outVx = d.vx;
  if (outVy) *outVy = d.vy;
  const RayHit hit = ray_cast_world(s_cam.x, s_cam.y, s_cam.z, s_seedSect,
                                    d.vx, d.vy, d.vz);
  const TeleportAim aim = freecam_teleport_aim(hit.sect, hit.wall, hit.sprite,
                                               hit.x, hit.y, hit.z, d.vx, d.vy);
  if (!aim.ok) { p.reason = aim.reason; return p; }

// The landing sector is the cast's own (the step-back stays in the sector the ray travelled), so no updatesectorz.
  SectorSlope slope;
  if (!world_sector_slope(hit.sect, &slope)) { p.reason = TELEPORT_REASON_NO_WRITE; return p; }
  int ceilz = 0, florz = 0;
  sector_slope_z(slope, aim.x, aim.y, &ceilz, &florz);
  return freecam_teleport_plan(aim.x, aim.y, hit.sect, florz, ceilz, PHEIGHT);
}

static void camera_teleport_tic() {
  const int edge = input_take_teleport_edge();
  const bool fire = freecam_teleport_rule(s_cam.active, edge != 0, input_menu_open());
  const bool wantMark = s_cam.active && g_cfg.freecamteleportmarker != 0;
  if (!fire && !wantMark) { s_markValid = 0; return; }

  int vx = 0, vy = 0;
  const TeleportPlan plan = camera_teleport_compute(&vx, &vy);
  s_markPlan = plan;
  s_markValid = wantMark ? 1 : 0;
  if (!fire) return;

  const int snum = local_player();
  if (snum < 0 || snum > 15) return;
  unsigned char* q = player_ptr(snum);
  if (!q || !s_pNumSectors) return;
  if (!plan.ok) {
    if (freecam_teleport_has_notice(plan)) {
      s_teleportNotice = plan.reason;
      s_teleportNoticeMs = wallclock_ms();
    }
    wt_log("camera: teleport refused, %s", plan.reason);
    return;
  }
  if (plan.sect < 0 || plan.sect >= (int)*s_pNumSectors) {
    s_teleportNotice = TELEPORT_REASON_NO_WRITE;
    s_teleportNoticeMs = wallclock_ms();
    return;
  }
  *(int*)(q + P_POSX) = plan.x;
  *(int*)(q + P_POSY) = plan.y;
  *(int*)(q + P_POSZ) = plan.z;
  *(unsigned short*)(q + P_CURSECT) = (unsigned short)plan.sect;
// The jump reset must not see this write as a teleporter: recording the landing makes the next tic measure zero.
  s_lastPx = plan.x;
  s_lastPy = plan.y;
  s_lastPz = plan.z;
  s_haveLast = 1;
  s_teleportNotice = TELEPORT_REASON_NONE;
  wt_log("camera: teleport to (%d,%d,%d) sector %d", plan.x, plan.y, plan.z, plan.sect);
}

// The landing column, pushed into the in-world layer from the render clock. Drawn only for a plan that would succeed (a
// marker over a refusal promises a teleport that won't happen).
void camera_draw_teleport_marker() {
// s_cam.active FIRST: the tic that maintains s_markValid stops when the camera lands, so a stale flag would leave the
// column standing.
  if (!s_cam.active || !s_markValid || !s_markPlan.ok) return;
  const FreecamMarkBox b = freecam_teleport_box(s_markPlan.x, s_markPlan.y, s_markPlan.z);
// C_TELEPORT_MARK carries the secret volume's alpha, so the column stacks without washing out what is behind it.
  const unsigned colour = C_TELEPORT_MARK;
  for (int i = 0; i < 4; i++) {
    const FreecamMarkCorner& a = b.c[i];
    const FreecamMarkCorner& c = b.c[(i + 1) & 3];
    WorldQuad3 q;
    q.p[0].x = a.x; q.p[0].y = a.y; q.p[0].z = b.zTop;
    q.p[1].x = c.x; q.p[1].y = c.y; q.p[1].z = b.zTop;
    q.p[2].x = c.x; q.p[2].y = c.y; q.p[2].z = b.zBottom;
    q.p[3].x = a.x; q.p[3].y = a.y; q.p[3].z = b.zBottom;
    q.rgba = colour;
// A COLUMN, not a face: it stands in open air with its own depth, so it takes the room pull.
    q.pull = W3D_PULL_ROOM;
    if (!world3d_push_quad(q)) return;
  }
}

// A paired sector effector 7 carries the player across a teleporter or water surface (movetransports); without this the
// camera comes out in the wrong half. No updatesector here, so the landing takes the PAIR'S OWN sector; the next frame
// resolves the exact one.
static void camera_transport_tic() {
  if (s_transportHold > 0) s_transportHold--;
  if (g_cfg.freecamtransport == 0 || s_transportHold != 0) return;
  if (!world_ready() || s_seedSect < 0) return;

  WorldWalk w;
  if (!world_walk_begin(&w, FREECAM_TRANSPORT_STAT)) return;
  int i = 0;
  while (world_walk_next(&w, &i)) {
    WorldSprite se, own;
    if (!world_sprite(i, &se)) continue;
    // owner == self is the engine's unpaired effector, which it skips.
    if (se.owner < 0 || se.owner == i) continue;
    // Only the sector the camera is tracked into: the z bands are read against its floor and ceiling.
    if (se.sectnum < 0 || se.sectnum != s_seedSect) continue;
    // An unreadable temp_data[4] is not a zero: lotag 0 tests "== 0", so it would invent a floor teleporter.
    int td4 = 0;
    if (!world_actor_temp_data(i, 4, &td4)) continue;
    if (!world_sprite(se.owner, &own)) continue;

    int secCeil = 0, secFloor = 0, ownCeil = 0, ownFloor = 0;
    if (!world_sector_zrange(se.sectnum, &secCeil, &secFloor)) continue;
    if (!world_sector_zrange(own.sectnum, &ownCeil, &ownFloor)) continue;

    FreecamTransport t;
    t.lotag = world_sector_lotag(se.sectnum);   // -1 when unreadable: no case matches
    t.seX = se.x;  t.seY = se.y;  t.seZ = se.z;
    t.ownerX = own.x; t.ownerY = own.y; t.ownerZ = own.z;
    t.ownerCeilZ = ownCeil;
    t.ownerFloorZ = ownFloor;
    t.secFloorZ = secFloor;
    t.secCeilZ = secCeil;
    t.tempData4 = td4;

    const FreecamJump jump = freecam_transport_jump(t, s_cam.x, s_cam.y, s_cam.z);
    if (!jump.hit) continue;
    s_cam.x = jump.x;
    s_cam.y = jump.y;
    s_cam.z = jump.z;
// Collapse the previous-tic position onto the landing so no frame interpolates a streak across the jump.
    s_cam.px = s_cam.x;
    s_cam.py = s_cam.y;
    s_cam.pz = s_cam.z;
    if (own.sectnum >= 0) s_seedSect = own.sectnum;
    s_transportHold = FREECAM_TRANSPORT_HOLD_TICS;
    wt_log("camera: transport se=%d lotag=%d -> (%d,%d,%d) seed sector %d",
           i, t.lotag, s_cam.x, s_cam.y, s_cam.z, s_seedSect);
    break;
  }
}

void camera_tic() {
  if (!s_ok || !s_cam.active) return;
  const FreecamInput in = read_keys();
  s_mouseDx = 0;
  s_mouseDy = 0;
// The turn rate follows the aim's, so the camera cannot drift from how the gun feels.
  const FreecamSens sens = freecam_sens_from_mouseaim(g_cfg.mouseaimon != 0,
                                                      g_cfg.mouseaimsens,
                                                      g_cfg.mouseaimvscale);
  s_cam = freecam_step(s_cam, in, g_cfg.freecamspeed, sens, s_pSin);

// A pose pasted from the clipboard, spent on the tic clock. The previous-tic copy is set to the same pose so no frame
// interpolates a streak. The sector seed is left alone: the bracket's updatesector keeps the old sector if the point
// is outside every one.
  if (InterlockedExchange(&s_pendingPoseSet, 0)) {
    s_cam.x = s_pendingPose.x;
    s_cam.y = s_pendingPose.y;
    s_cam.z = s_pendingPose.z;
    s_cam.ang = s_pendingPose.ang & (FREECAM_ANG_UNITS - 1);
    s_cam.pitch = clamp_int(s_pendingPose.pitch, -FREECAM_PITCH_MAX, FREECAM_PITCH_MAX);
    s_cam.px = s_cam.x; s_cam.py = s_cam.y; s_cam.pz = s_cam.z;
    s_cam.pang = s_cam.ang; s_cam.ppitch = s_cam.pitch;
    s_cam.yawRem = 0; s_cam.pitchRem = 0;
    wt_log("camera: pose set from the clipboard, %d,%d,%d ang %d pitch %d",
           s_cam.x, s_cam.y, s_cam.z, s_cam.ang, s_cam.pitch);
  }
  camera_transport_tic();
  // After the transport, so a teleport on the same tic wins.
  camera_teleport_tic();
  publish_pose();
}

static int mulscale16(int a, int ratio) {
  return (int)(((long long)a * (long long)ratio) >> 16);
}

// The engine's own frame fraction (FUN_006fa4a0): min(totalclock - ototalclock, 100) * 65536 / 100. Read, not hooked:
// the bracket has no argument to take it from.
static int smooth_ratio() {
  if (!s_pTotalClock || !s_pOTotalClock) return 0x10000;
  int d = *s_pTotalClock - *s_pOTotalClock;
  if (d < 0) d = 0;
  if (d > 100) d = 100;
  return d * 65536 / 100;
}

// Refuses when a camera sprite owns the view (viewscreen, end-of-level pan), when the player looks through something
// (newowner >= 0), and when globalcursectnum is past MAXSECTORS (displayrooms' marker for the security-monitor pass:
// substituting would put the flying camera on the monitor).
static int may_draw() {
  if (!s_ok || !s_cam.active) return 0;
  if (*s_pCameraSprite >= 0) return 0;
  if ((unsigned short)*s_pCurSec >= 0x1000u) return 0;
  const int snum = local_player();
  const unsigned char* q = player_ptr(snum);
  if (!q) return 0;
  if (*(const short*)(q + P_NEWOWNER) >= 0) return 0;
  return 1;
}

// Shared by both brackets so billboard yaw and view matrix cannot disagree; interpolates the short way round.
static int camera_interp_ang(int ratio) {
  return (int)(((long long)s_cam.pang +
                mulscale16(((s_cam.ang - s_cam.pang + 1024) & (FREECAM_ANG_UNITS - 1)) - 1024,
                           ratio)) & (FREECAM_ANG_UNITS - 1));
}

typedef void (__fastcall* SpriteUpdateFn)(void*, void*, int, void*);
static SpriteUpdateFn volatile s_origSprite = 0;
static int s_spriteOk = 0;
static int s_loggedSprite = 0;

static void __fastcall wt_sprite_update(void* self, void* edx, int spr, void* a2) {
  SpriteUpdateFn orig = s_origSprite;
  while (!orig) { YieldProcessor(); orig = s_origSprite; }
// may_draw(), not s_armed: this runs at the top of the frame, outside the render bracket.
  if (!may_draw() || !s_pAng) { orig(self, edx, spr, a2); return; }
  const short saveAng = *s_pAng;
  *s_pAng = (short)camera_interp_ang(smooth_ratio());
  if (!s_loggedSprite) {
    s_loggedSprite = 1;
    wt_log("camera: first sprite update with the camera's angle substituted -- globalang %d "
           "became %d for the billboard bake", (int)saveAng, (int)*s_pAng);
  }
  __try {
    orig(self, edx, spr, a2);
  } __finally {
    *s_pAng = saveAng;
  }
}

static void __cdecl wt_hw_drawrooms(void) {
  HwDrawRoomsFn orig = s_orig;
  while (!orig) { YieldProcessor(); orig = s_orig; }

  if (!may_draw()) { s_armed = 0; orig(); return; }

// Restored in the __finally, so no path out of the original (even an unwinding one) leaves one of the eight changed.
// POD locals only, which lets __finally sit in a /EHsc unit.
  const int   savePosX = *s_pPosX, savePosY = *s_pPosY, savePosZ = *s_pPosZ;
  const short saveAng = *s_pAng;
  const int   saveHoriz = *s_pHoriz;
  const short saveSect = *s_pCurSec;
  const int   saveSin = *s_pSinAng, saveCos = *s_pCosAng;

  const int ratio = smooth_ratio();
// The frame between tics interpolates the pair freecam_step keeps; angle goes the short way, which is why freecam_step
// collapses the pair on a flick past half a turn.
  const int cx = clamp_ll_to_int((long long)s_cam.px + mulscale16(s_cam.x - s_cam.px, ratio));
  const int cy = clamp_ll_to_int((long long)s_cam.py + mulscale16(s_cam.y - s_cam.py, ratio));
  const int cz = clamp_ll_to_int((long long)s_cam.pz + mulscale16(s_cam.z - s_cam.pz, ratio));
  const int cang = camera_interp_ang(ratio);
  const int cpitch = clamp_int(clamp_ll_to_int((long long)s_cam.ppitch +
                                               mulscale16(s_cam.pitch - s_cam.ppitch, ratio)),
                               -FREECAM_PITCH_MAX, FREECAM_PITCH_MAX);
  const int choriz = freecam_pitch_to_horiz(cpitch, s_pSin);
// FUN_007fa150 runs updatesector over the seed from the written globalpos and keeps the old value if the point is
// outside every sector.
  const int seed = (s_seedSect >= 0 && s_seedSect < (int)*s_pNumSectors) ? s_seedSect : (int)saveSect;

  *s_pPosX = cx;
  *s_pPosY = cy;
  *s_pPosZ = cz;
  *s_pAng = (short)cang;
  *s_pHoriz = choriz;
  *s_pCurSec = (short)seed;
// drawrooms derives these two from its angle argument; the scene pass copies them and drawmasks sorts on them.
  *s_pSinAng = (int)s_pSin[cang & (FREECAM_ANG_UNITS - 1)];
  *s_pCosAng = (int)s_pSin[(cang + FREECAM_ANG_UNITS / 4) & (FREECAM_ANG_UNITS - 1)];
  s_armed = 1;

  int tracked = seed;
  __try {
    orig();
    tracked = (int)*s_pCurSec;
  } __finally {
    *s_pPosX = savePosX;
    *s_pPosY = savePosY;
    *s_pPosZ = savePosZ;
    *s_pAng = saveAng;
    *s_pHoriz = saveHoriz;
    *s_pCurSec = saveSect;
    *s_pSinAng = saveSin;
    *s_pCosAng = saveCos;
    s_armed = 0;
  }

// The engine's updatesector answer is the next frame's seed: the whole of the camera's sector tracking.
  if (tracked >= 0 && tracked < (int)*s_pNumSectors) s_seedSect = tracked;

  if (!s_loggedDraw) {
    s_loggedDraw = 1;
    wt_log("camera: first substituted frame at %d,%d,%d ang %d horiz %d, sector seed %d "
           "-> engine resolved %d", cx, cy, cz, cang, choriz, seed, tracked);
  }
}

void camera_draw_player_marker(int screenW, int screenH) {
  if (!s_ok || !s_cam.active) return;
  const bool wantBody = g_cfg.freecamshowplayer != 0;
  const bool wantText = g_cfg.freecamdebug != 0;
// A refused teleport says why for a couple of seconds, not gated on the switches above: the key was pressed and
// nothing happened.
  const bool wantNotice = s_teleportNotice != TELEPORT_REASON_NONE &&
                          (wallclock_ms() - s_teleportNoticeMs) < TELEPORT_NOTICE_MS;
  if (!wantBody && !wantText && !wantNotice) return;
  if (screenW <= 0 || screenH <= 0) return;
// The matrix is the one the bracket just had the renderer build from the camera.
  if (!project_frame_begin()) return;

  const int snum = local_player();
  const PlayerPos p = player_pos(snum);

  ScreenPt pt;
  const bool haveBody = wantBody && p.valid && project_world_to_screen(p.posx, p.posy, p.posz, &pt);

  char line[96];
  if (wantText)
    _snprintf_s(line, sizeof(line), _TRUNCATE, "cam x=%d y=%d z=%d ang=%d pitch=%d",
                s_cam.x, s_cam.y, s_cam.z, s_cam.ang, s_cam.pitch);

  if (!haveBody && !wantText && !wantNotice) return;

// Its own canvas frame at the default scale, so the panel drawn after starts from its usual state.
  canvas_begin(screenW, screenH);
  canvas_set_scale(UI_SCALE_DEFAULT);
  if (haveBody) esp_draw_cross_mark(pt, canvas_player_colour(), "You", 0);
  if (wantNotice)
    canvas_text(TELEPORT_NOTICE_X, TELEPORT_NOTICE_Y, s_teleportNotice, C_HAZARD, FONT_SMALL);
  if (wantText) canvas_text(8, 8, line, canvas_player_colour(), FONT_MONO);
  canvas_end();
}

// The six-byte prologue is common, so the tail is checked too; every refusal prints the bytes found.
static int check_hw_drawrooms(const unsigned char* at) {
  if (memcmp(at, kFpHwDrawrooms, sizeof(kFpHwDrawrooms)) != 0) {
    wt_log("camera: hw drawrooms' prologue is %02X %02X %02X %02X %02X %02X, not the one we read; "
           "installing nothing", at[0], at[1], at[2], at[3], at[4], at[5]);
    return 0;
  }
  const unsigned char* tail = at + kHwDrawroomsTailAt;
  if (memcmp(tail, kHwDrawroomsTail, sizeof(kHwDrawroomsTail)) != 0) {
    wt_log("camera: hw drawrooms at +%u is %02X %02X %02X %02X %02X, not the sub esp,8 / push esi / "
           "push edi we read; installing nothing", kHwDrawroomsTailAt,
           tail[0], tail[1], tail[2], tail[3], tail[4]);
    return 0;
  }
  return 1;
}

bool camera_install() {
  s_base = (unsigned char*)GetModuleHandleA(0);
  if (!s_base) { wt_log("camera: no module base"); return false; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("camera: not a DOS image"); return false; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("camera: not a PE image"); return false; }
  s_imageSize = nt->OptionalHeader.SizeOfImage;

// The camera takes its mouse from real mouse aiming's raw accumulator and its lockout from the getinput hook.
  if (!mouseaim_installed()) {
    wt_log("camera: real mouse aiming is not installed, so there is no raw mouse to fly with "
           "and no getinput hook to hold the player still; the free camera stays off "
           "(set mouseaimon=1 in wtmod.ini)");
    return false;
  }

  s_pPosX  = (int*)  engine_ptr(kRvaGlobalPosX, sizeof(int));
  s_pPosY  = (int*)  engine_ptr(kRvaGlobalPosY, sizeof(int));
  s_pPosZ  = (int*)  engine_ptr(kRvaGlobalPosZ, sizeof(int));
  s_pAng   = (short*)engine_ptr(kRvaGlobalAng, sizeof(short));
  s_pHoriz = (int*)  engine_ptr(kRvaGlobalHoriz, sizeof(int));
  s_pCurSec = (short*)engine_ptr(kRvaGlobalCurSec, sizeof(short));
  s_pSinAng = (int*) engine_ptr(kRvaSinGlobalAng, sizeof(int));
  s_pCosAng = (int*) engine_ptr(kRvaCosGlobalAng, sizeof(int));
  s_pSin   = (const short*)engine_ptr(kRvaSinTable, 2048 * sizeof(short));
  s_pLocal = (const int*)  engine_ptr(kRvaLocalPlyr, sizeof(int));
  s_pCameraSprite = (const short*)engine_ptr(kRvaCameraSprt, sizeof(short));
  s_pNumSectors   = (const short*)engine_ptr(kRvaNumSectors, sizeof(short));
  s_pButtons = (const unsigned*)engine_ptr(kRvaButtonBits, sizeof(unsigned));
  s_pTotalClock  = (const int*)engine_ptr(kRvaTotalClock, sizeof(int));
  s_pOTotalClock = (const int*)engine_ptr(kRvaOTotalClock, sizeof(int));
  s_pFvel = (int*)engine_ptr(kRvaFvel, sizeof(int));
  s_pSvel = (int*)engine_ptr(kRvaSvel, sizeof(int));
  s_pAvel = (int*)engine_ptr(kRvaAvel, sizeof(int));
  s_pHorz = (int*)engine_ptr(kRvaHorz, sizeof(int));
  s_pRecord = (unsigned char*)engine_ptr(kRvaRecord, 12);
  const unsigned char* hw = (const unsigned char*)
      engine_ptr(kRvaHwDrawrooms, kHwDrawroomsTailAt + (unsigned)sizeof(kHwDrawroomsTail));

  if (!s_pPosX || !s_pPosY || !s_pPosZ || !s_pAng || !s_pHoriz || !s_pCurSec ||
      !s_pSinAng || !s_pCosAng || !s_pSin || !s_pLocal || !s_pCameraSprite ||
      !s_pNumSectors || !s_pButtons || !s_pTotalClock || !s_pOTotalClock ||
      !s_pFvel || !s_pSvel || !s_pAvel || !s_pHorz || !s_pRecord || !hw ||
      !engine_ptr(kRvaPlayers, 16 * P_STRIDE) ||
      !engine_ptr(kRvaSprites, 16384 * SPR_STRIDE)) {
    wt_log("camera: an engine address did not fit the image; installing nothing");
    return false;
  }
  if (!check_hw_drawrooms(hw)) return false;

  void* tramp = 0;
  if (!hook_install(kRvaHwDrawrooms, kFpHwDrawrooms, sizeof(kFpHwDrawrooms),
                    kHwDrawroomsPrologue, (void*)wt_hw_drawrooms, &tramp) || !tramp) {
    wt_log("camera: the hardware drawrooms hook did not take; the free camera stays off");
    return false;
  }
  s_orig = (HwDrawRoomsFn)tramp;
  s_ok = 1;

// Its absence costs only sprite orientation, so a refusal is logged and the camera still installs.
  {
    const unsigned char* at = (const unsigned char*)
        engine_ptr(kRvaSpriteUpdate, kSpriteUpdatePrologue + (unsigned)sizeof(kFpSpriteUpdateTail));
    if (!at) {
      wt_log("camera: rva 0x%X is not inside the image; sprites keep the player's facing",
             kRvaSpriteUpdate);
    } else if (memcmp(at, kFpSpriteUpdate, sizeof(kFpSpriteUpdate)) != 0) {
      wt_log("camera: rva 0x%X is %02X %02X %02X %02X %02X %02X, not the push ebp / mov ebp,esp / "
             "and esp,-8 we read; sprites keep the player's facing", kRvaSpriteUpdate,
             at[0], at[1], at[2], at[3], at[4], at[5]);
    } else if (memcmp(at + kSpriteUpdatePrologue, kFpSpriteUpdateTail,
                      sizeof(kFpSpriteUpdateTail)) != 0) {
      const unsigned char* t = at + kSpriteUpdatePrologue;
      wt_log("camera: rva 0x%X at +%u is %02X %02X %02X %02X %02X %02X, not the mov eax,[ebp+8] / "
             "push esi / push edi we read -- the drawrooms body shares the first six bytes, so "
             "this tail is what tells them apart; sprites keep the player's facing",
             kRvaSpriteUpdate, kSpriteUpdatePrologue, t[0], t[1], t[2], t[3], t[4], t[5]);
    } else {
      void* stramp = 0;
      if (hook_install(kRvaSpriteUpdate, kFpSpriteUpdate, sizeof(kFpSpriteUpdate),
                       kSpriteUpdatePrologue, (void*)wt_sprite_update, &stramp) && stramp) {
        s_origSprite = (SpriteUpdateFn)stramp;
        s_spriteOk = 1;
        wt_log("camera: rva 0x%X is the per-sprite geometry update, and while the camera flies it "
               "runs with globalang substituted so face sprites billboard toward the camera "
               "instead of the player. globalang ALONE -- it is the only one of the eight that "
               "anything in that subtree reads", kRvaSpriteUpdate);
      } else {
        wt_log("camera: the sprite update hook did not take; sprites keep the player's facing");
      }
    }
  }
  wt_log("camera: installed. rva 0x%X is the hardware drawrooms body (one caller, 0x00756f85); "
         "the bracket writes globalpos x/y/z, globalang, globalhoriz, globalcursectnum and the "
         "two angle trig globals and puts all eight back, and the engine's own updatesector "
         "inside it (FUN_007fa150) is what re-seeds the portal walk from the camera's position",
         kRvaHwDrawrooms);
  wt_log("camera: toggle key vk 0x%02X, speed %d%% of %d units a tic, feature %s",
         g_cfg.freecamkey, g_cfg.freecamspeed, FREECAM_BASE_SPEED,
         g_cfg.freecamon ? "on" : "off (the View tab's Free camera row arms it)");
  return true;
}

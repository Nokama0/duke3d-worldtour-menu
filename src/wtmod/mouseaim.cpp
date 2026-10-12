// Real mouse aiming. The game's own path samples ~13 times a second, clamps the axis to +-127 and divides x by screen
// WIDTH, y by HEIGHT, so this takes the mouse away from it. Record writes happen only in the getinput hook,
// player-state writes only in the processinput hook (same thread/tic).
#include "mouseaim.h"
#include "config.h"
#include "hook.h"
#include "input.h"
#include "firerate.h"
#include "wswitch.h"
#include "trigger.h"
#include "secretpath.h"
#include "tbot.h"
#include "aim.h"
#include "target.h"
#include "freecam.h"
#include "camera.h"
#include <windows.h>
#include <string.h>
#include "mousebind.h"

static const unsigned kRvaGetMouseDelta = 0x37460;    // CONTROL_GetMouseDelta
static const unsigned kRvaProcessInput  = 0x7A7A0;    // __fastcall(short snum)
// __fastcall(short snum in CX).
static const unsigned kRvaGetInput      = 0x79820;
// The 12-byte input record getinput fills; the same twelve bytes are sync[]'s stride.
static const unsigned kRvaInputRecord   = 0x73E1B0;
// getinput's own pre-rotation strafe, already clamped to +-90 when stored.
static const unsigned kRvaSvelGlobal    = 0x5F74BC;   // int32
// The button state getinput tests for the strafe modifier (bit 4); smoothview.cpp reads the same bit.
static const unsigned kRvaButtonBits    = 0x5DB314;   // uint32
static const unsigned kRvaSinTable      = 0xD4B230;
static const unsigned kRvaMouseSource   = 0xEDEF20;   // pointer to the accumulator object
static const unsigned kRvaAxisX         = 0x1113E10;  // MouseAxes[0].analog, int32
static const unsigned kRvaAxisY         = 0x1113E18;  // MouseAxes[1].analog, int32
static const unsigned kRvaLocalPlayer   = 0x7824CC;   // int32
static const unsigned kRvaPlayers       = 0x5FD5C0;   // player[0], stride 0x408
static const unsigned kRvaPauseFlag     = 0xEDFC76;   // int16; non-zero stops the tic entirely
static const unsigned kRvaCameraSprite  = 0xEDFC7A;   // int16; < 0 is the player's own view
// getinput negates the pitch unless the invert setting for the active device is on; we bypass getinput, so we obey it.
static const unsigned kRvaInvertPrimary = 0xEE0080;   // int32
static const unsigned kRvaInvertAlt     = 0xEE0084;   // int32

#define P_HORIZ     0x220   // int32, centre 100, engine range [-99, 299]
#define P_AIMMODE   0x288   // int32; 0 makes processinput snap the horizon back to 100
#define P_ANG       0x2cc   // int16, 0..2047
#define P_NEWOWNER  0x306   // int16, >= 0 means a viewscreen owns the view
// With 0x11 set (menu up or typing) getinput zeroes the record and returns, so a strafe has nothing to add to.
#define P_GM        0x398   // int8
#define P_STRIDE    0x408

#define REC_AVEL    0       // int8
#define REC_HORZ    1       // int8
#define REC_FVEL    2       // int16, world axis
#define REC_SVEL    4       // int16, world axis
#define REC_PAD     6       // int16, no reference anywhere in the image
#define REC_BITS    8       // int32
// Duke's fire button, bit 2 of the record's bits (the game's netcode format, not an address).
#define REC_BIT_FIRE 4
#define REC_SIZE    12

// CONTROL_GetMouseDelta: push ebp; mov ebp,esp; sub esp,8. Six bytes, the first boundary at or past a 5-byte JMP.
static const unsigned char kFpMouseDelta[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08 };
static const unsigned kMouseDeltaPrologue = 6;

// processinput: push ebp; mov ebp,esp; push -1 -- exactly five bytes, nothing to pad.
static const unsigned char kFpProcessInput[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
static const unsigned kProcessInputPrologue = 5;
// Five bytes is a common prologue, so the SEH frame behind it is checked first (push <scope table>; mov eax,fs:[0]; push
// eax; sub esp,0xAC); the pushed address is absolute, so it is compared against this process's base.
static const unsigned kRvaProcessInputSeh = 0x22A4D0;   // the scope table it pushes
static const unsigned char kProcessInputTail[] = {
  0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x81, 0xEC, 0xAC, 0x00, 0x00, 0x00
};

// getinput: push ebx; mov ebx,esp; sub esp,8 -- the MSVC align-the-stack prologue.
static const unsigned char kFpGetInput[] = { 0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08 };
static const unsigned kGetInputPrologue = 6;
// Those bytes start many MSVC functions, so the sixteen at +25 are checked too: movsx eax,cx, lea ecx,[ebp-0x40], imul
// esi,eax,0x408 (player stride) and the ADD ESI,imm32 whose operand is player[0], compared against this process's base.
static const unsigned kGetInputTailAt = 25;
static const unsigned char kGetInputTail[] = {
  0x0F, 0xBF, 0xC1, 0x8D, 0x4D, 0xC0, 0x56, 0x69, 0xF0, 0x08, 0x04, 0x00, 0x00, 0x57, 0x81, 0xC6
};
static const unsigned kGetInputPlayersAt = 41;   // the imm32 of that ADD ESI

// Vtable slot +0x14 of the accumulator object, __thiscall (callee RET 0x8).
typedef void (__thiscall* DrainFn)(void* self, float* outX, float* outY);
typedef void (__cdecl*    MouseDeltaFn)(void);
typedef void (__fastcall* ProcessInputFn)(short snum);
typedef void (__fastcall* GetInputFn)(short snum);

static unsigned char* s_base = 0;
static DWORD          s_imageSize = 0;
static void**    s_pSource  = 0;
static int*      s_pAxisX   = 0;
static int*      s_pAxisY   = 0;
static const int*   s_pLocal  = 0;
static const short* s_pPause  = 0;
static const short* s_pCamera = 0;
static const int*   s_pInvertPrimary = 0;
static const int*   s_pInvertAlt = 0;
static unsigned char*  s_pRecord  = 0;   // the 12-byte input record getinput fills
static const int*      s_pSvel    = 0;   // getinput's own pre-rotation strafe
static const unsigned* s_pButtons = 0;   // bit 4 is the strafe modifier
static const short*    s_pSin     = 0;   // 2048 entries
static MouseDeltaFn   volatile s_origMouseDelta   = 0;
static ProcessInputFn volatile s_origProcessInput = 0;
static GetInputFn     volatile s_origGetInput     = 0;

// Written by the draw thread, read on the window thread and in the tic: aligned loads and interlocked stores.
static volatile LONG s_enabled   = 1;
static volatile LONG s_sens      = MOUSEAIM_SENS_DEFAULT;
static volatile LONG s_vscale    = MOUSEAIM_VSCALE_DEFAULT;
static volatile LONG s_installed = 0;
static volatile LONG s_polls     = 0;
static volatile LONG s_zeroed    = 0;
static volatile LONG s_logged    = 0;
static volatile LONG s_strafeLogged = 0;

// The window thread is the only producer; the tic drains it with one InterlockedExchange (no count read twice or lost).
// The drawn view only peeks.
static volatile LONG s_rawX = 0, s_rawY = 0;
static const LONG kRawMax = 1 << 20;

static MouseAimState s_state = { 0.0, 0.0, 0.0 };
// What the AIM is owed: getinput drains the counters into these once a tic and processinput spends them. Plain ints:
// only the game's main loop touches them.
static int s_owedX = 0, s_owedY = 0;

void mouseaim_set_enabled(int on)     { InterlockedExchange(&s_enabled, on ? 1 : 0); }
void mouseaim_set_sens(int sens)      { InterlockedExchange(&s_sens, mouseaim_sens_clamped(sens)); }
void mouseaim_set_vscale(int vscale)  { InterlockedExchange(&s_vscale, mouseaim_vscale_clamped(vscale)); }
int  mouseaim_installed()             { return s_installed != 0; }
long mouseaim_poll_count()            { return (long)s_polls; }
long mouseaim_zeroed_count()          { return (long)s_zeroed; }

static void* engine_ptr(unsigned rva, unsigned size) {
  if (!s_base || rva > s_imageSize || size > s_imageSize - rva) {
    wt_log("mouseaim: rva 0x%X (%u bytes) is outside the image", rva, size);
    return 0;
  }
  return s_base + rva;
}

static int taking_the_mouse() {
  return s_installed != 0 && s_enabled != 0 && input_raw_ready() != 0;
}

// An untrusted source answers "not inverted".
static bool pitch_flipped() {
  if (!s_pSource || !s_pInvertPrimary || !s_pInvertAlt) return false;
  const unsigned char* src = (const unsigned char*)*s_pSource;
  if (!src) return false;
  const int idx = *(const int*)(src + 0x3E8);
  if (idx < 0 || idx > 63) return false;
  const int device = *(const int*)(src + 0x2C + idx * 4);
  return ((device == 1) ? *s_pInvertAlt : *s_pInvertPrimary) != 0;
}

// An untrusted source answers "not held".
static int strafe_held() {
  return s_pButtons && (*s_pButtons & 0x10u) != 0;
}

static int local_player() {
  if (!s_pLocal) return -1;
  const int n = *s_pLocal;
  return (n >= 0 && n <= 15) ? n : -1;
}

// The camera flies on the same raw counts, so it keeps the accumulator alive even with the aim's switch off.
static int camera_wants_mouse() {
  return s_installed != 0 && camera_active() && input_raw_ready() != 0;
}

static int collecting_now() {
  if (!taking_the_mouse() && !camera_wants_mouse()) return 0;
  if (input_menu_open()) return 0;                 // the panel owns the mouse
  if (s_pPause && *s_pPause != 0) return 0;        // paused: no tic runs, so nothing would drain
  if (s_pCamera && *s_pCamera >= 0) return 0;      // a camera sprite owns the view
  return 1;
}

static void raw_zero() {
  InterlockedExchange(&s_rawX, 0);
  InterlockedExchange(&s_rawY, 0);
}

// Refuses to add past the ceiling rather than clamping after, since a clamp could race the tic's exchange and put taken
// motion back.
static void raw_add(volatile LONG* acc, int delta) {
  const LONG cur = *acc;
  if (cur > kRawMax || cur < -kRawMax) return;
  InterlockedExchangeAdd(acc, (LONG)delta);
}

void mouseaim_raw_counts(int dx, int dy) {
  static int s_was = 0;   // window thread only
  if (!collecting_now()) {
    if (s_was) { raw_zero(); s_was = 0; }
    return;
  }
  if (!s_was) { raw_zero(); s_was = 1; }
  raw_add(&s_rawX, dx);
  raw_add(&s_rawY, dy);
}

static int mouseaim_tic_recent();

int mouseaim_view_pending(double horizNowUnits, double* yawUnits, double* horizUnits) {
  if (yawUnits)   *yawUnits = 0.0;
  if (horizUnits) *horizUnits = 0.0;
  if (!taking_the_mouse()) return 0;
  if (camera_active()) return 1;
  // NO TIC, NO PENDING TERM: this shows the motion the NEXT tic will apply, so with the tic clock stopped (the pause
  // menu stops processinput) counts pile up and the view would keep turning. Keyed on the tic clock, not a menu flag.
  if (!mouseaim_tic_recent()) return 1;
  const int sens = (int)s_sens, vscale = (int)s_vscale;
  const int dx = (int)s_rawX + s_owedX, dy = (int)s_rawY + s_owedY;
  if (yawUnits && !strafe_held())
    *yawUnits = mouseaim_clamp_pending(s_state.yawPending + dx * mouseaim_yaw_units_per_count(sens),
                                       MOUSEAIM_YAW_PENDING_MAX);
  const int snum = local_player();
  if (horizUnits && snum >= 0) {
    const unsigned char* p = s_base + kRvaPlayers + (unsigned)snum * P_STRIDE;
    if (*(const int*)(p + P_AIMMODE) != 0)
      *horizUnits = mouseaim_clamp_pending(
          s_state.horizPending + mouseaim_pitch_units_delta(dy, sens, vscale, pitch_flipped(),
                                                            horizNowUnits),
          MOUSEAIM_HORIZ_PENDING_MAX);
  }
  return 1;
}

// Runs instead of CONTROL_GetMouseDelta (FUN_006e7460): DRAINS the game's accumulator and zeroes both axes. No disk on
// the game thread.
static void __cdecl wt_mouse_delta(void) {
  InterlockedIncrement(&s_polls);
  // Silenced while this feature drives the view, while the panel is open, and while the camera flies (else the 13 Hz
  // pipeline turns the player behind the camera).
  if (!taking_the_mouse() && !input_menu_open() && !camera_active()) {
    MouseDeltaFn orig = s_origMouseDelta;
    while (!orig) { YieldProcessor(); orig = s_origMouseDelta; }
    orig();
    return;
  }
  void* source = *s_pSource;
  if (source) {
    float x = 0.0f, y = 0.0f;
    DrainFn drain = (DrainFn)(*(void***)source)[0x14 / sizeof(void*)];
    drain(source, &x, &y);
  }
  *s_pAxisX = 0;
  *s_pAxisY = 0;
  InterlockedIncrement(&s_zeroed);
}

// WHEN THE TIC CLOCK LAST TURNED: written at the processinput tail, read on the RENDER thread to tell a stopped
// simulation from one between tics. Four tics: frame drops never trip it, a stopped clock is caught fast.
static volatile LONG s_lastTicMs = 0;
static const unsigned MOUSEAIM_TIC_STALE_MS = 134;   // four tics

static void mouseaim_note_tic() {
  InterlockedExchange(&s_lastTicMs, (LONG)GetTickCount());
}

static int mouseaim_tic_recent() {
  const unsigned last = (unsigned)InterlockedCompareExchange(&s_lastTicMs, 0, 0);
  if (!last) return 0;
  return ((unsigned)GetTickCount() - last) < MOUSEAIM_TIC_STALE_MS;
}

static int tic_owns_mouse(int snum) {
  if (!taking_the_mouse() || input_menu_open()) return 0;
  if (s_pPause && *s_pPause != 0)   return 0;
  if (s_pCamera && *s_pCamera >= 0) return 0;
  const unsigned char* p = s_base + kRvaPlayers + (unsigned)snum * P_STRIDE;
  if (*(const short*)(p + P_NEWOWNER) >= 0) return 0;
  // THE GAME'S OWN MENU, not Duke's pause flag: the engine stops turning the player in its menu or console and this
  // replacement must stop too.
  if ((*(const unsigned char*)(p + P_GM) & 0x11) != 0) return 0;
  return 1;
}

static void apply_tic(int snum) {
  const int dx = s_owedX, dy = s_owedY;
  s_owedX = 0;
  s_owedY = 0;
  if (!tic_owns_mouse(snum)) { mouseaim_reset(&s_state); return; }

  unsigned char* p = s_base + kRvaPlayers + (unsigned)snum * P_STRIDE;
  int* pHoriz = (int*)(p + P_HORIZ);
  short* pAng = (short*)(p + P_ANG);
  const int horizNow = mouseaim_horiz_clamped(*pHoriz);
  const bool aimMode = *(const int*)(p + P_AIMMODE) != 0;
  const MouseAimTic t = mouseaim_tic(&s_state, dx, dy, (int)s_sens, (int)s_vscale,
                                     pitch_flipped(), aimMode, horizNow);
  if (t.angAdd)   *pAng   = (short)((*pAng + t.angAdd) & 0x7FF);
  if (t.horizAdd) *pHoriz = mouseaim_horiz_clamped(horizNow + t.horizAdd);

  if ((t.angAdd || t.horizAdd) && InterlockedCompareExchange(&s_logged, 1, 0) == 0)
    wt_log("mouseaim: live. snum=%d counts %d,%d -> ang +%d horiz +%d "
           "(sens=%d vscale=%d aimmode=%d flip=%d horiz was %d)",
           snum, dx, dy, t.angAdd, t.horizAdd, (int)s_sens, (int)s_vscale,
           aimMode ? 1 : 0, pitch_flipped() ? 1 : 0, horizNow);
}

static int owed_add(int cur, int add) {
  if (cur > kRawMax || cur < -kRawMax) return cur;
  return cur + add;
}

static void strafe_tic(int snum) {
  const unsigned char* p = s_base + kRvaPlayers + (unsigned)snum * P_STRIDE;
  if ((*(const unsigned char*)(p + P_GM) & 0x11) != 0) return;
  const int svelNow = mouseaim_svel_clamped(*s_pSvel);
  const int add = mouseaim_strafe_tic(&s_state, s_owedX, (int)s_sens, svelNow);
  const int spent = s_owedX;
  s_owedX = 0;      // spent sideways
  if (!add) return;

  const int ang = (int)*(const short*)(p + P_ANG) & 0x7FF;
  short* recFvel = (short*)(s_pRecord + REC_FVEL);
  short* recSvel = (short*)(s_pRecord + REC_SVEL);
  mouseaim_strafe_into_record(recFvel, recSvel, add,
                              (int)s_pSin[ang], (int)s_pSin[(ang + 1536) & 0x7FF]);
  if (InterlockedCompareExchange(&s_strafeLogged, 1, 0) == 0)
    wt_log("mouseaim: strafe live. snum=%d counts %d -> svel %+d (engine had %d, ang %d), "
           "record at rva 0x%X now fvel=%d svel=%d",
           snum, spent, add, svelNow, ang, kRvaInputRecord, (int)*recFvel, (int)*recSvel);
}

// The triggerbot's one write, on the record after the original getinput.
void input_set_fire_bit() {
  if (!s_pRecord) return;
  *(int*)(s_pRecord + REC_BITS) |= REC_BIT_FIRE;
}

// HOLD VIEW. The aimbot writes ang/horiz at the previous tic's processinput tail; the engine adds the player's turn
// from this record at the TOP of this tic's, before shoot, so a turn drags the ray off the target. Only avel and horz
// are cleared.
static void input_clear_turn() {
  if (!s_pRecord) return;
  *(signed char*)(s_pRecord + REC_AVEL) = 0;
  *(signed char*)(s_pRecord + REC_HORZ) = 0;
}

// Logs every CHANGE of the record's bit mask and names the bits that went up, so a wrong REC_BIT_FIRE shows as a
// number. The engine's config parser stores mouse indices 0..4 only (rva 0x32187), so thumb buttons are written here.
static void tbot_report_bits() {
  static int reports = 0;
  static int lastBits = 0;
  if (!s_pRecord || g_cfg.debuglog == 0) return;
  const int bits = *(const int*)(s_pRecord + REC_BITS);
  if (bits == lastBits) return;
  const int wentUp = bits & ~lastBits;
  lastBits = bits;
  if (!wentUp || reports >= 40) return;
  reports++;
  int lowest = 0;
  while (lowest < 31 && !(wentUp & (1 << lowest))) lowest++;
  wt_log("input bits: 0x%08X (bit %d went up, mask 0x%08X)%s",
         bits, lowest, wentUp, (wentUp & REC_BIT_FIRE) ? "  <- fire, the known one" : "");
}

// getinput (FUN_00729820): AFTER the original, the WM_INPUT counters drain into statics; with the strafe modifier held
// the horizontal counts are spent here on the record's velocity fields instead of left for the aim.
static void __fastcall wt_getinput(short snum) {
  camera_release_tic();
  GetInputFn orig = s_origGetInput;
  while (!orig) { YieldProcessor(); orig = s_origGetInput; }
  orig(snum);
  s_owedX = owed_add(s_owedX, (int)InterlockedExchange(&s_rawX, 0));
  s_owedY = owed_add(s_owedY, (int)InterlockedExchange(&s_rawY, 0));
  const int n = (int)snum;
  if (n < 0 || n > 15 || n != local_player()) return;
  // Right after the real getinput filled the record, before anything is spent; writes the record, no player state.
  camera_toggle_tic(n);
  if (camera_active()) {
    camera_feed_mouse(s_owedX, s_owedY);
    s_owedX = 0;
    s_owedY = 0;
    mouseaim_reset(&s_state);
    return;
  }
  // THE TRIGGERBOT'S WINDOW: the only point where the fire bit is settable and honoured this tic.
  tbot_report_bits();
  // THE THUMB BUTTONS' ACTIONS, same window (mousebind.h). OR, never assign: the player's own keys are in this mask.
  if (s_pRecord) {
    const unsigned add = mousebind_bits(g_cfg.mouse4action, input_mouse4_down(),
                                        g_cfg.mouse5action, input_mouse5_down());
    if (add) *(int*)(s_pRecord + REC_BITS) |= (int)add;
  }
  // Hold view, before the triggerbot: it locks this tic's turn whatever produced it.
  {
    const bool holdView = aim_hold_view_rule(aim_view_held(), input_menu_open(),
                                             camera_active());
    aim_view_hold_note(holdView);
    if (holdView) input_clear_turn();
  }
  {
    PlayerAim pa;
    // This hook still runs every FRAME while paused: without the term a paused crosshair on an enemy would OR the fire
    // bit in every frame.
    const bool paused = s_pPause && *s_pPause != 0;
    if (camera_build_player_aim(&pa)) tbot_tic(pa, camera_active(), paused);
  }

  if (!tic_owns_mouse(n) || !strafe_held()) return;
  strafe_tic(n);
}

// processinput (FUN_0072a7a0): AFTER the original, what the aim is owed becomes whole build units of ang/horiz on the
// LOCAL player; runs after getinput within a tic.
static void __fastcall wt_processinput(short snum) {
  ProcessInputFn orig = s_origProcessInput;
  while (!orig) { YieldProcessor(); orig = s_origProcessInput; }
  orig(snum);
  const int n = (int)snum;
  if (n < 0 || n > 15 || n != local_player()) return;
  if (camera_active()) {
    camera_tic();
    s_owedX = 0;
    s_owedY = 0;
    mouseaim_reset(&s_state);
  } else {
    apply_tic(n);
  }
  mouseaim_note_tic();
  camera_light_tic();
  // Same clock, after the original; each gates itself on the camera and its slider. freezeActive is false: this port has
  // no view freeze yet.
  firerate_tic(n, false);
  wswitch_tic(n, false);
  // Both only read MAP memory, so the local-player gate above is all they need.
  trigger_tic();
  secret_volume_tic();
  secretpath_tic();
  if (!camera_active()) aim_run_tic();
  camera_release_tic();
}

// The 5-byte prologue is common, so the SEH frame behind it is checked too.
static int check_processinput(const unsigned char* at) {
  if (memcmp(at, kFpProcessInput, sizeof(kFpProcessInput)) != 0) {
    wt_log("mouseaim: processinput's prologue is %02X %02X %02X %02X %02X, not the one we read; "
           "installing nothing", at[0], at[1], at[2], at[3], at[4]);
    return 0;
  }
  if (at[kProcessInputPrologue] != 0x68) {
    wt_log("mouseaim: processinput does not push an SEH scope table (found %02X); "
           "installing nothing", at[kProcessInputPrologue]);
    return 0;
  }
  const unsigned found = *(const unsigned*)(at + kProcessInputPrologue + 1);
  const unsigned want  = (unsigned)(UINT_PTR)(s_base + kRvaProcessInputSeh);
  if (found != want) {
    wt_log("mouseaim: processinput pushes 0x%08X, not the relocated 0x%08X; installing nothing",
           found, want);
    return 0;
  }
  // The push is FIVE bytes (opcode plus a four-byte operand), so what follows begins at prologue + 5.
  const unsigned tailAt = kProcessInputPrologue + 5;
  if (memcmp(at + tailAt, kProcessInputTail, sizeof(kProcessInputTail)) != 0) {
    wt_log("mouseaim: processinput's SEH frame is %02X %02X %02X %02X, not the one we read; "
           "installing nothing", at[tailAt], at[tailAt + 1], at[tailAt + 2], at[tailAt + 3]);
    return 0;
  }
  return 1;
}

static int check_getinput(const unsigned char* at) {
  if (memcmp(at, kFpGetInput, sizeof(kFpGetInput)) != 0) {
    wt_log("mouseaim: getinput's prologue is %02X %02X %02X %02X %02X %02X, not the one we read; "
           "installing nothing", at[0], at[1], at[2], at[3], at[4], at[5]);
    return 0;
  }
  const unsigned char* tail = at + kGetInputTailAt;
  if (memcmp(tail, kGetInputTail, sizeof(kGetInputTail)) != 0) {
    wt_log("mouseaim: getinput at +%u is %02X %02X %02X %02X %02X %02X %02X %02X, not the "
           "movsx eax,cx / imul esi,eax,0x408 we read; installing nothing", kGetInputTailAt,
           tail[0], tail[1], tail[2], tail[3], tail[4], tail[5], tail[6], tail[7]);
    return 0;
  }
  const unsigned found = *(const unsigned*)(at + kGetInputPlayersAt);
  const unsigned want  = (unsigned)(UINT_PTR)(s_base + kRvaPlayers);
  if (found != want) {
    wt_log("mouseaim: getinput adds 0x%08X to esi, not the relocated player[0] at 0x%08X; "
           "installing nothing", found, want);
    return 0;
  }
  return 1;
}

void mouseaim_install(int sens, int vscale) {
  s_sens   = mouseaim_sens_clamped(sens);
  s_vscale = mouseaim_vscale_clamped(vscale);

  s_base = (unsigned char*)GetModuleHandleA(0);
  if (!s_base) { wt_log("mouseaim: no module base"); return; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("mouseaim: not a DOS image"); return; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("mouseaim: not a PE image"); return; }
  s_imageSize = nt->OptionalHeader.SizeOfImage;

  s_pSource = (void**)engine_ptr(kRvaMouseSource, sizeof(void*));
  s_pAxisX  = (int*)  engine_ptr(kRvaAxisX, sizeof(int));
  s_pAxisY  = (int*)  engine_ptr(kRvaAxisY, sizeof(int));
  s_pLocal  = (const int*)  engine_ptr(kRvaLocalPlayer, sizeof(int));
  s_pPause  = (const short*)engine_ptr(kRvaPauseFlag, sizeof(short));
  s_pCamera = (const short*)engine_ptr(kRvaCameraSprite, sizeof(short));
  s_pInvertPrimary = (const int*)engine_ptr(kRvaInvertPrimary, sizeof(int));
  s_pInvertAlt     = (const int*)engine_ptr(kRvaInvertAlt, sizeof(int));
  s_pRecord  = (unsigned char*) engine_ptr(kRvaInputRecord, REC_SIZE);
  s_pSvel    = (const int*)     engine_ptr(kRvaSvelGlobal, sizeof(int));
  s_pButtons = (const unsigned*)engine_ptr(kRvaButtonBits, sizeof(unsigned));
  s_pSin     = (const short*)   engine_ptr(kRvaSinTable, 2048 * sizeof(short));
  const unsigned char* pi = (const unsigned char*)engine_ptr(kRvaProcessInput,
                                                             kProcessInputPrologue + 19);
  const unsigned char* gi = (const unsigned char*)engine_ptr(kRvaGetInput,
                                                             kGetInputPlayersAt + 4);
  if (!s_pSource || !s_pAxisX || !s_pAxisY || !s_pLocal || !s_pPause || !s_pCamera ||
      !s_pInvertPrimary || !s_pInvertAlt || !s_pRecord || !s_pSvel || !s_pButtons ||
      !s_pSin || !pi || !gi ||
      !engine_ptr(kRvaPlayers, 16 * P_STRIDE) || !engine_ptr(kRvaProcessInputSeh, 4)) {
    wt_log("mouseaim: an engine address did not fit the image; installing nothing");
    return;
  }
  if (!check_processinput(pi) || !check_getinput(gi)) return;

  void* tramp = 0;
  if (!hook_install(kRvaProcessInput, kFpProcessInput, sizeof(kFpProcessInput),
                    kProcessInputPrologue, (void*)wt_processinput, &tramp) || !tramp) {
    wt_log("mouseaim: processinput did not take; installing nothing");
    return;
  }
  s_origProcessInput = (ProcessInputFn)tramp;

  void* tramp3 = 0;
  if (!hook_install(kRvaGetInput, kFpGetInput, sizeof(kFpGetInput),
                    kGetInputPrologue, (void*)wt_getinput, &tramp3) || !tramp3) {
    wt_log("mouseaim: getinput did not take; installing nothing");
    return;
  }
  s_origGetInput = (GetInputFn)tramp3;

  void* tramp2 = 0;
  if (!hook_install(kRvaGetMouseDelta, kFpMouseDelta, sizeof(kFpMouseDelta),
                    kMouseDeltaPrologue, (void*)wt_mouse_delta, &tramp2)) {
    wt_log("mouseaim: CONTROL_GetMouseDelta did not take; the tic hook adds nothing");
    return;
  }
  s_origMouseDelta = (MouseDeltaFn)tramp2;

  mouseaim_reset(&s_state);
  raw_zero();
  s_owedX = 0;
  s_owedY = 0;
  s_installed = 1;
  wt_log("mouseaim: installed. processinput rva 0x%X is __fastcall(short snum in CX) and "
         "identifies its player by snum == *(int*)(base + 0x%X), read from the dump at "
         "0x0072ccee and 0x0072b3c1; sens=%d (%d.%03d deg/count of yaw) vscale=%d%%",
         kRvaProcessInput, kRvaLocalPlayer, (int)s_sens, (int)s_sens / 1000, (int)s_sens % 1000,
         (int)s_vscale);
  wt_log("mouseaim: getinput rva 0x%X is __fastcall(short snum in CX) too, called at 0x006ea267 "
         "with the local index; strafe modifier is bit 4 of base + 0x%X; record at base + 0x%X "
         "(avel +0, horz +1, fvel +2, svel +4, bits +8); %.3f svel per raw count",
         kRvaGetInput, kRvaButtonBits, kRvaInputRecord,
         mouseaim_svel_units_per_count((int)s_sens));
}

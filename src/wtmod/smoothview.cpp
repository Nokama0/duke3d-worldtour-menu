// The fractional view angle: engine reads, three hooks, two stubs. Two MULSS in the camera build (FUN_007b9870) turn
// integer yaw/pitch into float degrees; each jumps to a stub adding a fraction computed per frame at displayrooms'
// entry (interpolation remainder plus unspent mouse motion). Engine memory is read only.
#include "smoothview.h"
#include "hook.h"
#include "mouseaim.h"
#include "freecam.h"    // camera_active
#include <windows.h>
#include "light.h"

// RVAs (the game loads at a randomised base), read from the runtime dump at image base 0x6B0000.
static const unsigned kRvaDisplayrooms  = 0x3DAC0;    // __fastcall(short snum, int smoothratio)
static const unsigned kRvaMulssYaw      = 0x109CAD;   // MULSS XMM0,[360/2048]
static const unsigned kRvaMulssPitch    = 0x109CEA;   // MULSS XMM2,[360/2048]
static const unsigned kRvaDegPerUnit    = 0x29D8E8;   // the 0.17578125f both read
static const unsigned kRvaPlayers       = 0x5FD5C0;   // player[0], stride 0x408
static const unsigned kRvaPauseFlag     = 0xEDFC76;   // int16; non-zero forces ratio 65536
static const unsigned kRvaCameraSprite  = 0xEDFC7A;   // int16; < 0 is the player's own view
// getinput's two leftovers: mouse counts not yet worth a whole unit of turn or horizon.
static const unsigned kRvaYawRemainder   = 0xC827C8;  // int32
static const unsigned kRvaPitchRemainder = 0xC82578;  // int32
static const unsigned kRvaButtonBits     = 0x5DB314;  // uint32; bit 4 sends mouse x to strafe
static const unsigned kRvaMouseAimOn     = 0x782564;  // int32; 0 leaves the pitch leftover stale
static const unsigned kRvaInputSource    = 0xEDEF20;  // the input object getinput asks
static const unsigned kRvaInvertPrimary  = 0xEE0080;  // int32
static const unsigned kRvaInvertAlt      = 0xEE0084;  // int32

// Player struct offsets, confirmed against the tic's backup at 0x0072a9e2 (ohoriz, ohorizoff) and 0x0072b30e (oang).
#define P_HORIZ      0x220   // int32
#define P_OHORIZ     0x224   // int32
#define P_OHORIZOFF  0x228   // int32
#define P_ANG        0x2cc   // int16
#define P_OANG       0x2ce   // int16
#define P_LOOKANG    0x2d4   // int16, added to the interpolated yaw
#define P_HORIZOFF   0x300   // int16, read signed by displayrooms
#define P_NEWOWNER   0x306   // int16, >= 0 means a camera, not the player
#define P_ONCRANE    0x326   // int16, >= 0 forces ratio 65536

// The first four bytes of each MULSS; the last four are a relocated operand, checked separately.
static const unsigned char kFpYaw[]   = { 0xF3, 0x0F, 0x59, 0x05 };
static const unsigned char kFpPitch[] = { 0xF3, 0x0F, 0x59, 0x15 };
static const unsigned kMulssLen = 8;

// push ebx; mov ebx,esp; sub esp,8: six bytes, the first instruction boundary at or past a five-byte JMP.
static const unsigned char kFpDisplayrooms[] = { 0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08 };
static const unsigned kDisplayroomsPrologue = 6;

// Read by the two naked stubs, written once per frame by the hook.
static float s_fracYaw    = 0.0f;
static float s_fracPitch  = 0.0f;
static float s_degPerUnit = kSmoothDegPerUnit;
static void* s_retYaw     = 0;
static void* s_retPitch   = 0;

// Mid-function stubs must clobber nothing: ECX/EDX hold pending getangle arguments at the yaw site and XMM0 is a live
// camera coordinate at the pitch site. LEA keeps EFLAGS, MOVUPS because the stack is 4-byte aligned, and the jump back
// is indirect since the return address needs the base.
__declspec(naked) static void wt_mulss_yaw() {
  __asm {
    lea    esp, [esp - 16]
    movups [esp], xmm7
    movss  xmm7, dword ptr [s_fracYaw]
    addss  xmm0, xmm7
    movss  xmm7, dword ptr [s_degPerUnit]
    mulss  xmm0, xmm7
    movups xmm7, [esp]
    lea    esp, [esp + 16]
    jmp    dword ptr [s_retYaw]
  }
}

__declspec(naked) static void wt_mulss_pitch() {
  __asm {
    lea    esp, [esp - 16]
    movups [esp], xmm7
    movss  xmm7, dword ptr [s_fracPitch]
    addss  xmm2, xmm7
    movss  xmm7, dword ptr [s_degPerUnit]
    mulss  xmm2, xmm7
    movups xmm7, [esp]
    lea    esp, [esp + 16]
    jmp    dword ptr [s_retPitch]
  }
}

static unsigned char* s_base = 0;
static DWORD          s_imageSize = 0;
static int            s_live = 0;     // every hook took; otherwise the fractions stay zero
// The panel's switch. Off, the stubs add zero: the game's own picture.
static volatile LONG  s_enabled = 1;

void smoothview_set_enabled(int on) { InterlockedExchange(&s_enabled, on ? 1 : 0); }
int  smoothview_installed() { return s_live; }

static const short*    s_pPause  = 0;
static const short*    s_pCamera = 0;
static const int*      s_pYawRem = 0;
static const int*      s_pPitchRem = 0;
static const unsigned* s_pButtons = 0;
static const int*      s_pMouseAim = 0;
static void* const*    s_pInputSource = 0;
static const int*      s_pInvertPrimary = 0;
static const int*      s_pInvertAlt = 0;

typedef void (__fastcall* DisplayroomsFn)(short snum, int smoothratio);
static DisplayroomsFn s_origDisplayrooms = 0;

static volatile LONG s_loggedFrame = 0;

static volatile LONG s_localPlayer = -1;

int smoothview_local_player() { return (int)s_localPlayer; }

static void* engine_ptr(unsigned rva, unsigned size) {
  if (!s_base || rva > s_imageSize || size > s_imageSize - rva) {
    wt_log("smoothview: rva 0x%X (%u bytes) is outside the image", rva, size);
    return 0;
  }
  return s_base + rva;
}

// getinput negates the pitch leftover unless the device's invert setting is on (0x00729d2e / 0x00729e28); this follows
// it. 0 when the object cannot be trusted: drop the term rather than aim the wrong way.
static int pitch_pending_sign() {
  if (!s_pInputSource || !s_pInvertPrimary || !s_pInvertAlt) return 0;
  const unsigned char* src = (const unsigned char*)*s_pInputSource;
  if (!src) return 0;
  const int idx = *(const int*)(src + 0x3E8);
  if (idx < 0 || idx > 63) return 0;
  const int device = *(const int*)(src + 0x2C + idx * 4);
  const int invert = (device == 1) ? *s_pInvertAlt : *s_pInvertPrimary;
  return invert == 0 ? -1 : 1;
}

// Everything the drawn camera needs, computed as the engine is about to and again in double. Writes only our floats.
static void compute_fractions(short snum, int smoothratio) {
  s_fracYaw = 0.0f;
  s_fracPitch = 0.0f;
  if (!s_live || !s_enabled || snum < 0 || snum > 15) return;
  if (*s_pCamera >= 0) return;                       // a camera sprite draws its own angle
  // The free camera's pose is substituted where the MULSS sites sit, so the PLAYER's unspent motion would turn it.
  if (camera_active()) return;

  const unsigned char* p = s_base + kRvaPlayers + (unsigned)snum * 0x408;
  if (*(const short*)(p + P_NEWOWNER) >= 0) return;  // the player is looking through something

  int ratio = smoothratio;
  if (ratio < 0) ratio = 0;
  if (ratio > 0x10000) ratio = 0x10000;
  if (*s_pPause != 0 || *(const short*)(p + P_ONCRANE) >= 0) ratio = 0x10000;

  const int ang   = *(const unsigned short*)(p + P_ANG);
  const int oang  = *(const unsigned short*)(p + P_OANG);
  const int delta = smooth_ang_delta(ang, oang);

  const int horiz     = *(const int*)(p + P_HORIZ);
  const int horizoff  = *(const short*)(p + P_HORIZOFF);
  const int ohoriz    = *(const int*)(p + P_OHORIZ);
  const int ohorizoff = *(const int*)(p + P_OHORIZOFF);
  const int hDelta    = (horiz + horizoff) - (ohoriz + ohorizoff);
  const int hInt      = smooth_horiz_int(horiz, horizoff, ohoriz, ohorizoff, ratio);

  // Motion measured but not yet applied. With real mouse aiming the engine's leftovers are zero, so the term comes from
  // mouseaim.cpp's raw accumulator.
  double pendingYawNow = 0.0;
  double pendingHorizNow = 0.0;
  if (!mouseaim_view_pending((double)hInt, &pendingYawNow, &pendingHorizNow)) {
    // 64 counts make one avel and processinput doubles it (0x0072c0cd): a leftover count is 1/32 build unit. Stale with
    // the strafe modifier held (mouse x goes to strafe).
    if (s_pYawRem && s_pButtons && ((*s_pButtons >> 4) & 1u) == 0)
      pendingYawNow = (double)*s_pYawRem / 32.0;
    // 64 counts make one horz and processinput adds horz >> 1 (0x0072caa4): 1/128 horizon unit per count. Stale with
    // mouse aiming off.
    if (s_pPitchRem && s_pMouseAim && *s_pMouseAim != 0) {
      const int sign = pitch_pending_sign();
      if (sign) pendingHorizNow = (double)sign * (double)*s_pPitchRem / 128.0;
    }
  }

  // The pending term must cross the tic like the angle (at full strength it stepped at each tic boundary), so ang +
  // pending is interpolated; a new tic is where the ratio stops climbing.
  static double s_prevYaw = 0.0, s_curYaw = 0.0;
  static double s_prevHoriz = 0.0, s_curHoriz = 0.0;
  static int s_lastRatio = -1;
  static int s_seeded = 0;
  if (!s_seeded) {
    s_prevYaw = s_curYaw = pendingYawNow;
    s_prevHoriz = s_curHoriz = pendingHorizNow;
    s_seeded = 1;
  } else if (ratio <= s_lastRatio) {
    s_prevYaw = s_curYaw;     s_curYaw = pendingYawNow;
    s_prevHoriz = s_curHoriz; s_curHoriz = pendingHorizNow;
  } else {
    // Mid-tic the leftovers cannot change.
    s_curYaw = pendingYawNow;
    s_curHoriz = pendingHorizNow;
  }
  s_lastRatio = ratio;
  const double r = (double)ratio / 65536.0;
  const double pendingYaw   = s_prevYaw   + (s_curYaw   - s_prevYaw)   * r;
  const double pendingHoriz = s_prevHoriz + (s_curHoriz - s_prevHoriz) * r;

  const double fracYaw = smooth_clamp_frac(smooth_interp_frac(delta, ratio) + pendingYaw,
                                           kSmoothFracMaxYaw);
  // Pitch goes through the horizon, not added to it: the renderer sees getangle(128, horiz - 100).
  const double hDrawn = smooth_horiz_clamp((double)hInt);
  const double hExact = smooth_horiz_clamp((double)hInt + smooth_interp_frac(hDelta, ratio)
                                           + pendingHoriz);
  const double fracPitch = smooth_clamp_frac(smooth_horiz_to_units(hExact)
                                             - smooth_horiz_to_units(hDrawn),
                                             kSmoothFracMaxPitch);

  s_fracYaw = (float)fracYaw;
  s_fracPitch = (float)fracPitch;

  if ((fracYaw != 0.0 || fracPitch != 0.0) &&
      InterlockedCompareExchange(&s_loggedFrame, 1, 0) == 0)
    wt_log("smoothview: live. snum=%d smoothratio=%d ang=%d oang=%d "
           "engine cang=%d exact=%.4f fracYaw=%.4f fracPitch=%.4f "
           "(horiz int=%d exact=%.4f, pending yaw=%.4f horiz=%.4f)",
           (int)snum, smoothratio, ang, oang,
           (int)(short)(oang + smooth_mulscale16(delta, ratio)
                        + *(const short*)(p + P_LOOKANG)),
           (double)oang + (double)delta * (double)ratio / 65536.0
                        + (double)*(const short*)(p + P_LOOKANG),
           fracYaw, fracPitch, hInt, hExact, pendingYaw, pendingHoriz);
}

// The tic must never see any of this: fractions exist only for the span of the draw.
static void __fastcall wt_displayrooms(short snum, int smoothratio) {
  if (snum >= 0 && snum <= 15) s_localPlayer = snum;
  compute_fractions(snum, smoothratio);
  // Fullbright borrows every sprite's shade for this one draw and gives it back; no-ops with the feature off.
  light_draw_arm();
  s_origDisplayrooms(snum, smoothratio);
  light_draw_release();
  s_fracYaw = 0.0f;
  s_fracPitch = 0.0f;
}

// Each MULSS ends in an absolute operand ASLR moves: the fingerprint covers the opcode, the operand is checked against
// this process's base.
static int check_mulss(unsigned rva, const unsigned char* fp, const void* wantOperand) {
  const unsigned char* at = (const unsigned char*)engine_ptr(rva, kMulssLen);
  if (!at) return 0;
  if (memcmp(at, fp, 4) != 0) {
    wt_log("smoothview: rva 0x%X is not the MULSS we expected "
           "(found %02X %02X %02X %02X); installing nothing",
           rva, at[0], at[1], at[2], at[3]);
    return 0;
  }
  const unsigned found = *(const unsigned*)(at + 4);
  if (found != (unsigned)(UINT_PTR)wantOperand) {
    wt_log("smoothview: rva 0x%X reads 0x%08X, not the relocated 0x%08X; installing nothing",
           rva, found, (unsigned)(UINT_PTR)wantOperand);
    return 0;
  }
  wt_log("smoothview: rva 0x%X fingerprint %02X %02X %02X %02X ok, operand 0x%08X relocated as expected",
         rva, fp[0], fp[1], fp[2], fp[3], found);
  return 1;
}

void smoothview_install() {
  s_base = (unsigned char*)GetModuleHandleA(0);
  if (!s_base) { wt_log("smoothview: no module base"); return; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("smoothview: not a DOS image"); return; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("smoothview: not a PE image"); return; }
  s_imageSize = nt->OptionalHeader.SizeOfImage;

  // If the constant moved, so did everything else here.
  const float* konst = (const float*)engine_ptr(kRvaDegPerUnit, sizeof(float));
  if (!konst) return;
  if (*konst != kSmoothDegPerUnit) {
    wt_log("smoothview: the degrees-per-unit constant at rva 0x%X is %.9f, not %.9f; "
           "installing nothing", kRvaDegPerUnit, (double)*konst, (double)kSmoothDegPerUnit);
    return;
  }
  s_degPerUnit = *konst;
  wt_log("smoothview: constant at rva 0x%X (%p) is %.9f, as read from the dump",
         kRvaDegPerUnit, (const void*)konst, (double)*konst);

  s_pPause    = (const short*)engine_ptr(kRvaPauseFlag, sizeof(short));
  s_pCamera   = (const short*)engine_ptr(kRvaCameraSprite, sizeof(short));
  s_pYawRem   = (const int*)  engine_ptr(kRvaYawRemainder, sizeof(int));
  s_pPitchRem = (const int*)  engine_ptr(kRvaPitchRemainder, sizeof(int));
  s_pButtons  = (const unsigned*)engine_ptr(kRvaButtonBits, sizeof(unsigned));
  s_pMouseAim = (const int*)  engine_ptr(kRvaMouseAimOn, sizeof(int));
  s_pInputSource   = (void* const*)engine_ptr(kRvaInputSource, sizeof(void*));
  s_pInvertPrimary = (const int*)engine_ptr(kRvaInvertPrimary, sizeof(int));
  s_pInvertAlt     = (const int*)engine_ptr(kRvaInvertAlt, sizeof(int));
  if (!engine_ptr(kRvaPlayers, 16 * 0x408) || !s_pPause || !s_pCamera) {
    wt_log("smoothview: an engine address did not fit the image; installing nothing");
    return;
  }

  if (!check_mulss(kRvaMulssYaw, kFpYaw, konst)) return;
  if (!check_mulss(kRvaMulssPitch, kFpPitch, konst)) return;
  s_retYaw   = s_base + kRvaMulssYaw + kMulssLen;
  s_retPitch = s_base + kRvaMulssPitch + kMulssLen;

  void* unused = 0;
  if (!hook_install(kRvaMulssYaw, kFpYaw, sizeof(kFpYaw), kMulssLen,
                    (void*)wt_mulss_yaw, &unused)) {
    wt_log("smoothview: the yaw site did not take; installing nothing further");
    return;
  }
  if (!hook_install(kRvaMulssPitch, kFpPitch, sizeof(kFpPitch), kMulssLen,
                    (void*)wt_mulss_pitch, &unused)) {
    wt_log("smoothview: the pitch site did not take; the yaw stub stays in but adds nothing");
    return;
  }
  if (!hook_install(kRvaDisplayrooms, kFpDisplayrooms, sizeof(kFpDisplayrooms),
                    kDisplayroomsPrologue, (void*)wt_displayrooms,
                    (void**)&s_origDisplayrooms) || !s_origDisplayrooms) {
    wt_log("smoothview: displayrooms did not take; both stubs stay in but add nothing");
    return;
  }

  // Only now is every piece in place, so only now may the fractions be nonzero.
  s_live = 1;
  wt_log("smoothview: installed. displayrooms rva 0x%X is "
         "__fastcall(short snum in ECX, int smoothratio in EDX), confirmed at the "
         "call site 0x006faad6 of the dump; ohoriz is player+0x%X, ohorizoff player+0x%X, "
         "from the tic backup at 0x0072a9e2",
         kRvaDisplayrooms, P_OHORIZ, P_OHORIZOFF);
}

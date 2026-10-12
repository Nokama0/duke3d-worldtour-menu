// See fovwt.h. One five-byte call is rewritten and one function added to the frame: the engine's own setaspect, called
// with a scaled viewingrange. setaspect does the writing, so viewingrangerecip, xdimenscale and xdimscale stay
// consistent.
#include "fovwt.h"
#include "config.h"   // g_cfg.fovon, g_cfg.fov
#include "hook.h"     // wt_log, hook_patch_call
#include <windows.h>

// RVAs: the game loads at a randomised base (image base 0x6B0000).
static const unsigned kRvaSetAspect     = 0xB44E0;    // FUN_007644e0, __fastcall(int viewingrange, int yxaspect)
static const unsigned kRvaViewCall      = 0x4AAC9;    // the main loop's setaspect, immediately before displayrooms
static const unsigned kRvaViewingrange  = 0xE9F040;   // DAT_0154f040, int32, 16.16

// setaspect's first bytes: push ebx; push esi; mov eax,ecx; mov [viewingrange],ecx. The last operand is relocated by
// the loader, so it is checked separately; that proves setaspect is where we think and writes viewingrange.
static const unsigned char kFpSetAspect[] = { 0x53, 0x56, 0x8B, 0xC1, 0x89, 0x0D };
static const unsigned kSetAspectOperandAt = 6;

typedef void (__fastcall* SetAspectFn)(int viewingrange, int yxaspect);
static SetAspectFn s_setaspect = 0;

static unsigned char* s_base = 0;
static unsigned       s_imageSize = 0;
static const int*     s_pViewingrange = 0;
static int            s_live = 0;
static volatile LONG  s_logged = 0;

int fov_installed() { return s_live; }

static void* engine_ptr(unsigned rva, unsigned size) {
  if (!s_base || rva > s_imageSize || size > s_imageSize - rva) {
    wt_log("fov: rva 0x%X (%u bytes) is outside the image", rva, size);
    return 0;
  }
  return s_base + rva;
}

// The replacement for the call at 0x006faac9. Render thread, once per drawn frame, viewingrange in ECX and yxaspect in
// EDX; both go to the real setaspect. Switched off it passes the engine's value through, so there is nothing to
// restore.
static void __fastcall wt_setaspect_view(int viewingrange, int yxaspect) {
  const int on = g_cfg.fovon;
  const int use = on ? (int)fov_viewingrange_for(viewingrange, g_cfg.fov) : viewingrange;
  s_setaspect(use, yxaspect);
  if (on && InterlockedCompareExchange(&s_logged, 1, 0) == 0)
    wt_log("fov: live. engine viewingrange=%d -> %d (fov=%d deg, multiplier=%d), "
           "stored=%d; the projection's angle before the aspect divide is "
           "%.2f deg, was %.2f",
           viewingrange, use, g_cfg.fov, fov_to_viewingrange(g_cfg.fov),
           s_pViewingrange ? *s_pViewingrange : 0,
           fov_engine_degrees(use), fov_engine_degrees(viewingrange));
}

void fov_install() {
  s_base = (unsigned char*)GetModuleHandleA(0);
  if (!s_base) { wt_log("fov: no module base"); return; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("fov: not a DOS image"); return; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("fov: not a PE image"); return; }
  s_imageSize = nt->OptionalHeader.SizeOfImage;

  const unsigned char* setaspect =
      (const unsigned char*)engine_ptr(kRvaSetAspect, kSetAspectOperandAt + 4);
  s_pViewingrange = (const int*)engine_ptr(kRvaViewingrange, sizeof(int));
  if (!setaspect || !s_pViewingrange || !engine_ptr(kRvaViewCall, 5)) {
    wt_log("fov: an engine address did not fit the image; installing nothing");
    return;
  }
  if (memcmp(setaspect, kFpSetAspect, sizeof(kFpSetAspect)) != 0) {
    wt_log("fov: rva 0x%X is not setaspect (found %02X %02X %02X %02X %02X %02X); "
           "installing nothing", kRvaSetAspect, setaspect[0], setaspect[1], setaspect[2],
           setaspect[3], setaspect[4], setaspect[5]);
    return;
  }
  const unsigned wrote = *(const unsigned*)(setaspect + kSetAspectOperandAt);
  if (wrote != (unsigned)(UINT_PTR)s_pViewingrange) {
    wt_log("fov: setaspect writes 0x%08X, not the relocated viewingrange 0x%08X; "
           "installing nothing", wrote, (unsigned)(UINT_PTR)s_pViewingrange);
    return;
  }

// The call site's five bytes are E8 plus the distance to setaspect: relative, so ASLR does not move it; a game update
// that moves either end changes it.
  unsigned char fp[5];
  fp[0] = 0xE8;
  *(int*)(fp + 1) = (int)kRvaSetAspect - (int)kRvaViewCall - 5;
  s_setaspect = (SetAspectFn)(s_base + kRvaSetAspect);
  if (!hook_patch_call(kRvaViewCall, fp, (void*)wt_setaspect_view)) {
    wt_log("fov: the setaspect call site did not take; the game's own field of view stands");
    s_setaspect = 0;
    return;
  }

  s_live = 1;
  wt_log("fov: installed. viewingrange is rva 0x%X (int32, 16.16), written only by "
         "setaspect rva 0x%X, which the main loop calls at rva 0x%X just before "
         "displayrooms; it reads %d right now, which the projection draws as %.2f deg "
         "before the aspect divide",
         kRvaViewingrange, kRvaSetAspect, kRvaViewCall,
         *s_pViewingrange, fov_engine_degrees(*s_pViewingrange));
}

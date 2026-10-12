// Screenshot mode: weapon, crosshair, status bar and this mod's markers off together while the free camera flies. This
// file decides nothing: every decision is a pure rule in shot.h. This fork has no thunks, ud.screen_size or classic
// status bar (docs/re/screenshot-mode-sites.md).
#include <windows.h>
#include "shot.h"
#include "config.h"
#include "hook.h"        // wt_log, hook_install

// weapon: FUN_007282b0, one caller (displayrest 0x3CF29); hooked and skipped. crosshair: gated at 0x3D217 by the cfg's
// `Crosshairs` global, held at 0. status bar: ScreenSize selects a UI page; #NoHud (0xA2B4E) runs ScreenSize = 0,
// FUN_00730890() (recompute view), FUN_006f5b20() (page switch).
static const unsigned kRvaDisplayWeapon = 0x782B0;    // FUN_007282b0, no arguments
static const unsigned kRvaRecomputeView = 0x80890;
static const unsigned kRvaHudPage       = 0x45B20;
static const unsigned kRvaScreenSize    = 0xEDFCD0;   // DAT_0158fcd0, int, 0/4/8
static const unsigned kRvaCrosshairs    = 0xEDFCD8;   // DAT_0158fcd8, int, 0 = off

// push ebp; mov ebp,esp; sub esp,0x40; mov eax,[...]: the seventh byte starts a relocated operand and is not in the
// fingerprint.
static const unsigned char kFpDisplayWeapon[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x40 };
static const unsigned kDisplayWeaponPrologue = 6;  

// The two functions are CALLED, not hooked, so only their first bytes are checked; a mismatch keeps the whole mode off
// rather than leave the screen size written with nothing to restore it.
static const unsigned char kFpRecomputeView[] = { 0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08, 0x83, 0xE4 };
static const unsigned char kFpHudPage[]       = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 };

typedef void (__cdecl* VoidFn)(void);

static unsigned char* s_base = 0;
static unsigned       s_imageSize = 0;
static VoidFn s_origWeapon = 0;
static VoidFn s_recomputeView = 0;
static VoidFn s_hudPage = 0;
static int*   s_screenSize = 0;
static int*   s_crosshairs = 0;
static bool   s_live = false;

static bool     s_active = false;          // what the last tic decided
static ShotSizeHold s_view = { false, 0 };
static bool     s_heldCross = false;
static int      s_savedCross = 0;

bool shot_active_now() { return s_active; }

static void* engine_ptr(unsigned rva, unsigned size) {
  if (!s_base || rva > s_imageSize || size > s_imageSize - rva) {
    wt_log("shot: rva 0x%X (%u bytes) is outside the image", rva, size);
    return 0;
  }
  return s_base + rva;
}

static VoidFn verified_call(unsigned rva, const unsigned char* fp, unsigned n, const char* name) {
  const unsigned char* at = (const unsigned char*)engine_ptr(rva, n);
  if (!at) return 0;
  for (unsigned i = 0; i < n; i++) {
    if (at[i] != fp[i]) {
      wt_log("shot: %s at rva 0x%X is not the function we expected "
             "(byte %u is %02X, not %02X); screenshot mode is off this session",
             name, rva, i, at[i], fp[i]);
      return 0;
    }
  }
  return (VoidFn)at;
}

static void __cdecl hooked_displayweapon(void) {
  if (shot_call_original(s_active) && s_origWeapon) s_origWeapon();
}

// The three-step sequence the game's own #NoHud runs. The global alone changes nothing on screen until the page switch.
static void apply_screen_size(int value) {
  *s_screenSize = value;
  s_recomputeView();
  s_hudPage();
}

void shot_tic(bool cameraActive, bool menuOpen) {
  const bool now = s_live && shot_active(cameraActive, g_cfg.freecamshot != 0, menuOpen);
  const int edge = shot_edge(s_active, now);
  s_active = now;

  long write = 0;
  const long size = (long)*s_screenSize;
  const int act = shot_view_step(&s_view, edge, size, &write);
  if (act != SHOT_ACT_NONE) {
    apply_screen_size((int)write);
    wt_log(act == SHOT_ACT_WRITE_ZERO_AND_VSCRN
             ? "shot: on, ScreenSize %d saved and the hud page hidden"
             : "shot: off, ScreenSize %d restored and the hud page back",
           act == SHOT_ACT_WRITE_ZERO_AND_VSCRN ? (int)size : (int)write);
  }

// The crosshair is held rather than bracketed: taken on the first tic that wants it and given back on the first that
// does not, restoring the player's own value.
  if (now) {
    if (!s_heldCross) { s_savedCross = *s_crosshairs; s_heldCross = true; }
    *s_crosshairs = 0;
  } else if (s_heldCross) {
    *s_crosshairs = s_savedCross;
    s_heldCross = false;
  }
}

// The camera landing. Not wired to DLL unload (a page switch into the game's UI during teardown would crash); the tic
// already restores everything on its OFF edge.
void shot_drop() {
  if (!s_live) return;
  shot_tic(false, false);
}

bool shot_install() {
  s_base = (unsigned char*)GetModuleHandleA(0);
  if (!s_base) { wt_log("shot: no module base"); return false; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("shot: not a DOS image"); return false; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("shot: not a PE image"); return false; }
  s_imageSize = nt->OptionalHeader.SizeOfImage;

  s_screenSize = (int*)engine_ptr(kRvaScreenSize, sizeof(int));
  s_crosshairs = (int*)engine_ptr(kRvaCrosshairs, sizeof(int));
  s_recomputeView = verified_call(kRvaRecomputeView, kFpRecomputeView,
                                  sizeof(kFpRecomputeView), "the view recompute");
  s_hudPage = verified_call(kRvaHudPage, kFpHudPage, sizeof(kFpHudPage), "the hud page switch");
  if (!s_screenSize || !s_crosshairs || !s_recomputeView || !s_hudPage) {
    wt_log("shot: screenshot mode is off for this session");
    return false;
  }
  // A ScreenSize outside the game's 0..8 would mean this is not the global the dump named, and the first ON edge would
  // save a value the restore hands back to code that cannot take it.
  if (*s_screenSize < 0 || *s_screenSize > 8) {
    wt_log("shot: ScreenSize at rva 0x%X reads %d, outside the game's own 0..8; "
           "screenshot mode is off for this session", kRvaScreenSize, *s_screenSize);
    return false;
  }

  void* trampoline = 0;
  if (!hook_install(kRvaDisplayWeapon, kFpDisplayWeapon, sizeof(kFpDisplayWeapon),
                    kDisplayWeaponPrologue, (void*)&hooked_displayweapon, &trampoline)) {
    wt_log("shot: the weapon draw at rva 0x%X did not take; screenshot mode is off "
           "for this session", kRvaDisplayWeapon);
    return false;
  }
  s_origWeapon = (VoidFn)trampoline;
  s_live = true;
  wt_log("shot: installed. weapon hook at rva 0x%X, ScreenSize 0x%X reads %d, "
         "Crosshairs 0x%X reads %d, view recompute 0x%X, hud page 0x%X",
         kRvaDisplayWeapon, kRvaScreenSize, *s_screenSize,
         kRvaCrosshairs, *s_crosshairs, kRvaRecomputeView, kRvaHudPage);
  return true;
}

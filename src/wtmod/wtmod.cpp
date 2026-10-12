// Nuke Panel for Duke Nukem 3D: 20th Anniversary World Tour. 32-bit, Direct3D 11, code encrypted by the store's
// protection, so nothing hooks it: the game imports two XInput functions BY ORDINAL (2, 3) from a library absent
// from its folder, so a same-named DLL loads first and forwards to the real one. The executable is never touched.
#include <windows.h>
#include <xinput.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include "hook.h"
#include "mouseaim.h"
#include "smoothview.h"
#include "fovwt.h"
#include "overlay.h"
#include "overlay_internal.h"   // ov_size
#include "input.h"
#include "light.h"      // light_drop
#include "config.h"
#include "menu.h"
#include "entry.h"
#include "features.h"
#include "canvas.h"
#include "canvas_selftest.h"
#include "esp.h"
#include "dmgnum.h"
#include "trigger.h"
#include "silent.h"
#include "aim.h"
#include "secretpath.h"
#include "firerate.h"
#include "wswitch.h"
#include "worldwt.h"
#include "dbgdump.h"
#include "camera.h"
#include "freecam.h"
#include "nocullwt.h"
#include "nocull.h"
#include "shot.h"
#include "unported.h"
#include "autosave.h"
#include "autoswitch.h"
#include "actorstate.h"
#include "bounds.h"

void wt_log(const char* fmt, ...) {
  char path[MAX_PATH];
  // A game path of MAX_PATH or more logs nowhere rather than one folder up.
  const DWORD got = GetModuleFileNameA(0, path, MAX_PATH);
  if (!path_beside_module(path, got, MAX_PATH, "wtmod.log", path, MAX_PATH)) return;
  FILE* f = 0;
  if (fopen_s(&f, path, "a") != 0 || !f) return;
  SYSTEMTIME t;
  GetLocalTime(&t);
  fprintf(f, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fputc('\n', f);
  fclose(f);
}

// Loaded by absolute path from the system directory (SysWOW64 for a 32-bit process on 64-bit Windows): a plain name
// would find this file again and recurse.
typedef DWORD (WINAPI* GetStateFn)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI* SetStateFn)(DWORD, XINPUT_VIBRATION*);
static HMODULE    s_real = 0;
static GetStateFn s_getState = 0;
static SetStateFn s_setState = 0;

static void load_real_xinput() {
  if (s_real) return;
  char path[MAX_PATH];
  const UINT n = GetSystemDirectoryA(path, MAX_PATH);
  if (n == 0 || n >= MAX_PATH - 20) return;
  strcat_s(path, "\\XINPUT1_3.dll");
  s_real = LoadLibraryA(path);
  if (!s_real) {
    if (n < MAX_PATH - 20) {
      GetSystemDirectoryA(path, MAX_PATH);
      strcat_s(path, "\\XInput1_4.dll");
      s_real = LoadLibraryA(path);
    }
  }
  if (!s_real) { wt_log("xinput: no system library found; the pad will not work"); return; }
  s_getState = (GetStateFn)GetProcAddress(s_real, "XInputGetState");
  s_setState = (SetStateFn)GetProcAddress(s_real, "XInputSetState");
  wt_log("xinput: forwarding to %s (get=%p set=%p)", path, (void*)s_getState, (void*)s_setState);
}

extern "C" DWORD WINAPI wt_XInputGetState(DWORD user, XINPUT_STATE* state) {
  load_real_xinput();
  if (!s_getState) return ERROR_DEVICE_NOT_CONNECTED;
  return s_getState(user, state);
}
extern "C" DWORD WINAPI wt_XInputSetState(DWORD user, XINPUT_VIBRATION* vib) {
  load_real_xinput();
  if (!s_setState) return ERROR_DEVICE_NOT_CONNECTED;
  return s_setState(user, vib);
}

// Read once when the worker starts, from wtmod.ini beside the executable; everything is off by default so a fresh
// install does not change the window.
static char s_iniPath[MAX_PATH] = { 0 };
const char* wt_ini_path() { return s_iniPath; }

static void read_cfg() {
  char path[MAX_PATH];
  // No usable path keeps the defaults and an empty ini path, which the writers skip.
  const DWORD got = GetModuleFileNameA(0, path, MAX_PATH);
  if (!path_beside_module(path, got, MAX_PATH, "wtmod.ini", path, MAX_PATH)) return;
  strcpy_s(s_iniPath, path);
  config_load(path);
  wt_log("cfg: borderless=%d rect=%d,%d %dx%d dump=%d fpscap=%d mouseaim=%d sens=%d vscale=%d "
         "smoothview=%d overlay=%d readout=%d panelkey=0x%02X canvastest=%d (%s)",
         g_cfg.borderless, g_cfg.winx, g_cfg.winy, g_cfg.winw, g_cfg.winh, g_cfg.dump,
         g_cfg.fpscap, g_cfg.mouseaimon, g_cfg.mouseaimsens, g_cfg.mouseaimvscale,
         g_cfg.smoothview, g_cfg.overlay, g_cfg.readout, g_cfg.menukey, g_cfg.canvastest, path);
}

// The shipped code is encrypted on disk and decrypted in memory; this writes the mapped image out ONCE for analysis (a
// local development aid, off unless dump=1), flat: each section's file offset is its virtual address, so a
// disassembler's addresses are the running game's.
static volatile LONG s_dumped = 0;
static void dump_image() {
  if (InterlockedExchange(&s_dumped, 1)) return;
  char path[MAX_PATH];
  const DWORD pathLen = GetModuleFileNameA(0, path, MAX_PATH);
  if (!path_beside_module(path, pathLen, MAX_PATH, "duke3d_dump.bin", path, MAX_PATH)) return;
  const unsigned char* base = (const unsigned char*)GetModuleHandleA(0);
  if (!base) { wt_log("dump: no module base"); return; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("dump: not a DOS image"); return; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("dump: not a PE image"); return; }
  const DWORD size = nt->OptionalHeader.SizeOfImage;
  if (size < 0x1000 || size > 0x10000000u) { wt_log("dump: implausible size %lu", size); return; }

  unsigned char* copy = (unsigned char*)VirtualAlloc(0, size, MEM_COMMIT, PAGE_READWRITE);
  if (!copy) { wt_log("dump: out of memory for %lu bytes", size); return; }
  SIZE_T got = 0;
  if (!ReadProcessMemory(GetCurrentProcess(), base, copy, size, &got) || got != size) {
    wt_log("dump: whole-image read failed (%lu of %lu), falling back per page", (unsigned long)got, size);
    for (DWORD off = 0; off < size; off += 0x1000) {
      SIZE_T n = 0;
      const DWORD want = (size - off) < 0x1000 ? (size - off) : 0x1000;
      ReadProcessMemory(GetCurrentProcess(), base + off, copy + off, want, &n);
    }
  }

  IMAGE_NT_HEADERS32* ntc = (IMAGE_NT_HEADERS32*)(copy + dos->e_lfanew);
  IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(ntc);
  for (unsigned i = 0; i < ntc->FileHeader.NumberOfSections; i++) {
    sec[i].PointerToRawData = sec[i].VirtualAddress;
    sec[i].SizeOfRawData    = sec[i].Misc.VirtualSize;
  }

  FILE* f = 0;
  if (fopen_s(&f, path, "wb") == 0 && f) {
    fwrite(copy, 1, size, f);
    fclose(f);
    wt_log("dump: wrote %lu bytes to %s (image base %p)", size, path, (void*)base);
  } else {
    wt_log("dump: could not open %s for writing", path);
  }
  VirtualFree(copy, 0, MEM_RELEASE);
}

// The Present hook serves the frame cap (the game's menus run at thousands of fps) and the overlay. A vtable patch, not
// a detour: DXGI gives every swap chain one shared vtable, so a throwaway swap chain finds the game's slot.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static LONGLONG s_qpf       = 0;
static LONGLONG s_nextFrame = 0;   // the tick this frame's Present may go out
static HANDLE   s_capTimer  = 0;
static volatile LONG s_frames = 0;
static volatile LONG s_aimFrames = 0;
static volatile LONG s_windowSettled = 0;
// Raised by wt_apply_borderless when the window is ALREADY as wanted, reset when it changed something; the worker stops
// once it held for a few passes.
static volatile LONG s_borderlessStable = 0;
static volatile LONG s_borderlessDiffs = 0;
static volatile LONG s_presented = 0;

// Holds the calling thread until this frame's slot: a high resolution timer covers all but the last half millisecond,
// a spin the rest. A frame far out of step resynchronises, so a hitch can't become a burst of uncapped frames.
static void cap_wait() {
  const int hz = g_cfg.fpscap;
  if (hz <= 0 || s_qpf <= 0) return;
  const LONGLONG period = s_qpf / hz;
  if (period <= 0) return;

  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  const LONGLONG drift = now.QuadPart - s_nextFrame;
  if (s_nextFrame == 0 || drift > 4 * period || drift < -4 * period) {
    s_nextFrame = now.QuadPart + period;
    return;
  }
  const LONGLONG margin = s_qpf / 2000;
  if (s_capTimer && s_nextFrame - now.QuadPart > margin) {
    LARGE_INTEGER due;                             // negative: relative, 100 ns units
    due.QuadPart = -((s_nextFrame - margin - now.QuadPart) * 10000000 / s_qpf);
    if (due.QuadPart < 0 && SetWaitableTimer(s_capTimer, &due, 0, 0, 0, FALSE))
      WaitForSingleObject(s_capTimer, 100);
  }
  while (now.QuadPart < s_nextFrame) {
    YieldProcessor();
    QueryPerformanceCounter(&now);
  }
  s_nextFrame += period;
}

// Readout: frame rate from this hook's timing plus the player's position and facing read from the engine (bounds
// checked); an unreadable line is drawn as ?.
static const unsigned kRvaOvPlayers = 0x5FD5C0;    // player[0]
#define OV_P_STRIDE 0x408
#define OV_P_POSX   0x214
#define OV_P_POSY   0x218
// posz confirmed in the dump at 0x007321f9 and 0x006ff209-0x006ff21b (four consecutive ints from +0x214).
#define OV_P_POSZ   0x21c
#define OV_P_HORIZ  0x220    // centre 100
#define OV_P_ANG    0x2cc    // uint16, 0..2047

static const unsigned char* s_ovBase = 0;
static int s_ovTried = 0;

static void readout_resolve() {
  if (s_ovTried) return;
  s_ovTried = 1;
  const unsigned char* base = (const unsigned char*)GetModuleHandleA(0);
  if (!base) { wt_log("overlay: no module base; the player lines stay ?"); return; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("overlay: not a DOS image"); return; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("overlay: not a PE image"); return; }
  const DWORD size = nt->OptionalHeader.SizeOfImage;
  if (kRvaOvPlayers > size || (DWORD)(16 * OV_P_STRIDE) > size - kRvaOvPlayers) {
    wt_log("overlay: the player array at rva 0x%X does not fit a %lu byte image; "
           "the player lines stay ?", kRvaOvPlayers, size);
    return;
  }
  s_ovBase = base;
  wt_log("overlay: player array at %p (rva 0x%X, stride 0x%X)",
         (const void*)(base + kRvaOvPlayers), kRvaOvPlayers, OV_P_STRIDE);
}

static double   s_fps      = 0.0;
static LONGLONG s_fpsMark  = 0;
static int      s_fpsCount = 0;

static void fps_tick() {
  if (s_qpf <= 0) return;
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  s_fpsCount++;
  if (s_fpsMark == 0) { s_fpsMark = now.QuadPart; return; }
  const LONGLONG span = now.QuadPart - s_fpsMark;
  if (span >= s_qpf / 2) {
    s_fps = (double)s_fpsCount * (double)s_qpf / (double)span;
    s_fpsCount = 0;
    s_fpsMark = now.QuadPart;
  }
}

static void draw_readout() {
  readout_resolve();

  char line[4][64];
  strcpy_s(line[0], "Nuke Panel WT");
  sprintf_s(line[1], "fps   %.1f", s_fps);
  // The local player's index is displayrooms' own argument, seen only by the smoothview hook; without it: ?.
  const int snum = smoothview_local_player();
  if (s_ovBase && snum >= 0 && snum <= 15) {
    const unsigned char* p = s_ovBase + kRvaOvPlayers + (unsigned)snum * OV_P_STRIDE;
    sprintf_s(line[2], "pos   %d, %d, %d", *(const int*)(p + OV_P_POSX),
              *(const int*)(p + OV_P_POSY), *(const int*)(p + OV_P_POSZ));
    sprintf_s(line[3], "ang   %d  horiz %d", (int)*(const unsigned short*)(p + OV_P_ANG),
              *(const int*)(p + OV_P_HORIZ));
  } else {
    strcpy_s(line[2], "pos   ?");
    strcpy_s(line[3], "ang   ?");
  }

  const int x = 8, y = 8, pad = 4;
  const int lineH = overlay_text_height();
  int wide = 0;
  for (int i = 0; i < 4; i++) {
    const int w = overlay_text_width(line[i]);
    if (w > wide) wide = w;
  }
  overlay_rect(x - pad, y - pad, wide + 2 * pad, lineH * 4 + 2 * pad,
               OVERLAY_RGBA(0, 0, 0, 160));
  for (int i = 0; i < 4; i++)
    overlay_text(x, y + i * lineH, OVERLAY_RGBA(255, 255, 255, 255), line[i]);
}

// menu_draw owns its state and runs only here, on the render thread; it has no open/closed guard, so the `if` below
// alone stops it drawing forever.

static bool s_menuWasOpen = false;

// The keys the window thread queued, spent here so menu.cpp's state stays on the render thread. Order: an open field,
// an armed binding row, then Escape.
static void drain_keys() {
  int packed;
  while ((packed = input_take_key()) != 0) {
    const int  vk     = wt_key_vk(packed);
    const bool repeat = (packed & WT_KEY_REPEAT) != 0;
    const bool shift  = (packed & WT_KEY_SHIFT) != 0;
    const bool ctrl   = (packed & WT_KEY_CTRL) != 0;
    int sc = 0;
    const bool haveSc = vk_to_sdl_scancode(vk, &sc);

    if (menu_entry_open()) {
      if (!haveSc) continue;
      // Text and caret movement repeat at the OS rate, Enter/Escape are one-shot. Ctrl+V is absent (no clipboard read
      // here).
      const bool typing = (sc == SDL_SCANCODE_BACKSPACE || entry_char_of_scancode(sc) != 0);
      const bool moving = (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_RIGHT
                           || sc == SDL_SCANCODE_HOME || sc == SDL_SCANCODE_END
                           || sc == SDL_SCANCODE_DELETE);
      const bool oneShot = (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_RETURN
                            || sc == SDL_SCANCODE_KP_ENTER);
      const bool chording = ctrl && (sc == SDL_SCANCODE_A || sc == SDL_SCANCODE_C
                                     || sc == SDL_SCANCODE_X);
      if ((typing || moving || oneShot || chording)
          && (typing || moving || ui_key_is_initial_press(repeat)))
        menu_entry_key_mod(sc, shift, ctrl);
      continue;   // everything else is swallowed while a field owns the keyboard
    }

    if (!ui_key_is_initial_press(repeat)) continue;   // one press is one action

    if (menu_keycap_armed()) {
      if (vk == VK_ESCAPE) { menu_keycap_cancel(); continue; }
      menu_keycap_bind(vk);   // refuses what keycap_step refuses (0, Mouse 1)
      continue;
    }
    if (vk == VK_ESCAPE) input_close_menu();
  }
}

// The panel's own cursor, drawn last. The game hides the system cursor and warps it to the window centre every frame:
// a plain arrow with a dark one-pixel skirt so it reads on both panel and level.
static void draw_cursor(int screenH) {
  int x = 0, y = 0;
  input_cursor(&x, &y);
  // Drawn at the panel's UI scale (the cursor arrives in device pixels); the open/close transform is reset first so it
  // doesn't slide with the panel.
  const int pct = menu_applied_scale(screenH);
  canvas_set_transform(0, 0, 1.0, 0, 0);
  canvas_set_scale(pct);
  x = ui_unscale_px(x, pct);
  y = ui_unscale_px(y, pct);
  const int H = 16;      // rows; the arrow is 16 tall and at most 11 wide
  for (int pass = 0; pass < 2; pass++) {
    const int grow = pass == 0 ? 1 : 0;                 // skirt first, then the fill
    const unsigned c = pass == 0 ? C_BEVELLO : C_BONE;
    for (int i = -grow; i < H + grow; i++) {
      const int row = i < 0 ? 0 : (i >= H ? H - 1 : i);
      const int right = row <= 10 ? row : 30 - 2 * row;
      if (right < 0) continue;
      const int w = right + 1 + 2 * grow;
      canvas_rect(x - grow, y + i, w, 1, c);
    }
  }
  canvas_set_scale(UI_SCALE_DEFAULT);
}

// The world markers, drawn before the panel (a menu belongs over everything). esp_draw opens and closes its own
// canvas frame and leaves the scale at default.
static void draw_esp() {
  // Screenshot mode takes this mod's markers off the shot; the panel is NOT gated, it turns the mode back off.
  if (shot_hides_overlays(shot_active_now())) return;
  if (!overlay_world_should_draw(camera_local_in_play())) return;
  int w = 0, h = 0;
  ov_size(&w, &h);
  if (w <= 0 || h <= 0) return;
  esp_draw(w, h);
  dmgnum_draw(w, h);
  trigger_draw(w, h);
  secretpath_draw(w, h);
  aim_draw_marker(w, h);
  // The secret volume's quads: not a draw, it fills the in-world layer's queue the seam hook drains next frame.
  secret_volume_collect();
  camera_draw_teleport_marker();
}

// The free camera's furniture (player-body marker, pose readout): after the ESP, before the panel; nothing with the
// camera down.
static void draw_camera() {
  if (shot_hides_overlays(shot_active_now())) return;
  if (!overlay_world_should_draw(camera_local_in_play())) return;
  int w = 0, h = 0;
  ov_size(&w, &h);
  if (w <= 0 || h <= 0) return;
  camera_draw_player_marker(w, h);
}

static void draw_panel() {
  int w = 0, h = 0;
  ov_size(&w, &h);
  if (w <= 0 || h <= 0) return;

  const bool open = input_menu_open();
  if (open != s_menuWasOpen) {
    s_menuWasOpen = open;
    wt_log("%s", open ? MENU_LOG_OPEN : MENU_LOG_CLOSE);
    if (!open) {
      // Disarmed HERE, keeping menu.cpp's state on this thread: a capture left armed would eat the next in-game key, a
      // field left open would keep eating them.
      menu_keycap_close();
      menu_entry_close();
      input_set_keycap_armed(0);
      input_mouse_release_all();
    }
  }
  if (!open && !menu_closing_animation_active()) return;

  input_begin_frame(w, h);
  drain_keys();

  canvas_begin(w, h);
  menu_draw(w, h);
  draw_cursor(h);
  canvas_end();

  features_sync();
  input_set_keycap_armed(menu_keycap_armed() ? 1 : 0);
  if (g_cfgDirty) config_write_changed(wt_ini_path());
}

// One frame bracket for everything we draw: readout, canvas column and panel queue into one batcher and overlay_end
// is the single place the game's device state is restored. The panel goes last.
static void draw_frame(IDXGISwapChain* sc) {
  if (!overlay_begin(sc)) return;
  input_set_ready(1);
  if (g_cfg.canvastest) canvas_selftest();
  if (g_cfg.overlay && g_cfg.readout) draw_readout();
  if (g_cfg.overlay) draw_esp();
  if (g_cfg.overlay) draw_camera();
  if (g_cfg.overlay) draw_panel();
  overlay_end();
}

typedef HRESULT (STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT (STDMETHODCALLTYPE* Present1Fn)(IDXGISwapChain1*, UINT, UINT,
                                                const DXGI_PRESENT_PARAMETERS*);
static PresentFn  s_origPresent  = 0;
static Present1Fn s_origPresent1 = 0;

// Pace it, time it, draw on it, all before the original Present, in the only order that works. A test present puts no
// picture on screen, so it is skipped.
static void present_frame(IDXGISwapChain* sc, UINT flags) {
  if (flags & DXGI_PRESENT_TEST) return;
  if (!s_presented) InterlockedExchange(&s_presented, 1);
  // One line, once: mouse readings asked for against frames drawn, and how many were answered with zero (the second
  // tracking the first means the old pipeline is out of the picture).
  if (InterlockedIncrement(&s_aimFrames) == 2000)
    wt_log("mouseaim: %ld polls over %ld frames, %ld of them handed the tic nothing",
           mouseaim_poll_count(), 2000L, mouseaim_zeroed_count());
  cap_wait();
  if (g_cfg.fpscap > 0 && InterlockedIncrement(&s_frames) == 1)
    wt_log("cap: pacing presents at %d fps", g_cfg.fpscap);
  // OUTSIDE the gate below, deliberately: the free camera works with the overlay off and the panel never opened, and its
  // key can't go live until camera_install succeeds.
  features_arm_freecam_key();
  if ((g_cfg.overlay || g_cfg.canvastest) && s_windowSettled) {
    if (g_cfg.overlay) fps_tick();
    draw_frame(sc);
  }
}


static HRESULT STDMETHODCALLTYPE wt_Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
  present_frame(sc, flags);
  return s_origPresent(sc, sync, flags);
}
static HRESULT STDMETHODCALLTYPE wt_Present1(IDXGISwapChain1* sc, UINT sync, UINT flags,
                                             const DXGI_PRESENT_PARAMETERS* params) {
  present_frame(sc, flags);
  return s_origPresent1(sc, sync, flags, params);
}

static bool patch_slot(void** vtbl, int slot, void* fn, void** outOrig) {
  DWORD old = 0;
  if (!VirtualProtect(&vtbl[slot], sizeof(void*), PAGE_READWRITE, &old)) return false;
  *outOrig = vtbl[slot];
  vtbl[slot] = fn;
  VirtualProtect(&vtbl[slot], sizeof(void*), old, &old);
  return true;
}

static void install_present_hook(HINSTANCE inst) {
  LARGE_INTEGER f;
  if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0) { wt_log("cap: no performance counter"); return; }
  s_qpf = f.QuadPart;
  // One handle whether or not the cap is on (the panel can turn it on any time); the high resolution flag needs Windows
  // 10 1803, else the spin covers more.
  s_capTimer = CreateWaitableTimerExW(0, 0, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!s_capTimer) s_capTimer = CreateWaitableTimerW(0, FALSE, 0);

  // Loaded from System32 only and called through a pointer so this DLL imports nothing from Direct3D (it loads before
  // a renderer is chosen).
  HMODULE d3d = LoadLibraryExW(L"d3d11.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  typedef HRESULT (WINAPI* CreateFn)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
                                     const D3D_FEATURE_LEVEL*, UINT, UINT,
                                     const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
                                     ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
  CreateFn create = d3d ? (CreateFn)GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain") : 0;
  if (!create) { wt_log("cap: no D3D11CreateDeviceAndSwapChain"); return; }

  WNDCLASSEXA wc = { sizeof(WNDCLASSEXA) };
  wc.lpfnWndProc   = DefWindowProcA;
  wc.hInstance     = inst;
  wc.lpszClassName = "wtmod_probe";
  RegisterClassExA(&wc);
  HWND probe = CreateWindowExA(0, "wtmod_probe", "", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, 0, 0, inst, 0);
  if (!probe) { wt_log("cap: no probe window"); UnregisterClassA("wtmod_probe", inst); return; }

  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount        = 1;
  sd.BufferDesc.Width   = 16;
  sd.BufferDesc.Height  = 16;
  sd.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow       = probe;
  sd.SampleDesc.Count   = 1;
  sd.Windowed           = TRUE;
  sd.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;

  IDXGISwapChain*      swap = 0;
  ID3D11Device*        dev  = 0;
  ID3D11DeviceContext* ctx  = 0;
  HRESULT hr = create(0, D3D_DRIVER_TYPE_WARP, 0, 0, 0, 0, D3D11_SDK_VERSION, &sd, &swap, &dev, 0, &ctx);
  if (FAILED(hr) || !swap) {
    wt_log("cap: probe swap chain failed (hr=0x%08lx)", (unsigned long)hr);
  } else {
    void** vtbl = *(void***)swap;
    if (patch_slot(vtbl, 8, (void*)wt_Present, (void**)&s_origPresent)) {
      wt_log("cap: Present hooked (orig=%p), fpscap=%d overlay=%d",
             (void*)s_origPresent, g_cfg.fpscap, g_cfg.overlay);
      // Newer code presents through IDXGISwapChain1::Present1; its slot exists only if both interfaces share one
      // vtable, which the comparison establishes.
      IDXGISwapChain1* swap1 = 0;
      if (SUCCEEDED(swap->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&swap1)) && swap1) {
        if (*(void***)swap1 == vtbl && patch_slot(vtbl, 22, (void*)wt_Present1, (void**)&s_origPresent1))
          wt_log("cap: Present1 hooked (orig=%p)", (void*)s_origPresent1);
        swap1->Release();
      }
    } else {
      wt_log("cap: could not write the Present slot");
    }
  }
  if (ctx)  ctx->Release();
  if (dev)  dev->Release();
  if (swap) swap->Release();
  DestroyWindow(probe);
  UnregisterClassA("wtmod_probe", inst);
}

// The first visible top-level window of this process with a non-empty client area; not found by class or title (an
// update can change them).
struct FindWin { DWORD pid; HWND best; };
// Said once, about the first refused window: the log could not otherwise tell a refused window from one the game had
// not made yet.
static int s_wndRefusedLogged = 0;

static BOOL CALLBACK pick_window(HWND h, LPARAM p) {
  FindWin* f = (FindWin*)p;
  DWORD pid = 0;
  GetWindowThreadProcessId(h, &pid);
  if (pid != f->pid) return TRUE;
  RECT r;
  const int haveRect = GetClientRect(h, &r) ? 1 : 0;
  const int vis = IsWindowVisible(h) ? 1 : 0;
  const int cw  = haveRect ? (int)(r.right - r.left) : -1;
  const int ch  = haveRect ? (int)(r.bottom - r.top) : -1;
  if (!vis || !haveRect || cw < 64 || ch < 64) {
    if (!s_wndRefusedLogged) {
      s_wndRefusedLogged = 1;
      wt_log("window: first window of this process is %p, REFUSED -- visible %d, client %dx%d. "
             "Both must hold before it is taken, so the gap to the accept line below is either "
             "this window becoming visible and big enough, or the game not having made the real "
             "one yet", (void*)h, vis, cw, ch);
    }
    return TRUE;
  }
  f->best = h;
  return FALSE;
}

// Runs ON THE WINDOW'S OWN THREAD, posted by the worker as WTM_BORDERLESS: every call is local, so nothing can block
// on another thread.
void wt_apply_borderless(HWND h) {
  MONITORINFO mi = { sizeof(MONITORINFO) };
  if (!GetMonitorInfoA(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return;
  int x = mi.rcMonitor.left, y = mi.rcMonitor.top;
  int w = mi.rcMonitor.right - mi.rcMonitor.left;
  int hgt = mi.rcMonitor.bottom - mi.rcMonitor.top;
  if (g_cfg.winw > 0 && g_cfg.winh > 0) { x = g_cfg.winx; y = g_cfg.winy; w = g_cfg.winw; hgt = g_cfg.winh; }

  const LONG style = GetWindowLongA(h, GWL_STYLE);
  const LONG ex    = GetWindowLongA(h, GWL_EXSTYLE);
  const LONG wantStyle = (style & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX
                                    | WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER
                                    | WS_DLGFRAME)) | WS_POPUP;
  const LONG wantEx = ex & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE
                             | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
  // Already what this call would make it? Then nothing to re-apply and the worker can stop waiting.
  RECT cur;
  const int haveRect = GetWindowRect(h, &cur) ? 1 : 0;
  if (style == wantStyle && ex == wantEx && haveRect &&
      cur.left == x && cur.top == y &&
      (cur.right - cur.left) == w && (cur.bottom - cur.top) == hgt) {
    if (InterlockedIncrement(&s_borderlessStable) == 1)
      wt_log("borderless: window %p already %d,%d %dx%d with the style we want; "
             "re-applying only until it stays that way", (void*)h, x, y, w, hgt);
    return;
  }
  InterlockedExchange(&s_borderlessStable, 0);
  SetWindowLongA(h, GWL_STYLE, wantStyle);
  SetWindowLongA(h, GWL_EXSTYLE, wantEx);
  // SWP_ASYNCWINDOWPOS because this is not the window's thread: synchronous delivery waits for it while it is inside
  // our Present hook (the game hung white on the first overlay frame).
  SetWindowPos(h, HWND_TOP, x, y, w, hgt,
               SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_ASYNCWINDOWPOS);
  wt_log("borderless: window %p moved to %d,%d %dx%d", (void*)h, x, y, w, hgt);
}

// The window doesn't exist when the DLL loads and the game resizes it while starting, so the worker re-applies for a
// few seconds, then stops (a permanent fight would be worse than a border). drm_wait is the protection wait less what
// is spent.
static void drm_wait(DWORD startMs) {
  const DWORD spent = GetTickCount() - startMs;
  if (spent < 3000) Sleep(3000 - spent);
}

static DWORD WINAPI worker(LPVOID param) {
  read_cfg();
  // Rows for features this port has no reader for are marked unavailable before anything draws them.
  features_init();
  // World Tour keeps settings in a binary profile, not a DUKE3D.CFG: an empty buffer means no row warns.
  menu_gamecfg_load(nullptr);
  // THE BORDERLESS RESTYLE DOES NOT NEED DECRYPTED CODE, so it runs before the protection wait (drm_wait subtracts what
  // it spent).
  const DWORD startMs = GetTickCount();
  HWND hEarly = 0;
  if (g_cfg.borderless || g_cfg.overlay || g_cfg.mouseaimon) {
    const DWORD pid0 = GetCurrentProcessId();
    for (int i = 0; i < 30 && !hEarly; i++) {
      FindWin f0 = { pid0, 0 };
      EnumWindows(pick_window, (LPARAM)&f0);
      hEarly = f0.best;
      if (!hEarly) Sleep(100);
    }
    // Called DIRECTLY, not posted: safe here (no subclass yet, and the overlay deadlock needs the Present hook,
    // installed below). SWP_ASYNCWINDOWPOS cannot block either way.
    if (hEarly && g_cfg.borderless) {
      wt_apply_borderless(hEarly);
      wt_log("borderless: first restyle at %lu ms, before the protection wait",
             (unsigned long)(GetTickCount() - startMs));
    }
  }
  int waited = 0;
  if (g_cfg.fpscap > 0 || g_cfg.overlay) {
    drm_wait(startMs);
    waited = 1;
    install_present_hook((HINSTANCE)param);
    // Then wait for the game to present a frame before touching its code: a hung run read the screen size as -1x-1
    // before video state existed.
    for (int i = 0; i < 600 && !s_presented; i++) Sleep(100);
    wt_log("worker: first frame %s", s_presented ? "seen" : "never arrived; carrying on");
  } else {
    wt_log("cap: off, overlay: off");
  }
  // The mouse hook fingerprints the game's code, decrypted only once the protection has run (the cap's three
  // seconds); a mismatch installs nothing, so a too-short wait shows in the log.
  if (g_cfg.mouseaimon) {
    if (!waited) { drm_wait(startMs); waited = 1; }
    mouseaim_install(g_cfg.mouseaimsens, g_cfg.mouseaimvscale);
  } else {
    wt_log("mouseaim: off");
  }
  if (g_cfg.smoothview) {
    if (!waited) { drm_wait(startMs); waited = 1; }
    smoothview_install();
  } else {
    wt_log("smoothview: off");
  }
  actorstate_install();
  // Autosave: one fingerprinted address, no code patch, so not gated on a switch.
  if (!waited) { drm_wait(startMs); waited = 1; }
  autosave_install();
  // Weapon switching: two hooks and the preference list, applied on the first checkavailweapon (DUKE3D.CFG may not be
  // parsed yet).
  autoswitch_install();
  if (g_cfg.fovon) {
    if (!waited) { drm_wait(startMs); waited = 1; }
    fov_install();
  } else {
    wt_log("fov: off");
  }
  // After mouseaim on purpose: the camera flies on that feature's raw counts and holds the player still through its
  // getinput hook.
  if (g_cfg.freecamon) {
    if (!waited) { drm_wait(startMs); waited = 1; }
    if (camera_install()) {
      nocullwt_install();
      // Screenshot mode follows the camera in; its weapon hook is a code patch and needs the protection wait.
      if (!shot_install()) unported_shot_mark_unavailable();
    } else {
      nocull_mark_unavailable();
      unported_shot_mark_unavailable();
    }
  } else {
    wt_log("camera: off (freecamon=0; the hook is only installed when the ini asks for it, "
           "so switching the View tab's row on needs a restart)");
  }
  // The in-world layer's seam hook, installed unconditionally (with no producer its draw sees an empty queue).
  {
    if (!waited) { drm_wait(startMs); waited = 1; }
    const unsigned char* base = (const unsigned char*)GetModuleHandleA(0);
    const IMAGE_DOS_HEADER* d = (const IMAGE_DOS_HEADER*)base;
    const IMAGE_NT_HEADERS* n = base && d->e_magic == IMAGE_DOS_SIGNATURE
        ? (const IMAGE_NT_HEADERS*)(base + d->e_lfanew) : 0;
    // The dump key, read before anything that might report through it. Default 0x50 (P): in none of the game, panel,
    // camera or clipboard bindings.
    dbgdump_configure(GetPrivateProfileIntA("wtmod", "debugdump", 0, wt_ini_path()),
                      GetPrivateProfileIntA("wtmod", "debugdumpkey", 0x50, wt_ini_path()));
    if (n && n->Signature == IMAGE_NT_SIGNATURE)
      world3d_install(base, n->OptionalHeader.SizeOfImage);
    if (n && n->Signature == IMAGE_NT_SIGNATURE)
      wswitch_install(base, n->OptionalHeader.SizeOfImage);
    if (n && n->Signature == IMAGE_NT_SIGNATURE)
      firerate_install(base, n->OptionalHeader.SizeOfImage);
    else
      wt_log("world3d: no PE image; the in-world layer stays off");
  }
  // Damage numbers fingerprint ifhitbyweapon, so they wait for the protection; installed whenever the ini asks. With the
  // row off the hook forwards and records nothing.
  if (g_cfg.dmgnumbers) {
    if (!waited) { drm_wait(startMs); waited = 1; }
    dmgnum_install();
  } else {
    wt_log("dmgnum: off (dmgnumbers=0; the hook is only installed when the ini asks for it, "
           "so switching the row on needs a restart)");
  }
  // THE SHOOT HOOK, ON ITS OWN: inside the damage-number branch, turning DAMAGE NUMBERS off took silent aim and perfect
  // accuracy down with it, silently. It serves two switches, so off it forwards at a compare per shot.
  if (!waited) { drm_wait(startMs); waited = 1; }
  silent_install();
  // The window exists, so the protection has long finished. One shot: the key is cleared in the ini, so it must be set
  // again for another dump.
  if (g_cfg.dump) {
    Sleep(8000);
    dump_image();
    if (wt_ini_path()[0]) WritePrivateProfileStringA("wtmod", "dump", "0", wt_ini_path());
  }
  // One window search for the borderless restyle and the subclass; real mouse aiming needs the window too (WM_INPUT is
  // its only source).
  if (!g_cfg.borderless && !g_cfg.overlay && !g_cfg.mouseaimon) {
    wt_log("borderless: off, panel: off, mouse aim off");
    return 0;
  }
  const DWORD pid = GetCurrentProcessId();
  HWND h = hEarly;            // usually already found, and already restyled
  const DWORD searchStart = GetTickCount();
  int tries = 0;
  for (int i = 0; i < 600 && !h; i++) {       // up to a minute, 100 ms apart
    FindWin f = { pid, 0 };
    EnumWindows(pick_window, (LPARAM)&f);
    h = f.best;
    tries = i + 1;
    if (!h) Sleep(100);
  }
  if (h && !hEarly)
    wt_log("window: accepted %p after %lu ms and %d passes%s. The restyle goes out on the next "
           "line, so everything before it is the player watching a bordered window",
           (void*)h, (unsigned long)(GetTickCount() - searchStart), tries,
           s_wndRefusedLogged ? "" : " -- and NO window of this process was refused before it, "
                                     "so the game had not made one until now");
  if (!h) { wt_log("window: none found; no borderless and no panel"); return 0; }
  // Set before the subclass so the window thread never reads a half-set pair. The subclass does NOT move early with the
  // restyle: it is a LIVE CALLBACK running our panel, camera and clipboard code on every key.
  input_set_clipboard_keys(g_cfg.freecamcopykey, g_cfg.freecampastekey);
  {
    const int mv[WTFK_COUNT] = {
      g_cfg.freecamfwdkey, g_cfg.freecambackkey, g_cfg.freecamleftkey,
      g_cfg.freecamrightkey, g_cfg.freecamupkey, g_cfg.freecamdownkey,
      g_cfg.freecamfastkey, g_cfg.freecamslowkey
    };
    input_set_freecam_move_keys(mv);
    wt_log("input: free camera movement keys 0x%02X/0x%02X/0x%02X/0x%02X, up 0x%02X, "
           "DOWN 0x%02X, fast 0x%02X, slow 0x%02X -- added to the game's own bindings, "
           "not in place of them", mv[0], mv[1], mv[2], mv[3], mv[4], mv[5], mv[6], mv[7]);
  }
  if (g_cfg.overlay || g_cfg.mouseaimon) input_install(h, g_cfg.menukey);
  else wt_log("panel: off, the overlay it draws through is off");
  if (!g_cfg.borderless) {
    // SETTLE ANYWAY: nothing of ours moves the window, and returning before the flag left the gate closed all session.
    InterlockedExchange(&s_windowSettled, 1);
    wt_log("borderless: off; nothing of ours moves the window, so drawing may start");
    return 0;
  }
  // Posted, never called: the work happens on the window's thread. Up to five seconds, ending on a SIGNAL: four passes
  // running that read back correct leave nothing to fight.
  int passes = 0;
  for (int i = 0; i < 20; i++) {              // up to ~5 s of re-applying
    PostMessageA(h, WTM_BORDERLESS, 0, 0);
    Sleep(250);
    passes = i + 1;
    if (s_borderlessStable >= 4) break;
  }
  wt_log("borderless: %d passes of a possible 20, %ld of which moved the window; stopped "
         "because %s. Nothing is drawn until this line", passes, s_borderlessDiffs,
         s_borderlessStable >= 4 ? "the window stayed put" : "the twenty passes ran out");
  // Belt and braces with SWP_ASYNCWINDOWPOS: the overlay's first frame is slow enough (buffers, states, textures) to
  // still be running when a move arrives.
  InterlockedExchange(&s_windowSettled, 1);
  wt_log("overlay: the window has settled; drawing may start");
  wt_log("borderless: done");
  return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);
    wt_log("wtmod loaded into pid %lu", GetCurrentProcessId());
    CreateThread(0, 0, worker, inst, 0, 0);
  } else if (reason == DLL_PROCESS_DETACH) {
    // The subclass lives in another module's data, so it is the one thing worth taking back out; reserved non-null
    // means the process is exiting.
    input_shutdown(reserved != 0);
    // Fullbright patches the game's code, so it must come out on the way down (skipped at process exit).
    if (reserved == 0) light_drop();
  }
  return TRUE;
}

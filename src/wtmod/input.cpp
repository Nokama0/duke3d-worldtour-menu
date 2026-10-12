// The window subclass and the two-thread handover. The previous procedure is chained except while the panel is open,
// when its keys and mouse messages are answered here (a key Duke never sees cannot fire a weapon). Only the render
// thread talks to menu.cpp.
#include "input.h"
#include "dbgdump.h"
#include "hook.h"
#include "mouseaim.h"
#include "camera.h"
#include "campos.h"
#include "clipboard.h"
#include <windows.h>
#include <windowsx.h>   // client coordinates are signed

static WNDPROC s_prev   = 0;
static HWND    s_hwnd   = 0;
static int     s_panelVk = VK_INSERT;   // window thread reads it; never written after install
static int     s_copyVk  = 0;
static int     s_pasteVk = 0;

// Shared by the two threads: written with interlocked operations, read as plain aligned loads (atomic on x86).
static volatile LONG s_open  = 0;   // window thread writes, render thread reads
static volatile LONG s_ready = 0;   // render thread writes, window thread reads
static volatile LONG s_keycapArmed = 0;   // render thread writes, window thread reads
static volatile LONG s_rawReady = 0;      // set once by install, read everywhere
static volatile LONG s_freecamVk   = 0;   // render thread writes, window thread reads
static volatile LONG s_freecamEdge = 0;   // window thread sets, tic clears
static volatile LONG s_teleportVk = 0;    // 0 disarms: the camera is down
static volatile LONG s_aimVk = 0;         // 0 disarms: the aimbot is off
static volatile LONG s_aimHeld = 0;       // window thread sets and clears
static volatile LONG s_teleportEdge = 0;  // window thread sets, tic clears

// The panel's own cursor and buttons, in CLIENT pixels. Window thread writes, render thread reads once a frame.
static volatile LONG s_curX = 0, s_curY = 0;
static volatile LONG s_clientW = 0, s_clientH = 0;
static volatile LONG s_downL = 0, s_downR = 0;
static volatile LONG s_seqL = 0, s_seqR = 0;   // bumped on every press, so a short click survives

// Window thread only: where the system cursor was when last heard from; the next message's delta is measured from it.
static int  s_lastX = 0, s_lastY = 0;
static bool s_haveLast = false;
static int  s_originX = 0, s_originY = 0;

// Buttons the GAME saw go down (bit per button): its press needs its release even with the panel open, or it holds.
enum { BTN_L = 0, BTN_R, BTN_M, BTN_X1, BTN_X2, BTN_COUNT };
static unsigned s_gameSawPress = 0;

UiInput g_ui = { 0, 0, false, false, false, false };

bool input_menu_open() { return s_open != 0; }

int input_raw_ready() { return s_rawReady != 0; }

void input_set_ready(int ready) {
  const LONG want = ready ? 1 : 0;
  if (s_ready != want) InterlockedExchange(&s_ready, want);
}

void input_set_keycap_armed(int armed) {
  const LONG want = armed ? 1 : 0;
  if (s_keycapArmed != want) InterlockedExchange(&s_keycapArmed, want);
}

void input_close_menu() { InterlockedExchange(&s_open, 0); }

// One bit a slot. Written only by the window thread, read by the tic thread.
static volatile LONG s_camVk[WTFK_COUNT] = { 0 };
static volatile LONG s_camKeys = 0;

void input_set_freecam_move_keys(const int* vks) {
  if (!vks) return;
  for (int i = 0; i < WTFK_COUNT; i++) {
    const LONG want = (vks[i] > 0 && vks[i] < 255) ? (LONG)vks[i] : 0;
    if (s_camVk[i] != want) InterlockedExchange(&s_camVk[i], want);
  }
  InterlockedExchange(&s_camKeys, 0);
}

unsigned input_freecam_move_keys() { return (unsigned)s_camKeys; }

// WM_KEYDOWN DELIVERS THE GENERIC MODIFIER, NOT THE SIDE: wParam is VK_SHIFT/VK_CONTROL/VK_MENU, never the L/R codes.
// The side is in lParam: bit 24 is the extended flag, and Shift resolves its scancode via MAPVK_VSC_TO_VK_EX.
static int vk_with_side(int vk, LPARAM lp) {
  const UINT sc = (UINT)((lp >> 16) & 0xFFu);
  const int ext = (lp & 0x01000000) != 0;
  switch (vk) {
    case VK_SHIFT: {
      const UINT side = MapVirtualKeyA(sc, 3 /*MAPVK_VSC_TO_VK_EX*/);
      return side ? (int)side : vk;
    }
    case VK_CONTROL: return ext ? VK_RCONTROL : VK_LCONTROL;
    case VK_MENU:    return ext ? VK_RMENU : VK_LMENU;
    default:         return vk;
  }
}

static void cam_key_mark(int vk, int down) {
  for (int i = 0; i < WTFK_COUNT; i++) {
    if (!s_camVk[i] || vk != (int)s_camVk[i]) continue;
    if (down) InterlockedOr(&s_camKeys, (LONG)(1L << i));
    else      InterlockedAnd(&s_camKeys, (LONG)~(1L << i));
  }
}

// Marked under BOTH spellings: the side (VK_LSHIFT) matches that side only, the generic (VK_SHIFT) either.
static void cam_key_mark_msg(int vk, LPARAM lp, int down) {
  const int side = vk_with_side(vk, lp);
  cam_key_mark(side, down);
  if (side != vk) cam_key_mark(vk, down);
}

static void cam_keys_clear() { InterlockedExchange(&s_camKeys, 0); }

void input_set_freecam_key(int vk) {
  const LONG want = (vk > 0 && vk < 255) ? (LONG)vk : 0;
  if (s_freecamVk != want) {
    InterlockedExchange(&s_freecamVk, want);
    // A rebind must not leave a press waiting for a key nobody will look at again.
    InterlockedExchange(&s_freecamEdge, 0);
  }
}

int input_take_freecam_edge() { return (int)InterlockedExchange(&s_freecamEdge, 0); }

void input_set_teleport_key(int vk) {
  const LONG want = (vk > 0 && vk < 255) ? (LONG)vk : 0;
  if (s_teleportVk != want) {
    InterlockedExchange(&s_teleportVk, want);
    InterlockedExchange(&s_teleportEdge, 0);
  }
}

int input_take_teleport_edge() { return (int)InterlockedExchange(&s_teleportEdge, 0); }

void input_set_aim_key(int vk) {
  const LONG want = (vk > 0 && vk < 255) ? (LONG)vk : 0;
  if (s_aimVk != want) {
    InterlockedExchange(&s_aimVk, want);
    // A rebind must not leave the aim believing the old key is held.
    InterlockedExchange(&s_aimHeld, 0);
  }
}

bool input_aim_held() { return InterlockedCompareExchange(&s_aimHeld, 0, 0) != 0; }

// THE THUMB BUTTONS (mousebind.h), latched here as the only thread that sees them. Down only while the panel is closed,
// up always: a bound action must not fire from behind the panel, nor stick if released while it is open.
static volatile LONG s_downX1 = 0, s_downX2 = 0;
bool input_mouse4_down() { return InterlockedCompareExchange(&s_downX1, 0, 0) != 0; }
bool input_mouse5_down() { return InterlockedCompareExchange(&s_downX2, 0, 0) != 0; }

// One producer (window thread), one consumer (render thread); counters only grow, the index is their low bits. A full
// queue drops the newest key: 64 keys between two frames is not typeable.
static const int KEYQ_SIZE = 64;
static volatile LONG s_keyHead = 0;   // window thread writes
static volatile LONG s_keyTail = 0;   // render thread writes
static volatile LONG s_keyRing[KEYQ_SIZE];

static void key_queue_push(int packed) {
  const LONG head = s_keyHead, tail = s_keyTail;
  if (head - tail >= KEYQ_SIZE) return;
  s_keyRing[head & (KEYQ_SIZE - 1)] = packed;
  InterlockedExchange(&s_keyHead, head + 1);
}

int input_take_key() {
  const LONG tail = s_keyTail;
  if (s_keyHead == tail) return 0;
  const int packed = (int)s_keyRing[tail & (KEYQ_SIZE - 1)];
  InterlockedExchange(&s_keyTail, tail + 1);
  return packed;
}

static void key_queue_clear() {
  InterlockedExchange(&s_keyTail, s_keyHead);
}

// Win32 calls here run on the window's own thread: a cross-thread window call once deadlocked this mod.
static void cursor_cache_client() {
  if (!s_hwnd) return;
  RECT rc;
  if (!GetClientRect(s_hwnd, &rc)) return;
  POINT org = { 0, 0 };
  if (!ClientToScreen(s_hwnd, &org)) return;
  InterlockedExchange(&s_clientW, rc.right - rc.left);
  InterlockedExchange(&s_clientH, rc.bottom - rc.top);
  s_originX = org.x;
  s_originY = org.y;
}

// The cursor starts at the client centre, where the game has been holding the system cursor.
static void cursor_open() {
  cursor_cache_client();
  const int w = s_clientW, h = s_clientH;
  InterlockedExchange(&s_curX, w > 0 ? w / 2 : 0);
  InterlockedExchange(&s_curY, h > 0 ? h / 2 : 0);
  s_haveLast = false;
  InterlockedExchange(&s_downL, 0);
  InterlockedExchange(&s_downR, 0);
  key_queue_clear();
}

// Puts the system cursor back in the middle and records that as the position the next delta is measured from, so the
// warp's echo WM_MOUSEMOVE yields a delta of zero.
static void cursor_recentre(int cx, int cy) {
  s_lastX = cx;
  s_lastY = cy;
  s_haveLast = true;
  if (!SetCursorPos(s_originX + cx, s_originY + cy)) {
    // Once, not per stray: the panel's own cursor still moves, right up to the screen edge.
    static bool logged = false;
    if (!logged) {
      logged = true;
      wt_log("input: SetCursorPos failed (err %lu); the panel's cursor may stop "
             "at a screen edge", GetLastError());
    }
  }
}

// Raw input, because cursor POSITIONS cannot say where the mouse went: the game warps the system cursor to the window
// centre, so differencing positions cancelled the warp against the mouse.
static void raw_register(HWND h) {
  RAWINPUTDEVICE rid;
  rid.usUsagePage = 0x01;
  rid.usUsage     = 0x02;          // generic desktop / mouse
  rid.dwFlags     = 0;             // messages only while this window has focus
  rid.hwndTarget  = h;
  if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
    wt_log("input: raw mouse registration failed (err %lu); the panel's cursor will not move "
           "and real mouse aiming has no source", GetLastError());
  else {
    InterlockedExchange(&s_rawReady, 1);
    wt_log("input: raw mouse registered");
  }
}

// One WM_INPUT, read whether the panel is open or not: open, the counts move the panel's cursor; closed, they are the
// aim's only source, at the device's report rate.
static void raw_motion(LPARAM lp) {
  RAWINPUT ri;
  UINT size = sizeof(ri);
  if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) return;
  if (ri.header.dwType != RIM_TYPEMOUSE) return;
  // A tablet or remote desktop reports where it IS, not how far it moved: no delta to add.
  if (ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) return;
  // Handed over unconditionally: mouseaim refuses while the panel is open, while paused and under a camera.
  mouseaim_raw_counts((int)ri.data.mouse.lLastX, (int)ri.data.mouse.lLastY);
  if (!s_open) return;
  const int w = s_clientW, h = s_clientH;
  InterlockedExchange(&s_curX, wt_cursor_clamped((int)s_curX + ri.data.mouse.lLastX, w));
  InterlockedExchange(&s_curY, wt_cursor_clamped((int)s_curY + ri.data.mouse.lLastY, h));
}

// WM_MOUSEMOVE while the panel is open: motion comes from raw input; this only notices the cursor drifting off the
// window, which would take our button messages with it.
static void cursor_motion(int x, int y) {
  const int w = s_clientW, h = s_clientH;
  const int cx = w / 2, cy = h / 2;
  if (wt_should_recentre(x, y, cx, cy, WT_RECENTRE_PX)) cursor_recentre(cx, cy);
}

void input_mouse_release_all() {
  InterlockedExchange(&s_downL, 0);
  InterlockedExchange(&s_downR, 0);
  key_queue_clear();
}

void input_cursor(int* outX, int* outY) {
  if (outX) *outX = g_ui.mx;
  if (outY) *outY = g_ui.my;
}

void input_begin_frame(int overlayW, int overlayH) {
  static unsigned s_seenL = 0, s_seenR = 0;
  static bool s_wasDown = false, s_wasRDown = false;

  const int clientW = (int)s_clientW, clientH = (int)s_clientH;
  g_ui.mx = wt_cursor_to_overlay((int)s_curX, clientW, overlayW);
  g_ui.my = wt_cursor_to_overlay((int)s_curY, clientH, overlayH);

  const unsigned seqL = (unsigned)s_seqL, seqR = (unsigned)s_seqR;
  g_ui.down  = wt_button_down_this_frame(s_downL != 0, seqL, s_seenL);
  g_ui.rdown = wt_button_down_this_frame(s_downR != 0, seqR, s_seenR);
  s_seenL = seqL;
  s_seenR = seqR;

  g_ui.pressed  = ui_press_edge(g_ui.down, s_wasDown);
  g_ui.rpressed = ui_press_edge(g_ui.rdown, s_wasRDown);
  s_wasDown  = g_ui.down;
  s_wasRDown = g_ui.rdown;
}

// Which of our five latched buttons a message names, or -1. The thumb buttons are latched only for their release rule.
static int button_of(UINT msg, WPARAM wp, int* isDown) {
  switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: *isDown = 1; return BTN_L;
    case WM_LBUTTONUP:                          *isDown = 0; return BTN_L;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: *isDown = 1; return BTN_R;
    case WM_RBUTTONUP:                          *isDown = 0; return BTN_R;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: *isDown = 1; return BTN_M;
    case WM_MBUTTONUP:                          *isDown = 0; return BTN_M;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: *isDown = 1;
      return GET_XBUTTON_WPARAM(wp) == XBUTTON2 ? BTN_X2 : BTN_X1;
    case WM_XBUTTONUP:                          *isDown = 0;
      return GET_XBUTTON_WPARAM(wp) == XBUTTON2 ? BTN_X2 : BTN_X1;
    default: return -1;
  }
}

// The camera's two chords run HERE on the window thread, where every key is decided and the OS clipboard is reachable.
// Each answers whether the key was its own (false: the camera is not flying, the press falls through).
static bool chord_copy_pose() {
  FreecamPose p;
  if (!camera_pose_read(&p)) return false;
  char line[96];
  if (!campos_format(p, line, (int)sizeof line)) {
    wt_log("input: the camera's pose did not fit its own line; nothing copied");
    return true;
  }
  const bool ok = clipboard_write_text(line);
  wt_log("input: camera pose %s the clipboard: %s", ok ? "copied to" : "NOT copied to", line);
  return true;
}

static bool chord_paste_pose() {
  FreecamPose live;
  if (!camera_pose_read(&live)) return false;
  // 1200 bytes holds the whole two-line readout with room to spare; anything longer is not a readout.
  char text[1200];
  FreecamPose want;
  if (!clipboard_read_text(text, (int)sizeof text) || !campos_accept(text, &want)) {
    // One log line and the camera stays: a wrong paste must not put it somewhere the player cannot fly back from.
    wt_log("input: the clipboard holds no plausible \"cam x= y= z= ang= pitch=\" line; "
           "the camera is left where it is");
    return true;
  }
  camera_request_pose(want);
  wt_log("input: camera pose taken from the clipboard, %d,%d,%d ang %d pitch %d; "
         "the next tic flies there", want.x, want.y, want.z, want.ang, want.pitch);
  return true;
}

static LRESULT CALLBACK wt_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  // Posted by the worker (WTM_BORDERLESS): runs on the window's thread, so the restyle is a local call.
  if (msg == WTM_BORDERLESS) { wt_apply_borderless(h); return 0; }

  // Re-read the client rectangle here (borderless restyle, resize), not per mouse message.
  if (msg == WM_SIZE || msg == WM_MOVE) cursor_cache_client();
  // Chained afterwards, always: WM_INPUT must reach DefWindowProc.
  if (msg == WM_INPUT) raw_motion(lp);

  const bool open = s_open != 0;

  {
    int isDown = 0;
    const int btn = button_of(msg, wp, &isDown);
    if (btn >= 0) {
      const bool sawPress = (s_gameSawPress & (1u << btn)) != 0;
      const bool chains = wt_mouse_chains(isDown ? WTM_BUTTON_DOWN : WTM_BUTTON_UP,
                                          open, sawPress);
      if (chains) {
        if (isDown) s_gameSawPress |= (1u << btn);
        else        s_gameSawPress &= ~(1u << btn);
      }
      // Thumb-button latch, before the panel branch: releases always, presses only with the panel closed.
      if (btn == BTN_X1 && (!open || !isDown)) InterlockedExchange(&s_downX1, isDown ? 1 : 0);
      if (btn == BTN_X2 && (!open || !isDown)) InterlockedExchange(&s_downX2, isDown ? 1 : 0);
      if (open) {
        // Only left and right reach the panel; middle and the thumb buttons are swallowed, so no bound action fires behind it.
        if (btn == BTN_L) {
          InterlockedExchange(&s_downL, isDown ? 1 : 0);
          if (isDown) InterlockedIncrement(&s_seqL);
        } else if (btn == BTN_R) {
          InterlockedExchange(&s_downR, isDown ? 1 : 0);
          if (isDown) InterlockedIncrement(&s_seqR);
        }
      }
      if (!chains) return msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP
                        || msg == WM_XBUTTONDBLCLK ? TRUE : 0;
      return CallWindowProcA(s_prev, h, msg, wp, lp);
    }
  }

  switch (msg) {
    case WM_MOUSEMOVE:
      if (!wt_mouse_chains(WTM_MOVE, open, false)) {
        cursor_motion(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
      }
      break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
      // Swallowed, not fed anywhere (the panel has no wheel input); a wheel reaching the game would change weapon.
      if (!wt_mouse_chains(WTM_WHEEL, open, false)) return 0;
      break;
// Focus lost with a button held: the release goes to the other window, so a latched button would have the panel
// dragging with nothing held. Cleared, then chained so the game sees the focus change.
    case WM_KILLFOCUS:
      input_mouse_release_all();
// The camera's movement keys are LEVELS: one still down at focus loss would fly the camera for ever.
      cam_keys_clear();
      s_haveLast = false;
      break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      // Bit 30 is the previous key state: set means auto-repeat.
      const int repeat = (lp & 0x40000000) != 0;
      const int vk     = (int)wp;
// Clipboard chords, tested BEFORE the panel key because by default they share it: Ctrl+Insert copies the viewpoint,
// Shift+Insert flies to the one on the clipboard. Panel closed, initial press only; else false and Insert opens it.
      if (!repeat && !open) {
        const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl && !shift && s_copyVk && vk == s_copyVk && chord_copy_pose()) return 0;
        if (shift && !ctrl && s_pasteVk && vk == s_pasteVk && chord_paste_pose()) return 0;
      }
      if (vk == s_panelVk && s_ready) {
        if (!repeat) {
          if (!open) cursor_open();
          InterlockedXor(&s_open, 1);
          cam_keys_clear();   // no level survives the panel opening or closing
        }
        return 0;
      }
      if (open) {
// Camera keys pressed with the panel open belong to the panel (the mask was cleared on open). Escape closes it unless a
// binding row is armed, when it is queued for the capture; the armed flag is last frame's answer, and if stale the
// drain closes the panel, so Escape always does one of the two.
        if (vk == VK_ESCAPE && !s_keycapArmed) { InterlockedExchange(&s_open, 0); return 0; }
        if (vk > 0 && vk < 255) {
          int packed = vk;
          if (GetKeyState(VK_SHIFT)   & 0x8000) packed |= WT_KEY_SHIFT;
          if (GetKeyState(VK_CONTROL) & 0x8000) packed |= WT_KEY_CTRL;
          if (repeat) packed |= WT_KEY_REPEAT;
          key_queue_push(packed);
        }
        // Queued or not, the game never sees it.
        return 0;
      }
// The camera's movement levels, panel closed: marked and CHAINED like any key. While the camera flies
// camera_lock_input has zeroed the velocity globals and action bits, so a level taken here reaches no game action.
      cam_key_mark_msg(vk, lp, 1);
// The camera's toggle, panel closed: taken from Duke so a key that is also a game function cannot fire it, and recorded
// as ONE press however long held (a repeat would flap the camera). The RELEASE still chains.
      if (vk > 0 && vk < 255 && vk == s_freecamVk) {
        if (!repeat) InterlockedExchange(&s_freecamEdge, 1);
        return 0;
      }
// The teleport, armed only while the camera flies (camera.cpp sets and clears the binding). Swallowed and de-repeated
// like the toggle.
      if (vk > 0 && vk < 255 && vk == s_teleportVk) {
        if (!repeat) InterlockedExchange(&s_teleportEdge, 1);
        return 0;
      }
// The aim hold key is NOT swallowed, unlike the toggle and teleport: its default is a mouse thumb button that may keep
// doing what it already does. Held state only, so a repeat is harmless.
      if (vk > 0 && vk < 255 && vk == s_aimVk) InterlockedExchange(&s_aimHeld, 1);
// The debug dump's key, panel closed: swallowed like the camera toggle so a rebind onto a game key cannot fire a
// weapon; initial press only. Off unless the ini asked (dbgdump_key answers 0).
      {
        const int dumpVk = dbgdump_key();
        if (dumpVk && vk == dumpVk) {
          if (!repeat) dbgdump_request();
          return 0;
        }
      }
      break;
    }
    case WM_CHAR:
      if (open) return 0;
      break;
// Releases ALWAYS chain, open or closed: swallowing one left Duke running after the panel opened mid-run. A release the
// game never saw pressed only tells it a key is up, which it already believed.
    case WM_KEYUP:
    case WM_SYSKEYUP:
      // Unconditional, before any gate: a release must clear its level whatever the panel is doing, or a key held while
      // the panel opened would stay down.
      cam_key_mark_msg((int)wp, lp, 0);
// The aim hold key's release, cleared unconditionally for the same reason.
      if ((int)wp > 0 && (int)wp < 255 && (LONG)wp == s_aimVk)
        InterlockedExchange(&s_aimHeld, 0);
      break;
    default:
      break;
  }
  return CallWindowProcA(s_prev, h, msg, wp, lp);
}

void input_set_clipboard_keys(int copyVk, int pasteVk) {
  s_copyVk  = (copyVk  > 0 && copyVk  < 255) ? copyVk  : 0;
  s_pasteVk = (pasteVk > 0 && pasteVk < 255) ? pasteVk : 0;
  wt_log("input: camera clipboard chords are Ctrl+0x%02X (copy) and Shift+0x%02X (paste), "
         "in Megaton's own \"cam x= y= z= ang= pitch=\" spelling", s_copyVk, s_pasteVk);
}

void input_install(struct HWND__* hwnd, int panelVk) {
  if (s_prev) { wt_log("input: already subclassed"); return; }
  if (!hwnd || !IsWindow((HWND)hwnd)) { wt_log("input: no window to subclass"); return; }
  if (panelVk > 0 && panelVk < 255) s_panelVk = panelVk;
  else wt_log("input: panelkey %d is not a virtual-key code; keeping 0x%02X", panelVk, s_panelVk);

  SetLastError(0);
  raw_register((HWND)hwnd);
  const LONG_PTR prev = SetWindowLongPtrA((HWND)hwnd, GWLP_WNDPROC, (LONG_PTR)wt_wndproc);
  if (!prev) {
    wt_log("input: SetWindowLongPtrA failed (err %lu); the panel cannot be opened", GetLastError());
    return;
  }
  s_prev = (WNDPROC)prev;
  s_hwnd = (HWND)hwnd;
  cursor_cache_client();
  wt_log("input: subclassed window %p (previous procedure %p), panel key 0x%02X, client %ldx%ld",
         (void*)s_hwnd, (void*)s_prev, s_panelVk, s_clientW, s_clientH);
}

void input_shutdown(int processExiting) {
  if (!s_prev || !s_hwnd) return;
  if (processExiting) {
    // Other threads are already dead and we hold the loader lock: touching user32 would buy nothing.
    wt_log("input: process exiting; the subclass goes with it");
    return;
  }
  if (!IsWindow(s_hwnd)) { wt_log("input: the window is gone; nothing to put back"); return; }
  const LONG_PTR cur = GetWindowLongPtrA(s_hwnd, GWLP_WNDPROC);
  if (cur != (LONG_PTR)wt_wndproc) {
    wt_log("input: window procedure is %p, not ours; someone subclassed after us "
           "and putting the old one back would cut them out. Leaving it alone.", (void*)cur);
    return;
  }
  SetWindowLongPtrA(s_hwnd, GWLP_WNDPROC, (LONG_PTR)s_prev);
  wt_log("input: subclass removed, %p restored", (void*)s_prev);
  s_prev = 0;
  s_hwnd = 0;
}

// The game's window procedure, subclassed, so a key can open the panel and the panel's keys never reach Duke. The
// window and render threads share only aligned 32-bit latches and one single-producer key queue, no lock; the window
// thread only queues keys.
#ifndef WTMOD_INPUT_H
#define WTMOD_INPUT_H

#include "ui.h"      // UiInput, the cursor/button state the panel reads

void input_install(struct HWND__* hwnd, int panelVk);

// The free camera's two clipboard chords, Ctrl+<copy key> and Shift+<paste key>, as virtual keys. Ini-only, handed over
// once before input_install; 0 switches a chord off.
void input_set_clipboard_keys(int copyVk, int pasteVk);

enum FreecamMoveSlot {
  WTFK_FWD = 0, WTFK_BACK, WTFK_LEFT, WTFK_RIGHT,
  WTFK_UP, WTFK_DOWN, WTFK_FAST, WTFK_SLOW,
  WTFK_COUNT
};
void input_set_freecam_move_keys(const int* vks);
unsigned input_freecam_move_keys();
// Restyling a window from a thread that does not own it hung the game (the call waited on the owning thread, inside
// Present, which waited on us). The worker POSTS this message; the window's own thread does the work. Defined in
// wtmod.cpp.
const unsigned WTM_BORDERLESS = 0x8000 + 0x4E55;   // WM_APP + "NU"
void wt_apply_borderless(struct HWND__* hwnd);

// From DLL_PROCESS_DETACH. Restores the old procedure only if ours is still installed; processExiting (DllMain's third
// argument non-null) touches nothing.
void input_shutdown(int processExiting);

void input_set_ready(int ready);

bool input_menu_open();

// True once WM_INPUT is registered on the game's window. Real mouse aiming needs raw input, so until then it must leave
// the game's mouse alone.
int input_raw_ready();

extern UiInput g_ui;

// The game warps the system cursor back to one point each frame, so the panel keeps its own cursor: seeded at the
// client centre on open, moved by WM_MOUSEMOVE deltas, clamped, drawn by the overlay.

// Once per frame before menu_draw, on the render thread: converts the latched cursor and buttons into g_ui, scaling
// client to overlay pixels (a stretched back buffer would offset every hit test), and derives the press edges.
void input_begin_frame(int overlayW, int overlayH);

void input_mouse_release_all();

void input_cursor(int* outX, int* outY);

// One queued key press, or 0. The render thread drains the queue and is the only caller into menu.cpp. Low byte is the
// virtual key, then the flags below.
int input_take_key();
const int WT_KEY_SHIFT  = 0x100;
const int WT_KEY_CTRL   = 0x200;
const int WT_KEY_REPEAT = 0x400;
inline int wt_key_vk(int packed) { return packed & 0xFF; }

// Published after each menu_draw: with a binding row armed, Escape cancels the capture and must not close the panel on
// the window thread, which queues it instead.
void input_set_keycap_armed(int armed);

void input_close_menu();

// Armed from the render thread when the panel changes the binding or the Free camera row; 0 disarms. Armed, the window
// thread takes the key AWAY from Duke (press and release) and records the press for the tic.
void input_set_freecam_key(int vk);

// One press, or 0. Edges collapse: a key held through a slow tic is one toggle. Drained on the tic clock.
int input_take_freecam_edge();

// The free camera's teleport key: same shape as the toggle, armed only while the camera flies. Taken from Duke while
// armed because the default 'T' may be bound.
void input_set_teleport_key(int vk);

int input_take_teleport_edge();

// The triggerbot's one write ORs the fire bit into the tic's input record, only inside the getinput hook after the
// original filled it. Never clears the bit: the player's key may hold it.
void input_set_fire_bit();

// The aim hold key is HELD, not an edge: "Hold key" aims only while the key is down. 0 disarms.
void input_set_aim_key(int vk);
bool input_aim_held();

bool input_mouse4_down();
bool input_mouse5_down();

// Which mouse messages the game still gets. Panel closed: all chain. Open: all are answered here, except the release of
// a button whose press the game saw (else it stays held forever).
enum WtMouseKind { WTM_MOVE = 0, WTM_BUTTON_DOWN, WTM_BUTTON_UP, WTM_WHEEL };
inline bool wt_mouse_chains(int kind, bool panelOpen, bool gameSawPress) {
  if (!panelOpen) return true;
  return kind == WTM_BUTTON_UP && gameSawPress;
}

inline int wt_cursor_clamped(int v, int dim) {
  if (v < 0) return 0;
  if (dim <= 0) return 0;
  return v >= dim ? dim - 1 : v;
}

inline int wt_cursor_to_overlay(int v, int clientDim, int overlayDim) {
  if (clientDim <= 0 || overlayDim <= 0 || clientDim == overlayDim) return v;
  return (int)(((long long)v * overlayDim) / clientDim);
}

// Whether the system cursor strayed far enough from the client centre to be put back. With the panel open the game
// never sees WM_MOUSEMOVE, so never warps, and the cursor walks to a screen edge. Measured against the centre, never a
// previous target.
const int WT_RECENTRE_PX = 192;
inline bool wt_should_recentre(int x, int y, int cx, int cy, int limit) {
  if (limit <= 0 || cx <= 0 || cy <= 0) return false;
  const int dx = x - cx, dy = y - cy;
  return dx > limit || dx < -limit || dy > limit || dy < -limit;
}

// Whether the panel reads the button as held this frame. A press and release can both land between two frames: the
// window thread bumps a counter per press, and a frame that sees it move reports down. Keeps `pressed` implying `down`.
inline bool wt_button_down_this_frame(bool physicalDown, unsigned seq, unsigned seen) {
  return physicalDown || seq != seen;
}

// SDL button numbering (1 left, 2 middle, 3 right, 4/5 thumb), also the clamp config.cpp migrates a legacy `aimbutton`
// through.
const int MOUSE_BUTTON_MIN = 1;
const int MOUSE_BUTTON_MAX = 5;

const int SDL_SCANCODE_A = 4;
const int SDL_SCANCODE_C = 6;

#endif

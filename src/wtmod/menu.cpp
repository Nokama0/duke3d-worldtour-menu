// Copied from Megaton's src/duke3dmod/menu.cpp; kept close to it, edit sparingly.
#include "menu.h"
#include "unported.h"
#include "panel_layout.h"
#include "canvas.h"
#include "config.h"
#include "autoswitch.h"
#include "common.h"
#include "font.h"
#include "input.h"
#include "fov.h"
#include "esp_layout.h"   // draw-distance slider bounds (the cull's own)
#include "target.h"       // Combat slider bounds
#include "freecam.h"      // View speed slider bounds
#include "accuracy.h"     // Weapons tab and hazard strip share the gate
#include "nocull.h"       // View tab's "(Unavailable)" query
#include "light.h"        // Fullbright rows share the same query
#include "bind.h"
#include "gamecfg.h"
#include "mouseaim.h"     // slider bounds, so it cannot offer a value the integration clamps
#include "firerate.h"     // the knob lands only on the stops
#include "anim.h"
#include "wallclock.h"
#include "tooltip.h"
#include "entry.h"
#include "autosave.h"
#include <stdio.h>
#include <string.h>
#include "mousebind.h"   // the thumb-button rows

static int s_tab = TAB_VISUALS;

// One capture for the whole panel (a second armed row would make the next press ambiguous); keycap_step owns the
// transitions.
static KeyCapState s_keycap = { KEYCAP_IDLE, -1 };

// Read once per panel open, scanned per row per frame by gamecfg_key_bound. Empty on failure: every row reads unbound.
static char s_gameCfg[GAMECFG_MAX] = { 0 };

void menu_gamecfg_load(const char* path) {
  s_gameCfg[0] = 0;
  if (!path || !path[0]) return;
  FILE* f = nullptr;
  if (fopen_s(&f, path, "rb") != 0 || !f) return;
  const size_t n = fread(s_gameCfg, 1, sizeof(s_gameCfg) - 1, f);
  s_gameCfg[n] = 0;
  fclose(f);
}

bool menu_keycap_armed() { return s_keycap.phase == KEYCAP_ARMED; }

void menu_keycap_bind(int vk) {
// A mouse button and a key are one binding to config_bind_set, but keycap_step refuses mouse 1 on the mouse arm.
  KeyCapEvent e;
  e.kind = bind_is_mouse(vk) ? KEYCAP_EV_MOUSE : KEYCAP_EV_KEY;
  e.row = -1;
  e.vk = vk;
  const KeyCapResult r = keycap_step(s_keycap, e);
  s_keycap = r.state;
  if (r.boundRow >= 0) {
    config_bind_set(g_cfg, r.boundRow, r.boundVk);
    // hooked_swap writes the ini off the draw path when this is set.
    g_cfgDirty = true;
  }
}

static void menu_note_real_close();

void menu_keycap_close() {
  KeyCapEvent e;
  e.kind = KEYCAP_EV_CLOSE;
  e.row = -1;
  e.vk = 0;
  s_keycap = keycap_step(s_keycap, e).state;
  menu_note_real_close();
}

void menu_keycap_cancel() {
  // Escape with a row armed. menu_keycap_close() reaches the same state; callers differ only in `consumed`.
  KeyCapEvent e;
  e.kind = KEYCAP_EV_ESCAPE;
  e.row = -1;
  e.vk = 0;
  s_keycap = keycap_step(s_keycap, e).state;
}

static EntryState s_entry = entry_begin();

// A committed value waits here: committed on the poll hook's thread, applied on the draw. One slot: two fields cannot
// be open in a frame.
struct MenuEntryCommit { bool pending; int rowKey; int value; };
static MenuEntryCommit s_entryCommit = { false, -1, 0 };

// The left button went down inside the field and is still down: a press puts the caret, held frames drag the selection.
// Cleared in menu_draw's preamble and at menu_entry_begin_edit.
static bool s_entryCaretDrag = false;

bool menu_entry_open() { return s_entry.open != 0; }

const char* menu_entry_text() { return s_entry.text; }

// A click away commits (left press outside the field, or a right press opening another row's field); Escape cancels.
static void entry_clickaway_commit() {
  if (!s_entry.open) return;
  const int row = s_entry.rowKey;
  const EntryResult r = entry_step(s_entry, ENTRY_EV_CLICKAWAY, 0, 0, EK_NONE, 0, 0, 0);
  s_entry = r.state;
  if (r.commit) {
    s_entryCommit.pending = true;
    s_entryCommit.rowKey = row;
    s_entryCommit.value = r.value;
  }
}

void menu_entry_begin_edit(int rowKey, int kind, int lo, int hi, const char* text) {
  // An armed capture goes down first: input.cpp asks the field before the capture, so a capture armed behind an open
  // field could never bind a digit, Backspace, Enter or Escape. Cancel, not close: the row keeps its binding.
  menu_keycap_cancel();
  // A right-click can open a field while the left button is still held in the one it replaces: no spurious selection.
  s_entryCaretDrag = false;
  s_entry = entry_step(s_entry, ENTRY_EV_OPEN, 0, rowKey, kind, lo, hi, text).state;
}

void menu_entry_close() {
  s_entry = entry_begin();
  // Else the pending commit is applied next session to a row no longer shown.
  s_entryCommit.pending = false;
  s_entryCommit.rowKey = -1;
  s_entryCommit.value = 0;
}

// Keeps the Ctrl+C/X text long enough for input.cpp to read (entry.h hands it back on the stack).
static char s_entryClip[ENTRY_TEXT_CAP + 1] = { 0 };

const char* menu_entry_key_mod(int sc, bool shift, bool ctrl) {
  if (!s_entry.open) return nullptr;
  const int row = s_entry.rowKey;

  int ev = ENTRY_EV_CHAR;
  int ch = 0;
  if (ctrl && sc == SDL_SCANCODE_A)      ev = ENTRY_EV_SELECT_ALL;
  else if (ctrl && sc == SDL_SCANCODE_C) ev = ENTRY_EV_COPY;
  else if (ctrl && sc == SDL_SCANCODE_X) ev = ENTRY_EV_CUT;
  // Ctrl+V needs a clipboard read this file cannot make (menu.h); input.cpp reads it and calls menu_entry_paste.
  else if (ctrl && sc == SDL_SCANCODE_V) return nullptr;
  else if (sc == SDL_SCANCODE_ESCAPE)    ev = ENTRY_EV_ESCAPE;
  else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
                                         ev = ENTRY_EV_ENTER;
  else if (sc == SDL_SCANCODE_BACKSPACE) ev = ENTRY_EV_BACKSPACE;
  else if (sc == SDL_SCANCODE_DELETE)    ev = ENTRY_EV_DELETE;
  else if (sc == SDL_SCANCODE_LEFT)  { ev = ENTRY_EV_LEFT;  ch = shift ? 1 : 0; }
  else if (sc == SDL_SCANCODE_RIGHT) { ev = ENTRY_EV_RIGHT; ch = shift ? 1 : 0; }
  else if (sc == SDL_SCANCODE_HOME)  { ev = ENTRY_EV_HOME;  ch = shift ? 1 : 0; }
  else if (sc == SDL_SCANCODE_END)   { ev = ENTRY_EV_END;   ch = shift ? 1 : 0; }
  else {
    ch = entry_char_of_scancode(sc);
    if (!ch) return nullptr;
  }

  const EntryResult r = entry_step(s_entry, ev, ch, 0, EK_NONE, 0, 0, 0);
  s_entry = r.state;
  if (r.commit) {
    s_entryCommit.pending = true;
    s_entryCommit.rowKey = row;
    s_entryCommit.value = r.value;
  }
  if (!r.hasClip) return nullptr;
  int n = 0;
  while (n < ENTRY_TEXT_CAP && r.clip[n]) { s_entryClip[n] = r.clip[n]; n++; }
  s_entryClip[n] = 0;
  return s_entryClip;
}

void menu_entry_key(int sc) { menu_entry_key_mod(sc, false, false); }

void menu_entry_paste(const char* s) {
  if (!s_entry.open || !s) return;
  s_entry = entry_step(s_entry, ENTRY_EV_PASTE, 0, 0, EK_NONE, 0, 0, s).state;
}

bool menu_entry_pending(int* rowKey, int* value) {
  if (!s_entryCommit.pending) return false;
  if (rowKey) *rowKey = s_entryCommit.rowKey;
  if (value)  *value  = s_entryCommit.value;
  return true;
}

// PANEL_W/H/INSET/VINSET live in panel_layout.h so panel_layout() can reproduce this rect. The vertical margin is half
// the horizontal (30 rather than 60 gives a 960px column).

const int GROUP_H = 22, GROUP_GAP = 6;
const int SUB_RULE_X = 3, SUB_INDENT = 21;

// Drag state for panel_layout.h's rule: machine, last live box, and a drag in flight (rows read it so none acts under a
// hand moving the panel).
static PanelDrag s_drag = { PDRAG_IDLE, 0, 0, { 0, 0, 0, 0 } };
static PanelBox  s_dragBox = { 0, 0, 0, 0 };
static bool      s_dragLive = false;

// One press must not do two things where the reset glyph (over the track's grab box) or the entry click-away sit inside
// a row. Spans the hold; cleared in menu_draw's preamble once the button is up.
static bool      s_pressSpent = false;

// A control claims a left press that would otherwise move the panel. Per frame, not s_pressSpent (which spans the hold,
// so a dragging slider would disarm itself on frame two).
static bool      s_pressClaimed = false;

static PanelBox panel_stored_box() {
  PanelBox b = { g_cfg.panelx, g_cfg.panely, g_cfg.panelw, g_cfg.panelh };
  return b;
}

PanelBox menu_panel_box(int logicalW, int logicalH) {
  const PanelBox laid = panel_layout(panel_stored_box(), logicalW, logicalH);
  return s_dragLive ? s_dragBox : laid;
}

UiRect menu_panel_rect(int screenW, int screenH) {
  const PanelBox b = menu_panel_box(screenW, screenH);
  return UiRect{ b.x, b.y, b.w, b.h };
}

// The scale the panel settles at: the stored setting, capped so the logical height fits. Public: a click arrives in
// device pixels. The ceiling follows the STORED height (the drawn one moves each frame of a resize), unset is PANEL_H,
// floored at PANEL_MIN_H.
static int menu_ceiling_height() {
  const int h = g_cfg.panelh > 0 ? g_cfg.panelh : PANEL_H;
  return h < PANEL_MIN_H ? PANEL_MIN_H : h;
}

int menu_applied_scale(int screenH) {
  const int stored = ui_scale_clamped(g_cfg.uiscale);
  const int ceiling = ui_scale_max_for(screenH, menu_ceiling_height(), PANEL_VINSET);
  return stored < ceiling ? stored : ceiling;
}

// Applied scale carried between frames, written only while the button is UP so it is the pre-drag value for a press.
static int s_scaleHeld = UI_SCALE_DEFAULT;

int  menu_active_tab() { return s_tab; }
void menu_set_tab(int tab) { if (tab >= 0 && tab < TAB_COUNT) s_tab = tab; }

static const char* TAB_NAMES[TAB_COUNT] = { "Visuals", "Combat", "Weapons", "View", "Settings" };
static const unsigned TAB_COLOURS[TAB_COUNT] = { C_BLUE, C_RED, C_HAZARD, C_GREEN, C_SETTINGS };

// Presentation state: cosmetic, drawing only (hit testing uses the logical UiRect, so a click mid-transition lands where
// the control is going). knob/fill/hover are indexed by row ordinal, so two tabs' Nth rows share a slot; a tab switch
// snaps every spring.
struct MenuPresentation {
  unsigned lastMs;
  AnimSpring open;
  int openTarget;
  unsigned tabSwitchMs;
  int tabFrom;
  AnimSpring pillX, pillW;
  AnimSpring knob[64];
  AnimSpring fill[64];
  double hover[64];
  double resetHover[64];
  // Glass only: openStartMs times the condense and sweep; wasOpen is the previous openTarget (an edge once per open);
  // toggleMs/toggleSeen per row: when the switch last flipped and which way (the knob's bloom).
  unsigned openStartMs;
  int wasOpen;
  unsigned toggleMs[64];
  int toggleSeen[64];   // 0 not seen yet, 1 off, 2 on
  int rowsThisFrame;
};
static MenuPresentation s_pres;

// The one moment production learns the panel closed (under ANIM_OFF the closing frame is never drawn; close_menu()
// calls menu_keycap_close() on every close). Drag and tooltip reset too: a mid-drag close would resume a half box.
static void menu_tooltip_note_close();
static void menu_note_real_close() {
  s_pres.wasOpen = 0;
  s_drag.phase = PDRAG_IDLE;
  s_dragLive = false;
  s_pressSpent = false;
  menu_tooltip_note_close();
}

// Bounds for knob/fill/hover; Combat, the longest tab, has fifteen rows.
static const int ANIM_ROW_SLOTS = 64;
static bool s_tabSwitchedThisFrame = false;
static double s_frameDt = 0.0;

// Hints as tooltips (rule in tooltip.h). s_lastRow/s_lastRowKey are the row just drawn, so row_hint attaches by
// adjacency. Key = s_tab * 1000 + ordinal (ordinals are stable only within a tab); -1 is none.
static UiRect s_lastRow = { 0, 0, 0, 0 };
static int s_lastRowKey = -1;
// This frame's answer, cleared at the top of menu_draw: the row under the cursor and its hint.
static int s_hoverRowKey = -1;
// Copied, not pointed at: the tooltip draws at the end of menu_draw and some hints are stack buffers.
static char s_hintBuf[96];
static const char* s_hoverHint = nullptr;
static const char* s_tooltipText = nullptr;
static TooltipState s_tooltip = tooltip_begin();
static const int TOOLTIP_R = 4;
// The reset glyph's hint replaces the row's while hovered. Its own static, since a row's hint is set right after its
// helper returns and would overwrite one written in row_slider_at. A literal, never a copy.
static const char* s_glyphHint = nullptr;

// Reset so the reopen's first frame starts its rest from that frame's clock.
static void menu_tooltip_note_close() {
  s_tooltip = tooltip_begin();
  s_tooltipText = nullptr;
  s_hoverHint = nullptr;
  s_glyphHint = nullptr;
  s_hoverRowKey = -1;
  s_lastRowKey = -1;
}

const char* menu_tooltip_text() { return s_tooltipText; }

static void row_hint(const char* text) {
  if (s_lastRowKey < 0 || !text) return;
  if (!ui_hit(s_lastRow, g_ui.mx, g_ui.my)) return;
  s_hoverRowKey = s_lastRowKey;
  strncpy_s(s_hintBuf, sizeof(s_hintBuf), text, _TRUNCATE);
  s_hoverHint = s_hintBuf;
}

static unsigned s_nowOverride = 0;
void menu_set_now_ms(unsigned ms) { s_nowOverride = ms; }
static unsigned menu_now() { return s_nowOverride ? s_nowOverride : wallclock_ms(); }

bool menu_closing_animation_active() {
  if (g_cfg.menuanim == ANIM_OFF || input_menu_open()) return false;
  // A spring nothing has stepped for a second is at rest; settled here so the next open animates from 0.
  if (menu_now() - s_pres.lastMs > ANIM_STALE_MS) { s_pres.open.x = 0.0; s_pres.open.v = 0.0; }
  return !anim_spring_settled(s_pres.open, 0.0, 0.01);
}

static unsigned pres_colour(unsigned c) {
  if (g_cfg.menuanim == ANIM_OFF) return c;
  const int srcA = (int)((c >> 24) & 0xFFu);
  const int a = (int)(srcA * s_pres.open.x + 0.5);
  return canvas_with_alpha(c, a);
}

static unsigned alpha_scale(unsigned c, double a) {
  if (a < 0.0) a = 0.0;
  if (a > 1.0) a = 1.0;
  const int srcA = (int)((c >> 24) & 0xFFu);
  return canvas_with_alpha(c, (int)(srcA * a + 0.5));
}

// Further scaled by the tab crossfade; rail and chrome use pres_colour.
static unsigned pres_content_colour(unsigned c) {
  const unsigned withOpen = pres_colour(c);
  if (g_cfg.menuanim == ANIM_OFF) return withOpen;
  const double tabP = anim_ease_out(anim_progress(s_pres.tabSwitchMs, s_pres.lastMs, ANIM_TAB_MS));
  const int srcA = (int)((withOpen >> 24) & 0xFFu);
  const int a = (int)(srcA * tabP + 0.5);
  return canvas_with_alpha(withOpen, a);
}

static unsigned pres_hover_lighten(unsigned c, double hoverFrac) {
  if (g_cfg.menuanim == ANIM_OFF) return c;
  return anim_blend_colour(c, C_BONE, hoverFrac);
}

static unsigned pres_hover_toward(unsigned c, unsigned target, double hoverFrac) {
  if (g_cfg.menuanim == ANIM_OFF) return c;
  return anim_blend_colour(c, target, hoverFrac);
}

// Glass style, in logical pixels and ms.
static const int      GLASS_R            = 8;    // the slab's own corner radius
static const int      GLASS_ROW_R        = 4;    // a control's, smaller so a switch still reads as a switch
static const int      GLASS_TOP_H        = 40;   // the lighter top edge's height
static const unsigned GLASS_CONDENSE_MS  = 250;  // the backdrop's wobble and settle
static const unsigned GLASS_SWEEP_MS     = 400;  // the one highlight, after the condense
static const int      GLASS_SWEEP_W      = 80;   // and how wide its band is
static const unsigned GLASS_TOGGLE_MS    = 200;  // a knob's bloom as it lands
static const int      GLASS_BLUR_TAPS    = 9;    // canvas_backdrop_draw takes 1, 5 or 9
static const int      GLASS_BLUR_RADIUS  = 2;
static const double   GLASS_SLAB_MIN     = 0.96; // the slab scales from 96% on open
static const double   GLASS_WOBBLE_AMP   = 6.0;
static const double   GLASS_PILL_STRETCH = 0.0008;
static const double   GLASS_PILL_MAX     = 0.35;
static const double   GLASS_HOVER_GLOW   = 0.5;  // hover is a hint, not a flare
static const double   GLASS_GROUND_A     = 0.45; // how much of an interior fill survives

static const int      SIMPLE_SLIDE_PX    = 8;

static bool glass_style() { return g_cfg.menuanim == ANIM_GLASS; }

// No row acts while the panel is dragged (a clamped move leaves the cursor over a row with the button held, which a
// slider reads as a drag); s_pressSpent also bars a press the glyph or click-away claimed.
static bool row_pressed() { return !s_dragLive && !s_pressSpent && g_ui.pressed; }
static bool row_down()    { return !s_dragLive && !s_pressSpent && g_ui.down; }

// The open field's rect from last frame (the click-away test runs before any row draws).
static UiRect s_entryFieldRect = { 0, 0, 0, 0 };

static unsigned glass_glow(double f) {
  if (f < 0.0) f = 0.0;
  if (f > 1.0) f = 1.0;
  const int a = (int)(((C_GLASS_GLOW >> 24) & 0xFFu) * f * s_pres.open.x + 0.5);
  return canvas_with_alpha(C_GLASS_GLOW, a);
}

// A panel-interior fill: opaque under Off and Simple, a fraction of alpha under Glass (else glass in name only).
static unsigned glass_ground(unsigned c) {
  if (!glass_style()) return c;
  const int a = (int)(((c >> 24) & 0xFFu) * GLASS_GROUND_A + 0.5);
  return canvas_with_alpha(c, a);
}

// A bevel under Off and Simple, a rounded slab under Glass; every canvas_bevel goes through here (one suite property).
static void chrome_box(int x, int y, int w, int h, unsigned fill, unsigned hi,
                       unsigned lo, int r) {
  if (glass_style()) canvas_round_rect(x, y, w, h, r, fill);
  else               canvas_bevel(x, y, w, h, fill, hi, lo);
}

// Body of both switch rows; returns the new value. Too little room draws nothing (ui_row has no floor). A disabled row
// is C_BONEFNT and inert; ON-but-disabled fills C_RAISED so "on, doing nothing" differs from "on and live".
static int row_switch_at(UiRect& cursor, const char* label, int value, unsigned accent,
                         bool enabled, int indent, unsigned labelOn) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const UiRect sw = { r.x + r.w - 42, r.y + 5, 42, 20 };
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;

  const bool hovered = enabled && ui_hit(sw, g_ui.mx, g_ui.my);
  if (g_cfg.menuanim == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }

  // Knob ON fraction: sprung toward the value, reset on the first row after a tab switch; ANIM_OFF skips the spring.
  int knobX;
  if (g_cfg.menuanim == ANIM_OFF) {
    knobX = value ? sw.x + 23 : sw.x + 2;
  } else {
    const double targetX = value ? 1.0 : 0.0;
    if (s_tabSwitchedThisFrame) { s_pres.knob[idx].x = targetX; s_pres.knob[idx].v = 0.0; }
    else anim_spring_step(&s_pres.knob[idx], targetX, ANIM_SPRING_CRITICAL.stiffness,
                          ANIM_SPRING_CRITICAL.damping, s_frameDt);
    knobX = sw.x + 2 + (int)(21.0 * s_pres.knob[idx].x + 0.5);
  }

  // When the switch last flipped (Glass bloom); stamped only when the value moved.
  {
    const int seen = value ? 2 : 1;
    if (!s_tabSwitchedThisFrame && s_pres.toggleSeen[idx] != 0 &&
        s_pres.toggleSeen[idx] != seen)
      s_pres.toggleMs[idx] = s_pres.lastMs;
    s_pres.toggleSeen[idx] = seen;
  }

  if (indent > 0) canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(pres_hover_lighten(enabled ? labelOn : C_BONEFNT, s_pres.hover[idx])), FONT_UI);
  chrome_box(sw.x, sw.y, sw.w, sw.h,
             pres_content_colour(value ? (enabled ? accent : C_RAISED) : C_WELL),
             pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED), GLASS_ROW_R);
  canvas_rect(knobX, sw.y + 2, 17, 16,
              pres_content_colour((g_cfg.menuanim == ANIM_OFF ? value != 0 : s_pres.knob[idx].x >= 0.5)
                                   ? C_BONE : C_RAISED));
  if (glass_style()) {
    const double bloom = s_pres.toggleMs[idx] == 0 ? 0.0
                       : 1.0 - anim_progress(s_pres.toggleMs[idx], s_pres.lastMs, GLASS_TOGGLE_MS);
    if (bloom > 0.0) canvas_glow_rect(knobX - 3, sw.y - 1, 23, 22, glass_glow(bloom));
    if (s_pres.hover[idx] > 0.0)
      canvas_glow_rect(sw.x, sw.y, sw.w, sw.h, glass_glow(s_pres.hover[idx] * GLASS_HOVER_GLOW));
  }
  if (ui_row_accepts_click(enabled, row_pressed(), ui_hit(sw, g_ui.mx, g_ui.my))) {
    s_pressClaimed = true;
    g_cfgDirty = true;
    return !value;
  }
  return value;
}

static int row_switch(UiRect& cursor, const char* label, int value, unsigned accent, bool enabled) {
  return row_switch_at(cursor, label, value, accent, enabled, 0, C_BONE);
}

static int row_switch_sub(UiRect& cursor, const char* label, int value, unsigned accent,
                          bool enabled) {
  return row_switch_at(cursor, label, value, accent, enabled, SUB_INDENT, C_BONEDIM);
}

// VALUE_MARGIN lives in menu.h. FIELD_PAD is the air a bind row's field keeps each side of its text.
const int FIELD_PAD        = 8;
const int TRACK_GAP        = 8;
const int TRACK_X_OFFSET   = 172;  // the track's left edge
const int TRACK_MAX_W      = 120;  // the track's width when the readout is narrow
const int TRACK_MIN_W      = 40;   // a floor so an extreme label cannot collapse it

// The reset button's room: 24 logical pixels between every numeric track and its readout, hovered or not (shortening
// the track on hover would move the knob from under the hand; overlaying would put a click target on a drag target).
const int RESET_W          = 20;
const int RESET_RESERVE    = 24;

// Body of both slider rows. `reserveText` is the widest readout and sizes the track, never the live text (it collapsed
// mid-drag when "141%" grew to "141% max"). `kind` is entry.h's EK_*; EK_NONE rows take no right-click or glyph.
static int row_slider_at(UiRect& cursor, const char* label, int value, int lo, int hi,
                         const char* valueText, const char* reserveText,
                         bool enabled, int indent, unsigned labelOn,
                         int kind, int def) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int key = s_lastRowKey;
  const int valueW = canvas_text_width(valueText, FONT_MONO);
  const int valueX = r.x + r.w - VALUE_MARGIN - valueW;
  const int reserveX = r.x + r.w - VALUE_MARGIN - canvas_text_width(reserveText, FONT_MONO);
  // Fixed TRACK_X_OFFSET so a tab's tracks line up; a wider reserve slides it left so TRACK_MIN_W holds. A numeric row
  // is RESET_RESERVE further short.
  const int resetRoom   = (kind == EK_NONE) ? 0 : RESET_RESERVE;
  const int trackRight  = reserveX - TRACK_GAP - resetRoom;
  const int trackXFixed = r.x + r.w - TRACK_X_OFFSET;
  const int trackXFit   = trackRight - TRACK_MIN_W;
  const int trackX      = trackXFixed < trackXFit ? trackXFixed : trackXFit;
  const UiRect track = { trackX, r.y + 11,
                         clamp_int(trackRight - trackX, TRACK_MIN_W, TRACK_MAX_W),
                         8 };
  const UiRect grab = { track.x - 6, track.y - 8, track.w + 12, track.h + 16 };
  // glyphLive: enabled, a number, cursor on the row, no field open there.
  const UiRect glyph = { trackRight + (RESET_RESERVE - RESET_W) / 2, r.y + 5, RESET_W, RESET_W };
  const bool fieldHere   = s_entry.open != 0 && s_entry.rowKey == key;
  const bool glyphLive   = enabled && kind != EK_NONE && !fieldHere
                           && !s_dragLive && ui_hit(r, g_ui.mx, g_ui.my);
  const bool glyphHot    = glyphLive && ui_hit(glyph, g_ui.mx, g_ui.my);

  // The glyph's click, before the commit slot is read so the reset lands this frame (same one-slot commit as typing).
  if (glyphHot && row_pressed()) {
    s_entryCommit.pending = true;
    s_entryCommit.rowKey = key;
    s_entryCommit.value = def;
    s_pressSpent = true;
    s_pressClaimed = true;
  }

  // Clamped to THIS frame's bounds (the UI scale ceiling moves with panel height); `committed` is compared after the
  // clamp.
  bool committed = false;
  if (s_entryCommit.pending && s_entryCommit.rowKey == key) {
    s_entryCommit.pending = false;
    s_entryCommit.rowKey = -1;
    const int landed = clamp_int(s_entryCommit.value, lo, hi);
    committed = landed != value;
    value = landed;
  }

  if (kind != EK_NONE && enabled && !s_dragLive && g_ui.rpressed
      && ui_hit(r, g_ui.mx, g_ui.my)) {
    char prefill[ENTRY_TEXT_CAP + 1];
    entry_format(prefill, sizeof prefill, value, kind);
    entry_clickaway_commit();
    menu_entry_begin_edit(key, kind, lo, hi, prefill);
  }

  const bool fieldOpen = s_entry.open != 0 && s_entry.rowKey == key;

  if (glyphHot) { s_hoverRowKey = key; s_glyphHint = MENU_RESET_HINT; }

  const int kx = ui_slider_x(track, value, lo, hi);

  const bool hovered = enabled && ui_hit(grab, g_ui.mx, g_ui.my);
  if (g_cfg.menuanim == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }
  if (g_cfg.menuanim == ANIM_OFF) {
    s_pres.resetHover[idx] = glyphHot ? 1.0 : 0.0;
  } else {
    const double rate = glyphHot ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double rv = s_pres.resetHover[idx] + rate * s_frameDt;
    if (rv < 0.0) rv = 0.0; else if (rv > 1.0) rv = 1.0;
    s_pres.resetHover[idx] = rv;
  }

  int drawnKx;
  if (g_cfg.menuanim == ANIM_OFF) {
    drawnKx = kx;
  } else {
    const double targetFrac = track.w > 0 ? (double)(kx - track.x) / (double)track.w : 0.0;
    const AnimSpringParams sp = glass_style() ? ANIM_SPRING_GLASS : ANIM_SPRING_CRITICAL;
    if (s_tabSwitchedThisFrame) { s_pres.fill[idx].x = targetFrac; s_pres.fill[idx].v = 0.0; }
    else anim_spring_step(&s_pres.fill[idx], targetFrac, sp.stiffness, sp.damping, s_frameDt);
    drawnKx = track.x + (int)(s_pres.fill[idx].x * track.w + 0.5);
    if (drawnKx < track.x) drawnKx = track.x;
    if (drawnKx > track.x + track.w) drawnKx = track.x + track.w;
  }

  if (indent > 0) canvas_rect(r.x + SUB_RULE_X, r.y, 1, r.h, pres_content_colour(C_RAISED));
  canvas_text(r.x + indent, r.y + 6, label,
              pres_content_colour(pres_hover_lighten(enabled ? labelOn : C_BONEFNT, s_pres.hover[idx])), FONT_UI);
  chrome_box(track.x, track.y, track.w, track.h, pres_content_colour(C_WELL),
             pres_content_colour(C_BEVELLO), pres_content_colour(C_RAISED), GLASS_ROW_R);
  canvas_rect(track.x, track.y, drawnKx - track.x, track.h, pres_content_colour(C_HAZARDDIM));
  canvas_rect(drawnKx - 4, track.y - 4, 9, 16, pres_content_colour(enabled ? C_HAZARD : C_BONEFNT));
  if (fieldOpen) {
    // The hotkey field's shape (rounded well, 4px C_HAZARD rim), sized for the widest text it can hold ("-2147483648").
    const int fieldW = canvas_text_width("-2147483648", FONT_MONO) + 2 * FIELD_PAD;
    const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };
    s_entryFieldRect = field;
    canvas_round_rect(field.x, field.y, field.w, field.h, 4,
                      pres_content_colour(C_WELL));
    canvas_round_rect_outline(field.x, field.y, field.w, field.h, 4, 1,
                              pres_content_colour(C_HAZARD));
    const Glyph& capA  = font_glyphs[FONT_MONO]['A' - 0x20];
    const Glyph& descY = font_glyphs[FONT_MONO]['y' - 0x20];
    const int inkTop = capA.by;
    const int inkH   = (descY.by + descY.h) - inkTop;
    const int textY  = field.y + (field.h - inkH) / 2 - inkTop;
    const int textX  = field.x + FIELD_PAD;
    // FONT_MONO has one advance per character: the boundary before glyph i is i advances in (entry_index_at inverts it).
    const int advance = canvas_text_width("0", FONT_MONO);
    // A left press in the well puts the caret; holding drags the selection's far end. Not row_pressed()/row_down(): the
    // s_pressSpent this press sets filters those.
    if (g_ui.pressed && ui_hit(field, g_ui.mx, g_ui.my)) {
      s_entry = entry_step(s_entry, ENTRY_EV_SET_CARET,
                           entry_index_at(s_entry.len, textX, g_ui.mx, advance),
                           0, EK_NONE, 0, 0, 0).state;
      s_entryCaretDrag = true;
    } else if (s_entryCaretDrag && g_ui.down) {
      s_entry = entry_step(s_entry, ENTRY_EV_SET_CARET,
                           entry_index_at(s_entry.len, textX, g_ui.mx, advance),
                           1, EK_NONE, 0, 0, 0).state;
    }
    // Selection under the text, in C_HAZARDDIM so selected glyphs stay readable in C_BONE.
    const int selA = entry_sel_lo(s_entry), selB = entry_sel_hi(s_entry);
    if (selB > selA)
      canvas_rect(textX + selA * advance, field.y + 4, (selB - selA) * advance,
                  field.h - 8, pres_content_colour(C_HAZARDDIM));
    canvas_text(textX, textY, s_entry.text, pres_content_colour(C_BONE), FONT_MONO);
    const bool caretOn = (g_cfg.menuanim == ANIM_OFF) || ((menu_now() / 500u) % 2u == 0u);
    if (caretOn)
      canvas_rect(textX + s_entry.caret * advance + 1, field.y + 4,
                  1, field.h - 8, pres_content_colour(C_BONE));
  } else {
    canvas_text(valueX, r.y + 7, valueText, pres_content_colour(C_BONE), FONT_MONO);
    // The reset button: a well with a circular arrow. Two open rings (radii 6 and 5, 2px stroke) of eight points joined
    // by seven segments (the suite pins that neither ring closes), plus an arrowhead: a 3x3 block and one barb line.
    if (glyphLive) {
      const double rf = s_pres.resetHover[idx];
      canvas_round_rect(glyph.x, glyph.y, glyph.w, glyph.h, 4,
                        pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, rf)));
      canvas_round_rect_outline(glyph.x, glyph.y, glyph.w, glyph.h, 4, 1,
                                pres_content_colour(pres_hover_toward(C_RAISED, C_BONEDIM, rf)));
      static const int RING6[8][2] = {
        { 16, 10 }, { 15, 6 }, { 11, 4 }, { 7, 5 }, { 4, 8 },
        { 4, 12 }, { 7, 15 }, { 11, 16 }
      };
      static const int RING5[8][2] = {
        { 15, 10 }, { 14, 7 }, { 11, 5 }, { 7, 6 }, { 5, 8 },
        { 5, 12 }, { 7, 14 }, { 11, 15 }
      };
      // Runs back across the opening so it reads as an arrow travelling round, not a spoke.
      static const int BARB[2] = { 13, 13 };
      const int ix = glyph.x - 1, iy = glyph.y;
      const unsigned gc = pres_content_colour(glyphHot ? C_BONE : C_BONEDIM);
      for (int i = 0; i < 7; i++) {
        canvas_line(ix + RING6[i][0], iy + RING6[i][1],
                    ix + RING6[i + 1][0], iy + RING6[i + 1][1], gc);
        canvas_line(ix + RING5[i][0], iy + RING5[i][1],
                    ix + RING5[i + 1][0], iy + RING5[i + 1][1], gc);
      }
      canvas_rect(ix + RING6[0][0] - 1, iy + RING6[0][1] - 1, 3, 3, gc);
      canvas_line(ix + RING6[0][0], iy + RING6[0][1],
                  ix + BARB[0], iy + BARB[1], gc);
    }
  }
  if (glass_style() && s_pres.hover[idx] > 0.0)
    canvas_glow_rect(track.x, track.y - 4, track.w, track.h + 8,
                     glass_glow(s_pres.hover[idx] * GLASS_HOVER_GLOW));
  // The held button, not the press edge (a slider is dragged); the logical track, never the drawn one. Not while a field
  // is open on this row.
  if (!fieldOpen &&
      ui_row_accepts_click(enabled, row_down(), ui_hit(grab, g_ui.mx, g_ui.my))) {
    // This track's press, claimed every frame; not s_pressSpent (it spans the hold, so setting it would end the drag).
    s_pressClaimed = true;
    const int v = ui_slider_value(track, g_ui.mx, lo, hi);
    if (v != value) { g_cfgDirty = true; return v; }
  }
  if (committed) g_cfgDirty = true;
  return value;
}

// A top-level slider. EK_INT because "%d%s" is what this formats (metre and thousandths rows use their own kind).
static int row_slider(UiRect& cursor, const char* label, int value, int lo, int hi,
                      const char* suffix, bool enabled, int def) {
  char buf[32], reserve[32];
  sprintf_s(buf, "%d%s", value, suffix);
  sprintf_s(reserve, "%d%s", hi, suffix);
  return row_slider_at(cursor, label, value, lo, hi, buf, reserve, enabled, 0, C_BONE,
                       EK_INT, def);
}

static int row_slider_sub(UiRect& cursor, const char* label, int value, int lo, int hi,
                          const char* suffix, bool enabled, int def) {
  char buf[32], reserve[32];
  sprintf_s(buf, "%d%s", value, suffix);
  sprintf_s(reserve, "%d%s", hi, suffix);   // widest readout; see row_slider
  return row_slider_at(cursor, label, value, lo, hi, buf, reserve, enabled,
                       SUB_INDENT, C_BONEDIM, EK_INT, def);
}

static int row_slider_named(UiRect& cursor, const char* label, int value, int lo, int hi,
                            const char* (*labelFn)(int), bool enabled) {
  const char* reserve = labelFn(lo);
  int reserveW = canvas_text_width(reserve, FONT_MONO);
  for (int v = lo + 1; v <= hi; v++) {
    const char* s = labelFn(v);
    const int w = canvas_text_width(s, FONT_MONO);
    if (w > reserveW) { reserve = s; reserveW = w; }
  }
  return row_slider_at(cursor, label, value, lo, hi, labelFn(value), reserve,
                       enabled, 0, C_BONE, EK_NONE, 0);
}

static int row_slider_named_sub(UiRect& cursor, const char* label, int value, int lo, int hi,
                                const char* (*labelFn)(int), bool enabled) {
  const char* reserve = labelFn(lo);
  int reserveW = canvas_text_width(reserve, FONT_MONO);
  for (int v = lo + 1; v <= hi; v++) {
    const char* s = labelFn(v);
    const int w = canvas_text_width(s, FONT_MONO);
    if (w > reserveW) { reserve = s; reserveW = w; }
  }
  return row_slider_at(cursor, label, value, lo, hi, labelFn(value), reserve,
                       enabled, SUB_INDENT, C_BONEDIM, EK_NONE, 0);
}

// A dropdown row (seven named stops read as a list, not a slider). Mouse only. The open list is deferred to
// menu_dropdown_draw: the canvas has no z-order, so a list drawn in the row would be painted over by the rows below.
struct MenuDropdown {
  int  open;            // int, not bool: MSVC 19.50 /O2 folds byte-sized bools (see tooltip.h)
  int  rowKey;
  UiRect box;           // the closed row's own value box, last frame
  int  count;
  const char* (*labelFn)(int);
};
static MenuDropdown s_dd = { 0, -1, { 0, 0, 0, 0 }, 0, 0 };

static const int DD_CHEVRON_W = 14;   // room for the chevron inside the box
static const int DD_PAD       = 6;

// One closed dropdown. Takes the open list's committed value from the shared slot, as the typed field and reset glyph do.
static int row_dropdown(UiRect& cursor, const char* label, int value, int count,
                        const char* (*labelFn)(int), bool enabled) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; return value; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  const int key = s_lastRowKey;

  int nameW = 0;
  for (int v = 0; v < count; v++) {
    const int w = canvas_text_width(labelFn(v), FONT_MONO);
    if (w > nameW) nameW = w;
  }
  const int boxW = nameW + DD_PAD * 2 + DD_CHEVRON_W;
  const UiRect box = { r.x + r.w - VALUE_MARGIN - boxW, r.y + 4, boxW, ROW_H - 8 };

  const bool hot = enabled && ui_hit(box, g_ui.mx, g_ui.my);
  const bool mine = s_dd.open != 0 && s_dd.rowKey == key;
  if (hot && row_pressed()) {
    s_pressSpent = true;
    if (mine) { s_dd.open = 0; s_dd.rowKey = -1; }
    else { s_dd.open = 1; s_dd.rowKey = key; s_dd.box = box; s_dd.count = count;
           s_dd.labelFn = labelFn; }
  } else if (mine) {
    s_dd.box = box;              // follow the row if the panel was dragged
  }

  const unsigned fill = enabled ? (hot || mine ? C_RAISED : C_WELL) : C_WELL;
  chrome_box(box.x, box.y, box.w, box.h, pres_content_colour(fill),
             pres_content_colour(C_BEVELHI), pres_content_colour(C_BEVELLO), 4);
  canvas_text(r.x, r.y + 8, label, pres_colour(enabled ? C_BONE : C_BONEFNT), FONT_UI);
  canvas_text(box.x + DD_PAD, box.y + 4, labelFn(clamp_int(value, 0, count - 1)),
              pres_colour(enabled ? C_BONE : C_BONEFNT), FONT_MONO);
  {
    const int cx = box.x + box.w - DD_CHEVRON_W / 2 - DD_PAD / 2;
    const int cy = box.y + box.h / 2;
    const unsigned cc = pres_colour(enabled ? C_BONEDIM : C_BONEFNT);
    const int dy = mine ? -3 : 3;
    canvas_line(cx - 4, cy - dy / 2, cx, cy + dy, cc);
    canvas_line(cx, cy + dy, cx + 4, cy - dy / 2, cc);
  }

  if (s_entryCommit.pending && s_entryCommit.rowKey == key) {
    const int v = clamp_int(s_entryCommit.value, 0, count - 1);
    s_entryCommit.pending = false;
    s_entryCommit.rowKey = -1;
    return v;
  }
  return value;
}

static void menu_dropdown_draw() {
  if (!s_dd.open || !s_dd.labelFn || s_dd.count <= 0) return;
  const int itemH = ROW_H - 6;
  const UiRect list = { s_dd.box.x, s_dd.box.y + s_dd.box.h + 2,
                        s_dd.box.w, itemH * s_dd.count + 4 };
  chrome_box(list.x, list.y, list.w, list.h, pres_content_colour(C_PANEL),
             pres_content_colour(C_BEVELHI), pres_content_colour(C_BEVELLO), 4);
  bool onAnItem = false;
  for (int i = 0; i < s_dd.count; i++) {
    const UiRect it = { list.x + 2, list.y + 2 + i * itemH, list.w - 4, itemH };
    const bool hot = ui_hit(it, g_ui.mx, g_ui.my);
    if (hot) {
      onAnItem = true;
      canvas_rect(it.x, it.y, it.w, it.h, pres_content_colour(C_RAISED));
    }
    canvas_text(it.x + DD_PAD - 2, it.y + 3, s_dd.labelFn(i),
                pres_colour(hot ? C_BONE : C_BONEDIM), FONT_MONO);
    if (hot && row_pressed()) {
      s_pressSpent = true;
      s_entryCommit.pending = true;
      s_entryCommit.rowKey = s_dd.rowKey;
      s_entryCommit.value = i;
      s_dd.open = 0; s_dd.rowKey = -1;
      return;
    }
  }
  // Click away closes without changing anything; the box is excluded (its own row toggles on that press).
  if (!onAnItem && row_pressed() && !ui_hit(s_dd.box, g_ui.mx, g_ui.my)) {
    s_pressSpent = true;
    s_dd.open = 0; s_dd.rowKey = -1;
  }
}

// A group heading: uppercase (no text-transform in this renderer), letter-spaced, bone-faint.
static void draw_group(UiRect& cursor, const char* heading) {
  if (cursor.h < GROUP_H) return;
  const UiRect r = ui_row(cursor, GROUP_H, GROUP_GAP);
  canvas_text(r.x, r.y + 5, heading, pres_content_colour(C_BONEFNT), FONT_SMALL);
}

// A line of text that is a row of its own: its one caller, MENU_GAMECFG_NOTE, is a state that must show unhovered, so
// it cannot be a tooltip.
static void draw_placeholder(UiRect& cursor, const char* text) {
  if (cursor.h < ROW_H) return;
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  canvas_text(r.x, r.y + 6, text, pres_content_colour(C_BONEFNT), FONT_SMALL);
}

// A binding row: a click-taking field (a well with a rim), not a slider. The click arms; the next key or button binds
// at hooked_poll. Always live: the feature's own switch decides whether the key acts.
static void row_bind(UiRect& cursor, const char* label, int row) {
  if (cursor.h < ROW_H) { s_lastRowKey = -1; return; }
  const UiRect r = ui_row(cursor, ROW_H, ROW_GAP);
  const int idx = s_pres.rowsThisFrame++ % ANIM_ROW_SLOTS;
  s_lastRow = r; s_lastRowKey = s_tab * 1000 + idx;
  // Sized for the widest thing it can show (the prompt, the invitation and the widest key name), so it never moves
  // under the hand.
  int fieldW = canvas_text_width(MENU_KEYCAP_PROMPT, FONT_MONO);
  {
    const int hoverW = canvas_text_width(MENU_KEYCAP_HOVER, FONT_MONO);
    const int nameW  = canvas_text_width(BIND_LABEL_WIDEST, FONT_MONO);
    if (hoverW > fieldW) fieldW = hoverW;
    if (nameW  > fieldW) fieldW = nameW;
  }
  fieldW += 2 * FIELD_PAD;
  const UiRect field = { r.x + r.w - fieldW, r.y + 5, fieldW, 20 };

  const bool hovered = ui_hit(field, g_ui.mx, g_ui.my);
  if (g_cfg.menuanim == ANIM_OFF) {
    s_pres.hover[idx] = hovered ? 1.0 : 0.0;
  } else {
    const double rate = hovered ? (1.0 / ANIM_HOVER_IN_MS) : (-1.0 / ANIM_HOVER_OUT_MS);
    double hv = s_pres.hover[idx] + rate * s_frameDt;
    if (hv < 0.0) hv = 0.0; else if (hv > 1.0) hv = 1.0;
    s_pres.hover[idx] = hv;
  }

  canvas_text(r.x, r.y + 6, label, pres_content_colour(C_BONE), FONT_UI);
  const bool armed = s_keycap.phase == KEYCAP_ARMED && s_keycap.row == row;
  const int vk = config_bind_get(g_cfg, row);
  // Hover lifts the well, not the text: fill C_WELL -> C_RAISED, rim C_RAISED -> C_BONEDIM; HAZARD rim in capture.
  canvas_round_rect(field.x, field.y, field.w, field.h, 4,
                    pres_content_colour(pres_hover_toward(C_WELL, C_RAISED, s_pres.hover[idx])));
  canvas_round_rect_outline(field.x, field.y, field.w, field.h, 4, 1,
                            pres_content_colour(armed ? C_HAZARD
                                                      : pres_hover_toward(C_RAISED, C_BONEDIM, s_pres.hover[idx])));
  const char* value = bind_field_text(armed, hovered, vk,
                                      MENU_KEYCAP_PROMPT, MENU_KEYCAP_HOVER);
  const int valueW = canvas_text_width(value, FONT_MONO);
  // Centre the ink band (cap height to deepest descender), not the pen position, so the descender stays off the rim.
  const Glyph& capA  = font_glyphs[FONT_MONO]['A' - 0x20];
  const Glyph& descY = font_glyphs[FONT_MONO]['y' - 0x20];
  const int inkTop = capA.by;
  const int inkH   = (descY.by + descY.h) - inkTop;
  const int textY  = field.y + (field.h - inkH) / 2 - inkTop;
  canvas_text(field.x + (field.w - valueW) / 2, textY, value,
              pres_content_colour(armed ? C_HAZARD : C_BONE), FONT_MONO);
  if (ui_row_accepts_click(true, row_pressed(), ui_hit(r, g_ui.mx, g_ui.my))) {
    KeyCapEvent e; e.kind = KEYCAP_EV_CLICK; e.row = row; e.vk = 0;
    s_keycap = keycap_step(s_keycap, e).state;
    s_pressClaimed = true;
  }
  if (gamecfg_key_bound(s_gameCfg, vk))
    draw_placeholder(cursor, MENU_GAMECFG_NOTE);
}

void menu_draw(int screenW, int screenH) {
  // Presentation preamble, once per frame; row ordinals reset before any row call.
  s_pres.rowsThisFrame = 0;
  s_hoverRowKey = -1;
  s_hoverHint = nullptr;
  s_lastRowKey = -1;
  s_glyphHint = nullptr;
  if (!g_ui.down) s_pressSpent = false;
  if (!g_ui.down) s_entryCaretDrag = false;
  s_pressClaimed = false;
  {
    const unsigned now = menu_now();
    s_frameDt = anim_dt_clamped(s_pres.lastMs, now);
    s_pres.lastMs = now;
    // input_menu_open() is the logical state; the open spring only chases it, so menu_closing_animation_active() can
    // ask whether the fade finished.
    s_pres.openTarget = input_menu_open() ? 1 : 0;
    if (g_cfg.menuanim == ANIM_OFF) {
      s_pres.open.x = (double)s_pres.openTarget;
      s_pres.open.v = 0.0;
    } else {
      anim_spring_step(&s_pres.open, (double)s_pres.openTarget,
                       ANIM_SPRING_CRITICAL.stiffness, ANIM_SPRING_CRITICAL.damping, s_frameDt);
    }
    // Edge on openTarget so the sweep runs once per open; stamped in every style so switching to Glass mid-open does not
    // replay an unwatched opening.
    if (s_pres.openTarget && !s_pres.wasOpen) s_pres.openStartMs = now;
    s_pres.wasOpen = s_pres.openTarget;
  }
  // Below is logical pixels; the canvas converts. The factor must be back to UI_SCALE_DEFAULT on return (the ESP shares
  // the canvas). `pct` is `settled` held at its pre-drag value while the button is down.
  const int settled = menu_applied_scale(screenH);
  const int pct = ui_scale_effective(settled, s_scaleHeld, g_ui.down);
  if (!g_ui.down) s_scaleHeld = settled;
  canvas_set_scale(pct);
  // The cursor arrives in DEVICE pixels and hit tests are logical: converted once here, since one miss at a call site is
  // a row that draws right and cannot be clicked.
  const int deviceMx = g_ui.mx, deviceMy = g_ui.my;
  g_ui.mx = ui_unscale_px(deviceMx, pct);
  g_ui.my = ui_unscale_px(deviceMy, pct);
  // Laid out against the LOGICAL screen, which keeps the panel anchored at every scale.
  const int logicalW = ui_unscale_px(screenW, pct);
  const int logicalH = ui_unscale_px(screenH, pct);

  // Click away, once, before any row: a press of either button outside the open field's well finishes it (it may land
  // on an earlier row or nothing). A right press opening a field elsewhere reads `g_ui.rpressed` raw. Runs after the
  // cursor is logical and before the drag machine takes the press.
  if (s_entry.open && (g_ui.pressed || g_ui.rpressed) && !s_dragLive) {
    if (g_ui.pressed) { s_pressClaimed = true; s_pressSpent = true; }
    if (!ui_hit(s_entryFieldRect, g_ui.mx, g_ui.my)) entry_clickaway_commit();
  }

  // The drag, before anything is drawn: the box is the four ini keys through panel_layout, overridden by the live box
  // during a move or resize. The title band is tested here so the grip wins.
  const PanelBox laid = panel_layout(panel_stored_box(), logicalW, logicalH);
  PanelBox box = laid;
  bool gripHit = false;
  {
    const bool titleHit = ui_hit(panel_title_rect(laid, STRIP_H, STATUS_H), g_ui.mx, g_ui.my);
    // Tested against the box the grip is drawn at, not `laid`: they diverge during a live drag.
    gripHit = ui_hit(panel_grip_rect(s_dragLive ? s_dragBox : laid), g_ui.mx, g_ui.my);
    bool ended = false;
    s_drag = panel_drag_step(s_drag, g_ui.pressed, g_ui.down, titleHit, gripHit,
                             g_ui.mx, g_ui.my, laid, logicalW, logicalH, &box, &ended);
    s_dragLive = s_drag.phase != PDRAG_IDLE;
    if (ended) {
      // The box the frame before drew: with the button gone there is no mouse to re-derive it from.
      box = s_dragBox;
      // A click that moved nothing stores nothing (else a stray click freezes a default-positioned panel).
      if (box.x != laid.x || box.y != laid.y || box.w != laid.w || box.h != laid.h) {
        g_cfg.panelx = box.x; g_cfg.panely = box.y;
        g_cfg.panelw = box.w; g_cfg.panelh = box.h;
        g_cfgDirty = true;
      }
    } else if (s_dragLive) {
      s_dragBox = box;
    }
  }
  const UiRect p = { box.x, box.y, box.w, box.h };

  // What is on for the hazard strip: parent switches only (menu_live_count), each accuracy switch masked by its pair's
  // latch (a fingerprint mismatch on Chaingun or Shotgun latches both off together).
  const int liveCount = menu_live_count(g_cfg.fovon, g_cfg.espenemies,
                                        g_cfg.espitems, g_cfg.espsecrets,
                                        g_cfg.espexplodables, g_cfg.espswitches,
                                        g_cfg.aimon, g_cfg.silenton,
                                        g_cfg.freecamon,
                                        accuracy_row_live(g_cfg.accpistol,   accuracy_pair_latched(ACC_PAIR_PISTOL))   ? 1 : 0,
                                        accuracy_row_live(g_cfg.accchaingun, accuracy_pair_latched(ACC_PAIR_HITSCAN))  ? 1 : 0,
                                        accuracy_row_live(g_cfg.accshotgun,  accuracy_pair_latched(ACC_PAIR_HITSCAN))  ? 1 : 0,
                                        accuracy_row_live(g_cfg.accexpander, accuracy_pair_latched(ACC_PAIR_EXPANDER)) ? 1 : 0,
                                        g_cfg.dmgnumbers, g_cfg.secrettriggers, g_cfg.tboton,
                                        g_cfg.espbreakables);
  const unsigned strip = liveCount ? C_RED : C_HAZARD;

  // The whole panel moves as one unit: one canvas-wide transform (canvas.h) every primitive applies on the way out, so
  // `p` and `cursor` stay put and ui_hit tests the logical layout. Not put back: canvas_begin clears it each frame, and
  // leaving it set lets the suite read it back.
  if (g_cfg.menuanim == ANIM_SIMPLE) {
    const int slideY = (int)((1.0 - s_pres.open.x) * (double)SIMPLE_SLIDE_PX + 0.5);
    canvas_set_transform(0, -slideY, 1.0, 0, 0);
  } else if (glass_style()) {
    // The same factor condenses the whole panel about its centre, rows included.
    canvas_set_transform(0, 0, GLASS_SLAB_MIN + (1.0 - GLASS_SLAB_MIN) * s_pres.open.x,
                         p.x + p.w / 2, p.y + p.h / 2);
  } else {
    canvas_set_transform(0, 0, 1.0, 0, 0);
  }

  if (glass_style()) {
    // The slab, on the logical rect (the condense is the transform's). `openP` is how far into the condense this is: the
    // wobble damps to nothing across it and the sweep starts at its end.
    const double openP = anim_progress(s_pres.openStartMs, s_pres.lastMs, GLASS_CONDENSE_MS);
    // The capture comes first: it reads the back buffer, so anything the panel drew earlier would be blurred back onto
    // the slab. Its return is not branched on (a failed capture draws the tint alone).
    canvas_backdrop_capture(p.x, p.y, p.w, p.h);
    canvas_backdrop_draw(p.x, p.y, p.w, p.h, GLASS_R, pres_colour(C_GLASS_TINT),
                         GLASS_WOBBLE_AMP * (1.0 - openP), openP,
                         GLASS_BLUR_TAPS, GLASS_BLUR_RADIUS);
    canvas_round_rect_outline(p.x, p.y, p.w, p.h, GLASS_R, 1, pres_colour(C_GLASS_RIM));
    canvas_gradient_rect(p.x, p.y, p.w, GLASS_TOP_H, pres_colour(C_GLASS_TOP),
                         canvas_with_alpha(C_GLASS_TOP, 0));
    // One highlight per open, after the condense: a band sweeping left to right, clipped to the slab, timed from
    // openStartMs so it cannot restart while open.
    const unsigned age = s_pres.lastMs - s_pres.openStartMs;
    if (s_pres.openTarget && age >= GLASS_CONDENSE_MS &&
        age < GLASS_CONDENSE_MS + GLASS_SWEEP_MS) {
      const double sp = anim_progress(s_pres.openStartMs + GLASS_CONDENSE_MS,
                                      s_pres.lastMs, GLASS_SWEEP_MS);
      int bx = p.x - GLASS_SWEEP_W + (int)((p.w + GLASS_SWEEP_W) * sp + 0.5);
      int bw = GLASS_SWEEP_W;
      if (bx < p.x) { bw -= (p.x - bx); bx = p.x; }
      if (bx + bw > p.x + p.w) bw = p.x + p.w - bx;
      if (bw > 0) canvas_glow_rect(bx, p.y, bw, p.h, glass_glow(1.0 - sp));
    }
    canvas_hazard(p.x + 1, p.y + 1, p.w - 2, STRIP_H, pres_colour(strip), pres_colour(C_PANEL), 9);
  } else {
    canvas_bevel(p.x, p.y, p.w, p.h,
                pres_colour(C_PANEL), pres_colour(C_BEVELHI), pres_colour(C_BEVELLO));
    canvas_hazard(p.x + 1, p.y + 1, p.w - 2, STRIP_H, pres_colour(strip), pres_colour(C_PANEL), 9);
  }

  const int sy = p.y + 1 + STRIP_H;
  canvas_rect(p.x + 1, sy, p.w - 2, STATUS_H, pres_colour(glass_ground(C_RECESS)));
  canvas_rect(p.x + PAD, sy + 11, 9, 9, pres_colour(strip));
  char status[32];
  if (liveCount) sprintf_s(status, "%d cheat%s live", liveCount, liveCount == 1 ? "" : "s");
  else           sprintf_s(status, "Idle");
  canvas_text(p.x + PAD + 18, sy + 7, status, pres_colour(C_BONEDIM), FONT_SMALL);
  canvas_text(p.x + p.w - 100, sy + 7, "Ins closes", pres_colour(C_BONEFNT), FONT_SMALL);

  const int bodyY = sy + STATUS_H;
  const int bodyH = p.y + p.h - bodyY - 1;

  canvas_rect(p.x + 1, bodyY, RAIL_W, bodyH, pres_colour(glass_ground(C_WELL)));
  // The Glass pill springs along the rail's vertical axis (tabs share x and width): pillX carries Y, pillW the height,
  // squashing via anim_pill_stretch. Drawn before the loop so labels sit on top.
  if (glass_style()) {
    const double ty = (double)(bodyY + s_tab * 46), th = 46.0;
    if (s_pres.pillW.x <= 0.0) {
      s_pres.pillX.x = ty; s_pres.pillX.v = 0.0;
      s_pres.pillW.x = th; s_pres.pillW.v = 0.0;
    } else {
      anim_spring_step(&s_pres.pillX, ty, ANIM_SPRING_GLASS.stiffness,
                       ANIM_SPRING_GLASS.damping, s_frameDt);
      anim_spring_step(&s_pres.pillW, th, ANIM_SPRING_GLASS.stiffness,
                       ANIM_SPRING_GLASS.damping, s_frameDt);
    }
    const AnimPill pill = anim_pill_stretch(s_pres.pillX.x, s_pres.pillW.x,
                                            s_pres.pillX.v, GLASS_PILL_STRETCH, GLASS_PILL_MAX);
    canvas_round_rect(p.x + 1, (int)(pill.x + 0.5), RAIL_W, (int)(pill.w + 0.5),
                      GLASS_ROW_R, pres_colour(C_GLASS_TINT));
  }
  // The rail click is applied after the content: it is also a click away from an open field, whose commit is stored
  // under its own tab, and switching first would leave no row owning that key. menu_set_tab runs at the foot.
  int railTab = -1;
  for (int i = 0; i < TAB_COUNT; i++) {
    const UiRect t = { p.x + 1, bodyY + i * 46, RAIL_W, 46 };
    if (i == s_tab) {
      if (!glass_style()) canvas_rect(t.x, t.y, t.w, t.h, pres_colour(C_PANEL));
      canvas_rect(t.x, t.y, 3, t.h, pres_colour(TAB_COLOURS[i]));
    }
    // Centred in the 46px entry (canvas_text takes the top of the line box).
    const int nameY = t.y + (t.h - font_height[FONT_UI]) / 2;
    canvas_text(t.x + RAIL_LABEL_X, nameY, TAB_NAMES[i], pres_colour(i == s_tab ? C_BONE : C_BONEDIM), FONT_UI);
    if (g_ui.pressed && ui_hit(t, g_ui.mx, g_ui.my)) {
      railTab = i;
      s_pressClaimed = true;
    }
  }

  // A tab switch snaps every row's spring on its first frame of content and starts the crossfade. A rail click's switch
  // is applied at the foot, so this is the frame after the click.
  s_tabSwitchedThisFrame = (s_tab != s_pres.tabFrom);
  if (s_tabSwitchedThisFrame) s_pres.tabSwitchMs = s_pres.lastMs;
  s_pres.tabFrom = s_tab;

  UiRect cursor = { p.x + 1 + RAIL_W + PAD, bodyY + PAD, p.w - RAIL_W - 2 * PAD - 2, bodyH - 2 * PAD };
  switch (s_tab) {
    case TAB_VIEW: {
      g_cfg.fovon = row_switch(cursor, "Custom field of view", g_cfg.fovon, C_GREEN, true);
      g_cfg.fov   = row_slider(cursor, "Field of view", g_cfg.fov, FOV_MIN, FOV_MAX, "",
                               g_cfg.fovon != 0, FOV_DEFAULT);
      char defaultMsg[48];
      sprintf_s(defaultMsg, "Updates as you drag. Default is %d degrees.", FOV_DEFAULT);
      row_hint(defaultMsg);

      draw_group(cursor, "FREE CAMERA");
      // The switch gates the toggle key; it does not put the camera in the air.
      g_cfg.freecamon = row_switch(cursor, "Free camera", g_cfg.freecamon, C_GREEN, true);
      const bool freecamOn = combat_sub_enabled(g_cfg.freecamon);
      g_cfg.freecamspeed = row_slider_sub(cursor, "Speed", g_cfg.freecamspeed,
                                          FREECAM_SPEED_MIN, FREECAM_SPEED_MAX, "%",
                                          freecamOn, FREECAM_SPEED_DEFAULT);
      g_cfg.freecamshowplayer = row_switch_sub(cursor, "Show player",
                                               g_cfg.freecamshowplayer, C_GREEN, freecamOn);
      // Latched unavailable for the session: the switch stays live and still writes g_cfg.
      g_cfg.freecamnocull = row_switch_sub(cursor,
          nocull_row_unavailable() ? "Disable culling (Unavailable)" : "Disable culling",
          g_cfg.freecamnocull, C_GREEN, freecamOn);
      // Measured on the shipped build at a 175 fps cap: about 170 with every row on, worst case high 80s in open rooms.
      row_hint("Every sector, walls included. Near free; dips in the open.");
      g_cfg.freecamdebug = row_switch_sub(cursor, "Debug readout", g_cfg.freecamdebug,
                                          C_GREEN, freecamOn);
      row_hint("Camera pose and paint counts while flying.");
      // Defaults on: else the camera exits in the wrong half of a teleporter or water pair.
      g_cfg.freecamtransport = row_switch_sub(cursor, "Follow teleporters",
                                              g_cfg.freecamtransport, C_GREEN, freecamOn);
      row_hint("Water and teleporters carry the camera, as they do the player.");
      // The teleport key's destination, shown before the key is pressed; nothing draws when the aim has nowhere to land.
      g_cfg.freecamteleportmarker = row_switch_sub(cursor, "Teleport marker",
                                                   g_cfg.freecamteleportmarker, C_GREEN, freecamOn);
      row_hint("Shows where the teleport key will drop you.");
      g_cfg.freecamfog = row_switch_sub(cursor, "Remove fog", g_cfg.freecamfog, C_GREEN, freecamOn);
      row_hint("Distance fog off while flying.");
      g_cfg.freecamfullbright = row_switch_sub(cursor,
          light_row_unavailable() ? "Fullbright (Unavailable)" : "Fullbright",
          g_cfg.freecamfullbright, C_GREEN, freecamOn);
      row_hint("No shading or fog while flying.");
      g_cfg.freecamshot = row_switch_sub(cursor, unported_shot() ? "Screenshot mode (Unavailable)" : "Screenshot mode", g_cfg.freecamshot, C_GREEN, freecamOn);
      row_hint("Hides weapon, crosshair, HUD and markers while flying.");
      break;
    }
    case TAB_SETTINGS: {
      // The master reads as "the game may change my weapon at all"; "Switch to" only matters when the ammo-empty event
      // may fire.
      draw_group(cursor, "WEAPON SWITCHING");
      g_cfg.wswitchauto = row_switch(cursor, "Automatic weapon switch",
                                     g_cfg.wswitchauto, C_GREEN, true);
      g_cfg.wswitchonpickup = row_switch_sub(cursor, "On new pickup",
                                             g_cfg.wswitchonpickup, C_GREEN,
                                             g_cfg.wswitchauto != 0);
      g_cfg.wswitchonempty = row_switch_sub(cursor, "On empty ammo",
                                            g_cfg.wswitchonempty, C_GREEN,
                                            g_cfg.wswitchauto != 0);
      {
        // The preference goes to the FRONT of the engine's list (an empty choice falls through to the next with ammo,
        // finally the Mighty Foot). Written on CHANGE: it is engine memory.
        const int before = g_cfg.wswitchprefer;
        g_cfg.wswitchprefer =
            row_dropdown(cursor, "Switch to", g_cfg.wswitchprefer,
                         AUTOSWITCH_PREFER_MAX + 1, autoswitch_prefer_name,
                         g_cfg.wswitchauto != 0 && g_cfg.wswitchonempty != 0);
        if (g_cfg.wswitchprefer != before) autoswitch_apply_preference();
      }
      row_hint("The game itself lost this option; the mod supplies it");

      // Input configuration: Duke's config keeps five mouse bindings and stores nothing past index 4 (mousebind.h has
      // the parser's gate and the bits).
      draw_group(cursor, "MOUSE BUTTONS");
      g_cfg.mouse4action = row_dropdown(cursor, "Mouse 4 does", g_cfg.mouse4action,
                                        MB_ACTION_COUNT, mousebind_action_name, true);
      g_cfg.mouse5action = row_dropdown(cursor, "Mouse 5 does", g_cfg.mouse5action,
                                        MB_ACTION_COUNT, mousebind_action_name, true);
      row_hint("The game itself has no slot for these two");

      draw_group(cursor, "HOTKEYS");
      // The panel's own key first: a player who lost it looks for this row. A hand-edited ini can leave it unmappable;
      // bind_menu_key_effective (input.cpp) falls back to Insert.
      row_bind(cursor, "Panel key", BIND_ROW_MENU);
      row_bind(cursor, "Free camera key", BIND_ROW_CAMERA);
      row_bind(cursor, "Teleport key", BIND_ROW_TELEPORT);
      row_bind(cursor, "Aimbot button", BIND_ROW_AIM);

      // Drawn near the top: UI scale has to stay reachable if everything else clips off the panel. Always live.
      {
        // The range is what this screen can apply (else the track would store values the panel would not draw); the
        // reserve is the literal "200%". Drawing is not a write: only a drag stores, so a taller screen gives the player
        // their number back. The ceiling is menu_applied_scale's.
        const int ceiling = ui_scale_max_for(screenH, menu_ceiling_height(), PANEL_VINSET);
        char scaleText[16];
        sprintf_s(scaleText, "%d%%", settled);
        const int v = row_slider_at(cursor, "UI scale", settled, UI_SCALE_MIN, ceiling,
                                    scaleText, "200%", true, 0, C_BONE,
                                    EK_INT, UI_SCALE_DEFAULT);
        if (v != settled) g_cfg.uiscale = v;
      }

      draw_group(cursor, "AUTOSAVE");
      {
        // A range whose 0 is off needs no parent switch (the readout says "Off"). row_slider_at directly: EK_INT keeps
        // the typed value and reset glyph. The hint says which slot is overwritten.
        char minsText[16], hint[64];
        const int mins = autosave_mins_clamped(g_cfg.autosavemins);
        if (mins == 0) strcpy_s(minsText, "Off"); else sprintf_s(minsText, "%d min", mins);
        const int v = row_slider_at(cursor, "Autosave", mins,
                                    AUTOSAVE_MINS_MIN, AUTOSAVE_MINS_MAX, minsText, "30 min",
                                    true, 0, C_BONE, EK_INT, AUTOSAVE_MINS_DEFAULT);
        if (v != mins) g_cfg.autosavemins = autosave_mins_clamped(v);
        sprintf_s(hint, "Saves to slot %d every N minutes; 0 is off",
                  autosave_slot_clamped(g_cfg.autosaveslot));
        row_hint(hint);
      }

      draw_group(cursor, "ANIMATIONS");
      // anim_switch_apply (anim.h): off writes ANIM_OFF, on writes ANIM_SIMPLE from off and keeps a stored Glass.
      g_cfg.menuanim = anim_switch_apply(g_cfg.menuanim, row_switch(cursor, "Animations",
          g_cfg.menuanim != ANIM_OFF, C_GREEN, true));
      row_hint("Fades and slides in the panel");

      draw_group(cursor, "DIAGNOSTICS");
      // Always live. The log itself is always written (a mod that fails to install must say so); this switch controls
      // the per-feature commentary (secret scans, chains, routes).
      g_cfg.debuglog = row_switch(cursor, "Debug log", g_cfg.debuglog, C_GREEN, true);
      row_hint("Per-feature detail in wtmod.log. Off by default.");
      break;
    }
    case TAB_VISUALS: {
      // Each row writes straight back into g_cfg: a toggle takes effect on the next ESP frame.
      draw_group(cursor, "ENEMIES");
      g_cfg.espenemies  = row_switch(cursor, "Show enemies", g_cfg.espenemies, C_RED, true);
      // The four below are inert while the parent is off, not merely greyed (a click behind a greyed label would change
      // a setting the player was told was unavailable).
      const bool enemiesOn = g_cfg.espenemies != 0;
      g_cfg.espboxes    = row_switch_sub(cursor, "Bounding boxes",  g_cfg.espboxes,    C_RED, enemiesOn);
      g_cfg.esphealth   = row_switch_sub(cursor, "Health bars",     g_cfg.esphealth,   C_RED, enemiesOn);
      g_cfg.espnames    = row_switch_sub(cursor, "Name and health", g_cfg.espnames,    C_RED, enemiesOn);
      g_cfg.espdistance = row_switch_sub(cursor, "Distance",        g_cfg.espdistance, C_RED, enemiesOn);

      draw_group(cursor, "WORLD");
      // Each parent marks a different kind of thing and draws on its own; none is gated by the others or by "Show enemies".
      g_cfg.espitems    = row_switch(cursor, "Items and pickups", g_cfg.espitems,   C_BLUE,   true);
      g_cfg.espsecrets  = row_switch(cursor, "Secret areas",      g_cfg.espsecrets, C_HAZARD, true);
      // Sub-switch of "Secret areas" (inert while off); draws the secret's room in the world.
      const bool secretsOn = g_cfg.espsecrets != 0;
      g_cfg.secretvolume = row_switch_sub(cursor, "Secret volume", g_cfg.secretvolume,
                                          C_HAZARD, secretsOn);
      row_hint("Draws each secret room's walls in the world");
      // Second sub-switch of "Secret areas": the route to the nearest; geometry only, so no keycard, locked activator or
      // one-way drop.
      g_cfg.espsecretpath = row_switch_sub(cursor, "Path to a secret", g_cfg.espsecretpath,
                                           C_HAZARD, secretsOn);
      row_hint("Route to the nearest secret. A hint, not a guarantee.");
      g_cfg.espexplodables = row_switch(cursor, "Explodables", g_cfg.espexplodables,
                                        C_HAZARD, true);
      row_hint("Cracks that blow up when shot");
      g_cfg.espbreakables = row_switch(cursor, "Breakables", g_cfg.espbreakables,
                                       C_HAZARD, true);
      row_hint("Glass and props the engine breaks");
      g_cfg.espswitches = row_switch(cursor, "Switches and locks", g_cfg.espswitches,
                                     C_BLUE, true);
      g_cfg.dmgnumbers = row_switch(cursor, "Damage numbers", g_cfg.dmgnumbers,
                                    C_RED, true);
      row_hint("Shows the damage your hits do over the target");
      // The chain from a hidden touchplate (or switch) to what its lotag operates, when that is or borders a secret.
      g_cfg.secrettriggers = row_switch(cursor, "Secret triggers", g_cfg.secrettriggers,
                                        C_HAZARD, true);
      row_hint("Hidden triggers whose chain ends at a secret");

      draw_group(cursor, "RANGE");
      // Always live, shared by every category on this tab. Bounds are the cull's own (esp_layout.h). Shown in metres,
      // stored in Build units.
      {
        char buf[16], reserve[16];
        sprintf_s(buf, "%d m", g_cfg.espdrawdist / BUILD_UNITS_PER_METRE);
        sprintf_s(reserve, "%d m", ESP_DRAWDIST_MAX / BUILD_UNITS_PER_METRE);
        g_cfg.espdrawdist = row_slider_at(cursor, "Draw distance", g_cfg.espdrawdist,
                                          ESP_DRAWDIST_MIN, ESP_DRAWDIST_MAX, buf, reserve,
                                          true, 0, C_BONE,
                                          EK_METRES, ESP_DRAWDIST_DEFAULT);
      }
      row_hint("Markers stop at this range.");
      g_cfg.espnolimit = row_switch_sub(cursor, "Unlimited range", g_cfg.espnolimit,
                                        C_GREEN, true);
      row_hint("Ignores the distance. Slow on a large map.");

      // Always live, not gated by any ESP category; same "(Unavailable)" suffix and latch as the View tab's Fullbright.
      g_cfg.fullbright = row_switch(cursor,
          light_row_unavailable() ? "Fullbright (Unavailable)" : "Fullbright",
          g_cfg.fullbright, C_BLUE, true);
      row_hint("No shading or fog. Textures reload once.");
      break;
    }
    case TAB_COMBAT: {
      // Options for both aim modes get their own group, enabled when either parent is on (combat_shared_enabled).
      const bool targetingOn = combat_shared_enabled(g_cfg.aimon, g_cfg.silenton);
      draw_group(cursor, "TARGETING");
      // "Pick target": 0 nearest crosshair / 1 nearest player; "Aim at": 0 mid-body / 1 upper body. Each picks a
      // dimension, not a feature, hence row_slider_named.
      g_cfg.aimtarget = row_slider_named(cursor, "Pick target", g_cfg.aimtarget, 0, 1,
                                         aim_target_label, targetingOn);
      g_cfg.aimpoint  = row_slider_named(cursor, "Aim at", g_cfg.aimpoint, 0, 1,
                                         aim_point_label, targetingOn);
      g_cfg.aimcone   = row_slider(cursor, "Cone", g_cfg.aimcone,
                                   AIM_CONE_MIN, AIM_CONE_MAX, "", targetingOn,
                                   AIM_CONE_DEFAULT);
      // How far a candidate may be, independent of the Visuals "Draw distance" (target.h says why); shown in metres.
      {
        char buf[16], reserve[16];
        sprintf_s(buf, "%d m", g_cfg.aimrange / BUILD_UNITS_PER_METRE);
        sprintf_s(reserve, "%d m", AIM_RANGE_MAX / BUILD_UNITS_PER_METRE);
        g_cfg.aimrange = row_slider_at(cursor, "Aim range", g_cfg.aimrange,
                                       AIM_RANGE_MIN, AIM_RANGE_MAX, buf, reserve,
                                       targetingOn, 0, C_BONE,
                                       EK_METRES, AIM_RANGE_DEFAULT);
      }
      g_cfg.aimlead   = row_switch(cursor, "Lead moving targets", g_cfg.aimlead, C_RED, targetingOn);
      g_cfg.aimleadscale = row_slider(cursor, "Lead amount", g_cfg.aimleadscale,
                                      AIM_LEADSCALE_MIN, AIM_LEADSCALE_MAX, "%", targetingOn,
                                      AIM_LEADSCALE_DEFAULT);
      g_cfg.aimdebug  = row_switch(cursor, "Show aim marker", g_cfg.aimdebug, C_RED, targetingOn);

      draw_group(cursor, "AIMBOT");
      g_cfg.aimon = row_switch(cursor, "Aimbot", g_cfg.aimon, C_RED, true);
      // These need aimon specifically: combat_sub_enabled's single-parent rule, not combat_shared_enabled's either/or.
      const bool aimOn = combat_sub_enabled(g_cfg.aimon);
      g_cfg.aimhold = row_switch_sub(cursor, "Hold key", g_cfg.aimhold, C_RED, aimOn);
      g_cfg.aimsmooth = row_slider_sub(cursor, "Smoothing", g_cfg.aimsmooth,
                                       AIM_SMOOTH_MIN, AIM_SMOOTH_MAX, "%", aimOn,
                                       AIM_SMOOTH_DEFAULT);

      draw_group(cursor, "TRIGGERBOT");
      g_cfg.tboton = row_switch(cursor, "Triggerbot", g_cfg.tboton, C_RED, true);
      row_hint("Fires by itself while the condition below holds");
      const bool tbotOn = combat_sub_enabled(g_cfg.tboton);
      g_cfg.tbotwhen = row_slider_named_sub(cursor, "Fire when", g_cfg.tbotwhen,
                                            TBOT_WHEN_CROSSHAIR, TBOT_WHEN_AIMBOT,
                                            tbot_when_label, tbotOn);
      g_cfg.tbotdelay = row_slider_sub(cursor, "Delay", g_cfg.tbotdelay,
                                       TBOT_DELAY_MS_MIN, TBOT_DELAY_MS_MAX, "ms", tbotOn,
                                       TBOT_DELAY_MS_DEFAULT);

      draw_group(cursor, "SILENT AIM");
      g_cfg.silenton = row_switch(cursor, "Silent aim", g_cfg.silenton, C_RED, true);
      const bool silentOn = combat_sub_enabled(g_cfg.silenton);
      g_cfg.silentmaxangle = row_slider_sub(cursor, "Max redirect", g_cfg.silentmaxangle,
                                            SILENT_MAXANGLE_MIN, SILENT_MAXANGLE_MAX, "",
                                            silentOn, SILENT_MAXANGLE_DEFAULT);
      break;
    }
    case TAB_WEAPONS: {
      // Four parents, each named for a weapon and decided from the shot's own `atwith` (accuracy.h): top-level, always
      // live.
      draw_group(cursor, "PERFECT ACCURACY");
      // A latched pair gets "(Unavailable)" so an "on" switch doing nothing is not silent; it still writes g_cfg (the
      // latch is session-only and must not overwrite the saved value).
      const bool pistolLatched   = accuracy_row_unavailable(accuracy_pair_latched(ACC_PAIR_PISTOL));
      const bool chaingunLatched = accuracy_row_unavailable(accuracy_pair_latched(ACC_PAIR_HITSCAN));
      const bool shotgunLatched  = accuracy_row_unavailable(accuracy_pair_latched(ACC_PAIR_HITSCAN));
      const bool expanderLatched = accuracy_row_unavailable(accuracy_pair_latched(ACC_PAIR_EXPANDER));
      g_cfg.accpistol   = row_switch(cursor, pistolLatched   ? "Pistol (Unavailable)"   : "Pistol",
                                     g_cfg.accpistol,   C_HAZARD, true);
      row_hint("Removes this weapon's own spread.");
      g_cfg.accchaingun = row_switch(cursor, chaingunLatched ? "Chaingun (Unavailable)" : "Chaingun",
                                     g_cfg.accchaingun, C_HAZARD, true);
      row_hint("Removes this weapon's own spread.");
      g_cfg.accshotgun  = row_switch(cursor, shotgunLatched  ? "Shotgun (Unavailable)"  : "Shotgun",
                                     g_cfg.accshotgun,  C_HAZARD, true);
      row_hint("Removes the spread; pellets land in one hole.");
      g_cfg.accexpander = row_switch(cursor, expanderLatched ? "Expander (Unavailable)" : "Expander",
                                     g_cfg.accexpander, C_HAZARD, true);
      row_hint("Removes this weapon's own spread.");

      draw_group(cursor, "MOUSE AIM");
      g_cfg.mouseaimon = row_switch(cursor, "Modern mouse aim", g_cfg.mouseaimon, C_HAZARD, true);
      row_hint("Raw input, equal axes, no dead zone.");
      const bool mouseAimOn = g_cfg.mouseaimon != 0;
      {
        // mouseaimsens is degrees TIMES 1000 (mouseaim.h): integer and remainder are the digits, no floating point.
        // row_slider_at for its own two-part format; the reserve is the widest string (200 -> "0.200").
        char sensText[16];
        sprintf_s(sensText, "%d.%03d", g_cfg.mouseaimsens / 1000, g_cfg.mouseaimsens % 1000);
        g_cfg.mouseaimsens = row_slider_at(cursor, "Sensitivity", g_cfg.mouseaimsens,
                                           MOUSEAIM_SENS_MIN, MOUSEAIM_SENS_MAX, sensText, "0.200",
                                           mouseAimOn, SUB_INDENT, C_BONEDIM,
                                           EK_MILLI, MOUSEAIM_SENS_DEFAULT);
      }
      g_cfg.mouseaimvscale = row_slider_sub(cursor, "Vertical scale", g_cfg.mouseaimvscale,
                                            MOUSEAIM_VSCALE_MIN, MOUSEAIM_VSCALE_MAX, "%",
                                            mouseAimOn, MOUSEAIM_VSCALE_DEFAULT);

      // Two ranges with no parent switch, top-level like UI scale; 100 is each one's identity (the DLL writes no player
      // byte). A drag is snapped through firerate_rate_clamped so the knob lands on the 50s.
      draw_group(cursor, "WEAPON SPEED");
      {
        char rateText[16];
        sprintf_s(rateText, "%d%%", g_cfg.firerate);
        const int dragged = row_slider_at(cursor, "Fire rate", g_cfg.firerate,
                                          FIRERATE_MIN, FIRERATE_MAX, rateText, "400%",
                                          true, 0, C_BONE, EK_INT, FIRERATE_DEFAULT);
        if (dragged != g_cfg.firerate) g_cfg.firerate = firerate_rate_clamped(dragged);
      }
      row_hint("Compresses every frame that is not a shot.");
      {
        char switchText[16];
        sprintf_s(switchText, "%d%%", g_cfg.switchspeed);
        const int dragged = row_slider_at(cursor, "Weapon switch speed", g_cfg.switchspeed,
                                          FIRERATE_MIN, FIRERATE_MAX, switchText, "400%",
                                          true, 0, C_BONE, EK_INT, FIRERATE_DEFAULT);
        if (dragged != g_cfg.switchspeed) g_cfg.switchspeed = firerate_rate_clamped(dragged);
      }
      row_hint("Faster lowering and raising when you switch");
      break;
    }
  }

  // After the content: the row owning an open field has taken the click-away's committed value, so the tab can change.
  if (railTab >= 0) menu_set_tab(railTab);

  // The resize grip: three diagonals in panel_grip_rect, drawn after the content (never covered by a row) and before
  // the tooltip; C_BONEFNT at rest (furniture), C_BONEDIM under the cursor.
  {
    const UiRect g = panel_grip_rect(box);
    const unsigned gc = pres_colour(gripHit ? C_BONEDIM : C_BONEFNT);
    for (int i = 1; i <= 3; i++) {
      const int off = i * 4;
      canvas_line(g.x + off, g.y + g.h - 1, g.x + g.w - 1, g.y + off, gc);
    }
  }

  // An open dropdown, over every row: after the content and before the tooltip (the canvas has no z-order).
  menu_dropdown_draw();

  // The tooltip, last so it is on top: stepped once per frame (one row can be rested on; `s_hoverRowKey` -1 is "left").
  // Either button puts it away: a right press on a numeric row opens a field instead.
  s_tooltip = tooltip_step(s_tooltip, s_hoverRowKey, g_ui.pressed || g_ui.rpressed,
                           s_pres.lastMs, g_cfg.menuanim, s_frameDt);
  const bool entryFieldOpen = s_entry.open != 0;
  const char* const hint = s_glyphHint ? s_glyphHint : s_hoverHint;
  s_tooltipText = nullptr;
  // Not while the panel is closing: `openTarget` is false from the key press while the fade runs, and the rest timer
  // would expire inside it.
  if (s_tooltip.shown && s_tooltip.alpha > 0.0 && hint && !entryFieldOpen &&
      s_pres.openTarget) {
    const UiRect b = tooltip_box_place(g_ui.mx, g_ui.my,
                                       canvas_text_width(hint, FONT_SMALL),
                                       font_height[FONT_SMALL], logicalW, logicalH);
    canvas_round_rect(b.x, b.y, b.w, b.h, TOOLTIP_R,
                      alpha_scale(C_WELL, s_tooltip.alpha));
    canvas_round_rect_outline(b.x, b.y, b.w, b.h, TOOLTIP_R, 1,
                              alpha_scale(C_RAISED, s_tooltip.alpha));
    canvas_text(b.x + TOOLTIP_PAD, b.y + TOOLTIP_PAD, hint,
                alpha_scale(C_BONE, s_tooltip.alpha), FONT_SMALL);
    s_tooltipText = hint;
  }

  // A left press nothing claimed is a grab, continuing through panel_drag_step next frame; last, since every control
  // must have looked. `g_ui.down` as well as `pressed` (the suite sets an edge with the button up); s_dragBox/s_dragLive
  // are stamped here as panel_drag_step reports `current` on release.
  if (g_ui.pressed && g_ui.down && !s_pressClaimed && !s_dragLive &&
      s_pres.openTarget && ui_hit(p, g_ui.mx, g_ui.my)) {
    s_drag.phase = PDRAG_MOVE;
    s_drag.start = box;
    s_drag.grabDx = g_ui.mx - box.x;
    s_drag.grabDy = g_ui.my - box.y;
    s_dragBox = box;
    s_dragLive = true;
  }

  // Put back what was borrowed (the cursor is the caller's, the scale the ESP's); the transform stays for the suite, and
  // canvas_begin clears it.
  g_ui.mx = deviceMx;
  g_ui.my = deviceMy;
  canvas_set_scale(UI_SCALE_DEFAULT);
}

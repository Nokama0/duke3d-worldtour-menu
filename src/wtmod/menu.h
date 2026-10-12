// Verbatim from Megaton's src/duke3dmod/menu.h.
#pragma once
#include "ui.h"
#include "common.h"
#include "input.h"    // MOUSE_BUTTON_MIN/MAX
#include "tbot.h"
#include "panel_layout.h"

// TAB_SETTINGS is not a fifth cheat tab: nothing on it reaches menu_live_count.
enum { TAB_VISUALS = 0, TAB_COMBAT = 1, TAB_WEAPONS = 2, TAB_VIEW = 3, TAB_SETTINGS = 4, TAB_COUNT = 5 };

UiRect menu_panel_rect(int screenW, int screenH);
// The box the panel is drawn at: panel_layout's answer for the stored ini keys, or the live box of a drag in flight.
// Logical pixels (device / applied scale).
PanelBox menu_panel_box(int logicalW, int logicalH);
int    menu_active_tab();
void   menu_set_tab(int tab);
void   menu_draw(int screenW, int screenH);
int    menu_applied_scale(int screenH);

// How many cheat categories are live (hazard strip, "N cheats live"): PARENT switches only, never sub-settings, or a
// fresh install would report cheats live. The four accuracy switches count individually; mouseaimon and the
// firerate/switchspeed ranges are not cheats.
inline int menu_live_count(int fovon, int espenemies, int espitems, int espsecrets,
                           int espexplodables, int espswitches,
                           int aimon, int silenton, int freecamon,
                           int accpistol, int accchaingun, int accshotgun, int accexpander,
                           int dmgnumbers, int secrettriggers, int tboton,
                           int espbreakables = 0) {
  return (fovon       ? 1 : 0) + (espenemies  ? 1 : 0)
       + (espitems    ? 1 : 0) + (espsecrets  ? 1 : 0)
       + (espexplodables ? 1 : 0) + (espswitches ? 1 : 0)
       + (aimon       ? 1 : 0) + (silenton    ? 1 : 0)
       + (freecamon   ? 1 : 0)
       + (accpistol   ? 1 : 0) + (accchaingun ? 1 : 0)
       + (accshotgun  ? 1 : 0) + (accexpander ? 1 : 0)
       + (dmgnumbers  ? 1 : 0) + (secrettriggers ? 1 : 0)
       + (tboton      ? 1 : 0) + (espbreakables  ? 1 : 0);
}

// Menu log lines, single words to grep for; named here because input.cpp can't link into test_host.
const char* const MENU_LOG_OPEN  = "menu: open";
const char* const MENU_LOG_CLOSE = "menu: close";

// The key-capture copy, named here for the same reason. Sentence case, no em dash.
const char* const MENU_KEYCAP_PROMPT = "Press a key";
const char* const MENU_GAMECFG_NOTE  = "Also bound in the game";
// What a hotkey field says while hovered with nothing armed (armed beats hovered: bind_field_text).
const char* const MENU_KEYCAP_HOVER  = "Click to set";

const char* const MENU_RESET_HINT = "Reset to default";

// Reads the game's own cfg for the "Also bound in the game" note, once per panel open. A null/empty path or unopenable
// file leaves it empty, so no row warns.
void menu_gamecfg_load(const char* path);

bool menu_keycap_armed();

// Binds `vk` to the armed row, saves, disarms. No-op with none armed; refuses what keycap_step refuses (0, mouse 1).
void menu_keycap_bind(int vk);

// Disarms without changing any binding (Escape, or the panel closing). Safe with nothing armed.
void menu_keycap_cancel();

// Disarms because the panel is closing: keycap_step does not mark it consumed, and a capture left armed would eat the
// next in-game key.
void menu_keycap_close();

// One numeric entry field for the whole panel, as with key capture. Rules live in entry.h; menu.cpp keeps the state.

bool menu_entry_open();

// Hands one SDL scancode to the open field: 41 Escape cancels, 40/88 Return/Enter commit, 42 Backspace deletes,
// anything entry_char_of_scancode names is typed. No-op with no field open.
void menu_entry_key(int sc);

// menu_entry_key plus shift/ctrl: adds Left/Right, Home/End, Delete and Ctrl+A/C/X. Returns the text for the clipboard
// on Ctrl+C/X, else nullptr. Ctrl+V needs a clipboard read first, so input.cpp calls menu_entry_paste.
const char* menu_entry_key_mod(int sc, bool shift, bool ctrl);

// Puts `s` into the open field one typed character at a time, so a pasted "FOV: 90" delivers "90" and a word nothing.
void menu_entry_paste(const char* s);

// Opens a field on `rowKey`, showing `text`. EK_NONE `kind` opens nothing and closes whatever was open (entry.h).
void menu_entry_begin_edit(int rowKey, int kind, int lo, int hi, const char* text);

// Closes the field with the panel: one left open would keep eating keys with nothing on screen.
void menu_entry_close();

// The open field's text (menu.cpp's own buffer, rewritten every keystroke).
const char* menu_entry_text();

// The last committed value and its row, or false; not consumed here.
bool menu_entry_pending(int* rowKey, int* value);

// Test hook: overrides the wall clock menu_draw reads (0 = none).
void menu_set_now_ms(unsigned ms);

// True while the panel is closing under animation (input_menu_open() already false, the open spring not settled at 0).
// overlay.cpp folds it into its draw decisions. Always false under ANIM_OFF.
bool menu_closing_animation_active();

const char* menu_tooltip_text();

const int RAIL_W = 116;
const int RAIL_LABEL_X = 12;

// Structural lengths, exported for the suite's minimum-height and title band (1 + STRIP_H + STATUS_H) arithmetic.
const int STRIP_H = 15, STATUS_H = 30, ROW_H = 30, ROW_GAP = 4, PAD = 14;

// Value column's right margin, exported so the suite can identify slider readouts (right-aligned; a hotkey field is
// centred). Sized for the widest named readout ("Crosshair").
const int VALUE_MARGIN = 10;

// Whether a Combat-tab sub-row is enabled: its parent's raw value is nonzero (AIMBOT's sub-rows by aimon, "Max redirect"
// by silenton).
inline bool combat_sub_enabled(int parentValue) { return parentValue != 0; }

// Whether the TARGETING group is enabled: its rows serve both aim modes, so either parent suffices.
inline bool combat_shared_enabled(int aimon, int silenton) {
  return aimon != 0 || silenton != 0;
}

// "Pick target" shows a NAME, not a switch knob: it picks a dimension, not a feature.
inline const char* aim_target_label(int aimtarget) {
  return aimtarget ? "Closest" : "Crosshair";
}

inline const char* aim_point_label(int aimpoint) {
  return aimpoint ? "Upper" : "Centre";
}

inline const char* tbot_when_label(int when) {
  return tbot_when_clamped(when) == TBOT_WHEN_AIMBOT ? "Aimbot locked" : "Crosshair on enemy";
}

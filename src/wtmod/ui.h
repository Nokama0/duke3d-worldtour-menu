// Verbatim from Megaton's src/duke3dmod/ui.h; kept identical on purpose -- do not edit here.
#pragma once
#include "common.h"

struct UiRect { int x, y, w, h; };

struct UiInput {
  int  mx, my;      // cursor position in overlay pixels
  bool down;        // left button currently held
  bool pressed;     // left button went down this frame (edge)
  // Right button; a right-click on a slider row opens the numeric entry field (entry.h).
  bool rdown;       // right button currently held
  bool rpressed;    // right button went down this frame (edge)
};

bool   ui_hit(const UiRect& r, int x, int y);
UiRect ui_row(UiRect& cursor, int height, int gap);
int    ui_slider_value(const UiRect& track, int mx, int lo, int hi);
int    ui_slider_x(const UiRect& track, int value, int lo, int hi);

bool   ui_press_edge(bool down, bool wasDown);

bool   ui_key_is_initial_press(bool repeat);

// For a save/force/restore toggle: `openValue` while the panel owns input, else `savedValue`. Never invents a default.
int    ui_saved_toggle_value(bool panelOwnsInput, int openValue, int savedValue);

bool   ui_bytes_any_nonzero(const unsigned char* bytes, int count);

// A disabled row is inert, not merely dimmed. `buttonState` is the press edge for a switch, the held state for a slider.
bool   ui_row_accepts_click(bool enabled, bool buttonState, bool hit);

// Floor 75: at 50 a 30px row is 15px tall and its switch unhittable.
const int UI_SCALE_MIN     = 75;
const int UI_SCALE_MAX     = 200;
const int UI_SCALE_DEFAULT = 100;

inline int ui_scale_clamped(int pct) { return clamp_int(pct, UI_SCALE_MIN, UI_SCALE_MAX); }

// Logical -> device, half away from zero (truncation would erase the 1px bevels at 75%). Widened: saturated inputs.
inline int ui_scale_px(int px, int pct) {
  const long long p = (long long)ui_scale_clamped(pct);
  const long long v = (long long)px * p;
  return clamp_ll_to_int(v >= 0 ? (v + 50) / 100 : (v - 50) / 100);
}

inline int ui_unscale_px(int px, int pct) {
  const long long p = (long long)ui_scale_clamped(pct);
  const long long v = (long long)px * 100;
  return clamp_ll_to_int(v >= 0 ? (v + p / 2) / p : (v - p / 2) / p);
}

// Largest scale at which the panel's logical height fits a screen this tall: pct <= (100*screenH + 49) / total, snapped
// down to a multiple of five.
inline int ui_scale_max_for(int screenH, int panelLogicalH, int insetLogical) {
  const long long total = (long long)panelLogicalH + 2LL * (long long)insetLogical;
  if (total <= 0) return UI_SCALE_MAX;
  long long pct = ((long long)screenH * 100 + 49) / total;
  pct -= pct % 5;
  return clamp_int(clamp_ll_to_int(pct), UI_SCALE_MIN, UI_SCALE_MAX);
}

// While the left button is held the applied scale holds: row_slider_at has no drag capture, so a moving layout would
// slide the row from under the cursor.
inline int ui_scale_effective(int stored, int applied, bool dragging) {
  return dragging ? applied : stored;
}

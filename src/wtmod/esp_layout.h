// Verbatim from Megaton's src/duke3dmod/esp_layout.h; kept identical on purpose -- do not edit here.
#pragma once
#include "project.h"
#include "common.h"
#include "canvas.h"
#include "font.h"

// ESP marker layout (corner brackets, health bar, labels) given a box. Pure and header-only (esp.cpp can't link into
// test_host), so the suite pins every placement. Everything widens to long long and clamps back: project_world
// saturates coordinates to the full int range.

// Bracket arm length is a FRACTION of the edge, so brackets read as corners at any box size.
const int ESP_BRACKET_T    = 2;
const int ESP_BRACKET_DIV  = 3;
const int ESP_BRACKET_MIN  = 3;   // floor, when it fits

// Health bar, outside the box on its left so it keeps the box's full height.
const int ESP_BAR_W   = 3;
const int ESP_BAR_GAP = 3;

// ESP_LABEL_GAP = font_height[FONT_SMALL]: a descender never clips into the box's top edge.
const int ESP_LABEL_GAP = 17;
const int ESP_DIST_GAP  = 2;

// Arm length for an edge `dim` long. Capped at (dim-1)/2, not dim/2: two opposing arms of dim/2 on an even edge would
// MEET into a closed outline that merges markers into a grid in a crowd. The cap overrides the minimum on tiny boxes.
inline int esp_bracket_arm(int dim) {
  if (dim <= 0) return 0;
  int a = dim / ESP_BRACKET_DIV;
  if (a < ESP_BRACKET_MIN) a = ESP_BRACKET_MIN;
  const int cap = (dim - 1) / 2;
  if (a > cap) a = cap;
  return a < 0 ? 0 : a;
}

inline int esp_bracket_thickness(int armX, int armY) {
  int t = ESP_BRACKET_T;
  if (t > armX) t = armX;
  if (t > armY) t = armY;
  return t < 0 ? 0 : t;
}

// The projected box if all four corners came back, else the anchor box (a hard pitch can reject the top pair alone).
inline ScreenBox esp_box_or_anchor(const ScreenBox& box, int anchorX, int anchorY) {
  if (box.valid) return box;
  return proj_anchor_box(anchorX, anchorY);
}

inline int esp_label_x(int boxX, int boxW, int textW) {
  return clamp_ll_to_int((long long)boxX + (long long)boxW / 2 - (long long)textW / 2);
}

inline int esp_name_label_y(int boxY) {
  return clamp_ll_to_int((long long)boxY - ESP_LABEL_GAP);
}
inline int esp_dist_label_y(int boxY, int boxH) {
  return clamp_ll_to_int((long long)boxY + (long long)boxH + ESP_DIST_GAP);
}

// Bar left edge and top of its fill (fills bottom up); canvas_bar_fill bounds `fill` to [0, total].
inline int esp_bar_x(int boxX) {
  return clamp_ll_to_int((long long)boxX - ESP_BAR_GAP - ESP_BAR_W);
}
inline int esp_bar_fill_y(int boxY, int boxH, int fill) {
  return clamp_ll_to_int((long long)boxY + (long long)boxH - (long long)fill);
}

// Draw order: sort FAR to NEAR so the near label wins; STABLE at equal distances (strict `<`) so equidistant markers
// don't flicker. EXPLODABLE covers a crack sprite and a breakable wall, SWITCH a switch sprite and wall.
enum { ESP_KIND_SECRET = 0, ESP_KIND_ITEM = 1, ESP_KIND_ENEMY = 2, ESP_KIND_EXPLODABLE = 3,
       ESP_KIND_SWITCH = 4 };
const int ESP_MARKER_MAX = 256;

// Per-marker frame cost and the panel's guaranteed share, both estimates on purpose: running the panel out of room is
// worse than a marker either way. ESP_VERTS_PER_MARKER errs HIGH (eight bracket rects at 6 verts, a bar, a label).
const int ESP_VERTS_PER_MARKER   = 120;
const int ESP_PANEL_RESERVE_VERTS = 6000;   // the panel measured well under this
struct EspMarker {
  int kind, index, dist2d, seq;
  ScreenPt p;
  ScreenBox box;
};
// Keep the NEAREST `keep`, THEN let the caller sort far-to-near: that is painter order, and a frame out of room (158
// enemies, unlimited range) dropped the markers next to the player.
inline void esp_keep_nearest(EspMarker* m, int count, int keep) {
  if (!m || count <= 0 || keep <= 0 || keep >= count) return;
  for (int i = 0; i < keep; i++) {
    int best = i;
    for (int j = i + 1; j < count; j++)
      if (m[j].dist2d < m[best].dist2d) best = j;
    if (best != i) { const EspMarker t = m[i]; m[i] = m[best]; m[best] = t; }
  }
}

inline void esp_sort_far_to_near(EspMarker* m, int count) {
  if (!m || count < 2) return;
  if (count > ESP_MARKER_MAX) count = ESP_MARKER_MAX;
  for (int i = 1; i < count; i++) {
    const EspMarker key = m[i];
    int j = i - 1;
    while (j >= 0 && m[j].dist2d < key.dist2d) { m[j + 1] = m[j]; j--; }
    m[j + 1] = key;
  }
}

// Two runs in two faces (Bahnschrift name, Cascadia Mono cur/max) so digits line up down a column; centred as a pair.
const int ESP_LABEL_RUN_GAP = 4;
struct EspLabelRuns { int nameX, numX, totalW; };
inline EspLabelRuns esp_label_runs(int boxX, int boxW, int nameW, int numW) {
  EspLabelRuns r = { 0, 0, 0 };
  const int gap = (nameW > 0 && numW > 0) ? ESP_LABEL_RUN_GAP : 0;
  r.totalW = clamp_ll_to_int((long long)nameW + gap + numW);
  r.nameX = esp_label_x(boxX, boxW, r.totalW);
  r.numX = clamp_ll_to_int((long long)r.nameX + nameW + gap);
  return r;
}

// Mono run's top, bottom-aligned to the taller name run (the atlas has no per-face ascent to share).
inline int esp_label_mono_y(int nameY, int smallH, int monoH) {
  return clamp_ll_to_int((long long)nameY + smallH - monoH);
}

// The item marker is the enemy's bounding box: the engine sizes any face sprite the same way regardless of picnum
// (polymost_drawsprite face branch, VA 0x00482d70-0x00482dd8). Only colour and label content differ.

// Still present? cstat bit 15 (invisible) is set, not deleted, for CON-scripted respawn items and by moveactors (VA
// 0x004a3ea7-0x004a3ec1); the short sign-extends to -32768, so mask. xrepeat 0 is a filtered pickup (VA 0x004c1e00).
inline bool esp_item_present(int cstat, int xrepeat) {
  return (cstat & SPR_CSTAT_INVISIBLE) == 0 && xrepeat > 0;
}

// esp_any_on() (esp.h) opens overlay.cpp's GL bracket on this. aimmarker, playermarker and notice draw through it too;
// each is its own parameter so a forgotten one fails loudly.
inline bool esp_any_category_on(int espenemies, int espitems, int espsecrets,
                                int aimmarker, int playermarker, int notice,
                                int espexplodables, int espswitches,
                                int espbreakables = 0) {
  return espenemies != 0 || espitems != 0 || espsecrets != 0
      || aimmarker != 0 || playermarker != 0 || notice != 0
      || espexplodables != 0 || espswitches != 0 || espbreakables != 0;
}

// The teleport notice line: centred, y a FRACTION of screen height so it sits under the HUD at 1024x768 and 3440x1440.
const int ESP_NOTICE_Y_DIVISOR = 8;

// Never negative: a negative x would clip the start of the sentence the player most needs.
inline int esp_notice_x(int screenW, int textW) {
  const int x = (screenW - textW) / 2;
  return x < 0 ? 0 : x;
}
inline int esp_notice_y(int screenH) {
  const int y = screenH / ESP_NOTICE_Y_DIVISOR;
  return y < 0 ? 0 : y;
}

const int ESP_SECRET_EXIT_NOTE_MARGIN = 8;
const char* const ESP_SECRET_EXIT_NOTE = "Secret exit in this level";
inline int esp_secret_exit_note_x() { return ESP_SECRET_EXIT_NOTE_MARGIN; }
inline int esp_secret_exit_note_y() { return ESP_SECRET_EXIT_NOTE_MARGIN; }

// Takes the resolved flag, not g_cfg: esp.cpp can't link into test_host but canvas_text can (NO_GL). esp_draw passes
// g_cfg.espsecrets && esp_level_has_secret_exit().
inline void esp_draw_secret_exit_note(bool show) {
  if (!show) return;
  canvas_text(esp_secret_exit_note_x(), esp_secret_exit_note_y(),
              ESP_SECRET_EXIT_NOTE, canvas_esp_secret_colour(), FONT_SMALL);
}

// The aim calibration marker needs its switch AND a consumer of what it instruments. Acquisition runs when either
// assist asks (aim_acquire_should_run, target.h), so aimdebug alone publishes nothing.
inline bool aim_marker_on(int aimon, int silenton, int aimdebug) {
  return (aimon != 0 || silenton != 0) && aimdebug != 0;
}

// The secret-area marker: a CROSS on the sector's centre, not a box (a box around a room would fill the screen close up).
const int ESP_SECRET_ARM = 5;   // half-length of each arm, in pixels
const int ESP_SECRET_T   = 2;   // bar thickness; matches ESP_BRACKET_T's weight

const char* const ESP_SECRET_LABEL = "Secret";

// The touch-exit label is the text switch_classify gives the secret exit button (esp_switch.h).
const char* const ESP_TOUCH_EXIT_LABEL = "Secret exit";

inline int esp_secret_bar_span() { return ESP_SECRET_ARM * 2 + 1; }

inline int esp_secret_hbar_x(int cx) {
  return clamp_ll_to_int((long long)cx - ESP_SECRET_ARM);
}
inline int esp_secret_hbar_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_SECRET_T / 2);
}
inline int esp_secret_vbar_x(int cx) {
  return clamp_ll_to_int((long long)cx - ESP_SECRET_T / 2);
}
inline int esp_secret_vbar_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_SECRET_ARM);
}

// Labels use the enemy marker's gaps, from ESP_SECRET_ARM so they move with the cross.
inline int esp_secret_name_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_SECRET_ARM - ESP_LABEL_GAP);
}
inline int esp_secret_dist_label_y(int cy) {
  return clamp_ll_to_int((long long)cy + ESP_SECRET_ARM + 1 + ESP_DIST_GAP);
}

// The keycard/lock "card" marker: a small rounded-rectangle outline with a 3px filled notch at its left end (the
// magnetic stripe). Colour is keycard_colour(pal) (esp_switch.h); labels sit from half the card's height.
const int ESP_CARD_W = 14;
const int ESP_CARD_H = 10;
const int ESP_CARD_R = 2;
const int ESP_CARD_T = 1;
const int ESP_CARD_NOTCH = 3;

inline int esp_card_x(int cx) { return clamp_ll_to_int((long long)cx - ESP_CARD_W / 2); }
inline int esp_card_y(int cy) { return clamp_ll_to_int((long long)cy - ESP_CARD_H / 2); }

inline int esp_card_notch_x(int cx) { return esp_card_x(cx); }
inline int esp_card_notch_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_CARD_NOTCH / 2);
}

inline int esp_card_name_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - ESP_CARD_H / 2 - ESP_LABEL_GAP);
}
inline int esp_card_dist_label_y(int cy) {
  return clamp_ll_to_int((long long)cy + ESP_CARD_H / 2 + ESP_DIST_GAP);
}

// Card mark: outline, notch, label and an optional second line in one colour. Shared by the keycard pickup and the
// SW_ACCESS lock; pure, so the suite checks it draws a round-rect outline.
inline void esp_draw_card_mark(const ScreenPt& p, unsigned colour,
                               const char* label, const char* line2) {
  canvas_round_rect_outline(esp_card_x(p.x), esp_card_y(p.y),
                            ESP_CARD_W, ESP_CARD_H, ESP_CARD_R, ESP_CARD_T, colour);
  canvas_rect(esp_card_notch_x(p.x), esp_card_notch_y(p.y),
             ESP_CARD_NOTCH, ESP_CARD_NOTCH, colour);
  if (label)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(label, FONT_SMALL)),
                esp_card_name_label_y(p.y), label, colour, FONT_SMALL);
  if (line2)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(line2, FONT_SMALL)),
                esp_card_dist_label_y(p.y), line2, colour, FONT_SMALL);
}

// Cross mark: the two bars every point marker in esp.cpp uses (secret, explodable, plain switch, touch exit). Pure, so
// the suite checks a plain switch draws a cross, not a round-rect outline.
inline void esp_draw_cross_mark(const ScreenPt& p, unsigned colour,
                                const char* label, const char* line2) {
  canvas_rect(esp_secret_hbar_x(p.x), esp_secret_hbar_y(p.y),
              esp_secret_bar_span(), ESP_SECRET_T, colour);
  canvas_rect(esp_secret_vbar_x(p.x), esp_secret_vbar_y(p.y),
              ESP_SECRET_T, esp_secret_bar_span(), colour);
  if (label)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(label, FONT_SMALL)),
                esp_secret_name_label_y(p.y), label, colour, FONT_SMALL);
  if (line2)
    canvas_text(esp_label_x(p.x, 0, canvas_text_width(line2, FONT_SMALL)),
                esp_secret_dist_label_y(p.y), line2, colour, FONT_SMALL);
}

// The aim calibration marker: a HOLLOW square at the RAW aim point (target_aim_z) and a FILLED one at the LED point
// (target_lead). They coincide for hitscan and separate along the enemy's travel for projectiles: that gap IS the lead
// solver. Not a box, so it is not mistaken for an ESP category.
const int AIM_MARKER_HALF = 4;   // half-size: the square spans 2*this + 1
const int AIM_MARKER_T    = 1;

const char* const AIM_MARKER_LABEL = "Aim";

inline int aim_marker_span() { return AIM_MARKER_HALF * 2 + 1; }

inline int aim_marker_x(int cx) {
  return clamp_ll_to_int((long long)cx - AIM_MARKER_HALF);
}
inline int aim_marker_y(int cy) {
  return clamp_ll_to_int((long long)cy - AIM_MARKER_HALF);
}

inline int aim_marker_edge_far(int c) {
  return clamp_ll_to_int((long long)c + AIM_MARKER_HALF + 1 - AIM_MARKER_T);
}

inline int aim_marker_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - AIM_MARKER_HALF - ESP_LABEL_GAP);
}

// Free camera's player marker: a DIAMOND on the player's own sprite, labelled "You". Neither box nor square, so it never
// reads as a target; it is the only way to find the body once the camera detaches.
const int PLAYER_MARKER_HALF = 6;   // half-height: the diamond spans 2*this + 1

const char* const PLAYER_MARKER_LABEL = "You";

inline int player_marker_rows() { return PLAYER_MARKER_HALF * 2 + 1; }

// Half-width of one row, widest at the middle. Rows outside return 0, so an off-by-one loop draws one pixel, not a
// negative-width rect.
inline int player_marker_row_halfwidth(int row) {
  const int d = row - PLAYER_MARKER_HALF;
  const int w = PLAYER_MARKER_HALF - (d < 0 ? -d : d);
  return w < 0 ? 0 : w;
}

inline int player_marker_row_x(int cx, int row) {
  return clamp_ll_to_int((long long)cx - player_marker_row_halfwidth(row));
}
inline int player_marker_row_y(int cy, int row) {
  return clamp_ll_to_int((long long)cy - PLAYER_MARKER_HALF + row);
}
inline int player_marker_row_w(int row) { return player_marker_row_halfwidth(row) * 2 + 1; }

inline int player_marker_label_y(int cy) {
  return clamp_ll_to_int((long long)cy - PLAYER_MARKER_HALF - ESP_LABEL_GAP);
}

// The live camera state must be the first term: a marker appearing the moment the feature is enabled would sit on the
// body while still looking through its eyes.
inline bool freecam_marker_on(bool freecamActive, int freecamshowplayer) {
  return freecamActive && freecamshowplayer != 0;
}

// Draw distance, the one range cull for every category, in BUILD world units (512 per metre). Compared against
// ScreenPt::dist2d, never ::depth (depth scales with viewingrange and moves with the FOV slider).
const int ESP_DRAWDIST_MIN     = 512;    // one metre: tightest useful range
// 256 metres: from outside a level (the cutaway view) the whole map is in view.
const int ESP_DRAWDIST_MAX     = 131072;

// Default is the maximum: a cull shipped active would blank markers past a room's far wall, which reads as a broken
// projection.
const int ESP_DRAWDIST_DEFAULT = ESP_DRAWDIST_MAX;

// Shared by config.cpp's load clamp and the cull, so any value reaching g_cfg still gives a usable range.
inline int esp_draw_distance_clamped(int maxdist) {
  return clamp_int(maxdist, ESP_DRAWDIST_MIN, ESP_DRAWDIST_MAX);
}

// The cull, shared by every category so the slider means the same to all. Inclusive: the last visible distance is the
// one shown under the slider.
inline bool esp_within_draw_distance(int dist2d, int maxdist) {
  // dist2d is a sqrt result: negative is a garbage read, so refuse.
  if (dist2d < 0) return false;
  return dist2d <= esp_draw_distance_clamped(maxdist);
}

// The cull off, as its own term so the slider keeps meaning metres. Costs frame time on a large map.
inline bool esp_range_unlimited(int noLimit) { return noLimit != 0; }
inline bool esp_within_draw_distance_or_unlimited(int dist2d, int maxdist, int noLimit) {
  if (dist2d < 0) return false;
  return esp_range_unlimited(noLimit) ? true : esp_within_draw_distance(dist2d, maxdist);
}

// The cull as esp.cpp applies it. Pinned so WHICH FIELD is read (dist2d, not depth) is a suite check: reading p.depth
// would compile and silently cull at a range that moves with the FOV slider.
inline bool esp_marker_in_range(const ScreenPt& p, int maxdist) {
  return esp_within_draw_distance(p.dist2d, maxdist);
}
inline bool esp_marker_in_range(const ScreenPt& p, int maxdist, int noLimit) {
  return esp_within_draw_distance_or_unlimited(p.dist2d, maxdist, noLimit);
}

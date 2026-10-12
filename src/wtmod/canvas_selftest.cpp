// TEMPORARY, see canvas_selftest.h. Every primitive canvas.h declares, once each, labelled, at UI_SCALE_DEFAULT and the
// identity transform, the one state meant to be pixel for pixel. It exists to be photographed.
#include "canvas_selftest.h"
#include "canvas.h"
#include "font.h"
#include "ui.h"
#include "overlay_internal.h"

// The column, in logical pixels.
static const int PAD      = 10;
static const int LABEL_W  = 132;
static const int SAMPLE_W = 190;
static const int SAMPLE_H = 22;
static const int ROW_H    = 30;
static const int ROWS     = 8;      // the eight shapes below the heading
static const int TEXT_ROW_H = 34;   // a font sample plus its measured rule

static UiRect selftest_row(int x, int y, const char* label) {
  canvas_text(x, y + 6, label, C_BONEDIM, FONT_SMALL);
  UiRect r;
  r.x = x + LABEL_W;
  r.y = y + (ROW_H - SAMPLE_H) / 2;
  r.w = SAMPLE_W;
  r.h = SAMPLE_H;
  return r;
}

void canvas_selftest() {
  int devW = 0, devH = 0;
  ov_size(&devW, &devH);
  if (devW <= 0 || devH <= 0) return;

  canvas_begin(devW, devH);
  // Explicit rather than inherited, so the reference picture does not move with whatever the last caller left set.
  canvas_set_scale(UI_SCALE_DEFAULT);
  canvas_set_transform(0, 0, 1.0, 0, 0);

  const int x = 40, y0 = 60;
  const int headH = 26;
  const int totalW = LABEL_W + SAMPLE_W + 2 * PAD;
  const int totalH = headH + ROWS * ROW_H + FONT_COUNT * TEXT_ROW_H + 2 * PAD;

  canvas_rect(x - PAD, y0 - PAD, totalW, totalH, C_PANEL);

  canvas_text(x, y0, "canvas selftest", C_BONE, FONT_UI);
  int y = y0 + headH;

  UiRect r = selftest_row(x, y, "rect");
  canvas_rect(r.x, r.y, r.w, r.h, C_RAISED);
  y += ROW_H;

  r = selftest_row(x, y, "bevel");
  canvas_bevel(r.x, r.y, r.w, r.h, C_RAISED, C_BEVELHI, C_BEVELLO);
  y += ROW_H;

  // An axis-aligned rule and a diagonal (the one shape a quad cannot rasterise exactly as GL does).
  r = selftest_row(x, y, "line, flat + diagonal");
  canvas_line(r.x, r.y + r.h / 2, r.x + r.w / 2 - 6, r.y + r.h / 2, C_BONE);
  canvas_line(r.x + r.w / 2, r.y + r.h - 1, r.x + r.w - 1, r.y, C_BONE);
  y += ROW_H;

  r = selftest_row(x, y, "hazard");
  canvas_hazard(r.x, r.y, r.w, r.h, C_HAZARD, C_WELL, 6);
  y += ROW_H;

  r = selftest_row(x, y, "round rect r8");
  canvas_round_rect(r.x, r.y, r.w, r.h, 8, C_RECESS);
  y += ROW_H;

  r = selftest_row(x, y, "round outline r8 t1");
  canvas_round_rect_outline(r.x, r.y, r.w, r.h, 8, 1, C_GLASS_RIM);
  y += ROW_H;

  r = selftest_row(x, y, "gradient");
  canvas_gradient_rect(r.x, r.y, r.w, r.h, C_GLASS_TOP, canvas_with_alpha(C_GLASS_TOP, 0));
  y += ROW_H;

  // Additive, so it needs something under it to add to: the well first.
  r = selftest_row(x, y, "glow");
  canvas_rect(r.x, r.y, r.w, r.h, C_WELL);
  canvas_glow_rect(r.x + 8, r.y + 4, r.w - 16, r.h - 8, C_GLASS_GLOW);
  y += ROW_H;

  // Each font with canvas_text_width's width as a rule under it: if the atlas or metrics drift, the rule stops ending
  // where the text does.
  static const char* const kNames[FONT_COUNT] = { "FONT_UI", "FONT_SMALL", "FONT_MONO" };
  static const char* const kSample = "Handgun 0123 WQ,;";
  for (int f = 0; f < FONT_COUNT; f++) {
    canvas_text(x, y + 6, kNames[f], C_BONEDIM, FONT_SMALL);
    const int tx = x + LABEL_W;
    canvas_text(tx, y, kSample, C_BONE, f);
    const int w = canvas_text_width(kSample, f);
    canvas_rect(tx, y + font_height[f] + 4, w, 1, C_HAZARD);
    y += TEXT_ROW_H;
  }

  canvas_end();
}

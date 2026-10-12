// From Megaton's fontgen.h.
#pragma once
#include "font.h"

// `pixels` uses the layout of the baked `font_atlas`, so the canvas uploads it the same way.
struct FontAtlas {
  int w, h;                       // atlas size, one of FONTPACK_LADDER
  unsigned char* pixels;          // w*h alpha bytes, owned (free with fontgen_free)
  Glyph glyphs[FONT_COUNT][96];   // DEVICE pixels: bx/by/w/h/adv at this scale
  int height[FONT_COUNT];         // tmHeight per face, device pixels
  int pct;                        // the scale it was built for
};

// All or nothing: on failure `*out` is untouched. False for pct == UI_SCALE_DEFAULT (100% IS the baked atlas). `out`
// must be zeroed or previously built: the old contents are freed first.
bool fontgen_build(int pct, FontAtlas* out);

void fontgen_free(FontAtlas* a);

// Fullbright: every world surface at palette shade row 0 (docs/re/fullbright-shade-dead-end.md). The composite pass
// shows the world shader's SECOND colour, loaded from PaletteTex at row g_paletteLookup.y * 256 + index; row 0 is full.
#include <windows.h>
#include "light.h"
#include "world.h"
#include "esp_world.h"
#include "config.h"
#include "freecam.h"
#include "hook.h"

// g_shade feeds only the vertex shader's fog term (dropped when g_sectorVis is 0), and the only pal/shade store pair
// (FUN_007FD6F0) writes a surface RECORD copied to the buffer later. So this holds the MAP's shade bytes, saved on the
// way in, restored on the way out, re-taken on a level change. Point lights still apply.
static signed char s_secCeil[WORLD_MAXSECTORS];   // sized to the ARRAYS, not a level
static signed char s_secFlor[WORLD_MAXSECTORS];
static signed char s_wallShade[WORLD_MAXWALLS];

static bool s_held = false;
static int  s_heldSectors = 0;
static int  s_heldWalls = 0;

// Restoring an old level's shades into a new one would corrupt its lighting: a count change throws the copy away.
static bool world_changed() {
  return world_sector_shade_count() != s_heldSectors ||
         world_wall_shade_count() != s_heldWalls;
}

static void light_take() {
  const int ns = world_sector_shade_count();
  const int nw = world_wall_shade_count();
  if (ns <= 0 && nw <= 0) return;          // no map yet
  signed char zero = 0;
  for (int i = 0; i < ns; i++) {
    signed char c = 0, f = 0;
    if (!world_sector_shade_rw(i, &c, &f, false)) { c = 0; f = 0; }
    s_secCeil[i] = c;
    s_secFlor[i] = f;
    world_sector_shade_rw(i, &zero, &zero, true);
  }
  for (int i = 0; i < nw; i++) {
    signed char sh = 0;
    if (!world_wall_shade_rw(i, &sh, false)) sh = 0;
    s_wallShade[i] = sh;
    world_wall_shade_rw(i, &zero, true);
  }
  s_heldSectors = ns;
  s_heldWalls = nw;
  s_held = true;
  if (g_cfg.debuglog != 0)
    wt_log("fullbright: on, %d sectors and %d walls held at shade 0", ns, nw);
}

static void light_put_back() {
  if (!s_held) return;
  const bool same = !world_changed();
  if (same) {
    for (int i = 0; i < s_heldSectors; i++)
      world_sector_shade_rw(i, &s_secCeil[i], &s_secFlor[i], true);
    for (int i = 0; i < s_heldWalls; i++)
      world_wall_shade_rw(i, &s_wallShade[i], true);
  }
  s_held = false;
  s_heldSectors = 0;
  s_heldWalls = 0;
  if (g_cfg.debuglog != 0)
    wt_log("fullbright: off, the map's own shades are %s",
           same ? "back" : "dropped (the level changed while it was on)");
}

void light_sync() {
  const bool want = light_wants(g_cfg.fullbright != 0, camera_active(),
                                g_cfg.freecamfullbright != 0);
  if (want && s_held && world_changed()) {
    s_held = false;
    s_heldSectors = 0;
    s_heldWalls = 0;
    light_take();
    return;
  }
  if (want == s_held) return;
  if (want) light_take();
  else      light_put_back();
}

// The map must never be left zeroed behind us.
void light_drop() { light_put_back(); }

// Sprites cannot be held (actors rewrite their shade every tic), so they are BORROWED: zeroed before the original
// displayrooms and restored after, inside smoothview.cpp's hook. A flat scan misses nothing.
static signed char s_sprShade[WORLD_MAXSPRITES];
static bool s_sprBorrowed = false;

void light_draw_arm() {
  if (s_sprBorrowed) return;                       // a mirror frame draws twice
  if (!light_wants(g_cfg.fullbright != 0, camera_active(),
                   g_cfg.freecamfullbright != 0)) return;
  signed char zero = 0;
  for (int i = 0; i < WORLD_MAXSPRITES; i++) {
    signed char sh = 0;
    if (!world_sprite_shade_rw(i, &sh, false)) { s_sprBorrowed = false; return; }
    s_sprShade[i] = sh;
    world_sprite_shade_rw(i, &zero, true);
  }
  s_sprBorrowed = true;
}

void light_draw_release() {
  if (!s_sprBorrowed) return;
  for (int i = 0; i < WORLD_MAXSPRITES; i++)
    world_sprite_shade_rw(i, &s_sprShade[i], true);
  s_sprBorrowed = false;
}

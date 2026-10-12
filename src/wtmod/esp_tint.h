// The combination-lock tint's pure half.
#pragma once
#include <string.h>
#include "esp_switch.h"
#include "common.h"

// One world point in the map plane (trigvol_quad_corners, trigger.h, hands two of them out).
struct WorldXY { int x, y; };

// Only a combination lock gets a verdict: an access switch's "Needs blue card" is a fact about the player's keys, not an
// instruction.
enum { TINT_NONE = 0, TINT_FLIP = 1, TINT_LEAVE = 2 };

// `line` is switch_classify's second line, compared rather than re-derived from picnum/hitag, so the tint and the
// marker label cannot disagree about one switch.
inline int tint_verdict(int family, const char* line) {
  if (family != SW_COMBINATION || !line) return TINT_NONE;
  if (strcmp(line, "Flip") == 0)  return TINT_FLIP;
  if (strcmp(line, "Leave") == 0) return TINT_LEAVE;
  return TINT_NONE;
}

// 0 for TINT_NONE is not a colour: the caller refuses the tint first.
inline unsigned tint_colour(int verdict, unsigned flip, unsigned leave) {
  return verdict == TINT_FLIP ? flip : verdict == TINT_LEAVE ? leave : 0u;
}

// The +1536 is not a mistaken cosine: x uses the sine at `ang`, y the term at `ang+1536`. Endpoints are NOT rounded:
// rounding tilts the quad's plane off the sprite's, so the depth cut moves with the player.
inline void tint_wall_sprite_endpoints(int x, int y, int ang, int xrepeat, int tilesizx,
                                       int xoff, const short* sintable,
                                       double* ax, double* ay, double* bx, double* by) {
  if (!ax || !ay || !bx || !by || !sintable) return;
  const double xv = (double)xrepeat * (double)sintable[ang & 2047] / 65536.0;
  const double yv = (double)xrepeat * (double)sintable[(ang + 1536) & 2047] / 65536.0;
  const double f  = (double)(tilesizx >> 1) + (double)xoff;
  const double x0 = (double)x - xv * f, y0 = (double)y - yv * f;
  *ax = x0;
  *ay = y0;
  *bx = x0 + xv * (double)tilesizx;
  *by = y0 + yv * (double)tilesizx;
}

// `centred` is cstat bit 7: clear, z is the bottom; set, the engine adds half the height back plus one yrepeat when
// tilesizy is odd. z grows downward, so `top` is the smaller number.
inline void tint_wall_sprite_zrange(int z, int yrepeat, int tilesizy, int yoff,
                                    bool centred, int* top, int* bottom) {
  if (!top || !bottom) return;
  long long zz = (long long)z - 4LL * (long long)yrepeat * (long long)yoff;
  if (centred) {
    zz += 2LL * (long long)tilesizy * (long long)yrepeat;
    if (tilesizy & 1) zz += 2LL * (long long)yrepeat;
  }
  *bottom = clamp_ll_to_int(zz);
  *top    = clamp_ll_to_int(zz - 4LL * (long long)yrepeat * (long long)tilesizy);
}

// The engine compares the extent against the sector's FLAT ceilingz/floorz, not the slope at the sprite's point. Each
// side is refused on its own parallax bit (a sky is not a surface).
inline void tint_clamp_to_sector(int ceilingz, bool ceilingParallax,
                                 int floorz, bool floorParallax,
                                 int* top, int* bottom) {
  if (!top || !bottom) return;
  if (!ceilingParallax && *top < ceilingz)  *top = ceilingz;
  if (!floorParallax   && *bottom > floorz) *bottom = floorz;
}

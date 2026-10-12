// The map side of the ESP: secret sectors, breakable wall panels and the wall half of the switch marks. PURE (no engine
// memory), so the suite runs it in a throwaway host. sector[] has stride 0x28 (4096 slots), wall[] 0x20 (16384).
#ifndef WTMOD_ESP_WORLD_H
#define WTMOD_ESP_WORLD_H

#include "common.h"
#include <math.h>

// Where each array ends: sector[] at nextspritesect, wall[] at headspritesect.
const int WORLD_MAXSECTORS = 4096;
const int WORLD_MAXWALLS   = 16384;

const unsigned WORLD_SECTOR_STRIDE = 0x28;
const unsigned SEC_WALLPTR   = 0x00;
const unsigned SEC_WALLNUM   = 0x02;
const unsigned SEC_CEILINGZ  = 0x04;
const unsigned SEC_FLOORZ    = 0x08;
// The classic visibility byte, NOT a fog level: FUN_007659a0 (0x00765e70) uses it as a MULTIPLIER on globalvisibility,
// which "Remove fog" moves instead (fogwt.cpp).
const unsigned SEC_VISIBILITY = 0x20;
const unsigned SEC_LOTAG     = 0x22;
// The four slope fields; only the secret volume reads them (else a sloped room gets panels cutting its slope).
const unsigned SEC_CEILINGSTAT  = 0x0C;
const unsigned SEC_FLOORSTAT    = 0x0E;
const unsigned SEC_CEILINGHEINUM = 0x12;
const unsigned SEC_FLOORHEINUM   = 0x1A;
// Bit 1 of either stat word: this surface is sloped. Flat sectors never touch the heinum.
const int SEC_STAT_SLOPED = 2;

const unsigned WORLD_WALL_STRIDE = 0x20;
const unsigned WAL_X       = 0x00;
const unsigned WAL_Y       = 0x04;
const unsigned WAL_POINT2  = 0x08;
// The neighbour sector, -1 when one-sided: a switch usually opens a DOOR into the secret (trigger_sector_near_secret).
const unsigned WAL_NEXTSECTOR = 0x0C;
// cstat bit 0 is the engine's clip bit: a two-sided wall with it is a force field or railing, not a way through.
const unsigned WAL_CSTAT   = 0x0E;
const unsigned WAL_PICNUM  = 0x10;
// The wall's OVER picture, which is what a breakable pane or vent is: tbot_wall_breakable keys on it (0x1f7, 0x197, 0x1fe).
const unsigned WAL_OVERPICNUM = 0x12;
const unsigned WAL_PAL     = 0x15;
const unsigned WAL_LOTAG   = 0x1A;
const unsigned WAL_HITAG   = 0x1C;

// Build's getzsofslope as a pure rule. A surface tilts about the sector's FIRST WALL: flat z plus heinum scaled by the
// point's perpendicular distance over the wall length. Doubles, since dx*(py-y0) overflows a long.
struct SectorSlope {
  int  ceilingz, floorz;
  int  ceilingheinum, floorheinum;
  int  ceilingSloped, floorSloped;   // stat bit 1, as 0/1
  int  x0, y0, x1, y1;           // the first wall and the vertex it runs to
};

// The tilt at (px,py) for one surface; 0 when flat or when the first wall is degenerate (no direction to tilt about).
inline double sector_slope_delta(const SectorSlope& s, int heinum, int sloped, int px, int py) {
  if (!sloped) return 0.0;
  const double dx = (double)s.x1 - (double)s.x0, dy = (double)s.y1 - (double)s.y0;
  const double len = sqrt(dx * dx + dy * dy) * 32.0;
  if (!(len > 0.0)) return 0.0;
  const double j = (dx * ((double)py - (double)s.y0) - dy * ((double)px - (double)s.x0)) / 8.0;
  return (double)heinum * j / len;
}

// Saturated, since every term comes from live map memory; a NaN takes clamp_double_to_int's refusing path.
inline void sector_slope_z(const SectorSlope& s, int px, int py, int* ceilz, int* florz) {
  if (ceilz) *ceilz = clamp_double_to_int((double)s.ceilingz +
                        sector_slope_delta(s, s.ceilingheinum, s.ceilingSloped, px, py));
  if (florz) *florz = clamp_double_to_int((double)s.floorz +
                        sector_slope_delta(s, s.floorheinum, s.floorSloped, px, py));
}

// Build's getangle as a pure rule, via atan2 rather than the table search: a degree either way only orients a fallback
// panel.
inline int world_getangle(int dx, int dy) {
  if (dx == 0 && dy == 0) return 0;   // atan2(0,0) is a platform choice; 0 is ours
  const double a = atan2((double)dy, (double)dx) * 1024.0 / 3.14159265358979323846;
  int ang = (int)(a < 0.0 ? a - 0.5 : a + 0.5);
  return ang & 2047;
}

// Build marks a secret area with this sector lotag and the engine's checksectors ZEROES it when the player stands in
// the sector, so a found secret stops answering by itself; a found-set would only drift from the engine's.
const int WORLD_SECRET_LOTAG = 32767;

inline bool world_lotag_is_secret(int lotag) { return lotag == WORLD_SECRET_LOTAG; }

// A sector's anchor: the average of its wall vertices (z is the floorz, added by world.cpp). The loop runs `wallnum`
// times so it terminates; a point2 that leaves the sector's range or points at its own wall is corrupt map memory and
// the sector is refused.
struct SectorCentre { int x, y, ok; };   // int, not bool: MSVC /O2 mishandles a bool in a returned struct

inline bool world_point2_ok(int point2, int self, int wallptr, int wallnum) {
  if (point2 == self) return false;
  return point2 >= wallptr && point2 < wallptr + wallnum;
}

// Sums widen to long long (16384 walls at full int range is ~3.5e13); wallnum is positive by wall_range_ok.
inline SectorCentre world_sector_centre_xy(const unsigned char* walls, unsigned stride,
                                           int wallptr, int wallnum, int numwalls) {
  SectorCentre c = { 0, 0, 0 };
  if (!walls || stride < WORLD_WALL_STRIDE) return c;
  if (!wall_range_ok(wallptr, wallnum, numwalls)) return c;
  long long sx = 0, sy = 0;
  for (int w = 0; w < wallnum; w++) {
    const int idx = wallptr + w;
    const unsigned char* wl = walls + (unsigned)idx * stride;
    const int point2 = *(const short*)(wl + WAL_POINT2);
    if (!world_point2_ok(point2, idx, wallptr, wallnum)) return c;
    sx += *(const int*)(wl + WAL_X);
    sy += *(const int*)(wl + WAL_Y);
  }
  c.x = clamp_ll_to_int(sx / wallnum);
  c.y = clamp_ll_to_int(sy / wallnum);
  c.ok = 1;
  return c;
}

// Widened before the add (two large same-signed coordinates overflow int). Z uses the flat ceilingz/floorz: identical
// in a flat room, off by the slope's rise in a sloped one.
inline int esp_wall_mid(int a, int b) {
  return clamp_ll_to_int(((long long)a + (long long)b) / 2);
}
inline int esp_wall_mid_z(int florz, int ceilz) {
  return clamp_ll_to_int(((long long)florz + (long long)ceilz) / 2);
}

// One frame's wall-mark cache: a level has tens of such walls; overflow is counted, not hidden.
const int ESP_WALL_MAX = 128;

enum { ESP_WKIND_BREAKABLE = 0, ESP_WKIND_SWITCH = 1 };

struct EspWallMark {
  int kind;      // ESP_WKIND_*
  int wallnum;   // the wall index this came from; picnum and tags are re-read live
  int x, y, z;   // the marker's world point
};

inline bool esp_world_should_scan(int espbreakables, int espswitches) {
  return espbreakables != 0 || espswitches != 0;
}

// The two terms esp_world_should_scan ORs, split apart so the scan gate and the draw filter cannot drift.
inline bool esp_wall_mark_wanted(int kind, int espbreakables, int espswitches) {
  return kind == ESP_WKIND_BREAKABLE ? espbreakables != 0 : espswitches != 0;
}

inline int esp_wall_count_clamped(int n) { return clamp_int(n, 0, ESP_WALL_MAX); }

// Build's STANDABLE status list: every crack sprite lives here.
const int ESP_STAT_STANDABLE = 6;

// A crack only detonates when its hitag is nonzero (it links the crack to the barrels and effectors the DETONATE branch
// fires); hitag 0 is inert decoration. Liveness needs no test: the game deletes the sprite when it blows.
inline bool esp_crack_visible(int hitag) { return hitag > 0; }

#endif

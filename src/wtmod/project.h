#pragma once
// The SUBSET of Megaton's project.h that the copied esp_layout.h, freecam.h and entry.h name.
#include "common.h"   // clamp_int, clamp_ll_to_int

struct PlayerPos {
  int posx, posy, posz;
  int oposx, oposy, oposz;
  int cursectnum;
  bool valid;
};
// The local player as the aimbot reads it (Megaton's engine.h shape, so aim.h and target.h take it unchanged).
struct PlayerAim {
  int  index;                // the local player index, 0..15, or -1 when invalid
  int  spriteIndex;          // the player's own sprite, or -1
  int  eyex, eyey, eyez;     // posx/posy/posz (posz IS the eye)
  int  oposx, oposy, oposz;  // the previous tic's position; pos - opos is one tic of movement
  int  ang, horiz, horizoff; // ang 0..2047
  int  pyoff;                // view bob added to posz when a shot's origin is built
  int  cursectnum;
  int  currWeapon;
  int  autoAim;
  bool dead;
};

const int SPR_CSTAT_INVISIBLE = 0x8000;

const int BUILD_UNITS_PER_METRE = 512;

struct ScreenPt {
  int  x, y;      // overlay pixels
  int  depth;     // forward-axis distance, Build units, viewingrange-scaled: what the projection divides by
  int  dist2d;    // true 2D distance, unscaled by viewingrange; for display only
  bool visible;   // false when behind/at camera, behind the pitched view plane, or camera invalid
};

// Minimums keep a distant enemy legible, the maximum stops a near-camera sprite overflowing canvas_rect's `x + w`.
const int PROJ_BOX_MIN_W       = 4;
const int PROJ_BOX_MIN_H       = 6;
const int PROJ_BOX_MAX         = 8192;
const int PROJ_BOX_COORD_LIMIT = 65536;

struct ScreenBox {
  int  x, y, w, h;
  bool valid;   // false unless ALL FOUR corners projected; every other field is 0
  int  corners; // how many of the four corners were accepted (0..4); diagnostic
};

// Fallback when project_sprite_box refuses: claims a POSITION and no size.
inline ScreenBox proj_anchor_box(int ax, int ay) {
  ScreenBox b = { 0, 0, 0, 0, false, 0 };
  b.w = PROJ_BOX_MIN_W;
  b.h = PROJ_BOX_MIN_H;
  // Widened before the subtraction, clamped after.
  b.x = clamp_int(clamp_ll_to_int((long long)ax - PROJ_BOX_MIN_W / 2),
                  -PROJ_BOX_COORD_LIMIT, PROJ_BOX_COORD_LIMIT);
  b.y = clamp_int(clamp_ll_to_int((long long)ay - PROJ_BOX_MIN_H),
                  -PROJ_BOX_COORD_LIMIT, PROJ_BOX_COORD_LIMIT);
  b.valid = true;
  b.corners = 0;   // it measured nothing
  return b;
}

// Addresses: engine-facts.md 4.5. This reads the finished world-to-clip matrix the game copies into its scene constant
// buffer, since a rebuilt matrix drifts and markers slide off targets.

struct ProjViewport { int x, y, w, h; };

// Build units into the renderer's scaled world space (the view matrix translates by the negated SCALED camera).
const double PROJ_WORLD_XSCALE = -0.001;
const double PROJ_WORLD_YSCALE =  0.001;
const double PROJ_WORLD_ZSCALE = -0.0625 * 0.001;

const double PROJ_DEPTH_PER_UNIT = 1000.0;

const double PROJ_MIN_CLIP_W = 1e-6;

// The matrix and viewport sit behind a heap pointer and cannot be bounds-checked against the image.
const int    PROJ_VIEWPORT_MAX = 16384;
const double PROJ_MATRIX_MAX   = 1e9;

// FRUSTUM is project_world_to_screen's contract. DEPTH is for a sprite's corners: near and far planes but NOT the
// sides, so a close-up corner pushed off screen still measures. AHEAD is the near plane alone.
enum { PROJ_ACCEPT_FRUSTUM = 0, PROJ_ACCEPT_AHEAD = 1, PROJ_ACCEPT_DEPTH = 2 };

inline bool proj_finite(double v) { return (v == v) && v < 1e30 && v > -1e30; }

inline bool proj_matrix_ok(const float* m) {
  if (!m) return false;
  double sum = 0.0;
  int identity = 1;
  for (int i = 0; i < 16; i++) {
    const double v = (double)m[i];
    if (!proj_finite(v) || v > PROJ_MATRIX_MAX || v < -PROJ_MATRIX_MAX) return false;
    sum += v < 0.0 ? -v : v;
    const double want = ((i >> 2) == (i & 3)) ? 1.0 : 0.0;
    const double d = v - want;
    if ((d < 0.0 ? -d : d) > 1e-6) identity = 0;
  }
  // AN IDENTITY IS NOT A PROJECTION: the all-zero test passes one, so a field read before the engine fills it looked valid.
  return sum > 1e-6 && !identity;
}

inline bool proj_viewport_ok(const ProjViewport& vp) {
  return vp.w > 0 && vp.h > 0 && vp.w <= PROJ_VIEWPORT_MAX && vp.h <= PROJ_VIEWPORT_MAX
      && vp.x >= -PROJ_VIEWPORT_MAX && vp.x <= PROJ_VIEWPORT_MAX
      && vp.y >= -PROJ_VIEWPORT_MAX && vp.y <= PROJ_VIEWPORT_MAX;
}

inline double proj_scale_x(int wx) { return (double)wx * PROJ_WORLD_XSCALE; }
inline double proj_scale_y(int wy) { return (double)wy * PROJ_WORLD_YSCALE; }
inline double proj_scale_z(int wz) { return (double)wz * PROJ_WORLD_ZSCALE; }

// clip = M * (X, Y, Z, 1), column-vector convention, translation in the fourth COLUMN: flat index is row*4 + col.
inline void proj_clip_coords(const float* m, double X, double Y, double Z, double c[4]) {
  c[0] = (double)m[0]  * X + (double)m[1]  * Y + (double)m[2]  * Z + (double)m[3];
  c[1] = (double)m[4]  * X + (double)m[5]  * Y + (double)m[6]  * Z + (double)m[7];
  c[2] = (double)m[8]  * X + (double)m[9]  * Y + (double)m[10] * Z + (double)m[11];
  c[3] = (double)m[12] * X + (double)m[13] * Y + (double)m[14] * Z + (double)m[15];
}

inline bool proj_ahead(const double c[4]) { return c[3] > PROJ_MIN_CLIP_W; }

inline bool proj_inside_frustum(const double c[4]) {
  if (!proj_ahead(c)) return false;
  const double w = c[3];
  return c[0] >= -w && c[0] <= w
      && c[1] >= -w && c[1] <= w
      && c[2] >= -w && c[2] <= w;
}

inline bool proj_inside_depth(const double c[4]) {
  if (!proj_ahead(c)) return false;
  const double w = c[3];
  return c[2] >= -w && c[2] <= w;
}

inline int proj_pixel_x(double cx, double cw, const ProjViewport& vp) {
  return clamp_double_to_int((double)vp.x + (cx / cw * 0.5 + 0.5) * (double)vp.w);
}
inline int proj_pixel_y(double cy, double cw, const ProjViewport& vp) {
  return clamp_double_to_int((double)vp.y + (0.5 - cy / cw * 0.5) * (double)vp.h);
}

// No engine memory in it, so hand-built matrices can be checked; dist2d is the caller's to fill.
inline bool proj_project_scaled(const float* m, const ProjViewport& vp,
                                double X, double Y, double Z, int accept, ScreenPt* out) {
  const ScreenPt zero = { 0, 0, 0, 0, false };
  if (out) *out = zero;
  if (!proj_matrix_ok(m) || !proj_viewport_ok(vp)) return false;
  double c[4];
  proj_clip_coords(m, X, Y, Z, c);
  for (int i = 0; i < 4; i++) if (!proj_finite(c[i])) return false;
  const bool accepted = (accept == PROJ_ACCEPT_AHEAD) ? proj_ahead(c)
                      : (accept == PROJ_ACCEPT_DEPTH) ? proj_inside_depth(c)
                                                      : proj_inside_frustum(c);
  if (!accepted) return false;
  ScreenPt p = zero;
  p.x = proj_pixel_x(c[0], c[3], vp);
  p.y = proj_pixel_y(c[1], c[3], vp);
  p.depth = clamp_double_to_int(c[3] * PROJ_DEPTH_PER_UNIT);
  p.visible = true;
  if (out) *out = p;
  return true;
}

// The camera's RIGHT vector in scaled world space. Newton rather than sqrt() keeps this free of <math.h>.
inline bool proj_camera_right(const float* m, double r[3]) {
  r[0] = r[1] = r[2] = 0.0;
  if (!proj_matrix_ok(m)) return false;
  const double x = (double)m[0], y = (double)m[1], z = (double)m[2];
  const double len2 = x * x + y * y + z * z;
  if (!proj_finite(len2) || len2 <= 1e-18) return false;
  double len = len2 > 1.0 ? len2 : 1.0;
  for (int i = 0; i < 40; i++) len = 0.5 * (len + len2 / len);
  if (!proj_finite(len) || len <= 0.0) return false;
  r[0] = x / len; r[1] = y / len; r[2] = z / len;
  return true;
}

// The renderer's quad sizing gives half-width = tilesizx*xrepeat/10 and height = tilesizy*yrepeat*4 z units (the 0.25
// factor is a wall/floor sprite).

struct SpriteExtents {
  int halfWidth;   // lateral half-extent, in x/y world units
  int topZ;        // world z of the sprite's top edge (smaller z is higher)
  int bottomZ;     // world z of its bottom edge
};

const int SPR_CSTAT_CENTRED = 0x80;

const int PROJ_MAX_TILE_DIM = 4096;
const int PROJ_MAX_REPEAT   = 255;

const int PROJ_SPRITE_XDIV   = 10;   // half-width = tilesizx * xrepeat / this
const int PROJ_SPRITE_ZSCALE = 4;    // height     = tilesizy * yrepeat * this

// Live memory: each argument is clamped to its field's real range BEFORE multiplying.
inline SpriteExtents proj_sprite_extents(int wz, int cstat, int tilesizx, int tilesizy,
                                         int xrepeat, int yrepeat) {
  SpriteExtents e = { 0, wz, wz };
  const long long tsx  = clamp_int(tilesizx, 0, PROJ_MAX_TILE_DIM);
  const long long tsy  = clamp_int(tilesizy, 0, PROJ_MAX_TILE_DIM);
  const long long xrep = clamp_int(xrepeat,  0, PROJ_MAX_REPEAT);
  const long long yrep = clamp_int(yrepeat,  0, PROJ_MAX_REPEAT);

  e.halfWidth = clamp_ll_to_int(tsx * xrep / PROJ_SPRITE_XDIV);

  const long long h = tsy * yrep * PROJ_SPRITE_ZSCALE;
  if (cstat & SPR_CSTAT_CENTRED) {
    e.topZ    = clamp_ll_to_int((long long)wz - h / 2);
    e.bottomZ = clamp_ll_to_int((long long)wz + (h - h / 2));
  } else {
    e.topZ    = clamp_ll_to_int((long long)wz - h);
    e.bottomZ = wz;
  }
  return e;
}

// Once per drawn frame, first.
bool project_frame_begin();

// False when behind the camera or outside the frustum; `out` is zeroed then.
bool project_world_to_screen(int wx, int wy, int wz, ScreenPt* out);

// Clipped against the near plane in clip space, then divided to pixels (project_world_to_screen zeroes a refusal, so
// depth cannot locate the crossing).
bool project_clip_line_to_screen(int ax, int ay, int az, int bx, int by, int bz,
                                 int* x0, int* y0, int* x1, int* y1);

// `valid` only when all four corners came back AND the size is inside PROJ_BOX_MAX; else the caller draws
// proj_anchor_box.
ScreenBox project_sprite_box(int wx, int wy, const SpriteExtents& ext);

const float* project_frame_matrix();
// The source field is filled for part of the frame only: in the in-world layer's resolve window both copies read
// identity.
const float* project_last_good_matrix();
// Reads the live matrix AT THE CALL, refusing it unless the validity flag (R+0x190) is set: the matrix captured at
// Present is the previous camera position.
bool project_capture_render_matrix();
const float* project_render_matrix();
int project_render_matrix_misses();
bool project_last_good_viewport(ProjViewport* out);
int project_viewport_w();
int project_viewport_h();

// For a caller working in CLIP space.
const float* project_matrix();
bool project_viewport_rect(ProjViewport* out);
bool project_camera_pos(int* x, int* y, int* z);

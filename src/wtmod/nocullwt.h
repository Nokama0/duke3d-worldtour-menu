// "Disable culling" for World Tour, as pure rules for a throwaway host. nocull.h (Megaton's copy) owns the on/off rule.
// Visibility is WHICH sectors FUN_007fa1b0 (rva 0x14A1B0, "emit sector N", a no-op when visited) is called for: from
// FUN_007f9c80 (0x007f9def, seeded by globalcursectnum), FUN_007fac80 and FUN_007fb310.
#ifndef WTMOD_NOCULLWT_H
#define WTMOD_NOCULLWT_H

// The masked-wall lists FUN_007f9c80 fills after the walk (0x007f9ea0) are three fixed short arrays on one counter with
// NO bound: maskwall 0x014c21f0, thewall 0x014c91f0, thesector 0x014db1f0, capacity 6144. The sweep removes the portal
// walk's bound, so the budget below supplies one.
const int NOCULLWT_MASKWALL_MAX = 6144;

// Each masked wall appends records to a 16-byte-element vector on the scene object; how many is NOT established, so the
// reserve is four per wall plus 64, re-read before every sector.
const int NOCULLWT_PER_WALL_RECORDS = 4;
const int NOCULLWT_SECTOR_MARGIN    = 64;

// spritesortcnt's ceiling, the one fixed list that STOPS SILENTLY: FUN_007fa440 appends while DAT_0154e834 < 0x400
// (zero inside the walk), so the only cap is FUN_007f9c80's copy loop (0x007f9e39, 1024 entries of 0x2C at 0x01565b60).
// Survivors depend on sector ORDER; this only measures.
const int NOCULLWT_SPRITE_MAX = 1024;

const int NOCULLWT_STOP_BUDGET  = 1;   // this DLL's own masked-wall reserve
const int NOCULLWT_STOP_SPRITES = 2;   // the engine's 1024-sprite list

inline int nocullwt_stop_flags(bool stoppedOnBudget, int spriteCount) {
  int flags = 0;
  if (stoppedOnBudget) flags |= NOCULLWT_STOP_BUDGET;
  if (spriteCount >= NOCULLWT_SPRITE_MAX) flags |= NOCULLWT_STOP_SPRITES;
  return flags;
}

const int NOCULLWT_MAX_SECTORS = 4096;

inline bool nocullwt_sectors_ok(int numsectors) {
  return numsectors > 0 && numsectors <= NOCULLWT_MAX_SECTORS;
}

// Masked-wall records pending in the scene object's vector. -1 means "do not trust this" and disables the sweep: a
// backwards or mis-sized range is the renderer not being where we think.
inline int nocullwt_pending_records(unsigned begin, unsigned end) {
  if (end < begin) return -1;
  const unsigned bytes = end - begin;
  if ((bytes & 15u) != 0u) return -1;          // 16-byte elements, 0x007f9ead
  const unsigned n = bytes >> 4;
  if (n > (unsigned)NOCULLWT_MASKWALL_MAX * 4u) return -1;   // far past any sane frame
  return (int)n;
}

inline bool nocullwt_budget_allows(int pending, int wallnum) {
  if (pending < 0 || wallnum < 0) return false;
  if (wallnum > NOCULLWT_MAX_SECTORS * 4) return false;      // 16384 walls, the whole array
  const int cost = pending + wallnum * NOCULLWT_PER_WALL_RECORDS + NOCULLWT_SECTOR_MARGIN;
  return cost <= NOCULLWT_MASKWALL_MAX;
}

// The census: where the geometry goes, measured not guessed: sectors offered, wall records built, how many the builder
// would have skipped, how many walls reached the draw. Built minus drawn is what the six-plane frustum test rejected.

inline bool nocullwt_sample_due(unsigned nowMs, unsigned lastMs, unsigned periodMs) {
  if (lastMs == 0u) return true;
  return (unsigned)(nowMs - lastMs) >= periodMs;
}
const unsigned NOCULLWT_SAMPLE_MS = 1000u;

// FUN_007f9230's plane convention (rva 0x149230): positive is inside. FUN_007f95d0 culls with a genuine six-plane test
// (planes from the NDC corners via FUN_0080d320 and FUN_0080d420, read-only at scene +8, zFar 100000), so clearing the
// skip byte loses nothing.
inline float nocullwt_plane_side(const float* plane, const float* pt) {
  if (!plane || !pt) return 0.0f;
  return plane[0] * pt[0] + plane[1] * pt[1] + plane[2] * pt[2] - plane[3];
}

inline void nocullwt_build_to_world(int x, int y, int z, float* out) {
  if (!out) return;
  out[0] = (float)x * -0.001f;
  out[1] = (float)y * 0.001f;
  out[2] = (float)z * -0.0625f * 0.001f;
}

inline int nocullwt_plane_count(unsigned begin, unsigned end) {
  if (end < begin) return -1;
  const unsigned bytes = end - begin;
  if ((bytes & 15u) != 0u) return -1;
  const unsigned n = bytes >> 4;
  if (n > 64u) return -1;          // six is the number; sixty-four is absurd
  return (int)n;
}


// FUN_007f95d0's wall rule (rva 0x1495D0, loop at 0x007f9600): a wall quad is thrown away at the FIRST plane where all
// four corner scores are outside (this wall's vectors at +0x7c/+0x8c, and the next wall's). Returns the plane, or -1
// for kept.
inline int nocullwt_reject_plane(const unsigned char* wallA, const unsigned char* wallB,
                                 const unsigned char* nextA, const unsigned char* nextB,
                                 int planes) {
  if (!wallA || !wallB || !nextA || !nextB || planes <= 0 || planes > 64) return -1;
  for (int i = 0; i < planes; i++)
    if (wallA[i] == 0 && wallB[i] == 0 && nextA[i] == 0 && nextB[i] == 0) return i;
  return -1;
}

// FUN_007f9740's floor and ceiling rule (rva 0x149740, loop at 0x007f97d0): thrown away at the first plane EVERY corner
// point is outside; `pts` is one score vector per wall record.
inline int nocullwt_reject_plane_all(const unsigned char* const* pts, int count, int planes) {
  if (!pts || count <= 0 || planes <= 0 || planes > 64) return -1;
  for (int i = 0; i < planes; i++) {
    bool allOutside = true;
    for (int r = 0; r < count; r++) {
      if (!pts[r]) return -1;
      if (pts[r][i] != 0) { allOutside = false; break; }
    }
    if (allOutside) return i;
  }
  return -1;
}

inline unsigned char nocullwt_score(float f) {
  if (f < -0.1f) return 0;
  return (f <= 0.1f) ? (unsigned char)2 : (unsigned char)1;
}

// The sentinel: FUN_007f8d60 (0x007f8e0d) sets ALL FOUR z values of a two-sided wall to INT_MIN when both sectors have
// the parallax ceiling bit, scaling to +134217.728, 134 km above the map. Detect it by COORDINATE (past 1e5; a real map
// is +-65.5 in x,y and +-8 in z), not plane scores: it is outside near yet inside far.
const float NOCULLWT_SENTINEL_Z    = 134217.728f;
const float NOCULLWT_IMPLAUSIBLE_Z = 1.0e5f;

inline bool nocullwt_is_sentinel_z(float z) {
  return z >= NOCULLWT_IMPLAUSIBLE_Z || z <= -NOCULLWT_IMPLAUSIBLE_Z;
}

// The sentinel poisons the SECTOR, not the wall: FUN_007f9740 judges floor and ceiling by the same per-wall score
// vectors (0x007f9766/0x007f978b read [rec+0x8c]/[rec+0x7c]). This is that rule with poisoned points abstaining; -1
// (keep) when no point is real.
inline int nocullwt_reject_plane_real(const unsigned char* const* pts, const unsigned char* real,
                                      int count, int planes) {
  if (!pts || !real || count <= 0 || planes <= 0 || planes > 64) return -1;
  int anyReal = 0;
  for (int r = 0; r < count; r++) if (real[r]) { anyReal = 1; break; }
  if (!anyReal) return -1;
  for (int i = 0; i < planes; i++) {
    bool allOutside = true;
    for (int r = 0; r < count; r++) {
      if (!real[r]) continue;
      if (!pts[r]) return -1;
      if (pts[r][i] != 0) { allOutside = false; break; }
    }
    if (allOutside) return i;
  }
  return -1;
}

const double NOCULLWT_MIN_W = 1e-6;   // the same epsilon project.h uses

inline bool nocullwt_onscreen(const double c[4]) {
  if (!(c[3] == c[3]) || c[3] <= NOCULLWT_MIN_W) return false;
  const double w = c[3];
  return c[0] >= -w && c[0] <= w && c[1] >= -w && c[1] <= w;
}


inline bool nocullwt_normalise(double v[3]) {
  const double l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  if (!(l2 == l2) || l2 <= 1e-18 || l2 > 1e30) return false;
  double l = l2 > 1.0 ? l2 : 1.0;
  for (int i = 0; i < 40; i++) l = 0.5 * (l + l2 / l);   // Newton, as project.h does
  if (!(l == l) || l <= 0.0) return false;
  v[0] /= l; v[1] /= l; v[2] /= l;
  return true;
}

inline bool nocullwt_matrix_forward(const float* m, double f[3]) {
  f[0] = f[1] = f[2] = 0.0;
  if (!m) return false;
  f[0] = (double)m[12]; f[1] = (double)m[13]; f[2] = (double)m[14];
  return nocullwt_normalise(f);
}

inline void nocullwt_cross(const double a[3], const double b[3], double out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

inline double nocullwt_dot(const double a[3], const double b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline double nocullwt_cos_between(const double a[3], const double b[3]) {
  const double d = nocullwt_dot(a, b);
  if (!(d == d)) return 2.0;
  if (d > 1.0) return 1.0;
  if (d < -1.0) return -1.0;
  return d;
}

inline void nocullwt_point_along(const double eye[3], const double dir[3], double dist,
                                 double out[3]) {
  out[0] = eye[0] + dir[0] * dist;
  out[1] = eye[1] + dir[1] * dist;
  out[2] = eye[2] + dir[2] * dist;
}

// FUN_007f8d60 ORs three decisions into the wall record's skip byte (+0x74): FUN_007f9180's software one-sided rule
// (the cutaway; kept by default), both endpoints behind the view axis (0x007f9157; kept) and the nearer endpoint past
// 100000 (0x007f9164; suppressed for a camera inside the map).
const float NOCULLWT_FAR_LIMIT = 100000.0f;

// Guard: if the record's two projections (nearer +0x9C, farther +0xA0) are not plausible finite floats the byte is left
// as the engine set it. They tell the tests apart: "both behind" is farther < 0, the far test nearer >= 100000, the
// rest the one-sided rule.
inline bool nocullwt_proj_ok(float nearer, float farther) {
  if (!(nearer == nearer) || !(farther == farther)) return false;          // NaN
  if (nearer < -1.0e9f || nearer > 1.0e9f) return false;
  if (farther < -1.0e9f || farther > 1.0e9f) return false;
  return nearer <= farther;
}

const int NOCULLWT_SKIP_ONESIDED = 1;
const int NOCULLWT_SKIP_BEHIND   = 2;
const int NOCULLWT_SKIP_FAR      = 4;

inline int nocullwt_skip_reasons(bool skipped, float nearer, float farther) {
  if (!skipped) return 0;
  if (!nocullwt_proj_ok(nearer, farther)) return NOCULLWT_SKIP_ONESIDED;   // fail closed: keep it
  int r = 0;
  if (farther < 0.0f) r |= NOCULLWT_SKIP_BEHIND;
  if (nearer >= NOCULLWT_FAR_LIMIT) r |= NOCULLWT_SKIP_FAR;
  if (r == 0) r = NOCULLWT_SKIP_ONESIDED;   // nothing else explains it
  return r;
}

inline bool nocullwt_skip_wanted(int reasons, bool keepOneSided, bool keepBehind, bool keepFar) {
  if (reasons == 0) return false;
  if (keepOneSided && (reasons & NOCULLWT_SKIP_ONESIDED)) return true;
  if (keepBehind   && (reasons & NOCULLWT_SKIP_BEHIND))   return true;
  if (keepFar      && (reasons & NOCULLWT_SKIP_FAR))      return true;
  return false;
}

// True3D mesh cache census (docs/re/true3d-renderer.md): True3D draws a per-sector mesh (DAT_01592918[sect]);
// FUN_00802080 returns for a null cache pointer and FUN_007fac80 falls back to the plain path for a zero entry. Counted
// read-only. Cache object 0x1c; per-wall array at +0x10, 0x18 each.
const unsigned NOCULLWT_CACHE_WALLS_OFF = 0x10;
const unsigned NOCULLWT_CACHE_ENTRY     = 0x18;
const unsigned NOCULLWT_CACHE_SIZE      = 0x1C;

// MAXWALLS, and the bitmap marking drawn walls: FUN_007fac80 falls back to FUN_007fb760 for walls with no mesh, so an
// empty cache entry is a hole only if also undrawn.
const int NOCULLWT_MAX_WALLS = 16384;

inline bool nocullwt_bit_get(const unsigned char* bits, int i, int count) {
  if (!bits || i < 0 || i >= count) return false;
  return (bits[i >> 3] & (unsigned char)(1 << (i & 7))) != 0;
}

inline void nocullwt_bit_set(unsigned char* bits, int i, int count) {
  if (!bits || i < 0 || i >= count) return;
  bits[i >> 3] |= (unsigned char)(1 << (i & 7));
}

inline bool nocullwt_wallrange_ok(int wallptr, int wallnum, int numwalls) {
  if (wallnum <= 0 || wallptr < 0 || numwalls <= 0 || numwalls > 16384) return false;
  return wallptr <= numwalls - wallnum;
}

const int NOCULLWT_ATTRIB_MAX = 256;

// Redirects FUN_007f9c80's seed call to FUN_007fa1b0 through this DLL so the sweep runs after it, and hooks
// FUN_007f8d60 so emitted walls keep none of the builder's three rejections. Never fatal: a refusal logs the bytes
// found; only the redirect failing marks the row unavailable.
bool nocullwt_install();

// The empty node: the span tree's allocator (FUN_00814460) inits a fresh node with BOTH ranges inverted
// (n3=n5=+FLT_MAX, n4=n6=-FLT_MAX). The coverage test asks q2 <= n5, true against +FLT_MAX, so a node with NO span
// would occlude everything.
inline bool nocullwt_node_has_no_depth(double n5, double n6) {
  return n5 > n6 || n5 >= 3.402823466e38;
}

// The cutaway is a POSITION test, not a facing one: left alone, the one-sided rule makes the FAR wall vanish for a
// camera outside. A dollhouse draws every wall and removes those nearer the camera than their sector's centre,
// horizontally only (height would bring the near wall back).

#endif

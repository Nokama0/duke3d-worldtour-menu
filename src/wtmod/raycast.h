// A ray through the sector graph without the engine's hitscan. Build's method: find the wall the ray leaves through; if
// two-sided, non-blocking and the height fits the neighbour, step in; else it is the hit. Floor/ceiling go first.
#pragma once

#include "common.h"
#include <math.h>

// Crossing limit per cast: a refusal, not a clamp (past it the ray is looping or too far).
const int RAY_MAX_STEPS = 64;

// Direction as in the engine's hitscan: vx/vy at sintable's 16384 scale, vz ((100 - horiz) << 5) << 6. Sprites are NOT
// hit (engine-forced divergence): a ray aimed at one lands behind it.
struct RayHit {
  int sect;      // the sector the impact is in, or -1 for nothing hit
  int wall;      // the wall hit, or -1 for a floor/ceiling hit
  int sprite;    // always -1 here: this cast does not test sprites
  int x, y, z;
};

// Where two segments cross, in the FIRST one's parameter (`t` ray, `u` wall), or -1. Doubles: live map coordinates
// overflow a long. Parallel and NaN refuse.
inline double ray_segment_cross(double px, double py, double dx, double dy,
                                double ax, double ay, double bx, double by) {
  const double ex = bx - ax, ey = by - ay;
  const double den = dx * ey - dy * ex;
  if (!(den > 1e-12) && !(den < -1e-12)) return -1.0;
  const double rx = ax - px, ry = ay - py;
  const double t = (rx * ey - ry * ex) / den;
  const double u = (rx * dy - ry * dx) / den;
  // t > 0 not >= 0: a crossing at the origin is the wall just crossed.
  if (!(t > 1e-9) || !(u >= 0.0) || !(u <= 1.0)) return -1.0;
  return t;
}

inline int ray_z_at(int z, double vz, double t) {
  return clamp_double_to_int((double)z + vz * t);
}

// `t` where the ray crosses the horizontal plane `planeZ`, or -1 (parallel or behind).
inline double ray_plane_t(int z, double vz, int planeZ) {
  if (!(vz > 1e-9) && !(vz < -1e-9)) return -1.0;
  const double t = ((double)planeZ - (double)z) / vz;
  return (t > 1e-9) ? t : -1.0;
}

inline void ray_point(int x, int y, int z, double dx, double dy, double vz,
                      double t, int* outX, int* outY, int* outZ) {
  if (outX) *outX = clamp_double_to_int((double)x + dx * t);
  if (outY) *outY = clamp_double_to_int((double)y + dy * t);
  if (outZ) *outZ = ray_z_at(z, vz, t);
}


RayHit ray_cast_world(int x, int y, int z, int sect, int vx, int vy, int vz);

// `slack` is the overshoot that counts as arrival.
bool ray_cansee(int x, int y, int z, int sect, int tx, int ty, int tz, int slack);

// As above, reporting `want` and `got` (2D distances to the target and to what the ray struck) and the `hit`.
bool ray_cansee_why(int x, int y, int z, int sect, int tx, int ty, int tz, int slack,
                    double* want, double* got, RayHit* hit);

extern int g_rayWhy, g_rayStep, g_raySect, g_rayWallnum, g_rayWallptr;

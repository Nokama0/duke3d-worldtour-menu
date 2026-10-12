// The sector-graph cast and line-of-sight test shared by teleport and the aimbot.
#include "raycast.h"
#include "world.h"
#include "esp_world.h"

// Floor and ceiling are tested BEFORE the walls each step, so a ray angled into the ground stops there.
int g_rayWhy = 0, g_rayStep = 0, g_raySect = -1, g_rayWallnum = -1, g_rayWallptr = -1;

RayHit ray_cast_world(int x, int y, int z, int sect, int vx, int vy, int vz) {
  RayHit h = { -1, -1, -1, x, y, z };
  g_rayWhy = 0; g_rayStep = -1; g_raySect = sect; g_rayWallnum = -1; g_rayWallptr = -1;
  if (!world_ready()) return h;
  const double dx = (double)vx, dy = (double)vy, dz = (double)vz;
  if (!(dx * dx + dy * dy > 0.0)) return h;   // no direction in the map plane

  const int nsec = world_numsectors();
  const int nw = world_numwalls();
  int cur = sect;
  double base = 0.0;   // how far along the ray the current sector was entered

  for (int step = 0; step < RAY_MAX_STEPS; step++) {
    if (cur < 0 || cur >= nsec) { g_rayWhy = 1; g_rayStep = step; return h; }
    int wallptr = 0, wallnum = 0;
    if (!world_sector_wall_range(cur, &wallptr, &wallnum)) {
      g_rayWhy = 2; g_rayStep = step; g_raySect = cur; return h;
    }
    g_rayWallnum = wallnum;
    g_rayWallptr = wallptr;

    // Which wall of this sector the ray leaves through, and where.
    double bestT = 0.0;
    int bestWall = -1;
    for (int i = 0; i < wallnum; i++) {
      WorldWall w0, w1;
      if (!world_wall(wallptr + i, &w0)) continue;
      if (w0.point2 < 0 || w0.point2 >= nw) continue;
      if (!world_wall(w0.point2, &w1)) continue;
      const double t = ray_segment_cross((double)x, (double)y, dx, dy,
                                         (double)w0.x, (double)w0.y,
                                         (double)w1.x, (double)w1.y);
      if (t < 0.0) continue;
      // Past where this sector was entered, not merely past the origin: the portal just stepped through has the same `t`
      // in the neighbour and the walk would ping-pong (why=4 on every cast).
      if (t <= base + 1e-9) continue;
      if (bestWall < 0 || t < bestT) { bestT = t; bestWall = wallptr + i; }
    }

    // Floor and ceiling planes the ray may cross before that wall, taken at the EXIT point so a slope is measured there.
    SectorSlope slope;
    if (world_sector_slope(cur, &slope)) {
      const double probe = (bestWall >= 0) ? bestT : base;
      int px = 0, py = 0, pz = 0;
      ray_point(x, y, z, dx, dy, dz, probe, &px, &py, &pz);
      int ceilz = 0, florz = 0;
      sector_slope_z(slope, px, py, &ceilz, &florz);
      const double tf = ray_plane_t(z, dz, florz);
      const double tc = ray_plane_t(z, dz, ceilz);
      double tPlane = -1.0;
      if (tf > 0.0 && (bestWall < 0 || tf < bestT)) tPlane = tf;
      if (tc > 0.0 && (bestWall < 0 || tc < bestT) && (tPlane < 0.0 || tc < tPlane)) tPlane = tc;
      if (tPlane > 0.0) {
        h.sect = cur;
        h.wall = -1;
        ray_point(x, y, z, dx, dy, dz, tPlane, &h.x, &h.y, &h.z);
        return h;
      }
    }

    if (bestWall < 0) { g_rayWhy = 3; g_rayStep = step; g_raySect = cur; return h; }   // no crossing

    WorldWall hitw;
    if (!world_wall(bestWall, &hitw)) return h;
    int hx = 0, hy = 0, hz = 0;
    ray_point(x, y, z, dx, dy, dz, bestT, &hx, &hy, &hz);

    // A one-sided wall, a blocking one, or a neighbour the ray does not fit through at this height stops it.
    int stop = 1;
    if (hitw.nextsector >= 0 && hitw.nextsector < nsec &&
        ((unsigned)hitw.cstat & 1u) == 0) {
      SectorSlope ns;
      if (world_sector_slope(hitw.nextsector, &ns)) {
        int nCeil = 0, nFlor = 0;
        sector_slope_z(ns, hx, hy, &nCeil, &nFlor);
        if (hz > nCeil && hz < nFlor) stop = 0;
      }
    }
    if (stop) {
      h.sect = cur;
      h.wall = bestWall;
      h.x = hx; h.y = hy; h.z = hz;
      return h;
    }
    cur = hitw.nextsector;
    base = bestT;
  }
  g_rayWhy = 4;
  return h;   // ran out of steps: nothing hit, rather than a point by exhaustion
}

// Whether a clear line runs to the target. The engine's cansee is not callable, so this fires at the target and checks
// the ray was not stopped SHORT. `slack` is the overshoot that still counts as arrival (a hit on the target's own
// surface lands a hair before the aimed point).
bool ray_cansee_why(int x, int y, int z, int sect, int tx, int ty, int tz, int slack,
                    double* wantOut, double* gotOut, RayHit* hitOut) {
  const double dx = (double)tx - (double)x, dy = (double)ty - (double)y;
  const double want = sqrt(dx * dx + dy * dy);
  if (wantOut) *wantOut = want;
  if (gotOut) *gotOut = -1.0;
  if (hitOut) { RayHit z0 = { -1, -1, -1, 0, 0, 0 }; *hitOut = z0; }
  if (!(want > 0.0)) return true;          // same spot: nothing in the way
  // The cast wants xy normalised to 16384; z is scaled by the same factor so it has advanced by (tz - z) where xy
  // reaches the target.
  const double k = 16384.0 / want;
  const int vx = clamp_double_to_int(dx * k);
  const int vy = clamp_double_to_int(dy * k);
  const int vz = clamp_double_to_int(((double)tz - (double)z) * k);
  const RayHit h = ray_cast_world(x, y, z, sect, vx, vy, vz);
  if (hitOut) *hitOut = h;
  if (h.sect < 0) return false;            // hit nothing at all: not a sightline
  const double hx = (double)h.x - (double)x, hy = (double)h.y - (double)y;
  const double got = sqrt(hx * hx + hy * hy);
  if (gotOut) *gotOut = got;
  return got + (double)slack >= want;
}

bool ray_cansee(int x, int y, int z, int sect, int tx, int ty, int tz, int slack) {
  return ray_cansee_why(x, y, z, sect, tx, ty, tz, slack, 0, 0, 0);
}

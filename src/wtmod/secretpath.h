#pragma once

#include "common.h"

// BFS from the player's room to the nearest lotag 32767 sector. A HINT, NOT A GUARANTEE: geometry only, so it routes
// through keycard doors, locked activators and one-way drops.

// Over a quarter of the 4096 sector slots is no hint: the map is refused.
const int SPATH_MAX_SECTORS = 1024;
// Nothing deeper is searched, so a farther goal reads as "no path".
const int SPATH_MAX_HOPS    = 64;
// A step the player can climb (Build units, 512/metre).
const int SPATH_STEP_UP     = 0x1800;
// A crouching player's height: the gap a room needs.
const int SPATH_MIN_GAP     = 0x2800;

// A walk edge crosses a two-sided wall; a teleport edge is half of a paired effector 7 (drawn as a cross).
enum { SPATH_EDGE_WALK = 0, SPATH_EDGE_TELEPORT = 1 };

// Build z grows DOWNWARD: a higher floor is a SMALLER floorz.
struct SPathSector { int floorz, ceilingz, lotag; };

// One candidate edge: a two-sided wall's midpoint at the near sector's floor, or the effector sprite's point.
struct SPathEdge { int from, to, kind, x, y, z; };

// Duke's door/lift tags (sector.c ST_*): 20-23 and 25-31 are doors; 24 and the 15-19 platforms are judged on their step.
inline int spath_lotag_is_door(int lotag) {
  return ((lotag >= 20 && lotag <= 23) || (lotag >= 25 && lotag <= 31)) ? 1 : 0;
}

// Passable: the step within SPATH_STEP_UP and b's gap fitting SPATH_MIN_GAP. A door or lift passes whatever its gap is
// now (it opens) but not its STEP: a raised floor door is a wall here.
inline int spath_edge_passable(const SPathSector& a, const SPathSector& b, int bIsDoor) {
  const long long climb = (long long)a.floorz - (long long)b.floorz;   // > 0: b's floor is higher
  if (climb > (long long)SPATH_STEP_UP) return 0;
  if (bIsDoor) return 1;
  const long long gap = (long long)b.floorz - (long long)b.ceilingz;
  return (gap >= (long long)SPATH_MIN_GAP) ? 1 : 0;
}

// Returns the hop count (0 for none), filling `out` with at most `outCap` edges. Bounded: each sector queued once.
inline int spath_search(const SPathEdge* edges, int edgeCount, const SPathSector* sectors,
                        int sectorCount, const unsigned char* isDoor,
                        const unsigned char* isGoal, int start,
                        SPathEdge* out, int outCap) {
  if (!edges || !sectors || !isDoor || !isGoal || !out) return 0;
  if (edgeCount < 0 || outCap <= 0) return 0;
  if (sectorCount <= 0 || sectorCount > SPATH_MAX_SECTORS) return 0;
  if (start < 0 || start >= sectorCount) return 0;
  if (isGoal[start]) return 0;

  unsigned char seen[SPATH_MAX_SECTORS];
  short depth[SPATH_MAX_SECTORS];
  int   via[SPATH_MAX_SECTORS];     // edge that reached this sector, -1 at the start
  short queue[SPATH_MAX_SECTORS];
  for (int i = 0; i < sectorCount; i++) { seen[i] = 0; depth[i] = 0; via[i] = -1; }

  int head = 0, tail = 0, goal = -1;
  seen[start] = 1;
  queue[tail++] = (short)start;
  while (head < tail && goal < 0) {
    const int s = queue[head++];
    if (depth[s] >= SPATH_MAX_HOPS) continue;   // past the drawn path's cap
    for (int e = 0; e < edgeCount; e++) {
      const SPathEdge& ed = edges[e];
      if (ed.from != s) continue;
      const int to = ed.to;
      if (to < 0 || to >= sectorCount || seen[to]) continue;
      if (ed.kind == SPATH_EDGE_WALK &&
          !spath_edge_passable(sectors[s], sectors[to], isDoor[to] != 0)) continue;
      seen[to] = 1;
      via[to] = e;
      depth[to] = (short)(depth[s] + 1);
      queue[tail++] = (short)to;
      if (isGoal[to]) { goal = to; break; }
    }
  }
  if (goal < 0) return 0;

  SPathEdge rev[SPATH_MAX_HOPS];
  int at = goal, n = 0;
  while (at != start && n < SPATH_MAX_HOPS) {
    const int e = via[at];
    if (e < 0 || e >= edgeCount) break;
    rev[n++] = edges[e];
    at = edges[e].from;
  }
  const int keep = (n < outCap) ? n : outCap;
  for (int i = 0; i < keep; i++) out[i] = rev[n - 1 - i];
  return depth[goal];
}

inline int spath_rescan_due(int scanned, unsigned sig, unsigned lastSig,
                            int sect, int lastSect,
                            unsigned nowMs, unsigned lastMs, unsigned periodMs) {
  if (!scanned) return 1;
  if (sig != lastSig) return 1;
  if (sect != lastSect) return 1;
  return ((unsigned)(nowMs - lastMs) >= periodMs) ? 1 : 0;
}

struct SPathPoint { int x, y, z, kind; };

// `seq` is the seqlock counter (target.h's snap_*). `hops` can exceed `count`.
struct SPathSnapshot {
  unsigned seq;
  int hops;          // rooms between the player and the secret
  int teleports;     // how many of the stored points are teleport hops
  int count;         // stored points, 0..SPATH_MAX_HOPS
  int fromSect, toSect;
  SPathPoint p[SPATH_MAX_HOPS];
};

inline int spath_count_clamped(int n) { return clamp_int(n, 0, SPATH_MAX_HOPS); }

inline const char* spath_room_word(int hops) { return hops == 1 ? "room" : "rooms"; }

// secretpathwt.cpp: no-ops unless "Secret areas" and "Path to a secret" are on.
void secretpath_tic();
void secretpath_draw(int screenW, int screenH);
bool secretpath_snapshot(SPathSnapshot* out);

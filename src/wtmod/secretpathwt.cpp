// See secretpath.h. It decides nothing: it reads live map memory, hands it to spath_search (pure) and draws the answer.
// The tic and render clocks meet at s_pub via snap_begin/snap_end: one thread here, but a partial path must be
// DETECTABLE, not drawable.
#include <windows.h>
#include <stdio.h>
#include "secretpath.h"
#include "trigger.h"
#include "freecam.h"
#include "world.h"
#include "esp_world.h"
#include "esp_layout.h"
#include "camera.h"
#include "project.h"
#include "canvas.h"
#include "common.h"
#include "config.h"
#include "font.h"
#include "ui.h"
#include "wallclock.h"
#include "target.h"
#include "hook.h"

// A refusal, not a clamp: past it the search runs on the edges staged, so a huge map gives a shorter answer, not a
// wrong one.
const int SPATH_EDGE_MAX = 8192;

// Half the secret marker's arm, as the trigger marker does: a full-size cross beside a secret marker reads as one smudge.
const int SPATH_CROSS_ARM = ESP_SECRET_ARM / 2;

// A blocking two-sided wall (cstat bit 0; a force field or railing) is not a way through.
const unsigned SPATH_CSTAT_BLOCK = 1u;

// Static, not stack: the tic scan runs inside the engine's update, where a 200 KB frame is unacceptable.
static SPathSnapshot s_pub = { 0u, 0, 0, 0, -1, -1, {} };
static SPathSector   s_sec[SPATH_MAX_SECTORS];
static unsigned char s_isDoor[SPATH_MAX_SECTORS];
static unsigned char s_isGoal[SPATH_MAX_SECTORS];
static SPathEdge     s_edges[SPATH_EDGE_MAX];
static SPathEdge     s_route[SPATH_MAX_HOPS];

static unsigned s_sig = 0;
static unsigned s_lastScanMs = 0;
static int      s_lastSect = -1;
static bool     s_scanned = false;
// What the last line reported: one log line per NEW path.
static int s_logFrom = -2, s_logTo = -2, s_logHops = -1, s_logTele = -1;

static unsigned path_seq_read() { return *(volatile unsigned*)&s_pub.seq; }
static void path_seq_write(unsigned v) { *(volatile unsigned*)&s_pub.seq = v; }

// What the off state and every refusal leave, so nothing stays latched on screen.
static void path_publish_empty() {
  unsigned seq = path_seq_read();
  snap_begin(&seq);
  path_seq_write(seq);
  s_pub.hops = 0;
  s_pub.teleports = 0;
  s_pub.count = 0;
  s_pub.fromSect = -1;
  s_pub.toSect = -1;
  snap_end(&seq);
  path_seq_write(seq);
}

bool secretpath_snapshot(SPathSnapshot* out) {
  if (!out) return false;
  const unsigned before = path_seq_read();
  SPathSnapshot tmp = s_pub;
  const unsigned after = path_seq_read();
  if (!snap_read_ok(before, after)) return false;
  tmp.seq = before;
  *out = tmp;
  return true;
}

// Sub-switch of "Secret areas".
static bool path_on() { return g_cfg.espsecrets != 0 && g_cfg.espsecretpath != 0; }

// Each two-sided wall becomes a walk edge at the wall's midpoint and the near sector's floor. `overflow` says whether
// SPATH_EDGE_MAX stopped it early.
static int path_stage_walls(int nsec, int at, bool* overflow) {
  const int nwalls = world_numwalls();
  for (int s = 0; s < nsec; s++) {
    int wallptr = 0, wallnum = 0;
    if (!world_sector_wall_range(s, &wallptr, &wallnum)) continue;
    int ceilz = 0, florz = 0;
    if (!world_sector_zrange(s, &ceilz, &florz)) continue;
    for (int w = wallptr; w < wallptr + wallnum; w++) {
      WorldWall wl;
      if (!world_wall(w, &wl)) continue;
      if (wl.nextsector < 0 || wl.nextsector >= nsec) continue;  // one-sided, or past the cap
      if (((unsigned)wl.cstat & SPATH_CSTAT_BLOCK) != 0) continue;
      if (at >= SPATH_EDGE_MAX) { if (overflow) *overflow = true; return at; }
      // point2 is unvouched for: bounds-checked against live numwalls.
      if (wl.point2 < 0 || wl.point2 >= nwalls) continue;
      WorldWall w2;
      if (!world_wall(wl.point2, &w2)) continue;
      SPathEdge& e = s_edges[at++];
      e.from = s;
      e.to = wl.nextsector;
      e.kind = SPATH_EDGE_WALK;
      e.x = esp_wall_mid(wl.x, w2.x);
      e.y = esp_wall_mid(wl.y, w2.y);
      e.z = florz;
    }
  }
  return at;
}

// Each paired sector effector 7 (statnum 9, paired through the owner field) becomes a teleport edge; the EFFECTOR'S
// SECTOR's lotag gives the kind (0 teleporter, 1 water surface, 2 underwater). Each half of a pair is its own sprite.
static int path_stage_transports(int nsec, int at, bool* overflow) {
  WorldWalk w;
  if (!world_walk_begin(&w, FREECAM_TRANSPORT_STAT)) return at;
  int i = 0;
  while (world_walk_next(&w, &i)) {
    WorldSprite sp, os;
    if (!world_sprite(i, &sp)) continue;
    if (sp.owner < 0 || sp.owner == i) continue;      // the engine skips an unpaired effector
    if (!world_sprite(sp.owner, &os)) continue;
    const int from = sp.sectnum, to = os.sectnum;
    if (from < 0 || from >= nsec || to < 0 || to >= nsec || from == to) continue;
    const int lotag = world_sector_lotag(from);
    if (lotag != 0 && lotag != 1 && lotag != 2) continue;   // not a transport sector
    if (at >= SPATH_EDGE_MAX) { if (overflow) *overflow = true; return at; }
    SPathEdge& e = s_edges[at++];
    e.from = from;
    e.to = to;
    e.kind = SPATH_EDGE_TELEPORT;
    e.x = sp.x;
    e.y = sp.y;
    e.z = sp.z;
  }
  return at;
}

void secretpath_tic() {
// Off means no scan and no stale path; s_scanned is cleared so turning it back on rebuilds.
  if (!path_on() || !world_ready()) {
    if (s_scanned || s_pub.count != 0) path_publish_empty();
    s_scanned = false;
    s_lastSect = -1;
    return;
  }

  PlayerPos pp;
  const bool havePos = camera_local_player_pos(&pp);
  const int nsectors = world_numsectors();
// A corrupt numsectors must not widen the window.
  const int nsec = (nsectors < SPATH_MAX_SECTORS) ? nsectors : SPATH_MAX_SECTORS;
  if (!havePos || !pp.valid || pp.cursectnum < 0 || pp.cursectnum >= nsec) {
    if (s_pub.count != 0) path_publish_empty();
    s_scanned = false;
    s_lastSect = -1;
    return;
  }

// Third signature term 0 on purpose: this scan also reruns on the player's room and the one-second timer.
  const unsigned sig = trigger_map_signature(nsectors, world_numwalls(), 0);
  const unsigned now = wallclock_ms();
  if (!spath_rescan_due(s_scanned ? 1 : 0, sig, s_sig, pp.cursectnum, s_lastSect,
                        now, s_lastScanMs, TRIG_SCAN_PERIOD_MS))
    return;

  for (int s = 0; s < nsec; s++) {
    const int lotag = world_sector_lotag(s);
    int ceilz = 0, florz = 0;
    // An unreadable sector is a zero-height room, which spath_edge_passable refuses: unreadable is impassable.
    if (!world_sector_zrange(s, &ceilz, &florz)) { ceilz = 0; florz = 0; }
    s_sec[s].floorz = florz;
    s_sec[s].ceilingz = ceilz;
    s_sec[s].lotag = lotag;
    s_isDoor[s] = (unsigned char)spath_lotag_is_door(lotag);
    // The engine clears a secret's lotag when found, so it drops out of the goal set by itself.
    s_isGoal[s] = world_lotag_is_secret(lotag) ? (unsigned char)1 : (unsigned char)0;
  }

  bool overflow = false;
  int edgeCount = path_stage_walls(nsec, 0, &overflow);
  edgeCount = path_stage_transports(nsec, edgeCount, &overflow);

  const int hops = spath_search(s_edges, edgeCount, s_sec, nsec, s_isDoor, s_isGoal,
                                pp.cursectnum, s_route, SPATH_MAX_HOPS);
  const int count = (hops < SPATH_MAX_HOPS) ? hops : SPATH_MAX_HOPS;
  int teleports = 0;
  for (int k = 0; k < count; k++)
    if (s_route[k].kind == SPATH_EDGE_TELEPORT) teleports++;

  s_sig = sig;
  s_lastScanMs = now;
  s_lastSect = pp.cursectnum;
  s_scanned = true;

  if (hops <= 0) {
    path_publish_empty();
    return;
  }

// Count LAST inside the bracket: the render clock reads s_pub.p under that count.
  const int toSect = s_route[count - 1].to;
  unsigned seq = path_seq_read();
  snap_begin(&seq);
  path_seq_write(seq);          // odd: readers refuse
  s_pub.hops = hops;
  s_pub.teleports = teleports;
  s_pub.fromSect = pp.cursectnum;
  s_pub.toSect = toSect;
  for (int k = 0; k < count; k++) {
    s_pub.p[k].x = s_route[k].x;
    s_pub.p[k].y = s_route[k].y;
    s_pub.p[k].z = s_route[k].z;
    s_pub.p[k].kind = s_route[k].kind;
  }
  s_pub.count = count;
  snap_end(&seq);
  path_seq_write(seq);          // even

// One line per NEW path, not per scan: a standing player would fill the log with one route.
  if (g_cfg.debuglog != 0 &&
      (pp.cursectnum != s_logFrom || toSect != s_logTo ||
       hops != s_logHops || teleports != s_logTele)) {
    s_logFrom = pp.cursectnum; s_logTo = toSect;
    s_logHops = hops; s_logTele = teleports;
    wt_log("secretpath: sector %d to %d, %d hops (%d drawn, %d teleports), "
           "%d edges over %d sectors, overflow=%d",
           pp.cursectnum, toSect, hops, count, teleports, edgeCount, nsec,
           overflow ? 1 : 0);
  }
}

// Widens before subtracting and clamps back: the projection saturates its coordinates to the int range.
static void path_draw_cross(int cx, int cy, unsigned colour) {
  const int span = SPATH_CROSS_ARM * 2 + 1;
  canvas_rect(clamp_ll_to_int((long long)cx - SPATH_CROSS_ARM),
              clamp_ll_to_int((long long)cy - ESP_SECRET_T / 2),
              span, ESP_SECRET_T, colour);
  canvas_rect(clamp_ll_to_int((long long)cx - ESP_SECRET_T / 2),
              clamp_ll_to_int((long long)cy - SPATH_CROSS_ARM),
              ESP_SECRET_T, span, colour);
}

// THE SAME TWO-ANSWER SPLIT THE CHAINS NEEDED: range is a WORLD distance (a refused ScreenPt is zeroed and would pass
// the cull); SEGMENTS clip against the near plane (an off-screen point is still a direction); CROSSES and the LABEL
// project against the frustum.
void secretpath_draw(int screenW, int screenH) {
  if (!path_on() || !world_ready()) return;
  if (screenW <= 0 || screenH <= 0) return;
  if (!project_frame_begin()) return;

  SPathSnapshot sp;
  if (!secretpath_snapshot(&sp)) return;
  const int n = spath_count_clamped(sp.count);
  if (n <= 0) return;

  int camx = 0, camy = 0, camz = 0;
  if (!project_camera_pos(&camx, &camy, &camz)) return;
  const unsigned colour = canvas_esp_secret_colour();

// Its own canvas frame: esp_draw opens and closes its own.
  canvas_begin(screenW, screenH);
  canvas_set_scale(UI_SCALE_DEFAULT);

  int prevX = 0, prevY = 0, prevZ = 0;
  bool prevOk = false;
  for (int k = 0; k < n; k++) {
    const SPathPoint& pt = sp.p[k];
    // A segment needs both points in range: half a segment to a culled point would point nowhere.
    const bool inRange = esp_within_draw_distance_or_unlimited(
        trigger_world_dist2d(camx, camy, pt.x, pt.y), g_cfg.espdrawdist, g_cfg.espnolimit);

    if (prevOk && inRange) {
      int lx0 = 0, ly0 = 0, lx1 = 0, ly1 = 0;
      if (project_clip_line_to_screen(prevX, prevY, prevZ, pt.x, pt.y, pt.z,
                                      &lx0, &ly0, &lx1, &ly1))
        canvas_line(lx0, ly0, lx1, ly1, colour);
    }

    ScreenPt p;
    if (inRange && project_world_to_screen(pt.x, pt.y, pt.z, &p)) {
      if (pt.kind == SPATH_EDGE_TELEPORT) path_draw_cross(p.x, p.y, colour);
      // One label, at the first turn: the hop count the search returned, which can exceed the points drawn.
      if (k == 0) {
        char lab[32];
        _snprintf_s(lab, sizeof(lab), _TRUNCATE, "Secret: %d %s",
                    sp.hops, spath_room_word(sp.hops));
        canvas_text(esp_label_x(p.x, 0, canvas_text_width(lab, FONT_SMALL)),
                    esp_secret_dist_label_y(p.y), lab, colour, FONT_SMALL);
      }
    }
    prevX = pt.x; prevY = pt.y; prevZ = pt.z; prevOk = inRange;
  }
  canvas_end();
}

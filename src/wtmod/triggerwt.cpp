// See trigger.h. This file decides nothing: it reads live sprite/sector/wall memory and draws; every rule is pure in
// trigger.h. trigger_tic reads on the tic clock, trigger_draw on the render clock, meeting at s_chains.
#include <windows.h>
#include <stdio.h>
#include "trigger.h"
#include "world.h"
#include "esp_world.h"
#include "esp_switch.h"
#include "esp_layout.h"
#include "worldwt.h"
#include "project.h"
#include "canvas.h"
#include "common.h"
#include "config.h"
#include "font.h"
#include "ui.h"
#include "wallclock.h"
#include "hook.h"

// Statnum 6 is where movestandables looks for touchplates; 8 is what operateactivators walks.
const int TRIG_STAT_STANDABLE = ESP_STAT_STANDABLE;   // 6
const int TRIG_STAT_ACTIVATOR = 8;

// TOUCHPLATE and MASTERSWITCH share statnum 6, told apart by picnum; ACTIVATORLOCKED (statnum 8) toggles its sector's
// LOCK bit.
const int TRIG_PIC_TOUCHPLATE      = 3;
const int TRIG_PIC_MASTERSWITCH    = 8;
const int TRIG_PIC_ACTIVATORLOCKED = 4;

// hittype[i].temp_data[5]: the engine's "plate used up" flag, set when a hitag-limited touchplate's budget runs out.
const int TRIG_TEMP_EXHAUSTED = 5;

// The chain families ignore the player's keys, so the walk does not pay to read them.
const int TRIG_GOT_ACCESS_UNKNOWN = 0;

// A refusal, not a clamp: past it the walk stops and chains come only from sprites seen.
const int TRIG_SPRITE_MAX = 512;

// Diagnostic throttle window, shared with esp.cpp and dmgnumwt.cpp.
const unsigned TRIG_DIAG_WINDOW_MS = 3000;

// Half the fallback curtain's width in Build units: two metres across, unmissable yet not mistakable for a room outline.
const int TRIGVOL_CURTAIN_HALF = 512;

// Past this the walk stops (the 512-wall budget usually binds first).
const int SECVOL_MAX_SECTORS = 32;


static TrigChain s_chains[TRIG_MAX_CHAINS];
static int       s_count = 0;
static unsigned  s_sig = 0;
static unsigned  s_lastScanMs = 0;
static bool      s_scanned = false;      // false until the first scan completes

// Static, not stack: this runs on the tic clock inside the engine's update, where a 100 KB frame is unacceptable.
static TrigSprite s_sp[TRIG_SPRITE_MAX];
static TrigSector s_sec[WORLD_MAXSECTORS];
static short      s_wallNext[WORLD_MAXWALLS];

// The SECRET volume's snapshot: one group per undiscovered secret (a trigger-less secret room is staged; one a plate
// merely opens is not), so no dedupe.
static TrigWall s_secWalls[TRIGVOL_WALLS_MAX];
static int      s_secWallCount = 0;                  // how much of s_secWalls the last scan filled
static int      s_secFirst[SECVOL_MAX_SECTORS];      // where room r's group starts
static int      s_secN[SECVOL_MAX_SECTORS];          // how many walls it has (0: nothing to draw)
static int      s_secSect[SECVOL_MAX_SECTORS];       // the sector that group outlines
static int      s_secCx[SECVOL_MAX_SECTORS];         // the room's centre, for the range test
static int      s_secCy[SECVOL_MAX_SECTORS];
static int      s_secCount = 0;                      // groups published, the render clock's bound
static unsigned s_secSig = 0;
static unsigned s_secLastScanMs = 0;
static bool     s_secScanned = false;                // false until the first scan completes
static bool     s_secOn = false;                     // what the two switches were when it was staged

// `last`/`first` are the caller's, so the two diagnostics throttle independently.
static bool trig_throttle_due(unsigned* last, bool* first) {
  const unsigned now = (unsigned)GetTickCount();
  if (!*first && (now - *last) < TRIG_DIAG_WINDOW_MS) return false;
  *first = false;
  *last = now;
  return true;
}

// The role fields differ per list and are the caller's.
static void trig_fill_common(TrigSprite& s, const WorldSprite& sp) {
  s.index   = sp.index;
  s.lotag   = sp.lotag;
  s.hitag   = sp.hitag;
  s.sectnum = sp.sectnum;
  s.x       = sp.x;
  s.y       = sp.y;
  s.z       = sp.z;
}

void trigger_tic() {
  // Off: no scan and no stale chains; s_scanned is cleared so turning it on rebuilds.
  if (!g_cfg.secrettriggers || !world_ready()) {
    s_count = 0;
    s_scanned = false;
    return;
  }

  const int nsectors = world_numsectors();
  const int nwalls   = world_numwalls();
  if (nsectors <= 0 || nwalls <= 0) { s_count = 0; s_scanned = false; return; }

  // Standables list length, the third signature term.
  int standables = 0;
  {
    WorldWalk w;
    int i = 0;
    if (world_walk_begin(&w, TRIG_STAT_STANDABLE))
      while (world_walk_next(&w, &i)) standables++;
  }

  const unsigned sig = trigger_map_signature(nsectors, nwalls, standables);
  const unsigned now = wallclock_ms();
  // Signature first (a level change is immediate), timer second (found secrets and opened locks go at most a second
  // stale).
  if (s_scanned && sig == s_sig && (now - s_lastScanMs) < TRIG_SCAN_PERIOD_MS) return;

  int nsp = 0;
  // The cap is shared across the three walks in LIST ORDER. Activators go first so a flood starves only the lists with
  // likely hundreds; `overflow` makes starvation visible.
  bool overflow = false;
  WorldWalk w;
  int i = 0;
  WorldSprite sp;

  if (world_walk_begin(&w, TRIG_STAT_ACTIVATOR)) {
    while (world_walk_next(&w, &i)) {
      if (nsp >= TRIG_SPRITE_MAX) { overflow = true; break; }
      if (!world_sprite(i, &sp)) continue;
      TrigSprite& s = s_sp[nsp++];
      trig_fill_common(s, sp);
      s.kind = -1;
      s.isActivator = 1;
      s.isLocked = (sp.picnum == TRIG_PIC_ACTIVATORLOCKED) ? 1 : 0;
      s.exhausted = false;
    }
  }

  // A master switch is an activator, just delayed (operatemasterswitches starts a countdown).
  if (world_walk_begin(&w, TRIG_STAT_STANDABLE)) {
    while (world_walk_next(&w, &i)) {
      if (!world_sprite(i, &sp)) continue;
      if (sp.picnum != TRIG_PIC_TOUCHPLATE && sp.picnum != TRIG_PIC_MASTERSWITCH) continue;
      if (nsp >= TRIG_SPRITE_MAX) { overflow = true; break; }
      TrigSprite& s = s_sp[nsp++];
      trig_fill_common(s, sp);
      if (sp.picnum == TRIG_PIC_TOUCHPLATE) {
        s.kind = TRIG_TOUCHPLATE; s.isActivator = 0; s.isLocked = 0;
        // An untrusted read is NOT "used up": exhausted stays false rather than hiding a live plate.
        int t5 = 0;
        s.exhausted = world_actor_temp_data(sp.index, TRIG_TEMP_EXHAUSTED, &t5) && t5 == 1;
      } else {
        s.kind = -1; s.isActivator = 1; s.isLocked = 0; s.exhausted = false;
      }
    }
  }

  // switch_classify knows the two-frame families, multiswitch, combination members and door tiles, and refuses a
  // lotag-0 switch (no channel, so no chain end).
  if (world_walk_begin(&w, ESP_STAT_SWITCH)) {
    while (world_walk_next(&w, &i)) {
      if (!world_sprite(i, &sp)) continue;
      if (switch_base_for(sp.picnum) < 0) continue;   // cheap gate
      if (switch_classify(sp.picnum, sp.pal, sp.hitag, sp.lotag,
                          TRIG_GOT_ACCESS_UNKNOWN).family == SW_NONE) continue;
      if (nsp >= TRIG_SPRITE_MAX) { overflow = true; break; }
      TrigSprite& s = s_sp[nsp++];
      trig_fill_common(s, sp);
      s.kind = TRIG_SWITCH; s.isActivator = 0; s.isLocked = 0; s.exhausted = false;
    }
  }

  const int nsec = (nsectors < WORLD_MAXSECTORS) ? nsectors : WORLD_MAXSECTORS;
  for (int s = 0; s < nsec; s++) {
    s_sec[s].lotag = world_sector_lotag(s);
    // A refused range is staged as (0, 0): no neighbour, rather than borrowing sector 0's. wallptr 0 is a REAL index, so
    // wallnum 0 is the refusal flag.
    int wp = 0, wn = 0;
    const bool haveRange = world_sector_wall_range(s, &wp, &wn);
    s_sec[s].wallptr = haveRange ? wp : 0;
    s_sec[s].wallnum = haveRange ? wn : 0;
  }
  const int nw = (nwalls < WORLD_MAXWALLS) ? nwalls : WORLD_MAXWALLS;
  // -1 is the engine's "no neighbour": world_wall zeroes its output on a refusal, which would read as "faces sector 0".
  for (int wi = 0; wi < nw; wi++) {
    WorldWall wl;
    s_wallNext[wi] = world_wall(wi, &wl) ? (short)wl.nextsector : (short)-1;
  }

  // WAL_NEXTSECTOR (0x0C) and temp_data[5] were read off classic Build layouts, so they are proved once on real map
  // data. A wrong offset cannot fake the shapes: every nextsector is -1 or a real index, a used-up flag is 0 or 1.
  static bool s_provedOnce = false;
  if (!s_provedOnce && nw > 0) {
    s_provedOnce = true;
    int badNext = 0, oneSided = 0;
    for (int wi = 0; wi < nw; wi++) {
      if (s_wallNext[wi] == -1) { oneSided++; continue; }
      if (s_wallNext[wi] < 0 || s_wallNext[wi] >= nsec) badNext++;
    }
    int plates = 0, badFlag = 0;
    for (int k = 0; k < nsp; k++) {
      if (s_sp[k].kind != TRIG_TOUCHPLATE) continue;
      plates++;
      int t5 = 0;
      if (world_actor_temp_data(s_sp[k].index, TRIG_TEMP_EXHAUSTED, &t5)
          && t5 != 0 && t5 != 1) badFlag++;
    }
    wt_log("trigger: offsets proved on %d walls -- nextsector out of range %d, "
           "one-sided %d; temp_data[5] on %d touchplates -- not 0 or 1: %d. "
           "Both counts must be 0; a non-zero one means the offset is wrong",
           nw, badNext, oneSided, plates, badFlag);
  }

  // Publish s_count LAST: the render clock reads s_chains under it.
  const int newCount = trigger_resolve(s_sp, nsp, s_sec, nsec, s_wallNext, nw,
                                       s_chains, TRIG_MAX_CHAINS);
  s_sig = sig;
  s_lastScanMs = now;
  s_scanned = true;
  s_count = newCount;

  // Looked up while s_sp still holds the sprites (TrigChain doesn't carry the lotag).
  static unsigned s_diagLast = 0;
  static bool     s_diagFirst = true;
  if (g_cfg.debuglog != 0 && trig_throttle_due(&s_diagLast, &s_diagFirst)) {
    char lotags[TRIG_MAX_CHAINS * 8];
    int at = 0;
    lotags[0] = 0;
    for (int c = 0; c < s_count && at < (int)sizeof(lotags) - 1; c++) {
      int lotag = 0;
      for (int k = 0; k < nsp; k++)
        if (s_sp[k].index == s_chains[c].trigger) { lotag = s_sp[k].lotag; break; }
      const int wrote = _snprintf_s(lotags + at, sizeof(lotags) - at, _TRUNCATE,
                                    c ? ",%d" : "%d", lotag);
      if (wrote < 0) break;   // truncated: keep what fits and stop
      at += wrote;
    }
    wt_log("trigger: chains=%d sprites=%d overflow=%d lotags=%s sectors=%d walls=%d",
           s_count, nsp, overflow ? 1 : 0, lotags, nsec, nw);
  }
}

// The secret marker's cross at HALF the arm length (a touchplate often sits beside the secret's marker; two full crosses
// would smudge). Thickness unchanged: a one-pixel bar vanishes at distance.
const int TRIG_MARKER_ARM = ESP_SECRET_ARM / 2;
static void trigger_draw_marker(int cx, int cy, unsigned colour) {
  const int span = TRIG_MARKER_ARM * 2 + 1;
  canvas_rect(clamp_ll_to_int((long long)cx - TRIG_MARKER_ARM),
              clamp_ll_to_int((long long)cy - ESP_SECRET_T / 2),
              span, ESP_SECRET_T, colour);
  canvas_rect(clamp_ll_to_int((long long)cx - ESP_SECRET_T / 2),
              clamp_ll_to_int((long long)cy - TRIG_MARKER_ARM),
              ESP_SECRET_T, span, colour);
}

// Without a seen-list, one touchplate paired with several activators would draw its marker and "Step here" once per
// activator.
static bool trig_seen(int* seen, int& count, int cap, int index) {
  for (int i = 0; i < count; i++) if (seen[i] == index) return true;
  if (count < cap) seen[count++] = index;
  return false;
}

// Three rules (trigger.h has the reasoning). 1: range is a WORLD distance; a chain counts if EITHER end is in range.
// 2: the line is clipped, not dropped. 3: a marker needs its own end in range and projected.
void trigger_draw(int screenW, int screenH) {
  if (!g_cfg.secrettriggers || !world_ready() || s_count <= 0) return;
  if (screenW <= 0 || screenH <= 0) return;
  if (!project_frame_begin()) return;

  int camx = 0, camy = 0, camz = 0;
  if (!project_camera_pos(&camx, &camy, &camz)) return;

  const unsigned colour = canvas_esp_secret_colour();

  int trigSeen[TRIG_MAX_CHAINS];  int trigSeenCount = 0;
  int actSeen[TRIG_MAX_CHAINS];   int actSeenCount = 0;

  // Its own canvas frame: esp_draw opens and closes its own, so later calls are outside any frame.
  canvas_begin(screenW, screenH);
  canvas_set_scale(UI_SCALE_DEFAULT);

  // Bounded again in case a producer forgot TRIG_MAX_CHAINS.
  for (int c = 0; c < s_count && c < TRIG_MAX_CHAINS; c++) {
    const TrigChain& ch = s_chains[c];

    // Rule 1: world distance, before projecting.
    const bool tRange = trigger_end_in_range(
        trigger_world_dist2d(camx, camy, ch.tx, ch.ty), g_cfg.espdrawdist, g_cfg.espnolimit);
    const bool aRange = trigger_end_in_range(
        trigger_world_dist2d(camx, camy, ch.ax, ch.ay), g_cfg.espdrawdist, g_cfg.espnolimit);
    if (!tRange && !aRange) continue;

    // MARKERS project against the frustum (a label must be on screen). The LINE is clipped in clip space instead: the
    // frustum answer cut chains short ("off to the side" read as "behind the camera").
    ScreenPt tOnScreen, aOnScreen;
    const bool tFrustum = project_world_to_screen(ch.tx, ch.ty, ch.tz, &tOnScreen);
    const bool aFrustum = project_world_to_screen(ch.ax, ch.ay, ch.az, &aOnScreen);

    // Rule 3: a marker needs its own end in range and ON SCREEN.
    const bool tOn = tRange && tFrustum;
    const bool aOn = aRange && aFrustum;

    // A switch gets neither ("Switches and locks" already draws a cross).
    if (ch.kind == TRIG_TOUCHPLATE && tOn
        && !trig_seen(trigSeen, trigSeenCount, TRIG_MAX_CHAINS, ch.trigger)) {
      trigger_draw_marker(tOnScreen.x, tOnScreen.y, colour);
      const char* step = trigger_step_label();
      canvas_text(esp_label_x(tOnScreen.x, 0, canvas_text_width(step, FONT_SMALL)),
                  esp_secret_dist_label_y(tOnScreen.y), step, colour, FONT_SMALL);
    }

    // Rule 2: clipped against the near plane; one end behind is cut at the plane, neither in front draws nothing. The
    // GPU clips the sides.
    int lx0 = 0, ly0 = 0, lx1 = 0, ly1 = 0;
    const bool haveLine = project_clip_line_to_screen(ch.tx, ch.ty, ch.tz,
                                                      ch.ax, ch.ay, ch.az,
                                                      &lx0, &ly0, &lx1, &ly1);
    if (haveLine) canvas_line(lx0, ly0, lx1, ly1, colour);


    if (aOn && !trig_seen(actSeen, actSeenCount, TRIG_MAX_CHAINS, ch.activator)) {
      const char* lab = trigger_effect_label(ch.effect);
      canvas_text(esp_label_x(aOnScreen.x, 0, canvas_text_width(lab, FONT_SMALL)),
                  esp_secret_name_label_y(aOnScreen.y), lab, colour, FONT_SMALL);
    }
  }
  canvas_end();
}

// Stages one room's wall loop, or a curtain if it doesn't fit or can't be read. Returns walls staged (0: nothing, not
// even a curtain). A refused read costs the whole room: half a room lies about where it ends.
static int secvol_stage_room(int sect, int wallptr, int wallnum, int nw,
                             int cx, int cy, const short* sintable, bool* fallback) {
  const int first = s_secWallCount;
  bool fb = false;
  const int plan = trigvol_stage_plan(wallnum, TRIGVOL_WALLS_PER_SECTOR,
                                      s_secWallCount, TRIGVOL_WALLS_MAX, &fb);
  // An unreadable slope refuses the room rather than drawing flat (a flat panel in a sloped room cuts through its
  // floor).
  SectorSlope slope;
  const bool haveSlope = world_sector_slope(sect, &slope);
  bool staged = false;
  if (plan > 0 && haveSlope) {
    // Subtracted from the count so neither term can overflow.
    staged = wallptr >= 0 && wallptr <= nw - plan;
    for (int i = 0; staged && i < plan; i++) {
      WorldWall w0, w1;
      if (!world_wall(wallptr + i, &w0)) { staged = false; break; }
      if (w0.point2 < 0 || w0.point2 >= nw) { staged = false; break; }
      if (!world_wall(w0.point2, &w1)) { staged = false; break; }
      TrigWall& tw = s_secWalls[s_secWallCount];
      tw.sector = sect;
      tw.x0 = w0.x; tw.y0 = w0.y;
      tw.x1 = w1.x; tw.y1 = w1.y;
      sector_slope_z(slope, tw.x0, tw.y0, &tw.z0top, &tw.z0bot);
      sector_slope_z(slope, tw.x1, tw.y1, &tw.z1top, &tw.z1bot);
      s_secWallCount++;
    }
    if (!staged) { s_secWallCount = first; fb = true; }
  } else if (plan > 0) {
    fb = true;
  }
  if (fallback) *fallback = fb;
  if (staged) return plan;

  // The curtain: one panel at the room's centre along its first wall (a sector has no angle). A room that can't be
  // measured draws nothing rather than a panel at a guessed height.
  if (s_secWallCount >= TRIGVOL_WALLS_MAX || !haveSlope) return 0;
  int ztop = 0, zbot = 0;
  sector_slope_z(slope, cx, cy, &ztop, &zbot);
  int ang = 0;
  if (wallptr >= 0 && wallptr < nw) {
    WorldWall w0, w1;
    if (world_wall(wallptr, &w0) && w0.point2 >= 0 && w0.point2 < nw
        && world_wall(w0.point2, &w1))
      ang = secretvol_curtain_angle(world_getangle(w1.x - w0.x, w1.y - w0.y));
  }
  trigvol_curtain(cx, cy, ang & 2047, TRIGVOL_CURTAIN_HALF, ztop, zbot,
                  sintable, &s_secWalls[s_secWallCount]);
  s_secWallCount++;
  return 1;
}

void secret_volume_tic() {
  // Off: no scan and no stale rooms. "Secret areas" is the parent, the sub-row the feature.
  const bool on = g_cfg.espsecrets != 0 && g_cfg.secretvolume != 0;
  if (!on || !world_ready()) {
    s_secCount = 0;
    s_secWallCount = 0;
    s_secScanned = false;
    s_secOn = on;
    return;
  }

  const int nsectors = world_numsectors();
  const int nwalls   = world_numwalls();
  if (nsectors <= 0 || nwalls <= 0) { s_secCount = 0; s_secWallCount = 0; return; }

  const unsigned sig = trigger_map_signature(nsectors, nwalls, 0);
  const unsigned now = wallclock_ms();
  // The switch joins the signature test so flipping "Secret volume" on shows rooms immediately.
  if (s_secScanned && sig == s_secSig && on == s_secOn &&
      (now - s_secLastScanMs) < TRIG_SCAN_PERIOD_MS) return;

  const int nsec = (nsectors < WORLD_MAXSECTORS) ? nsectors : WORLD_MAXSECTORS;
  const int nw   = (nwalls   < WORLD_MAXWALLS)   ? nwalls   : WORLD_MAXWALLS;
  const short* sintable = world_sintable();
  if (!sintable) { s_secCount = 0; s_secWallCount = 0; return; }

  s_secWallCount = 0;
  int count = 0, fallbacks = 0, nocentre = 0;
  bool overflow = false;
  for (int sct = 0; sct < nsec; sct++) {
    // -1 (a refused read) is never the secret lotag.
    if (!secretvol_wanted(world_sector_lotag(sct))) continue;
    if (count >= SECVOL_MAX_SECTORS) { overflow = true; break; }
    // The secret marker's centre, so panels and cross agree.
    int cx = 0, cy = 0, cz = 0;
    if (!world_sector_centre(sct, &cx, &cy, &cz)) { nocentre++; continue; }
    int wp = 0, wn = 0;
    if (!world_sector_wall_range(sct, &wp, &wn)) { nocentre++; continue; }
    bool fb = false;
    const int n = secvol_stage_room(sct, wp, wn, nw, cx, cy, sintable, &fb);
    if (fb) fallbacks++;
    if (n <= 0) continue;
    s_secFirst[count] = s_secWallCount - n;
    s_secN[count]     = n;
    s_secSect[count]  = sct;
    s_secCx[count]    = cx;
    s_secCy[count]    = cy;
    count++;
  }

  s_secSig = sig;
  s_secLastScanMs = now;
  s_secScanned = true;
  s_secOn = on;
  s_secCount = count;

  // s_secSect names WHICH rooms, for checking against the level's real secrets.
  static unsigned s_diagLast = 0;
  static bool     s_diagFirst = true;
  if (g_cfg.debuglog != 0 && trig_throttle_due(&s_diagLast, &s_diagFirst)) {
    char sects[SECVOL_MAX_SECTORS * 8];
    int at = 0;
    sects[0] = 0;
    for (int r = 0; r < s_secCount && at < (int)sizeof(sects) - 1; r++) {
      const int wrote = _snprintf_s(sects + at, sizeof(sects) - at, _TRUNCATE,
                                    r ? ",%d" : "%d", s_secSect[r]);
      if (wrote < 0) break;   // truncated: keep what fits and stop
      at += wrote;
    }
    wt_log("secretvol: sectors=%d walls=%d fallback=%d nocentre=%d overflow=%d sects=%s",
           s_secCount, s_secWallCount, fallbacks, nocentre, overflow ? 1 : 0, sects);
  }
}

// In-world producer: these panels are the room itself, depth-tested, vanishing behind walls. The layer takes WORLD
// COORDINATES and projects inside.
void secret_volume_collect() {
  if (!g_cfg.espsecrets || !g_cfg.secretvolume) return;
  if (!world_ready()) return;
  if (s_secCount <= 0 || s_secWallCount <= 0) return;

  int camx = 0, camy = 0, camz = 0;
  if (!project_camera_pos(&camx, &camy, &camz)) return;

  for (int r = 0; r < s_secCount && r < SECVOL_MAX_SECTORS; r++) {
    const int n = s_secN[r], first = s_secFirst[r];
    // A group outside what the last scan filled is skipped.
    if (n <= 0 || first < 0 || first > s_secWallCount - n) continue;
    // Asked THIS frame: the engine clears a sector's lotag the instant the player stands in it, so the panels would
    // otherwise linger up to a second.
    if (!secretvol_wanted(world_sector_lotag(s_secSect[r]))) continue;
    if (!trigger_end_in_range(trigger_world_dist2d(camx, camy, s_secCx[r], s_secCy[r]),
                              g_cfg.espdrawdist, g_cfg.espnolimit)) continue;

    for (int i = 0; i < n; i++) {
      const TrigWall& w = s_secWalls[first + i];
      WorldXY a, b;
      int aTop = 0, aBot = 0, bTop = 0, bBot = 0;
      if (!trigvol_quad_corners(w, &a, &b, &aTop, &aBot, &bTop, &bBot)) continue;
      // Wound top-left, top-right, bottom-right, bottom-left.
      WorldQuad3 q;
      q.p[0].x = a.x; q.p[0].y = a.y; q.p[0].z = aTop;
      q.p[1].x = b.x; q.p[1].y = b.y; q.p[1].z = bTop;
      q.p[2].x = b.x; q.p[2].y = b.y; q.p[2].z = bBot;
      q.p[3].x = a.x; q.p[3].y = a.y; q.p[3].z = aBot;
      q.rgba = C_SECRET_VOLUME;
      // A ROOM, not a coincident face: the tint's pull drags a distant room's far walls in front of near ones.
      q.pull = W3D_PULL_ROOM;
      if (!world3d_push_quad(q)) return;   // queue full
    }
  }
}

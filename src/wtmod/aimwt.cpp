// See aim.h. The aimbot's acquisition and its one write, from Megaton's aim.cpp. Decides nothing: the rules are in
// target.h/.cpp. aim_tic and aim_apply run from wt_processinput's tail AFTER the original, never on the render clock.
// No player auto_aim field here (autoAim is 0): silent aim holds autoaim off via camera.cpp's P_AIMMODE.
#include <windows.h>
#include <stdio.h>
#include "aim.h"
#include "target.h"
#include "trigger.h"      // trigger_world_dist2d: the shared 2D distance
#include "actorstate.h"   // actor_state: alive, dying and dead told apart
#include "world.h"
#include "esp_pics.h"     // esp_is_enemy: this port's badguy()
#include "esp_world.h"
#include "input.h"        // input_menu_open
#include "config.h"
#include "canvas.h"
#include "font.h"
#include "ui.h"           // UI_SCALE_DEFAULT
#include "hook.h"         // wt_log
#include "freecam.h"     // camera_active: the aim rules take it for real
#include "dbgdump.h"     // dbgdump_take: the probe report is on the player's key

static AimSnapshot s_pub = { -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, 0u };

static unsigned pub_seq_read() { return *(volatile unsigned*)&s_pub.seq; }
static void pub_seq_write(unsigned v) { *(volatile unsigned*)&s_pub.seq = v; }

static bool s_aimWroteLastTic = false;

// The SELECTED target's smoothed velocity across tics (one slot; aim_vel_update keys on (index, picnum)). Megaton
// solves from it, but here it made flying enemies WORSE, so the fold runs and the solve does not use it.
static VelSmooth s_vel = { -1, 0, 0, 0, 0, 0 };

static void publish_empty() {
  s_aimWroteLastTic = false;
  aim_vel_reset(&s_vel);
  unsigned seq = pub_seq_read();
  snap_begin(&seq);
  pub_seq_write(seq);
  s_pub.targetIndex = -1;
  s_pub.targetPicnum = 0;
  s_pub.targetRung = 0;
  s_pub.aimx = s_pub.aimy = s_pub.aimz = 0;
  s_pub.rawx = s_pub.rawy = s_pub.rawz = 0;
  s_pub.solvedAng = s_pub.solvedHoriz = 0;
  s_pub.leadScale = 0;
  s_pub.choice = 0;
  s_pub.los = false;
  snap_end(&seq);
  pub_seq_write(seq);
}

bool aim_snapshot(AimSnapshot* out) {
  if (!out) return false;
  const unsigned before = pub_seq_read();
  AimSnapshot tmp = s_pub;
  const unsigned after = pub_seq_read();
  if (!snap_read_ok(before, after)) return false;
  tmp.seq = before;
  *out = tmp;
  return true;
}

void aim_clear() { publish_empty(); }

static bool s_probeDumpArmed = false;
static int  s_probeDumpLines = 0;

// THIS TIC'S SAMPLE from the engine's own mover: the sprite's live position minus hittype[i]'s previous-tic bpos.
// EXACT, so the solve uses it; the smoothed average was strictly worse here.
static bool aim_raw_velocity(int index, int* vx, int* vy, int* vz) {
  if (!vx || !vy || !vz) return false;
  *vx = 0; *vy = 0; *vz = 0;
  WorldSprite sp;
  int bx = 0, by = 0, bz = 0;
  if (!world_sprite(index, &sp)) return false;
  if (!world_actor_bpos(index, &bx, &by, &bz)) return false;
  return target_velocity_from(sp.x, sp.y, sp.z, bx, by, bz, vx, vy, vz);
}

// A READ of the smoothed average, never a fold; kept because the diagnostic line prints it.
bool aim_smoothed_velocity(int index, int picnum, int* vx, int* vy, int* vz) {
  return vel_smooth_read(&s_vel, index, picnum, vx, vy, vz);
}

// The exact sample, guarded like the smoothed read: an (index, picnum) mismatch refuses, so a recycled slot can't
// inherit a dead actor's motion.
bool aim_live_velocity(int index, int picnum, int* vx, int* vy, int* vz) {
  if (!vx || !vy || !vz) return false;
  *vx = 0; *vy = 0; *vz = 0;
  WorldSprite sp;
  if (!world_sprite(index, &sp) || sp.picnum != picnum) return false;
  return aim_raw_velocity(index, vx, vy, vz);
}

// The hold key, from the window thread's view; armed in aim_tic so the binding follows the switch.
bool aim_hold_active() { return input_aim_held(); }

// DID THE AIMBOT'S WRITE LAND: read by the triggerbot and by hold view in getinput. Set at aim_apply's write site and
// CLEARED on every non-writing path (not rolled per tic): the readers run BEFORE this tic's aim_apply and so see last
// tic's write.
bool aim_view_held() { return s_aimWroteLastTic; }

static bool s_viewHeldThisTic = false;
void aim_view_hold_note(bool held) { s_viewHeldThisTic = held; }

// ---------------------------------------------------------------- acquisition
void aim_tic(const PlayerAim& p) {
  input_set_aim_key((g_cfg.aimon != 0 && g_cfg.aimhold != 0) ? g_cfg.aimbind : 0);
  {
    static bool lastHold = false;
    if (g_cfg.debuglog != 0 && s_viewHeldThisTic != lastHold)
      wt_log("aim: hold view %s", s_viewHeldThisTic ? "on, the aimbot owns the turn" : "off");
    lastHold = s_viewHeldThisTic;
  }
  // THE CAMERA TERM IS PASSED FOR REAL, not a literal false: it changes nothing today (aim_run_tic only runs when
  // !camera_active()), but a literal hides the rules' own refusal in one call site's `if`.
  s_probeDumpArmed = false;
  const bool valid = p.index >= 0 && p.spriteIndex >= 0;
  const bool aimbotWants =
      aim_should_run(g_cfg.aimon != 0, g_cfg.aimhold != 0, aim_hold_active(),
                     input_menu_open(), p.dead, valid, camera_active());
  if (!aim_acquire_should_run(aimbotWants, g_cfg.silenton != 0,
                              input_menu_open(), p.dead, valid, camera_active())) {
    publish_empty();
    return;
  }
  if (!world_ready()) { publish_empty(); return; }

  const WeaponInfo w = target_weapon_info(p.currWeapon);
  if (!aim_weapon_usable(w.cls)) { publish_empty(); return; }

  const AimPoint ap = aim_point_from_config(g_cfg.aimpoint);
  const TargetMode mode = aim_mode_from_config(g_cfg.aimtarget);

  const int shotz = aim_shot_origin_z(p.eyez, p.pyoff);

  Point3 eye;  eye.x = p.eyex;  eye.y = p.eyey;  eye.z = p.eyez;
  const int adx = aim_ahead_step(p.eyex, p.oposx, AIM_AHEAD_MAX_STEP);
  const int ady = aim_ahead_step(p.eyey, p.oposy, AIM_AHEAD_MAX_STEP);
  const int adz = aim_ahead_step(p.eyez, p.oposz, AIM_AHEAD_MAX_STEP_Z);
  const Point3 eyeAhead = aim_ahead_origin(eye, adx, ady, adz);
  const int shotzAhead = clamp_ll_to_int((long long)shotz + (long long)adz);

  TargetCandidate cands[AIM_MAX_CANDIDATES];
  int n = 0;
  int enemies = 0, losPassed = 0, inRangeCount = 0;
  int rejDead = 0, rejRange = 0, rejLos = 0, rejReach = 0;
  int diagDist = 0x7FFFFFFF, diagIndex = -1, diagRungs = 0;
  int diagSpriteZ = 0, diagAimZ = 0, diagFloorZ = 0;
  bool diagLos = false, diagReach = false;

  const int stats[2] = { WORLD_STAT_ACTOR, WORLD_STAT_ZOMBIEACTOR };
  for (int si = 0; si < 2 && n < AIM_MAX_CANDIDATES; si++) {
    WorldWalk walk;
    if (!world_walk_begin(&walk, stats[si])) continue;
    int i = 0;
    while (world_walk_next(&walk, &i) && n < AIM_MAX_CANDIDATES) {
      WorldSprite sp;
      if (!world_sprite(i, &sp)) continue;
      const bool badguy = esp_is_enemy(sp.picnum);
      if (!badguy) continue;
      enemies++;
      if (!target_hittable_rule(sp.xrepeat, sp.cstat)) continue;

      // LIVENESS IS actor_state's, not a health test (`extra > 0` ignored living enemies the ESP showed at 0 health):
      // `extra < 0` is dying/dead, `extra == 0` with hittable cstat is alive, a corpse is AS_DEAD. One computation
      // (actorstate.h), no cache.
      const ActorState state = actor_state_live(sp.index);
      const bool alive = actor_state_targetable(state);

      // WHICH STATE A REFUSAL WAS ("dead=1" names a count, not a reason): one line per (picnum, state), budgeted;
      // keyed on the pair, not the sprite index, which a long session fills with the first corpses.
      if (!alive && g_cfg.debuglog != 0) {
        static int  refusedPic[64] = {0};
        static int  refusedState[64] = {0};
        static int  refusedCount = 0;
        bool known = false;
        for (int k = 0; k < refusedCount; k++)
          if (refusedPic[k] == sp.picnum && refusedState[k] == (int)state) { known = true; break; }
        if (!known && refusedCount < 64) {
          refusedPic[refusedCount] = sp.picnum;
          refusedState[refusedCount] = (int)state;
          refusedCount++;
          wt_log("aim: refusing sprite %d (pic %d) -- state %s, statnum %d, "
                 "cstat 0x%04X, extra %d",
                 sp.index, sp.picnum, actor_state_name(state), sp.statnum,
                 (unsigned)(sp.cstat & 0xFFFF), sp.extra);
        }
      }

      int secCeil = 0, secFlor = 0;
      const bool haveSectorZ = world_sector_zrange(sp.sectnum, &secCeil, &secFlor);

      const int dist = trigger_world_dist2d(eyeAhead.x, eyeAhead.y, sp.x, sp.y);
      const bool inRange = aim_within_range(dist, aim_range_clamped(g_cfg.aimrange));

      // The probe ladder: aim at the lowest visible point, fall back toward the head only when the body is covered;
      // ORDER IS THE DECISION. It needs a REAL sprite height (junk tilesizy/yrepeat put rungs thousands of units off
      // and fail every sightline), else the sprite's own z.
      const int tsy = world_tilesizy(sp.picnum);
      int zs[AIM_PROBE_MAX];
      int nz = 0;
      if (tsy > 0 && sp.yrepeat > 0) {
        nz = aim_probe_zs(sp.z, sp.yrepeat, tsy, ap, zs, AIM_PROBE_MAX);
      }
      if (nz <= 0) { zs[0] = sp.z; nz = 1; }
      // No sector clamp on the rungs (it dropped legitimate head probes above the sector ceiling). cansee is the
      // engine's OWN and resolves the target's sector; asked ONLY of sprites the cheap terms accepted (forty enemies
      // stuttered a room).
      const bool worthProbing = alive && inRange && nz > 0;
      int aimz = 0, seen = -1;
      for (int k = 0; worthProbing && k < nz; k++) {
        if (!world_cansee(eyeAhead.x, eyeAhead.y, shotzAhead, p.cursectnum,
                          sp.x, sp.y, zs[k], sp.sectnum)) continue;
        aimz = zs[k];        // the probe that WON: an enemy behind cover is
        seen = k;            // aimed at its exposed part, not its centre
        break;               // first visible wins; the rest are not worth paying for
      }
      const bool los = seen >= 0;

      int rung = 0;
      if (los && tsy > 0 && sp.yrepeat > 0) {
        for (int rk = 0; rk < AIM_PROBE_MAX; rk++)
          if (target_probe_z(sp.z, sp.yrepeat, tsy, ap, rk) == aimz) { rung = rk; break; }
      }
      if (los) losPassed++;
      if (inRange) inRangeCount++;

      int vx = 0, vy = 0, vz = 0;
      aim_raw_velocity(sp.index, &vx, &vy, &vz);

      const bool grounded = haveSectorZ && aim_actor_grounded(sp.z, secFlor, AIM_GROUND_MARGIN);
      if (grounded) vz = aim_grounded_vz(sp.z, secFlor, vz, AIM_GROUND_MARGIN);

      // REACHABLE: a pitch the engine would clamp is not a shot. Solved on the RAW point with no lead and gated as a
      // candidate term, so the pick moves on to an enemy that CAN be hit.
      bool reachable = false;
      if (los) {
        Point3 pe;  pe.x = p.eyex;  pe.y = p.eyey;  pe.z = p.eyez;
        Point3 pa;  pa.x = sp.x;    pa.y = sp.y;    pa.z = aimz;
        const AimSolution probeSol = target_solve(pe, pa, p.horizoff, w);
        reachable = probeSol.valid && aim_horiz_reachable(probeSol.horiz);
      }

      // Probe report (dump key), from the Egg (675): rungs are multiples of the sprite's height, so a short sprite's
      // rungs hug its floor and cansee is a coin toss.
      if (g_cfg.debuglog != 0 && alive && inRange && s_probeDumpLines < 24) {
        static long lastProbeDump = 0;
        if (s_probeDumpArmed || dbgdump_take(&lastProbeDump)) {
          s_probeDumpArmed = true;
          s_probeDumpLines++;
          wt_log("aim probe: spr %d pic %d | yrep=%d tilesizy=%d h=%d | spritez=%d "
                 "floorz=%d (%s) | rungs=%d z=[%d,%d,%d] | aimz=%d los=%d reach=%d",
                 sp.index, sp.picnum, sp.yrepeat, tsy, sp.yrepeat * tsy,
                 sp.z, haveSectorZ ? secFlor : 0, haveSectorZ ? "read" : "UNREAD",
                 nz, nz > 0 ? zs[0] : 0, nz > 1 ? zs[1] : 0, nz > 2 ? zs[2] : 0,
                 aimz, los ? 1 : 0, reachable ? 1 : 0);
        }
      }

      // WHICH TERM REFUSED, counted rather than guessed, plus THE NEAREST ENEMY, ACCEPTED OR NOT, for one line a
      // second: the counters say which rule refused, the geometry says a wall from a muzzle under the floor.
      if (alive && inRange && dist < diagDist) {
        diagDist = dist;
        diagIndex = sp.index;
        diagLos = los;
        diagReach = reachable;
        diagRungs = nz;
        diagSpriteZ = sp.z;
        diagAimZ = aimz;
        diagFloorZ = haveSectorZ ? secFlor : 0;
      }

      if (!aim_candidate_ok(badguy, alive, inRange, los, reachable, alive)) {
        if (!alive) rejDead++;
        else if (!inRange) rejRange++;
        else if (!los) rejLos++;
        else if (!reachable) rejReach++;
        continue;
      }

      TargetCandidate& c = cands[n++];
      c.index = sp.index;  c.picnum = sp.picnum;
      c.x = sp.x;  c.y = sp.y;  c.z = sp.z;
      c.vx = vx;   c.vy = vy;   c.vz = vz;
      c.aimz = aimz;
      c.sectnum = sp.sectnum;
      c.dist2d = dist;
      c.aimzRung = rung;
      c.floorz = haveSectorZ ? secFlor : 0;
      c.grounded = grounded;
      c.extra = sp.extra;
    }
  }

  static unsigned diagLast = 0;
  if (g_cfg.debuglog != 0) {
    const unsigned now = (unsigned)GetTickCount();
    if (now - diagLast >= 1000) {
      diagLast = now;
      wt_log("aim: %d enemies, %d in range, %d with line of sight, %d candidates "
             "(weapon %d cls %d, sector %d) rejected: dead=%d range=%d los=%d reach=%d",
             enemies, inRangeCount, losPassed, n, p.currWeapon, (int)w.cls, p.cursectnum,
             rejDead, rejRange, rejLos, rejReach);
      // The nearest live enemy in range and the geometry the rules judged; shotz is the muzzle (eyez + pyoff + 0x400)
      // and Build z points DOWN, so a large walk bob can put cansee's origin inside the floor.
      if (diagIndex >= 0)
        wt_log("aim near: tgt=%d dist=%d los=%d reach=%d rungs=%d | spritez=%d aimz=%d "
               "floorz=%d | eyez=%d shotz=%d pyoff=%d horizoff=%d",
               diagIndex, diagDist, diagLos ? 1 : 0, diagReach ? 1 : 0, diagRungs,
               diagSpriteZ, diagAimZ, diagFloorZ, p.eyez, shotz, p.pyoff, p.horizoff);
    }
  }

  if (n <= 0) { publish_empty(); return; }

  const int cone = target_degrees_to_units(aim_cone_clamped(g_cfg.aimcone));
  const int pick = target_pick(cands, n, p.ang, eyeAhead.x, eyeAhead.y, mode, cone);
  if (pick < 0) { publish_empty(); return; }

  const TargetCandidate* t = 0;
  for (int k = 0; k < n; k++) if (cands[k].index == pick) { t = &cands[k]; break; }
  if (!t) { publish_empty(); return; }

  Point3 raw;  raw.x = t->x;  raw.y = t->y;  raw.z = t->aimz;

  // ONE TIC OF THE ENEMY'S OWN MOVEMENT, as applied to the origin: processinput and moveactors ran first. The
  // smoother folds but nothing solves from it (bpos is MEASURED, so the average only adds lag).
  int svx = 0, svy = 0, svz = 0;
  aim_vel_update(&s_vel, t->index, t->picnum, t->vx, t->vy, t->vz, &svx, &svy, &svz);

  const Point3 rawAhead = aim_ahead_target(raw, t->vx, t->vy, t->vz);

  {
    static unsigned velLast = 0;
    const unsigned now = (unsigned)GetTickCount();
    if (g_cfg.debuglog != 0 && now - velLast >= 1000) {
      velLast = now;
      wt_log("aim vel: tgt=%d pic=%d samples=%d raw=(%d,%d,%d) smooth=(%d,%d,%d) "
             "grounded=%d rawz=%d aheadz=%d",
             t->index, t->picnum, s_vel.samples, t->vx, t->vy, t->vz,
             svx, svy, svz, t->grounded ? 1 : 0, raw.z, rawAhead.z);
    }
  }

  const int leadVel = aim_lead_vel(w.vel, g_cfg.aimlead);
  const Point3 full = target_lead(eyeAhead, rawAhead, t->vx, t->vy, t->vz, leadVel);

  const int leadScale = aim_leadscale_clamped(g_cfg.aimleadscale);
  const Point3 led = target_lead_scaled(rawAhead, full, leadScale);

  // SOLVED FROM THE EYE, NOT THE MUZZLE: the probes cast from the muzzle but the view renders from the eye, and a
  // muzzle pitch aims above the enemy at close range (target.h).
  const AimSolution sol = target_solve(eyeAhead, led, p.horizoff, w);

  // The walk proved the RAW point reachable, so a led point the engine would clamp falls back to the ADVANCED raw
  // point rather than dropping the tic.
  AimSolution rawSol;
  rawSol.ang = 0; rawSol.horiz = 0; rawSol.valid = false;
  if (!(sol.valid && aim_horiz_reachable(sol.horiz)))
    rawSol = target_solve(eyeAhead, rawAhead, p.horizoff, w);

  const AimSolutionChoice choice =
      aim_choose_solution(sol.valid, sol.horiz, rawSol.valid, rawSol.horiz);
  if (choice == ASC_NONE) { publish_empty(); return; }
  const bool useLed = (choice == ASC_LED);
  const AimSolution& use = useLed ? sol : rawSol;
  const Point3& aimPt = useLed ? led : rawAhead;


  unsigned seq = pub_seq_read();
  snap_begin(&seq);
  pub_seq_write(seq);
  s_pub.targetPicnum = t->picnum;
  s_pub.targetRung = t->aimzRung;
  s_pub.aimx = aimPt.x;  s_pub.aimy = aimPt.y;  s_pub.aimz = aimPt.z;
  s_pub.rawx = raw.x;    s_pub.rawy = raw.y;    s_pub.rawz = raw.z;
  s_pub.solvedAng = use.ang;
  s_pub.solvedHoriz = use.horiz;
  s_pub.leadScale = leadScale;
  s_pub.choice = (int)choice;
  s_pub.los = true;
  s_pub.targetIndex = t->index;   // last: the draw reads the rest under it
  snap_end(&seq);
  pub_seq_write(seq);
}

// ------------------------------------------------------------ the one write
// Turns the player toward what aim_tic published, smoothed; called only from wt_processinput's tail right after
// aim_tic. Returns whether it wrote.
bool aim_apply(const PlayerAim& p, short* pAng, int* pHoriz) {
  // Every refusal clears the term too, or a stale "the aimbot steered" sends the triggerbot's ray down the view on
  // the tic silent aim shot elsewhere.
  if (!pAng || !pHoriz) { s_aimWroteLastTic = false; return false; }
  const bool valid = p.index >= 0 && p.spriteIndex >= 0;
  if (!aim_should_run(g_cfg.aimon != 0, g_cfg.aimhold != 0, aim_hold_active(),
                      input_menu_open(), p.dead, valid, camera_active())) {
    s_aimWroteLastTic = false;
    return false;
  }
  if (s_pub.targetIndex < 0 || !s_pub.los) { s_aimWroteLastTic = false; return false; }

  const int smooth = aim_smooth_clamped(g_cfg.aimsmooth);
  const int wantAng = target_smooth_ang((int)*pAng, s_pub.solvedAng, smooth);
  const int wantHoriz = target_smooth_horiz(*pHoriz, s_pub.solvedHoriz, smooth);
  *pAng = (short)(wantAng & 2047);
  *pHoriz = wantHoriz;
  s_aimWroteLastTic = true;
  return true;
}

// ------------------------------------------------------------ the marker
// Two points: the RAW body point (hollow square) and the point aimed at after lead (filled). Measurements:
// esp_layout.h.
void aim_draw_marker(int screenW, int screenH) {
  if (!aim_marker_on(g_cfg.aimon, g_cfg.silenton, g_cfg.aimdebug)) return;
  if (screenW <= 0 || screenH <= 0) return;
  if (!project_frame_begin()) return;

  AimSnapshot as;
  if (!aim_snapshot(&as) || as.targetIndex < 0) return;

  ScreenPt rawPt, aimPt;
  const bool haveRaw = project_world_to_screen(as.rawx, as.rawy, as.rawz, &rawPt);
  const bool haveAim = project_world_to_screen(as.aimx, as.aimy, as.aimz, &aimPt);
  if (!haveRaw && !haveAim) return;

  const unsigned colour = canvas_aim_colour();
  canvas_begin(screenW, screenH);
  canvas_set_scale(UI_SCALE_DEFAULT);

  if (haveRaw) {
    const int bx = aim_marker_x(rawPt.x), by = aim_marker_y(rawPt.y);
    const int span = aim_marker_span();
    canvas_rect(bx, by, span, AIM_MARKER_T, colour);                            // top
    canvas_rect(bx, aim_marker_edge_far(rawPt.y), span, AIM_MARKER_T, colour);  // bottom
    canvas_rect(bx, by, AIM_MARKER_T, span, colour);                            // left
    canvas_rect(aim_marker_edge_far(rawPt.x), by, AIM_MARKER_T, span, colour);  // right
  }

  if (haveAim) {
    canvas_rect(aim_marker_x(aimPt.x), aim_marker_y(aimPt.y),
                aim_marker_span(), aim_marker_span(), colour);
    char lab[32];
    _snprintf_s(lab, sizeof(lab), _TRUNCATE, "%s %d", AIM_MARKER_LABEL, as.targetIndex);
    canvas_text(esp_label_x(aimPt.x, 0, canvas_text_width(lab, FONT_SMALL)),
                aim_marker_label_y(aimPt.y), lab, colour, FONT_SMALL);
  }
  canvas_end();
}

// Silent aim: the shot goes where the aimbot solved and the VIEW never moves (from Megaton's silent.cpp). The engine
// builds a shot's direction from the player's ang and horiz inside shoot(), so they are substituted for exactly that
// call and restored. shoot runs BEFORE aim_tic publishes this tic's snapshot, so the published pair is one tic stale.
#include <windows.h>
#include <math.h>
#include "silent.h"
#include "aim.h"
#include "target.h"
#include "actorstate.h"
#include "world.h"
#include "tbot.h"
#include "trigger.h"
#include "accuracywt.h"
#include "camera.h"
#include "esp_pics.h"
#include "config.h"
#include "project.h"
#include "firerate.h"
#include "aimdiag.h"
#include "wallclock.h"
#include "hook.h"

typedef void (__fastcall* ShootFn)(short i, short atwith);
static ShootFn s_origShoot = 0;

// shoot is FUN_00724f00, confirmed by its body: picnum == APLAYER (CMP AX,0x57D), stride 0x408, posx/y/z
// +0x214/218/21C, pyoff +0x244 plus a 0x400 z bias, ang +0x2CC. FUN_006ee810 (a signature and caller match) is SPAWN:
// hooking it moved no bullets.
static const unsigned kRvaShoot = 0x74F00;
// PUSH EBP; MOV EBP,ESP; AND ESP,-8: six bytes, the first instruction boundary past a five-byte JMP, none position
// dependent.
static const unsigned char kFpShoot[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8 };
static const unsigned kShootPrologue = 6;

static unsigned s_redirectLogCount = 0;

// RE-ENTRANCY: shoot calls checkhitsprite, which calls shoot back with a blood picnum that can carry the player's own
// sprite index. Megaton filters on atwith tiles, but this fork renumbered them (the pistol arrives as 0x9E5, not
// 0xA23). A depth counter refuses exactly the nested calls for every weapon.
static int s_shootDepth = 0;

// The one place the miss exists as a value (from Megaton's aim_shot_diag), read with shoot's own ray (tbot_ray) as the
// shot leaves. angerr near zero with a clean sprite hit means something downstream is wrong; a large angerr, the solve
// or substitution; a hitscan stopping on a wall, geometry.
static unsigned s_shotLines = 0;
static const unsigned kShotLinesMax = 60;

static int shot_build_angle(int dx, int dy) {
  if (dx == 0 && dy == 0) return 0;
  const double a = atan2((double)dy, (double)dx) * 1024.0 / 3.14159265358979323846;
  int v = (int)(a < 0 ? a - 0.5 : a + 0.5);
  return v & 2047;
}

// Why silent aim declined, in silent_decide's own order so it names the term that refused. With aimon=0 a declined
// redirect looks to the player like the bot firing early.
static const char* silent_why_not(bool haveP, bool isLocalPlayerShot,
                                  bool haveSnap, const AimSnapshot& snap) {
  if (!haveP) return "noplayer";
  if (!isLocalPlayerShot) return "notlocal";
  if (g_cfg.silenton == 0) return "off";
  if (!haveSnap) return "nosnap";
  if (!silent_snapshot_usable(snap.targetIndex, snap.solvedAng, snap.solvedHoriz))
    return "unusable";
  if (!snap.los) return "nolos";
  return "cone";   // the max-angle cone is the only term left
}

static void shot_diag(const PlayerAim& p, int atwith, bool redirect, int useAng,
                      int useHoriz, const AimSnapshot& snap, bool haveSnap, bool live,
                      const char* silentWhy, bool isLocalPlayerShot) {
// The shooter has to be the player: wt_shoot is top-level for every actor's shoot() too, and a gate asking only whether
// the bot fired this tic logged enemy fire as lines built from the player's ray.
  if (!isLocalPlayerShot) return;
// The ledger is its own switch: debuglog keeps the sixty-line budget; debugaim drops it and rate-limits instead.
  if (g_cfg.debuglog == 0 && g_cfg.debugaim == 0) return;
  if (g_cfg.debugaim == 0 && s_shotLines >= kShotLinesMax) return;
  if (!redirect && !tbot_fired_last_tic() && !aim_view_held()) return;

  const short* sin = world_sintable();
  if (!sin) return;

  short liveAng = 0; int liveHoriz = 0;
  camera_player_read_view(&liveAng, &liveHoriz);
  const int ang = (int)(unsigned short)liveAng & 2047;

  int vx = 0, vy = 0, vz = 0;
  tbot_ray(ang, liveHoriz, p.horizoff, sin, &vx, &vy, &vz);
  const int sz = aim_shot_origin_z(p.eyez, p.pyoff);

  int hs = -1, hw = -1, hp = -1, hx = 0, hy = 0, hz = 0;
  const bool hitOk = world_hitscan(p.eyex, p.eyey, sz, p.cursectnum, vx, vy, vz,
                                   &hs, &hw, &hp, &hx, &hy, &hz);
  const int hitDist = hitOk ? trigger_world_dist2d(p.eyex, p.eyey, hx, hy) : -1;

  int exact = -1, angerr = 0, fwd = 0, lat = 0, vert = 0, tExtra = 0, tStat = -1;
  unsigned tCstat = 0;
  const int target = haveSnap ? snap.targetIndex : -1;
  WorldSprite t;
  const bool haveT = target >= 0 && world_sprite(target, &t);
  if (haveT) {
    exact = shot_build_angle(t.x - p.eyex, t.y - p.eyey);
    angerr = aim_shot_angerr(ang, exact);
    fwd = shot_forward(p.eyex, p.eyey, ang, t.x, t.y, sin);
    lat = shot_lateral(p.eyex, p.eyey, ang, t.x, t.y, sin);
    const int tsy = world_tilesizy(t.picnum);
    const int centre = (tsy > 0 && t.yrepeat > 0)
                     ? target_probe_z(t.z, t.yrepeat, tsy, AP_CENTRE, 0) : t.z;
    vert = shot_vertical(sz, vz, fwd, centre);
    tExtra = t.extra; tStat = t.statnum; tCstat = (unsigned)(t.cstat & 0xFFFF);
  }

// Did it land, and if not early or wild, measured against the box the ESP draws so the verdict matches what the player
// saw. The ray z at the target's distance is centre + vert; the box is compared (a cstat-centred sprite is asymmetric).
  const TbotLast& tb = tbot_last();
  const char* verdict = tb.firing ? "notarget" : "manual";
  if (haveT) {
    const SpriteExtents ext = proj_sprite_extents(t.z, t.cstat,
                                                  world_tilesizx(t.picnum),
                                                  world_tilesizy(t.picnum),
                                                  t.xrepeat, t.yrepeat);
    const int tsy2 = world_tilesizy(t.picnum);
    const int centre2 = (tsy2 > 0 && t.yrepeat > 0)
                      ? target_probe_z(t.z, t.yrepeat, tsy2, AP_CENTRE, 0) : t.z;
    const int rayZ    = centre2 + vert;
    const int boxMid  = (ext.topZ + ext.bottomZ) / 2;
    const int halfH   = (ext.bottomZ - ext.topZ) / 2;
// Scored only when aimed at this target: a crosshair-mode shot must not be scored against a sprite it never pointed at.
// The triggerbot's own ray counts as aimed (tb.raySprite), else EARLY and WILD were unreachable with the aimbot not
// steering.
    const bool aimedHere = redirect || aim_view_held() ||
                           (tb.firing && target >= 0 && tb.raySprite == target);
// `hs < 0` meant both "struck a wall" and "the hitscan refused"; hitOk separates them.
    const bool onBox = aimedHere &&
        aimdiag_aim_on_box(lat, rayZ - boxMid, ext.halfWidth, halfH) &&
        (!hitOk || hp == target);   // and the engine's own hitscan agrees
    verdict = aimedHere ? aimdiag_shot_verdict(tb.firing != 0, onBox, tb.heldTics)
                        : "elsewhere";
  }
  if (g_cfg.debugaim != 0) tbot_note_shot(verdict);

// Anomalies always, ordinary shots once a second (a held chaingun at firerate 400 is several shots a tic). Both
// bounded: wt_log opens, writes and closes per call on the game thread, so anomalies get their own faster budget.
  static unsigned s_lastOrdinaryMs = 0;
  static unsigned s_lastAnomalyMs = 0;
  const unsigned nowMs = wallclock_ms();
  if (aimdiag_verdict_is_anomaly(verdict)) {
    if (s_shotLines != 0 && (unsigned)(nowMs - s_lastAnomalyMs) < 100) return;
    s_lastAnomalyMs = nowMs;
  } else {
    if (s_shotLines != 0 && (unsigned)(nowMs - s_lastOrdinaryMs) < 1000) return;
    s_lastOrdinaryMs = nowMs;
  }

  const FireRateLast& fr = firerate_last();
  s_shotLines++;
  wt_log("shot #%u %s via=%s atwith=0x%X redirect=%d live=%d silent=%s "
         "| held=%d/%d reason=%s | ang=%d horiz=%d horizoff=%d pyoff=%d "
         "| pub=(%d,%d) use=(%d,%d) | tgt=%d exact=%d angerr=%d fwd=%d lat=%d "
         "vert=%d stat=%d cstat=0x%04X extra=%d | hs=%d hp=%d hw=%d hd=%d "
         "| firerate w%d kick %d->%d steps=%d carry=%d %s | step=(%d,%d,%d)",
         s_shotLines, verdict,
         redirect ? "silent" : (aim_view_held() ? "aim" : "tbot"),
         (unsigned)(unsigned short)atwith, redirect ? 1 : 0, live ? 1 : 0,
         redirect ? "yes" : (silentWhy ? silentWhy : "?"),
         tb.heldTics, tb.delayTics, tb.reason ? tb.reason : "?",
         ang, liveHoriz, p.horizoff, p.pyoff,
         haveSnap ? snap.solvedAng : -1, haveSnap ? snap.solvedHoriz : -1,
         useAng, useHoriz,
         target, exact, angerr, fwd, lat, vert, tStat, tCstat, tExtra,
         hs, hp, hw, hitDist,
         fr.weapon, fr.kickbackIn, fr.kickbackOut, fr.steps, fr.carryMilli,
         fr.acted ? "acted" : (fr.why ? fr.why : "?"),
         p.eyex - p.oposx, p.eyey - p.oposy, p.eyez - p.oposz);
}

static void __fastcall wt_shoot(short i, short atwith) {
  ShootFn orig = s_origShoot;
  while (!orig) { YieldProcessor(); orig = s_origShoot; }

  // A nested call is never the player's own shot: pass it through and keep the depth balanced on every path out.
  if (s_shootDepth > 0) { orig(i, atwith); return; }

  PlayerAim p;
  const bool haveP = camera_build_player_aim(&p);

  AimSnapshot snap;
  // Explicit init, never a valid-looking zero: target index 0 is a real sprite and a failed seqlock read must not look
  // like last tic's answer.
  snap.targetIndex = -1; snap.solvedAng = 0; snap.solvedHoriz = 0;
  snap.leadScale = 0; snap.choice = 0; snap.los = false;
  snap.targetPicnum = 0; snap.targetRung = 0;
  const bool haveSnap = aim_snapshot(&snap);

  const bool isLocalPlayerShot = haveP && p.spriteIndex >= 0 && (int)i == p.spriteIndex;
  const bool atwithIsPlayerShot = true;
  (void)atwith;

  const bool redirect = haveP && silent_decide(
      g_cfg.silenton != 0, isLocalPlayerShot, atwithIsPlayerShot,
      haveSnap && silent_snapshot_usable(snap.targetIndex, snap.solvedAng, snap.solvedHoriz),
      haveSnap && snap.los, p.ang, snap.solvedAng,
      target_degrees_to_units(silent_maxangle_clamped(g_cfg.silentmaxangle)));

  if (!redirect) {
// Perfect accuracy runs on this path too: a player firing normally, with aimbot and silent aim off, still gets spread
// removed per weapon row. The bracket is armed just before the one call and released just after, so .text is never left
// patched.
    shot_diag(p, (int)atwith, false, p.ang, p.horiz, snap, haveSnap, false,
              silent_why_not(haveP, isLocalPlayerShot, haveSnap, snap),
              isLocalPlayerShot);
    accuracy_arm_for_shot(atwith, isLocalPlayerShot);
    s_shootDepth++;
    orig(i, atwith);
    s_shootDepth--;
    accuracy_release_after_shot();
    return;
  }

// Re-solve from where the target is NOW, since the published pair is one tic stale; fall back to it on any mismatch.
// The fallback is muzzle-solved too: the published pair is solved from the eye (aimwt.cpp), which kept the pyoff + 1024
// low bias.
  const Point3 shotOrigin = { p.eyex, p.eyey, aim_shot_origin_z(p.eyez, p.pyoff) };
  int pubAng = snap.solvedAng, pubHoriz = snap.solvedHoriz;
  if (haveSnap) {
    Point3 pubAim;  pubAim.x = snap.aimx;  pubAim.y = snap.aimy;  pubAim.z = snap.aimz;
    const AimSolution pubSol =
        target_solve(shotOrigin, pubAim, p.horizoff, target_weapon_info(p.currWeapon));
    if (pubSol.valid && aim_horiz_reachable(pubSol.horiz)) {
      pubAng = pubSol.ang;  pubHoriz = pubSol.horiz;
    }
  }

  int useAng = pubAng, useHoriz = pubHoriz;
  bool live = false, coneRejected = false;

// Identity first: a Build slot is recycled the instant its actor dies, so the index may be a different actor, and the
// same picnum can be a corpse, so the state is checked too.
  WorldSprite live_sp;
  if (world_sprite(snap.targetIndex, &live_sp) &&
      live_sp.picnum == snap.targetPicnum &&
      actor_state_targetable(actor_state_live(snap.targetIndex))) {
// The same rung the probe ladder chose, not rung 0: cansee may have rejected rung 0 for a partly covered target, and
// substituting it would aim into the cover.
    Point3 liveRaw;
    liveRaw.x = live_sp.x;
    liveRaw.y = live_sp.y;
    liveRaw.z = target_probe_z(live_sp.z, live_sp.yrepeat, world_tilesizy(live_sp.picnum),
                               aim_point_from_config(g_cfg.aimpoint), snap.targetRung);

// The same sample the aimbot solved with, read live: the smoothed average was measurably wrong (a z of -28 against a
// raw +110), so lead built on it aims where the target is not. Zeroes on refusal, which makes lead a no-op.
    int vx = 0, vy = 0, vz = 0;
    aim_live_velocity(snap.targetIndex, snap.targetPicnum, &vx, &vy, &vz);

// From the muzzle, where this bullet leaves. The aimbot solves from the EYE on purpose, but nobody looks along silent
// aim's pair; solving from the eye and firing from the muzzle gives a parallel line pyoff + 1024 lower (up to 28% of a
// pigcop's height).
    const Point3 eye = shotOrigin;
    const WeaponInfo w = target_weapon_info(p.currWeapon);
    const SilentResolve rs = silent_resolve(
        eye, p.horizoff, w, pubAng, pubHoriz,
        liveRaw, vx, vy, vz,
        aim_lead_vel(w.vel, g_cfg.aimlead),
        aim_leadscale_clamped(g_cfg.aimleadscale),
        p.ang, target_degrees_to_units(silent_maxangle_clamped(g_cfg.silentmaxangle)));
    useAng = rs.ang;
    useHoriz = rs.horiz;
    live = rs.live;
    coneRejected = rs.coneRejected;
  }

// Save, substitute, call, restore (unconditional). Five fields: ang/horiz (what shoot reads), their oang/ohoriz
// partners and aim_mode = 2, this fork's only way to hold the engine's autoaim off (camera.cpp has the disassembly),
// else aim() drags the shot onto an enemy near the REAL crosshair.
  ShotView save;
  const bool armed = camera_player_read_shot_view(&save) &&
                     camera_player_substitute_shot_view((short)(useAng & 2047), useHoriz);

  if (armed && g_cfg.debuglog != 0 && s_redirectLogCount < 10) {
    s_redirectLogCount++;
    wt_log("silent: redirect #%u atwith=0x%X target=%d live=%d%s ang %d->%d (pub %d) "
           "horiz %d->%d (pub %d) leadscale=%d choice=%d",
           s_redirectLogCount, (unsigned)(unsigned short)atwith, snap.targetIndex,
           live ? 1 : 0, coneRejected ? " cone" : "",
           (int)save.ang, useAng, snap.solvedAng,
           save.horiz, useHoriz, snap.solvedHoriz, snap.leadScale, snap.choice);
  }

  shot_diag(p, (int)atwith, true, useAng, useHoriz, snap, haveSnap, live,
            coneRejected ? "cone-fallback" : "yes", isLocalPlayerShot);

// Both brackets wrap the same call, the accuracy one innermost: the view substitution must be in place first, because
// the pistol's sites only run when the engine's autoaim found nothing, which the substituted aim_mode ensures.
  accuracy_arm_for_shot(atwith, isLocalPlayerShot);
  s_shootDepth++;
  orig(i, atwith);
  s_shootDepth--;
  accuracy_release_after_shot();

  if (armed) camera_player_restore_shot_view(save);
}

bool silent_install() {
  void* tramp = 0;
  if (!hook_install(kRvaShoot, kFpShoot, (unsigned)sizeof(kFpShoot),
                    kShootPrologue, (void*)wt_shoot, &tramp) || !tramp) {
    wt_log("silent: shoot did NOT hook at rva 0x%X; silent aim stays inert", kRvaShoot);
    return false;
  }
  s_origShoot = (ShootFn)tramp;
  wt_log("silent: shoot hooked at rva 0x%X (FUN_006ee810), called by processinput and the "
         "actor code alike, which is how it was identified. It carries silent aim "
         "(silenton=%d) and perfect accuracy (p=%d c=%d s=%d e=%d)",
         kRvaShoot, g_cfg.silenton,
         g_cfg.accpistol, g_cfg.accchaingun, g_cfg.accshotgun, g_cfg.accexpander);
  return true;
}

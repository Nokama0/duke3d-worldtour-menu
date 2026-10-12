// See tbot.h. The triggerbot: decide, then OR the fire bit into this tic's input record (from Megaton's tbot.cpp). One
// write, and it is an input record, not player state: the player's own fire bit, never CLEARED (they may hold the key).
#include <windows.h>
#include "tbot.h"
#include "aim.h"
#include "target.h"
#include "actorstate.h"
#include "world.h"
#include "esp_pics.h"     // esp_is_enemy
#include "trigger.h"      // trigger_world_dist2d
#include "input.h"        // input_menu_open, input_set_fire_bit
#include "config.h"
#include "hook.h"         // wt_log
#include "wallclock.h"    // wallclock_ms: the stuck-target report's throttle
#include "dbgdump.h"      // dbgdump_take: the gate probe is on the player's key
#include "aimdiag.h"      // the ledger's rules: jumpiness, EARLY vs WILD

static TbotState s_state = { 0, false };
static bool s_firedLastTic = false;
static TbotLast s_last = { 0, 0, 0, -1, -1, "never ran" };
const TbotLast& tbot_last() { return s_last; }
static AimJump s_jump = { 0, 0, 0, 0, 0, 0, 0, -1, 0, { 0 }, 0 };
void tbot_note_shot(const char* verdict) { aimjump_shot(&s_jump, verdict); }

bool tbot_fired_last_tic() { return s_firedLastTic; }

struct TbotRay { int hitsprite, hitwall, hitsect, hitz, dist2d; };

// The direction is tbot_ray's (tbot.h), which carries the HORIZOFF term shoot builds zvel with; the free camera's ray
// drops it, but the player's is nonzero exactly while moving, so the ray pitched wrong on walking tics and the bot went
// quiet.
static void tbot_cast(int x, int y, int z, int sect, int ang, int horiz, int horizoff,
                      int shooterSprite, TbotRay* out) {
  TbotRay r = { -1, -1, -1, 0, 0 };
  *out = r;
  const short* sin = world_sintable();
  if (!sin) return;
  int vx = 0, vy = 0, vz = 0;
  tbot_ray(ang, horiz, horizoff, sin, &vx, &vy, &vz);

  // THE SHOOTER IS NOT COVER: shoot clears its sprite's 0x0101 around its hitscan and this ray starts at the same
  // muzzle, so without the bracket it stops on the player (seen: rdist 0 with the target 3400 away). Restored on every
  // path out.
  int savedCstat = 0;
  const bool masked = shooterSprite >= 0 &&
                      world_sprite_clear_blocking(shooterSprite, &savedCstat);

  int hs = -1, hw = -1, hp = -1, hx = 0, hy = 0, hz = 0;
  const bool ok = world_hitscan(x, y, z, sect, vx, vy, vz, &hs, &hw, &hp, &hx, &hy, &hz);

  if (masked) world_sprite_restore_cstat(shooterSprite, savedCstat);
  if (!ok) return;
  out->hitsprite = hp;
  out->hitwall = hw;
  out->hitsect = hs;
  out->hitz = hz;
  out->dist2d = trigger_world_dist2d(x, y, hx, hy);
}

// What the engine itself would BREAK: a ray stopping on a vent, window or breakable prop is not blocked (the first
// bullet opens it). Rules are tbot.h's, from checkhitwall/checkhitsprite; an earlier build hardcoded this false.
static bool tbot_ray_hits_breakable(const TbotRay& r, bool rayHitsEnemy) {
  if (r.hitwall >= 0) {
    WorldWall w;
    if (world_wall(r.hitwall, &w)) {
      int nf = 0, nc = 0;
      const bool haveNext = w.nextsector >= 0 &&
                            world_sector_zrange(w.nextsector, &nc, &nf);
      if (tbot_wall_breakable(w.cstat, w.overpicnum,
                              haveNext ? w.nextsector : -1, r.hitz, nf, nc))
        return true;
    }
  }
  if (!rayHitsEnemy && r.hitsprite >= 0) {
    WorldSprite sp;
    if (world_sprite(r.hitsprite, &sp) && tbot_sprite_breakable(sp.picnum)) return true;
  }
  return false;
}

// What the ray struck, classified as the aimbot does so they cannot disagree; badguy and alive are REPORTED SEPARATELY.
static void tbot_classify_sprite(int index, int eyex, int eyey, bool splashCapable,
                                 bool* badguyOut, bool* aliveOut, int* dist2dOut) {
  *badguyOut = false;
  *aliveOut = false;
  *dist2dOut = 0;
  if (index < 0) return;
  WorldSprite sp;
  if (!world_sprite(index, &sp)) return;
  *dist2dOut = trigger_world_dist2d(eyex, eyey, sp.x, sp.y);
  const bool badguy = esp_is_enemy(sp.picnum);
  *badguyOut = badguy;
  if (!badguy) return;
  // Through the ONE computation, not a hand-rolled actor_state (that left the Commander's death record read by the ESP
  // and nothing that shoots). actor_state_live carries the sitting-trooper rule and needs no cache, so it is safe on
  // this clock.
  *aliveOut = actor_state_targetable(actor_state_live(index));

  // The bullet must be able to TOUCH it: a sleeper at any angle but zero is neither woken nor credited
  // (actor_damage_lands), so the bot holds fire and opens the tic it wakes. Here, not in actor_state: the AIMBOT keeps
  // aiming.
  if (*aliveOut &&
      !actor_damage_lands(sp.picnum, sp.statnum, sp.ang, sp.xrepeat, splashCapable)) {
    *aliveOut = false;
    const int heldKey = sp.picnum * 4 + (sp.statnum == TARGET_STAT_ZOMBIEACTOR ? 2 : 0) +
                        (sp.xrepeat < ACTOR_FIREFLY_FULL_XREPEAT ? 1 : 0);
    static int  heldPic[16] = {0};
    static int  heldCount = 0;
    bool heldKnown = false;
    for (int k = 0; k < heldCount; k++) if (heldPic[k] == heldKey) { heldKnown = true; break; }
    if (g_cfg.debuglog != 0 && !heldKnown && heldCount < 16) {
      heldPic[heldCount++] = heldKey;
      wt_log("tbot: holding fire on sprite %d (pic %d) -- statnum %d, ang %d, "
             "xrepeat %d; the engine discards the hit rather than recording it",
             index, sp.picnum, sp.statnum, sp.ang, sp.xrepeat);
    }
  }
}

void tbot_tic(const PlayerAim& p, bool cameraActive, bool paused) {
  const int when = tbot_when_clamped(g_cfg.tbotwhen);
  const WeaponInfo w = target_weapon_info(p.currWeapon);
  const bool weaponUsable = aim_weapon_usable(w.cls);
  // Cheap refusals first, so a full engine hitscan every tic is not spent with the panel open; tbot_decide names the
  // real reason.
  const bool cheapRefusal = g_cfg.tboton == 0 || input_menu_open() || paused
                         || p.dead || cameraActive || !weaponUsable;

  // The shot's own ray: crosshair mode's whole condition, aimbot mode's confirmation. With silent aim on and no
  // steering the shot leaves along silent aim's re-solve inside shoot (not yet run), so the snapshot's solved pair
  // stands in.
  AimSnapshot snapForRay;
  const bool haveSnapForRay = !cheapRefusal && aim_snapshot(&snapForRay);
  const int target = haveSnapForRay ? snapForRay.targetIndex : -1;
  const bool aimSteering = aim_view_held();
  const bool solvedRay = !aimSteering && g_cfg.silenton != 0 && target >= 0;
  const int rayAng = solvedRay ? snapForRay.solvedAng : p.ang;
  int rayHoriz = solvedRay ? snapForRay.solvedHoriz : p.horiz;

  const bool castRay = !cheapRefusal &&
                       (when == TBOT_WHEN_CROSSHAIR || target >= 0);
  // FROM WHERE THE SHOT WILL LEAVE: this hook runs in getinput a tic before processinput moves and fires, and
  // acquisition solved from the ADVANCED origin (aim_tic's eyeAhead). Un-advanced, the lateral miss swung -164..-326 at
  // ~230 units/tic.
  const int adx = aim_ahead_step(p.eyex, p.oposx, AIM_AHEAD_MAX_STEP);
  const int ady = aim_ahead_step(p.eyey, p.oposy, AIM_AHEAD_MAX_STEP);
  const int adz = aim_ahead_step(p.eyez, p.oposz, AIM_AHEAD_MAX_STEP_Z);
  Point3 eye;  eye.x = p.eyex;  eye.y = p.eyey;  eye.z = p.eyez;
  const Point3 eyeAhead = aim_ahead_origin(eye, adx, ady, adz);

  // THE PITCH IS RE-SOLVED FROM THE MUZZLE: solvedHoriz is solved from the EYE (target.h) but the ray leaves the muzzle
  // (aim_shot_origin_z = shoot's sz), 1024 units lower (Build z runs down), so it meets the floor sooner
  // (hitz=-102400).
  if (solvedRay) {
    Point3 muzzle;
    muzzle.x = eyeAhead.x; muzzle.y = eyeAhead.y;
    muzzle.z = aim_shot_origin_z(p.eyez, p.pyoff);
    Point3 aim;
    aim.x = snapForRay.aimx; aim.y = snapForRay.aimy; aim.z = snapForRay.aimz;
    const AimSolution muzzleSol =
        target_solve(muzzle, aim, p.horizoff, target_weapon_info(p.currWeapon));
    if (muzzleSol.valid) rayHoriz = muzzleSol.horiz;
  }
  // THE MUZZLE'S Z IS NOT ADVANCED: advancing z pushed the origin through floors and ceilings while jumping or falling
  // (cansee tolerates that, hitscan does not), so acquisition may advance all three axes and this may not.
  const int shotzAhead = aim_shot_origin_z(p.eyez, p.pyoff);

  TbotRay ray = { -1, -1, -1, 0, 0 };
  if (castRay) {
    tbot_cast(eyeAhead.x, eyeAhead.y, shotzAhead, p.cursectnum,
              rayAng, rayHoriz, p.horizoff, p.spriteIndex, &ray);
    // THE ADVANCE MOVES THE ORIGIN BUT NOT THE SECTOR, and Build's hitscan ends at once outside the sector given;
    // updatesector is not located, so a degenerate answer falls back to the un-advanced origin (one tic of lateral
    // error beats nothing).
    const bool degenerate = ray.hitsprite < 0 && ray.hitwall < 0 && ray.dist2d <= 0;
    if (degenerate)
      tbot_cast(p.eyex, p.eyey, shotzAhead, p.cursectnum,
                rayAng, rayHoriz, p.horizoff, p.spriteIndex, &ray);
  }

  bool rayBadguy = false, rayAlive = false;
  int spriteDist = 0;
  // Range from the advanced origin too, as acquisition measures. The weapon decides whether either discard applies:
  // both are in checkhitsprite, which a hitradius blast never enters; the SPLASH question, not the class (a
  // projectile's impact enters it).
  tbot_classify_sprite(ray.hitsprite, eyeAhead.x, eyeAhead.y,
                       weapon_escapes_hit_discards(p.currWeapon), &rayBadguy, &rayAlive,
                       &spriteDist);

  static unsigned aliveFlipLines = 0;
  if (g_cfg.debuglog != 0 && rayBadguy && !rayAlive && aliveFlipLines < 40) {
    WorldSprite dbg;
    if (world_sprite(ray.hitsprite, &dbg)) {
      aliveFlipLines++;
      wt_log("tbot: sprite %d not targetable -- statnum=%d picnum=%d xrepeat=%d "
             "cstat=0x%04X extra=%d",
             ray.hitsprite, dbg.statnum, dbg.picnum, dbg.xrepeat,
             (unsigned)(dbg.cstat & 0xFFFF), dbg.extra);
    }
  }
  const bool rayHitsEnemy = rayBadguy && rayAlive &&
                            aim_within_range(spriteDist, aim_range_clamped(g_cfg.aimrange));
  const bool rayHitsBreakable = tbot_ray_hits_breakable(ray, rayHitsEnemy);

  TbotCond cond = { false, "skipped" };
  if (!cheapRefusal) {
    if (when == TBOT_WHEN_CROSSHAIR) {
      cond = tbot_when_crosshair(ray.hitsprite, rayBadguy, rayAlive,
                                 spriteDist, aim_range_clamped(g_cfg.aimrange));
    } else {
      cond = tbot_when_aimbot(target, haveSnapForRay && snapForRay.los,
                              rayHitsEnemy, rayHitsBreakable);
    }
  }

  // WHAT STOPS THE RAY: a vent BREAKS (tbot_wall_breakable reproduces checkhitwall); a lowering gate is shot THROUGH (a
  // masked wall whose cstat does not take hitscans). Wall, sprite and clear-ray refusals look alike from outside, hence
  // this report.
  {
    static long lastDump = 0;
    // SELF-TRIGGERING as well as on the key: the failure is intermittent and mostly on the machine gun, so asking the
    // player to catch it is a race. A CONDITION, not a position: a locked target with line of sight whose ray still
    // refuses.
    const bool blockedWithLock = target >= 0 && haveSnapForRay && snapForRay.los &&
                                 !rayHitsEnemy && !rayHitsBreakable;
    static int selfSeen[16] = { -1, -1, -1, -1, -1, -1, -1, -1,
                                -1, -1, -1, -1, -1, -1, -1, -1 };
    static int selfCount = 0;
    bool selfTrigger = false;
    if (blockedWithLock && selfCount < 16) {
      bool known = false;
      for (int k = 0; k < selfCount; k++) if (selfSeen[k] == target) { known = true; break; }
      if (!known) { selfSeen[selfCount++] = target; selfTrigger = true; }
    }
    if (selfTrigger || dbgdump_take(&lastDump)) {
      // THE TWO ORIGINS: (ang,horiz) was solved from eyeAhead.z, the ray is cast from shotzAhead = eyez + pyoff +
      // 0x400: parallel lines, so the ray sits `delta` off at the target; it clears the target when delta > h (Egg
      // 1920, Octabrain 3640).
      {
        WorldSprite ts;
        const int tsy = target >= 0 ? world_tilesizy(
                          world_sprite(target, &ts) ? ts.picnum : 0) : 0;
        const int h = (target >= 0 && world_sprite(target, &ts)) ? ts.yrepeat * tsy : 0;
        const int solveZ = eyeAhead.z;
        const int delta  = shotzAhead - solveZ;
        wt_log("tbot dump: origins | solve z=%d | ray z=%d (eye %d + pyoff %d "
               "+ 1024) | delta=%d | target h=%d -> predicts %s",
               solveZ, shotzAhead, p.eyez, p.pyoff, delta, h,
               (h > 0 && delta > h) ? "MISS" : "hit");
      }
      wt_log("tbot dump: weapon=%d %s", p.currWeapon,
             selfTrigger ? "(self-triggered: locked, los, ray refused)" : "(key press)");
      wt_log("tbot dump: when=%d target=%d castRay=%d los=%d | ray hitwall=%d "
             "hitsprite=%d hitsect=%d hitz=%d rdist=%d | rayEnemy=%d rayBreakable=%d",
             when, target, castRay ? 1 : 0,
             (haveSnapForRay && snapForRay.los) ? 1 : 0,
             ray.hitwall, ray.hitsprite, ray.hitsect, ray.hitz, ray.dist2d,
             rayHitsEnemy ? 1 : 0, rayHitsBreakable ? 1 : 0);
      if (ray.hitwall >= 0) {
        WorldWall w;
        if (world_wall(ray.hitwall, &w)) {
          int nf = 0, nc = 0;
          const bool haveNext = w.nextsector >= 0 &&
                                world_sector_zrange(w.nextsector, &nc, &nf);
          wt_log("tbot dump: wall %d picnum=%d overpicnum=%d pal=%d cstat=0x%04X "
                 "(blocking=%d masked=%d oneway=%d hitscan=%d) nextsector=%d "
                 "nextceil=%d nextfloor=%d | breakable rule says %d",
                 ray.hitwall, w.picnum, w.overpicnum, w.pal, (unsigned)(w.cstat & 0xFFFF),
                 (w.cstat & 0x0001) ? 1 : 0, (w.cstat & 0x0010) ? 1 : 0,
                 (w.cstat & 0x0020) ? 1 : 0, (w.cstat & 0x0040) ? 1 : 0,
                 w.nextsector, haveNext ? nc : 0, haveNext ? nf : 0,
                 tbot_wall_breakable(w.cstat, w.overpicnum, w.nextsector,
                                     ray.hitz, haveNext ? nf : 0, haveNext ? nc : 0) ? 1 : 0);
        } else {
          wt_log("tbot dump: wall %d could not be read", ray.hitwall);
        }
      }
      if (ray.hitsprite >= 0) {
        WorldSprite sp;
        if (world_sprite(ray.hitsprite, &sp))
          wt_log("tbot dump: sprite %d picnum=%d pal=%d cstat=0x%04X statnum=%d "
                 "extra=%d | enemy=%d spriteBreakable=%d",
                 ray.hitsprite, sp.picnum, sp.pal, (unsigned)(sp.cstat & 0xFFFF),
                 sp.statnum, sp.extra, esp_is_enemy(sp.picnum) ? 1 : 0,
                 tbot_sprite_breakable(sp.picnum) ? 1 : 0);
      }
      if (ray.hitwall < 0 && ray.hitsprite < 0)
        wt_log("tbot dump: the ray hit NOTHING -- it is not a wall or a sprite "
               "blocking this shot, so look at los and the target above");
    }
  }

  const char* reason = 0;
  const bool fire = tbot_decide(g_cfg.tboton != 0, input_menu_open(), paused,
                                p.dead, cameraActive, weaponUsable, cond,
                                tbot_delay_tics(g_cfg.tbotdelay), &s_state, &reason);
  const char* logReason = reason;
  if (reason && aimdiag_streq(reason, "blocked"))
    logReason = aimjump_blocked_reason(ray.hitsprite >= 0 && ray.hitsprite == target,
                                       rayAlive, ray.hitwall >= 0);

  if (fire) input_set_fire_bit();
  s_firedLastTic = fire;

  s_last.firing = fire ? 1 : 0;
  s_last.heldTics = s_state.heldTics;
  s_last.delayTics = tbot_delay_tics(g_cfg.tbotdelay);
  s_last.target = target;
  s_last.raySprite = ray.hitsprite;
  s_last.reason = logReason ? logReason : "?";

  // What the bot did about a LABELLED target: says whether it shot, and if not which term refused. One line per
  // (picnum, state, reason).
  if (g_cfg.debugaim != 0 && target >= 0) {
    WorldSprite ts;
    if (world_sprite(target, &ts)) {
      const ActorState tst = actor_state_live(target);
      if (actor_state_labelled(tst)) {
        static int  s_actKey[24] = {0};
        static int  s_actCount = 0;
        const int key = ts.picnum * 64 + (int)tst * 8 + aimjump_reason_index(reason);
        bool known = false;
        for (int k = 0; k < s_actCount; k++) if (s_actKey[k] == key) { known = true; break; }
        if (!known && s_actCount < 24) {
          s_actKey[s_actCount++] = key;
          wt_log("state act: tgt %d pic %d is %s -- bot %s (%s), held=%d/%d "
                 "| targetable=%d rayspr=%d rayAlive=%d dist=%d",
                 target, ts.picnum, actor_state_name(tst),
                 fire ? "FIRES" : "holds", reason ? reason : "?",
                 s_state.heldTics, tbot_delay_tics(g_cfg.tbotdelay),
                 (int)actor_state_targetable(tst), ray.hitsprite,
                 (int)rayAlive, spriteDist);
        }
      }
    }
  }

  // The jumpiness summary: one line per interval in which the bot did something. `flips` is the complaint (a bot
  // chattering at an enemy behind a grate has hundreds); the top stop reason says what kept turning it off.
  if (g_cfg.debugaim != 0) {
    const unsigned now = wallclock_ms();
    if (s_jump.startedMs == 0) aimjump_reset(&s_jump, now);
    if (!cheapRefusal) aimjump_tic(&s_jump, fire ? 1 : 0, target, logReason);
    if (aimjump_elapsed(s_jump, now, 5000)) {
      if (aimjump_worth_reporting(s_jump)) {
        const int top = aimjump_top_stop(s_jump);
        wt_log("aim run: %d tics, %d firing | %d flips (%d on a kill) | %d shots "
               "(%d EARLY, %d WILD) | %d targets | top stop %s x%d",
               s_jump.ticsSeen, s_jump.ticsFiring, s_jump.flips, s_jump.killStops,
               s_jump.shots, s_jump.early, s_jump.wild, s_jump.targets,
               aimjump_reason_name(top), s_jump.stops[top]);
      }
      aimjump_reset(&s_jump, now);
    }
  }

  // The dump key marks the moment: self-triggering is too conjunctive when the lock moves between eight sprites in four
  // seconds. Per enemy in aim range: `extra` (applied), `pend` (landed, waiting for its CON), cstat bit 8 (touchable).
  {
    static long s_dumpSeen = 0;
    const long dump = dbgdump_take(&s_dumpSeen);
    if (dump) {
      wt_log("tbot enemies #%ld: target=%d rayspr=%d raywall=%d | eye=(%d,%d) z=%d "
             "horiz=%d pyoff=%d",
             dump, target, ray.hitsprite, ray.hitwall, eyeAhead.x, eyeAhead.y,
             p.eyez, p.horiz, p.pyoff);
      const int range = aim_range_clamped(g_cfg.aimrange);
      int listed = 0;
      for (int pass = 0; pass < 2 && listed < 24; pass++) {
        WorldWalk w;
        if (!world_walk_begin(&w, pass == 0 ? TARGET_STAT_ACTOR : TARGET_STAT_ZOMBIEACTOR))
          continue;
        int idx = -1;
        while (listed < 24 && world_walk_next(&w, &idx)) {
          WorldSprite sp;
          if (!world_sprite(idx, &sp) || !esp_is_enemy(sp.picnum)) continue;
          const int d = trigger_world_dist2d(eyeAhead.x, eyeAhead.y, sp.x, sp.y);
          if (d > range) continue;
          int pend = 0, t4 = 0;
          world_actor_pending_damage(idx, &pend);
          world_actor_temp_data(idx, 4, &t4);
          listed++;
          wt_log("tbot enemies #%ld:   spr %d pic %d stat=%d cstat=0x%04X "
                 "xrep=%d yrep=%d | extra=%d pend=%d | t4=%d dist=%d z=%d "
                 "| targetable=%d",
                 dump, idx, sp.picnum, sp.statnum, (unsigned)(sp.cstat & 0xFFFF),
                 sp.xrepeat, sp.yrepeat, sp.extra, pend, t4, d, sp.z,
                 actor_state_targetable(actor_state_live(idx)) ? 1 : 0);
        }
      }
      if (listed == 0)
        wt_log("tbot enemies #%ld:   no enemy within %d units", dump, range);
    }
  }

  // The "it will not die" report is SELF-TRIGGERING (a counted budget is gone before the enemy is met): aimed at for a
  // second of tics, several shooting, health unmoved. Tics accumulate PER SPRITE in a table, since the lock moves.
  {
    const int      kStuckSlots = 6;
    const unsigned kStuckLines = 40;
    const int      kStuckTics  = 30;    // a second of tics spent on this sprite
    const int      kStuckFired = 8;     // of which this many actually shot
    const int      kStuckStale = 90;    // three seconds unseen retires a slot
    const unsigned kStuckMs    = 250;

    // `held` is whether actor_damage_lands called this target undamageable when `extra` was recorded: a Firefly that
    // grows to 48, takes the hit and shrinks back would report a drop beside its CURRENT xrepeat of 18, like a failed
    // rule.
    struct StuckSlot { int index; int extra; int eff; int tics; int fired;
                       int lastTic; int held; };
    static StuckSlot s_slots[6] = { {-1,0,0,0,0,0,0}, {-1,0,0,0,0,0,0}, {-1,0,0,0,0,0,0},
                                    {-1,0,0,0,0,0,0}, {-1,0,0,0,0,0,0}, {-1,0,0,0,0,0,0} };
    static int       s_stuckTic = 0;
    static unsigned  s_stuckLines = 0;
    static unsigned  s_stuckLastMs = 0;

    s_stuckTic++;
    const int watched = target >= 0 ? target : ray.hitsprite;
    WorldSprite ws;
    if (watched >= 0 && world_sprite(watched, &ws)) {
      int pend = 0;
      world_actor_pending_damage(watched, &pend);
      const int eff = pend > 0 ? ws.extra - pend : ws.extra;
      const bool undamageableNow =
          !actor_damage_lands(ws.picnum, ws.statnum, ws.ang, ws.xrepeat,
                              weapon_escapes_hit_discards(p.currWeapon));

      int at = -1;
      for (int k = 0; k < kStuckSlots; k++)
        if (s_slots[k].index == watched) { at = k; break; }
      if (at < 0) {
        at = 0;
        for (int k = 1; k < kStuckSlots; k++)
          if (s_slots[k].lastTic < s_slots[at].lastTic) at = k;
        s_slots[at].index = watched;
        s_slots[at].tics = 0;
        s_slots[at].fired = 0;
        s_slots[at].extra = ws.extra;
        s_slots[at].eff = eff;
        s_slots[at].held = undamageableNow ? 1 : 0;
      }
      StuckSlot& sl = s_slots[at];
      // THE COUNTER-EXAMPLE TO actor_damage_lands: the test is the rule itself, so a shrunk Firefly at statnum 1 counts
      // too. Undamageable at BOTH ends, or it proves nothing (aimdiag.h).
      if (s_stuckTic - sl.lastTic <= kStuckStale && g_cfg.debuglog != 0 &&
          aimdiag_contradicts_discard(ws.extra, sl.extra, undamageableNow,
                                      sl.held != 0)) {
        static int  seen[8] = {0};
        static int  seenCount = 0;
        bool known = false;
        for (int k = 0; k < seenCount; k++) if (seen[k] == ws.picnum) { known = true; break; }
        if (!known && seenCount < 8) {
          seen[seenCount++] = ws.picnum;
          wt_log("tbot: picnum %d CAN be damaged where the rule says it cannot -- sprite %d went "
                 "extra %d -> %d at statnum %d, ang %d, xrepeat %d. actor_damage_lands says %d",
                 ws.picnum, watched, sl.extra, ws.extra, ws.statnum, ws.ang,
                 ws.xrepeat, undamageableNow ? 0 : 1);
        }
      }
      if (s_stuckTic - sl.lastTic > kStuckStale || ws.extra < sl.extra || eff < sl.eff) {
        sl.tics = 0;
        sl.fired = 0;
        sl.extra = ws.extra;
        sl.eff = eff;
        sl.held = undamageableNow ? 1 : 0;
      }
      sl.lastTic = s_stuckTic;
      sl.tics++;
      if (fire) sl.fired++;

      if (sl.tics >= kStuckTics && sl.fired >= kStuckFired &&
          g_cfg.debuglog != 0 && s_stuckLines < kStuckLines) {
        const unsigned now = wallclock_ms();
        if (s_stuckLines == 0 || now - s_stuckLastMs >= kStuckMs) {
          s_stuckLastMs = now;
          s_stuckLines++;
          int t4 = 0;
          world_actor_temp_data(watched, 4, &t4);
          wt_log("tbot stuck: spr %d pic %d stat=%d cstat=0x%04X xrep=%d yrep=%d "
                 "| extra=%d pend=%d eff=%d | %d tics, %d of them firing | t4=%d "
                 "| ray sprite=%d wall=%d rdist=%d | tgtdist=%d z=%d",
                 watched, ws.picnum, ws.statnum, (unsigned)(ws.cstat & 0xFFFF),
                 ws.xrepeat, ws.yrepeat, ws.extra, pend, eff,
                 sl.tics, sl.fired, t4,
                 ray.hitsprite, ray.hitwall, ray.dist2d,
                 trigger_world_dist2d(eyeAhead.x, eyeAhead.y, ws.x, ws.y), ws.z);
        }
      }
    }
  }

  static bool lastFiring = false;
  if (g_cfg.debuglog != 0 && fire != lastFiring) {
    wt_log("tbot: %s (%s) when=%d held=%d delay=%d target=%d raysprite=%d "
           "badguy=%d alive=%d dist=%d breakable=%d",
           fire ? "FIRE" : "stop", logReason ? logReason : "?", when,
           s_state.heldTics, tbot_delay_tics(g_cfg.tbotdelay), target,
           ray.hitsprite, rayBadguy ? 1 : 0, rayAlive ? 1 : 0, spriteDist,
           rayHitsBreakable ? 1 : 0);
    wt_log("tbot: ray via %s (aim steering %d, silent %d, cast %d)",
           solvedRay ? "the solved pair" : "the view", aimSteering ? 1 : 0,
           g_cfg.silenton ? 1 : 0, castRay ? 1 : 0);
    // Where the ray went instead, for a decision alternating FIRE/blocked while walking: cast from the MUZZLE along a
    // pair solved from the EYE, and pyoff is the walk bob; a vert that swings with pyoff names the cause.
    const short* sn = world_sintable();
    if (sn && castRay) {
      int rvx = 0, rvy = 0, rvz = 0;
      tbot_ray(rayAng, rayHoriz, p.horizoff, sn, &rvx, &rvy, &rvz);
      const int sz = shotzAhead;
      int fwd = 0, vert = 0, lat = 0;
      WorldSprite ts;
      if (target >= 0 && world_sprite(target, &ts)) {
        const int tsy = world_tilesizy(ts.picnum);
        const int centre = (tsy > 0 && ts.yrepeat > 0)
                         ? target_probe_z(ts.z, ts.yrepeat, tsy, AP_CENTRE, 0) : ts.z;
        fwd = shot_forward(eyeAhead.x, eyeAhead.y, rayAng, ts.x, ts.y, sn);
        lat = shot_lateral(eyeAhead.x, eyeAhead.y, rayAng, ts.x, ts.y, sn);
        vert = shot_vertical(sz, rvz, fwd, centre);
      }
      wt_log("tbot: ray hw=%d hsect=%d hitz=%d rdist=%d | to target fwd=%d lat=%d vert=%d "
             "| horiz=%d horizoff=%d pyoff=%d step=(%d,%d,%d)",
             ray.hitwall, ray.hitsect, ray.hitz, ray.dist2d, fwd, lat, vert,
             rayHoriz, p.horizoff, p.pyoff,
             p.eyex - p.oposx, p.eyey - p.oposy, p.eyez - p.oposz);
    }
  }
  lastFiring = fire;
}

// The World Tour port's suite: this port copied Megaton's code but not its suite, so "pure rules are pinned" held for
// the headers only and "it compiled" was the only check. build.ps1 fails the build on a red check. Each check below
// is a shipped defect and names what shipped.
#include "test_common.h"
#include "../autoswitch.h"
#include "../bounds.h"

// Not static: the ported Megaton blocks live in their own translation units and count into the same totals. The next
// block is esp_layout.h's pure rules, declared rather than included (that header pulls canvas.h and font.h).
struct ScreenPt { int x, y; };
struct ScreenBox { int x, y, w, h; };
struct EspMarker { int kind, index, dist2d, seq; ScreenPt p; ScreenBox box; };
const int ESP_MARKER_MAX = 256;
inline void esp_keep_nearest(EspMarker* m, int count, int keep) {
  if (!m || count <= 0 || keep <= 0 || keep >= count) return;
  for (int i = 0; i < keep; i++) {
    int best = i;
    for (int j = i + 1; j < count; j++)
      if (m[j].dist2d < m[best].dist2d) best = j;
    if (best != i) { const EspMarker t = m[i]; m[i] = m[best]; m[best] = t; }
  }
}

int passed = 0, failed = 0;

const void* q_stub_last_addr = 0;
unsigned q_stub_last_bytes = 0;
int q_stub_refuse(const void* addr, unsigned bytes) {
  q_stub_last_addr = addr; q_stub_last_bytes = bytes; return 0;
}
int q_stub_accept(const void* addr, unsigned bytes) {
  q_stub_last_addr = addr; q_stub_last_bytes = bytes; return 1;
}

int aim_leadscale_clamped(int pct) { return clamp_int(pct, AIM_LEADSCALE_MIN, AIM_LEADSCALE_MAX); }

// PORTED FROM MEGATON, VERBATIM: target.h is identical between the two ports and actorstate.h shares its rules, so
// these blocks pin this port too.
void tb_0110_actorstate_core();
void tb_0111_shot_geometry();
void tb_0112_shot_hitscan_halfw();
void tb_0113_enemy_states();

void tb_0018_thunk_encode_rel32_thunk_decode_target_pure_rel32_arithmetic();
void tb_0019_player_index_valid_player_field_rva_pure_arithmetic_behind();
void tb_0020_engine_constant_pinning_final_review();
void tb_0022_task_2_targeting_core();
void tb_0023_read_field_bytes_write_field_bytes_pure_width_aware();
void tb_0024_collapse_view_values_pure_which_captured_value();
void tb_0053_phase_3_task_4_aimbot();
void tb_0106_aim_ahead();
void tb_0114_hold_view_rule();

// aimdiag.h, the ledger's rules: three of the five defects found in review were here, each one line to pin.
static void tb_0001_verdict_anomaly_is_early_or_wild_only() {
  // SHIPPED BROKEN: the first cut tested `v[0] != 'h'`, so "notarget" was an anomaly and a crosshair-mode triggerbot
  // logged every shot.
  CHECK(aimdiag_verdict_is_anomaly("EARLY"), "EARLY is an anomaly");
  CHECK(aimdiag_verdict_is_anomaly("WILD"), "WILD is an anomaly");
  CHECK(!aimdiag_verdict_is_anomaly("hit"), "hit is not an anomaly");
  CHECK(!aimdiag_verdict_is_anomaly("notarget"), "notarget is not an anomaly");
  CHECK(!aimdiag_verdict_is_anomaly(0), "a null verdict is not an anomaly");
}

static void tb_0002_verdict_splits_on_settle() {
  // botFiring IS THE FIRST TERM: without it every missed manual pull scored EARLY (tbot_decide resets heldTics on a
  // refusal).
  CHECK(!strcmp(aimdiag_shot_verdict(false, false, 0), "manual"),
        "a shot the bot did not cause gets no verdict about the bot");
  CHECK(!strcmp(aimdiag_shot_verdict(false, true, 99), "manual"),
        "even when it hit");
  CHECK(!strcmp(aimdiag_shot_verdict(true, true, 0), "hit"), "a landing bot shot is a hit");
  CHECK(!strcmp(aimdiag_shot_verdict(true, false, 0), "EARLY"), "a miss at 0 tics is EARLY");
  CHECK(!strcmp(aimdiag_shot_verdict(true, false, AIMDIAG_SETTLE_TICS), "EARLY"),
        "a miss at the settle boundary is EARLY");
  CHECK(!strcmp(aimdiag_shot_verdict(true, false, AIMDIAG_SETTLE_TICS + 1), "WILD"),
        "a miss past the settle boundary is WILD");
  CHECK(!aimdiag_verdict_is_anomaly("manual"), "and manual is not an anomaly");
}

static void tb_0003_aim_on_box_bounds() {
  CHECK(aimdiag_aim_on_box(0, 0, 100, 100), "dead centre is on the box");
  CHECK(aimdiag_aim_on_box(100, 100, 100, 100), "the corner still is");
  CHECK(!aimdiag_aim_on_box(101, 0, 100, 100), "one unit wide is not");
  CHECK(!aimdiag_aim_on_box(0, -101, 100, 100), "one unit high is not");
  CHECK(aimdiag_aim_on_box(9999, 9999, 0, 0), "an unreadable extent does not invent a miss");
}


static void tb_0004_jump_interval_resets_when_idle() {
  // SHIPPED BROKEN: aimjump_due folded "has the interval elapsed" into "was there anything in it", so an idle
  // interval never reset.
  AimJump j = {};
  aimjump_reset(&j, 1000);
  CHECK(!aimjump_worth_reporting(j), "an empty interval is not worth reporting");
  CHECK(aimjump_elapsed(j, 6000, 5000), "an empty interval still elapses");
  CHECK(!aimjump_elapsed(j, 5999, 5000), "and not before its period");
}

static void tb_0005_jump_counts_flips_and_stops() {
  AimJump j = {};
  aimjump_reset(&j, 0);
  aimjump_tic(&j, 1, 5, "fire");     // off -> on
  aimjump_tic(&j, 1, 5, "fire");
  aimjump_tic(&j, 0, 5, "blocked");  // on -> off
  aimjump_tic(&j, 1, 7, "fire");     // off -> on, new target
  CHECK(j.ticsSeen == 4, "every tic is seen");
  CHECK(j.ticsFiring == 3, "firing tics are counted");
  CHECK(j.flips == 3, "three transitions");
  CHECK(j.stops[AIMJUMP_R_BLOCKED] == 1, "the stop reason is tallied once");
  CHECK(aimjump_top_stop(j) == AIMJUMP_R_BLOCKED, "and is the top stop");
  CHECK(j.targets == 2, "two distinct targets");
  CHECK(aimjump_worth_reporting(j), "a busy interval is worth reporting");
}

// actorstate.h, the rules four features share. PORTED FROM MEGATON: this gate asks whether the decoder looks like a
// run of action declarations, so an all-zero record (a legal action) must NOT pass.
static void tb_0006_plausible_keeps_megatons_bounds() {
  CHECK(action_plausible(50, 5, 1), "ATROOPDYING is plausible");
  CHECK(action_plausible(0, 1, 5), "ATROOPSTAND is plausible");
  CHECK(action_plausible(1023, 64, 8), "both upper bounds, inclusive");
  CHECK(action_plausible(0, 1, 3) && action_plausible(0, 1, 7), "the other viewtypes");
  CHECK(!action_plausible(0, 1, 0), "viewtype 0 is not one the CON rotates with");
  CHECK(!action_plausible(0, 1, 2) && !action_plausible(0, 1, 4), "nor 2 or 4");
  CHECK(!action_plausible(0, 0, 1), "numframes 0 is not a run of frames");
  CHECK(!action_plausible(0, 65, 1) && !action_plausible(0, -1, 1), "frame bounds");
  CHECK(!action_plausible(-1, 5, 1) && !action_plausible(1024, 5, 1), "startframe bounds");
  CHECK(!action_plausible(47, 17, 2762), "the commander's garbage read is not plausible");
}

// THE DIVERGENCE THAT COST A DAY: action_record_is is Megaton's (readable and five exact values); adding `&&
// a.plausible` retired "Playing dead", whose record is deliberately implausible.
static void tb_0006b_record_is_does_not_demand_plausible() {
  ActorAction dead = {};
  dead.startframe = 54; dead.readable = true;
  dead.plausible = action_plausible(54, 0, 0);
  CHECK(!dead.plausible, "ATROOPDEAD is not plausible, by design");
  CHECK(action_record_is(dead, 54, 0, 0, 0, 0),
        "and action_record_is reads it anyway");
  ActorAction unreadable = {};
  CHECK(!action_record_is(unreadable, 0, 0, 0, 0, 0),
        "but an unreadable record is never a match");
}

static ActorAction rec(int sf, int nf, int vt, int iv, int dl) {
  ActorAction a = {};
  a.startframe = sf; a.numframes = nf; a.viewtype = vt; a.incval = iv; a.delay = dl;
  a.readable = true;
  a.plausible = action_plausible(sf, nf, vt);
  return a;
}

static void tb_0007_playdead_reads_its_three_records() {
  const ActorAction dead      = rec(54, 0, 0, 0, 0);
  const ActorAction playdead  = rec(54, 0, 0, 0, 0);
  const ActorAction sufferdead= rec(58, 2, 1, -4, 24);
  const ActorAction dying     = rec(50, 5, 1, 1, 16);
  CHECK(actor_record_is_playdead(playdead, dead, sufferdead), "playing dead is read");
  CHECK(!actor_record_is_playdead(dead, dying, playdead), "a real corpse is not");
  CHECK(actor_record_is_suffering(rec(59, 2, 1, 1, 21)), "choking is read");
}

static void tb_0008_choking_is_alive_and_targetable() {
  const ActorState st = actor_state(TARGET_STAT_ACTOR, true, 40, 0x0101, 0,
                                    /*choking*/true, false, false);
  CHECK(st == AS_CHOKING, "a hittable enemy at zero health with the record is choking");
  CHECK(actor_state_targetable(st), "and choking is targetable");
  CHECK(actor_state_shown(st), "and shown");
}

static void tb_0009_damage_lands_is_a_picnum_list() {
  // SHIPPED BROKEN: written as a global claim about sleeping actors; 0x9EA20 is only the tail of checkhitsprite,
  // whose picnum switch returns long before it.
  CHECK(actor_damage_lands(ACTOR_PICNUM_SHARK, TARGET_STAT_ACTOR, 512, 40, false),
        "an awake shark can be damaged");
  CHECK(!actor_damage_lands(ACTOR_PICNUM_SHARK, TARGET_STAT_ZOMBIEACTOR, 512, 40, false),
        "a dormant shark at a non-zero angle cannot");
  CHECK(actor_damage_lands(ACTOR_PICNUM_SHARK, TARGET_STAT_ZOMBIEACTOR, 0, 40, false),
        "a dormant shark at angle zero is woken by the hit");
  CHECK(actor_damage_lands(1820, TARGET_STAT_ZOMBIEACTOR, 512, 40, false),
        "a dormant octabrain is NOT in the list");

  // THE FIREFLY TROOPER, whose gate is a size and not a status list. The
  // engine threshold is its FULL xrepeat, 48 (rva 0x9E7CD, `cmp byte …, 0x30`).
  CHECK(actor_damage_lands(ACTOR_PICNUM_FIREFLY, TARGET_STAT_ACTOR, 512, 48, false),
        "a full-size firefly can be damaged");
  CHECK(!actor_damage_lands(ACTOR_PICNUM_FIREFLY, TARGET_STAT_ACTOR, 512, 47, false),
        "one step under full size it cannot -- the compare is `jb 0x30`");
  CHECK(!actor_damage_lands(ACTOR_PICNUM_FIREFLY, TARGET_STAT_ACTOR, 512, 12, false),
        "and shrunk (FF_SHRUNKSIZEX) it certainly cannot");
  CHECK(actor_damage_lands(1680, TARGET_STAT_ACTOR, 512, 12, false),
        "no other picnum is judged on its size");

  // SPLASH IS NOT SUBJECT TO EITHER DISCARD: hitradius writes hittype[].extra itself (rva 0x2199D) and never enters
  // checkhitsprite, where both gates live.
  CHECK(actor_damage_lands(ACTOR_PICNUM_FIREFLY, TARGET_STAT_ACTOR, 512, 12, true),
        "a shrunk firefly CAN be hurt by splash");
  CHECK(actor_damage_lands(ACTOR_PICNUM_SHARK, TARGET_STAT_ZOMBIEACTOR, 512, 40, true),
        "and so can a sleeping shark");

  // THE EXEMPTION IS SPLASH, NOT CLASS: a projectile's direct impact goes through checkhitsprite like a bullet, so
  // the freezer (WC_PROJECTILE, no blast) is subject to both discards.
  CHECK(!weapon_has_splash(9), "the freezer has no splash");
  CHECK(!weapon_has_splash(3) && !weapon_has_splash(1), "nor the chaingun or pistol");
  CHECK(!weapon_has_splash(6) && !weapon_has_splash(11), "nor shrinker or expander");
  CHECK(weapon_has_splash(4) && weapon_has_splash(7), "the RPG and devastator do");
  CHECK(weapon_has_splash(5) && weapon_has_splash(8) && weapon_has_splash(10),
        "and the pipebomb, tripbomb and detonator");
  CHECK(!actor_damage_lands(ACTOR_PICNUM_FIREFLY, TARGET_STAT_ACTOR, 512, 12,
                            weapon_escapes_hit_discards(9)),
        "so the freezer still holds fire on a shrunk firefly");

  // WORLD TOUR'S OWN TWELFTH WEAPON: the Incinerator is curr_weapon 12 (DEFS.CON:802) and its damage route is
  // unsettled (its flame is a CON actor), so an unsettled weapon is exempt rather than guessed.
  CHECK(!weapon_has_splash(12), "12 is not in the settled splash list");
  CHECK(weapon_escapes_hit_discards(12), "but it is exempt, because unsettled");
  CHECK(weapon_escapes_hit_discards(-1) && weapon_escapes_hit_discards(99),
        "and so is any index outside the table");
  CHECK(!weapon_escapes_hit_discards(9) && !weapon_escapes_hit_discards(3),
        "a settled no-splash weapon is still bound");
  CHECK(weapon_escapes_hit_discards(4), "a settled splash weapon is exempt");
  CHECK(actor_damage_lands(ACTOR_PICNUM_FIREFLY, TARGET_STAT_ACTOR, 512, 12,
                           weapon_escapes_hit_discards(12)),
        "so the Incinerator does NOT hold fire on a shrunk firefly");
}

static void tb_0010_effective_extra_needs_real_pending() {
  CHECK(actor_effective_extra(ACTOR_PICNUM_TROOP_JUST_SIT, 30, -1) == 30,
        "a resting pending changes nothing");
  CHECK(actor_effective_extra(ACTOR_PICNUM_TROOP_JUST_SIT, 30, 40) == -10,
        "a sitting trooper discounts real pending damage");
  CHECK(actor_effective_extra(1680, 30, 40) == 30,
        "an ordinary trooper does not");
}

static void tb_0011_commander_dying_is_a_record() {
  // CLOSED: the health rule that hid any hittable commander at zero health also hid two the player was still fighting
  // (temp_data[4] decodes to ACOMMSHOOT). The verdict is the RECORD again, plus ACOMMDEAD (one index past ACOMMDYING
  // in this fork's decode).
  ActorAction dying = {};
  dying.startframe = 30; dying.numframes = 8; dying.viewtype = 1;
  dying.incval = 1; dying.delay = 12; dying.readable = true;
  CHECK(actor_record_is_commander_dying(dying), "ACOMMDYING is a death record");

  ActorAction dead = {};
  dead.startframe = 38; dead.numframes = 1; dead.viewtype = 1;
  dead.incval = 1; dead.delay = 1; dead.readable = true;
  CHECK(actor_record_is_commander_dying(dead), "ACOMMDEAD is too");

  ActorAction shooting = {};
  shooting.startframe = 20; shooting.numframes = 1; shooting.viewtype = 5;
  shooting.incval = 1; shooting.delay = 35; shooting.readable = true;
  CHECK(!actor_record_is_commander_dying(shooting),
        "a commander mid-attack is NOT dying");

  ActorAction unreadable = {};
  CHECK(!actor_record_is_commander_dying(unreadable),
        "and an unreadable record is not");

  CHECK(actor_state(TARGET_STAT_ACTOR, true, 40, 0x0101, 0, false, false, false) == AS_ALIVE,
        "a hittable enemy at zero health with no death record is alive");
  CHECK(actor_state(TARGET_STAT_ACTOR, true, 40, 0x0101, 0, false, false, true) == AS_DYING,
        "and dying only when the record says so");
}

// ---------------------------------------------------------------------------
// firerate.h -- the counter the ledger prints beside every shot.
static void tb_0012_firerate_steps_and_carry() {
  double carry = 0.0;
  CHECK(firerate_steps(100, &carry) == 0, "100 percent adds no steps");
  carry = 0.0;
  const int a = firerate_steps(200, &carry);
  CHECK(a == 1, "200 percent adds one step a tic");
  carry = 0.0;
  const int b = firerate_steps(150, &carry);
  const int c = firerate_steps(150, &carry);
  CHECK(b + c == 1, "150 percent adds one step across two tics");
  CHECK(firerate_rate_clamped(9999) == FIRERATE_MAX, "the rate is clamped high");
  CHECK(firerate_rate_clamped(0) == FIRERATE_MIN, "and low");
}

// target.h, the probe ladder: aimpoint=upper collapsed it to two probes inside the top 18.75% of the sprite, so cover
// over the top quarter killed every probe. Rung 3 is the centre, tried last.
static void tb_0013_probe_ladder_keeps_a_lower_rung() {
  const int z = 100000, yrep = 40, tsy = 78;   // a pigcop: h = 3120, body = 4h
  const int h = yrep * tsy;

  CHECK(target_probe_z(z, yrep, tsy, AP_CENTRE, 0) == z - 2 * h, "rung 0 at centre is the centre");
  CHECK(target_probe_z(z, yrep, tsy, AP_UPPER, 0) == z - 3 * h, "rung 0 at upper is the upper body");
  CHECK(target_probe_z(z, yrep, tsy, AP_UPPER, 1) == z - 3 * h, "rung 1 is the upper body");
  CHECK(target_probe_z(z, yrep, tsy, AP_UPPER, 2) == z - 4 * h + h / 4, "rung 2 is the head");
  CHECK(target_probe_z(z, yrep, tsy, AP_UPPER, 3) == z - 2 * h, "rung 3 is the centre");
  CHECK(target_probe_z(z, yrep, tsy, AP_CENTRE, 2) == z - 4 * h + h / 4, "the head is the head either way");

  int zs[AIM_PROBE_MAX];
  const int nUp = aim_probe_zs(z, yrep, tsy, AP_UPPER, zs, AIM_PROBE_MAX);
  CHECK(nUp == 3, "at upper the ladder is three distinct probes, not two");
  CHECK(zs[0] == z - 3 * h, "upper body first: it is what the player configured");
  CHECK(zs[2] == z - 2 * h, "and the centre is LAST -- cover blocks from the bottom up");

  const int nCt = aim_probe_zs(z, yrep, tsy, AP_CENTRE, zs, AIM_PROBE_MAX);
  CHECK(nCt == 3, "at centre it is still three: rung 3 duplicates rung 0 and is dropped");
  CHECK(zs[0] == z - 2 * h, "lowest first");

  const int fh = 10 * 90;
  CHECK(aim_probe_zs(z, 10, 90, AP_UPPER, zs, AIM_PROBE_MAX) == 3, "and three when tiny too");
  CHECK(zs[0] - zs[1] == fh - fh / 4, "the two upper probes stay inside a 3600-unit body");
}

// The counter-example to the discard rules, the only instrument that can prove actor_damage_lands wrong, so what it
// accepts as proof is itself pinned.
static void tb_0014_contradiction_needs_both_ends() {
  CHECK(aimdiag_contradicts_discard(10, 50, true, true),
        "health fell while the rule held throughout: a real contradiction");
  // The Firefly case that proved nothing: it grew, took the hit while damageable, and shrank back before the sample.
  CHECK(!aimdiag_contradicts_discard(-13, 50, true, false),
        "damageable when the health was recorded: proves nothing");
  CHECK(!aimdiag_contradicts_discard(-13, 50, false, true),
        "damageable now: proves nothing either");
  CHECK(!aimdiag_contradicts_discard(50, 50, true, true), "no drop, no claim");
  CHECK(!aimdiag_contradicts_discard(60, 50, true, true), "a RISE is not a drop");
}

// The states the ESP puts a word on: AS_CHOKING is both shown AND targetable, so it crossed neither boundary the two
// existing ledgers watched (why the choking report had no data).
static void tb_0015_labelled_states() {
  CHECK(actor_state_labelled(AS_CHOKING), "choking is labelled");
  CHECK(actor_state_labelled(AS_PLAYDEAD) && actor_state_labelled(AS_CLOAKED),
        "so are playing dead and cloaked");
  CHECK(!actor_state_labelled(AS_ALIVE) && !actor_state_labelled(AS_DYING) &&
        !actor_state_labelled(AS_DEAD) && !actor_state_labelled(AS_NONE),
        "the plain states are not");
  CHECK(actor_state_targetable(AS_CHOKING) && actor_state_shown(AS_CHOKING),
        "choking is targetable AND shown, so it is never refused and never vanishes");
}

// WHICH MARKERS SURVIVE A FRAME THAT RAN OUT OF ROOM: with unlimited range on 158 enemies the frame hit its vertex
// ceiling and nearby markers and the PANEL vanished (markers emit far-to-near, so the far ones spent the budget).
// Selection and draw order are now separate.
static void tb_0016_short_frame_keeps_the_nearest() {
  EspMarker m[5] = {};
  const int d[5] = { 900, 100, 700, 300, 500 };
  for (int i = 0; i < 5; i++) { m[i].dist2d = d[i]; m[i].index = i; }

  esp_keep_nearest(m, 5, 2);
  CHECK(m[0].dist2d == 100 && m[1].dist2d == 300,
        "the two nearest are moved to the front");

  EspMarker n[3] = {};
  n[0].dist2d = 50; n[1].dist2d = 10; n[2].dist2d = 30;
  esp_keep_nearest(n, 3, 3);
  CHECK(n[0].dist2d == 50 && n[1].dist2d == 10 && n[2].dist2d == 30,
        "keep >= count is a no-op");
  esp_keep_nearest(n, 3, 9);
  CHECK(n[0].dist2d == 50, "and so is a keep past the end");

  esp_keep_nearest(0, 5, 2);
  esp_keep_nearest(m, 0, 2);
  esp_keep_nearest(m, 5, 0);
  CHECK(m[0].dist2d == 100, "null, empty and zero-keep leave the list alone");

  EspMarker f[4] = {};
  f[0].dist2d = 30000; f[1].dist2d = 28000; f[2].dist2d = 26000; f[3].dist2d = 400;
  esp_keep_nearest(f, 4, 1);
  CHECK(f[0].dist2d == 400, "the enemy next to the player is the one kept");
}

// "blocked" was three answers under one name, and reading it as geometry sent an investigation after a wall that was
// not there (20 of 23 were kills).
static void tb_0017_blocked_splits_into_three() {
  CHECK(!strcmp(aimjump_blocked_reason(true, false, false), "died"),
        "ray reached the target and it is no longer alive: a kill");
  CHECK(!strcmp(aimjump_blocked_reason(true, false, true), "died"),
        "and a kill even if a wall is also in the trace");
  CHECK(!strcmp(aimjump_blocked_reason(false, false, true), "wall"),
        "ray stopped on geometry: an obstruction");
  CHECK(!strcmp(aimjump_blocked_reason(false, true, false), "stray"),
        "ray went somewhere else entirely");
  CHECK(!strcmp(aimjump_blocked_reason(true, true, false), "stray"),
        "on target and alive is not a refusal this function explains");
  CHECK(aimjump_reason_is_kill("died") && !aimjump_reason_is_kill("wall"),
        "only a kill counts as a kill");

  AimJump j = {};
  aimjump_reset(&j, 0);
  aimjump_tic(&j, 1, 5, "fire");
  aimjump_tic(&j, 0, 5, "died");
  aimjump_tic(&j, 1, 6, "fire");
  aimjump_tic(&j, 0, 6, "wall");
  CHECK(j.flips == 4, "four transitions");
  CHECK(j.killStops == 1, "one of them was the bot killing something");
  CHECK(j.stops[AIMJUMP_R_DIED] == 1 && j.stops[AIMJUMP_R_WALL] == 1,
        "and the two are tallied apart");
}

// Automatic weapon switching: the engine half is two hooks, read not run; everything that DECIDES is here. World Tour
// removed the game's own option (no `WeaponSwitchMode` or `autoswitch` in its executable), so these rules are the
// whole feature.
static void tb_0018_auto_weapon_switch() {
  // The master gates both events; out of the box everything is on (the stock game): the feature is the ability to
  // turn it off.
  CHECK(autoswitch_pickup_allowed(1, 1) && autoswitch_empty_allowed(1, 1),
        "all on is stock behaviour");
  CHECK(!autoswitch_pickup_allowed(0, 1) && !autoswitch_empty_allowed(0, 1),
        "the master turns both off whatever the sub-rows say");
  CHECK(autoswitch_pickup_allowed(1, 1) && !autoswitch_empty_allowed(1, 0),
        "and the two events are independent");

  // THE QUEUED WEAPON KEY MUST SURVIVE: checkavailweapon services wantweaponfire through the same write to
  // curr_weapon, so suppressing it blindly would eat the player's key.
  CHECK(autoswitch_restore_after_empty(1, 0, AUTOSWITCH_NO_REQUEST, 2),
        "switch disallowed and nothing requested: undo it");
  CHECK(!autoswitch_restore_after_empty(1, 0, 3, 3),
        "the player asked for 3 and got 3: that switch stands");
  // THE LEAK A REQUEST-ONLY TEST MISSED: the queued weapon was unowned or dry and the engine fell back to another;
  // with "on empty ammo" off that must still be undone.
  CHECK(autoswitch_restore_after_empty(1, 0, 3, 7),
        "asked for 3, got 7: that is the fallback, not the key");
  CHECK(!autoswitch_restore_after_empty(1, 1, AUTOSWITCH_NO_REQUEST, 2),
        "switch allowed: never undo it");

  // The engine's own encoding, not the obvious one: it decodes with `v == 0 ? 9 : v - 1`, so a stored 1 is weapon 0.
  CHECK(autoswitch_prefer_weapon(0) == -1, "0 is leave the order alone");
  CHECK(autoswitch_prefer_weapon(1) == 0, "1 is the Mighty Foot");
  CHECK(autoswitch_prefer_weapon(4) == 3, "4 is the chaingun");
  CHECK(autoswitch_prefer_weapon(10) == 9, "10 is the freezer");
  CHECK(autoswitch_wchoice_value(0) == 1 && autoswitch_wchoice_value(3) == 4,
        "and the inverse is what ud.wchoice wants");
  CHECK(autoswitch_wchoice_value(9) == 10,
        "weapon 9 stores as 10, not 0: both decode to 9 and 10 does not read as unset");

  CHECK(autoswitch_prefer_clamped(-1) == 0 && autoswitch_prefer_clamped(99) == 0,
        "an out-of-range preference falls back to the engine default");
  CHECK(!strcmp(autoswitch_prefer_name(0), "Engine default"), "and says so");
  CHECK(!strcmp(autoswitch_prefer_name(4), "Chaingun"), "the names line up with the values");

  // Restoring curr_weapon alone leaves the HUD weapon mid-raise, so the restore carries all six fields. 0 AND 10 ARE
  // THE SAME WEAPON to the engine: the slide putting a preference first searched for the raw value, so a freezer
  // stored as 0 never matched 10 and was duplicated.
  CHECK(autoswitch_wchoice_weapon(0) == 9 && autoswitch_wchoice_weapon(10) == 9,
        "both spellings of the freezer decode to the same weapon");
  CHECK(autoswitch_wchoice_weapon(1) == 0 && autoswitch_wchoice_weapon(4) == 3,
        "and the ordinary entries decode as the engine does");
  for (int w = 0; w < AUTOSWITCH_WEAPON_COUNT; w++)
    CHECK(autoswitch_wchoice_weapon(autoswitch_wchoice_value(w)) == w,
          "every weapon round-trips through the stored form");

  WeaponSwitchState a = {}, b = {};
  a.currWeapon = 3; a.weaponPos = 4; a.kickbackPic = 1; b = a;
  CHECK(!weapon_switch_changed(a, b), "no switch, nothing to put back");
  b.currWeapon = 5;
  CHECK(weapon_switch_changed(a, b), "a changed weapon is a switch");
  // Re-picking a held weapon rewrites the animation fields but leaves curr_weapon alone, so the change test must see
  // them.
  b = a; b.weaponPos = -1;
  CHECK(weapon_switch_changed(a, b), "an interrupted raise counts too");
  b = a; b.kickbackPic = 0;
  CHECK(weapon_switch_changed(a, b), "and a reset kickback");
}

// bounds.h: the checks in front of a code patch, a file path and the clipboard read.
// CHECK1, not the two-argument CHECK: MSVC's traditional preprocessor hands CHECK_PICK its __VA_ARGS__ as one
// argument, so CHECK(cond, "what") expands to CHECK1((cond, "what")) and a comma expression that is always true.
static void tb_0115_image_range_fits() {
  CHECK1(image_range_fits(0x1000, 5, 0x2000));
  CHECK1(image_range_fits(0x1FFB, 5, 0x2000));           // ends exactly at the image end
  CHECK1(!image_range_fits(0x1FFC, 5, 0x2000));          // one byte past
  CHECK1(!image_range_fits(0x2000, 1, 0x2000));          // starts at the end
  CHECK1(!image_range_fits(0xFFFFFFF0u, 0x20, 0x2000));  // rva + len wraps
  CHECK1(!image_range_fits(0x10, 5, 0));                 // no image
}

static void tb_0116_eip_inside_patch() {
  CHECK1(!eip_inside_patch(0x400000, 0x400000, 6));      // at the start: runs the new JMP whole
  CHECK1(eip_inside_patch(0x400001, 0x400000, 6));       // after push ebp: mid-patch
  CHECK1(eip_inside_patch(0x400005, 0x400000, 6));       // last patched byte
  CHECK1(!eip_inside_patch(0x400006, 0x400000, 6));      // past the patch
  CHECK1(!eip_inside_patch(0x3FFFFF, 0x400000, 6));      // before it
}

static void tb_0117_path_beside_module() {
  char out[32];
  CHECK1(path_beside_module("AB\\g\\duke3d.exe", 15, 32, "wtmod.ini", out, 32) && streq(out, "AB\\g\\wtmod.ini"));
  // GetModuleFileNameA answers the buffer size when it truncated: the last backslash is then a parent folder's.
  out[0] = 'x';
  CHECK1(!path_beside_module("AB\\long\\folder\\na", 16, 16, "wtmod.ini", out, 32) && out[0] == 0);
  out[0] = 'x';
  CHECK1(!path_beside_module("AB\\g\\duke3d.exe", 0, 32, "wtmod.ini", out, 32) && out[0] == 0);   // call failed
  out[0] = 'x';
  CHECK1(!path_beside_module("duke3d.exe", 10, 32, "wtmod.ini", out, 32) && out[0] == 0);         // no folder
  out[0] = 'x';
  CHECK1(!path_beside_module("AB\\g\\duke3d.exe", 15, 32, "wtmod.ini", out, 13) && out[0] == 0);  // name won't fit
  char inplace[32] = "AB\\g\\duke3d.exe";
  CHECK1(path_beside_module(inplace, 15, 32, "wtmod.log", inplace, 32) && streq(inplace, "AB\\g\\wtmod.log"));
}

static void tb_0118_clip_copy_len() {
  const char block[4] = { 'a', 'b', 'c', 'd' };   // no terminator
  CHECK1(clip_copy_len(block, 4, 64) == 4);       // stops at the block's end
  CHECK1(clip_copy_len("abc", 4, 64) == 3);       // stops at NUL
  CHECK1(clip_copy_len("abcdef", 7, 4) == 3);     // room for the terminator
  CHECK1(clip_copy_len(block, 0, 64) == 0);       // empty block
}

int main() {
  tb_0001_verdict_anomaly_is_early_or_wild_only();
  tb_0002_verdict_splits_on_settle();
  tb_0003_aim_on_box_bounds();
  tb_0004_jump_interval_resets_when_idle();
  tb_0005_jump_counts_flips_and_stops();
  tb_0006_plausible_keeps_megatons_bounds();
  tb_0006b_record_is_does_not_demand_plausible();
  tb_0007_playdead_reads_its_three_records();
  tb_0008_choking_is_alive_and_targetable();
  tb_0009_damage_lands_is_a_picnum_list();
  tb_0010_effective_extra_needs_real_pending();
  tb_0011_commander_dying_is_a_record();
  tb_0012_firerate_steps_and_carry();
  tb_0013_probe_ladder_keeps_a_lower_rung();
  tb_0014_contradiction_needs_both_ends();
  tb_0015_labelled_states();
  tb_0016_short_frame_keeps_the_nearest();
  tb_0017_blocked_splits_into_three();
  tb_0018_auto_weapon_switch();

  tb_0110_actorstate_core();
  tb_0111_shot_geometry();
  tb_0112_shot_hitscan_halfw();
  tb_0113_enemy_states();
  tb_0018_thunk_encode_rel32_thunk_decode_target_pure_rel32_arithmetic();
  tb_0019_player_index_valid_player_field_rva_pure_arithmetic_behind();
  tb_0020_engine_constant_pinning_final_review();
  tb_0022_task_2_targeting_core();
  tb_0023_read_field_bytes_write_field_bytes_pure_width_aware();
  tb_0024_collapse_view_values_pure_which_captured_value();
  tb_0053_phase_3_task_4_aimbot();
  tb_0106_aim_ahead();
  tb_0114_hold_view_rule();
  tb_0115_image_range_fits();
  tb_0116_eip_inside_patch();
  tb_0117_path_beside_module();
  tb_0118_clip_copy_len();

  printf("%d passed, %d failed\n", passed, failed);
  return failed ? 1 : 0;
}

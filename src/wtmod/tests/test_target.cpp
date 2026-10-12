// Ported from Megaton's tests/test_target.cpp; target.h is identical between the ports apart from one documented ladder
// change. Blocks are kept verbatim or dropped whole (those needing config_load, canvas_record, CombatState or
// engine_player_aim). Two checks are quarantined, not rewritten: AIM_PROBE_MAX and the AP_UPPER ladder (see below).
#include "test_common.h"

void tb_0018_thunk_encode_rel32_thunk_decode_target_pure_rel32_arithmetic() {
    // Real thunk: VA 0x00401F91, bytes E9 9A 98 00 00, decodes to the CONTROL_GetInput entry, VA 0x0040B830.
    const unsigned thunkVA = 0x00401F91;
    unsigned givenRel32;
    const unsigned char givenBytes[5] = { 0xE9, 0x9A, 0x98, 0x00, 0x00 };
    memcpy(&givenRel32, givenBytes + 1, 4);
    CHECK(thunk_decode_target(thunkVA, givenRel32) == 0x0040B830u);
    CHECK(thunk_encode_rel32(thunkVA, 0x0040B830u) == givenRel32);

    CHECK(thunk_decode_target(thunkVA, thunk_encode_rel32(thunkVA, 0x0040B830u)) == 0x0040B830u);
    const unsigned backThunkVA = 0x00410000;
    const unsigned backTarget  = 0x00400000;   // 0x10005 bytes before the thunk
    const unsigned backRel32 = thunk_encode_rel32(backThunkVA, backTarget);
    CHECK(backRel32 == 0xFFFEFFFBu);   // -0x10005 as a 32-bit two's-complement value
    CHECK(thunk_decode_target(backThunkVA, backRel32) == backTarget);

    const unsigned adjTarget = thunkVA + 5;
    CHECK(thunk_encode_rel32(thunkVA, adjTarget) == 0u);
    CHECK(thunk_decode_target(thunkVA, 0u) == adjTarget);
  }

// Pins the arithmetic behind input.cpp's view freeze (not linkable into test_host): player_index_valid gates
// myconnectindex before ps[] is indexed; player_field_rva does the stride multiply plus field-offset add.

void tb_0019_player_index_valid_player_field_rva_pure_arithmetic_behind() {
    // ps is player_struct[16] (940-byte stride), so MAX_PLAYER_INDEX is 15; a stale bound of 63 let the tic-clock hooks
    // write nine fields 15k-59k bytes past the array. Pinned against the real constant, not a re-typed literal.
    CHECK(player_index_valid(MAX_PLAYER_INDEX, MAX_PLAYER_INDEX));        // last real slot: accepted
    CHECK(!player_index_valid(MAX_PLAYER_INDEX + 1, MAX_PLAYER_INDEX));   // one past the real array: rejected
    CHECK(!player_index_valid(63, MAX_PLAYER_INDEX));                     // the old, wrong bound: rejected now

    CHECK(!player_index_valid(-1, 63));
    CHECK(!player_index_valid(-1000, 63));
    CHECK(player_index_valid(0, 63));
    CHECK(player_index_valid(1, 63));
    CHECK(player_index_valid(63, 63));
    CHECK(!player_index_valid(64, 63));
    CHECK(!player_index_valid(1000, 63));

    // Production values from rva:: (common.h): ps RVA 0x015CBB60, stride 940 (0x3AC), horiz/ohoriz/ohorizoff at
    // +0x220/4/8.
    const unsigned rvaPs = rva::RVA_PS;
    const unsigned stride = rva::PS_STRIDE;
    CHECK(player_field_rva(rvaPs, 0, stride, 0x220) == rvaPs + 0x220u);
    CHECK(player_field_rva(rvaPs, 0, stride, 0x224) == rvaPs + 0x224u);
    CHECK(player_field_rva(rvaPs, 0, stride, 0x228) == rvaPs + 0x228u);

    CHECK(player_field_rva(rvaPs, 1, stride, 0x220) == rvaPs + stride + 0x220u);
    CHECK(player_field_rva(rvaPs, 2, stride, 0x220) == rvaPs + 2u * stride + 0x220u);

    CHECK(player_field_rva(rvaPs, 0, stride, 0x220) != player_field_rva(rvaPs, 0, stride, 0x224));
    CHECK(player_field_rva(rvaPs, 0, stride, 0x224) != player_field_rva(rvaPs, 0, stride, 0x228));
  }

void tb_0020_engine_constant_pinning_final_review() {
    CHECK(MAX_PLAYER_INDEX == 15);   // ps[] has exactly 16 slots (indices 0..15), per the shipping PDB

    // VIEW_FIELD_WIDTHS: LENGTH checked against VF_COUNT (an enum widened without extending the initializer zero-fills
    // the new slot) and CONTENTS, by loop and by name.
    CHECK((int)(sizeof(VIEW_FIELD_WIDTHS) / sizeof(VIEW_FIELD_WIDTHS[0])) == VF_COUNT);
    CHECK(VF_COUNT == 9);
    const int expectedWidths[VF_COUNT] = { 4, 4, 4, 2, 2, 1, 2, 2, 2 };
    for (int i = 0; i < VF_COUNT; i++) CHECK(VIEW_FIELD_WIDTHS[i] == expectedWidths[i]);
    CHECK(VIEW_FIELD_WIDTHS[VF_HORIZ]            == 4);
    CHECK(VIEW_FIELD_WIDTHS[VF_OHORIZ]           == 4);
    CHECK(VIEW_FIELD_WIDTHS[VF_OHORIZOFF]        == 4);
    CHECK(VIEW_FIELD_WIDTHS[VF_LOOK_ANG]         == 2);   // the case the coordinator specifically flagged
    CHECK(VIEW_FIELD_WIDTHS[VF_HORIZOFF]         == 2);
    CHECK(VIEW_FIELD_WIDTHS[VF_RETURN_TO_CENTER] == 1);
    CHECK(VIEW_FIELD_WIDTHS[VF_ANG]              == 2);
    CHECK(VIEW_FIELD_WIDTHS[VF_OANG]             == 2);
    CHECK(VIEW_FIELD_WIDTHS[VF_ANGVEL]           == 2);

    CHECK(rva::RVA_PS    == 0x015CBB60u);
    CHECK(rva::PS_STRIDE == 940u);
    CHECK(rva::PS_OFFSET_HORIZ            == 0x220u);
    CHECK(rva::PS_OFFSET_OHORIZ           == 0x224u);
    CHECK(rva::PS_OFFSET_OHORIZOFF        == 0x228u);
    CHECK(rva::PS_OFFSET_LOOK_ANG         == 0x294u);
    CHECK(rva::PS_OFFSET_HORIZOFF         == 0x2BEu);
    CHECK(rva::PS_OFFSET_RETURN_TO_CENTER == 0x398u);
    CHECK(rva::PS_OFFSET_ANG              == 0x28Cu);
    CHECK(rva::PS_OFFSET_OANG             == 0x28Eu);
    CHECK(rva::PS_OFFSET_ANGVEL           == 0x290u);
  }

void tb_0022_task_2_targeting_core() {
    // Weapon classes and speeds, pinned to the binary's constants.
    CHECK(target_weapon_info(1).cls == WC_HITSCAN && target_weapon_info(1).vel == 0);       // pistol
    CHECK(target_weapon_info(2).cls == WC_HITSCAN);                                          // shotgun
    CHECK(target_weapon_info(3).cls == WC_HITSCAN);                                          // chaingun
    CHECK(target_weapon_info(4).cls == WC_PROJECTILE && target_weapon_info(4).vel == 644 && target_weapon_info(4).horizMul == 81);   // rpg
    CHECK(target_weapon_info(7).cls == WC_PROJECTILE && target_weapon_info(7).vel == 644);  // devastator fires RPG
    CHECK(target_weapon_info(9).cls == WC_PROJECTILE && target_weapon_info(9).vel == 644);  // freezer
    CHECK(target_weapon_info(0).cls == WC_MELEE);
    CHECK(target_weapon_info(5).cls == WC_NONE && target_weapon_info(8).cls == WC_NONE);   // pipebomb, tripbomb: none
    CHECK(target_weapon_info(99).cls == WC_NONE);

    // Aim points from the engine's height model: sprite height is 4h.
    CHECK(target_aim_z(10000, 40, 60, AP_CENTRE) == 10000 - 2 * 40 * 60);
    CHECK(target_aim_z(10000, 40, 60, AP_UPPER)  == 10000 - 3 * 40 * 60);
    CHECK(target_aim_z(10000, 0, 60, AP_CENTRE)  == 10000);   // degenerate sprite: feet

    CHECK(target_angular_offset(0, 1000, 0) == 0);
    CHECK(target_angular_offset(0, 0, 1000) == 512);
    CHECK(target_angular_offset(0, -1000, 0) == 1024);
    CHECK(target_angular_offset(2040, 1000, 100) < 64);      // across the 2047/0 seam
    CHECK(target_angular_offset(512, 1000, 0) == 512);

    { TargetCandidate c[3] = {};
      c[0].index = 5;  c[0].x = 1000; c[0].y = 300;  c[0].dist2d = 1044;   // ~17 deg off
      c[1].index = 7;  c[1].x = 2000; c[1].y = 0;    c[1].dist2d = 2000;   // dead ahead, farther
      c[2].index = 9;  c[2].x = 100;  c[2].y = 900;  c[2].dist2d = 905;    // ~84 deg off, nearest
      CHECK(target_pick(c, 3, 0, 0, 0, TM_CROSSHAIR, 256) == 7);
      CHECK(target_pick(c, 3, 0, 0, 0, TM_CLOSEST,   256) == 9);
      CHECK(target_pick(c, 3, 0, 0, 0, TM_CROSSHAIR, 32)  == 7);           // only the dead-ahead one fits a tight cone
      CHECK(target_pick(c, 3, 512, 0, 0, TM_CROSSHAIR, 64) == 9);          // looking +y: the 84-deg one is now ~6 off
      CHECK(target_pick(c, 0, 0, 0, 0, TM_CROSSHAIR, 256) == -1);
      CHECK(target_pick(nullptr, 3, 0, 0, 0, TM_CROSSHAIR, 256) == -1);
      // A cone excluding every candidate returns -1, not the nearest (at viewAng 1024 nothing is under 548 off).
      CHECK(target_pick(c, 3, 1024, 0, 0, TM_CROSSHAIR, 500) == -1);

      // A tie on angular offset falls to the dist2d tie-break; the nearer is listed second, so a dropped clause fails.
      TargetCandidate tie[2] = {};
      tie[0].index = 2; tie[0].x = 5000; tie[0].y = 0; tie[0].dist2d = 5000;
      tie[1].index = 1; tie[1].x = 1000; tie[1].y = 0; tie[1].dist2d = 1000;
      CHECK(target_pick(tie, 2, 0, 0, 0, TM_CROSSHAIR, 256) == 1); }

    // The pick PREFERS healthy candidates: liveness alone leaves a death-animation actor pickable (the aimbot shot a
    // corpse beside a live enemy). A preference, not a filter.
    {
      CHECK( target_candidate_healthy(1));       // the boundary, from above
      CHECK( target_candidate_healthy(30));
      CHECK( target_candidate_healthy(4500));
      CHECK(!target_candidate_healthy(0));       // and from below
      CHECK(!target_candidate_healthy(-1));
      CHECK(!target_candidate_healthy(-7));      // the log's own reading
      CHECK(!target_candidate_healthy(-2147483647 - 1));
      for (int e = -20; e <= 20; e++)
        CHECK(target_candidate_healthy(e) == (target_hp_for_display(e) > 0));
    }
    {
      TargetCandidate m[2] = {};
      m[0].index = 3; m[0].x = 1000; m[0].y = 0;   m[0].dist2d = 1000; m[0].extra = -7;
      m[1].index = 4; m[1].x = 5000; m[1].y = 400; m[1].dist2d = 5016; m[1].extra = 30;
      CHECK(target_pick(m, 2, 0, 0, 0, TM_CROSSHAIR, 256) == 4);
      CHECK(target_pick(m, 2, 0, 0, 0, TM_CLOSEST,   256) == 4);
      TargetCandidate r[2] = {};
      r[0] = m[1];
      r[1] = m[0];
      CHECK(target_pick(r, 2, 0, 0, 0, TM_CROSSHAIR, 256) == 4);
      CHECK(target_pick(r, 2, 0, 0, 0, TM_CLOSEST,   256) == 4);

      // A lone unhealthy candidate is still picked: a filter here would bring back the vanishing-enemy bug.
      TargetCandidate lone[1] = {};
      lone[0].index = 3; lone[0].x = 1000; lone[0].y = 0; lone[0].dist2d = 1000;
      lone[0].extra = -7;
      CHECK(target_pick(lone, 1, 0, 0, 0, TM_CROSSHAIR, 256) == 3);
      CHECK(target_pick(lone, 1, 0, 0, 0, TM_CLOSEST,   256) == 3);
      lone[0].extra = 0;   // including exactly zero, the ESP's displayed value
      CHECK(target_pick(lone, 1, 0, 0, 0, TM_CROSSHAIR, 256) == 3);

      TargetCandidate u[2] = {};
      u[0].index = 6; u[0].x = 5000; u[0].y = 0;    u[0].dist2d = 5000; u[0].extra = -3;
      u[1].index = 8; u[1].x = 100;  u[1].y = 900;  u[1].dist2d = 905;  u[1].extra = -1;
      CHECK(target_pick(u, 2, 0, 0, 0, TM_CROSSHAIR, 256) == 6);
      CHECK(target_pick(u, 2, 0, 0, 0, TM_CLOSEST,   256) == 8);

      TargetCandidate h[2] = {};
      h[0].index = 6; h[0].x = 5000; h[0].y = 0;   h[0].dist2d = 5000; h[0].extra = 30;
      h[1].index = 8; h[1].x = 100;  h[1].y = 900; h[1].dist2d = 905;  h[1].extra = 250;
      CHECK(target_pick(h, 2, 0, 0, 0, TM_CROSSHAIR, 256) == 6);
      CHECK(target_pick(h, 2, 0, 0, 0, TM_CLOSEST,   256) == 8);
      CHECK(target_pick(h, 2, 0, 0, 0, TM_CROSSHAIR, 256) != 8);
    }
    {
      // The cone applies inside the healthy tier: tiering before the cone returns -1 with a shootable enemy in it.
      TargetCandidate c[2] = {};
      c[0].index = 11; c[0].x = 100;  c[0].y = 900; c[0].dist2d = 905;  c[0].extra = 30;
      c[1].index = 12; c[1].x = 2000; c[1].y = 0;   c[1].dist2d = 2000; c[1].extra = -5;
      CHECK(target_pick(c, 2, 0, 0, 0, TM_CROSSHAIR, 64) == 12);
      CHECK(target_pick(c, 2, 0, 0, 0, TM_CROSSHAIR, 512) == 11);
      CHECK(target_pick(c, 2, 1024, 0, 0, TM_CROSSHAIR, 100) == -1);
      CHECK(target_pick(c, 2, 0, 0, 0, TM_CLOSEST, 64) == 11);
    }
    {
      TargetCandidate c[3] = {};
      c[0].index = 21; c[0].x = 100;  c[0].y = 900; c[0].dist2d = 905;  c[0].extra = 30;
      c[1].index = 22; c[1].x = 6000; c[1].y = 300; c[1].dist2d = 6007; c[1].extra = 30;
      c[2].index = 23; c[2].x = 800;  c[2].y = 0;   c[2].dist2d = 800;  c[2].extra = -9;
      CHECK(target_pick(c, 3, 0, 0, 0, TM_CROSSHAIR, 64) == 22);
      CHECK(target_pick(c, 3, 0, 0, 0, TM_CLOSEST,   64) == 21);
      c[0].extra = -1; c[1].extra = -1;
      CHECK(target_pick(c, 3, 0, 0, 0, TM_CROSSHAIR, 64) == 23);
      CHECK(target_pick(c, 3, 0, 0, 0, TM_CLOSEST,   64) == 23);
    }
    {
      CHECK(target_pick(nullptr, 3, 0, 0, 0, TM_CROSSHAIR, 256) == -1);
      TargetCandidate many[8] = {};
      for (int k = 0; k < 8; k++) {
        many[k].index = 40 + k;
        many[k].x = 1000 + 100 * k;
        many[k].y = 0;
        many[k].dist2d = 1000 + 100 * k;
        many[k].extra = -1 - k;
      }
      CHECK(target_pick(many, 8, 0, 0, 0, TM_CROSSHAIR, 256) == 40);
      CHECK(target_pick(many, 8, 0, 0, 0, TM_CLOSEST,   256) == 40);
      many[7].extra = 5;
      CHECK(target_pick(many, 8, 0, 0, 0, TM_CROSSHAIR, 256) == 47);
      CHECK(target_pick(many, 8, 0, 0, 0, TM_CLOSEST,   256) == 47);
    }
    {
      // Sprite index 0 is real: the fallback is guarded on `healthy >= 0`, and a weakened `> 0` would discard an
      // index-0 winner. The healthy candidate is index 0, far and off-axis; the dying one is dead ahead and near.
      TargetCandidate z[2] = {};
      z[0].index = 0; z[0].x = 5000; z[0].y = 400; z[0].dist2d = 5016; z[0].extra = 30;
      z[1].index = 5; z[1].x = 1000; z[1].y = 0;   z[1].dist2d = 1000; z[1].extra = -7;
      CHECK(target_pick(z, 2, 0, 0, 0, TM_CROSSHAIR, 256) == 0);
      CHECK(target_pick(z, 2, 0, 0, 0, TM_CLOSEST,   256) == 0);
      TargetCandidate zr[2] = {};
      zr[0] = z[1];
      zr[1] = z[0];
      CHECK(target_pick(zr, 2, 0, 0, 0, TM_CROSSHAIR, 256) == 0);
      CHECK(target_pick(zr, 2, 0, 0, 0, TM_CLOSEST,   256) == 0);
      TargetCandidate zu[2] = {};
      zu[0].index = 0; zu[0].x = 1000; zu[0].y = 0;   zu[0].dist2d = 1000; zu[0].extra = -1;
      zu[1].index = 5; zu[1].x = 5000; zu[1].y = 400; zu[1].dist2d = 5016; zu[1].extra = -2;
      CHECK(target_pick(zu, 2, 0, 0, 0, TM_CROSSHAIR, 256) == 0);
      CHECK(target_pick(zu, 2, 0, 0, 0, TM_CLOSEST,   256) == 0);
    }

    // Lead: a target moving +x at 20/tic, 6440 away, vel 644 -> t ~ 10 tics -> lead ~ +200 x.
    { Point3 s = { 0, 0, 0 }, p = { 6440, 0, 0 };
      Point3 l = target_lead(s, p, 20, 0, 0, 644);
      CHECK(l.x >= 6640 && l.x <= 6650 && l.y == 0 && l.z == 0);
      Point3 h = target_lead(s, p, 20, 0, 0, 0);       // hitscan: no lead
      // Time of flight is 2D (xy) only: z/vz are added to the aim point, never mixed into the divisor.
      Point3 dn = { 6440, 0, 65536 };
      Point3 ld = target_lead(s, dn, 20, 0, 0, 644);
      CHECK(ld.x == l.x && ld.z == 65536);
      Point3 fall = target_lead(s, p, 0, 0, 1600, 644);
      CHECK(fall.x == 6440 && fall.z >= 15900 && fall.z <= 16100);
      CHECK(h.x == 6440 && h.y == 0);
      Point3 m = target_lead(s, p, 0, 300, 0, 644);    // lateral motion changes y, not x; t1 uses the LEAD point's distance
      CHECK(m.x == 6440 && m.y >= 3290 && m.y <= 3330); }

    // Lead scale: a dial between the raw body point and the fully led one (actors burst and stop, so a full
    // constant-velocity lead overshoots). Distinct nonzero z deltas so a mutant scaling only x/y cannot pass.
    {
      const Point3 raw = { 6440, 0, 1000 };
      const Point3 led = { 6640, 400, 1400 };     // deltas 200, 400, 400

      const Point3 none = target_lead_scaled(raw, led, 0);
      CHECK(none.x == 6440 && none.y == 0 && none.z == 1000);   // exactly the raw point

      const Point3 full = target_lead_scaled(raw, led, 100);
      CHECK(full.x == 6640 && full.y == 400 && full.z == 1400); // exactly the led point

      const Point3 def = target_lead_scaled(raw, led, AIM_LEADSCALE_DEFAULT);
      CHECK(def.x == 6580);    // 6440 + 200 * 70/100
      CHECK(def.y == 280);     //    0 + 400 * 70/100
      CHECK(def.z == 1280);    // 1000 + 400 * 70/100
      CHECK(def.x > raw.x && def.x < led.x);
      CHECK(def.y > raw.y && def.y < led.y);
      CHECK(def.z > raw.z && def.z < led.z);

      const Point3 half = target_lead_scaled(raw, led, 50);
      CHECK(half.x == 6540 && half.y == 200 && half.z == 1200);
      CHECK(half.x < def.x && def.x < full.x);
      CHECK(half.z < def.z && def.z < full.z);

      const Point3 o = { 0, 0, 0 }, back = { -3, -3, -300 };
      const Point3 b = target_lead_scaled(o, back, 70);
      CHECK(b.x == -2 && b.y == -2 && b.z == -210);

      const Point3 over  = target_lead_scaled(raw, led, 500);
      const Point3 under = target_lead_scaled(raw, led, -40);
      CHECK(over.x == full.x && over.y == full.y && over.z == full.z);
      CHECK(under.x == none.x && under.y == none.y && under.z == none.z);

      const Point3 lo = { -2147483647 - 1, 0, 0 }, hi = { 2147483647, 0, 0 };
      const Point3 e100 = target_lead_scaled(lo, hi, 100);
      CHECK(e100.x == 2147483647);
      const Point3 e50 = target_lead_scaled(lo, hi, 50);
      CHECK(e50.x >= lo.x && e50.x <= hi.x);
      const Point3 e0 = target_lead_scaled(hi, lo, 0);
      CHECK(e0.x == 2147483647);

      CHECK(AIM_LEADSCALE_MIN == 0);
      CHECK(AIM_LEADSCALE_MAX == 100);
      CHECK(AIM_LEADSCALE_DEFAULT == 70);
      CHECK(aim_leadscale_clamped(70) == 70);
      CHECK(aim_leadscale_clamped(0) == 0);
      CHECK(aim_leadscale_clamped(100) == 100);
      CHECK(aim_leadscale_clamped(-1) == 0);
      CHECK(aim_leadscale_clamped(-4000) == 0);
      CHECK(aim_leadscale_clamped(101) == 100);
      CHECK(aim_leadscale_clamped(4000) == 100);
    }
    {
      const WeaponInfo rpg = target_weapon_info(4);
      const Point3 eye = { 0, 0, -8000 };
      const Point3 body = { 6440, 1200, -6000 };
      const Point3 led = target_lead(eye, body, 140, -90, -80, rpg.vel);
      CHECK(led.x != body.x && led.y != body.y && led.z != body.z);   // there IS a lead

      const Point3 at70 = target_lead_scaled(body, led, 70);
      const Point3 at0  = target_lead_scaled(body, led, 0);
      const Point3 at100 = target_lead_scaled(body, led, 100);
      CHECK(at0.x == body.x && at0.y == body.y && at0.z == body.z);
      CHECK(at100.x == led.x && at100.y == led.y && at100.z == led.z);
      CHECK((at70.x - body.x) * 2 > (led.x - body.x));
      CHECK(at70.x != body.x && at70.x != led.x);
      CHECK(at70.y != body.y && at70.y != led.y);
      CHECK(at70.z != body.z && at70.z != led.z);
      const AimSolution s70  = target_solve(eye, at70, 0, rpg);
      const AimSolution s100 = target_solve(eye, at100, 0, rpg);
      CHECK(s70.valid && s100.valid);
      CHECK(s70.ang != s100.ang || s70.horiz != s100.horiz);
    }

    { Point3 eye = { 0, 0, 0 }, aim = { 4096, 0, 0 };
      WeaponInfo hs = target_weapon_info(1);
      AimSolution a = target_solve(eye, aim, 0, hs);
      CHECK(a.valid && a.ang == 0 && a.horiz == 100);
      AimSolution b = target_solve(eye, aim, 7, hs);
      CHECK(b.valid && b.horiz == 93);                                   // horizoff is subtracted, not ignored
      Point3 below = { 4096, 0, 4096 };                                  // dz = +4096 (down), dist 4096 -> 8
      AimSolution c = target_solve(eye, below, 0, hs);
      CHECK(c.valid && c.horiz == 92);
      Point3 above = { 4096, 0, -4096 };
      AimSolution d = target_solve(eye, above, 0, hs);
      CHECK(d.valid && d.horiz == 108);
      Point3 left = { 0, 4096, 0 };
      AimSolution e = target_solve(eye, left, 0, hs);
      CHECK(e.valid && e.ang == 512);
      // A target to the shooter's right (dy<0) makes atan2 negative before the & 2047 wrap; catches a raw -512.
      Point3 right = { 0, -4096, 0 };
      AimSolution rgt = target_solve(eye, right, 0, hs);
      CHECK(rgt.valid && rgt.ang == 1536);
      WeaponInfo rpg = target_weapon_info(4);                            // projectile: dz*vel/dist/81
      AimSolution f = target_solve(eye, below, 0, rpg);
      CHECK(f.valid && f.horiz == 100 - (4096 * 644 / 4096) / 81);
      AimSolution g = target_solve(eye, eye, 0, hs);                     // zero distance: invalid, never divides
      CHECK(!g.valid); }

    CHECK(target_smooth_ang(0, 100, 0) == 100);
    CHECK(target_smooth_ang(0, 100, 100) == 0);
    CHECK(target_smooth_ang(0, 100, 50) == 50);
    CHECK(target_smooth_ang(2000, 40, 50) == (2000 + 44) % 2048);      // delta is +88 the short way, not -1960
    CHECK(target_smooth_horiz(100, 140, 75) == 110);

    CHECK(target_smooth_ang(0, 100, -50) == 100);
    CHECK(target_smooth_ang(0, 100, 150) == 0);
    CHECK(target_smooth_horiz(100, 140, -50) == 140);
    CHECK(target_smooth_horiz(100, 140, 150) == 100);

    // `step = delta * (100 - pct) / 100` truncates toward zero and could stall aim just short of target: a zero step
    // becomes sign(delta), except at pct == 100 (frozen).
    CHECK(target_smooth_ang(0, 1, 90) == 1);
    CHECK(target_smooth_ang(1, 0, 90) == 0);
    CHECK(target_smooth_horiz(0, 1, 90) == 1);
    CHECK(target_smooth_horiz(1, 0, 90) == 0);

    CHECK(target_degrees_to_units(360) == 2048 && target_degrees_to_units(45) == 256 && target_degrees_to_units(0) == 0);
    CHECK(target_degrees_to_units(1) == 6);
    CHECK( target_within_redirect(0, 200, 256));
    CHECK(!target_within_redirect(0, 300, 256));
    CHECK( target_within_redirect(2040, 20, 64));                       // across the seam, delta 28
    CHECK(!target_within_redirect(0, 1024, 1000));
  }

// Pins the width-aware read/write behind input.cpp's nine-field view freeze (not linkable into test_host): longs,
// shorts and a char share one packed struct, and a width error corrupts the neighbouring field.

void tb_0023_read_field_bytes_write_field_bytes_pure_width_aware() {
    // A 2-byte write touches exactly its own bytes; 0xCC sentinels either side make any spill visible.
    unsigned char buf[8];
    memset(buf, 0xCC, sizeof(buf));
    write_field_bytes(buf, 2, 2, 0x1234);
    CHECK(buf[0] == 0xCC && buf[1] == 0xCC);   // untouched before the field
    CHECK(buf[2] == 0x34 && buf[3] == 0x12);   // the field itself, little-endian
    CHECK(buf[4] == 0xCC && buf[5] == 0xCC);   // untouched after -- NOT widened to 4 bytes
    CHECK(read_field_bytes(buf, 2, 2) == 0x1234);

    memset(buf, 0xCC, sizeof(buf));
    write_field_bytes(buf, 3, 1, 0x05);
    CHECK(buf[0] == 0xCC && buf[1] == 0xCC && buf[2] == 0xCC);
    CHECK(buf[3] == 0x05);
    CHECK(buf[4] == 0xCC && buf[5] == 0xCC && buf[6] == 0xCC && buf[7] == 0xCC);
    CHECK(read_field_bytes(buf, 3, 1) == 5);

    memset(buf, 0xCC, sizeof(buf));
    write_field_bytes(buf, 0, 4, 0x11223344L);
    CHECK(buf[0] == 0x44 && buf[1] == 0x33 && buf[2] == 0x22 && buf[3] == 0x11);
    CHECK(buf[4] == 0xCC);   // still untouched, one byte past the 4-byte field
    CHECK(read_field_bytes(buf, 0, 4) == 0x11223344L);

    write_field_bytes(buf, 0, 2, -1L);
    CHECK(buf[0] == 0xFF && buf[1] == 0xFF);
    CHECK(read_field_bytes(buf, 0, 2) == -1L);

    write_field_bytes(buf, 0, 1, -1L);
    CHECK(buf[0] == 0xFF);
    CHECK(read_field_bytes(buf, 0, 1) == -1L);

    write_field_bytes(buf, 0, 2, -12345L);
    CHECK(read_field_bytes(buf, 0, 2) == -12345L);

    write_field_bytes(buf, 0, 2, 12345L);
    CHECK(read_field_bytes(buf, 0, 2) == 12345L);
    write_field_bytes(buf, 0, 1, 100L);
    CHECK(read_field_bytes(buf, 0, 1) == 100L);

    // Three adjacent 2-byte fields, ang/oang/angvel at 0x28C/0x28E/0x290: a width mistake corrupts a neighbour's live
    // value.
    unsigned char adjBuf[8];
    memset(adjBuf, 0xCC, sizeof(adjBuf));
    write_field_bytes(adjBuf, 2, 2, 100L);   // "ang"    at offset 2..3
    write_field_bytes(adjBuf, 4, 2, 200L);   // "oang"   at offset 4..5
    write_field_bytes(adjBuf, 6, 2, 300L);   // "angvel" at offset 6..7
    CHECK(adjBuf[0] == 0xCC && adjBuf[1] == 0xCC);   // sentinel before the trio
    CHECK(read_field_bytes(adjBuf, 2, 2) == 100L);
    CHECK(read_field_bytes(adjBuf, 4, 2) == 200L);
    CHECK(read_field_bytes(adjBuf, 6, 2) == 300L);
    write_field_bytes(adjBuf, 4, 2, -1L);
    CHECK(read_field_bytes(adjBuf, 2, 2) == 100L);   // left neighbour ("ang") unaffected
    CHECK(read_field_bytes(adjBuf, 4, 2) == -1L);    // the field itself, updated
    CHECK(read_field_bytes(adjBuf, 6, 2) == 300L);   // right neighbour ("angvel") unaffected
  }

// The captured-value -> field mapping behind the render-interpolation jitter fix: Build interpolates the view each
// frame, so holding each field at its own capture left a gap when the mouse moved as the freeze engaged.
// ohoriz/ohorizoff mirror horiz/horizoff, oang mirrors ang, angvel zeroes.

void tb_0024_collapse_view_values_pure_which_captured_value() {
    ViewCollapsedValues v = collapse_view_values(
        /*savedHoriz=*/205, /*savedOhoriz=*/162,
        /*savedHorizoff=*/0, /*savedOhorizoff=*/0,
        /*savedLookAng=*/3, /*savedReturnToCenter=*/0,
        /*savedAng=*/104, /*savedOang=*/108, /*savedAngvel=*/-4);
    CHECK(v.ohoriz == 205);   // receives horiz's value...
    CHECK(v.ohoriz != 162);   // ...not its own captured value
    CHECK(v.horiz == 205);    // horiz itself passes through unchanged

    v = collapse_view_values(
        /*savedHoriz=*/0, /*savedOhoriz=*/0,
        /*savedHorizoff=*/40, /*savedOhorizoff=*/25,
        /*savedLookAng=*/0, /*savedReturnToCenter=*/0,
        /*savedAng=*/0, /*savedOang=*/0, /*savedAngvel=*/0);
    CHECK(v.ohorizoff == 40);   // receives horizoff's value...
    CHECK(v.ohorizoff != 25);   // ...not its own captured value
    CHECK(v.horizoff == 40);    // horizoff itself passes through unchanged

    v = collapse_view_values(
        /*savedHoriz=*/205, /*savedOhoriz=*/162,
        /*savedHorizoff=*/0, /*savedOhorizoff=*/0,
        /*savedLookAng=*/0, /*savedReturnToCenter=*/0,
        /*savedAng=*/104, /*savedOang=*/108, /*savedAngvel=*/-4);
    CHECK(v.oang == 104);     // receives ang's value...
    CHECK(v.oang != 108);     // ...not its own captured value
    CHECK(v.ang == 104);      // ang itself passes through unchanged
    CHECK(v.angvel == 0);     // zeroed outright, NOT held at the captured -4

    v = collapse_view_values(
        /*savedHoriz=*/1, /*savedOhoriz=*/1,
        /*savedHorizoff=*/2, /*savedOhorizoff=*/2,
        /*savedLookAng=*/77, /*savedReturnToCenter=*/9,
        /*savedAng=*/3, /*savedOang=*/3, /*savedAngvel=*/0);
    CHECK(v.lookAng == 77);
    CHECK(v.returnToCenter == 9);

    v = collapse_view_values(
        /*savedHoriz=*/93, /*savedOhoriz=*/93,
        /*savedHorizoff=*/0, /*savedOhorizoff=*/0,
        /*savedLookAng=*/0, /*savedReturnToCenter=*/0,
        /*savedAng=*/50, /*savedOang=*/50, /*savedAngvel=*/0);
    CHECK(v.horiz == 93 && v.ohoriz == 93);
    CHECK(v.ang == 50 && v.oang == 50);

    v = collapse_view_values(
        /*savedHoriz=*/-30, /*savedOhoriz=*/-15,
        /*savedHorizoff=*/-8, /*savedOhorizoff=*/4,
        /*savedLookAng=*/0, /*savedReturnToCenter=*/0,
        /*savedAng=*/-100, /*savedOang=*/-90, /*savedAngvel=*/70);
    CHECK(v.horiz == -30 && v.ohoriz == -30);
    CHECK(v.horizoff == -8 && v.ohorizoff == -8);
    CHECK(v.ang == -100 && v.oang == -100);
    CHECK(v.angvel == 0);   // zeroed even when the captured velocity was positive
  }

void tb_0053_phase_3_task_4_aimbot() {
    // AIM_HORIZ_MIN/MAX are read from the binary (processinput VA 0x004fd559-0x004fd583) and pinned by value, so a
    // symmetric +-100 tidy cannot put the aimbot outside the engine's own range.
    CHECK(AIM_HORIZ_MAX == 299);
    CHECK(AIM_HORIZ_MIN == -99);
    CHECK(aim_horiz_clamped(0) == 0);
    CHECK(aim_horiz_clamped(100) == 100);
    CHECK(aim_horiz_clamped(299) == 299);
    CHECK(aim_horiz_clamped(300) == 299);
    CHECK(aim_horiz_clamped(100000) == 299);
    CHECK(aim_horiz_clamped(-99) == -99);
    CHECK(aim_horiz_clamped(-100) == -99);
    CHECK(aim_horiz_clamped(-100000) == -99);
    CHECK(aim_horiz_clamped(2147483647) == 299);
    CHECK(aim_horiz_clamped(-2147483647 - 1) == -99);

    {
      int a = -1, h = -1;

      CHECK(aim_next_view(100, 100, 500, 150, 0, &a, &h));
      CHECK(a == 500);
      CHECK(h == 150);

      a = -1; h = -1;
      CHECK(aim_next_view(100, 100, 500, 150, 100, &a, &h));
      CHECK(a == 100);
      CHECK(h == 100);

      a = -1; h = -1;
      CHECK(aim_next_view(100, 100, 500, 150, 50, &a, &h));
      CHECK(a == 300);
      CHECK(h == 125);

      a = -1; h = -1;
      CHECK(aim_next_view(2040, 100, 10, 100, 0, &a, &h));
      CHECK(a == 10);
      a = -1; h = -1;
      CHECK(aim_next_view(2040, 100, 10, 100, 50, &a, &h));
      CHECK(a == 1);          // 2040 + 18/2, wrapped
      CHECK(a != 1025);       // long way round
      a = -1; h = -1;
      CHECK(aim_next_view(20, 100, 2040, 100, 50, &a, &h));
      CHECK(a == 6);
      CHECK(a != 1030);       // long way round again
      a = -1; h = -1;
      CHECK(aim_next_view(5, 100, 2000, 100, 0, &a, &h));
      CHECK(a >= 0 && a <= 2047);

      a = -1; h = -1;
      CHECK(aim_next_view(100, 100, 100, 5000, 0, &a, &h));
      CHECK(h == AIM_HORIZ_MAX);
      a = -1; h = -1;
      CHECK(aim_next_view(100, 100, 100, -5000, 0, &a, &h));
      CHECK(h == AIM_HORIZ_MIN);

      // Wanted pitch is clamped before the ease as well as after: clamping only the result gives 299 in one tic.
      a = -1; h = -1;
      CHECK(aim_next_view(100, 100, 100, 5000, 50, &a, &h));
      CHECK(h == 199);
      CHECK(h != AIM_HORIZ_MAX);
      a = -1; h = -1;
      CHECK(aim_next_view(100, 100, 100, -5000, 50, &a, &h));
      CHECK(h == 1);

      // Current pitch is clamped before the subtraction: p.horiz is an unbounded 4-byte read and `desired - current`
      // overflows on a garbage read before any result clamp could act.
      a = -1; h = -1;
      CHECK(aim_next_view(100, 5000, 100, 200, 50, &a, &h));
      CHECK(h == 250);
      CHECK(h != 2600);
      a = -1; h = -1;
      CHECK(aim_next_view(100, -5000, 100, 0, 50, &a, &h));
      CHECK(h == -50);
      a = -1; h = -1;
      CHECK(aim_next_view(100, 9000, 100, 100, 100, &a, &h));
      CHECK(h == AIM_HORIZ_MAX);
      a = -1; h = -1;
      CHECK(aim_next_view(100, -9000, 100, 100, 100, &a, &h));
      CHECK(h == AIM_HORIZ_MIN);
      a = -1; h = -1;
      CHECK(aim_next_view(100, 2147483647, 100, 100, 50, &a, &h));
      CHECK(h >= AIM_HORIZ_MIN && h <= AIM_HORIZ_MAX);
      a = -1; h = -1;
      CHECK(aim_next_view(100, -2147483647 - 1, 100, 100, 50, &a, &h));
      CHECK(h >= AIM_HORIZ_MIN && h <= AIM_HORIZ_MAX);

      {
        const int cs[9] = { -2147483647 - 1, -100000, -100, -99, 100, 299, 300, 100000, 2147483647 };
        const int ws[9] = { -2147483647 - 1, -100000, -100, -99, 100, 299, 300, 100000, 2147483647 };
        const int ps[7] = { -40, 0, 1, 33, 50, 100, 400 };
        bool allIn = true;
        bool anyAngBad = false;
        int swept = 0;
        for (int ci = 0; ci < 9; ci++)
          for (int wi = 0; wi < 9; wi++)
            for (int pi = 0; pi < 7; pi++) {
              int sa = -1, sh = -1;
              if (!aim_next_view(1500, cs[ci], 700, ws[wi], ps[pi], &sa, &sh)) { allIn = false; continue; }
              if (sh < AIM_HORIZ_MIN || sh > AIM_HORIZ_MAX) allIn = false;
              if (sa < 0 || sa > 2047) anyAngBad = true;
              swept++;
            }
        CHECK(swept == 9 * 9 * 7);
        CHECK(allIn);
        CHECK(!anyAngBad);
      }

      a = 4242; h = 2424;
      CHECK(!aim_next_view(100, 100, 500, 150, 0, 0, &h));
      CHECK(h == 2424);
      CHECK(!aim_next_view(100, 100, 500, 150, 0, &a, 0));
      CHECK(a == 4242);
      CHECK(!aim_next_view(100, 100, 500, 150, 0, 0, 0));

      a = -1; h = -1;
      CHECK(aim_next_view(100, 100, 500, 150, -40, &a, &h));
      CHECK(a == 500 && h == 150);
      a = -1; h = -1;
      CHECK(aim_next_view(100, 100, 500, 150, 400, &a, &h));
      CHECK(a == 100 && h == 100);
    }

    // Spike caps read from the binary (ssp VA 0x004f4d58 horizontal, makeitfall VA 0x004dd179 vertical), pinned by
    // value.
    CHECK(AIM_VEL_MAX_STEP == 840);
    CHECK(AIM_VEL_MAX_FALL == 6144);
    // Deliberately different: one cap for both axes would reject every falling enemy.
    CHECK(AIM_VEL_MAX_FALL > AIM_VEL_MAX_STEP);

    CHECK(!aim_vel_spike(0, 0, 0));
    CHECK(!aim_vel_spike(840, 0, 0));       // exactly at the cap is real motion
    CHECK( aim_vel_spike(841, 0, 0));
    CHECK(!aim_vel_spike(0, -840, 0));
    CHECK( aim_vel_spike(0, -841, 0));
    // Horizontal test is on 2D magnitude, not either axis alone: 600,600 is 848 units.
    CHECK( aim_vel_spike(600, 600, 0));
    CHECK(!aim_vel_spike(500, 500, 0));
    CHECK( aim_vel_spike(-600, 600, 0));
    CHECK(!aim_vel_spike(0, 0, 6144));
    CHECK(!aim_vel_spike(0, 0, -6144));
    CHECK( aim_vel_spike(0, 0, 6145));
    CHECK( aim_vel_spike(0, 0, -6145));
    CHECK(!aim_vel_spike(180, -140, 3000));   // running while falling is not a spike
    CHECK( aim_vel_spike(30000, -20000, 400));
    CHECK( aim_vel_spike(0, 0, -2147483647 - 1));
    CHECK( aim_vel_spike(-2147483647 - 1, 0, 0));

    CHECK(AIM_VEL_EMA_TICS == 6);
    CHECK(AIM_VEL_EMA_NUM == 2);
    CHECK(AIM_VEL_EMA_DEN == AIM_VEL_EMA_TICS + 1);
    CHECK(AIM_VEL_EMA_DEN == 7);
    {
      int ox = 11, oy = 22, oz = 33;
      VelSmooth s;
      aim_vel_reset(&s);
      CHECK(!aim_vel_update(0, 1, 2, 3, 4, 5, &ox, &oy, &oz));
      CHECK(ox == 11 && oy == 22 && oz == 33);
      CHECK(!aim_vel_update(&s, 1, 2, 3, 4, 5, 0, &oy, &oz));
      CHECK(!aim_vel_update(&s, 1, 2, 3, 4, 5, &ox, 0, &oz));
      CHECK(!aim_vel_update(&s, 1, 2, 3, 4, 5, &ox, &oy, 0));
      CHECK(ox == 11 && oy == 22 && oz == 33);
      aim_vel_reset(0);   // must not fault
      aim_vel_reset(&s);
      CHECK(s.index == -1);
      CHECK(s.samples == 0);
      CHECK(s.qx == 0 && s.qy == 0 && s.qz == 0);
    }
    {
      VelSmooth s; aim_vel_reset(&s);
      int ox = 0, oy = 0, oz = 0;
      CHECK(aim_vel_update(&s, 7, 1680, 100, -60, 20, &ox, &oy, &oz));
      CHECK(ox == 100 && oy == -60 && oz == 20);
      CHECK(s.index == 7 && s.picnum == 1680 && s.samples == 1);

      // A different picnum at the same index is a different actor (Build recycles sprite slots): history is dropped.
      CHECK(aim_vel_update(&s, 7, 1681, 0, 0, 0, &ox, &oy, &oz));
      CHECK(ox == 0 && oy == 0 && oz == 0);
      CHECK(s.picnum == 1681 && s.samples == 1);

      aim_vel_reset(&s);
      CHECK(aim_vel_update(&s, 7, 1680, 100, -60, 20, &ox, &oy, &oz));
      CHECK(aim_vel_update(&s, 8, 1680, 0, 0, 0, &ox, &oy, &oz));
      CHECK(ox == 0 && oy == 0 && oz == 0);
      CHECK(s.index == 8);
    }
    {
      // Bursty sequence: Duke's actors move in bursts (three tics of nothing, then a step); the raw estimate swings
      // 0..300 every four tics and the smoothed value settles on the mean, 75. Each axis differs so a copy fails.
      VelSmooth s; aim_vel_reset(&s);
      const int bx[4] = { 0, 0, 0, 300 };
      const int by[4] = { 0, 0, 0, -200 };
      const int bz[4] = { 0, 0, 0, 40 };
      int ox = 0, oy = 0, oz = 0;
      int loX = 1 << 30, hiX = -(1 << 30);
      int loY = 1 << 30, hiY = -(1 << 30);
      int loZ = 1 << 30, hiZ = -(1 << 30);
      long long sumX = 0, sumY = 0, sumZ = 0;
      for (int t = 0; t < 160; t++) {
        CHECK(aim_vel_update(&s, 4, 1680, bx[t % 4], by[t % 4], bz[t % 4], &ox, &oy, &oz));
        if (t >= 120) {     // the settled tail, a whole number of 4-tic cycles
          if (ox < loX) loX = ox;   if (ox > hiX) hiX = ox;
          if (oy < loY) loY = oy;   if (oy > hiY) hiY = oy;
          if (oz < loZ) loZ = oz;   if (oz > hiZ) hiZ = oz;
          sumX += ox; sumY += oy; sumZ += oz;
        }
      }
      CHECK(sumX / 40 == 74);   // 74.5, truncated
      CHECK(sumY / 40 == -49);  // -49.5, truncated
      CHECK(sumZ / 40 == 9);    // 9.5, truncated
      CHECK(hiX - loX == 73);
      CHECK(hiX - loX < 300 / 4);
      CHECK(hiY - loY == 49);
      CHECK(hiZ - loZ == 10);
      CHECK(loX > 40 && hiX < 120);
      CHECK(loY > -80 && hiY < -25);
      CHECK(s.samples == AIM_VEL_EMA_TICS);
    }
    {
      // An instant 180: smoothed velocity must not keep leading into the wall the enemy just came from.
      VelSmooth s; aim_vel_reset(&s);
      int ox = 0, oy = 0, oz = 0;
      for (int t = 0; t < 40; t++)
        aim_vel_update(&s, 3, 2000, 200, 150, 0, &ox, &oy, &oz);
      CHECK(ox == 200 && oy == 150);   // settled on the straight run
      int flipped = -1;
      for (int t = 0; t < 12; t++) {
        aim_vel_update(&s, 3, 2000, -200, -150, 0, &ox, &oy, &oz);
        if (flipped < 0 && ox <= 0 && oy <= 0) flipped = t + 1;
      }
      CHECK(flipped == 3);
      CHECK(flipped <= 5);           // "a few tics", stated as the bound
      CHECK(ox < 0 && oy < 0);       // genuinely following the turn
      for (int t = 0; t < 60; t++)
        aim_vel_update(&s, 3, 2000, -200, -150, 0, &ox, &oy, &oz);
      CHECK(ox == -199 && oy == -149);
      CHECK(ox >= -200 && ox <= -199);
      CHECK(oy >= -150 && oy <= -149);
      for (int t = 0; t < 200; t++)
        aim_vel_update(&s, 3, 2000, -200, -150, 0, &ox, &oy, &oz);
      CHECK(ox == -199 && oy == -149);
    }
    {
      // A single teleport-sized spike is ignored, not averaged in: state (sample count included) comes out unchanged.
      VelSmooth s; aim_vel_reset(&s);
      int ox = 0, oy = 0, oz = 0;
      for (int t = 0; t < 30; t++)
        aim_vel_update(&s, 5, 1550, 120, -90, 15, &ox, &oy, &oz);
      const int qx = s.qx, qy = s.qy, qz = s.qz, n = s.samples;
      CHECK(ox == 120 && oy == -90 && oz == 15);
      CHECK(aim_vel_update(&s, 5, 1550, 400000, -300000, 0, &ox, &oy, &oz));
      CHECK(ox == 120 && oy == -90 && oz == 15);
      CHECK(s.qx == qx && s.qy == qy && s.qz == qz);
      CHECK(s.samples == n);
      for (int t = 0; t < 10; t++)
        aim_vel_update(&s, 5, 1550, 400000, -300000, 0, &ox, &oy, &oz);
      CHECK(ox == 120 && oy == -90 && oz == 15);
      CHECK(s.qx == qx && s.qy == qy && s.qz == qz);

      // Also on a state not yet saturated: a mutant advancing `samples` on a spike survived the saturated fixture.
      VelSmooth y; aim_vel_reset(&y);
      CHECK(aim_vel_update(&y, 6, 1550, 100, -70, 12, &ox, &oy, &oz));
      CHECK(aim_vel_update(&y, 6, 1550, 140, -110, 18, &ox, &oy, &oz));
      CHECK(aim_vel_update(&y, 6, 1550, 180, -150, 24, &ox, &oy, &oz));
      CHECK(y.samples == 3);
      CHECK(y.samples < AIM_VEL_EMA_TICS);
      const int yqx = y.qx, yqy = y.qy, yqz = y.qz;
      const int yox = ox, yoy = oy, yoz = oz;
      CHECK(aim_vel_update(&y, 6, 1550, 400000, -300000, 0, &ox, &oy, &oz));
      CHECK(y.samples == 3);
      CHECK(y.qx == yqx && y.qy == yqy && y.qz == yqz);
      CHECK(ox == yox && oy == yoy && oz == yoz);

      VelSmooth f; aim_vel_reset(&f);
      CHECK(aim_vel_update(&f, 9, 1680, 400000, -300000, 0, &ox, &oy, &oz));
      CHECK(ox == 0 && oy == 0 && oz == 0);
      CHECK(f.index == 9 && f.picnum == 1680 && f.samples == 0);
      CHECK(aim_vel_update(&f, 9, 1680, 110, -55, 11, &ox, &oy, &oz));
      CHECK(f.samples == 1);
      CHECK(ox == 31 && oy == -15 && oz == 3);   // 110 * 2/7, from zero (was 2/11: 20, -10, 2)
    }
    {
      // The cap is absolute, not relative to the running average: 800 then a jump to 1500 is a spike absolutely (>840)
      // but not relative to the average (700 < 840).
      VelSmooth s; aim_vel_reset(&s);
      int ox = 0, oy = 0, oz = 0;
      for (int t = 0; t < 20; t++)
        aim_vel_update(&s, 11, 1680, 800, 0, 60, &ox, &oy, &oz);
      CHECK(ox >= 799 && ox <= 800);          // settled near the cap, legally
      CHECK(oz == 60);
      CHECK(!aim_vel_spike(800, 0, 60));      // and 800 really is accepted
      const int qx = s.qx, qy = s.qy, qz = s.qz;
      const int n = s.samples;
      CHECK(aim_vel_spike(1500, 0, 60));      // 1500 really is not
      CHECK(aim_vel_update(&s, 11, 1680, 1500, 0, 60, &ox, &oy, &oz));
      CHECK(s.qx == qx && s.qy == qy && s.qz == qz);   // dropped, not folded
      CHECK(s.samples == n);
      CHECK(ox >= 799 && ox <= 800);
      VelSmooth v; aim_vel_reset(&v);
      for (int t = 0; t < 20; t++)
        aim_vel_update(&v, 12, 1680, 0, -800, -60, &ox, &oy, &oz);
      CHECK(oy <= -799 && oy >= -800);
      const int vqy = v.qy;
      CHECK(aim_vel_update(&v, 12, 1680, 0, -1500, -60, &ox, &oy, &oz));
      CHECK(v.qy == vqy);
      CHECK(oy <= -799 && oy >= -800);
    }

    {
      CHECK(view_write_source(VW_AIM_TRACK, VF_ANG)              == VWS_ANG);
      CHECK(view_write_source(VW_AIM_TRACK, VF_HORIZ)            == VWS_HORIZ);
      CHECK(view_write_source(VW_AIM_TRACK, VF_ANGVEL)           == VWS_ZERO);
      CHECK(view_write_source(VW_AIM_TRACK, VF_RETURN_TO_CENTER) == VWS_ZERO);
      CHECK(view_write_source(VW_AIM_TRACK, VF_OANG)      == VWS_NONE);
      CHECK(view_write_source(VW_AIM_TRACK, VF_OHORIZ)    == VWS_NONE);
      CHECK(view_write_source(VW_AIM_TRACK, VF_OHORIZOFF) == VWS_NONE);
      CHECK(view_write_source(VW_AIM_TRACK, VF_LOOK_ANG)  == VWS_NONE);
      CHECK(view_write_source(VW_AIM_TRACK, VF_HORIZOFF)  == VWS_NONE);

      CHECK(view_write_source(VW_FREEZE_COLLAPSE, VF_ANG)              == VWS_ANG);
      CHECK(view_write_source(VW_FREEZE_COLLAPSE, VF_OANG)             == VWS_ANG);
      CHECK(view_write_source(VW_FREEZE_COLLAPSE, VF_HORIZ)            == VWS_HORIZ);
      CHECK(view_write_source(VW_FREEZE_COLLAPSE, VF_OHORIZ)           == VWS_HORIZ);
      CHECK(view_write_source(VW_FREEZE_COLLAPSE, VF_OHORIZOFF)        == VWS_HORIZOFF);
      CHECK(view_write_source(VW_FREEZE_COLLAPSE, VF_ANGVEL)           == VWS_ZERO);
      CHECK(view_write_source(VW_FREEZE_COLLAPSE, VF_RETURN_TO_CENTER) == VWS_ZERO);
      CHECK(view_write_source(VW_FREEZE_COLLAPSE, VF_LOOK_ANG)         == VWS_NONE);
      CHECK(view_write_source(VW_FREEZE_COLLAPSE, VF_HORIZOFF)         == VWS_NONE);

      int differ = 0, same = 0;
      for (int f = 0; f < VF_COUNT; f++) {
        if (view_write_source(VW_AIM_TRACK, f) == view_write_source(VW_FREEZE_COLLAPSE, f)) same++;
        else differ++;
      }
      CHECK(differ == 3);
      CHECK(same == VF_COUNT - 3);
      CHECK(VF_COUNT == 9);
      CHECK(view_write_source(VW_AIM_TRACK, VF_COUNT)      == VWS_NONE);
      CHECK(view_write_source(VW_FREEZE_COLLAPSE, VF_COUNT) == VWS_NONE);
      CHECK(view_write_source(VW_AIM_TRACK, -1)             == VWS_NONE);
      CHECK(view_write_source(VW_FREEZE_COLLAPSE, 999)      == VWS_NONE);
    }

    // The value each field receives, not just its source: mutating engine.cpp's `case VWS_HORIZOFF: v = horizoff` to
    // `v = 0` once stayed green (pitch wobble during weapon kick). Inputs distinct and nonzero so the arms differ.
    {
      const long ANG = 916, HORIZ = 118, HZOFF = -37;
      long v = 0;

      v = 0; CHECK( view_write_value(VW_FREEZE_COLLAPSE, VF_ANG,   ANG, HORIZ, HZOFF, &v));
      CHECK(v == 916);
      v = 0; CHECK( view_write_value(VW_FREEZE_COLLAPSE, VF_OANG,  ANG, HORIZ, HZOFF, &v));
      CHECK(v == 916);
      v = 0; CHECK( view_write_value(VW_FREEZE_COLLAPSE, VF_HORIZ, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 118);
      v = 0; CHECK( view_write_value(VW_FREEZE_COLLAPSE, VF_OHORIZ, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 118);
      v = 0; CHECK( view_write_value(VW_FREEZE_COLLAPSE, VF_OHORIZOFF, ANG, HORIZ, HZOFF, &v));
      CHECK(v == -37);
      CHECK(v != 0);
      CHECK(v != ANG);
      CHECK(v != HORIZ);
      v = 99; CHECK( view_write_value(VW_FREEZE_COLLAPSE, VF_ANGVEL, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 0);
      v = 99; CHECK( view_write_value(VW_FREEZE_COLLAPSE, VF_RETURN_TO_CENTER, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 0);
      v = 4242; CHECK(!view_write_value(VW_FREEZE_COLLAPSE, VF_LOOK_ANG, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 4242);
      v = 4242; CHECK(!view_write_value(VW_FREEZE_COLLAPSE, VF_HORIZOFF, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 4242);

      v = 0; CHECK( view_write_value(VW_AIM_TRACK, VF_ANG,   ANG, HORIZ, HZOFF, &v));
      CHECK(v == 916);
      v = 0; CHECK( view_write_value(VW_AIM_TRACK, VF_HORIZ, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 118);
      v = 99; CHECK( view_write_value(VW_AIM_TRACK, VF_ANGVEL, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 0);
      v = 99; CHECK( view_write_value(VW_AIM_TRACK, VF_RETURN_TO_CENTER, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 0);
      v = 4242; CHECK(!view_write_value(VW_AIM_TRACK, VF_OANG,      ANG, HORIZ, HZOFF, &v));
      CHECK(v == 4242);
      v = 4242; CHECK(!view_write_value(VW_AIM_TRACK, VF_OHORIZ,    ANG, HORIZ, HZOFF, &v));
      CHECK(v == 4242);
      v = 4242; CHECK(!view_write_value(VW_AIM_TRACK, VF_OHORIZOFF, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 4242);
      CHECK(!view_write_value(VW_FREEZE_COLLAPSE, VF_ANG, ANG, HORIZ, HZOFF, 0));
      v = 4242; CHECK(!view_write_value(VW_AIM_TRACK, VF_COUNT, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 4242);
      v = 4242; CHECK(!view_write_value(VW_FREEZE_COLLAPSE, -1, ANG, HORIZ, HZOFF, &v));
      CHECK(v == 4242);
    }
    {
      const long ANG = 916, HORIZ = 118, HZOFF = -37;
      const long before[VF_COUNT] = { 11, 22, 33, 44, 55, 66, 77, 88, 99 };
      long freeze[VF_COUNT], aim[VF_COUNT];
      for (int f = 0; f < VF_COUNT; f++) { freeze[f] = before[f]; aim[f] = before[f]; }
      for (int f = 0; f < VF_COUNT; f++) {
        long v = 0;
        if (view_write_value(VW_FREEZE_COLLAPSE, f, ANG, HORIZ, HZOFF, &v)) freeze[f] = v;
        v = 0;
        if (view_write_value(VW_AIM_TRACK, f, ANG, HORIZ, HZOFF, &v)) aim[f] = v;
      }
      CHECK(freeze[VF_ANG] == freeze[VF_OANG]);
      CHECK(freeze[VF_ANG] == ANG);
      CHECK(freeze[VF_HORIZ] == freeze[VF_OHORIZ]);
      CHECK(freeze[VF_HORIZ] == HORIZ);
      CHECK(freeze[VF_OHORIZOFF] == HZOFF);
      CHECK(freeze[VF_OHORIZOFF] != before[VF_OHORIZOFF]);
      CHECK(freeze[VF_ANGVEL] == 0 && freeze[VF_RETURN_TO_CENTER] == 0);
      CHECK(freeze[VF_LOOK_ANG] == before[VF_LOOK_ANG]);
      CHECK(freeze[VF_HORIZOFF] == before[VF_HORIZOFF]);
      CHECK(aim[VF_OANG] == before[VF_OANG]);
      CHECK(aim[VF_OHORIZ] == before[VF_OHORIZ]);
      CHECK(aim[VF_OHORIZOFF] == before[VF_OHORIZOFF]);
      CHECK(aim[VF_ANG] == ANG && aim[VF_HORIZ] == HORIZ);
      CHECK(aim[VF_ANGVEL] == 0 && aim[VF_RETURN_TO_CENTER] == 0);
      CHECK(aim[VF_LOOK_ANG] == before[VF_LOOK_ANG]);
      CHECK(aim[VF_HORIZOFF] == before[VF_HORIZOFF]);
      int differ = 0;
      for (int f = 0; f < VF_COUNT; f++) if (freeze[f] != aim[f]) differ++;
      CHECK(differ == 3);
    }

    // Post-lead fallback: if the lead pushes the solution out of the engine's envelope, dropping the tic loses a
    // hittable target; falling back to raw keeps the assist on the enemy.
    CHECK(aim_choose_solution(true, 350, true, 120) == ASC_RAW);
    CHECK(aim_choose_solution(true, -400, true, 120) == ASC_RAW);
    CHECK(aim_choose_solution(true, 120, true, 118) == ASC_LED);
    CHECK(aim_choose_solution(true, AIM_HORIZ_MAX, true, 118) == ASC_LED);
    CHECK(aim_choose_solution(true, AIM_HORIZ_MIN, true, 118) == ASC_LED);
    CHECK(aim_choose_solution(true, 350, true, 400) == ASC_NONE);
    CHECK(aim_choose_solution(true, 350, false, 120) == ASC_NONE);
    CHECK(aim_choose_solution(false, 120, false, 118) == ASC_NONE);
    CHECK(aim_choose_solution(false, 120, true, 118) == ASC_RAW);
    CHECK(aim_choose_solution(false, 0, true, AIM_HORIZ_MAX) == ASC_RAW);
    CHECK(aim_choose_solution(false, 0, true, AIM_HORIZ_MAX + 1) == ASC_NONE);
    for (int hh = AIM_HORIZ_MIN - 2; hh <= AIM_HORIZ_MAX + 2; hh++) {
      CHECK((aim_choose_solution(true, hh, true, 100) == ASC_LED) == aim_horiz_reachable(hh));
      CHECK((aim_choose_solution(false, 0, true, hh) == ASC_RAW) == aim_horiz_reachable(hh));
    }

    // Shot origin: shoot's own sz (VA 0x004f36c0-0x004f36d3) is posz + pyoff + 0x400; Build z points down.
    CHECK(AIM_SHOT_Z_BIAS == 0x400);
    CHECK(AIM_SHOT_Z_BIAS == 1024);
    CHECK(aim_shot_origin_z(0, 0) == 1024);
    CHECK(aim_shot_origin_z(-40000, 0) == -40000 + 1024);
    CHECK(aim_shot_origin_z(-40000, 512) == -40000 + 512 + 1024);
    CHECK(aim_shot_origin_z(-40000, -512) == -40000 - 512 + 1024);
    CHECK(aim_shot_origin_z(12345, 678) == 12345 + 678 + 1024);
    CHECK(aim_shot_origin_z(-40000, 100) > -40000);   // always below the eye for real bob
    CHECK(aim_shot_origin_z(2147483647, 1000) == 2147483647);
    CHECK(aim_shot_origin_z(-2147483647 - 1, -2000) == -2147483647 - 1);
    CHECK(aim_shot_origin_z(-2147483647 - 1, -1000) == (-2147483647 - 1) + 24);

    {
      const int Z = 0, YR = 40, TS = 64;
      int pr[AIM_PROBE_MAX];
      // PORT DIVERGENCE: this port appends rung 3, the centre, because it ships aimpoint=upper where rungs 0 and 1
      // collapse into the top 18.75% of the sprite (target.h's banner). Megaton's check stays here, disabled.
      // CHECK(AIM_PROBE_MAX == 3);   // Megaton: 3. This port: 4.
      CHECK(AIM_PROBE_MAX == 4);

      int n = aim_probe_zs(Z, YR, TS, AP_CENTRE, pr, AIM_PROBE_MAX);
      CHECK(n == 3);
      CHECK(pr[0] == -5120);
      CHECK(pr[1] == -7680);
      CHECK(pr[2] == -9600);
      CHECK(pr[0] == target_aim_z(Z, YR, TS, AP_CENTRE));
      CHECK(pr[0] > pr[1]);
      CHECK(pr[1] > pr[2]);

      n = aim_probe_zs(Z, YR, TS, AP_UPPER, pr, AIM_PROBE_MAX);
      // CHECK(n == 2);   // Megaton: the duplicate leaves two. This port: three,
      // the third being the centre at z - 2h = -5120, tried last.
      CHECK(n == 3);
      CHECK(pr[2] == -5120);
      CHECK(pr[0] == -7680);
      CHECK(pr[0] == target_aim_z(Z, YR, TS, AP_UPPER));
      CHECK(pr[1] == -9600);

      n = aim_probe_zs(-33000, YR, TS, AP_CENTRE, pr, AIM_PROBE_MAX);
      CHECK(n == 3);
      CHECK(pr[0] == -33000 - 5120);
      CHECK(pr[1] == -33000 - 7680);
      CHECK(pr[2] == -33000 - 9600);

      n = aim_probe_zs(-33000, 0, TS, AP_CENTRE, pr, AIM_PROBE_MAX);
      CHECK(n == 1);
      CHECK(pr[0] == -33000);
      n = aim_probe_zs(-33000, YR, 0, AP_CENTRE, pr, AIM_PROBE_MAX);
      CHECK(n == 1);

      int one[1] = { 4242 };
      CHECK(aim_probe_zs(Z, YR, TS, AP_CENTRE, one, 1) == 1);
      CHECK(one[0] == -5120);
      CHECK(aim_probe_zs(Z, YR, TS, AP_CENTRE, pr, 0) == 0);
      CHECK(aim_probe_zs(Z, YR, TS, AP_CENTRE, pr, -3) == 0);
      CHECK(aim_probe_zs(Z, YR, TS, AP_CENTRE, 0, AIM_PROBE_MAX) == 0);
      int guard[4] = { 0, 0, 7777, 7777 };
      CHECK(aim_probe_zs(Z, YR, TS, AP_CENTRE, guard, 2) == 2);
      CHECK(guard[2] == 7777 && guard[3] == 7777);
    }

    // First visible wins: first probe blocked, a later one not (an enemy behind cover with only its head showing).
    {
      const bool headOnly[3]     = { false, false, true };
      const bool shoulders[3]    = { false, true,  true };
      const bool fullyVisible[3] = { true,  true,  true };
      const bool blocked[3]      = { false, false, false };
      CHECK(aim_first_visible(headOnly, 3) == 2);
      CHECK(aim_first_visible(shoulders, 3) == 1);
      CHECK(aim_first_visible(fullyVisible, 3) == 0);
      CHECK(aim_first_visible(blocked, 3) == -1);
      const bool middleOnly[3] = { false, true, false };
      CHECK(aim_first_visible(middleOnly, 3) == 1);
      const bool firstAndLast[3] = { true, false, true };
      CHECK(aim_first_visible(firstAndLast, 3) == 0);
      CHECK(aim_first_visible(headOnly, 2) == -1);
      CHECK(aim_first_visible(shoulders, 2) == 1);
      CHECK(aim_first_visible(fullyVisible, 1) == 0);
      CHECK(aim_first_visible(0, 3) == -1);
      CHECK(aim_first_visible(headOnly, 0) == -1);
      CHECK(aim_first_visible(headOnly, -1) == -1);
    }

    // Reachability gate: a pitch the engine's clamp would refuse is not a shot the player can take (clamping aimed at
    // the wall below a turret).
    CHECK( aim_horiz_reachable(100));
    CHECK( aim_horiz_reachable(AIM_HORIZ_MIN));
    CHECK( aim_horiz_reachable(AIM_HORIZ_MAX));
    CHECK(!aim_horiz_reachable(AIM_HORIZ_MIN - 1));
    CHECK(!aim_horiz_reachable(AIM_HORIZ_MAX + 1));
    CHECK(!aim_horiz_reachable(350));      // the turret above a ledge
    CHECK(!aim_horiz_reachable(-500));
    CHECK(!aim_horiz_reachable(2147483647));
    CHECK(!aim_horiz_reachable(-2147483647 - 1));
    for (int hh = AIM_HORIZ_MIN - 3; hh <= AIM_HORIZ_MAX + 3; hh++)
      CHECK(aim_horiz_reachable(hh) == (aim_horiz_clamped(hh) == hh));

    // End to end: a turret high above needs a pitch outside the envelope, from a real solve (one metre away, two up).
    {
      Point3 eye;  eye.x = 0; eye.y = 0; eye.z = 0;
      Point3 high; high.x = 512; high.y = 0; high.z = -16384;
      const AimSolution up = target_solve(eye, high, 0, target_weapon_info(1));
      CHECK(up.valid);
      CHECK(up.horiz > AIM_HORIZ_MAX);
      CHECK(!aim_horiz_reachable(up.horiz));
      Point3 level; level.x = 1024; level.y = 0; level.z = -100;
      const AimSolution lv = target_solve(eye, level, 0, target_weapon_info(1));
      CHECK(lv.valid);
      CHECK(aim_horiz_reachable(lv.horiz));
    }

    CHECK( aim_candidate_ok(true, true, true, true, true, true));
    CHECK(!aim_candidate_ok(true, true, true, true, false, true));
    CHECK(!aim_candidate_ok(false, true, true, true, true, true));
    CHECK(!aim_candidate_ok(true, false, true, true, true, true));
    CHECK(!aim_candidate_ok(true, true, false, true, true, true));
    CHECK(!aim_candidate_ok(true, true, true, false, true, true));
    {
      bool allFive = true, anyFour = false;
      for (int m = 0; m < 32; m++) {
        const bool b0 = (m & 1) != 0, b1 = (m & 2) != 0, b2 = (m & 4) != 0;
        const bool b3 = (m & 8) != 0, b4 = (m & 16) != 0;
        const bool r = aim_candidate_ok(b0, b1, b2, b3, b4, true);
        if (m == 31) { if (!r) allFive = false; }
        else if (r) anyFour = true;
      }
      CHECK(allFive);
      CHECK(!anyFour);
    }

    // Grounded actors get vz = 0. Constants read from the actor mover, pinned by value: 256 is the height the engine
    // parks a landed actor at (VA 0x004dd133/0x004dd1da), 176 one tic of its gravity (`gc` VA 0x005ceb38, used
    // 0x004dd187).
    CHECK(AIM_FALL_FLOOR_OFFSET == 256);
    CHECK(AIM_FALL_GRAVITY == 176);
    CHECK(AIM_GROUND_MARGIN == 608);
    CHECK(AIM_GROUND_MARGIN > AIM_FALL_FLOOR_OFFSET);
    CHECK(AIM_GROUND_MARGIN - AIM_FALL_FLOOR_OFFSET > AIM_FALL_GRAVITY);
    CHECK(AIM_GROUND_MARGIN - AIM_FALL_FLOOR_OFFSET > 320);

    {
      const int FLOOR = -8192;

      CHECK( aim_actor_grounded(FLOOR - AIM_FALL_FLOOR_OFFSET, FLOOR, AIM_GROUND_MARGIN));
      CHECK( aim_actor_grounded(FLOOR, FLOOR, AIM_GROUND_MARGIN));
      CHECK( aim_actor_grounded(FLOOR + 4096, FLOOR, AIM_GROUND_MARGIN));
      CHECK( aim_actor_grounded(FLOOR - AIM_GROUND_MARGIN, FLOOR, AIM_GROUND_MARGIN));
      CHECK(!aim_actor_grounded(FLOOR - AIM_GROUND_MARGIN - 1, FLOOR, AIM_GROUND_MARGIN));
      CHECK(!aim_actor_grounded(FLOOR - 16384, FLOOR, AIM_GROUND_MARGIN));
      CHECK(!aim_actor_grounded(FLOOR - 2048, FLOOR, AIM_GROUND_MARGIN));
      CHECK( aim_actor_grounded(FLOOR - 2048, FLOOR, 4096));
      CHECK( aim_actor_grounded(FLOOR - 2048, FLOOR, 2048));
      CHECK(!aim_actor_grounded(FLOOR - 2048, FLOOR, 2047));
      CHECK(!aim_actor_grounded(FLOOR - 300, FLOOR, 256));
      CHECK( aim_actor_grounded(FLOOR - 300, FLOOR, 608));
      // Widened rather than wrapped: `floorz - margin` in plain int is undefined at the range bottom (INT_MIN - 608
      // wraps positive and would call an actor at z=0 not grounded far below the floor).
      CHECK( aim_actor_grounded(0, -2147483647 - 1, AIM_GROUND_MARGIN));
      CHECK(!aim_actor_grounded(0, 2147483647, AIM_GROUND_MARGIN));
      CHECK(!aim_actor_grounded(-2147483647 - 1, 2147483647, AIM_GROUND_MARGIN));
      CHECK( aim_actor_grounded(2147483647, -2147483647 - 1, AIM_GROUND_MARGIN));
    }

    {
      const int FLOOR = -8192;
      // Bug measured in game: a trooper standing still reporting +298 z/tic; must be zero.
      CHECK(aim_grounded_vz(FLOOR - AIM_FALL_FLOOR_OFFSET, FLOOR, 298, AIM_GROUND_MARGIN) == 0);
      CHECK(aim_grounded_vz(FLOOR - AIM_FALL_FLOOR_OFFSET, FLOOR, 272, AIM_GROUND_MARGIN) == 0);
      CHECK(aim_grounded_vz(FLOOR - AIM_FALL_FLOOR_OFFSET, FLOOR, 320, AIM_GROUND_MARGIN) == 0);
      CHECK(aim_grounded_vz(FLOOR - AIM_FALL_FLOOR_OFFSET, FLOOR, -298, AIM_GROUND_MARGIN) == 0);
      CHECK(aim_grounded_vz(FLOOR - 16384, FLOOR, 1024, AIM_GROUND_MARGIN) == 1024);
      CHECK(aim_grounded_vz(FLOOR - 16384, FLOOR, -1024, AIM_GROUND_MARGIN) == -1024);
      CHECK(aim_grounded_vz(FLOOR - 2048, FLOOR, 512, AIM_GROUND_MARGIN) == 512);
      CHECK(aim_grounded_vz(FLOOR - AIM_GROUND_MARGIN, FLOOR, 777, AIM_GROUND_MARGIN) == 0);
      CHECK(aim_grounded_vz(FLOOR - AIM_GROUND_MARGIN - 1, FLOOR, 777, AIM_GROUND_MARGIN) == 777);
      CHECK(aim_grounded_vz(FLOOR - AIM_FALL_FLOOR_OFFSET, FLOOR, 0, AIM_GROUND_MARGIN) == 0);
      for (int d = 0; d <= 900; d += 37) {
        const int zz = FLOOR - d;
        const int got = aim_grounded_vz(zz, FLOOR, 298, AIM_GROUND_MARGIN);
        CHECK(got == (aim_actor_grounded(zz, FLOOR, AIM_GROUND_MARGIN) ? 0 : 298));
      }
    }

    {
      const int FLOOR = -8192;
      const int GZ = FLOOR - AIM_FALL_FLOOR_OFFSET;     // grounded
      const int FZ = FLOOR - 16384;                     // flying
      Point3 eye;  eye.x = 0; eye.y = 0; eye.z = -12000;
      Point3 grounded; grounded.x = 6000; grounded.y = 2000; grounded.z = GZ;
      Point3 flyer;    flyer.x = 6000;    flyer.y = 2000;    flyer.z = FZ;
      const WeaponInfo rpg = target_weapon_info(4);
      const int VX = 140, VY = -90, VZ = 298;

      const Point3 gLed = target_lead(eye, grounded, VX, VY,
                                      aim_grounded_vz(GZ, FLOOR, VZ, AIM_GROUND_MARGIN),
                                      rpg.vel);
      const Point3 gRaw = target_lead(eye, grounded, VX, VY, VZ, rpg.vel);
      CHECK(gLed.x == gRaw.x);
      CHECK(gLed.y == gRaw.y);
      CHECK(gLed.x != grounded.x);     // and there IS horizontal lead to keep
      CHECK(gLed.y != grounded.y);
      CHECK(gLed.z == grounded.z);
      CHECK(gRaw.z > grounded.z + 1000);   // Build z points down: the old one sank
      CHECK(gLed.z < gRaw.z);

      const Point3 fLed = target_lead(eye, flyer, VX, VY,
                                      aim_grounded_vz(FZ, FLOOR, -1024, AIM_GROUND_MARGIN),
                                      rpg.vel);
      CHECK(fLed.z != flyer.z);
      CHECK(fLed.z < flyer.z);         // rising (z decreases upward)
      CHECK(fLed.x != flyer.x);
      CHECK(fLed.y != flyer.y);
    }

    // The smoother never sees the artefact: applied before it, so the average is never poisoned.
    {
      const int FLOOR = -8192;
      const int GZ = FLOOR - AIM_FALL_FLOOR_OFFSET;
      VelSmooth s; aim_vel_reset(&s);
      int ox = 0, oy = 0, oz = 0;
      for (int t = 0; t < 30; t++) {
        const int vz = aim_grounded_vz(GZ, FLOOR, 272 + (t % 3) * 24, AIM_GROUND_MARGIN);
        aim_vel_update(&s, 21, 1680, 140, -90, vz, &ox, &oy, &oz);
      }
      CHECK(oz == 0);
      CHECK(ox == 140);
      CHECK(oy == -90);
      // The raw artefact instead: average lands near 298, the number that sank the rockets.
      VelSmooth b; aim_vel_reset(&b);
      for (int t = 0; t < 30; t++)
        aim_vel_update(&b, 21, 1680, 140, -90, 272 + (t % 3) * 24, &ox, &oy, &oz);
      CHECK(oz > 280 && oz < 320);
    }

    {
      // weaponhit layout (72 bytes, from duke3d.pdb via Ghidra): dispicnum +16, temp_data +48, count 6.
      CHECK(rva::HITTYPE_STRIDE == 72);
      CHECK(rva::HITTYPE_DISPICNUM == 16);
      CHECK(rva::HITTYPE_TEMPDATA == 48);
      CHECK(rva::HITTYPE_TEMPDATA_COUNT == 6);
      // dispicnum is a short wedged between actorstayput (+14) and timetosleep (+18).
      CHECK(rva::HITTYPE_DISPICNUM + 2 <= rva::HITTYPE_FLOORZ);
      CHECK(rva::HITTYPE_DISPICNUM > 14);
      CHECK(rva::HITTYPE_DISPICNUM != rva::HITTYPE_FLOORZ);
      CHECK(rva::HITTYPE_TEMPDATA == rva::HITTYPE_BPOSZ + 4);
      CHECK(hittype_temp_data_offset(rva::HITTYPE_TEMPDATA_COUNT - 1) + 4
            == rva::HITTYPE_STRIDE);
      CHECK(hittype_temp_data_offset(0) == 48);
      CHECK(hittype_temp_data_offset(1) == 52);
      CHECK(hittype_temp_data_offset(4) == 64);
      CHECK(hittype_temp_data_offset(5) == 68);
      for (int slot = 0; slot < rva::HITTYPE_TEMPDATA_COUNT; slot++) {
        CHECK(hittype_temp_data_slot_ok(slot));
        CHECK(hittype_temp_data_offset(slot) + 4 <= rva::HITTYPE_STRIDE);
        CHECK(hittype_temp_data_offset(slot) >= rva::HITTYPE_TEMPDATA);
      }
      CHECK(!hittype_temp_data_slot_ok(-1));
      CHECK(!hittype_temp_data_slot_ok(rva::HITTYPE_TEMPDATA_COUNT));
      CHECK(!hittype_temp_data_slot_ok(6));
      CHECK(!hittype_temp_data_slot_ok(-2147483647 - 1));
    }

    // temp_data[4] is the CON interpreter's pointer to the action's five longs. This build compiles CON into a heap
    // buffer, so bounds are not knowable statically (engine-facts.md section D): the only guard is asking the OS
    // whether the 20 bytes are readable, then sanity-checking what comes back.
    {
      CHECK( action_plausible(50, 5, 1));      // a death action: ATROOPDYING 50 5 1 1 16
      CHECK( action_plausible(0, 1, 5));
      CHECK( action_plausible(1023, 64, 8));   // both upper bounds, inclusive
      CHECK( action_plausible(0, 1, 3) && action_plausible(0, 1, 7));
      CHECK(!action_plausible(0, 1, 0));       // viewtype 0 is not one the CON uses
      CHECK(!action_plausible(0, 1, 2));
      CHECK(!action_plausible(0, 1, 4));
      CHECK(!action_plausible(0, 1, 6));
      CHECK(!action_plausible(0, 1, 9));
      CHECK(!action_plausible(0, 0, 1));       // numframes 0 is not an action
      CHECK(!action_plausible(0, 65, 1));
      CHECK(!action_plausible(0, -1, 1));
      CHECK(!action_plausible(-1, 5, 1));
      CHECK(!action_plausible(1024, 5, 1));
      CHECK(!action_plausible(2000000000, 5, 1));
    }
    {
      long buf[ACTION_LONGS] = { 50, 5, 1, 1, 16 };
      CHECK(ACTION_BYTES == 20);
      CHECK((unsigned)(ACTION_LONGS * 4) == ACTION_BYTES);

      action_query_fn() = 0;
      ActorAction none = action_read((unsigned)(size_t)buf);
      CHECK(!none.readable);
      CHECK(!none.plausible);

      action_query_fn() = q_stub_refuse;      // returns 0, records addr and bytes
      ActorAction refused = action_read((unsigned)(size_t)buf);
      CHECK(!refused.readable && !refused.plausible);
      CHECK(q_stub_last_bytes == ACTION_BYTES);          // it asked about all 20
      CHECK(q_stub_last_addr == (const void*)buf);

      action_query_fn() = q_stub_accept;
      ActorAction ok = action_read((unsigned)(size_t)buf);
      CHECK(ok.readable && ok.plausible);
      CHECK(ok.startframe == 50 && ok.numframes == 5);
      CHECK(ok.viewtype == 1 && ok.incval == 1 && ok.delay == 16);

      long junk[ACTION_LONGS] = { 999999, 70000, 4, 0, 0 };
      ActorAction bad = action_read((unsigned)(size_t)junk);
      CHECK(bad.readable);
      CHECK(!bad.plausible);
      CHECK(bad.viewtype == 4);           // the values are still reported

      ActorAction zero = action_read(0);
      CHECK(!zero.readable && !zero.plausible);

      action_query_fn() = 0;              // leave the seam as it was found
    }

    CHECK(AIM_LOST_WINDOW_MS == 3000);
    CHECK(AIM_DIAG_WINDOW_MS == 3000);
    CHECK( aim_throttle_due(0, 0, true, AIM_LOST_WINDOW_MS));
    CHECK( aim_throttle_due(500000, 499999, true, AIM_LOST_WINDOW_MS));
    CHECK(!aim_throttle_due(100000, 100000, false, AIM_LOST_WINDOW_MS));
    CHECK(!aim_throttle_due(102999, 100000, false, AIM_LOST_WINDOW_MS));
    CHECK( aim_throttle_due(103000, 100000, false, AIM_LOST_WINDOW_MS));
    CHECK( aim_throttle_due(199999, 100000, false, AIM_LOST_WINDOW_MS));
    CHECK( aim_throttle_due(100500, 100000, false, 500));
    CHECK(!aim_throttle_due(100499, 100000, false, 500));
    // GetTickCount wraps after ~49 days; unsigned subtraction must keep that a small difference, not a stall.
    CHECK( aim_throttle_due(2000u, 4294966296u, false, AIM_LOST_WINDOW_MS));   // 3000 apart
    CHECK(!aim_throttle_due(1000u, 4294966296u, false, AIM_LOST_WINDOW_MS));   // 2000 apart
    // A signed-subtraction mutant passed the pair above (agrees with modular under INT_MAX); these pin past INT_MAX.
    CHECK( aim_throttle_due(0x90000000u, 0u, false, AIM_LOST_WINDOW_MS));
    CHECK( aim_throttle_due(0x80000BB8u, 0u, false, AIM_LOST_WINDOW_MS));
    CHECK( aim_throttle_due(0xC0000000u, 0x40000000u, false, AIM_LOST_WINDOW_MS));
    CHECK( aim_throttle_due(3000u, 0u, false, AIM_LOST_WINDOW_MS) ==
           aim_throttle_due(0x80000BB8u, 0x80000000u, false, AIM_LOST_WINDOW_MS));
    CHECK(!aim_throttle_due(0x80000BB7u, 0x80000000u, false, AIM_LOST_WINDOW_MS));

    // Presence set must not survive a level change: the new level's enemies against the old index set would burst
    // `lost enemy:` lines.
    CHECK(AIM_PRESENCE_MAX_GAP_MS == 1000);
    {
      const unsigned T0 = 500000u, T1 = T0 + 38u;

      CHECK( aim_presence_reset(12, 214, 37, 502, T0 + 2400u, T0, AIM_PRESENCE_MAX_GAP_MS));
      CHECK( aim_presence_reset(12, 214, 37, 214, T1, T0, AIM_PRESENCE_MAX_GAP_MS));  // sprite only
      CHECK( aim_presence_reset(12, 214, 12, 502, T1, T0, AIM_PRESENCE_MAX_GAP_MS));  // sectors only

      CHECK(!aim_presence_reset(12, 214, 12, 214, T1, T0, AIM_PRESENCE_MAX_GAP_MS));
      CHECK(!aim_presence_reset(12, 214, 12, 214, T0 + 999u, T0, AIM_PRESENCE_MAX_GAP_MS));

      // The time gap is the catch-all for a level change whose signature coincides (two maps can share both numbers).
      CHECK( aim_presence_reset(12, 214, 12, 214, T0 + 2400u, T0, AIM_PRESENCE_MAX_GAP_MS));
      CHECK( aim_presence_reset(12, 214, 12, 214, T0 + 1000u, T0, AIM_PRESENCE_MAX_GAP_MS));
      CHECK(!aim_presence_reset(12, 214, 12, 214, T0 + 999u,  T0, AIM_PRESENCE_MAX_GAP_MS));
      CHECK( aim_presence_reset(12, 214, 12, 214, T0 + 100u, T0, 100u));
      CHECK(!aim_presence_reset(12, 214, 12, 214, T0 + 99u,  T0, 100u));

      CHECK(!aim_presence_reset(12, 214, 12, 214, 500u, 4294967000u, AIM_PRESENCE_MAX_GAP_MS));
      CHECK( aim_presence_reset(12, 214, 12, 214, 1500u, 4294967000u, AIM_PRESENCE_MAX_GAP_MS));
      CHECK( aim_presence_reset(12, 214, 12, 214, 0x90000000u, 0u, AIM_PRESENCE_MAX_GAP_MS));
      for (unsigned g = 0; g <= 2000u; g += 143u)
        CHECK(aim_presence_reset(12, 214, 12, 214, T0 + g, T0, AIM_PRESENCE_MAX_GAP_MS) ==
              aim_throttle_due(T0 + g, T0, false, AIM_PRESENCE_MAX_GAP_MS));

      CHECK( aim_presence_reset(12, 214, 37, 502, T0, T0, AIM_PRESENCE_MAX_GAP_MS));
      CHECK( aim_presence_reset(0, 0, 1, 0, T0, T0, AIM_PRESENCE_MAX_GAP_MS));
      CHECK( aim_presence_reset(0, 0, 0, 1, T0, T0, AIM_PRESENCE_MAX_GAP_MS));
      CHECK(!aim_presence_reset(0, 0, 0, 0, T0, T0, AIM_PRESENCE_MAX_GAP_MS));
      CHECK( aim_presence_reset(12, 214, -1, 214, T1, T0, AIM_PRESENCE_MAX_GAP_MS));
      CHECK( aim_presence_reset(-1, 214, 12, 214, T1, T0, AIM_PRESENCE_MAX_GAP_MS));
    }

    CHECK( aim_lost_should_log(true,  false, true,  40, false));
    CHECK(!aim_lost_should_log(false, false, true,  40, false));  // never had it
    CHECK(!aim_lost_should_log(true,  true,  true,  40, false));  // still here
    CHECK(!aim_lost_should_log(true,  false, false, 40, false));  // slot recycled
    CHECK(!aim_lost_should_log(true,  false, true,  0,  false));  // engine deleted it
    CHECK(!aim_lost_should_log(true,  false, true,  40, true));   // already reported
    CHECK( aim_lost_should_log(true,  false, true,  1,   false));
    CHECK( aim_lost_should_log(true,  false, true,  255, false));
    {
      bool agree = true, sawT = false, sawF = false;
      for (int pl = 0; pl < 2; pl++)
      for (int pn = 0; pn < 2; pn++)
      for (int ps = 0; ps < 2; ps++)
      for (int xr = 0; xr < 2; xr++)
      for (int al = 0; al < 2; al++) {
        const bool got  = aim_lost_should_log(pl != 0, pn != 0, ps != 0, xr ? 40 : 0, al != 0);
        const bool want = (pl != 0) && (pn == 0) && (ps != 0) && (xr != 0) && (al == 0);
        if (got != want) agree = false;
        if (got) sawT = true; else sawF = true;
      }
      CHECK(agree);
      CHECK(sawT);
      CHECK(sawF);
    }

    // Aimlead sub-option: off hands target_lead vel 0 (the hitscan path), so the led point IS the raw point.
    CHECK(aim_lead_vel(644, 1) == 644);
    CHECK(aim_lead_vel(644, 0) == 0);
    CHECK(aim_lead_vel(768, 1) == 768);
    CHECK(aim_lead_vel(768, 0) == 0);
    CHECK(aim_lead_vel(0, 1) == 0);     // a hitscan weapon stays hitscan
    CHECK(aim_lead_vel(0, 0) == 0);
    CHECK(aim_lead_vel(target_weapon_info(4).vel, 1) == 644);   // rpg
    CHECK(aim_lead_vel(target_weapon_info(4).vel, 0) == 0);
    CHECK(aim_lead_vel(target_weapon_info(1).vel, 1) == 0);     // pistol
    {
      Point3 eye; eye.x = 0; eye.y = 0; eye.z = 0;
      Point3 p;   p.x = 6000; p.y = 2000; p.z = -900;
      const WeaponInfo w = target_weapon_info(4);   // rpg, vel 644
      const Point3 on  = target_lead(eye, p, 140, -90, 20, aim_lead_vel(w.vel, 1));
      const Point3 off = target_lead(eye, p, 140, -90, 20, aim_lead_vel(w.vel, 0));
      CHECK(off.x == p.x && off.y == p.y && off.z == p.z);
      CHECK(on.x != p.x && on.y != p.y);
      CHECK(on.x > p.x);      // led along the target's own +x motion
      CHECK(on.y < p.y);      // and its -y motion
    }
  }

void tb_0106_aim_ahead() {
    CHECK(aim_ahead_step(0, 0, AIM_AHEAD_MAX_STEP) == 0);
    CHECK(aim_ahead_step(12345, 12345, AIM_AHEAD_MAX_STEP) == 0);
    CHECK(aim_ahead_step(12385, 12345, AIM_AHEAD_MAX_STEP) == 40);
    CHECK(aim_ahead_step(12305, 12345, AIM_AHEAD_MAX_STEP) == -40);
    CHECK(aim_ahead_step(-12305, -12345, AIM_AHEAD_MAX_STEP) == 40);
    CHECK(aim_ahead_step(-12385, -12345, AIM_AHEAD_MAX_STEP) == -40);
    CHECK(AIM_AHEAD_MAX_STEP == 1024);
    CHECK(aim_ahead_step(AIM_AHEAD_MAX_STEP, 0, AIM_AHEAD_MAX_STEP) == AIM_AHEAD_MAX_STEP);
    CHECK(aim_ahead_step(-AIM_AHEAD_MAX_STEP, 0, AIM_AHEAD_MAX_STEP) == -AIM_AHEAD_MAX_STEP);
    CHECK(aim_ahead_step(1025, 0, AIM_AHEAD_MAX_STEP) == 0);
    CHECK(aim_ahead_step(-1025, 0, AIM_AHEAD_MAX_STEP) == 0);
    // Real measured run steps (+242 diagonal, +600) both pass; the old 256-unit cap passed the diagonal by chance.
    CHECK(aim_ahead_step(242, 0, AIM_AHEAD_MAX_STEP) == 242);
    CHECK(aim_ahead_step(600, 0, AIM_AHEAD_MAX_STEP) == 600);
    CHECK(aim_ahead_step(1000000, 0, AIM_AHEAD_MAX_STEP) == 0);
    // INT_MAX - INT_MIN wraps to -1 in int (inside the cap, wrong direction); widened, it is far above the cap.
    CHECK(aim_ahead_step(2147483647, -2147483647 - 1, AIM_AHEAD_MAX_STEP) == 0);
    CHECK(aim_ahead_step(-2147483647 - 1, 2147483647, AIM_AHEAD_MAX_STEP) == 0);

    // Z cap separately: Build z is 16x finer than xy. AIM_AHEAD_MAX_STEP_Z IS AIM_VEL_MAX_FALL, not a second literal.
    CHECK(AIM_AHEAD_MAX_STEP_Z == AIM_VEL_MAX_FALL);
    CHECK(AIM_AHEAD_MAX_STEP_Z == 6144);
    CHECK(aim_ahead_step(4096, 0, AIM_AHEAD_MAX_STEP_Z) == 4096);
    CHECK(aim_ahead_step(4096, 0, AIM_AHEAD_MAX_STEP) == 0);
    CHECK(aim_ahead_step(AIM_AHEAD_MAX_STEP_Z, 0, AIM_AHEAD_MAX_STEP_Z) == AIM_AHEAD_MAX_STEP_Z);
    CHECK(aim_ahead_step(6145, 0, AIM_AHEAD_MAX_STEP_Z) == 0);

    {
      const Point3 eye = { 1000, -2000, 3000 };
      const Point3 a = aim_ahead_origin(eye, 40, -7, 13);
      CHECK(a.x == 1040 && a.y == -2007 && a.z == 3013);
      const Point3 same = aim_ahead_origin(eye, 0, 0, 0);
      CHECK(same.x == eye.x && same.y == eye.y && same.z == eye.z);
      // Saturation, not wrap: a coordinate at the type's end plus a positive step must not reappear at INT_MIN.
      const Point3 hi = { 2147483647, 2147483647, 2147483647 };
      const Point3 sat = aim_ahead_origin(hi, 40, 1, 256);
      CHECK(sat.x == 2147483647 && sat.y == 2147483647 && sat.z == 2147483647);
      const Point3 lo = { -2147483647 - 1, -2147483647 - 1, -2147483647 - 1 };
      const Point3 satlo = aim_ahead_origin(lo, -40, -1, -256);
      CHECK(satlo.x == -2147483647 - 1 && satlo.y == -2147483647 - 1);
      CHECK(satlo.z == -2147483647 - 1);
    }

    {
      const Point3 raw = { 6000, 500, -4000 };
      const Point3 t = aim_ahead_target(raw, 30, -12, 96);
      CHECK(t.x == 6030 && t.y == 488 && t.z == -3904);
      const Point3 tx = aim_ahead_target(raw, 30, 0, 0);
      CHECK(tx.x == raw.x + 30 && tx.y == raw.y && tx.z == raw.z);
      const Point3 still = aim_ahead_target(raw, 0, 0, 0);
      CHECK(still.x == raw.x && still.y == raw.y && still.z == raw.z);
      const Point3 hi = { 2147483647, 2147483647, 2147483647 };
      const Point3 sat = aim_ahead_target(hi, 1, 840, 6144);
      CHECK(sat.x == 2147483647 && sat.y == 2147483647 && sat.z == 2147483647);
    }

    // Pipeline: an advanced origin moves the solve, pinning the composition aim_tic makes, not just the helpers.
    {
      const WeaponInfo hs = target_weapon_info(1);      // pistol: hitscan
      const Point3 eye = { 0, 0, 0 };
      const Point3 raw = { 4096, 0, 0 };
      const Point3 moved = { 0, 40, 0 };                // one tic of strafe

      const AimSolution stale = target_solve(eye, raw, 0, hs);
      const AimSolution ahead =
          target_solve(aim_ahead_origin(eye, 0, 40, 0), raw, 0, hs);
      const AimSolution byHand = target_solve(moved, raw, 0, hs);
      CHECK(stale.valid && ahead.valid && byHand.valid);
      CHECK(ahead.ang == byHand.ang && ahead.horiz == byHand.horiz);
      CHECK(ahead.ang != stale.ang);                    // the correction is real
      CHECK(aim_shot_angerr(ahead.ang, stale.ang) < 0);

      const AimSolution zero =
          target_solve(aim_ahead_origin(eye, 0, 0, 0), raw, 0, hs);
      CHECK(zero.ang == stale.ang && zero.horiz == stale.horiz);
      CHECK(zero.valid == stale.valid);

      const int warped = aim_ahead_step(1000000, 0, AIM_AHEAD_MAX_STEP);
      const AimSolution warp =
          target_solve(aim_ahead_origin(eye, 0, warped, 0), raw, 0, hs);
      CHECK(warp.ang == stale.ang && warp.horiz == stale.horiz);
    }

    CHECK(aim_shot_angerr(0, 0) == 0);
    CHECK(aim_shot_angerr(1234, 1234) == 0);
    CHECK(aim_shot_angerr(5, 2040) == 13);
    CHECK(aim_shot_angerr(2040, 5) == -13);
    CHECK(aim_shot_angerr(100, 60) == 40);
    CHECK(aim_shot_angerr(60, 100) == -40);
    CHECK(aim_shot_angerr(1024, 0) == -1024);
    CHECK(aim_shot_angerr(0, 1024) == -1024);
    for (int a = 0; a < 2048; a += 7) {
      const int err = aim_shot_angerr(a, 300);
      CHECK(err >= -1024 && err <= 1023);
    }
    CHECK(aim_shot_angerr(4096 + 5, 2040) == 13);
    CHECK(aim_shot_angerr(-2048 + 5, 2040) == 13);
    CHECK(aim_shot_angerr(2147483647, 0) >= -1024);
    CHECK(aim_shot_angerr(-2147483647 - 1, 0) <= 1023);
  }

// aim_hold_view_rule lives in target.h beside aim_should_run and aim_acquire_should_run, the other two gates here.

void tb_0114_hold_view_rule() {
  CHECK( aim_hold_view_rule(true,  false, false));   // the aimbot steers: hold
  CHECK(!aim_hold_view_rule(true,  false, true ));   // the lockout owns input
  CHECK(!aim_hold_view_rule(true,  true,  false));   // the panel owns input
  CHECK(!aim_hold_view_rule(true,  true,  true ));
  CHECK(!aim_hold_view_rule(false, false, false));   // no write last tic: the mouse is the player's
  CHECK(!aim_hold_view_rule(false, false, true ));
  CHECK(!aim_hold_view_rule(false, true,  false));
  CHECK(!aim_hold_view_rule(false, true,  true ));

  for (int m = 0; m < 2; m++)
    for (int l = 0; l < 2; l++)
      CHECK(!aim_hold_view_rule(false, m != 0, l != 0));
  CHECK(!aim_hold_view_rule(true, true, false) && !aim_hold_view_rule(true, false, true));
}

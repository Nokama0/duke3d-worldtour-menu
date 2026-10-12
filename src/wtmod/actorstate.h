#pragma once
#include "target.h"   // TARGET_STAT_ACTOR, TARGET_STAT_ZOMBIEACTOR
#include "common.h"

// Actor-state instrument: logs an enemy's state changes (g_cfg.debuglog; at most 64 actors and 400 lines a session).
// This header decides; actorstate.cpp walks live sprite memory and cannot link into test_host.

const int ACTORSTATE_MAX_TRACKED = 64;
const int ACTORSTATE_MAX_LINES   = 400;
const int ACTORSTATE_CSTAT_MASK  = 0x0001 | 0x0100 | 0x8000;

struct ActorKey   { int extra; int cstatBits; int statnum; int t4; int xrepeat; int pal; int picnum; };
struct ActorTrack { int index; int picnum; ActorKey key; bool used; bool seen; };

inline bool actorstate_changed(const ActorKey& a, const ActorKey& b) {
  return a.extra != b.extra || a.cstatBits != b.cstatBits ||
         a.statnum != b.statnum || a.t4 != b.t4 ||
         a.xrepeat != b.xrepeat || a.pal != b.pal ||
         a.picnum != b.picnum;
}

inline int actorstate_slot_for(ActorTrack* t, int n, int index) {
  if (!t || n <= 0) return -1;
  for (int k = 0; k < n; k++)
    if (t[k].used && t[k].index == index) return k;
  for (int k = 0; k < n; k++) {
    if (t[k].used) continue;
    t[k].used = true;
    t[k].index = index;
    t[k].seen = false;
    return k;
  }
  return -1;
}

inline bool actorstate_budget_ok(int linesSoFar, int cap) { return linesSoFar < cap; }

inline int budget_after_level_change(bool levelChanged, int linesSoFar) {
  return levelChanged ? 0 : linesSoFar;
}

// Enemy state: dying enemies leave the ESP and aimbot. The engine's alive test is `extra < 0` (troopers walk and shoot
// at exactly 0), so zero is alive here too.

enum ActorState { AS_ALIVE = 0, AS_CHOKING, AS_PLAYDEAD, AS_CLOAKED, AS_DYING, AS_DEAD, AS_NONE };
const int AS_CSTAT_HITTABLE  = 0x0100;   // cstat bit 8: hitscan and clipmove see the sprite
const int AS_CSTAT_INVISIBLE = 0x8000;   // cstat bit 15: the renderer skips it (a teleported captain)

inline bool action_record_is(const ActorAction& a, int sf, int nf, int vt, int iv, int dl) {
  return a.readable && a.startframe == sf && a.numframes == nf &&
         a.viewtype == vt && a.incval == iv && a.delay == dl;
}
inline bool actor_record_is_suffering(const ActorAction& self) { return action_record_is(self, 59, 2, 1, 1, 21); }
// ATROOPPLAYDEAD has ATROOPDEAD's contents (54,0,0,0,0), 20 bytes after it, then SUFFERDEAD (58,2,1,-4,24): a "down"
// record is playing dead iff its predecessor is also down and its successor is SUFFERDEAD.
inline bool actor_record_is_playdead(const ActorAction& self, const ActorAction& before, const ActorAction& after) {
  return action_record_is(self, 54, 0, 0, 0, 0) && action_record_is(before, 54, 0, 0, 0, 0) &&
         action_record_is(after, 58, 2, 1, -4, 24);
}

// Assault Commander's death record (30,8,1,1,12) runs several phases with `extra` flickering negative/0 while hittable:
// the record says dying. Keyed on picnum so no other live enemy is hidden.
const int ACTOR_PICNUM_COMMANDER = 1920;
inline bool actor_record_is_commander_dying(const ActorAction& self) {
  return action_record_is(self, 30, 8, 1, 1, 12) ||
         action_record_is(self, 38, 1, 1, 1, 1);
}

// Dead and does not know it: a sitting trooper has no `ifhitweapon` (GAME.CON 4953-4977): hits pile in hittype[i].extra
// for up to 24 tics (30 for LIZTROOPJUSTSIT) until `cactor LIZTROOP` hands off, while sprite.extra reads full health.
// Only this CON-block case is handled.
const int ACTOR_PICNUM_TROOP_ON_TOILET = 1741;
const int ACTOR_PICNUM_TROOP_JUST_SIT  = 1742;
inline bool actor_defers_damage(int picnum) {
  return picnum == ACTOR_PICNUM_TROOP_ON_TOILET || picnum == ACTOR_PICNUM_TROOP_JUST_SIT;
}

// Whether a bullet touches a SLEEPING actor: checkhitsprite tests statnum twice (rva 0x9EA20 dword cmp wakes only when
// `ang == 0`; 0x9EA78 word cmp skips all four hittype writes), so a sleeper at any other angle is neither woken nor
// credited (statnum is sprite+0x1A, ang +0x1C). A Shark read statnum=2 extra=35, then statnum=1 extra=12.
const int ACTOR_PICNUM_SHARK = 1550;
// A picnum list (Shark above, Egg below): the skip is checkhitsprite's tail, a switch on the victim's picnum (jump
// tables 0x74EC94, 0x74EDBC, 0x74EE8C). Only picnums WATCHED refusing damage while dormant are listed (Egg: four
// chaingun shots left extra 20); tbotwt.cpp logs any dormant actor whose health drops.
const int ACTOR_PICNUM_EGG   = 675;
// The Firefly trooper is invulnerable while small: checkhitsprite's default path (rva 0x9E7BF, after badguy()) compares
// picnum 5180 (`cmp word [edi+0x133681E], 0x143C`) and xrepeat < 48 (`cmp byte [edi+0x1336824], 0x30`) and records
// NOTHING.
const int ACTOR_PICNUM_FIREFLY       = 5180;
// Splash bypasses it: hitradius writes hittype[j].extra itself (rva 0x2199D; writers 0x22D90, 0x3E430, 0x3E810, 0x64220
// are not reachable from a player weapon). It regrows on its own schedule (CON 512, `ifcount 50`): wait it out,
// re-asked every tic, never blacklisted.
const int ACTOR_FIREFLY_FULL_XREPEAT = 48;

// An unsettled weapon is exempt: the Incinerator is curr_weapon 12 (DEFS.CON:802), and its flame is a CON actor with
// nothing in .text to trace; holding fire on no evidence is the worse error. Blast weapons: 4 RPG, 5 pipebomb, 7
// devastator, 8 tripbomb, 10 detonator.
const int WEAPON_LAST_SETTLED = 11;   // 12 is World Tour's own, and unsettled
inline bool weapon_has_splash(int currWeapon) {
  return currWeapon == 4 || currWeapon == 5 || currWeapon == 7 ||
         currWeapon == 8 || currWeapon == 10;
}
// Which weapons escape checkhitsprite's discards: those whose damage can also arrive by hitradius, not the weapon
// CLASS: a projectile's direct impact enters checkhitsprite like a bullet (moveprojectiles rva 0x280F1, 0x293B7).
inline bool weapon_escapes_hit_discards(int currWeapon) {
  if (currWeapon < 0 || currWeapon > WEAPON_LAST_SETTLED) return true;
  return weapon_has_splash(currWeapon);
}

inline bool actor_damage_lands(int picnum, int statnum, int ang, int xrepeat,
                               bool splashCapable) {
  if (splashCapable) return true;   // hitradius writes hittype[] itself
  if (picnum == ACTOR_PICNUM_FIREFLY && xrepeat < ACTOR_FIREFLY_FULL_XREPEAT)
    return false;
  if (statnum != TARGET_STAT_ZOMBIEACTOR) return true;
  if ((ang & 0xFFFF) == 0) return true;   // the engine wakes it, so the hit lands
  return picnum != ACTOR_PICNUM_SHARK && picnum != ACTOR_PICNUM_EGG;
}

// The health actor_state should judge by. Negative is the point: a hittable actor below zero is AS_DYING. `pending` <=
// 0 (rests at -1) changes nothing.
inline int actor_effective_extra(int picnum, int extra, int pending) {
  if (!actor_defers_damage(picnum) || pending <= 0) return extra;
  return extra - pending;
}

inline ActorState actor_state(int statnum, bool badguy, int xrepeat, int cstat, int extra,
                              bool choking, bool playDead, bool dyingRecord) {
  const bool onList = (statnum == TARGET_STAT_ACTOR || statnum == TARGET_STAT_ZOMBIEACTOR);
  if (!onList || !badguy || xrepeat == 0) return AS_NONE;
  const bool hittable = (cstat & AS_CSTAT_HITTABLE) != 0;
  if (extra < 0) return hittable ? AS_DYING : AS_DEAD;
  if (hittable) return dyingRecord ? AS_DYING : (choking ? AS_CHOKING : AS_ALIVE);
  if (playDead) return AS_PLAYDEAD;
  if (extra > 0) return AS_CLOAKED;   // a corpse resets to 0; above zero and unhittable is withdrawn, not dead
  return AS_DEAD;
}
inline bool actor_state_shown(ActorState s)      { return s == AS_ALIVE || s == AS_CHOKING || s == AS_PLAYDEAD || s == AS_CLOAKED; }
inline bool actor_state_targetable(ActorState s) { return s == AS_ALIVE || s == AS_CHOKING; }
inline bool actor_state_labelled(ActorState s) {
  return s == AS_CHOKING || s == AS_PLAYDEAD || s == AS_CLOAKED;
}
inline const char* actor_state_label(ActorState s) {
  switch (s) { case AS_CHOKING: return "Choking"; case AS_PLAYDEAD: return "Playing dead"; case AS_CLOAKED: return "Cloaked"; default: return nullptr; }
}
inline const char* actor_state_name(ActorState s) {
  switch (s) { case AS_ALIVE: return "alive"; case AS_CHOKING: return "choking"; case AS_PLAYDEAD: return "playdead";
               case AS_CLOAKED: return "cloaked"; case AS_DYING: return "dying"; case AS_DEAD: return "dead"; default: return "none"; }
}

// One state per sprite per tic: the ESP asks several times a frame and record questions cost up to three page queries.
// A byte per sprite plus a known bitset, cleared on a generation change; generation 0 is never real.
struct ActorStateCache { unsigned gen; unsigned char known[rva::MAX_SPRITES / 8]; unsigned char state[rva::MAX_SPRITES]; };
inline void actor_cache_begin(ActorStateCache* c, unsigned gen) {
  if (!c || c->gen == gen) return;
  c->gen = gen;
  for (int b = 0; b < rva::MAX_SPRITES / 8; b++) c->known[b] = 0;
}
inline bool actor_cache_get(const ActorStateCache* c, int i, ActorState* out) {
  if (!c || !out || i < 0 || i >= rva::MAX_SPRITES) return false;
  if (!(c->known[(unsigned)i >> 3] & (unsigned char)(1u << ((unsigned)i & 7u)))) return false;
  *out = (ActorState)c->state[i];
  return true;
}
inline void actor_cache_put(ActorStateCache* c, int i, ActorState s) {
  if (!c || i < 0 || i >= rva::MAX_SPRITES) return;
  c->state[i] = (unsigned char)s;
  c->known[(unsigned)i >> 3] |= (unsigned char)(1u << ((unsigned)i & 7u));
}

void actorstate_tic();
// Cached state of sprite i, once per generation. The generation turns at hooked_processinput's head, not its tail:
// silent aim's re-solve and the shoot-time instrument fire inside processinput and would see a just-killed target's
// previous state.
ActorState actor_state_cached(int i);
ActorState actor_state_live(int i);
void actorstate_install();
void actor_state_generation_begin(unsigned gen);

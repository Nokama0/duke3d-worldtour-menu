#pragma once
#include "project.h"   // PlayerAim
#include "common.h"   // clamp_ll_to_int
#include "target.h"   // aim_within_range: the aimbot's own range rule, reused

// The triggerbot: this header decides everything; tbot.cpp gathers the engine's answers and ORs the fire BIT
// (`sync.bits & 4`) into the input record, not a call to shoot. AIMBOT MODE also casts a ray, along the view or the
// snapshot's angles (silent aim), else silent aim fires into a force field.

const int TBOT_WHEN_CROSSHAIR = 0;   // the crosshair's own hitscan lands on a live enemy
const int TBOT_WHEN_AIMBOT    = 1;   // the aimbot's published target, with line of sight
const int TBOT_DELAY_MS_MIN = 0, TBOT_DELAY_MS_MAX = 500;
const int TBOT_DELAY_MS_DEFAULT = TBOT_DELAY_MS_MIN;
const int TBOT_TICS_PER_SECOND = 30;   // input is sampled 30 times a second
const int TBOT_HELD_TICS_CAP = 1000000;

struct TbotState { int heldTics; bool firing; };
struct TbotCond  { bool holds; const char* reason; };

inline int tbot_when_clamped(int when) { return when == TBOT_WHEN_AIMBOT ? TBOT_WHEN_AIMBOT : TBOT_WHEN_CROSSHAIR; }
inline int tbot_delay_ms_clamped(int ms) {
  return ms < TBOT_DELAY_MS_MIN ? TBOT_DELAY_MS_MIN : (ms > TBOT_DELAY_MS_MAX ? TBOT_DELAY_MS_MAX : ms);
}
inline int tbot_delay_tics(int ms) {
  const int c = tbot_delay_ms_clamped(ms);
  return (c * TBOT_TICS_PER_SECOND + 500) / 1000;
}

inline TbotCond tbot_when_crosshair(int hitsprite, bool badguy, bool alive, int dist2d, int range) {
  TbotCond c = { false, "nohit" };
  if (hitsprite < 0) return c;
  if (!badguy || !alive) { c.reason = "notenemy"; return c; }
  if (!aim_within_range(dist2d, range)) { c.reason = "range"; return c; }
  c.holds = true; c.reason = "hit";
  return c;
}

// Aimbot mode: a published target with line of sight AND a shot ray landing on a live enemy now (the snapshot is last
// tic's, so a lock alone fired after kills). Any live or choking enemy counts, not only the lock. `rayHitsBreakable`
// (glass, a vent grate) only WIDENS "blocked" into a hold: it is the shot's first bullet.
inline TbotCond tbot_when_aimbot(int targetIndex, bool los, bool rayHitsEnemy, bool rayHitsBreakable) {
  TbotCond c = { false, "nolock" };
  if (targetIndex < 0 || !los) return c;
  if (rayHitsEnemy) { c.holds = true; c.reason = "lock"; return c; }
  if (rayHitsBreakable) { c.holds = true; c.reason = "breaking"; return c; }
  c.reason = "blocked";
  return c;
}


// checkhitwall's gate: a wall becomes passable only when hittable (cstat bit 0x10, or the force-field tile 0xe6, a
// spark that opens nothing), with a real neighbour, the hit above its floor across a real gap (hitz < nextFloorz,
// nextFloorz != nextCeilingz), and an overpicnum the switch opens: 0x1f7 glass, 0x197 fan, 0x1fe coloured glass.
inline bool tbot_wall_breakable(int wallCstat, int overpicnum, int nextsector, int hitz, int nextFloorz, int nextCeilingz) {
  const bool hittable = (wallCstat & 0x10) != 0 || overpicnum == 0xe6;
  if (!hittable) return false;
  if (nextsector < 0) return false;
  if (nextFloorz == nextCeilingz) return false;
  if (!(hitz < nextFloorz)) return false;
  return overpicnum == 0x1f7 || overpicnum == 0x197 || overpicnum == 0x1fe;
}

// checkhitsprite's membership list: picnums whose sprite a PLAYER'S HITSCAN can delete or strip of its
// blocking/hittable cstat bits (0xa1e: xrepeat zeroed). Runtime gates (damage, hp, a random roll) do not exclude; a
// gate no hitscan can satisfy does (0x38c/0x38e/0x3b7/0x3d2/0x3de/0x38f need an attacker SHOTSPARK1 isn't). Sorted.
inline bool tbot_sprite_breakable(int picnum) {
  static const int kBreakable[] = {
    0x156, 0x157, 0x197, 0x204, 0x205, 0x227, 0x22c, 0x22d, 0x237, 0x239, 0x23b, 0x253, 0x25a, 0x25f, 0x29d, 0x2a6,
    0x2a8, 0x2ad, 0x2ae, 0x2b1, 0x2b6, 0x2b7, 0x2b9, 0x2cc, 0x2f1, 0x2fd, 0x300, 0x301, 0x306, 0x30b, 0x318, 0x365,
    0x385, 0x386, 0x3ba, 0x3bb, 0x3bc, 0x3bd, 0x3cb, 0x3cc, 0x3cd, 0x3ce, 0x3cf, 0x3d3, 0x3eb, 0x3f4, 0x3f5, 0x3f6,
    0x401, 0x459, 0x485, 0x486, 0x487, 0x488, 0x489, 0x48a, 0x48b, 0x48c, 0x48d, 0x48e, 0x548, 0x549, 0x54a, 0x54b,
    0x54f, 0xa1e, 0x1003, 0x115c, 0x1171, 0x134f,
  };
  for (int i = 0; i < (int)(sizeof(kBreakable) / sizeof(kBreakable[0])); i++)
    if (kBreakable[i] == picnum) return true;
  return false;
}

// shoot's own hitscan ray for the player with no autoaim target and no spread (VA 0x004f4417): vx = sintable[(ang +
// 512) & 2047], vy = sintable[ang & 2047], vz = ((100 - horiz - horizoff) << 5) << 6. Written as multiplication:
// shifting a negative operand is undefined before C++20.
inline void tbot_ray(int ang, int horiz, int horizoff, const short* sintable, int* vx, int* vy, int* vz) {
  if (!sintable || !vx || !vy || !vz) return;
  *vx = sintable[(ang + 512) & 2047];
  *vy = sintable[ang & 2047];
  const long long zvel = ((long long)100 - (long long)horiz - (long long)horizoff) * 32;
  *vz = clamp_ll_to_int(zvel * 64);
}

// The one answer per tic. Every refusal resets the state so a condition that drops and returns restarts its delay.
// `reason` is the first refusal that applied, "delay" while waiting, "fire" when firing.
inline bool tbot_decide(bool on, bool menuOpen, bool freezeActive, bool dead, bool cameraActive,
                        bool weaponUsable, TbotCond cond, int delayTics, TbotState* st, const char** reason) {
  const char* why = nullptr;
  if (!st) { if (reason) *reason = "nostate"; return false; }
  if (!on)              why = "off";
  else if (menuOpen)    why = "menu";
  else if (freezeActive) why = "freeze";
  else if (dead)        why = "dead";
  else if (cameraActive) why = "camera";
  else if (!weaponUsable) why = "weapon";
  else if (!cond.holds) why = cond.reason ? cond.reason : "cond";
  if (why) {
    st->heldTics = 0; st->firing = false;
    if (reason) *reason = why;
    return false;
  }
  if (st->heldTics < TBOT_HELD_TICS_CAP) st->heldTics++;
  st->firing = st->heldTics > delayTics;
  if (reason) *reason = st->firing ? "fire" : "delay";
  return st->firing;
}

// The last tic's decision, for the aim ledger (aimdiag.h): the shot hook runs inside processinput, after
// hooked_getinput, so this is the decision that produced the shot. heldTics against delayTics separates EARLY from
// WILD.
struct TbotLast {
  int firing;
  int heldTics;
  int delayTics;
  int target;       // the aimbot's published lock, or -1
  int raySprite;    // what the shot ray stopped on
  const char* reason;
};
const TbotLast& tbot_last();
void tbot_note_shot(const char* verdict);

void tbot_tic(const PlayerAim& p, bool cameraActive, bool paused);

// Whether the LAST tic's decision was to fire. The shot instrument gates on the aimbot's last view write, false for a
// crosshair-mode triggerbot shot, so it takes either gate. Last tic's because the shot fires inside processinput, after
// tbot_tic. DLL-only.
bool tbot_fired_last_tic();

#pragma once
#include "project.h"  // PlayerAim: this port keeps it there, having no engine.h

// Target acquisition on the tic clock, published to the render clock via a seqlock (AimSnapshot::seq, target.h):
// aim_tic runs in the processinput hook, the marker draws from the swap hook later in the same iteration. aim_tic only
// publishes; aim_apply is the sole view writer.

struct AimSnapshot {
  int  targetIndex;            // sprite index of the selected enemy, or -1 for none
  int  targetPicnum;
  int  targetRung;
  int  aimx, aimy, aimz;       // the solved aim point AFTER projectile lead
  int  rawx, rawy, rawz;       // aim point BEFORE lead; the marker draws both
  int  solvedAng, solvedHoriz; // the view angle and pitch that hit (aimx, aimy, aimz)
  int  leadScale;              // g_cfg.aimleadscale as clamped for this tic
  int  choice;                 // AimSolutionChoice: which solve was published
  bool los;
  unsigned seq;                // seqlock counter: even = stable, odd = being written
};

// One tic's acquisition: walks STAT_ACTOR then STAT_ZOMBIEACTOR, filters, picks, leads, solves, publishes; empty
// (targetIndex -1) when aim_should_run refuses or nothing survives. Call after the original in hooked_processinput,
// only when the view freeze is inactive.
void aim_tic(const PlayerAim& p);

void aim_clear();

void aim_draw_marker(int screenW, int screenH);

bool aim_snapshot(AimSnapshot* out);

bool aim_smoothed_velocity(int index, int picnum, int* vx, int* vy, int* vz);

// The sample the aimbot solves from: live position minus hittype[i]'s previous-tic bpos. Picnum-guarded against a
// recycled slot; a pure read. Exists because the smoothed value was measurably wrong (aimwt.cpp).
bool aim_live_velocity(int index, int picnum, int* vx, int* vy, int* vz);

bool aim_hold_active();

// Whether the aimbot's view write landed on the LAST tic, for hooked_getinput's hold-view clear: the write is at the
// tail of processinput and the mouse turn at the top of the next call. False on a refused tic, so a lost target frees
// the mouse.
bool aim_view_held();

void aim_view_hold_note(bool held);

// One tic's write: the snapshot becomes the view angle and pitch, eased by aimsmooth, clamped to AIM_HORIZ_MIN/MAX.
// Uses the NON-collapsing engine_write_player_aim (oang/ohoriz stay, so frames ease between tics). Has its own
// aim_should_run gate.
bool aim_apply(const PlayerAim& p, short* pAng, int* pHoriz);

// Shot-time measurement at the instant `shoot` runs: how far the facing is from the yaw that hits the aim point. Called
// from silent.cpp's hooked_shoot when silent aim did not substitute. `aa=` is autoAim: aim() can redirect onto a target
// tbot_ray never crossed.
void aim_shot_diag(const PlayerAim& p, int atwith, bool redirect);

void aim_shot_budget_reset();

void aim_last_shot(unsigned* no, unsigned* ms, int* target);

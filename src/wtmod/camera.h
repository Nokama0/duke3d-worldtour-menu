// The World Tour side of the free camera's DLL half; freecam.h declares the shared entry points.
#ifndef WTMOD_CAMERA_H
#define WTMOD_CAMERA_H

#include "freecam.h"
#include "project.h"

// For the WINDOW thread (the copy chord): a seqlock read. Answers 0 when the camera is not flying or the copy was torn,
// which also tells the chord the key was not its to swallow.
int camera_pose_read(FreecamPose* out);

// The LOCAL PLAYER's pose whatever the free camera does. False, `out` zeroed, when the player array did not resolve or
// the sector is one the engine treats as "not in play".
bool camera_local_player_pos(PlayerPos* out);

// The held-keycard bits (1 blue, 2 red, 4 yellow), or -1 with no player.
int camera_local_got_access();

// gm == 4 (MODE_GAME alone): the tally and menus leave the world behind them stale.
bool camera_local_in_play();

bool camera_local_in_play();

// Fills PlayerAim, runs acquisition, applies the turn. Called from the processinput tail.
void aim_run_tic();

// For callers on a different hook. False when the player or its sector is not one this DLL may read.
bool camera_build_player_aim(PlayerAim* out);

// The player's view pair. Silent aim substitutes it around one shoot call and restores it at once.
bool camera_player_read_view(short* ang, int* horiz);
bool camera_player_write_view(short ang, int horiz);

// The pair, its two interpolation partners and the engine's autoaim hold-off.
struct ShotView {
  short ang, oang;
  int horiz, ohoriz, aimmode;
};
bool camera_player_read_shot_view(ShotView* out);
bool camera_player_substitute_shot_view(short ang, int horiz);
bool camera_player_restore_shot_view(const ShotView& v);

// Until the hook has taken the toggle key stays Duke's: a swallowed key for a camera that cannot fly is lost.
int camera_installed();

// Serves "Remove fog" AND fullbright, which share the visibility field.
void camera_light_tic();

// The camera spends the counts so the view does not turn under it.
void camera_feed_mouse(int dx, int dy);

// The engine's OWN menu lockout stores, so the player stands still while the camera flies.
void camera_lock_input();

// Drawn after the ESP and before the panel; opens its own canvas frame, nothing when the camera is down.
void camera_draw_player_marker(int screenW, int screenH);

#endif

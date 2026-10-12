// Remove fog, for the free camera. The True3D world pass (VS 9489 / PS 9491) fills PlayerVisBuffer's g_playerVis (512,
// Duke's const_visibility) from the PLAYER'S OWN visibility, a SHORT at player +0x3DC (Megaton: +0x268). The classic
// global at rva 0xE07138 feeds FUN_007659a0, a path this game does not run (RenderDoc).
#include <windows.h>
#include "light.h"
#include "config.h"
#include "hook.h"         // wt_log

void fog_hold_release();

// Fullbright rides the same field: the world pass darkens only via o0.w = 1 - saturate(distance * visTerm), which with
// g_sectorVis 0 is distance * g_playerVis, so 0 removes it. It needs the palette shade row too (lightwt.cpp); point
// lights still apply. Details: docs/re/fullbright-shade-dead-end.md.
static int hold_wanted(bool cameraActive) {
  if (light_wants(g_cfg.fullbright != 0, cameraActive, g_cfg.freecamfullbright != 0))
    return 0;
  if (cameraActive && g_cfg.freecamfog != 0)
    return fog_visibility_clamped(g_cfg.fogvisibility);
  return -1;
}

// One writer: a render bracket saving/restoring the field per draw fought the whole-flight hold and the lighting
// flickered at frame rate. Called on camera landing and DLL unload: never leave the player holding a visibility they
// cannot account for.
void fog_drop() { fog_hold_release(); }

static short  s_holdSaved = 0, s_holdSavedOld = 0;
static short* s_holdField = 0;
static bool   s_holding = false;
static bool   s_loggedHold = false;

void fog_hold_tic(bool cameraActive, short* pPlayerVis) {
  const int want = hold_wanted(cameraActive);
  if (want < 0 || !pPlayerVis) { fog_hold_release(); return; }
  if (!s_holding) {
    s_holdField = pPlayerVis;
    s_holdSaved = pPlayerVis[0];
    s_holdSavedOld = pPlayerVis[1];
    s_holding = true;
    if (!s_loggedHold) {
      s_loggedHold = true;
      wt_log("fog: holding player visibility at %d for the whole flight (was %d, prev %d)",
             want, (int)s_holdSaved, (int)s_holdSavedOld);
    }
  }
  pPlayerVis[0] = (short)want;
  pPlayerVis[1] = (short)want;
}


void fog_hold_release() {
  if (!s_holding || !s_holdField) return;
  s_holdField[0] = s_holdSaved;
  s_holdField[1] = s_holdSavedOld;
  s_holding = false;
}

bool fog_held() { return s_holding; }

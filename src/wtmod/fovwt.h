// The field of view on top of Megaton's arithmetic (fov.h/fov.cpp). World Tour has no FOV control but derives its
// projection from Build's `viewingrange`: FUN_007b9870 (rva 0x109870) reads DAT_0154f040 (rva 0xE9F040, 16.16) at
// 0x007b987c: fovDeg = clamp(viewingrange*200/131072, 45, 175); fovY = fovDeg / (viewportW/viewportH).
#ifndef WTMOD_FOVWT_H
#define WTMOD_FOVWT_H

#include "fov.h"

const long FOV_VR_ENGINE_MIN = 29492;
const long FOV_VR_ENGINE_MAX = 114688;

inline double fov_engine_degrees(long viewingrange) {
  return (double)viewingrange * 200.0 / 131072.0;
}

// Engine value (aspect-corrected) scaled by the slider, held in the window. The input is capped first: the scale
// returns a 32-bit long and an absurd value times FOV_MAX's multiplier could truncate negative. A non-positive value is
// returned as is.
inline long fov_viewingrange_for(long engineViewingrange, int degrees) {
  if (engineViewingrange <= 0) return engineViewingrange;
  long v = engineViewingrange;
  if (v > FOV_VR_ENGINE_MAX) v = FOV_VR_ENGINE_MAX;
  v = fov_scale_viewingrange(v, fov_to_viewingrange(degrees));
  if (v < FOV_VR_ENGINE_MIN) v = FOV_VR_ENGINE_MIN;
  if (v > FOV_VR_ENGINE_MAX) v = FOV_VR_ENGINE_MAX;
  return v;
}

// setaspect (FUN_007644e0, rva 0xB44E0) is the sole writer of that global, called every frame at 0x006faac9 with
// 65536*(xdim/ydim)/1.425 before displayrooms (the 2D pass's call at 0x006fab16 restores it). Installs nothing unless
// setaspect, the site and the global all check.
void fov_install();

int fov_installed();

#endif

#pragma once
// The free camera's viewpoint as text, for the clipboard. Pure; clipboard.cpp is the syscall, input.cpp the chords. The
// format is Megaton's ("cam x=%d y=%d z=%d ang=%d pitch=%d"), so a viewpoint copied in one game pastes into the other;
// its extra cap=/cutaway= fields are ignored.
#include "freecam.h"   // FreecamPose, freecam_parse_readout (verbatim from Megaton)
#include <stdio.h>     // _snprintf_s

// A line that parses is not yet a pose. Build maps span about +-65536 in x/y and 16x that in z; these bounds are 8x the
// span, so no real pose is refused and a bad paste stays flyable.
const int CAMPOS_MAX_XY = 524288;
const int CAMPOS_MAX_Z  = CAMPOS_MAX_XY * 16;   // Build's z axis is 16:1 against x and y

inline bool campos_pose_plausible(const FreecamPose& p) {
  if (p.x < -CAMPOS_MAX_XY || p.x > CAMPOS_MAX_XY) return false;
  if (p.y < -CAMPOS_MAX_XY || p.y > CAMPOS_MAX_XY) return false;
  if (p.z < -CAMPOS_MAX_Z  || p.z > CAMPOS_MAX_Z)  return false;
  if (p.ang < 0 || p.ang >= FREECAM_ANG_UNITS) return false;
  if (p.pitch < -FREECAM_PITCH_MAX || p.pitch > FREECAM_PITCH_MAX) return false;
  return true;
}

inline bool campos_format(const FreecamPose& p, char* out, int cap) {
  if (!out || cap <= 0) return false;
  out[0] = 0;
  const int n = _snprintf_s(out, (size_t)cap, _TRUNCATE,
                            "cam x=%d y=%d z=%d ang=%d pitch=%d",
                            p.x, p.y, p.z, p.ang, p.pitch);
  if (n < 0) { out[0] = 0; return false; }
  return true;
}

inline bool campos_accept(const char* text, FreecamPose* out) {
  if (!text || !out) return false;
  FreecamPose p;
  if (!freecam_parse_readout(text, &p)) return false;
  if (!campos_pose_plausible(p)) return false;
  *out = p;
  return true;
}

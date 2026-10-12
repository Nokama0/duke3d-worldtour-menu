// Verbatim from Megaton's fov.h, except FOV_MAX.
#pragma once

const int FOV_MIN = 70;
// World Tour clamps its projection at 175 degrees and 3440x1440 already spends 167.6, so the slider saturates at 93
// (about 110 at 16:9, 124 at 4:3).
const int FOV_MAX = 93;
const int FOV_DEFAULT = 90;

// Horizontal FOV in degrees -> viewingrange; 90 degrees == 65536.
int fov_to_viewingrange(int degrees);

// Scales the engine's viewingrange instead of replacing it: it already encodes the aspect correction (117418 on an
// ultrawide).
long fov_scale_viewingrange(long viewingrange, int multiplier);

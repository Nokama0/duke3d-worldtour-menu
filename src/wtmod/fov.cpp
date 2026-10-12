// From Megaton's fov.cpp; this engine's additions are in fovwt.
#include "fov.h"
#include "common.h"
#include <math.h>

int fov_to_viewingrange(int degrees) {
  const int d = clamp_int(degrees, FOV_MIN, FOV_MAX);
  // Proportional to tan(half angle); 90 degrees is the neutral 65536.
  const double half = (double)d * 3.14159265358979323846 / 360.0;
  return (int)(65536.0 * tan(half) + 0.5);
}

long fov_scale_viewingrange(long viewingrange, int multiplier) {
  // 64-bit intermediate is mandatory: both operands reach ~180000 and the product overflows int32 before the >> 16.
  return (long)(((long long)viewingrange * (long long)multiplier) >> 16);
}

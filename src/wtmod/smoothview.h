// Arithmetic of the fractional view angle, free of Windows and engine types (engine reads and hooks: smoothview.cpp).
// The renderer truncates its interpolated angle to a short; this recovers the dropped fraction and adds it to the DRAWN
// camera only, never to the tic's integers.
#ifndef WTMOD_SMOOTHVIEW_H
#define WTMOD_SMOOTHVIEW_H

#include <math.h>

const float kSmoothDegPerUnit = 0.17578125f;

// Largest fraction handed to the renderer, in build units. Fault detectors, not shaping: above every legitimate value,
// so a bigger one means a wrong assumption. Yaw: under 3. Pitch: ~5 (one horizon unit is ~2.5 build units near centre);
// a 2.0 ceiling clipped an ordinary look-up.
const double kSmoothFracMaxYaw   = 4.0;
const double kSmoothFracMaxPitch = 6.0;

const double kSmoothUnitsPerRadian = 2048.0 / 6.283185307179586;

inline int smooth_mulscale16(int a, int b) {
  return (int)(((long long)a * (long long)b) >> 16);
}

inline int smooth_ang_delta(int ang, int oang) {
  return (int)(((unsigned)(ang - oang - 1024)) & 2047u) - 1024;
}

// What the engine's integer interpolation dropped: exact product minus the shifted one, in [0, 1) for a positive delta
// and (-1, 0] for a negative one.
inline double smooth_interp_frac(int delta, int ratio) {
  return (double)delta * (double)ratio / 65536.0 - (double)smooth_mulscale16(delta, ratio);
}

// The engine's interpolated horizon, with its final 16-bit add. ohoriz is +0x224 and ohorizoff +0x228; the tic backup
// at 0x0072a9e2 writes horiz and horizoff into them.
inline int smooth_horiz_int(int horiz, int horizoff, int ohoriz, int ohorizoff, int ratio) {
  const int d = (horiz + horizoff) - (ohoriz + ohorizoff);
  return (short)(smooth_mulscale16(d, ratio) + ohorizoff + ohoriz);
}

inline double smooth_horiz_clamp(double h) {
  if (h > 299.0) return 299.0;
  if (h < -99.0) return -99.0;
  return h;
}

inline double smooth_horiz_to_units(double horiz) {
  return atan2(horiz - 100.0, 128.0) * kSmoothUnitsPerRadian;
}

inline double smooth_clamp_frac(double v, double maxAbs) {
  if (!(v == v)) return 0.0;
  if (v >  maxAbs) return  maxAbs;
  if (v < -maxAbs) return -maxAbs;
  return v;
}

// The panel's switch. The three code patches stay in place; off, the displayrooms hook leaves both fractions at zero.
// Written on the render thread, read on the drawing thread.
void smoothview_set_enabled(int on);

int smoothview_installed();

// Checks the two code sites and engine addresses, installs the three hooks, and installs nothing unless every one
// takes.
void smoothview_install();

// The local player's index (the snum displayrooms gets), or -1 before the first drawn frame; answers only while the
// displayrooms hook is installed.
int smoothview_local_player();

#endif

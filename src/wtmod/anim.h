// Copied from Megaton's src/duke3dmod/anim.h.
#pragma once
#include <math.h>

// Pure animation layer for the Simple and Glass styles; menu.cpp is the only caller. Wall clock, never the tic clock
// (the panel is mostly open while the game is paused). Progress/dt are unsigned ms differences, wrap-safe.

enum { ANIM_OFF = 0, ANIM_SIMPLE = 1, ANIM_GLASS = 2 };

const unsigned ANIM_OPEN_MS      = 150;
const unsigned ANIM_TAB_MS       = 120;
const unsigned ANIM_CTRL_MS      = 120;
const unsigned ANIM_HOVER_IN_MS  = 80;
const unsigned ANIM_HOVER_OUT_MS = 160;

const double ANIM_DT_MAX_MS = 50.0;

const unsigned ANIM_STALE_MS = 1000;

struct AnimSpringParams { double stiffness, damping; };

// Critical damping at 400 stiffness is 2*sqrt(400) = 40: settles without crossing. Under-damped glass: 2*sqrt(300) ~=
// 34.6 > 22.
const AnimSpringParams ANIM_SPRING_CRITICAL = { 400.0, 40.0 };   // settles, never crosses
const AnimSpringParams ANIM_SPRING_GLASS    = { 300.0, 22.0 };   // one visible overshoot

struct AnimSpring { double x, v; };

struct AnimTween { double from, to; unsigned t0, dur; };

inline double anim_ease_out(double p) {
  if (p <= 0.0) return 0.0;
  if (p >= 1.0) return 1.0;
  const double q = 1.0 - p;
  return 1.0 - q * q * q;
}

inline double anim_progress(unsigned t0, unsigned now, unsigned durMs) {
  if (durMs == 0) return 1.0;
  const unsigned age = now - t0;   // wrap-safe: exact for any age under 2^32 ms
  return age >= durMs ? 1.0 : (double)age / (double)durMs;
}

inline double anim_tween_value(const AnimTween& t, unsigned now) {
  return t.from + (t.to - t.from) * anim_ease_out(anim_progress(t.t0, now, t.dur));
}

inline double anim_dt_clamped(unsigned last, unsigned now) {
  const unsigned d = now - last;
  if (d > 0x7FFFFFFFu) return 0.0;   // `now` is "before" `last`: clock went backwards, no step
  return d > (unsigned)ANIM_DT_MAX_MS ? ANIM_DT_MAX_MS : (double)d;
}

// Semi-implicit Euler is only conditionally stable on omega*dt: CRITICAL's omega is 20/s, so at the 50 ms clamp
// omega*dt is 1.0 and the pair oscillates forever. 4 ms keeps it at 0.08.
const double ANIM_SPRING_SUBSTEP_MS = 4.0;

// Velocity first keeps the critical pair from crossing its target at 16 ms. `dtMs` is covered in ceil(dtMs/SUBSTEP)
// equal substeps, so the result depends on elapsed time, not call frequency.
inline void anim_spring_step(AnimSpring* s, double target, double stiffness, double damping, double dtMs) {
  if (!s) return;
  if (!(dtMs > 0.0)) return;
  int n = (int)ceil(dtMs / ANIM_SPRING_SUBSTEP_MS);
  if (n < 1) n = 1;
  const double dt = dtMs / (double)n / 1000.0;
  for (int i = 0; i < n; i++) {
    const double a = stiffness * (target - s->x) - damping * s->v;
    s->v += a * dt;
    s->x += s->v * dt;
  }
}

inline bool anim_spring_settled(const AnimSpring& s, double target, double eps) {
  return fabs(s.x - target) < eps && fabs(s.v) < eps * 10.0;
}

inline int anim_style_clamped(int v) { return v < ANIM_OFF ? ANIM_OFF : v > ANIM_GLASS ? ANIM_GLASS : v; }

inline int anim_switch_apply(int stored, int on) {
  if (!on) return ANIM_OFF;
  return stored != ANIM_OFF ? stored : ANIM_SIMPLE;
}

// Blends one channel at a time toward `target`, the one place hover tinting is computed. A target, not C_BONE: a hotkey
// field lightened toward one bone and the text vanished. Alpha kept; `!(frac > 0.0)` also catches NaN.
inline unsigned anim_blend_colour(unsigned c, unsigned target, double frac) {
  if (!(frac > 0.0)) return c;
  if (frac >= 1.0) return (c & 0xFF000000u) | (target & 0x00FFFFFFu);
  const double br = (double)((c >> 16) & 0xFFu);
  const double bg = (double)((c >>  8) & 0xFFu);
  const double bb = (double)( c        & 0xFFu);
  const double tr = (double)((target >> 16) & 0xFFu);
  const double tg = (double)((target >>  8) & 0xFFu);
  const double tb = (double)( target        & 0xFFu);
  const unsigned r = (unsigned)(br + (tr - br) * frac);
  const unsigned g = (unsigned)(bg + (tg - bg) * frac);
  const unsigned b = (unsigned)(bb + (tb - bb) * frac);
  return (c & 0xFF000000u) | (r << 16) | (g << 8) | b;
}

// Glass geometry rules, header-only (canvas.cpp's GL bodies cannot link into test_host); one corner tessellation for
// both shapes.

struct AnimPill { double x, w; };
inline AnimPill anim_pill_stretch(double x, double w, double v,
                                  double stretchPerVelocity, double maxStretch) {
  double e = fabs(v) * stretchPerVelocity;
  if (e > maxStretch) e = maxStretch;
  const double s = 1.0 + e;
  AnimPill p;
  p.w = w * s;
  p.x = x - (w * s - w) / 2.0;
  return p;
}

inline double anim_wobble(double t01, double amp) {
  if (t01 <= 0.0 || t01 >= 1.0) return 0.0;
  const double pi = 3.14159265358979323846;
  return amp * sin(t01 * 6.0 * pi) * (1.0 - t01);
}

inline int anim_round_rect_segments(int r) {
  const int s = 3 + (r > 0 ? r / 2 : 0);
  return s < 3 ? 3 : s > 12 ? 12 : s;
}

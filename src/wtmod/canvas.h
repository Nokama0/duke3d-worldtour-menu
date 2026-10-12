// Copied from Megaton's src/duke3dmod/canvas.h; keep identical, do not edit here.
#pragma once
const unsigned C_PANEL   = 0xF014171A;
const unsigned C_RECESS  = 0xF01C2127;
const unsigned C_RAISED  = 0xFF333C46;
const unsigned C_BEVELHI = 0xFF48535F;
const unsigned C_BEVELLO = 0xFF0A0C0E;
const unsigned C_HAZARD  = 0xFFF2B01E;
const unsigned C_RED     = 0xFFD8402F;
const unsigned C_BLUE    = 0xFF3C9BD6;
const unsigned C_GREEN   = 0xFF6FBF5B;
const unsigned C_BONE    = 0xFFE8E4DA;
const unsigned C_BONEDIM = 0xFF9AA1A8;
const unsigned C_BONEFNT = 0xFF666E77;
const unsigned C_WELL      = 0xFF0E1114;  // darkest sunken steel: switch/slider wells, tab rail
const unsigned C_HAZARDDIM = 0xFF8A6512;  // dimmed hazard, the slider's filled track
const unsigned C_AIM       = 0xFFC061E8;  // aim calibration marker: violet
const unsigned C_PLAYER    = 0xFF2FE0D0;  // free camera's own-body marker: cyan
const unsigned C_SETTINGS  = 0xFFB8A9E0;  // Settings tab rail accent

// Switch-face tints: a translucent wash, pushed toward full saturation to survive a dark wall.
const unsigned C_TINT_FLIP  = 0x6060FF60;
const unsigned C_TINT_LEAVE = 0x60FF5050;

// C_HAZARD's hue at alpha 0x40: 2-3 room panels can overlap along a sightline and wash out the texture.
const unsigned C_SECRET_VOLUME = 0x40F2B01E;

// Teleport landing marker: C_PLAYER's cyan at C_SECRET_VOLUME's alpha, so it does not read as another secret room.
const unsigned C_TELEPORT_MARK = 0x402FE0D0;

const unsigned C_GLASS_TINT = 0x99141C26;  // slab ground over the blurred backdrop; alpha = scene showing through
const unsigned C_GLASS_RIM  = 0xB0A8C8E0;  // one-pixel rim; brighter than C_BEVELHI (no bevel in Glass)
const unsigned C_GLASS_TOP  = 0x40D8E8F8;  // top-edge sheen, so the slab does not read as a flat hole
const unsigned C_GLASS_GLOW = 0x66A8D8FF;  // additive bloom (open sweep, switch flare, hover glow)

// A non-positive period would never advance canvas_hazard's loop.
inline bool canvas_hazard_params_ok(int w, int h, int period) {
  return w > 0 && h > 0 && period > 0;
}

// Red enemy, blue pickup; keycards keep their colour in the NAME.
inline unsigned canvas_esp_colour(bool enemy) { return enemy ? C_RED : C_BLUE; }

inline unsigned canvas_esp_secret_colour() { return C_HAZARD; }

inline unsigned canvas_notice_colour() { return C_HAZARD; }

inline unsigned canvas_aim_colour() { return C_AIM; }

inline unsigned canvas_player_colour() { return C_PLAYER; }

inline unsigned canvas_esp_explodable_colour() { return C_HAZARD; }

inline unsigned canvas_esp_switch_colour() { return C_BLUE; }

inline unsigned canvas_health_colour(int hp, int maxhp) {
  if (maxhp <= 0 || hp <= 0) return C_RED;
  const long long pct = (long long)hp * 100 / maxhp;
  if (pct > 50) return C_GREEN;
  if (pct > 25) return C_HAZARD;
  return C_RED;
}

inline int canvas_bar_fill(int total, int hp, int maxhp) {
  if (total <= 0 || maxhp <= 0 || hp <= 0) return 0;
  if (hp >= maxhp) return total;
  return (int)((long long)total * hp / maxhp);
}

// Alpha of blur tap `i`; the alphas sum to exactly aTint. Cumulative rounding, since a flat aTint/taps plus remainder
// goes negative on the last tap and wraps to a wrong colour byte.
inline int canvas_tap_alpha(int aTint, int taps, int i) {
  if (taps <= 0 || i < 0 || i >= taps) return 0;
  if (aTint < 0) aTint = 0;
  if (aTint > 255) aTint = 255;
  const int upto = (int)((double)aTint * (double)(i + 1) / (double)taps + 0.5);
  const int prev = (int)((double)aTint * (double)i / (double)taps + 0.5);
  return upto - prev;
}

// A colour with its alpha replaced, clamped; the colour channels survive bit for bit.
inline unsigned canvas_with_alpha(unsigned rgba, int alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 255) alpha = 255;
  return (rgba & 0x00FFFFFFu) | ((unsigned)alpha << 24);
}

// The panel's open motion moves every pixel drawn while the LOGICAL layout used for hit testing stays put.
// d = ((p - c) * scale + c) + offset. Identity is the default; canvas_begin resets it.
struct CanvasTransform { int dx, dy, cx, cy; double scale; };
void canvas_set_transform(int dx, int dy, double scale, int cx, int cy);

#ifdef NO_GL
// Test-build-only recording of what was drawn.
const int CANVAS_RECORD_MAX = 512;
enum { CANVAS_OP_RECT = 0, CANVAS_OP_BEVEL, CANVAS_OP_HAZARD,
       CANVAS_OP_LINE, CANVAS_OP_TEXT,
       CANVAS_OP_ROUNDRECT, CANVAS_OP_GRADIENT, CANVAS_OP_GLOW,
       CANVAS_OP_CAPTURE, CANVAS_OP_BACKDROP,
       CANVAS_OP_ROUNDOUTLINE };
struct CanvasOp {
  int kind;         // CANVAS_OP_*
  int x, y, w, h;   // for TEXT, w is the rendered width and h is 0
  unsigned colour;  // for BEVEL, the FILL colour
  int font;         // TEXT only, else -1
  char text[64];    // TEXT only, truncated
};
void canvas_record_begin();      // start capturing; clears whatever was there
void canvas_record_end();        // stop, so the pixel sweeps cost one bool test
int  canvas_record_count();
bool canvas_record_overflowed(); // true if ops were dropped: never ignore it
const CanvasOp* canvas_record_op(int i);
const CanvasOp* canvas_record_find_text(const char* s);
const CanvasOp* canvas_record_find_kind(int kind, unsigned colour);
CanvasTransform canvas_record_transform();
#endif // NO_GL

// Coordinates are LOGICAL; the canvas multiplies by this factor on the way out. menu_draw restores UI_SCALE_DEFAULT on
// exit (the ESP draws through the same primitives).
void canvas_set_scale(int pct);
int  canvas_scale();

void canvas_begin(int w, int h);
void canvas_end();
void canvas_rect(int x, int y, int w, int h, unsigned rgba);
void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned hi, unsigned lo);
void canvas_line(int x0, int y0, int x1, int y1, unsigned rgba);
void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned ground, int period);
int canvas_text(int x, int y, const char* s, unsigned rgba, int font);
int canvas_text_width(const char* s, int font);   // stays LOGICAL: a scaled measurement would apply the factor twice

void canvas_round_rect(int x, int y, int w, int h, int r, unsigned rgba);
// A ring `thickness` logical pixels wide just inside the rect; its own primitive because a filled round rect under a
// translucent slab washed the face light blue.
void canvas_round_rect_outline(int x, int y, int w, int h, int r, int thickness,
                               unsigned rgba);
void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned bottom);
void canvas_glow_rect(int x, int y, int w, int h, unsigned rgba);
bool canvas_backdrop_capture(int x, int y, int w, int h);
void canvas_backdrop_draw(int x, int y, int w, int h, int r, unsigned tint,
                          double wobbleAmp, double wobblePhase, int blurTaps, int blurRadius);
void canvas_backdrop_release();

// The atlas texel a flat fill samples, for a caller queueing its OWN triangles through the batcher.
bool canvas_white_texel(float* u, float* v);

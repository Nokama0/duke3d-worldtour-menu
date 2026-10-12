#include "canvas.h"
#include "font.h"
#include "ui.h"    // UI_SCALE_DEFAULT, ui_scale_px: layout is logical pixels and this file spends the factor
#include "anim.h"  // anim_round_rect_segments, anim_wobble: the Glass geometry rules, spent here

// Megaton's canvas.cpp with its backend replaced: each function reproduces what the OpenGL one draws (same device
// arithmetic, tessellation and glyph quads) through overlay.cpp's vertex batcher. Read src/duke3dmod/canvas.cpp beside
// it.

// Outside the NO_GL guard: both builds scale, else a recorder logging logical coordinates would pin a panel nobody
// sees.
static int s_scale = UI_SCALE_DEFAULT;

void canvas_set_scale(int pct) { s_scale = ui_scale_clamped(pct); }
int  canvas_scale()            { return s_scale; }

static int    s_txDx = 0, s_txDy = 0;    // logical displacement
static int    s_txCx = 0, s_txCy = 0;    // logical centre the scale is about
static double s_txScale = 1.0;
static bool   s_txIdentity = true;

void canvas_set_transform(int dx, int dy, double scale, int cx, int cy) {
  s_txDx = dx; s_txDy = dy; s_txCx = cx; s_txCy = cy;
  s_txScale = scale;
  s_txIdentity = (dx == 0 && dy == 0 && scale == 1.0);
}

// Half away from zero, saturating: an out-of-range or NaN (int) cast is UB and this runs on the render thread.
static int tx_round(double v) {
  return clamp_double_to_int(v < 0.0 ? v - 0.5 : v + 0.5);
}

static int tx_dev_x(int d) {
  if (s_txIdentity) return d;
  const double c = (double)ui_scale_px(s_txCx, s_scale);
  return tx_round(((double)d - c) * s_txScale + c + (double)ui_scale_px(s_txDx, s_scale));
}
static int tx_dev_y(int d) {
  if (s_txIdentity) return d;
  const double c = (double)ui_scale_px(s_txCy, s_scale);
  return tx_round(((double)d - c) * s_txScale + c + (double)ui_scale_px(s_txDy, s_scale));
}
static int tx_dev_len(int d) {
  if (s_txIdentity) return d;
  return tx_round((double)d * s_txScale);
}

static int dev_x(int x)   { return tx_dev_x(ui_scale_px(x, s_scale)); }
static int dev_y(int y)   { return tx_dev_y(ui_scale_px(y, s_scale)); }
static int dev_len(int v) { return tx_dev_len(ui_scale_px(v, s_scale)); }

#ifndef NO_GL
#include <windows.h>
#include <d3d11.h>
#include <stdlib.h>
#include "overlay_internal.h"
#include "fontgen.h"

void wt_log(const char* fmt, ...);

static int s_devW = 0, s_devH = 0;

// The atlas: font_data.cpp's 8-bit coverage as A8_UNORM, one shader for this and the readout's atlas. Two opaque rows
// are appended: a flat fill samples the last, so rectangles share a draw call with text; the second gives filtering a
// neighbour.
#define CANVAS_WHITE_ROWS 2

static const float s_whiteU = 0.5f;

// One upload for the baked and runtime atlas. Reports the v of the flat-fill texel: the centre of the last opaque row.
static bool upload_atlas(ID3D11Device* dev, const unsigned char* src, int w, int h,
                         ID3D11ShaderResourceView** outSrv, float* outWhiteV) {
  const int th = h + CANVAS_WHITE_ROWS;
  unsigned char* pixels = (unsigned char*)malloc((size_t)w * (size_t)th);
  if (!pixels) {
    wt_log("canvas: out of memory for a %dx%d atlas", w, th);
    return false;
  }
  memcpy(pixels, src, (size_t)w * (size_t)h);
  memset(pixels + (size_t)w * (size_t)h, 0xFF, (size_t)w * (size_t)CANVAS_WHITE_ROWS);

  D3D11_TEXTURE2D_DESC td;
  ZeroMemory(&td, sizeof(td));
  td.Width            = (UINT)w;
  td.Height           = (UINT)th;
  td.MipLevels        = 1;
  td.ArraySize        = 1;
  td.Format           = DXGI_FORMAT_A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage            = D3D11_USAGE_IMMUTABLE;
  td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA init;
  ZeroMemory(&init, sizeof(init));
  init.pSysMem     = pixels;
  init.SysMemPitch = (UINT)w;                 // one byte a texel
  ID3D11Texture2D* tex = 0;
  HRESULT hr = dev->CreateTexture2D(&td, &init, &tex);
  free(pixels);
  if (FAILED(hr) || !tex) {
    wt_log("canvas: CreateTexture2D for a %dx%d atlas failed (hr=0x%08lx)",
           w, th, (unsigned long)hr);
    return false;
  }
  ID3D11ShaderResourceView* srv = 0;
  hr = dev->CreateShaderResourceView(tex, 0, &srv);
  tex->Release();
  if (FAILED(hr) || !srv) {
    wt_log("canvas: CreateShaderResourceView for a %dx%d atlas failed (hr=0x%08lx)",
           w, th, (unsigned long)hr);
    return false;
  }
  *outSrv    = srv;
  *outWhiteV = ((float)h + (float)CANVAS_WHITE_ROWS - 0.5f) / (float)th;
  return true;
}

static ID3D11ShaderResourceView* s_atlasSrv = 0;
static ID3D11Device* s_atlasDev = 0;     // the device it belongs to
static int   s_atlasFailed = 0;
static float s_atlasWhiteV = 0.0f;

static bool ensure_atlas() {
  ID3D11Device* dev = ov_device();
  if (!dev) return false;
  // A second device would own none of this; if one is ever created the panel stops rather than use the wrong texture.
  if (s_atlasSrv && dev == s_atlasDev) return true;
  if (s_atlasFailed) return false;
  if (s_atlasSrv) { s_atlasSrv->Release(); s_atlasSrv = 0; }
  if (!upload_atlas(dev, font_atlas, FONT_ATLAS_W, FONT_ATLAS_H, &s_atlasSrv, &s_atlasWhiteV)) {
    s_atlasSrv = 0;
    s_atlasFailed = 1;
    wt_log("canvas: the baked atlas would not upload; the panel will not draw");
    return false;
  }
  s_atlasDev = dev;
  wt_log("canvas: atlas %dx%d as A8_UNORM, %d fonts",
         FONT_ATLAS_W, FONT_ATLAS_H + CANVAS_WHITE_ROWS, FONT_COUNT);
  return true;
}

// The runtime atlas: lossless text at the applied scale (a scaled baked bitmap blurs). At any scale but 100 the faces
// are rasterized at that scale (fontgen.cpp, GDI) into a second texture; 100% stays baked. A failed scale is
// remembered.
static FontAtlas s_run = { 0 };                 // the runtime atlas itself, CPU side
static int s_runFailedPct = 0;                  // a scale fontgen already refused; 0 = none
static ID3D11ShaderResourceView* s_runSrv = 0;
static ID3D11Device* s_runSrvDev = 0;           // the device that texture belongs to
static int   s_runSrvPct = 0;                   // the atlas pct that texture holds
static float s_runWhiteV = 0.0f;

static bool runtime_ready() {
  return s_scale != UI_SCALE_DEFAULT && s_run.pct == s_scale && s_run.pixels != 0;
}

static void runtime_prepare() {
  if (s_scale == UI_SCALE_DEFAULT) return;   // 100 is the baked atlas
  if (s_scale == s_runFailedPct)   return;   // already tried, already refused
  if (runtime_ready())             return;   // already holding exactly this one
  fontgen_free(&s_run);                      // safe on a zeroed struct
  if (!fontgen_build(s_scale, &s_run)) s_runFailedPct = s_scale;
}

// The runtime atlas's texture. The RELEASE is why this has one caller: overlay.cpp's batches hold raw SRV pointers
// until overlay_end, so releasing after geometry is queued leaves a dangling batch; atlas_resolve calls it before the
// first primitive.
static bool ensure_runtime_texture() {
  ID3D11Device* dev = ov_device();
  if (!dev || !s_run.pixels) return false;
  if (s_runSrv && dev == s_runSrvDev && s_runSrvPct == s_run.pct) return true;
  if (s_runSrv) { s_runSrv->Release(); s_runSrv = 0; }
  if (!upload_atlas(dev, s_run.pixels, s_run.w, s_run.h, &s_runSrv, &s_runWhiteV)) {
    s_runSrv = 0;
    return false;
  }
  s_runSrvDev = dev;
  s_runSrvPct = s_run.pct;
  wt_log("canvas: runtime atlas %d%% %dx%d uploaded as A8_UNORM",
         s_run.pct, s_run.w, s_run.h + CANVAS_WHITE_ROWS);
  return true;
}

// Which atlas the panel draws from THIS FRAME, resolved on the first primitive and held: a fill and a glyph on
// different textures cannot share a batch. Lazy, not in canvas_begin, because menu_draw sets the UI scale after that.
static int    s_activeResolved = 0;
static ID3D11ShaderResourceView* s_activeSrv = 0;
static const Glyph (*s_activeGlyphs)[96] = font_glyphs;
static float  s_activeWhiteV = 0.0f;
static int    s_activeW = 0, s_activeH = 0;   // texture size, appended rows included
static int    s_activeDevice = 0;             // its metrics are device pixels

static bool atlas_resolve() {
  if (s_activeResolved) return s_activeSrv != 0;
  s_activeResolved = 1;
  runtime_prepare();
  if (runtime_ready() && ensure_runtime_texture()) {
    s_activeSrv    = s_runSrv;
    s_activeGlyphs = s_run.glyphs;
    s_activeWhiteV = s_runWhiteV;
    s_activeW      = s_run.w;
    s_activeH      = s_run.h + CANVAS_WHITE_ROWS;
    s_activeDevice = 1;
  } else if (ensure_atlas()) {
    s_activeSrv    = s_atlasSrv;
    s_activeGlyphs = font_glyphs;
    s_activeWhiteV = s_atlasWhiteV;
    s_activeW      = FONT_ATLAS_W;
    s_activeH      = FONT_ATLAS_H + CANVAS_WHITE_ROWS;
    s_activeDevice = 0;
  } else {
    s_activeSrv = 0;
    return false;
  }
  ov_set_texture(s_activeSrv);
  return true;
}

// The metrics the frame draws with, so a width and its glyphs never come from different atlases.
static const Glyph* metrics_for(int font) {
  if (s_activeResolved) return s_activeGlyphs[font];
  runtime_prepare();
  return runtime_ready() ? s_run.glyphs[font] : font_glyphs[font];
}
static bool metrics_are_device() {
  if (s_activeResolved) return s_activeDevice != 0;
  runtime_prepare();
  return runtime_ready();
}

static unsigned to_batch(unsigned c) { return (c << 8) | ((c >> 24) & 0xFFu); }

static OvPoint flat(float x, float y, unsigned rgba) {
  atlas_resolve();
  OvPoint p;
  p.x = x; p.y = y; p.u = s_whiteU; p.v = s_activeWhiteV; p.rgba = to_batch(rgba);
  return p;
}

bool canvas_white_texel(float* u, float* v) {
  if (u) *u = 0.0f;
  if (v) *v = 0.0f;
  if (!atlas_resolve()) return false;
  if (u) *u = s_whiteU;
  if (v) *v = s_activeWhiteV;
  return true;
}

static void flat_quad(float x0, float y0, float x1, float y1,
                      float x2, float y2, float x3, float y3, unsigned rgba) {
  const OvPoint a = flat(x0, y0, rgba), b = flat(x1, y1, rgba);
  const OvPoint c = flat(x2, y2, rgba), d = flat(x3, y3, rgba);
  ov_tri(a, b, c);
  ov_tri(a, c, d);
}

// A rect already in device pixels; everything draws through this so a multi-part shape scales its rect ONCE. Accepted:
// a right edge ui_scale_px(x)+ui_scale_px(w) can differ by one from ui_scale_px(x+w), a one-pixel seam at non-integer
// scales.
static void fill_device(int x, int y, int w, int h, unsigned rgba) {
  flat_quad((float)x,       (float)y,
            (float)(x + w), (float)y,
            (float)(x + w), (float)(y + h),
            (float)x,       (float)(y + h), rgba);
}

void canvas_begin(int w, int h) {
  s_devW = w;
  s_devH = h;
  // Cleared every frame: menu_draw leaves its transform standing, so earlier ESP drawing must not inherit its motion.
  canvas_set_transform(0, 0, 1.0, 0, 0);
  // This reopens the atlas choice, safe because overlay_begin just emptied the batch list.
  s_activeResolved = 0;
  s_activeSrv      = 0;
}

void canvas_end() {
  ov_set_texture(0);
  ov_set_additive(0);
  ov_clear_scissor();
}

void canvas_rect(int x, int y, int w, int h, unsigned rgba) {
  fill_device(dev_x(x), dev_y(y), dev_len(w), dev_len(h), rgba);
}

void canvas_line(int x0, int y0, int x1, int y1, unsigned rgba) {
  const int dx0 = dev_x(x0), dy0 = dev_y(y0);
  const int dx1 = dev_x(x1), dy1 = dev_y(y1);
  // GL draws a one-pixel line between pixel centres; here a quad half a pixel either side (same pixels when axis
  // aligned).
  const float ax = (float)dx0 + 0.5f, ay = (float)dy0 + 0.5f;
  const float bx = (float)dx1 + 0.5f, by = (float)dy1 + 0.5f;
  float nx = 0.5f, ny = 0.0f;
  const float ex = bx - ax, ey = by - ay;
  const float len = (float)sqrt((double)(ex * ex + ey * ey));
  if (len > 0.0f) { nx = -ey / len * 0.5f; ny = ex / len * 0.5f; }
  flat_quad(ax - nx, ay - ny, bx - nx, by - ny,
            bx + nx, by + ny, ax + nx, ay + ny, rgba);
}

void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned hi, unsigned lo) {
  // Scaled once, then the five parts built from the scaled rect (routing through canvas_rect would scale twice).
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  int e = dev_len(1);
  if (e < 1) e = 1;
  fill_device(dx, dy, dw, dh, fill);
  fill_device(dx,          dy,          dw, e,          hi);  // top, full width
  fill_device(dx,          dy + dh - e, dw, e,          lo);  // bottom, full width
  fill_device(dx,          dy + e,      e,  dh - 2 * e, hi);  // left, between top and bottom
  fill_device(dx + dw - e, dy + e,      e,  dh - 2 * e, lo);  // right, between top and bottom
}

void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned ground, int period) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  const int dp = dev_len(period);
  if (!canvas_hazard_params_ok(dw, dh, dp)) return;

  fill_device(dx, dy, dw, dh, ground);
  // 45 degree bars clipped to the strip. GL scissors from the bottom left; the rasteriser here from the top left.
  ov_set_scissor(dx, dy, dw, dh);
  for (int i = -dh; i < dw + dh; i += dp * 2) {
    flat_quad((float)(dx + i),           (float)(dy + dh),
              (float)(dx + i + dp),      (float)(dy + dh),
              (float)(dx + i + dp + dh), (float)dy,
              (float)(dx + i + dh),      (float)dy, stripe);
  }
  ov_clear_scissor();
}

// One tessellation for the rounded rect and the backdrop, else a hairline of untinted scene shows at every corner.
// Exactly 4*seg points, each quarter arc walked edge to edge inclusive, so at dr == 0 they collapse onto the plain
// rect.
static const int ROUND_FAN_MAX = 4 * 12;   // anim_round_rect_segments' own ceiling
static int round_fan_points(int dx, int dy, int dw, int dh, int dr, int seg,
                            float* px, float* py) {
  if (seg < 3) seg = 3;
  if (seg > 12) seg = 12;
  const double pi = 3.14159265358979323846;
  const float cx[4] = { (float)(dx + dr),      (float)(dx + dw - dr),
                        (float)(dx + dw - dr), (float)(dx + dr) };
  const float cy[4] = { (float)(dy + dr),      (float)(dy + dr),
                        (float)(dy + dh - dr), (float)(dy + dh - dr) };
  int n = 0;
  for (int c = 0; c < 4; c++) {
    const double a0 = (180.0 + 90.0 * c) * pi / 180.0;
    for (int i = 0; i < seg; i++) {
      const double a = a0 + (pi / 2.0) * (double)i / (double)(seg - 1);
      px[n] = cx[c] + (float)(dr * cos(a));
      py[n] = cy[c] + (float)(dr * sin(a));
      n++;
    }
  }
  return n;
}

// The rounded rectangle in DEVICE pixels: a fan from the centre, last point wrapped onto the first.
static void round_rect_device(int dx, int dy, int dw, int dh, int dr, int seg,
                              unsigned rgba) {
  float px[ROUND_FAN_MAX], py[ROUND_FAN_MAX];
  const int n = round_fan_points(dx, dy, dw, dh, dr, seg, px, py);
  const OvPoint c = flat(dx + dw * 0.5f, dy + dh * 0.5f, rgba);
  for (int i = 0; i < n; i++) {
    const int j = (i + 1 < n) ? i + 1 : 0;
    ov_tri(c, flat(px[i], py[i], rgba), flat(px[j], py[j], rgba));
  }
}

static int corner_radius_device(int dw, int dh, int r) {
  int dr = dev_len(r > 0 ? r : 0);
  const int half = (dw < dh ? dw : dh) / 2;
  if (dr > half) dr = half;
  return dr < 0 ? 0 : dr;
}

void canvas_round_rect(int x, int y, int w, int h, int r, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  const int dr = corner_radius_device(dw, dh, r);
  round_rect_device(dx, dy, dw, dh, dr, anim_round_rect_segments(dr), rgba);
}

// The rim is a real ring, not a filled shape one pixel larger (Glass's slab is translucent, a filled rim would wash a
// third of the face). The two outlines share a corner centre so the ring keeps its thickness (while dr >= t).
void canvas_round_rect_outline(int x, int y, int w, int h, int r, int thickness,
                               unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  int t = dev_len(thickness > 0 ? thickness : 0);
  if (t < 1) t = 1;
  const int dr  = corner_radius_device(dw, dh, r);
  const int seg = anim_round_rect_segments(dr);
  if (dw <= 2 * t || dh <= 2 * t) {
    round_rect_device(dx, dy, dw, dh, dr, seg, rgba);
    return;
  }
  float ox[ROUND_FAN_MAX], oy[ROUND_FAN_MAX];
  float ix[ROUND_FAN_MAX], iy[ROUND_FAN_MAX];
  const int n = round_fan_points(dx, dy, dw, dh, dr, seg, ox, oy);
  const int ir = dr - t < 0 ? 0 : dr - t;
  round_fan_points(dx + t, dy + t, dw - 2 * t, dh - 2 * t, ir, seg, ix, iy);
  for (int i = 0; i < n; i++) {
    const int j = (i + 1 < n) ? i + 1 : 0;
    const OvPoint o0 = flat(ox[i], oy[i], rgba), i0 = flat(ix[i], iy[i], rgba);
    const OvPoint o1 = flat(ox[j], oy[j], rgba), i1 = flat(ix[j], iy[j], rgba);
    ov_tri(o0, i0, o1);
    ov_tri(i0, i1, o1);
  }
}

void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned bottom) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  const OvPoint a = flat((float)dx,        (float)dy,        top);
  const OvPoint b = flat((float)(dx + dw), (float)dy,        top);
  const OvPoint c = flat((float)(dx + dw), (float)(dy + dh), bottom);
  const OvPoint d = flat((float)dx,        (float)(dy + dh), bottom);
  ov_tri(a, b, c);
  ov_tri(a, c, d);
}

void canvas_glow_rect(int x, int y, int w, int h, unsigned rgba) {
  const int dx = dev_x(x), dy = dev_y(y);
  const int dw = dev_len(w), dh = dev_len(h);
  if (dw <= 0 || dh <= 0) return;
  ov_set_additive(1);
  fill_device(dx, dy, dw, dh, rgba);
  ov_set_additive(0);
}

// The backdrop is STUBBED: Glass's ground is the scene copied from the back buffer, blurred, tinted; that needs a
// staging texture and CopyResource not built. Megaton ignores the capture's return, so false leaves the tint alone.
static int s_backSaid = 0;

bool canvas_backdrop_capture(int, int, int, int) {
  if (!s_backSaid) {
    s_backSaid = 1;
    wt_log("canvas: the Glass backdrop capture is not implemented yet; the style degrades to its tint");
  }
  return false;
}

// Draws the tint slab alone, as Megaton does when a capture fails; drawing nothing would float Glass's rim over bare
// game.
void canvas_backdrop_draw(int x, int y, int w, int h, int r, unsigned tint,
                          double, double, int, int) {
  static int said = 0;
  if (!said) { said = 1; wt_log("canvas: no captured ground; drawing the Glass tint alone"); }
  canvas_round_rect(x, y, w, h, r, tint);
}

void canvas_backdrop_release() {
  static int said = 0;
  if (!said) { said = 1; wt_log("canvas: canvas_backdrop_release is a stub; nothing to release"); }
}

int canvas_text(int x, int y, const char* s, unsigned rgba, int font) {
  if (!s || font < 0 || font >= FONT_COUNT) return x;
  if (!atlas_resolve()) return x;
  const int device = s_activeDevice;
  const Glyph* glyphs = s_activeGlyphs[font];
  const float iw = 1.0f / (float)s_activeW;
  const float ih = 1.0f / (float)s_activeH;
  const unsigned c = to_batch(rgba);
  // The pen: on the runtime atlas it is device from zero; on the baked atlas it stays logical and the scale applies to
  // the accumulated pen (scaling each advance rounds per character). The panel transform applies per glyph quad.
  const int ox = device ? ui_scale_px(x, s_scale) : 0;
  const int oy = device ? ui_scale_px(y, s_scale) : 0;
  int pen = device ? 0 : x;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7F) continue;
    const Glyph& g = glyphs[*p - 0x20];
    if (g.w > 0) {
      float gx, gy, gw, gh;
      if (device) {
        gx = (float)tx_dev_x(ox + pen + g.bx);  gy = (float)tx_dev_y(oy + g.by);
        gw = (float)tx_dev_len(g.w);            gh = (float)tx_dev_len(g.h);
      } else {
        gx = (float)dev_x(pen + g.bx);          gy = (float)dev_y(y + g.by);
        gw = (float)dev_len(g.w);               gh = (float)dev_len(g.h);
      }
      const float u0 = g.x * iw,           v0 = g.y * ih;
      const float u1 = (g.x + g.w) * iw,   v1 = (g.y + g.h) * ih;
      OvPoint a, b, d, e;
      a.x = gx;      a.y = gy;      a.u = u0; a.v = v0; a.rgba = c;
      b.x = gx + gw; b.y = gy;      b.u = u1; b.v = v0; b.rgba = c;
      d.x = gx + gw; d.y = gy + gh; d.u = u1; d.v = v1; d.rgba = c;
      e.x = gx;      e.y = gy + gh; e.u = u0; e.v = v1; e.rgba = c;
      ov_tri(a, b, d);
      ov_tri(a, d, e);
    }
    pen += g.adv;
  }
  return device ? x + ui_unscale_px(pen, s_scale) : pen;
}
#else  // NO_GL: recording bodies, so menu.cpp links into a test host without a renderer; this file stays the sole
       // definition of every canvas_* symbol. Recording each call makes the panel's output testable.
#include <string.h>

static CanvasOp s_ops[CANVAS_RECORD_MAX];
static int  s_opCount = 0;
static bool s_recording = false;
static bool s_overflowed = false;

void canvas_record_begin() { s_opCount = 0; s_overflowed = false; s_recording = true; }
void canvas_record_end()   { s_recording = false; }
int  canvas_record_count() { return s_opCount; }
bool canvas_record_overflowed() { return s_overflowed; }
const CanvasOp* canvas_record_op(int i) {
  return (i >= 0 && i < s_opCount) ? &s_ops[i] : nullptr;
}
const CanvasOp* canvas_record_find_text(const char* s) {
  if (!s) return nullptr;
  for (int i = 0; i < s_opCount; i++)
    if (s_ops[i].kind == CANVAS_OP_TEXT && strcmp(s_ops[i].text, s) == 0) return &s_ops[i];
  return nullptr;
}
const CanvasOp* canvas_record_find_kind(int kind, unsigned colour) {
  for (int i = 0; i < s_opCount; i++)
    if (s_ops[i].kind == kind && s_ops[i].colour == colour) return &s_ops[i];
  return nullptr;
}

// The one place an op is appended. Overflow sets a flag: a truncated recording would let a "never drawn" check pass.
static CanvasOp* push_op(int kind, int x, int y, int w, int h, unsigned colour) {
  if (!s_recording) return nullptr;
  if (s_opCount >= CANVAS_RECORD_MAX) { s_overflowed = true; return nullptr; }
  CanvasOp& o = s_ops[s_opCount++];
  o.kind = kind; o.x = x; o.y = y; o.w = w; o.h = h;
  o.colour = colour; o.font = -1; o.text[0] = 0;
  return &o;
}

// Every recorded coordinate is device space, as the device bodies hand the batcher, else "the scale is applied" could
// pass while nothing on screen moved. The transform is cleared here too: it belongs to the frame.
void canvas_begin(int, int) { canvas_set_transform(0, 0, 1.0, 0, 0); }
void canvas_end() {}
bool canvas_white_texel(float* u, float* v) {
  if (u) *u = 0.0f;
  if (v) *v = 0.0f;
  return false;
}
CanvasTransform canvas_record_transform() {
  CanvasTransform t;
  t.dx = s_txDx; t.dy = s_txDy; t.cx = s_txCx; t.cy = s_txCy; t.scale = s_txScale;
  return t;
}
void canvas_rect(int x, int y, int w, int h, unsigned c) {
  push_op(CANVAS_OP_RECT, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
void canvas_bevel(int x, int y, int w, int h, unsigned fill, unsigned, unsigned) {
  push_op(CANVAS_OP_BEVEL, dev_x(x), dev_y(y), dev_len(w), dev_len(h), fill);
}
void canvas_line(int x0, int y0, int x1, int y1, unsigned c) {
  const int dx0 = dev_x(x0), dy0 = dev_y(y0);
  const int dx1 = dev_x(x1), dy1 = dev_y(y1);
  push_op(CANVAS_OP_LINE, dx0, dy0, dx1 - dx0, dy1 - dy0, c);
}
void canvas_hazard(int x, int y, int w, int h, unsigned stripe, unsigned, int) {
  push_op(CANVAS_OP_HAZARD, dev_x(x), dev_y(y), dev_len(w), dev_len(h), stripe);
}
void canvas_round_rect(int x, int y, int w, int h, int, unsigned c) {
  push_op(CANVAS_OP_ROUNDRECT, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
void canvas_round_rect_outline(int x, int y, int w, int h, int, int, unsigned c) {
  push_op(CANVAS_OP_ROUNDOUTLINE, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
void canvas_gradient_rect(int x, int y, int w, int h, unsigned top, unsigned) {
  push_op(CANVAS_OP_GRADIENT, dev_x(x), dev_y(y), dev_len(w), dev_len(h), top);
}
void canvas_glow_rect(int x, int y, int w, int h, unsigned c) {
  push_op(CANVAS_OP_GLOW, dev_x(x), dev_y(y), dev_len(w), dev_len(h), c);
}
bool canvas_backdrop_capture(int x, int y, int w, int h) {
  push_op(CANVAS_OP_CAPTURE, dev_x(x), dev_y(y), dev_len(w), dev_len(h), 0u);
  return true;
}
void canvas_backdrop_draw(int x, int y, int w, int h, int, unsigned tint,
                          double, double, int, int) {
  push_op(CANVAS_OP_BACKDROP, dev_x(x), dev_y(y), dev_len(w), dev_len(h), tint);
}
void canvas_backdrop_release() {}

static const Glyph* metrics_for(int font) { return font_glyphs[font]; }
static bool metrics_are_device() { return false; }

int canvas_text(int x, int y, const char* s, unsigned c, int font) {
  // The recorded width is the rendered one (tab_text_fits compares it with a device edge). Accepted: ui_scale_px(sum of
  // advances) vs the device body's per-glyph rounding differs by about a pixel at a non-100 scale.
  CanvasOp* o = push_op(CANVAS_OP_TEXT, dev_x(x), dev_y(y),
                        dev_len(canvas_text_width(s, font)), 0, c);
  if (o) {
    o->font = font;
    if (s) strncpy_s(o->text, sizeof(o->text), s, _TRUNCATE);
  }
  return x;
}
#endif // NO_GL

// Logical for every caller (a scale-dependent width would apply the factor twice), measured through the atlas that will
// be drawn: GDI hinting is not linear, so the sum is over the live atlas with the scale divided back out.
int canvas_text_width(const char* s, int font) {
  if (!s || font < 0 || font >= FONT_COUNT) return 0;
  const Glyph* glyphs = metrics_for(font);
  int w = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if (*p < 0x20 || *p > 0x7F) continue;
    w += glyphs[*p - 0x20].adv;
  }
  return metrics_are_device() ? ui_unscale_px(w, s_scale) : w;
}

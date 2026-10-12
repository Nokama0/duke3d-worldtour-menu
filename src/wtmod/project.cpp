// The engine half of project.h: where the matrix, viewport and camera come from. THE MATRIX IS THE GAME'S OWN: World
// Tour writes a finished world-to-clip matrix once per 3D frame (late in FUN_007b9870, rva 0x109870); one rebuilt from
// globalang/globalhoriz drifts and markers slide towards the screen edges.
#include <windows.h>
#include <math.h>
#include "project.h"
#include "hook.h"     // wt_log

static const unsigned kRvaRenderer  = 0xEDEF28;   // DAT_0158ef28, a POINTER; call its value R
static const unsigned kOffMatrix    = 0x178;      // R + this: proj x view x world, 16 floats
static const unsigned kOffViewport  = 0x244;      // R + this: {x, y, w, h} ints
// R + this: the engine's "matrix is filled" flag, set by FUN_007e3fa0 when it writes the matrix; only when non-zero
// does the field mean anything.
static const unsigned kOffMatrixOk  = 0x190;
static const unsigned kRendererSpan = kOffViewport + 16;   // what we touch through R

static const unsigned kRvaGlobalPosX = 0xE07180;
static const unsigned kRvaGlobalPosY = 0xE07174;
static const unsigned kRvaGlobalPosZ = 0xE07170;

static const unsigned char* s_base = 0;
static unsigned s_imageSize = 0;
static void* const* s_pRenderer = 0;
static const int* s_pPosX = 0;
static const int* s_pPosY = 0;
static const int* s_pPosZ = 0;
static int s_resolved = 0;      // 0 not tried, 1 usable, -1 refused for good

// This frame's snapshot, taken by project_frame_begin, so one drawn frame is projected against ONE matrix.
static float        s_m[16];
static float        s_lastGoodM[16];
static bool         s_lastGoodOk = false;
// A matrix and its viewport travel together: the in-world pass draws with the last matrix the engine called valid, when
// s_vp may belong to a different moment, and the wrong viewport moves every vertex.
static ProjViewport s_lastGoodVp = { 0, 0, 0, 0 };
static ProjViewport s_vp = { 0, 0, 0, 0 };
static int          s_camX = 0, s_camY = 0, s_camZ = 0;
static bool         s_frameOk = false;
// The camera's right vector from the same matrix, once per frame (its square root is a Newton loop).
static double       s_right[3] = { 0.0, 0.0, 0.0 };
static bool         s_rightOk = false;

static int s_loggedNoRenderer = 0;
static int s_loggedBadRead = 0;
static int s_loggedFirst = 0;

static const void* engine_ptr(unsigned rva, unsigned size) {
  if (!s_base || rva > s_imageSize || size > s_imageSize - rva) return 0;
  return s_base + rva;
}

// R points into the heap, so it cannot be bounds-checked against the image: the pages we read must be committed and
// readable, and the span must fit inside the one region the query answers for.
static bool heap_readable(const void* p, unsigned size) {
  if (!p) return false;
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
  if (mbi.State != MEM_COMMIT) return false;
  const DWORD prot = mbi.Protect & 0xFF;
  if (prot == PAGE_NOACCESS || (mbi.Protect & PAGE_GUARD) != 0) return false;
  if (prot != PAGE_READONLY && prot != PAGE_READWRITE && prot != PAGE_WRITECOPY
      && prot != PAGE_EXECUTE_READ && prot != PAGE_EXECUTE_READWRITE
      && prot != PAGE_EXECUTE_WRITECOPY)
    return false;
  const unsigned char* start = (const unsigned char*)mbi.BaseAddress;
  const unsigned char* want  = (const unsigned char*)p;
  const SIZE_T used = (SIZE_T)(want - start);
  return used < mbi.RegionSize && (SIZE_T)size <= mbi.RegionSize - used;
}

static bool resolve() {
  if (s_resolved) return s_resolved > 0;
  s_resolved = -1;
  s_base = (const unsigned char*)GetModuleHandleA(0);
  if (!s_base) { wt_log("project: no module base; nothing will be projected"); return false; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("project: not a DOS image"); return false; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("project: not a PE image"); return false; }
  s_imageSize = nt->OptionalHeader.SizeOfImage;

  s_pRenderer = (void* const*)engine_ptr(kRvaRenderer, sizeof(void*));
  s_pPosX = (const int*)engine_ptr(kRvaGlobalPosX, sizeof(int));
  s_pPosY = (const int*)engine_ptr(kRvaGlobalPosY, sizeof(int));
  s_pPosZ = (const int*)engine_ptr(kRvaGlobalPosZ, sizeof(int));
  if (!s_pRenderer || !s_pPosX || !s_pPosY || !s_pPosZ) {
    wt_log("project: an engine address did not fit a %u byte image; nothing will be projected",
           s_imageSize);
    return false;
  }
  s_resolved = 1;
  wt_log("project: renderer pointer at rva 0x%X, camera at rva 0x%X/0x%X/0x%X",
         kRvaRenderer, kRvaGlobalPosX, kRvaGlobalPosY, kRvaGlobalPosZ);
  return true;
}

bool project_frame_begin() {
  s_frameOk = false;
  s_rightOk = false;
  s_vp.x = s_vp.y = s_vp.w = s_vp.h = 0;
  if (!resolve()) return false;

  const unsigned char* R = (const unsigned char*)*s_pRenderer;
  if (!R) {
    // Not a fault: before the first 3D frame there is no renderer object and so no projection; the next frame asks
    // again.
    if (!s_loggedNoRenderer) {
      s_loggedNoRenderer = 1;
      wt_log("project: the renderer pointer is null; no projection until a 3D frame is drawn");
    }
    return false;
  }
  if (!heap_readable(R, kRendererSpan)) {
    if (!s_loggedBadRead) {
      s_loggedBadRead = 1;
      wt_log("project: the renderer object at %p is not readable for %u bytes; "
             "nothing will be projected", (const void*)R, kRendererSpan);
    }
    return false;
  }

  const float* m = (const float*)(R + kOffMatrix);
  const int*   v = (const int*)(R + kOffViewport);
  ProjViewport vp;
  vp.x = v[0]; vp.y = v[1]; vp.w = v[2]; vp.h = v[3];
  if (!proj_matrix_ok(m) || !proj_viewport_ok(vp)) {
    if (!s_loggedBadRead) {
      s_loggedBadRead = 1;
      wt_log("project: the matrix or viewport at %p did not stand up "
             "(viewport %d,%d %dx%d); nothing will be projected",
             (const void*)R, vp.x, vp.y, vp.w, vp.h);
    }
    return false;
  }

  for (int i = 0; i < 16; i++) s_m[i] = m[i];
// Kept for readers that run where the field is not populated; proj_matrix_ok has already refused an identity.
  if (*(const int*)(R + kOffMatrixOk) != 0) {
    for (int i = 0; i < 16; i++) s_lastGoodM[i] = m[i];
    s_lastGoodVp = vp;
    s_lastGoodOk = true;
  }
  s_vp = vp;
  s_camX = *s_pPosX; s_camY = *s_pPosY; s_camZ = *s_pPosZ;
  s_rightOk = proj_camera_right(s_m, s_right);
  s_frameOk = true;
  if (!s_loggedFirst) {
    s_loggedFirst = 1;
    wt_log("project: first frame, viewport %d,%d %dx%d, camera %d,%d,%d",
           s_vp.x, s_vp.y, s_vp.w, s_vp.h, s_camX, s_camY, s_camZ);
  }
  return true;
}

const float* project_frame_matrix() { return s_frameOk ? s_m : 0; }
const float* project_last_good_matrix() { return s_lastGoodOk ? s_lastGoodM : 0; }

// Captured where the frame is DRAWN, not presented: the in-world pass draws mid-frame against this frame's depth buffer
// but used the previous Present's matrix, which differed by up to 5.771, so quads flickered. The caller takes this at
// the world's render-list submit; the flag check stays, as a right MOMENT is not a VALID matrix.
static float s_renderM[16];
static bool  s_renderOk = false;
static int   s_renderMisses = 0;

bool project_capture_render_matrix() {
  if (!s_pRenderer) { s_renderMisses++; return false; }
  const unsigned char* R = (const unsigned char*)*s_pRenderer;
  if (!R || !heap_readable(R, kRendererSpan)) { s_renderMisses++; return false; }
  const float* m = (const float*)(R + kOffMatrix);
  if (*(const int*)(R + kOffMatrixOk) == 0 || !proj_matrix_ok(m)) { s_renderMisses++; return false; }
  for (int i = 0; i < 16; i++) s_renderM[i] = m[i];
  s_renderOk = true;
  return true;
}

// The last matrix captured that way, or null before the first success. A miss keeps the previous one: a stale frame
// beats one with no quads.
const float* project_render_matrix() { return s_renderOk ? s_renderM : 0; }
int project_render_matrix_misses() { return s_renderMisses; }
bool project_last_good_viewport(ProjViewport* out) {
  if (out) { out->x = 0; out->y = 0; out->w = 0; out->h = 0; }
  if (!s_lastGoodOk || !out) return false;
  *out = s_lastGoodVp;
  return true;
}
int project_viewport_w() { return s_frameOk ? s_vp.w : 0; }
int project_viewport_h() { return s_frameOk ? s_vp.h : 0; }

// The raw snapshot, for a caller working in CLIP space: a POLYGON must be clipped against the near plane first. Null
// outside a frame project_frame_begin accepted.
const float* project_matrix() { return s_frameOk ? s_m : 0; }

static const double PROJ_CLIP_LINE_W = 1e-4;

bool project_clip_line_to_screen(int ax, int ay, int az, int bx, int by, int bz,
                                 int* x0, int* y0, int* x1, int* y1) {
  if (!s_frameOk) return false;
  double ca[4], cb[4];
  proj_clip_coords(s_m, proj_scale_x(ax), proj_scale_y(ay), proj_scale_z(az), ca);
  proj_clip_coords(s_m, proj_scale_x(bx), proj_scale_y(by), proj_scale_z(bz), cb);
  const bool aBehind = !(ca[3] > PROJ_CLIP_LINE_W);
  const bool bBehind = !(cb[3] > PROJ_CLIP_LINE_W);
  if (aBehind && bBehind) return false;          // none of it is in front
  if (aBehind || bBehind) {
// Where w crosses the plane, in the segment's own parameter (one end is above, the other below, so it cannot be zero).
    const double t = (PROJ_CLIP_LINE_W - ca[3]) / (cb[3] - ca[3]);
    double cut[4];
    // All four components lerp together, w included, so the clipped point lands exactly on the plane.
    for (int k = 0; k < 4; k++) cut[k] = ca[k] + (cb[k] - ca[k]) * t;
    for (int k = 0; k < 4; k++) { if (aBehind) ca[k] = cut[k]; else cb[k] = cut[k]; }
  }
  if (!(ca[3] > 0.0) || !(cb[3] > 0.0)) return false;   // a NaN takes this branch too
  if (x0) *x0 = proj_pixel_x(ca[0], ca[3], s_vp);
  if (y0) *y0 = proj_pixel_y(ca[1], ca[3], s_vp);
  if (x1) *x1 = proj_pixel_x(cb[0], cb[3], s_vp);
  if (y1) *y1 = proj_pixel_y(cb[1], cb[3], s_vp);
  return true;
}

bool project_viewport_rect(ProjViewport* out) {
  if (out) { out->x = 0; out->y = 0; out->w = 0; out->h = 0; }
  if (!s_frameOk || !out) return false;
  *out = s_vp;
  return true;
}

bool project_camera_pos(int* x, int* y, int* z) {
  if (x) *x = 0;
  if (y) *y = 0;
  if (z) *z = 0;
  if (!s_frameOk) return false;
  if (x) *x = s_camX;
  if (y) *y = s_camY;
  if (z) *z = s_camZ;
  return true;
}

// True 2D world distance from the camera. Doubles throughout: two int coordinates 2^31 apart square past what an int
// holds.
static int dist2d_from_camera(int wx, int wy) {
  const double dx = (double)wx - (double)s_camX;
  const double dy = (double)wy - (double)s_camY;
  return clamp_double_to_int(sqrt(dx * dx + dy * dy));
}

bool project_world_to_screen(int wx, int wy, int wz, ScreenPt* out) {
  const ScreenPt zero = { 0, 0, 0, 0, false };
  if (out) *out = zero;
  if (!s_frameOk) return false;
  ScreenPt p;
  if (!proj_project_scaled(s_m, s_vp, proj_scale_x(wx), proj_scale_y(wy), proj_scale_z(wz),
                           PROJ_ACCEPT_FRUSTUM, &p))
    return false;
  p.dist2d = dist2d_from_camera(wx, wy);
  if (out) *out = p;
  return true;
}

ScreenBox project_sprite_box(int wx, int wy, const SpriteExtents& ext) {
  ScreenBox b = { 0, 0, 0, 0, false, 0 };
  if (!s_frameOk) return b;

  // The lateral offset is along the camera's own right vector from the drawn matrix, not globalang: the two side
  // corners keep the anchor's depth exactly and the box stays centred at any angle.
  if (!s_rightOk) return b;
  const double half = (double)ext.halfWidth * 0.001;   // Build xy units to scaled
  const double ox = s_right[0] * half, oy = s_right[1] * half, oz = s_right[2] * half;

  const double cx = proj_scale_x(wx), cy = proj_scale_y(wy);
  const double zs[2] = { proj_scale_z(ext.topZ), proj_scale_z(ext.bottomZ) };

  static const int cc[4] = { 0, 1, 1, 0 };
  static const int rr[4] = { 0, 0, 1, 1 };

  long long minX = 0, maxX = 0, minY = 0, maxY = 0;
  for (int k = 0; k < 4; k++) {
    const double s = cc[k] ? 1.0 : -1.0;
    ScreenPt p;
// Depth, not frustum: a box is a SIZE, so a corner pushed off screen by a close-up enemy still measures; only the
// anchor is held to the frustum. The near plane stays in: a corner at w ~ 0 divides into millions.
    if (!proj_project_scaled(s_m, s_vp, cx + s * ox, cy + s * oy, zs[rr[k]] + s * oz,
                             PROJ_ACCEPT_DEPTH, &p))
      continue;
    if (b.corners == 0) { minX = maxX = p.x; minY = maxY = p.y; }
    else {
      if (p.x < minX) minX = p.x;
      if (p.x > maxX) maxX = p.x;
      if (p.y < minY) minY = p.y;
      if (p.y > maxY) maxY = p.y;
    }
    b.corners++;
  }

// All four corners, or nothing: unioning survivors left a full-width box with no height that the minimum-height floor
// turned into a screen-wide streak with `valid` true.
  if (b.corners != 4) return b;

// A box past the header's ceiling is a corner the near plane let through by a hair, not a big enemy; clamping would
// draw an 8192-pixel bracket and call it valid.
  if (maxX - minX > PROJ_BOX_MAX || maxY - minY > PROJ_BOX_MAX) return b;

  b.x = clamp_int(clamp_ll_to_int(minX), -PROJ_BOX_COORD_LIMIT, PROJ_BOX_COORD_LIMIT);
  b.y = clamp_int(clamp_ll_to_int(minY), -PROJ_BOX_COORD_LIMIT, PROJ_BOX_COORD_LIMIT);
  b.w = clamp_int(clamp_ll_to_int(maxX - minX), PROJ_BOX_MIN_W, PROJ_BOX_MAX);
  b.h = clamp_int(clamp_ll_to_int(maxY - minY), PROJ_BOX_MIN_H, PROJ_BOX_MAX);
  b.valid = true;
  return b;
}

// The in-world quad layer: depth-correct quads drawn among the world's own geometry. Pure half; worldwt.cpp is the
// D3D11 pass.
#pragma once

// BUILD world units: the transform happens once, GPU side, from the finished matrix at R+0x178.
struct WorldXYZ { int x, y, z; };

// Pull toward the camera, as a fraction of clip z. A tint sits ON a wall sprite's plane and needs a large pull; with it
// a far room drags every wall in front of every other and renders as a solid box.
const double W3D_PULL_COINCIDENT = 1e-4;   // a quad on a surface's own plane
// Measured on E1L1 sector 148: the room's z/w extent is 0.15x the 1e-4 pull at 73323 units. 1e-6 is 13x under it.
const double W3D_PULL_ROOM       = 1e-6;   // a quad on a room that has depth

struct WorldQuad3 { WorldXYZ p[4]; unsigned rgba; double pull; };

const int WORLD3D_MAX_QUADS = 256;

struct World3DQueue { WorldQuad3 q[WORLD3D_MAX_QUADS]; int n; int overflow; };

inline void world3d_reset(World3DQueue* w) { if (w) { w->n = 0; w->overflow = 0; } }

// A full queue REFUSES and counts the overflow, else a dropped quad and one never offered look identical.
inline bool world3d_push(World3DQueue* w, const WorldQuad3& q) {
  if (!w) return false;
  if (w->n >= WORLD3D_MAX_QUADS) { w->overflow++; return false; }
  w->q[w->n++] = q;
  return true;
}

// Clipped at the eye plane, not discarded whole: a quad with one corner behind the camera still has a visible part.
// Sutherland-Hodgman on w >= this value in HOMOGENEOUS clip space (interpolating after the divide is wrong).
const double WORLD3D_MIN_CLIP_W = 1e-4;
// At most five vertices; the return is the count written (< 3: wholly behind).
const int WORLD3D_CLIP_MAX = 5;

inline int world3d_clip_near(const double (*in)[4], int n, double (*out)[4]) {
  if (!in || !out || n < 3) return 0;
  int m = 0;
  for (int i = 0; i < n; i++) {
    const double* a = in[i];
    const double* b = in[(i + 1) % n];
    const bool ain = a[3] >= WORLD3D_MIN_CLIP_W;
    const bool bin = b[3] >= WORLD3D_MIN_CLIP_W;
    if (ain && m < WORLD3D_CLIP_MAX) {
      for (int k = 0; k < 4; k++) out[m][k] = a[k];
      m++;
    }
    if (ain != bin) {
      const double d = b[3] - a[3];
      if (d != 0.0 && m < WORLD3D_CLIP_MAX) {
        const double t = (WORLD3D_MIN_CLIP_W - a[3]) / d;
        for (int k = 0; k < 3; k++) out[m][k] = a[k] + t * (b[k] - a[k]);
        // W is assigned, not interpolated: interpolation can land a hair behind the plane, which is what this removes.
        out[m][3] = WORLD3D_MIN_CLIP_W;
        m++;
      }
    }
  }
  return m;
}

// A fast path that skips the clip. False means clip, not discard.
inline bool world3d_quad_ahead(const double w[4]) {
  for (int i = 0; i < 4; i++) if (!(w[i] > 0.0)) return false;
  return true;
}

// worldwt.cpp, the D3D11 half: draws on the game's render thread, so not host-linkable.
struct ID3D11Device;
struct ID3D11DeviceContext;
void world3d_set_device(ID3D11Device* dev, ID3D11DeviceContext* ctx);
void world3d_release();
void world3d_queue_reset();
bool world3d_push_quad(const WorldQuad3& q);
int  world3d_queue_count();
// After the world is submitted and before the HUD. The engine's depth is z/w, written unchanged (a remapped (z+w)/2
// read 0.9884 against the buffer's 0.9768): no remap, or the tint z-fights the button face.
void world3d_draw_now();
bool world3d_install(const unsigned char* base, unsigned imageSize);

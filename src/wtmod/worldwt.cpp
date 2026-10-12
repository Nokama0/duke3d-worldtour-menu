// The in-world quad layer's D3D11 half: depth-correct quads among the world's geometry (Megaton uses glBegin). Not the
// overlay's pass: that binds no depth buffer and draws last; this runs before the HUD and binds the depth buffer the
// overlay refuses. details: docs/re/in-world-quad-layer.md
#include <windows.h>
#include <d3d11.h>
#include "worldwt.h"
#include "world3d_shaders.h"
#include "project.h"
#include "hook.h"

struct World3DVertex { float x, y, z, w; float r, g, b, a; };

static ID3D11Device*            s_dev    = 0;
static ID3D11DeviceContext*     s_ctx    = 0;
static ID3D11VertexShader*      s_vs     = 0;
static ID3D11PixelShader*       s_ps     = 0;
static ID3D11InputLayout*       s_layout = 0;
static ID3D11Buffer*            s_vb     = 0;
static ID3D11DepthStencilState* s_depth  = 0;
static ID3D11BlendState*        s_blend  = 0;
static ID3D11RasterizerState*   s_raster = 0;
static int s_ready = 0;
static int s_failed = 0;
static World3DQueue s_queue;
static int s_loggedDraw = 0;
static int s_loggedCapture = 0;
static int s_loggedFallback = 0;
static int s_loggedNoMatrix = 0;
static void* const* s_pRenderer = 0;

// A fixed PROPORTION nearer, not a fixed amount: the world occupies 0.004 of the depth range, so a fixed offset cannot
// separate two copies of one plane (residual 4.5-5.2e-05, constant in depth); scaling clip z pulls by a fixed fraction.
// details: docs/re/in-world-layer-depth-offsets.md
#define W3D_DEPTH_PULL (1.0 - 1e-4)

// No rasteriser bias: -150 on this float depth buffer (world near z ~ 0.999) is a FIXED ~-8.9e-6, which once the room
// outline's pull fell to 1e-6 dragged a distant room's far walls in front of near ones. The per-quad fraction does all
// the work.
#define W3D_DEPTH_BIAS (0)

// Six vertices a quad (two triangles): no index buffer.
#define W3D_VERTS_PER_QUAD 6
// A clipped quad fans into up to three triangles: nine vertices a quad.
#define W3D_VERTS_PER_CLIPPED 9
#define W3D_MAX_VERTS (WORLD3D_MAX_QUADS * W3D_VERTS_PER_CLIPPED)

void world3d_queue_reset() { world3d_reset(&s_queue); }
bool world3d_push_quad(const WorldQuad3& q) { return world3d_push(&s_queue, q); }
int  world3d_queue_count() { return s_queue.n; }

static int world3d_fail(const char* what, HRESULT hr) {
  s_failed = 1;
  wt_log("world3d: %s (hr=0x%08lX); the in-world layer stays off for this session",
         what, (unsigned long)hr);
  return 0;
}

// Created once from the device the overlay has. A failure is permanent for the session and names the call that refused.
static int world3d_create() {
  if (s_ready) return 1;
  if (s_failed || !s_dev) return 0;

  HRESULT hr = s_dev->CreateVertexShader(kWorld3DVsBytecode, sizeof(kWorld3DVsBytecode), 0, &s_vs);
  if (FAILED(hr)) return world3d_fail("CreateVertexShader failed", hr);
  hr = s_dev->CreatePixelShader(kWorld3DPsBytecode, sizeof(kWorld3DPsBytecode), 0, &s_ps);
  if (FAILED(hr)) return world3d_fail("CreatePixelShader failed", hr);

  const D3D11_INPUT_ELEMENT_DESC el[2] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
  };
  hr = s_dev->CreateInputLayout(el, 2, kWorld3DVsBytecode, sizeof(kWorld3DVsBytecode), &s_layout);
  if (FAILED(hr)) return world3d_fail("CreateInputLayout failed", hr);

  D3D11_BUFFER_DESC bd;
  ZeroMemory(&bd, sizeof(bd));
  bd.ByteWidth      = sizeof(World3DVertex) * W3D_MAX_VERTS;
  bd.Usage          = D3D11_USAGE_DYNAMIC;
  bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = s_dev->CreateBuffer(&bd, 0, &s_vb);
  if (FAILED(hr)) return world3d_fail("CreateBuffer for the vertex buffer failed", hr);

  // Test ON, write OFF: hidden by geometry already in the depth buffer, and nothing put in it for the game's later
  // passes to be occluded by.
  D3D11_DEPTH_STENCIL_DESC dsd;
  ZeroMemory(&dsd, sizeof(dsd));
  dsd.DepthEnable    = TRUE;
  dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  dsd.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
  dsd.StencilEnable  = FALSE;
  hr = s_dev->CreateDepthStencilState(&dsd, &s_depth);
  if (FAILED(hr)) return world3d_fail("CreateDepthStencilState failed", hr);

  D3D11_BLEND_DESC bl;
  ZeroMemory(&bl, sizeof(bl));
  bl.RenderTarget[0].BlendEnable           = TRUE;
  bl.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
  bl.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
  bl.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
  bl.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
  bl.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_INV_SRC_ALPHA;
  bl.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
  bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  hr = s_dev->CreateBlendState(&bl, &s_blend);
  if (FAILED(hr)) return world3d_fail("CreateBlendState failed", hr);

  // Cull none: a tint quad is a flat face seen from either side. Depth clip stays ON: it makes the near plane mean
  // something.
  D3D11_RASTERIZER_DESC rd;
  ZeroMemory(&rd, sizeof(rd));
  rd.FillMode        = D3D11_FILL_SOLID;
  rd.CullMode        = D3D11_CULL_NONE;
  rd.DepthClipEnable = TRUE;
  // Polygon offset in this engine's magnitude: Megaton's glPolygonOffset(-1, -2) buys 1.2e-07, below the measured
  // coincidence residual (1.0e-06), so the units are the engine's own wall-sprite DepthBias -150 (8.9e-06), clamp
  // -0.01, slope factor 0. details: docs/re/in-world-layer-depth-offsets.md
  rd.DepthBias             = W3D_DEPTH_BIAS;
  rd.SlopeScaledDepthBias  = 0.0f;
  rd.DepthBiasClamp        = -0.01f;
  hr = s_dev->CreateRasterizerState(&rd, &s_raster);
  if (FAILED(hr)) return world3d_fail("CreateRasterizerState failed", hr);

  s_ready = 1;
  wt_log("world3d: pipeline up -- pass-through clip-space vs, depth test on with write off, "
         "alpha blend, cull none, depth bias %d with slope scale %.1f and clamp %.3f -- no fixed "
         "term, since a fixed offset cannot work in a buffer this compressed -- and each quad "
         "pulled nearer by its OWN fraction of clip z, %.0e for a coincident face and %.0e for a "
         "room that has depth. Up to %d quads a frame",
         rd.DepthBias, rd.SlopeScaledDepthBias, rd.DepthBiasClamp,
         W3D_PULL_COINCIDENT, W3D_PULL_ROOM,
         WORLD3D_MAX_QUADS);
  return 1;
}

void world3d_set_device(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
  if (s_dev == dev && s_ctx == ctx) return;
  world3d_release();
  s_dev = dev;
  s_ctx = ctx;
  s_failed = 0;
}

void world3d_release() {
  if (s_vs)     { s_vs->Release();     s_vs = 0; }
  if (s_ps)     { s_ps->Release();     s_ps = 0; }
  if (s_layout) { s_layout->Release(); s_layout = 0; }
  if (s_vb)     { s_vb->Release();     s_vb = 0; }
  if (s_depth)  { s_depth->Release();  s_depth = 0; }
  if (s_blend)  { s_blend->Release();  s_blend = 0; }
  if (s_raster) { s_raster->Release(); s_raster = 0; }
  s_ready = 0;
}

// Only what this pass touches: no shader resources, sampler or constant buffer.
struct W3DSaved {
  ID3D11RenderTargetView*  rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
  ID3D11DepthStencilView*  dsv;
  D3D11_PRIMITIVE_TOPOLOGY topology;
  ID3D11InputLayout*       layout;
  ID3D11Buffer*            vb;  UINT vbStride, vbOffset;
  ID3D11VertexShader*      vs;
  ID3D11PixelShader*       ps;
  ID3D11BlendState*        blend;  FLOAT blendFactor[4]; UINT blendMask;
  ID3D11DepthStencilState* depth;  UINT stencilRef;
  ID3D11RasterizerState*   raster;
};
static W3DSaved s_saved;

static void w3d_rel(IUnknown* p) { if (p) p->Release(); }

// Is this the identity? project.cpp's proj_matrix_ok only refuses non-finite, huge or all-zero, so an identity read
// looked good upstream.
void world3d_draw_now() {
  if (!s_ctx || s_queue.n <= 0) { world3d_reset(&s_queue); return; }
  if (!world3d_create()) { world3d_reset(&s_queue); return; }

  // The LAST MATRIX THE ENGINE CALLED VALID, not the live field: where this pass draws, R+0x178, R+0x510998 and the
  // flag at R+0x190 read identity/0 (w = 1, depth 0.564 against a floor of 0.9962). This frame's (captured at the
  // world's submit) first, then the previous frame's, which flickered.
  const float* m = project_render_matrix();
  if (!m) {
    m = project_last_good_matrix();
    if (m && !s_loggedFallback) {
      s_loggedFallback = 1;
      wt_log("world3d: no matrix has been captured at the world submit yet (%d misses), so this "
             "draw uses the one captured at Present, which is a frame behind",
             project_render_matrix_misses());
    }
  }
  if (!m) {
    if (!s_loggedNoMatrix) {
      s_loggedNoMatrix = 1;
      wt_log("world3d: no valid matrix has been seen yet, so nothing is drawn. This clears once "
             "a frame has been projected with the engine's R+0x190 flag set");
    }
    world3d_reset(&s_queue);
    return;
  }

  // Vertices first, so a quad the projection refuses costs no state change.
  static World3DVertex verts[W3D_MAX_VERTS];
  int nv = 0;
  int quadsDrawn = 0, quadsClipped = 0, quadsBehind = 0;
  for (int i = 0; i < s_queue.n; i++) {
    const WorldQuad3& q = s_queue.q[i];
    double c[4][4];
    double w[4];
    int ok = 1;
    for (int k = 0; k < 4; k++) {
      proj_clip_coords(m, proj_scale_x(q.p[k].x), proj_scale_y(q.p[k].y),
                       proj_scale_z(q.p[k].z), c[k]);
      w[k] = c[k][3];
    }
    (void)ok;
    // The quad's pull is bounded here, not trusted: an uninitialised or absurd value gets the coincident default
    // instead of a quad in front of the world.
    double pullFrac = q.pull;
    if (!(pullFrac > 0.0) || !(pullFrac < 1e-2)) pullFrac = W3D_PULL_COINCIDENT;
    const double pull = 1.0 - pullFrac;
    const float rr = (float)((q.rgba >> 16) & 0xFF) / 255.0f;
    const float gg = (float)((q.rgba >>  8) & 0xFF) / 255.0f;
    const float bb = (float)((q.rgba      ) & 0xFF) / 255.0f;
    const float aa = (float)((q.rgba >> 24) & 0xFF) / 255.0f;
    // Four corners go straight through; only a quad straddling the eye plane pays for the clip, and one wholly behind
    // costs nothing.
    double poly[WORLD3D_CLIP_MAX][4];
    int npoly;
    if (world3d_quad_ahead(w)) {
      for (int k = 0; k < 4; k++)
        for (int j = 0; j < 4; j++) poly[k][j] = c[k][j];
      npoly = 4;
    } else {
      npoly = world3d_clip_near(c, 4, poly);
      if (npoly >= 3) quadsClipped++;
    }
    if (npoly < 3) { quadsBehind++; continue; }
    // Fanned from the first vertex: the clip returns a convex ring, so a fan is a correct triangulation.
    for (int t = 1; t + 1 < npoly && nv + 3 <= W3D_MAX_VERTS; t++) {
      const int idx[3] = { 0, t, t + 1 };
      for (int j = 0; j < 3; j++) {
        World3DVertex& v = verts[nv++];
        const double* cv = poly[idx[j]];
        v.x = (float)cv[0];
        v.y = (float)cv[1];
        // Straight through: the GPU divides by w, and z/w is the convention of the engine's depth buffer (worldwt.h).
        v.z = (float)(cv[2] * pull);
        v.w = (float)cv[3];
        v.r = rr; v.g = gg; v.b = bb; v.a = aa;
      }
    }
    quadsDrawn++;
  }
  // The depth our first quad carries, in the buffer's units. The scene's depth sits in [0.9962, 1.0] (gluPerspective
  // n=0.01, f=100), so a quad at 0.98 is NEARER than everything and LESS_EQUAL passes everywhere: "draws through
  // walls".
  double firstZ = -1.0, firstW = 0.0;
  if (nv > 0 && verts[0].w != 0.0f) {
    firstW = (double)verts[0].w;
    firstZ = (double)verts[0].z / firstW;
  }
  if (nv <= 0) { world3d_reset(&s_queue); return; }

  D3D11_MAPPED_SUBRESOURCE map;
  if (FAILED(s_ctx->Map(s_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
    world3d_reset(&s_queue);
    return;
  }
  memcpy(map.pData, verts, sizeof(World3DVertex) * (size_t)nv);
  s_ctx->Unmap(s_vb, 0);

  ZeroMemory(&s_saved, sizeof(s_saved));
  s_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s_saved.rtv, &s_saved.dsv);
  // Taken here, not at the log: counting s_saved's targets after the restore and ZeroMemory reported "0 targets, depth
  // ABSENT" every frame. These are the state the Draw runs under.
  int atDrawColour = 0;
  for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
    if (s_saved.rtv[i]) atDrawColour++;
  const void* atDrawDsv = (const void*)s_saved.dsv;
  UINT nvpAtDraw = 1;
  D3D11_VIEWPORT vpAtDraw;
  ZeroMemory(&vpAtDraw, sizeof(vpAtDraw));
  s_ctx->RSGetViewports(&nvpAtDraw, &vpAtDraw);
  s_ctx->IAGetPrimitiveTopology(&s_saved.topology);
  s_ctx->IAGetInputLayout(&s_saved.layout);
  s_ctx->IAGetVertexBuffers(0, 1, &s_saved.vb, &s_saved.vbStride, &s_saved.vbOffset);
  s_ctx->VSGetShader(&s_saved.vs, 0, 0);
  s_ctx->PSGetShader(&s_saved.ps, 0, 0);
  s_ctx->OMGetBlendState(&s_saved.blend, s_saved.blendFactor, &s_saved.blendMask);
  s_ctx->OMGetDepthStencilState(&s_saved.depth, &s_saved.stencilRef);
  s_ctx->RSGetState(&s_saved.raster);

  // The game's own targets, kept as they are: this pass wants the depth buffer the game already renders against.
  const UINT stride = sizeof(World3DVertex), offset = 0;
  const FLOAT factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
  s_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  s_ctx->IASetInputLayout(s_layout);
  s_ctx->IASetVertexBuffers(0, 1, &s_vb, &stride, &offset);
  s_ctx->VSSetShader(s_vs, 0, 0);
  s_ctx->PSSetShader(s_ps, 0, 0);
  s_ctx->OMSetBlendState(s_blend, factor, 0xFFFFFFFFu);
  s_ctx->OMSetDepthStencilState(s_depth, 0);
  s_ctx->RSSetState(s_raster);

  s_ctx->Draw((UINT)nv, 0);

  s_ctx->IASetPrimitiveTopology(s_saved.topology);
  s_ctx->IASetInputLayout(s_saved.layout);
  s_ctx->IASetVertexBuffers(0, 1, &s_saved.vb, &s_saved.vbStride, &s_saved.vbOffset);
  s_ctx->VSSetShader(s_saved.vs, 0, 0);
  s_ctx->PSSetShader(s_saved.ps, 0, 0);
  s_ctx->OMSetBlendState(s_saved.blend, s_saved.blendFactor, s_saved.blendMask);
  s_ctx->OMSetDepthStencilState(s_saved.depth, s_saved.stencilRef);
  s_ctx->RSSetState(s_saved.raster);
  s_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s_saved.rtv, s_saved.dsv);

  // The counts are a condition, not a position: the first draw is the moment, one line a session.
  if (!s_loggedDraw) {
    s_loggedDraw = 1;
    wt_log("world3d: first draw -- %d quads offered, %d drawn, %d clipped at the eye plane, "
           "%d wholly behind the camera, %d refused by a full queue",
           s_queue.n, quadsDrawn, quadsClipped, quadsBehind, s_queue.overflow);

  }
  // How stale the matrix is: we draw with the last valid matrix, captured at the previous Present, against a depth
  // buffer written this frame. A moving camera flickers whatever the bias; a still one reads zero and rules it out.


  for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) w3d_rel(s_saved.rtv[i]);
  w3d_rel(s_saved.dsv);
  w3d_rel(s_saved.layout);
  w3d_rel(s_saved.vb);
  w3d_rel(s_saved.vs);
  w3d_rel(s_saved.ps);
  w3d_rel(s_saved.blend);
  w3d_rel(s_saved.depth);
  w3d_rel(s_saved.raster);
  ZeroMemory(&s_saved, sizeof(s_saved));

  world3d_reset(&s_queue);
}

// The seam: FUN_00805900 is the engine's clear; ECX is a descriptor (RTV +0x30, DSV +0x34) and the argument a mask (bit
// 1 depth, 2 stencil). Depth is cleared twice a frame, just before the first-person weapon, so draw on the FIRST
// qualifying clear. details: docs/re/in-world-quad-layer.md
static const unsigned kRvaClear = 0x155900;      // FUN_00805900
// The fingerprint skips the relocated dword at +3 (prologue `55 8B EC A1 <address>`, address slid by ASLR): 3 bytes at
// +0, step over the load, resume at +8 on fourteen address-free bytes (push ebx / mov ebx,[ebp+8] / push esi / mov
// esi,ecx / push edi / mov edi,[eax+0xB398]).
static const unsigned char kFpClear[3] = { 0x55, 0x8B, 0xEC };
static const unsigned kClearPrologue = 8;        // to the end of `mov eax,[imm32]`
static const unsigned kClearTailAt = 8;
static const unsigned char kFpClearTail[14] = {
  0x53, 0x8B, 0x5D, 0x08, 0x56, 0x8B, 0xF1, 0x57, 0x8B, 0xB8, 0x98, 0xB3, 0x00, 0x00
};
static const unsigned kClearDsvOffset = 0x34;
// Bit 1 is depth, bit 2 stencil; the engine skips the call when both are clear.
#define W3D_CLEAR_DEPTH_BITS 0x6u
static const unsigned kRvaRenderer = 0xEDEF28;   // R = *(void**)this

// The ordering signal: four identical qualifying clears run a frame and the first lands before the world is drawn.
// FUN_0078bf00 submits render lists per owner, twice a frame from FUN_007805a0 (call site 0x007805f1; R+0x10 is the
// camera, so the first call is the world's). Latch on it, take the first qualifying clear after.
static const unsigned kRvaSubmit = 0xDBF00;      // FUN_0078bf00
static const unsigned char kFpSubmit[5] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
static const unsigned kSubmitPrologue = 5;
// The tail skips a relocated dword too: after the prologue come `68 <handler>` (an SEH handler address ASLR slides);
// match the opcode at +5, skip four bytes, resume at +10 on `64 A1 00 00 00 00` (mov eax, fs:[0]).
#define W3D_PUSH_OPCODE 0x68
static const unsigned kSubmitTailAt = 10;
static const unsigned char kFpSubmitTail[6] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
static const unsigned kCameraAtR = 0x10;         // R+0x10 is the camera object

typedef void (__fastcall* SubmitFn)(void*, void*, void*);
static SubmitFn volatile s_origSubmit = 0;
static volatile LONG s_armed = 0;        // a world submit has happened, no clear taken since
static volatile LONG s_latchOk = 0;      // the submit hook installed; without it there is no order
static volatile LONG s_worldSubmits = 0;
static volatile LONG s_otherSubmits = 0;

static void __fastcall wt_submit_lists(void* self, void* edx, void* listOwner) {
  SubmitFn orig = s_origSubmit;
  while (!orig) { YieldProcessor(); orig = s_origSubmit; }
  orig(self, edx, listOwner);
  // WHICH submit this is, by the owner the call site passes; anything but the camera object is the second call (2D
  // pass).
  const void* camera = 0;
  if (s_pRenderer) {
    const unsigned char* R = (const unsigned char*)*s_pRenderer;
    if (R) camera = (const void*)(R + kCameraAtR);
  }
  if (camera && listOwner == camera) {
    // This frame's matrix, taken here because the renderer's field is populated at this moment (it reads identity at
    // the clear seam). The flag check is inside the capture.
    const int got = project_capture_render_matrix() ? 1 : 0;
    if (!s_loggedCapture) {
      s_loggedCapture = 1;
      wt_log("world3d: at the world's own render-list submit the renderer's matrix is %s. This "
             "is the matrix the depth buffer is about to be written with, so the quads are "
             "projected from the same camera position they are tested against",
             got ? "POPULATED and valid, and has been captured"
                 : "NOT valid here, so the draw falls back to the one captured at Present");
    }
    InterlockedIncrement(&s_worldSubmits);
    InterlockedExchange(&s_otherSubmits, 0);
    InterlockedExchange(&s_armed, 1);
  } else {
    InterlockedIncrement(&s_otherSubmits);
  }
}

typedef void (__fastcall* ClearFn)(void*, void*, unsigned);
static ClearFn volatile s_origClear = 0;
static int s_seamOk = 0;
static int s_loggedSeam = 0;

// The mask alone was not enough: the draw fired at a moment whose buffer held no world depth, so the call was clearing
// SOME OTHER depth resource. The clear's target (desc+0x34) must equal the view the output merger has bound: then it is
// about to discard the buffer the quads test against.
static void __fastcall wt_clear_targets(void* desc, void* edx, unsigned mask) {
  ClearFn orig = s_origClear;
  while (!orig) { YieldProcessor(); orig = s_origClear; }

  // BEFORE the original: the depth this call discards is the only depth these quads can be tested against.
  const int clearsDepth = (mask & W3D_CLEAR_DEPTH_BITS) != 0;
  const void* wanted = 0;
  if (desc && clearsDepth && !IsBadReadPtr(desc, kClearDsvOffset + sizeof(void*)))
    wanted = *(void* const*)((const unsigned char*)desc + kClearDsvOffset);
  if (s_ctx && wanted) {
    ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { 0 };
    ID3D11DepthStencilView* bound = 0;
    s_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &bound);
    int colour = 0;
    for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) if (rtv[i]) colour++;
    // Three measured conditions: the clear is about to empty the buffer the draw binds, exactly one colour target is
    // bound (four is the G-buffer, none no draw), and it is the first such call since the queue filled.
    const int sameView = (bound != 0 && (const void*)bound == wanted);
    const int matches = sameView && colour == 1;
    // The latch is the ordering: the first qualifying clear after the world's lists were submitted. Without the submit
    // hook the seam falls back to the first of the frame, known too early, and says so.
    const int armed = s_latchOk ? (InterlockedExchange(&s_armed, 0) != 0) : 1;
    const int usable = matches && armed;

    if (!s_loggedSeam && usable) {
      s_loggedSeam = 1;
      wt_log("world3d: the first clear this seam takes has mask 0x%X, one colour target, and "
             "clears the very depth view that is bound (%p). The quads go out HERE, before the "
             "clear, which is the last moment the world's depth is still in the buffer",
             mask, wanted);
    }
    for (int i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
      if (rtv[i]) rtv[i]->Release();
    if (bound) bound->Release();
    // project_frame_begin is NOT called: it reads the live field, an identity here that proj_matrix_ok correctly
    // refuses. The draw uses the last valid matrix.
    if (usable) world3d_draw_now();
  }
  orig(desc, edx, mask);
}


bool world3d_install(const unsigned char* base, unsigned imageSize) {
  if (!base || kRvaRenderer + 4 > imageSize) {
    wt_log("world3d: the renderer pointer at rva 0x%X does not fit the image", kRvaRenderer);
    return false;
  }
  s_pRenderer = (void* const*)(base + kRvaRenderer);
  const unsigned need = kClearTailAt + (unsigned)sizeof(kFpClearTail);
  if (kRvaClear >= imageSize || need > imageSize - kRvaClear) {
    wt_log("world3d: rva 0x%X is outside the image", kRvaClear);
    return false;
  }
  const unsigned char* at = base + kRvaClear;
  if (memcmp(at, kFpClear, sizeof(kFpClear)) != 0) {
    wt_log("world3d: rva 0x%X is %02X %02X %02X, not the push ebp / mov ebp,esp we read; the "
           "in-world layer stays off", kRvaClear, at[0], at[1], at[2]);
    return false;
  }
  if (memcmp(at + kClearTailAt, kFpClearTail, sizeof(kFpClearTail)) != 0) {
    const unsigned char* t = at + kClearTailAt;
    wt_log("world3d: rva 0x%X at +%u is %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X, not "
           "the push ebx / mov ebx,[ebp+8] / push esi / mov esi,ecx / push edi / "
           "mov edi,[eax+0xB398] we read; the in-world layer stays off",
           kRvaClear, kClearTailAt, t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9]);
    return false;
  }
  void* tramp = 0;
  if (!hook_install(kRvaClear, kFpClear, sizeof(kFpClear), kClearPrologue,
                    (void*)wt_clear_targets, &tramp) || !tramp) {
    wt_log("world3d: the clear hook did not take; the in-world layer stays off");
    return false;
  }
  s_origClear = (ClearFn)tramp;
  s_seamOk = 1;

  // The ordering latch. Its absence is not fatal (the clear list prints and the layer draws) but draws at the wrong
  // clear, so the failure says that.
  {
    const unsigned needS = kSubmitTailAt + (unsigned)sizeof(kFpSubmitTail);
    const unsigned char* sa = base + kRvaSubmit;
    void* strampoline = 0;
    if (kRvaSubmit >= imageSize || needS > imageSize - kRvaSubmit) {
      wt_log("world3d: rva 0x%X is outside the image, so there is no ordering latch and the "
             "seam falls back to the frame's FIRST qualifying clear, which is too early",
             kRvaSubmit);
    } else if (memcmp(sa, kFpSubmit, sizeof(kFpSubmit)) != 0) {
      wt_log("world3d: rva 0x%X is %02X %02X %02X %02X %02X, not the push ebp / mov ebp,esp / "
             "push -1 we read; no ordering latch, so the seam falls back to the frame's FIRST "
             "qualifying clear, which is too early", kRvaSubmit,
             sa[0], sa[1], sa[2], sa[3], sa[4]);
    } else if (sa[kSubmitPrologue] != W3D_PUSH_OPCODE) {
      wt_log("world3d: rva 0x%X at +%u is %02X, not the push opcode we read; no ordering latch",
             kRvaSubmit, kSubmitPrologue, sa[kSubmitPrologue]);
    } else if (memcmp(sa + kSubmitTailAt, kFpSubmitTail, sizeof(kFpSubmitTail)) != 0) {
      const unsigned char* t = sa + kSubmitTailAt;
      wt_log("world3d: rva 0x%X at +%u is %02X %02X %02X %02X %02X %02X, not the mov eax,fs:[0] "
             "we read; no ordering latch", kRvaSubmit, kSubmitTailAt,
             t[0], t[1], t[2], t[3], t[4], t[5]);
    } else if (!hook_install(kRvaSubmit, kFpSubmit, sizeof(kFpSubmit), kSubmitPrologue,
                             (void*)wt_submit_lists, &strampoline) || !strampoline) {
      wt_log("world3d: the list-submit hook did not take; no ordering latch");
    } else {
      s_origSubmit = (SubmitFn)strampoline;
      InterlockedExchange(&s_latchOk, 1);
      wt_log("world3d: rva 0x%X submits the render lists, and its call with the camera object at "
             "R+0x%X arms the seam -- so the quads go out on the first qualifying clear AFTER "
             "the world's lists, not the first of the frame", kRvaSubmit, kCameraAtR);
    }
  }
  wt_log("world3d: rva 0x%X is the engine's clear, and the quads go out on its first depth "
         "clear each frame -- the one the first-person weapon is about to be drawn behind -- "
         "so they are tested against the world's own depth rather than against a buffer that "
         "has already been emptied", kRvaClear);
  return true;
}


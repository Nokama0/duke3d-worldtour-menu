// The overlay's Direct3D 11 side: one font atlas, one pipeline, a draw per batch, and a full save and restore of every
// context state it touches. The game is mid-frame, so put back everything you changed and release every interface a
// Get* call hands back (it arrives with a reference; one leak per frame empties the process in minutes).
#include "overlay.h"
#include "worldwt.h"   // world3d_set_device: the in-world layer shares this device
#include "overlay_internal.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdlib.h>
#include <string.h>
#include "overlay_shaders.h"

// Defined in wtmod.cpp; the whole mod writes to one log.
void wt_log(const char* fmt, ...);

// 20 bytes: two floats of position in pixels, two of atlas uv, four colour
// bytes. Rectangles and glyphs use the same vertex, rectangles by sampling a
// white cell in the atlas, which is what lets one draw call serve both.
struct OvVertex { float x, y, u, v; unsigned color; };

// Far more than the readout needs; the cap keeps a looping caller from overrunning (a dropped quad is a missing
// character). Sized for the panel's rounded-corner fans (up to twelve segments each) and hazard strips (a quad per
// stripe).
#define OV_MAX_VERTS 24576

static OvVertex s_vert[OV_MAX_VERTS];
static int      s_vertCount = 0;

// One draw per run of vertices sharing an atlas, blend and scissor box (the panel needs its own atlas, an additive pass
// for glows and a scissor for hazard stripes). Order is call order, so the readout lands on top of the panel as queued.
#define OV_MAX_BATCHES 256

struct OvBatch {
  ID3D11ShaderResourceView* srv;      // null: the readout's own atlas
  int        additive;
  int        scissor;
  D3D11_RECT rect;                    // only read when scissor
  int        first, count;            // into s_vert
};

static OvBatch s_batch[OV_MAX_BATCHES];
static int     s_batchCount = 0;
static int     s_batchLost  = 0;      // logged once a frame, not once a quad

// What the next vertex will be drawn under.
static ID3D11ShaderResourceView* s_curSrv      = 0;
static int        s_curAdditive = 0;
static int        s_curScissor  = 0;
static D3D11_RECT s_curRect     = { 0, 0, 0, 0 };

#define OV_FIRST_GLYPH 32
#define OV_LAST_GLYPH  126
#define OV_GLYPHS      (OV_LAST_GLYPH - OV_FIRST_GLYPH + 1)   // 95
#define OV_COLS        16
#define OV_ROWS        6                                      // 96 cells, 95 used
#define OV_WHITE_CELL  (OV_COLS * OV_ROWS - 1)                // the spare one
#define OV_PAD         1                                      // slack around each glyph
#define OV_FONT_PX     16

struct OvGlyph { int x, y; };          // the cell's top left; all cells are one size
static OvGlyph s_glyph[OV_GLYPHS];
static int s_cellW = 0, s_cellH = 0;   // one cell, padding included
static int s_advance = 0;              // pen movement per character
static int s_lineH = 0;                // tmHeight: lines at this pitch do not gap
static int s_atlasW = 0, s_atlasH = 0;
static float s_whiteU = 0.0f, s_whiteV = 0.0f;

static ID3D11Device*             s_dev    = 0;
static ID3D11DeviceContext*      s_ctx    = 0;
static ID3D11VertexShader*       s_vs     = 0;
static ID3D11PixelShader*        s_ps     = 0;
static ID3D11InputLayout*        s_layout = 0;
static ID3D11Buffer*             s_vb     = 0;
static ID3D11Buffer*             s_cb     = 0;
static ID3D11BlendState*         s_blend  = 0;
static ID3D11BlendState*         s_blendAdd = 0;     // the panel's glows
static ID3D11DepthStencilState*  s_depth  = 0;
static ID3D11RasterizerState*    s_raster = 0;
static ID3D11RasterizerState*    s_rasterScissor = 0;  // the panel's hazard strips
static ID3D11SamplerState*       s_samp   = 0;
static ID3D11ShaderResourceView* s_srv    = 0;
static ID3D11RenderTargetView*   s_rtv    = 0;   // this frame's only; see overlay_begin
static int  s_built  = 0;
static int  s_failed = 0;
static UINT s_width = 0, s_height = 0;

static int overlay_fail(const char* why, HRESULT hr) {
  s_failed = 1;
  wt_log("overlay: %s (hr=0x%08lx); drawing nothing from here on", why, (unsigned long)hr);
  return 0;
}

static void rel(IUnknown* p) { if (p) p->Release(); }

// Everything the draw below sets, read back with the matching getters; the arrays are the API's sizes, so a game using
// every slot is restored as exactly as one using none.
#define OV_MAX_INSTANCES D3D11_SHADER_MAX_INTERFACES

struct OvSavedState {
  ID3D11RenderTargetView*  rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
  ID3D11DepthStencilView*  dsv;
  D3D11_VIEWPORT           vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
  UINT                     vpCount;
  D3D11_RECT               scissor[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
  UINT                     scissorCount;
  D3D11_PRIMITIVE_TOPOLOGY topology;
  ID3D11InputLayout*       layout;
  ID3D11Buffer*            vb;
  UINT                     vbStride, vbOffset;
  ID3D11Buffer*            ib;
  DXGI_FORMAT              ibFormat;
  UINT                     ibOffset;
  ID3D11VertexShader*      vs;  ID3D11ClassInstance* vsi[OV_MAX_INSTANCES]; UINT vsn;
  ID3D11PixelShader*       ps;  ID3D11ClassInstance* psi[OV_MAX_INSTANCES]; UINT psn;
  ID3D11GeometryShader*    gs;  ID3D11ClassInstance* gsi[OV_MAX_INSTANCES]; UINT gsn;
  ID3D11HullShader*        hs;  ID3D11ClassInstance* hsi[OV_MAX_INSTANCES]; UINT hsn;
  ID3D11DomainShader*      ds;  ID3D11ClassInstance* dsi[OV_MAX_INSTANCES]; UINT dsn;
  ID3D11Buffer*            vsCb;
  ID3D11Buffer*            psCb;
  ID3D11ShaderResourceView* srv;
  ID3D11SamplerState*      samp;
  ID3D11BlendState*        blend;  FLOAT blendFactor[4]; UINT blendMask;
  ID3D11DepthStencilState* depth;  UINT stencilRef;
  ID3D11RasterizerState*   raster;
};

// A file static rather than a local: 5 KB of class-instance array has no
// business on the game's render thread stack, and only one draw is ever in
// flight.
static OvSavedState s_saved;

static void state_save() {
  ZeroMemory(&s_saved, sizeof(s_saved));
  s_ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s_saved.rtv, &s_saved.dsv);
  s_saved.vpCount      = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  s_ctx->RSGetViewports(&s_saved.vpCount, s_saved.vp);
  s_saved.scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  s_ctx->RSGetScissorRects(&s_saved.scissorCount, s_saved.scissor);
  s_ctx->IAGetPrimitiveTopology(&s_saved.topology);
  s_ctx->IAGetInputLayout(&s_saved.layout);
  s_ctx->IAGetVertexBuffers(0, 1, &s_saved.vb, &s_saved.vbStride, &s_saved.vbOffset);
  s_ctx->IAGetIndexBuffer(&s_saved.ib, &s_saved.ibFormat, &s_saved.ibOffset);
  // Each count is how many the array can take on the way in and how many came
  // back on the way out; anything else and the runtime writes past the end.
  s_saved.vsn = OV_MAX_INSTANCES; s_ctx->VSGetShader(&s_saved.vs, s_saved.vsi, &s_saved.vsn);
  s_saved.psn = OV_MAX_INSTANCES; s_ctx->PSGetShader(&s_saved.ps, s_saved.psi, &s_saved.psn);
  s_saved.gsn = OV_MAX_INSTANCES; s_ctx->GSGetShader(&s_saved.gs, s_saved.gsi, &s_saved.gsn);
  s_saved.hsn = OV_MAX_INSTANCES; s_ctx->HSGetShader(&s_saved.hs, s_saved.hsi, &s_saved.hsn);
  s_saved.dsn = OV_MAX_INSTANCES; s_ctx->DSGetShader(&s_saved.ds, s_saved.dsi, &s_saved.dsn);
  s_ctx->VSGetConstantBuffers(0, 1, &s_saved.vsCb);
  s_ctx->PSGetConstantBuffers(0, 1, &s_saved.psCb);
  s_ctx->PSGetShaderResources(0, 1, &s_saved.srv);
  s_ctx->PSGetSamplers(0, 1, &s_saved.samp);
  s_ctx->OMGetBlendState(&s_saved.blend, s_saved.blendFactor, &s_saved.blendMask);
  s_ctx->OMGetDepthStencilState(&s_saved.depth, &s_saved.stencilRef);
  s_ctx->RSGetState(&s_saved.raster);
}

static void state_restore() {
  s_ctx->RSSetState(s_saved.raster);
  s_ctx->OMSetDepthStencilState(s_saved.depth, s_saved.stencilRef);
  s_ctx->OMSetBlendState(s_saved.blend, s_saved.blendFactor, s_saved.blendMask);
  s_ctx->PSSetSamplers(0, 1, &s_saved.samp);
  s_ctx->PSSetShaderResources(0, 1, &s_saved.srv);
  s_ctx->PSSetConstantBuffers(0, 1, &s_saved.psCb);
  s_ctx->VSSetConstantBuffers(0, 1, &s_saved.vsCb);
  s_ctx->DSSetShader(s_saved.ds, s_saved.dsn ? s_saved.dsi : 0, s_saved.dsn);
  s_ctx->HSSetShader(s_saved.hs, s_saved.hsn ? s_saved.hsi : 0, s_saved.hsn);
  s_ctx->GSSetShader(s_saved.gs, s_saved.gsn ? s_saved.gsi : 0, s_saved.gsn);
  s_ctx->PSSetShader(s_saved.ps, s_saved.psn ? s_saved.psi : 0, s_saved.psn);
  s_ctx->VSSetShader(s_saved.vs, s_saved.vsn ? s_saved.vsi : 0, s_saved.vsn);
  s_ctx->IASetIndexBuffer(s_saved.ib, s_saved.ibFormat, s_saved.ibOffset);
  s_ctx->IASetVertexBuffers(0, 1, &s_saved.vb, &s_saved.vbStride, &s_saved.vbOffset);
  s_ctx->IASetInputLayout(s_saved.layout);
  s_ctx->IASetPrimitiveTopology(s_saved.topology);
  s_ctx->RSSetScissorRects(s_saved.scissorCount, s_saved.scissor);
  s_ctx->RSSetViewports(s_saved.vpCount, s_saved.vp);
  s_ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, s_saved.rtv, s_saved.dsv);

  for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++) rel(s_saved.rtv[i]);
  rel(s_saved.dsv);
  rel(s_saved.layout);
  rel(s_saved.vb);
  rel(s_saved.ib);
  rel(s_saved.vs); for (UINT i = 0; i < s_saved.vsn; i++) rel(s_saved.vsi[i]);
  rel(s_saved.ps); for (UINT i = 0; i < s_saved.psn; i++) rel(s_saved.psi[i]);
  rel(s_saved.gs); for (UINT i = 0; i < s_saved.gsn; i++) rel(s_saved.gsi[i]);
  rel(s_saved.hs); for (UINT i = 0; i < s_saved.hsn; i++) rel(s_saved.hsi[i]);
  rel(s_saved.ds); for (UINT i = 0; i < s_saved.dsn; i++) rel(s_saved.dsi[i]);
  rel(s_saved.vsCb);
  rel(s_saved.psCb);
  rel(s_saved.srv);
  rel(s_saved.samp);
  rel(s_saved.blend);
  rel(s_saved.depth);
  rel(s_saved.raster);
  ZeroMemory(&s_saved, sizeof(s_saved));
}

// CreateFontA substitutes silently when a face is missing, so ask the DC which font we got after selecting it. Returns
// the font to delete, or 0 for a stock object (never delete).
static HFONT select_fixed_font(HDC dc) {
  static const char* const kNames[] = { "Consolas", "Courier New" };
  for (int i = 0; i < 2; i++) {
    HFONT f = CreateFontA(OV_FONT_PX, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                          FIXED_PITCH | FF_MODERN, kNames[i]);
    if (!f) continue;
    HGDIOBJ prev = SelectObject(dc, f);
    char got[LF_FACESIZE];
    got[0] = 0;
    if (GetTextFaceA(dc, LF_FACESIZE, got) && _stricmp(got, kNames[i]) == 0) {
      wt_log("overlay: font %s at %d px", got, OV_FONT_PX);
      return f;
    }
    SelectObject(dc, prev);
    DeleteObject(f);
  }
  HGDIOBJ sys = GetStockObject(SYSTEM_FIXED_FONT);
  if (sys) SelectObject(dc, sys);
  wt_log("overlay: neither Consolas nor Courier New is installed; using SYSTEM_FIXED_FONT");
  return 0;
}

// Draws 32..126 into a GDI DIB and returns R8G8B8A8 with coverage in alpha. No Direct2D or DirectWrite on purpose: they
// want a BGRA-capable device, which the game's may not be, and finding out is a black screen.
static unsigned char* build_atlas() {
  HDC dc = CreateCompatibleDC(0);
  if (!dc) { wt_log("overlay: CreateCompatibleDC failed"); return 0; }
  HFONT font = select_fixed_font(dc);

  TEXTMETRICA tm;
  if (!GetTextMetricsA(dc, &tm)) {
    wt_log("overlay: GetTextMetrics failed");
    if (font) DeleteObject(font);
    DeleteDC(dc);
    return 0;
  }
  s_advance = tm.tmAveCharWidth > 0 ? (int)tm.tmAveCharWidth : 8;
  const int widest = (int)tm.tmMaxCharWidth > s_advance ? (int)tm.tmMaxCharWidth : s_advance;
  s_lineH  = tm.tmHeight > 0 ? (int)tm.tmHeight : OV_FONT_PX;
  s_cellW  = widest + 2 * OV_PAD;
  s_cellH  = s_lineH + 2 * OV_PAD;
  s_atlasW = s_cellW * OV_COLS;
  s_atlasH = s_cellH * OV_ROWS;

  BITMAPINFO bi;
  ZeroMemory(&bi, sizeof(bi));
  bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth       = s_atlasW;
  bi.bmiHeader.biHeight      = -s_atlasH;          // negative: rows run top down
  bi.bmiHeader.biPlanes      = 1;
  bi.bmiHeader.biBitCount    = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = 0;
  HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, 0, 0);
  unsigned char* rgba = 0;
  if (!bmp || !bits) {
    wt_log("overlay: CreateDIBSection failed for a %dx%d atlas", s_atlasW, s_atlasH);
  } else {
    HGDIOBJ prevBmp = SelectObject(dc, bmp);
    memset(bits, 0, (size_t)s_atlasW * (size_t)s_atlasH * 4);
    // Transparent over a zeroed bitmap means the antialiased grey the font
    // rasteriser produces IS the coverage, in every colour byte.
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    for (int i = 0; i < OV_GLYPHS; i++) {
      const int col = i % OV_COLS, row = i / OV_COLS;
      s_glyph[i].x = col * s_cellW;
      s_glyph[i].y = row * s_cellH;
      const char ch = (char)(OV_FIRST_GLYPH + i);
      TextOutA(dc, s_glyph[i].x + OV_PAD, s_glyph[i].y + OV_PAD, &ch, 1);
    }
    GdiFlush();

    rgba = (unsigned char*)malloc((size_t)s_atlasW * (size_t)s_atlasH * 4);
    if (!rgba) {
      wt_log("overlay: out of memory for a %dx%d atlas", s_atlasW, s_atlasH);
    } else {
      const unsigned char* src = (const unsigned char*)bits;
      const int pixels = s_atlasW * s_atlasH;
      for (int i = 0; i < pixels; i++) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = src[i * 4];       // blue; all three carry the coverage
      }
      // The spare cell, opaque white: rectangles sample its centre, which is
      // what puts them in the same draw call as the text.
      const int wx = (OV_WHITE_CELL % OV_COLS) * s_cellW;
      const int wy = (OV_WHITE_CELL / OV_COLS) * s_cellH;
      for (int y = wy; y < wy + s_cellH; y++)
        for (int x = wx; x < wx + s_cellW; x++)
          rgba[(y * s_atlasW + x) * 4 + 3] = 255;
      s_whiteU = ((float)wx + (float)s_cellW * 0.5f) / (float)s_atlasW;
      s_whiteV = ((float)wy + (float)s_cellH * 0.5f) / (float)s_atlasH;
    }
    SelectObject(dc, prevBmp);
  }
  if (bmp) DeleteObject(bmp);
  if (font) DeleteObject(font);
  DeleteDC(dc);
  return rgba;
}

static int build_resources() {
  HRESULT hr = s_dev->CreateVertexShader(kOverlayVsBytecode, sizeof(kOverlayVsBytecode), 0, &s_vs);
  if (FAILED(hr)) return overlay_fail("CreateVertexShader failed", hr);
  hr = s_dev->CreatePixelShader(kOverlayPsBytecode, sizeof(kOverlayPsBytecode), 0, &s_ps);
  if (FAILED(hr)) return overlay_fail("CreatePixelShader failed", hr);

  const D3D11_INPUT_ELEMENT_DESC elems[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0,  8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
  };
  hr = s_dev->CreateInputLayout(elems, 3, kOverlayVsBytecode, sizeof(kOverlayVsBytecode), &s_layout);
  if (FAILED(hr)) return overlay_fail("CreateInputLayout failed", hr);

  D3D11_BUFFER_DESC bd;
  ZeroMemory(&bd, sizeof(bd));
  bd.ByteWidth      = sizeof(s_vert);
  bd.Usage          = D3D11_USAGE_DYNAMIC;
  bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = s_dev->CreateBuffer(&bd, 0, &s_vb);
  if (FAILED(hr)) return overlay_fail("CreateBuffer for the vertices failed", hr);

  ZeroMemory(&bd, sizeof(bd));
  bd.ByteWidth      = 16;                 // one float4; constant buffers come in 16s
  bd.Usage          = D3D11_USAGE_DYNAMIC;
  bd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  hr = s_dev->CreateBuffer(&bd, 0, &s_cb);
  if (FAILED(hr)) return overlay_fail("CreateBuffer for the viewport size failed", hr);

  D3D11_BLEND_DESC bl;
  ZeroMemory(&bl, sizeof(bl));
  bl.RenderTarget[0].BlendEnable           = TRUE;
  bl.RenderTarget[0].SrcBlend               = D3D11_BLEND_SRC_ALPHA;
  bl.RenderTarget[0].DestBlend              = D3D11_BLEND_INV_SRC_ALPHA;
  bl.RenderTarget[0].BlendOp                = D3D11_BLEND_OP_ADD;
  bl.RenderTarget[0].SrcBlendAlpha          = D3D11_BLEND_ONE;
  bl.RenderTarget[0].DestBlendAlpha         = D3D11_BLEND_INV_SRC_ALPHA;
  bl.RenderTarget[0].BlendOpAlpha           = D3D11_BLEND_OP_ADD;
  bl.RenderTarget[0].RenderTargetWriteMask  = D3D11_COLOR_WRITE_ENABLE_ALL;
  hr = s_dev->CreateBlendState(&bl, &s_blend);
  if (FAILED(hr)) return overlay_fail("CreateBlendState failed", hr);

// Additive, for canvas_glow_rect: the source adds to what is there, the only way a glow reads as light and not a pale
// film. Same factors as GL's glBlendFunc(GL_SRC_ALPHA, GL_ONE), alpha included, matching Megaton's glows.
  bl.RenderTarget[0].DestBlend      = D3D11_BLEND_ONE;
  bl.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_SRC_ALPHA;
  bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
  hr = s_dev->CreateBlendState(&bl, &s_blendAdd);
  if (FAILED(hr)) return overlay_fail("CreateBlendState for the additive pass failed", hr);

  // Depth off in both directions: the overlay goes on top of everything and
  // must leave nothing behind in the game's depth buffer.
  D3D11_DEPTH_STENCIL_DESC dsd;
  ZeroMemory(&dsd, sizeof(dsd));
  dsd.DepthEnable    = FALSE;
  dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
  dsd.DepthFunc      = D3D11_COMPARISON_ALWAYS;
  dsd.StencilEnable  = FALSE;
  hr = s_dev->CreateDepthStencilState(&dsd, &s_depth);
  if (FAILED(hr)) return overlay_fail("CreateDepthStencilState failed", hr);

  D3D11_RASTERIZER_DESC rd;
  ZeroMemory(&rd, sizeof(rd));
  rd.FillMode        = D3D11_FILL_SOLID;
  rd.CullMode        = D3D11_CULL_NONE;      // quad winding then cannot matter
  rd.DepthClipEnable = TRUE;
  rd.ScissorEnable   = FALSE;                // nothing of the game's can clip us
  hr = s_dev->CreateRasterizerState(&rd, &s_raster);
  if (FAILED(hr)) return overlay_fail("CreateRasterizerState failed", hr);

// The same state with clipping on, for canvas_hazard: its stripes are sheared parallelograms that run past the strip,
// and the scissor cuts them back. A second object because D3D11 fixes ScissorEnable at creation.
  rd.ScissorEnable = TRUE;
  hr = s_dev->CreateRasterizerState(&rd, &s_rasterScissor);
  if (FAILED(hr)) return overlay_fail("CreateRasterizerState with scissor failed", hr);

  D3D11_SAMPLER_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
  sd.MaxLOD         = D3D11_FLOAT32_MAX;
  hr = s_dev->CreateSamplerState(&sd, &s_samp);
  if (FAILED(hr)) return overlay_fail("CreateSamplerState failed", hr);

  unsigned char* pixels = build_atlas();
  if (!pixels) return overlay_fail("the font atlas could not be built", 0);

  D3D11_TEXTURE2D_DESC td;
  ZeroMemory(&td, sizeof(td));
  td.Width            = (UINT)s_atlasW;
  td.Height           = (UINT)s_atlasH;
  td.MipLevels        = 1;
  td.ArraySize        = 1;
  td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
  td.SampleDesc.Count = 1;
  td.Usage            = D3D11_USAGE_IMMUTABLE;
  td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA init;
  ZeroMemory(&init, sizeof(init));
  init.pSysMem     = pixels;
  init.SysMemPitch = (UINT)s_atlasW * 4;
  ID3D11Texture2D* tex = 0;
  hr = s_dev->CreateTexture2D(&td, &init, &tex);
  free(pixels);
  if (FAILED(hr) || !tex) return overlay_fail("CreateTexture2D for the atlas failed", hr);
  hr = s_dev->CreateShaderResourceView(tex, 0, &s_srv);
  tex->Release();
  if (FAILED(hr)) return overlay_fail("CreateShaderResourceView for the atlas failed", hr);

  s_built = 1;
  wt_log("overlay: ready. atlas %dx%d, cell %dx%d, advance %d, line %d",
         s_atlasW, s_atlasH, s_cellW, s_cellH, s_advance, s_lineH);
  return 1;
}

int overlay_begin(IDXGISwapChain* sc) {
  if (s_failed || !sc) return 0;

  if (!s_dev) {
    HRESULT hr = sc->GetDevice(__uuidof(ID3D11Device), (void**)&s_dev);
    if (FAILED(hr) || !s_dev) {
      s_dev = 0;
      return overlay_fail("the swap chain is not on a D3D11 device", hr);
    }
    s_dev->GetImmediateContext(&s_ctx);
    if (!s_ctx) return overlay_fail("the device has no immediate context", 0);
    if (s_dev->GetFeatureLevel() < D3D_FEATURE_LEVEL_10_0)
      return overlay_fail("the device is below feature level 10_0, which the shaders need", 0);
    // The in-world layer draws on the same device, at a different seam. It owns
    // its own pipeline objects and creates them lazily on first use.
    world3d_set_device(s_dev, s_ctx);
  } else {
// Every resource belongs to the first device seen. A second swap chain on a second device would quietly draw nothing,
// so that frame is skipped, not failed; the game's own swap chain keeps working.
    ID3D11Device* dev = 0;
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev)) || !dev) return 0;
    const int same = (dev == s_dev);
    dev->Release();
    if (!same) return 0;
  }
  if (!s_built && !build_resources()) return 0;

  DXGI_SWAP_CHAIN_DESC scd;
  HRESULT hr = sc->GetDesc(&scd);
  if (FAILED(hr)) return overlay_fail("GetDesc on the swap chain failed", hr);
  if (scd.BufferDesc.Width != s_width || scd.BufferDesc.Height != s_height) {
    wt_log("overlay: back buffer is now %ux%u", scd.BufferDesc.Width, scd.BufferDesc.Height);
    s_width  = scd.BufferDesc.Width;
    s_height = scd.BufferDesc.Height;
  }
  if (s_width == 0 || s_height == 0) return 0;

// The view is built and dropped inside the frame: holding one across frames keeps a reference on the back buffer, and
// the game's next ResizeBuffers (our borderless resize provokes one within five seconds) would fail and take the
// picture with it.
  ID3D11Texture2D* back = 0;
  hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
  if (FAILED(hr) || !back) return overlay_fail("GetBuffer(0) on the swap chain failed", hr);
  hr = s_dev->CreateRenderTargetView(back, 0, &s_rtv);
  back->Release();
  if (FAILED(hr) || !s_rtv) {
    s_rtv = 0;
    return overlay_fail("CreateRenderTargetView on the back buffer failed", hr);
  }

  s_vertCount   = 0;
  s_batchCount  = 0;
  s_batchLost   = 0;
  s_curSrv      = 0;
  s_curAdditive = 0;
  s_curScissor  = 0;
  return 1;
}

// 0xRRGGBBAA to the byte order R8G8B8A8_UNORM reads out of memory.
static unsigned pack_color(unsigned rgba) {
  return ((rgba & 0xFFu) << 24) | (((rgba >> 8) & 0xFFu) << 16)
       | (((rgba >> 16) & 0xFFu) << 8) | ((rgba >> 24) & 0xFFu);
}

// The batch `need` more vertices belong in: the open one when its three pieces of state still match, else a new one.
// Null means no room, dropping the geometry rather than splitting a triangle across batches or overrunning the array.
static OvBatch* batch_reserve(int need) {
  if (s_vertCount + need > OV_MAX_VERTS) { s_batchLost = 1; return 0; }
  if (s_batchCount > 0) {
    OvBatch* b = &s_batch[s_batchCount - 1];
    const int sameRect = !s_curScissor ||
      (b->rect.left == s_curRect.left && b->rect.top == s_curRect.top &&
       b->rect.right == s_curRect.right && b->rect.bottom == s_curRect.bottom);
    if (b->srv == s_curSrv && b->additive == s_curAdditive &&
        b->scissor == s_curScissor && sameRect) return b;
  }
  if (s_batchCount >= OV_MAX_BATCHES) { s_batchLost = 1; return 0; }
  OvBatch* b = &s_batch[s_batchCount++];
  b->srv      = s_curSrv;
  b->additive = s_curAdditive;
  b->scissor  = s_curScissor;
  b->rect     = s_curRect;
  b->first    = s_vertCount;
  b->count    = 0;
  return b;
}

static void push_quad(float x0, float y0, float x1, float y1,
                      float u0, float v0, float u1, float v1, unsigned rgba) {
  OvBatch* b = batch_reserve(6);
  if (!b) return;
  const unsigned c = pack_color(rgba);
  OvVertex* v = &s_vert[s_vertCount];
  v[0].x = x0; v[0].y = y0; v[0].u = u0; v[0].v = v0; v[0].color = c;
  v[1].x = x1; v[1].y = y0; v[1].u = u1; v[1].v = v0; v[1].color = c;
  v[2].x = x0; v[2].y = y1; v[2].u = u0; v[2].v = v1; v[2].color = c;
  v[3].x = x1; v[3].y = y0; v[3].u = u1; v[3].v = v0; v[3].color = c;
  v[4].x = x1; v[4].y = y1; v[4].u = u1; v[4].v = v1; v[4].color = c;
  v[5].x = x0; v[5].y = y1; v[5].u = u0; v[5].v = v1; v[5].color = c;
  s_vertCount += 6;
  b->count    += 6;
}

int ov_frame_open() { return s_rtv != 0; }
ID3D11Device* ov_device() { return s_dev; }

void ov_size(int* w, int* h) {
  if (w) *w = (int)s_width;
  if (h) *h = (int)s_height;
}

void ov_set_texture(ID3D11ShaderResourceView* srv) { s_curSrv = srv; }
void ov_set_additive(int on) { s_curAdditive = on ? 1 : 0; }

void ov_set_scissor(int x, int y, int w, int h) {
  s_curRect.left   = (LONG)x;
  s_curRect.top    = (LONG)y;
  s_curRect.right  = (LONG)(x + w);
  s_curRect.bottom = (LONG)(y + h);
  s_curScissor     = 1;
}

void ov_clear_scissor() { s_curScissor = 0; }

void ov_tri(const OvPoint& a, const OvPoint& b, const OvPoint& c) {
  if (!s_rtv) return;
  OvBatch* bt = batch_reserve(3);
  if (!bt) return;
  OvVertex* v = &s_vert[s_vertCount];
  v[0].x = a.x; v[0].y = a.y; v[0].u = a.u; v[0].v = a.v; v[0].color = pack_color(a.rgba);
  v[1].x = b.x; v[1].y = b.y; v[1].u = b.u; v[1].v = b.v; v[1].color = pack_color(b.rgba);
  v[2].x = c.x; v[2].y = c.y; v[2].u = c.u; v[2].v = c.v; v[2].color = pack_color(c.rgba);
  s_vertCount += 3;
  bt->count   += 3;
}

void overlay_rect(int x, int y, int w, int h, unsigned rgba) {
  if (!s_rtv || w <= 0 || h <= 0) return;
  push_quad((float)x, (float)y, (float)(x + w), (float)(y + h),
            s_whiteU, s_whiteV, s_whiteU, s_whiteV, rgba);
}

void overlay_text(int x, int y, unsigned rgba, const char* s) {
  if (!s_rtv || !s) return;
  const float du = (float)s_cellW / (float)s_atlasW;
  const float dv = (float)s_cellH / (float)s_atlasH;
  int pen = x;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++, pen += s_advance) {
    if (*p <= OV_FIRST_GLYPH || *p > OV_LAST_GLYPH) continue;   // space and the unprintable
    const OvGlyph* g = &s_glyph[*p - OV_FIRST_GLYPH];
    const float u0 = (float)g->x / (float)s_atlasW;
    const float v0 = (float)g->y / (float)s_atlasH;
    // The glyph sits one pixel into its cell, so the quad starts a pixel up
    // and left of the pen and the glyph itself lands exactly on it.
    push_quad((float)(pen - OV_PAD), (float)(y - OV_PAD),
              (float)(pen - OV_PAD + s_cellW), (float)(y - OV_PAD + s_cellH),
              u0, v0, u0 + du, v0 + dv, rgba);
  }
}

int overlay_text_width(const char* s) { return s ? (int)strlen(s) * s_advance : 0; }
int overlay_text_height() { return s_lineH; }
int overlay_width()  { return (int)s_width; }
int overlay_height() { return (int)s_height; }

// The first drawn frame only, one line per call site: wt_log closes the file each line, so the last line in the log is
// the call the frame did not return from, the only way to place a hang without a debugger.
static int s_traceFirst = 1;
#define OV_STEP(what) do { if (s_traceFirst) wt_log("overlay: first frame, %s", what); } while (0)

int overlay_verts_left() {
  const int left = OV_MAX_VERTS - s_vertCount;
  return left > 0 ? left : 0;
}

void overlay_end() {
  if (!s_rtv) return;
  OV_STEP("overlay_end entered");
  D3D11_MAPPED_SUBRESOURCE m;
  if (s_vertCount > 0 &&
      SUCCEEDED(s_ctx->Map(s_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
    memcpy(m.pData, s_vert, (size_t)s_vertCount * sizeof(OvVertex));
    s_ctx->Unmap(s_vb, 0);
    OV_STEP("the vertices are in");
    if (SUCCEEDED(s_ctx->Map(s_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
      float* view = (float*)m.pData;
      view[0] = (float)s_width;
      view[1] = (float)s_height;
      view[2] = 0.0f;
      view[3] = 0.0f;
      s_ctx->Unmap(s_cb, 0);

      OV_STEP("about to save the game's state");
      state_save();
      OV_STEP("state saved, setting the pipeline");
      const UINT  stride = sizeof(OvVertex), offset = 0;
      const FLOAT factor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
      D3D11_VIEWPORT vpd;
      vpd.TopLeftX = 0.0f;
      vpd.TopLeftY = 0.0f;
      vpd.Width    = (float)s_width;
      vpd.Height   = (float)s_height;
      vpd.MinDepth = 0.0f;
      vpd.MaxDepth = 1.0f;
      s_ctx->OMSetRenderTargets(1, &s_rtv, 0);
      s_ctx->RSSetViewports(1, &vpd);
      s_ctx->OMSetDepthStencilState(s_depth, 0);
      s_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      s_ctx->IASetInputLayout(s_layout);
      s_ctx->IASetVertexBuffers(0, 1, &s_vb, &stride, &offset);
      s_ctx->IASetIndexBuffer(0, DXGI_FORMAT_UNKNOWN, 0);
      s_ctx->VSSetShader(s_vs, 0, 0);
      s_ctx->VSSetConstantBuffers(0, 1, &s_cb);
      s_ctx->PSSetShader(s_ps, 0, 0);
      s_ctx->PSSetSamplers(0, 1, &s_samp);
      // Anything left on the rest of the path would run over our vertices; the
      // saved copies go back in state_restore.
      s_ctx->GSSetShader(0, 0, 0);
      s_ctx->HSSetShader(0, 0, 0);
      s_ctx->DSSetShader(0, 0, 0);
// One Draw per run sharing an atlas, a blend and a clip box. State is set unconditionally, not tracked across the loop:
// redundant calls cost nothing next to getting the tracking wrong.
      if (s_traceFirst) wt_log("overlay: first frame, %d vertices in %d batches, drawing",
                               s_vertCount, s_batchCount);
      for (int i = 0; i < s_batchCount; i++) {
        const OvBatch& b = s_batch[i];
        if (b.count <= 0) continue;
        ID3D11ShaderResourceView* srv = b.srv ? b.srv : s_srv;
        s_ctx->PSSetShaderResources(0, 1, &srv);
        s_ctx->OMSetBlendState(b.additive ? s_blendAdd : s_blend, factor, 0xFFFFFFFF);
        if (b.scissor) {
          s_ctx->RSSetScissorRects(1, &b.rect);
          s_ctx->RSSetState(s_rasterScissor);
        } else {
          s_ctx->RSSetState(s_raster);
        }
        s_ctx->Draw((UINT)b.count, (UINT)b.first);
      }
      OV_STEP("drawn, putting the game's state back");
      state_restore();
      OV_STEP("state restored");
    }
  }
// Once per frame at most while it keeps happening: a frame short of room drops geometry (a missing corner, not a crash)
// but must not be silent.
  if (s_batchLost) {
    static int said = 0;
    if (said < 8) {
      said++;
      wt_log("overlay: out of room at %d vertices in %d batches; some geometry was dropped",
             s_vertCount, s_batchCount);
    }
  }
  s_rtv->Release();
  s_rtv = 0;
  s_vertCount  = 0;
  s_batchCount = 0;
  s_traceFirst = 0;
}

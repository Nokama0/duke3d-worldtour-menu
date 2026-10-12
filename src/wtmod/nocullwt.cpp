// The DLL half of "Disable culling": a call-site redirect and three function hooks. The only game data written is
// per-wall scratch (one byte) and a queued item's own float copy. See nocullwt.h and
// docs/re/free-camera-wall-exemption.md.
#include <windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <intrin.h>
#include "project.h"
#include "nocullwt.h"
#include "nocull.h"
#include "esp_world.h"
#include "freecam.h"
#include "config.h"
#include "hook.h"

static const unsigned kRvaSeedCall   = 0x149DEF;   // the CALL at 0x007f9def
static const unsigned kRvaSeedSect   = 0x149DE5;
static const unsigned kRvaSeedThis   = 0x149DEC;
static const unsigned kRvaScanSector = 0x14A1B0;   // FUN_007fa1b0, the call's target
static const unsigned kRvaGlobalCurSec = 0xE07160;
static const unsigned kRvaNumSectors = 0xEC0B60;   // int16
static const unsigned kRvaSectors    = 0xD4C230;   // sector[0], stride 0x28
static const unsigned kRvaWallBuild  = 0x148D60;   // FUN_007f8d60, the per-wall record builder
// FUN_007fac80 decides a wall's whole fate (not just "draw"): push ebp / mov ebp,esp / and esp,-16 is six bytes, with
// sub esp,0xe8 behind it checked too. __thiscall, two stack arguments (RET 8), wall record at [ebp+8].
static const unsigned kRvaWallDraw   = 0x14AC80;
static const unsigned char kFpWallDraw[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0 };
static const unsigned kWallDrawPrologue = 6;
static const unsigned char kFpWallDrawTail[6] = { 0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00 };
// The byte at 0x017c39b8 gates both the portal recursion at 0x007fad05 and the whole mesh-record section from
// 0x007fae25.
static const unsigned kRvaTrue3DGate = 0x11139B8;
// Wall record fields FUN_007fac80 reads first: a negative +0x70 skips past the mesh branch; +0x68 selects which pair of
// endpoints the projection uses.
#define WALLREC_NEXT   0x70
#define WALLREC_TWOSID 0x68
static const unsigned kRvaGlobalAngN = 0xE07164;   // int16, the camera angle we substitute
static const unsigned kRvaGlobalPosX = 0xE07180;   // int, the three the matrix reads
static const unsigned kRvaGlobalPosY = 0xE07174;
static const unsigned kRvaGlobalPosZ = 0xE07170;
// FUN_007fa640 queues a sector's FLOOR and CEILING: it calls FUN_007fa7b0 per element, then appends a 16-byte record to
// the scene's draw vector at +0x284 if flags+0x11 / flags+0x10 is set. Those bytes are the whole floor/ceiling
// visibility decision.
static const unsigned kRvaSectorPlanes = 0x14A640;
static const unsigned char kFpSectorPlanes[5] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
static const unsigned kSectorPlanesPrologue = 5;

static const unsigned kRvaWallClip   = 0x1495D0;   // FUN_007f95d0, the wall frustum test
static const unsigned char kFpWallClip[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x18 };
static const unsigned kWallClipPrologue = 6;
static const unsigned char kFpWallClipTail[6] = { 0x56, 0x8B, 0x75, 0x08, 0x8B, 0xC1 };
static const unsigned kRvaSurfClip   = 0x149740;   // FUN_007f9740, the floor/ceiling test
// FALLBACK PATH: FUN_007fb760 takes a wall with no True3D mesh entry (`type = (param_2 != 0) + 4`, and a second type-4
// item for param_2 == 2). __thiscall, twenty dwords of stack argument, RET 0x50: a hook forwarding the wrong arity
// corrupts the stack.
static const unsigned kRvaFallback = 0x14B760;   // FUN_007fb760
// FUN_007ba740, the ONLY append site for the renderer's "normal" list (every mesh-path surface): __thiscall, `this` in
// ECX (LEA ECX,[EDI+0x35c]), three-int record at [ebp+8] ([0] sector, [1] wall, [2] type 0..5), RET 4.
static const unsigned kRvaMeshAppend = 0x10A740;
static const unsigned char kFpMeshAppend[5] = { 0x55, 0x8B, 0xEC, 0x56, 0x57 };
static const unsigned kMeshAppendPrologue = 5;
static const unsigned char kFpMeshAppendTail[6] = { 0x8B, 0xF9, 0x8B, 0x4D, 0x08, 0x8B };
// FUN_007ba740 is a DRAIN: its only caller FUN_007ba500 loops over a std::vector (begin/end/capacity at
// this+0x6c8/+0x6cc/+0x6d0, 0x10-byte records).

static const unsigned kRvaNodeAlloc = 0x164460;
static const unsigned char kFpNodeAlloc[5] = { 0x8B, 0xD1, 0x8B, 0x42, 0x14 };
static const unsigned kNodeAllocPrologue = 5;

static const unsigned kRvaDrawWrap = 0x163520;
static const unsigned char kFpDrawWrap[6] = { 0x55, 0x8B, 0xEC, 0x8B, 0x55, 0x08 };
static const unsigned kDrawWrapPrologue = 6;
static const unsigned char kFpDrawWrapTail[6] = { 0x85, 0xD2, 0x7E, 0x38, 0x83, 0x79 };

static const unsigned kRvaSpanInsert = 0x163870;     // FUN_00813870, the LEAF
static const unsigned char kFpSpanInsert[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14 };
static const unsigned kSpanInsertPrologue = 6;

// FUN_00813d00 is the INTERIOR span-tree node (97.5% of the traffic): node[1]/[2] children, [3]/[4] x range, [5]/[6]
// depth range, [7] payload. Its discard is the leaf's predicate inverted: quad gone when quad[0] >= node[3] && quad[1]
// <= node[4] && quad[2] <= node[5].
static const unsigned kRvaSpanNode = 0x163D00;       // FUN_00813d00
static const unsigned char kFpSpanNode[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C };
static const unsigned kSpanNodePrologue = 6;

// FUN_007fb570 is the refusal that loses the mesh walls: FUN_007fb490 runs it before appending a record, and it asks
// the span tree (FUN_00813ea0), the occlusion oracle for the mesh population too
// (docs/re/horizon-occlusion-override.md). stdcall, one stack arg, verdict in AL, RET 4.
static const unsigned kRvaOcclude    = 0x14B570;
static const unsigned char kFpOcclude[6] = { 0x53, 0x8B, 0xDC, 0x83, 0xEC, 0x08 };
static const unsigned kOccludePrologue = 6;
static const unsigned char kFpOccludeTail[6] = { 0x83, 0xE4, 0xF8, 0x83, 0xC4, 0x04 };
// The queue: FUN_007fbe60 appends, FUN_007fb310 dispatches one item, FUN_0080e500 is the emit the type 4/5 branch
// reaches only past a z threshold.
static const unsigned kRvaQueueAdd   = 0x14BE60;   // FUN_007fbe60
static const unsigned kRvaDrainItem  = 0x14B310;   // FUN_007fb310
static const unsigned kRvaEmitQuad   = 0x15E500;   // FUN_0080e500
// FUN_007fb310 drops a type 4/5 quad unless one of its corner pairs sits at or above this float on the scene object.
#define SCENE_ZCLIP_OFF 0x254

// Per-wall cache sub-entries (FUN_007ba740's dispatch, 0x007ba7a0-0x007ba7ee): type 2 uses +0, type 3 +8 (falling back
// to +0), type 4 +0x10, type 5 can append all three.
#define CACHE_WALLS_OFF  0x10
#define CACHE_ENTRY      0x18
#define CACHE_SUB_MID    0x00
#define CACHE_SUB_STEP   0x08
#define CACHE_SUB_HEADER 0x10

// FUN_00813520, the draw wrapper: push ebp; mov ebp,esp; mov edx,[ebp+8] (six bytes, 8-aligned); the test edx,edx and
// cmp behind it checked.

// FUN_0077ee10, the render-mode object's "normal" list appender (called from FUN_0078bf00 at 0x78BFE2): push ebp; mov
// ebp,esp; push ecx; push esi (five bytes, 8-aligned). Bare RET, object in ECX.

static const unsigned kRvaMeshCache  = 0xEE2918;   // DAT_01592918, one pointer a sector
static const unsigned kRvaNumWalls   = 0xD46A20;   // DAT_013F6A20, int16
static const unsigned kRvaWalls      = 0xD84230;   // wall[0], stride 0x20
// The renderer object, for the matrix it uploads and the viewport it draws into (docs/re/engine-facts.md 4.5). R is a
// POINTER; the world-to-clip matrix is 16 floats at R+0x178 and the viewport {x,y,w,h} ints at R+0x244.
static const unsigned kRvaRenderer   = 0xEDEF28;
#define RENDER_MATRIX_OFF   0x178
// THE WALL PATH'S OWN MATRIX: in FUN_007fac80 at 0x7fad2f the third push is scene+0x1EC, landing at [EBP+0x0C], which
// FUN_007fab60 passes as EDX to FUN_007faa20. Walls project with scene+0x1EC, the picture with R+0x178.
#define SCENE_MATRIX_OFF    0x1EC
#define RENDER_VIEWPORT_OFF 0x244
// Enhanced (True3D) is DAT_00e324ae == 0 && DAT_00984048 == 1, the expression FUN_00752060 reports to the UI as
// enhancedRenderMode.
static const unsigned kRvaModeSel    = 0x2D4048;
static const unsigned kRvaModeGate   = 0x7324AE;

#define SEC_STRIDE   0x28
#define SEC_WALLPTR  0x00   // int16
#define SEC_WALLNUM  0x02   // int16
#define SEC_CEILINGZ 0x04   // int32
#define SEC_FLOORZ   0x08   // int32
#define SEC_CEILSTAT 0x0C   // int16, bit 0 is parallax (sky)
#define SEC_FLOORSTAT 0x0E  // int16, bit 0 is parallax
#define WALL_STRIDE      0x20
#define WALL_NEXTSECTOR  0x0C   // int16, -1 when the wall is one-sided
#define WALL_CSTAT       0x0E   // int16

// Masked-wall record vector on the scene object: begin +0x28c, end +0x290, 0x10 an element (copy loop at 0x007f9e7e).
#define SCENE_MASKVEC_BEGIN  0x28C
#define SCENE_MASKVEC_END    0x290

// Floor/ceiling draw-item vector (FUN_007fa640 at 0x007fa6bd): begin +0x280, end +4, capacity +8, 16 bytes each.
#define SCENE_DRAWVEC_BEGIN  0x280

// Scene sprite index vector, 4 bytes an element: begin +0x2a4, end +0x2a8 (FUN_007f9c80 at 0x007f9e17); unlike
// spritesortcnt it IS filled during the walk.
#define SCENE_SPRITEVEC_BEGIN  0x2A4

// The wall record's two corner-score vectors, one per plane (FUN_007f8d60's FUN_007f9230 calls, record + 0x1e and +
// 0x22).
#define WALLREC_SCOREA_BEGIN  0x7C
#define WALLREC_SCOREA_END    0x80
#define WALLREC_SCOREB_BEGIN  0x8C

// Their source points: {x, y} qword at record+0x08 with z at +0x10 (vector A), {x, y} at +0x14 with z at +0x1C (vector
// B).
#define WALLREC_PTA  0x08
#define WALLREC_ZA   0x10
#define WALLREC_ZB   0x1C
#define WALLREC_PTB  0x14
#define WALLREC_SECT 0x00   // int
#define WALLREC_WALL 0x04   // int
// FUN_007f8d60's kept projections: param_1[0x27] nearer endpoint, param_1[0x28] farther.
#define WALLREC_TNEAR 0x9C
#define WALLREC_TFAR  0xA0

// E8 rel32: relative, so the same at any load address; 0x007f9def + 5 + 0x3BC = 0x007fa1b0.
static const unsigned char kFpSeedCall[5] = { 0xE8, 0xBC, 0x03, 0x00, 0x00 };
// MOVSX EAX, word ptr [globalcursectnum]. The last four bytes are an ABSOLUTE address ASLR relocates, so only the
// opcode is fingerprinted and the operand is checked against the base separately.
static const unsigned char kFpSeedSectOp[3] = { 0x0F, 0xBF, 0x05 };
static const unsigned char kFpSeedThis[3] = { 0x8B, 0xCB, 0x50 };
static const unsigned char kFpScanSector[5] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };
// FUN_007f8d60: push ebp; mov ebp,esp; and esp,-8 (six bytes, 8-aligned so hook.cpp patches atomically); sub esp,0x74 /
// mov edx,[ebp+8] behind it checked.
static const unsigned char kFpWallBuild[6] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8 };
static const unsigned kWallBuildPrologue = 6;
static const unsigned char kFpWallBuildTail[6] = { 0x83, 0xEC, 0x74, 0x8B, 0x55, 0x08 };
// FUN_007fac80: push ebp; mov ebp,esp; and esp,-16 (six bytes, 8-aligned), sub esp,0xE8 checked; hooked only to COUNT.
// FUN_007f95d0: push ebp; mov ebp,esp; sub esp,0x18. FUN_007f9740: push ebp; mov ebp,esp; push ecx; push ebx.
static const unsigned char kFpSurfClip[5] = { 0x55, 0x8B, 0xEC, 0x51, 0x53 };
static const unsigned kSurfClipPrologue = 5;
static const unsigned char kFpSurfClipTail[5] = { 0x8A, 0x5D, 0x0C, 0x56, 0x8B };
// FUN_007fb570: push ebx; mov ebx,esp; sub esp,8 (and esp,-8 / add esp,4 behind it checked). FUN_007fbe60: sub
// esp,0x54, RET 0x14. FUN_007fb310: push ebx; mov ebx,esp; sub esp,8, RET 8. FUN_0080e500: and esp,-8, RET 8.

// __thiscall, three stack arguments, RET 0xC: a five-parameter __fastcall on MSVC x86 (this in ECX, an unused EDX,
// three pushed).
typedef void (__fastcall* ScanSectorFn)(void* scene, void* edx, int sect, void* a2, void* a3);

// FUN_007f8d60: __thiscall, six stack arguments, RET 0x18: an eight-parameter __fastcall (record in ECX, unused EDX,
// six pushed).
typedef void (__fastcall* WallBuildFn)(void* rec, void* edx, int sect, int wallIdx,
                                       const void* planes, const void* cam, int ang,
                                       const void* dir);
#define WALLREC_SKIP 0x74   // the builder's "skip this wall" byte

typedef void (__fastcall* WallDrawFn)(void* scene, void* edx, void* rec, void* a3);

// FUN_007f95d0: __thiscall, two stack arguments, RET 0x8.
typedef void (__fastcall* WallClipFn)(void* vec, void* edx, const void* wall, const void* next);
typedef int (__stdcall* SurfClipFn)(const void* recVec, int which);
// FUN_007fb570: __stdcall, one stack argument, RET 4, answer in AL (nonzero = occluded). char, not int: the caller does
// TEST AL,AL.
typedef char (__stdcall* OccludeFn)(void* item);
typedef void (__fastcall* QueueAddFn)(void* q, void* edx, const int* type, void* a, void* b,
                                      void* c, void* d);
typedef void (__fastcall* DrainItemFn)(void* scene, void* edx, void* item, void* a3);
typedef int  (__fastcall* EmitQuadFn)(void* target, void* edx, void* a, void* b);

static const unsigned char* s_base = 0;
static unsigned s_imageSize = 0;
static int s_ok = 0;
// Volatile: hook_patch_call redirects the site before this is necessarily visible to the render thread.
static ScanSectorFn volatile s_orig = 0;
static WallBuildFn volatile s_origBuild = 0;
static int s_buildOk = 0;
// Set for exactly one swept drawrooms and cleared in a __finally, so no path out of the engine's body leaves it on;
// only the render thread reads it.
static int s_armed = 0;
static const short* s_pNumSectors = 0;
static const unsigned char* s_pSectors = 0;
static const int* s_pPosX = 0;
static const int* s_pPosY = 0;
static const int* s_pPosZ = 0;

#define NOCULL_PLANES_MAX 8
static const unsigned* s_pCache = 0;
static const short* s_pNumWalls = 0;
static const int* s_pModeSel = 0;
static const unsigned char* s_pModeGate = 0;
static const unsigned char* s_pWalls = 0;
static void* const* s_pRenderer = 0;

// nocullsweep (default 1) offers every sector to the engine's emit body, so the whole map draws. nocullwalls (default
// 0) also clears each wall record's +0x74 skip byte (Build's one-sided wall test).
static int s_sweep = 1;
static int s_keepOneSided = 1;   // nocullwalls=1 clears this
static const short* s_pCurSec = 0;
static unsigned s_frameId = 0;   // bumped where the bracket arms; 0 is "never"
static int s_keepBehind   = 1;   // nocullbehind=1 clears this
static int s_keepFar      = 1;   // nocullfar=1 suppresses it; it cuts nothing on screen
static int s_dropCeilings = 1;
static int s_spanInsertOk = 0;
static int s_spanNodeOk = 0;
#define NOCULL_SPAN_SAMPLES 4
static int s_wallOcc = 0;   // nocullwallocclusion; the exemption described below
// DISTINCT WALLS PER NODE: a node holds ONE payload, so a quad taking it over replaces the wall there; a takeover is n7
// changing to this quad's payload across the call.
#define TAKE_SLOTS 8192

static int s_hideMesh = 0;
static bool wall_has_mesh(int wall);

#define SPAN_BATCHER_SLOTS 8
static int s_wallOccTotal = 0;
// nocullwallocclusion: 1 SUBDIVIDE (draws pillars), 2 the let-through nudge, 3 TOTAL (default, faster than 2: 170 fps
// against about 100).
#define WALLOCC_SUBDIVIDE 1
#define WALLOCC_LETTHRU   2
#define WALLOCC_TOTAL     3
static int s_wallOccMode = WALLOCC_TOTAL;
static int s_clampSpan = 1;   // nocullclampspan, on by default

static short s_wallSector[NOCULLWT_MAX_WALLS];

static const unsigned kRvaPlayers = 0x5FD5C0;   // player[0], stride 0x408
#define P_STRIDE_N  0x408
#define P_POSX_N    0x214
#define P_POSY_N    0x218
#define P_ANG_N     0x2CC
// Who emitted each sector, read by the wall builder: a sector the portal walk claimed is emitted inside the walk with
// its clip state, one the sweep emits is emitted cold.
static int s_inSweep = 0;
static unsigned char s_sectorBy[NOCULLWT_MAX_SECTORS];   // 0 none, 1 walk, 2 sweep
static int s_camSectBy = 0;

#define SPANCLAMP_NOTCALLED 0
#define SPANCLAMP_PTRBAD    1
#define SPANCLAMP_FINITE    2
#define SPANCLAMP_WROTE     3

// Draw wrapper FUN_00813520, hooked as an ordinary engine function (device context from DAT_0158efa0+0xb398, Draw via
// +0x34, DrawIndexed via +0x30). __thiscall, two stack args, RET 8.
typedef void (__fastcall* DrawWrapFn)(void* batch, void* edx, int count, int start);

typedef void (__fastcall* NormalAppendFn)(void* self, void* edx);

// RenderDoc: the walls arrive as 308 DrawIndexed calls of 6 indices; the hook tallies callers by return address, apart
// from the 648-index sky draws.
#define DRAWCHAIN_DEPTH 4
#define DRAWSITE_MAX 32
struct DrawSite {
  unsigned rva[DRAWCHAIN_DEPTH];
  int depth, six, calls, indices, minIdx, maxIdx;
};
static int s_cPoisoned = 0;
static SurfClipFn volatile s_origSurf = 0;
static const void* s_planes = 0;

static int s_loggedFirst = 0;
static int s_loggedBadCount = 0;
static int s_loggedBadVec = 0;
static int s_disabled = 0;      // a failed read turns the sweep off for the session

static const void* engine_ptr(unsigned rva, unsigned size) {
  if (!s_base || rva > s_imageSize || size > s_imageSize - rva) {
    wt_log("nocull: rva 0x%X (%u bytes) is outside the image", rva, size);
    return 0;
  }
  return s_base + rva;
}

// The scene object is on the heap, so ptr_plausible is the cheap per-wall check (heap_readable is a VirtualQuery: three
// per wall took the diagnostic build from 105 fps to 17).
static bool ptr_plausible(const void* p) {
  return p != 0 && ((UINT_PTR)p & 3u) == 0;
}

static bool heap_readable(const void* p, unsigned size) {
  if (!p) return false;
  MEMORY_BASIC_INFORMATION mbi;
  if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
  if (mbi.State != MEM_COMMIT) return false;
  const DWORD prot = mbi.Protect & 0xFF;
  if ((mbi.Protect & PAGE_GUARD) != 0) return false;
  if (prot != PAGE_READONLY && prot != PAGE_READWRITE && prot != PAGE_WRITECOPY
      && prot != PAGE_EXECUTE_READ && prot != PAGE_EXECUTE_READWRITE
      && prot != PAGE_EXECUTE_WRITECOPY)
    return false;
  const unsigned char* start = (const unsigned char*)mbi.BaseAddress;
  const SIZE_T used = (SIZE_T)((const unsigned char*)p - start);
  return used < mbi.RegionSize && (SIZE_T)size <= mbi.RegionSize - used;
}



// FUN_007f95d0, the six-plane test. Counting only.
typedef void (__fastcall* WallClipFn)(void* vec, void* edx, const void* wall, const void* next);

typedef void (__fastcall* SectorPlanesFn)(void*, void*, int, void*, void*, void*);

static void __fastcall wt_wall_build(void* rec, void* edx, int sect, int wallIdx,
                                     const void* planes, const void* cam, int ang,
                                     const void* dir) {
  (void)edx;
  WallBuildFn orig = s_origBuild;
  while (!orig) { YieldProcessor(); orig = s_origBuild; }
  orig(rec, 0, sect, wallIdx, planes, cam, ang, dir);
  if (!s_armed || !rec) return;
  s_planes = planes;   // the plane vector the engine itself handed the builder
  if (wallIdx >= 0 && wallIdx < NOCULLWT_MAX_WALLS && sect >= 0 && sect < NOCULLWT_MAX_SECTORS)
    s_wallSector[wallIdx] = (short)sect;
  if (sect >= 0 && sect < NOCULLWT_MAX_SECTORS && s_sectorBy[sect] == 0) {
    s_sectorBy[sect] = (unsigned char)(s_inSweep ? 2 : 1);
    if (s_pCurSec && sect == (int)*s_pCurSec) s_camSectBy = s_inSweep ? 2 : 1;
  }
  // Capture the pair BEFORE the write (every later test uses these locals); the function has four return paths, so a
  // write placed after would be missed on three.
  const float tn = *(const float*)((const unsigned char*)rec + WALLREC_TNEAR);
  const float tf = *(const float*)((const unsigned char*)rec + WALLREC_TFAR);
  unsigned char* skip = (unsigned char*)rec + WALLREC_SKIP;
  if (!*skip) return;   // the builder already wants this wall drawn
  const int reasons = nocullwt_skip_reasons(true, tn, tf);
        if (!nocullwt_skip_wanted(reasons, s_keepOneSided != 0, s_keepBehind != 0, s_keepFar != 0)) {
    *skip = 0;
    return;
  }
}

static int rec_sector(const void* rec) {
  if (!rec || !s_pNumSectors || !s_pSectors) return -1;
  const int sect = *(const int*)rec;
  const int ns = (int)*s_pNumSectors;
  return (sect >= 0 && sect < ns && ns > 0 && ns <= NOCULLWT_MAX_SECTORS) ? sect : -1;
}

// Parallax ceilings are open sky and parallax floors open below: a cut there is the map speaking, not the frustum.
static bool sector_parallax(int sect, int which) {
  if (sect < 0) return false;
  const unsigned char* sec = s_pSectors + (unsigned)sect * SEC_STRIDE;
  const short stat = *(const short*)(sec + (which ? SEC_FLOORSTAT : SEC_CEILSTAT));
  return (stat & 1) != 0;
}

static int surf_sector(const void* recVec) {
  if (!recVec || !heap_readable(recVec, 8)) return -1;
  const unsigned begin = ((const unsigned*)recVec)[0];
  const unsigned end   = ((const unsigned*)recVec)[1];
  if (end < begin || ((end - begin) & 3u) != 0u || end == begin) return -1;
  if (!heap_readable((const void*)begin, 4)) return -1;
  const void* rec = *(const void* const*)begin;
  if (!rec || !heap_readable(rec, 8)) return -1;
  return rec_sector(rec);
}

static int rec_scores(const void* rec, const unsigned char** a, const unsigned char** b) {
  *a = 0; *b = 0;
  if (!ptr_plausible(rec)) return 0;
  const unsigned char* r = (const unsigned char*)rec;
  const unsigned beginA = *(const unsigned*)(r + WALLREC_SCOREA_BEGIN);
  const unsigned endA   = *(const unsigned*)(r + WALLREC_SCOREA_END);
  const unsigned beginB = *(const unsigned*)(r + WALLREC_SCOREB_BEGIN);
  if (endA < beginA) return 0;
  const unsigned n = endA - beginA;
  if (n == 0u || n > (unsigned)NOCULL_PLANES_MAX) return 0;
  if (!ptr_plausible((const void*)beginA) || !ptr_plausible((const void*)beginB)) return 0;
  *a = (const unsigned char*)beginA;
  *b = (const unsigned char*)beginB;
  return (int)n;
}

static int __stdcall wt_surf_clip(const void* recVec, int which) {
  SurfClipFn orig = s_origSurf;
  while (!orig) { YieldProcessor(); orig = s_origSurf; }
  const int kept = orig(recVec, which);
  if (!s_armed) return kept;

  // THE CUTAWAY: while the camera flies a ceiling is refused so the interior shows from above (never parallax ceilings:
  // the sky comes from FUN_007fa1b0's own ceilingstat read). The answer is replaced for this call only, inside the
  // bracket.
  if (s_dropCeilings && which == 0 && (kept & 0xFF) != 0) {
    const int sect = surf_sector(recVec);
    if (sect >= 0 && !sector_parallax(sect, 0)) {
      return 0;
    }
  }
  if ((kept & 0xFF) != 0) return kept;
  if (!recVec || !heap_readable(recVec, 8)) return kept;
  const unsigned begin = ((const unsigned*)recVec)[0];
  const unsigned end   = ((const unsigned*)recVec)[1];
  if (end < begin || ((end - begin) & 3u) != 0u) return kept;
  const int count = (int)((end - begin) >> 2);
  if (count <= 0 || count > NOCULLWT_ATTRIB_MAX ||
      !heap_readable((const void*)begin, (unsigned)count * 4u)) return kept;
  const unsigned char* pts[NOCULLWT_ATTRIB_MAX];
  unsigned char real[NOCULLWT_ATTRIB_MAX];
  const void* const* recs = (const void* const*)begin;
  int planes = 0, poisoned = 0;
  for (int r = 0; r < count; r++) {
    const unsigned char *a, *b;
    const int n = rec_scores(recs[r], &a, &b);
    if (n <= 0 || (planes && n != planes)) return kept;
    planes = n;
    pts[r] = which ? b : a;
    const float z = *(const float*)((const unsigned char*)recs[r] + (which ? WALLREC_ZB : WALLREC_ZA));
    real[r] = nocullwt_is_sentinel_z(z) ? 0u : 1u;
    if (!real[r]) poisoned++;
  }
  s_cPoisoned += poisoned;
  const int i = nocullwt_reject_plane_all(pts, count, planes);
  if (i >= 0) {
    const bool par = sector_parallax(rec_sector(recs[0]), which);
    const bool inf = (poisoned == count);   // every corner point is the sentinel
    // With the sentinel points set aside the real ones do not agree: the engine's cut was made on coordinates 134 km
    // away. Hand back "keep" (the floor that draws is the sector's own mesh from the level-load cache).
    if (poisoned && nocullwt_reject_plane_real(pts, real, count, planes) < 0) {
      return 1;
    }
  }
  return kept;
}

// Span structure: the insertion and early return that drops a quad whose span is already covered, recomputed from the
// same six floats before the original runs.
typedef void* (__fastcall* SpanInsertFn)(void*, void*, void*, void*);
static SpanInsertFn volatile s_origSpanInsert = 0;

static bool span_ptr_ok(const void* p) {
  return p != 0 && ((UINT_PTR)p & 3u) == 0;
}
// Infinity or NaN, from the bits rather than <math.h> (exponent all ones is both): this path runs twenty thousand times
// a frame.
static bool span_not_finite(float f) {
  return (*(const unsigned*)&f & 0x7F800000u) == 0x7F800000u;
}

// RUNAWAY SPANS: q0/q1 are x/w (visible range [-1, +1]); a wall crossing the eye plane sends x/w away as w nears zero
// (spans of 60.7 where most are under 0.02), and such a span owns every node beneath it.

// VIEWPORT CLAMP (nocullclampspan, default 1): x/w outside [-1, +1] is off screen, so cutting a runaway span there
// loses no visible geometry. The engine clips against scene+0x254, not w = 0.
static void span_clamp_width(void* quad) {
  if (!s_clampSpan || !span_ptr_ok(quad)) return;
  float* q = (float*)quad;
  if (!(q[1] > q[0])) return;
  if (q[0] >= -1.0f && q[1] <= 1.0f) return;          // already on screen
  const float lo = q[0] < -1.0f ? -1.0f : q[0];
  const float hi = q[1] >  1.0f ?  1.0f : q[1];
  if (!(hi > lo)) return;                             // wholly off screen: leave it
  q[0] = lo;
  q[1] = hi;
}
#define SPAN_WATCH_WALLS 4
struct SpanWallWatch { int wall; unsigned q2; long seen; long covered; long takenOver; };

// THE DEGENERATE QUAD: FUN_007faa20 divides by w; a corner at or behind the eye plane sends depth to infinity, so one
// wall swallows every wall sharing its span. Clamped to -FLT_MAX on the engine's scratch copy (by value: stack slots
// are reused).
static void span_clamp_degenerate(void* quad, int* outcome, float* copyOut) {
  if (copyOut) { copyOut[0] = copyOut[1] = copyOut[2] = copyOut[3] = copyOut[4] = 0.0f; }
  if (!span_ptr_ok(quad)) {
    if (outcome) *outcome = SPANCLAMP_PTRBAD;
    return;
  }
  float* q = (float*)quad;
  if (copyOut) for (int i = 0; i < 5; i++) copyOut[i] = q[i];
  const unsigned bits = *(const unsigned*)(q + 2);
  const int viaHelper = span_not_finite(q[2]) ? 1 : 0;
  const int viaBits = ((bits & 0x7F800000u) == 0x7F800000u) ? 1 : 0;
        if (!viaBits) {
    if (outcome) *outcome = SPANCLAMP_FINITE;
    return;
  }
  q[2] = -3.402823466e38f;
  if (copyOut) copyOut[2] = q[2];
  if (span_not_finite(q[2])) 
  if (outcome) *outcome = SPANCLAMP_WROTE;
}

// A node with NO DEPTH: FUN_00814460 initialises a fresh node to n3=+FLT_MAX n4=-FLT_MAX n5=+FLT_MAX n6=-FLT_MAX; it
// reaches the coverage test fresh (n5 > n6) or from FUN_00813870's four-allocation branch (X.n5 = X.n6 = parent.n5).
static bool span_node_has_no_depth(float n5, float n6) {
  return n5 > n6 || n5 >= 3.402823466e38f;
}

// Does this wall have a True3D mesh entry? Such walls go to the "normal" list (FUN_007ba740) and never enter the span
// tree. Read from the per-sector cache.
static bool wall_has_mesh(int wall) {
  if (wall < 0 || wall >= NOCULLWT_MAX_WALLS || !s_pCache || !s_pSectors || !s_pNumSectors)
    return false;
  const int sect = (int)s_wallSector[wall];
  if (sect < 0 || sect >= (int)*s_pNumSectors) return false;
  const unsigned cache = s_pCache[sect];
  if (!cache || !heap_readable((const void*)cache, 0x1C)) return false;
  const unsigned base = *(const unsigned*)(cache + CACHE_WALLS_OFF);
  const unsigned char* sec = s_pSectors + (unsigned)sect * SEC_STRIDE;
  const int wallptr = (int)*(const short*)(sec + SEC_WALLPTR);
  const int wallnum = (int)*(const short*)(sec + SEC_WALLNUM);
  const int rel = wall - wallptr;
  if (rel < 0 || rel >= wallnum) return false;
  if (!base || !heap_readable((const void*)base, (unsigned)wallnum * CACHE_ENTRY)) return false;
  const unsigned e = base + (unsigned)rel * CACHE_ENTRY;
  return *(const int*)(e + CACHE_SUB_MID) != 0 || *(const int*)(e + CACHE_SUB_STEP) != 0
      || *(const int*)(e + CACHE_SUB_HEADER) != 0;
}


// nocullhorizon makes FUN_007fb570 answer "not occluded" while flying: 0 off, 1 RECORDS ONLY (ships: types 0..3 decide
// whether to append a record), 2 everything (type 6 also prunes the portal traversal, costing frames).
#define HORIZON_RECORDS 1
#define HORIZON_ALL     2
static int s_noHorizon = HORIZON_RECORDS;


static void span_open_empty_node(void* node) {
  if (!span_ptr_ok(node)) return;
  int* n = (int*)node;
  const float n5 = *(const float*)&n[5], n6 = *(const float*)&n[6];
  if (!span_node_has_no_depth(n5, n6)) return;
  const float empty = -3.402823466e38f;
  *(float*)&n[5] = empty;
  *(float*)&n[6] = empty;
}

// THE TOTAL EXEMPTION (nocullwallocclusion=3, default): out-of-order occlusion is unstable under camera motion, so any
// node that would have occluded is opened, as an empty one is.
static void span_open_covering_node(void* node, const void* quad) {
  if (!s_wallOccTotal || !span_ptr_ok(node) || !span_ptr_ok(quad)) return;
  int* n = (int*)node;
  const float n3 = *(const float*)&n[3], n4 = *(const float*)&n[4];
  const float n5 = *(const float*)&n[5];
  const float* q = (const float*)quad;
  if (!(n3 <= q[0] && q[1] <= n4 && q[2] <= n5)) return;   // would not have covered
  const float empty = -3.402823466e38f;
  *(float*)&n[5] = empty;
  *(float*)&n[6] = empty;
}

static bool span_already_covered(const void* node, const void* quad) {
  // span_ptr_ok, NOT heap_readable: this runs ~20000 times a frame and the VirtualQuery cost 19 fps; the original
  // dereferences both pointers next anyway.
  if (!span_ptr_ok(node) || !span_ptr_ok(quad)) return false;
  const int* n = (const int*)node;
  const float* q = (const float*)quad;
  const float n3 = *(const float*)&n[3];
  const float n4 = *(const float*)&n[4];
  const float n5 = *(const float*)&n[5];
  const float n6 = *(const float*)&n[6];
  if (span_node_has_no_depth(n5, n6)) return false;
  return n3 <= q[0] && q[1] <= n4 && q[2] <= n5;
}

#define NOCULL_BLAME_SAMPLES 6

// OPTION A: nudge q0 just past n3 so a quad that would OVERWRITE an incumbent takes the split path; it only leaves the
// incumbent a SLIVER (branch D sets param_1[4] = q0), so the takeover counter says success while the picture does not
// move.
static void span_force_split(const void* node, void* quad) {
  if (s_wallOccMode != WALLOCC_SUBDIVIDE) return;
  if (!span_ptr_ok(node) || !span_ptr_ok(quad)) return;
  const int* n = (const int*)node;
  float* q = (float*)quad;
  const float n3 = *(const float*)&n[3], n4 = *(const float*)&n[4];
  const float n5 = *(const float*)&n[5];
  if (!(q[0] <= n3 && n4 <= q[1] && q[2] > n5)) return;   // absorb would not fire
  const float eps = (n3 < 0.0f ? -n3 : n3) * 1.0e-6f + 1.0e-6f;
  const float moved = n3 + eps;
  if (span_not_finite(moved) || !(moved > n3) || !(moved <= q[1])) return;
  q[0] = moved;
}

static void span_let_through(const void* node, void* quad) {
  if (s_wallOccMode != WALLOCC_LETTHRU) return;
  if (!span_ptr_ok(node) || !span_ptr_ok(quad)) return;
  float* q = (float*)quad;
  const float n5 = *(const float*)&((const int*)node)[5];
  const float was = q[2];
  const float eps = (n5 < 0.0f ? -n5 : n5) * 1.0e-6f + 1.0e-6f;
  const float moved = n5 + eps;
  // A fresh node has n5 = +FLT_MAX and n5 + eps overflows to +inf, which the engine max-propagates to every ancestor
  // (~50 births a frame). Nothing is representable past +FLT_MAX: refuse.
  if (span_not_finite(moved)) { return; }
  q[2] = moved;
}

// THE FREE CAMERA WALL EXEMPTION (nocullwallocclusion, default 3). Walls with no True3D mesh go through an occlusion
// tree that assumes front-to-back arrival (false with the camera outside), discarding walls whose floors and sprites
// still draw. Only the quad is edited.
static void* __fastcall wt_span_insert(void* node, void* edx, void* batcher, void* quad) {
  (void)edx;
  // camera_active() FIRST: this runs ~20000 times a frame and span_already_covered ends in a VirtualQuery; with the
  // camera down the hook is a load, a branch and the tail call.
  int clampOutcome = SPANCLAMP_NOTCALLED;
  float quadAtClamp[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  if (s_wallOcc && camera_active()) {
    span_clamp_degenerate(quad, &clampOutcome, quadAtClamp);
    span_clamp_width(quad);
    span_clamp_width(quad);
    span_open_empty_node(node);
    span_force_split(node, quad);
    span_open_covering_node(node, quad);
    if (span_already_covered(node, quad)) {
      const float n5 = span_ptr_ok(node) ? *(const float*)&((const int*)node)[5] : 0.0f;
      if (span_not_finite(n5)) {
      } else {
        span_let_through(node, quad);
      }
    }
    if (!span_already_covered(node, quad) && span_ptr_ok(node) && span_ptr_ok(quad)) {
      const int* n = (const int*)node;
      const float* q = (const float*)quad;
      const int abs_ = (q[0] <= *(const float*)&n[3] && *(const float*)&n[4] <= q[1]
                        && q[2] > *(const float*)&n[5]) ? 1 : 0;
    } else if (span_ptr_ok(quad)) {
    }
  }
  const unsigned q2AfterClamp = span_ptr_ok(quad) ? *(const unsigned*)&((const float*)quad)[2] : 0u;
  const int hadBefore = span_ptr_ok(node) ? 1 : 0;
  const float n5Before = hadBefore ? *(const float*)&((const int*)node)[5] : 0.0f;
  const int n7Before = hadBefore ? ((const int*)node)[7] : 0;
  const float n3Before = hadBefore ? *(const float*)&((const int*)node)[3] : 0.0f;
  const float n4Before = hadBefore ? *(const float*)&((const int*)node)[4] : 0.0f;
  SpanInsertFn orig = s_origSpanInsert;
  while (!orig) { YieldProcessor(); orig = s_origSpanInsert; }
  void* r = orig(node, 0, batcher, quad);
  return r;
}

typedef void* (__fastcall* SpanNodeFn)(void*, void*, void*, void*);
static SpanNodeFn volatile s_origSpanNode = 0;

static void* __fastcall wt_span_node(void* node, void* edx, void* batcher, void* quad) {
  (void)edx;
  int clampOutcome = SPANCLAMP_NOTCALLED;
  float quadAtClamp[5] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
  if (s_wallOcc && camera_active()) {
    span_clamp_degenerate(quad, &clampOutcome, quadAtClamp);
    span_open_empty_node(node);
    span_force_split(node, quad);
    span_open_covering_node(node, quad);
    if (span_already_covered(node, quad)) {
      const float n5 = span_ptr_ok(node) ? *(const float*)&((const int*)node)[5] : 0.0f;
      if (span_not_finite(n5)) {
        // Covered by an already-infinite node: counted, not fought.
      } else {
        span_let_through(node, quad);
      }
    }
    if (!span_already_covered(node, quad) && span_ptr_ok(node) && span_ptr_ok(quad)) {
      const int* n = (const int*)node;
      const float* q = (const float*)quad;
      const int abs_ = (q[0] <= *(const float*)&n[3] && *(const float*)&n[4] <= q[1]
                        && q[2] > *(const float*)&n[6]) ? 1 : 0;
    } else if (span_ptr_ok(quad)) {
    }
  }
  const unsigned q2AfterClamp = span_ptr_ok(quad) ? *(const unsigned*)&((const float*)quad)[2] : 0u;
  const int hadBefore = span_ptr_ok(node) ? 1 : 0;
  const float n5Before = hadBefore ? *(const float*)&((const int*)node)[5] : 0.0f;
  const int n7Before = hadBefore ? ((const int*)node)[7] : 0;
  const float n3Before = hadBefore ? *(const float*)&((const int*)node)[3] : 0.0f;
  const float n4Before = hadBefore ? *(const float*)&((const int*)node)[4] : 0.0f;
  SpanNodeFn orig = s_origSpanNode;
  while (!orig) { YieldProcessor(); orig = s_origSpanNode; }
  void* r = orig(node, 0, batcher, quad);
  return r;
}

typedef void (__fastcall* DrawWrapFn)(void* batch, void* edx, int count, int start);

typedef void* (__fastcall* NodeAllocFn)(void* pool, void* edx);

static void install_span(void) {
  const unsigned char* at = 0;
  at = (const unsigned char*)engine_ptr(kRvaSpanInsert, kSpanInsertPrologue);
  if (!at) {
    wt_log("nocull span: rva 0x%X is outside the image; the insertion is NOT hooked",
           kRvaSpanInsert);
  } else if (memcmp(at, kFpSpanInsert, sizeof(kFpSpanInsert)) != 0) {
    wt_log("nocull span: rva 0x%X begins %02X %02X %02X %02X %02X %02X, not the push ebp / "
           "mov ebp,esp / sub esp,0x14 we read; the insertion is NOT hooked",
           kRvaSpanInsert, at[0], at[1], at[2], at[3], at[4], at[5]);
  } else {
    void* tramp = 0;
    if (hook_install(kRvaSpanInsert, kFpSpanInsert, sizeof(kFpSpanInsert),
                     kSpanInsertPrologue, (void*)wt_span_insert, &tramp) && tramp) {
      s_origSpanInsert = (SpanInsertFn)tramp;
      s_spanInsertOk = 1;
    } else {
      wt_log("nocull span: the insertion hook did not take at rva 0x%X", kRvaSpanInsert);
    }
  }
  at = (const unsigned char*)engine_ptr(kRvaSpanNode, kSpanNodePrologue);
  if (!at) {
    wt_log("nocull span: rva 0x%X is outside the image; the interior node is NOT hooked",
           kRvaSpanNode);
  } else if (memcmp(at, kFpSpanNode, sizeof(kFpSpanNode)) != 0) {
    wt_log("nocull span: rva 0x%X begins %02X %02X %02X %02X %02X %02X, not the push ebp / "
           "mov ebp,esp / sub esp,0x1C we read; the interior node is NOT hooked",
           kRvaSpanNode, at[0], at[1], at[2], at[3], at[4], at[5]);
  } else {
    void* tramp = 0;
    if (hook_install(kRvaSpanNode, kFpSpanNode, sizeof(kFpSpanNode), kSpanNodePrologue,
                     (void*)wt_span_node, &tramp) && tramp) {
      s_origSpanNode = (SpanNodeFn)tramp;
      s_spanNodeOk = 1;
    } else {
      wt_log("nocull span: the interior node hook did not take at rva 0x%X", kRvaSpanNode);
    }
  }
  wt_log("nocull span: leaf %s, interior node %s. The interior node is the site that carries "
         "97.5%% of the traffic; both exist to keep a quad out of a node that still holds the "
         "fresh-node sentinel, which is what closed the holes",
         s_spanInsertOk ? "on" : "OFF", s_spanNodeOk ? "on" : "OFF");
}

// Fallback path: twenty dwords because the function takes twenty (RET 0x50); only a2 is interpreted.
typedef void (__fastcall* FallbackFn)(void*, void*, unsigned, unsigned, unsigned, unsigned,
                                      unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                                      unsigned, unsigned, unsigned, unsigned, unsigned, unsigned,
                                      unsigned, unsigned, unsigned, unsigned);

static int sweep_sectors(ScanSectorFn orig, void* scene, int sect, void* a2, void* a3,
                         int ns, int* stoppedOut) {
  const unsigned char* vec = (const unsigned char*)scene + SCENE_MASKVEC_BEGIN;
  int drawn = 0;
  for (int s = 0; s < ns && s < NOCULLWT_MAX_SECTORS; s++) {
    (void)sect;
    const unsigned begin = *(const unsigned*)vec;
    const unsigned end   = *(const unsigned*)(vec + 4);
    const int pending = nocullwt_pending_records(begin, end);
    const int wallnum = (int)*(const short*)(s_pSectors + (unsigned)s * SEC_STRIDE + SEC_WALLNUM);
    if (pending < 0) {
      if (!s_loggedBadVec) {
        s_loggedBadVec = 1;
        wt_log("nocull: the masked-wall vector reads begin %p end %p, which is not a vector "
               "of 16-byte records; the sweep is off for this session", (void*)begin, (void*)end);
      }
      s_disabled = 1;
      return drawn;
    }
    if (!nocullwt_budget_allows(pending, wallnum)) { *stoppedOut = 1; return drawn; }
    orig(scene, 0, s, a2, a3);
    drawn++;
  }
  return drawn;
}

static void __fastcall wt_scan_sector(void* scene, void* edx, int sect, void* a2, void* a3) {
  (void)edx;
  ScanSectorFn orig = s_orig;
  while (!orig) { YieldProcessor(); orig = s_orig; }

  // nocull.h's fourth argument: a visible mirror calls displayrooms twice, so the sweep must not double. The mirror
  // gate is still unwired (the engine's preparemirror flag is not located in this binary).
  const bool inMirror = false;

  // camera_render_armed() is the whole gate: true only between the camera bracket's substitution and its __finally.
  const int anyRelax = (!s_keepOneSided || !s_keepBehind || !s_keepFar || s_dropCeilings ||
                        s_wallOcc);
  if (s_disabled || !s_ok || !scene || (!s_sweep && !anyRelax) ||
      !nocull_should_scan(g_cfg.freecamnocull != 0, camera_active(), camera_render_armed(),
                          /*inMirror*/ inMirror)) {
    orig(scene, 0, sect, a2, a3);
    return;
  }

  const int ns = (int)*s_pNumSectors;
  if (!nocullwt_sectors_ok(ns)) {
    if (!s_loggedBadCount) {
      s_loggedBadCount = 1;
      wt_log("nocull: numsectors reads %d, which is not a map this may sweep; "
             "the sweep is off for this session", ns);
    }
    s_disabled = 1;
    orig(scene, 0, sect, a2, a3);
    return;
  }
  if (!heap_readable((const unsigned char*)scene + SCENE_DRAWVEC_BEGIN, 0x2C)) {
    if (!s_loggedBadVec) {
      s_loggedBadVec = 1;
      wt_log("nocull: the scene object's masked-wall vector at +0x%X is not readable; "
             "the sweep is off for this session", SCENE_MASKVEC_BEGIN);
    }
    s_disabled = 1;
    orig(scene, 0, sect, a2, a3);
    return;
  }

  int drawn = 0, stopped = 0;
  s_camSectBy = 0;

  // Only POD locals, so __finally can sit in a /EHsc translation unit: nothing the engine's body does leaves the
  // skip-clearing flag raised into normal play. The frame id is bumped before the walk.
  s_frameId++;
  if (s_frameId == 0) s_frameId = 1;   // 0 means "never computed"; never reuse it
  s_armed = 1;
  __try {
    // THE SWEEP GOES FIRST: with the portal walk first, it claimed every reachable sector and emitted each INSIDE the
    // walk with its clip state, the sweep only leftovers cold, and the split changed as the camera turned.
    if (s_sweep) {
      s_inSweep = 1;
      drawn = sweep_sectors(orig, scene, sect, a2, a3, ns, &stopped);
      s_inSweep = 0;
    }
    orig(scene, 0, sect, a2, a3);
  } __finally {
    s_armed = 0;
  }

  const int stops = nocullwt_stop_flags(stopped != 0, 0);
  if (!s_loggedFirst) {
    s_loggedFirst = 1;
    wt_log("nocull: first swept frame, seed sector %d, %d swept + the seed = %d of %d emitted, "
           "skip byte %s%s; renderer mode selector %d gate %u, so %s",
           sect, drawn, drawn + 1, ns,
           s_buildOk ? (s_keepOneSided ? "per-test (one-sided kept)" : "per-test (one-sided off)")
                     : "left alone (the builder hook is not in)",
           stopped ? ", masked-wall reserve stopped the sweep short" : "",
           s_pModeSel ? *s_pModeSel : -1, s_pModeGate ? (unsigned)*s_pModeGate : 0xFFu,
           (s_pModeSel && s_pModeGate && *s_pModeSel == 1 && *s_pModeGate == 0)
               ? "ENHANCED (True3D)" : "CLASSIC or unknown");
  }
}

static int check_site(void) {
  const unsigned char* op = s_base + kRvaSeedSect;
  if (memcmp(op, kFpSeedSectOp, sizeof(kFpSeedSectOp)) != 0) {
    wt_log("nocull: rva 0x%X is %02X %02X %02X, not the MOVSX that loads globalcursectnum; "
           "installing nothing", kRvaSeedSect, op[0], op[1], op[2]);
    return 0;
  }
  const unsigned char* want = s_base + kRvaGlobalCurSec;
  const unsigned char* got = *(const unsigned char* const*)(op + 3);
  if (got != want) {
    wt_log("nocull: the MOVSX at rva 0x%X reads %p, not globalcursectnum at %p; "
           "installing nothing", kRvaSeedSect, (const void*)got, (const void*)want);
    return 0;
  }
  const unsigned char* mid = s_base + kRvaSeedThis;
  if (memcmp(mid, kFpSeedThis, sizeof(kFpSeedThis)) != 0) {
    wt_log("nocull: rva 0x%X is %02X %02X %02X, not the MOV ECX,EBX / PUSH EAX before the "
           "seed call; installing nothing", kRvaSeedThis, mid[0], mid[1], mid[2]);
    return 0;
  }
  const unsigned char* body = s_base + kRvaScanSector;
  if (memcmp(body, kFpScanSector, sizeof(kFpScanSector)) != 0) {
    wt_log("nocull: the emit body at rva 0x%X is %02X %02X %02X %02X %02X, not the "
           "push ebp / mov ebp,esp / push -1 we read; installing nothing", kRvaScanSector,
           body[0], body[1], body[2], body[3], body[4]);
    return 0;
  }
  const unsigned char* site = s_base + kRvaSeedCall;
  const unsigned char* target = site + 5 + *(const int*)(site + 1);
  if (site[0] == 0xE8 && target != body) {
    wt_log("nocull: the seed call at rva 0x%X goes to %p, not the emit body at %p; "
           "installing nothing", kRvaSeedCall, (const void*)target, (const void*)body);
    return 0;
  }
  return 1;
}
// nocullhidemesh withholds the mesh path's WALL appends (types 2..5) while the free camera flies; it settled that the
// popping was the mesh population, not the span tree's.
static volatile OccludeFn s_origOcclude = 0;

static char __stdcall wt_occlude(void* item) {
  OccludeFn orig = s_origOcclude;
  while (!orig) { YieldProcessor(); orig = s_origOcclude; }
  const char v = orig(item);
  if (!camera_active()) return v;
  const int type = ptr_plausible(item) ? ((const int*)item)[1] : -1;
    // The original always runs first, so the tree is queried and updated as usual; only the answer handed back changes.
  if (s_noHorizon && v && camera_render_armed()) {
    if (type == 6) {
      if (s_noHorizon != HORIZON_ALL) return v;   // traversal stays the engine's
    }
    return 0;
  }
  return v;
}

static void install_occlude(void) {
  const unsigned char* at = (const unsigned char*)
      engine_ptr(kRvaOcclude, kOccludePrologue + (unsigned)sizeof(kFpOccludeTail));
  if (!at) {
    wt_log("nocull horizon: rva 0x%X is not inside the module; the occlusion test is unwatched",
           kRvaOcclude);
    return;
  }
  if (memcmp(at, kFpOcclude, sizeof(kFpOcclude)) != 0) {
    wt_log("nocull horizon: rva 0x%X is %02X %02X %02X %02X %02X %02X, not the push ebx / mov "
           "ebx,esp / sub esp,8 we read; the occlusion test is unwatched", kRvaOcclude,
           at[0], at[1], at[2], at[3], at[4], at[5]);
    return;
  }
  const unsigned char* tail = at + kOccludePrologue;
  if (memcmp(tail, kFpOccludeTail, sizeof(kFpOccludeTail)) != 0) {
    wt_log("nocull horizon: rva 0x%X at +%u is %02X %02X %02X %02X %02X %02X, not the and "
           "esp,-8 / add esp,4 we read; the occlusion test is unwatched", kRvaOcclude,
           kOccludePrologue, tail[0], tail[1], tail[2], tail[3], tail[4], tail[5]);
    return;
  }
  void* tramp = 0;
  if (!hook_install(kRvaOcclude, kFpOcclude, sizeof(kFpOcclude), kOccludePrologue,
                    (void*)wt_occlude, &tramp) || !tramp) {
    wt_log("nocull horizon: the FUN_007fb570 hook did not take; the occlusion test is unwatched");
    return;
  }
  s_origOcclude = (OccludeFn)tramp;
  wt_log("nocull horizon: rva 0x%X is the occlusion test FUN_007fb490 runs BEFORE it appends a "
         "mesh record, and it answers by querying the span tree at 0x813ea0. Watching it; "
         "nocullhorizon is %s", kRvaOcclude,
         s_noHorizon == HORIZON_ALL ? "2, so every YES is overridden while the camera flies, "
                                      "traversal pruning included"
         : s_noHorizon == HORIZON_RECORDS ? "1, so a YES is overridden for the record decision "
                                            "and left alone for the portal traversal"
         : "off, so its verdict stands");
}

typedef void (__fastcall *WallDrawFn)(void*, void*, void*, void*);

typedef void (__fastcall *MeshAppendFn)(void*, void*, const int*);
static volatile MeshAppendFn s_origMeshAppend = 0;

static void __fastcall wt_mesh_append(void* self, void* edx, const int* rec) {
  MeshAppendFn orig = s_origMeshAppend;
  while (!orig) { YieldProcessor(); orig = s_origMeshAppend; }
  // Types 0 and 1 are the sector's OWN floor and ceiling and are let through; types 2..5 are the wall surfaces this
  // withholds.
  if (!s_hideMesh || !camera_render_armed() || !ptr_plausible((void*)rec)) {
    orig(self, edx, rec);
    return;
  }
  const int type = rec[2];
  if (type >= 2 && type <= 5) return;
  orig(self, edx, rec);
}

static void install_mesh_append(void) {
  const unsigned char* at = (const unsigned char*)
      engine_ptr(kRvaMeshAppend, kMeshAppendPrologue + (unsigned)sizeof(kFpMeshAppendTail));
  if (!at) {
    wt_log("nocull hidemesh: rva 0x%X is not inside the module; the mesh path stays",
           kRvaMeshAppend);
    return;
  }
  if (memcmp(at, kFpMeshAppend, sizeof(kFpMeshAppend)) != 0) {
    wt_log("nocull hidemesh: rva 0x%X is %02X %02X %02X %02X %02X, not the push ebp / mov "
           "ebp,esp / push esi / push edi we read; the mesh path stays", kRvaMeshAppend,
           at[0], at[1], at[2], at[3], at[4]);
    return;
  }
  const unsigned char* tail = at + kMeshAppendPrologue;
  if (memcmp(tail, kFpMeshAppendTail, sizeof(kFpMeshAppendTail)) != 0) {
    wt_log("nocull hidemesh: rva 0x%X at +%u is %02X %02X %02X %02X %02X %02X, not the mov "
           "edi,ecx / mov ecx,[ebp+8] we read; the mesh path stays", kRvaMeshAppend,
           kMeshAppendPrologue, tail[0], tail[1], tail[2], tail[3], tail[4], tail[5]);
    return;
  }
  void* tramp = 0;
  if (!hook_install(kRvaMeshAppend, kFpMeshAppend, sizeof(kFpMeshAppend), kMeshAppendPrologue,
                    (void*)wt_mesh_append, &tramp) || !tramp) {
    wt_log("nocull hidemesh: the mesh append hook did not take; the mesh path stays");
    return;
  }
  s_origMeshAppend = (MeshAppendFn)tramp;
  wt_log("nocull hidemesh: ON -- rva 0x%X is the normal list's only append site, and while the "
         "free camera flies its WALL appends (types 2..5) are refused. Floors and ceilings are "
         "let through. The picture WILL look wrong: every mesh-path wall is gone on purpose. "
         "The question is whether what is LEFT still pops", kRvaMeshAppend);
}

static void install_wall_build(void) {
  const unsigned char* at = (const unsigned char*)
      engine_ptr(kRvaWallBuild, kWallBuildPrologue + (unsigned)sizeof(kFpWallBuildTail));
  if (!at) return;
  if (memcmp(at, kFpWallBuild, sizeof(kFpWallBuild)) != 0) {
    wt_log("nocull: the wall record builder at rva 0x%X is %02X %02X %02X %02X %02X %02X, not "
           "the push ebp / mov ebp,esp / and esp,-8 we read; the builder's rejections stay",
           kRvaWallBuild, at[0], at[1], at[2], at[3], at[4], at[5]);
    return;
  }
  const unsigned char* tail = at + kWallBuildPrologue;
  if (memcmp(tail, kFpWallBuildTail, sizeof(kFpWallBuildTail)) != 0) {
    wt_log("nocull: rva 0x%X at +%u is %02X %02X %02X %02X %02X %02X, not the sub esp,0x74 / "
           "mov edx,[ebp+8] we read; the builder's rejections stay", kRvaWallBuild,
           kWallBuildPrologue, tail[0], tail[1], tail[2], tail[3], tail[4], tail[5]);
    return;
  }
  void* tramp = 0;
  if (!hook_install(kRvaWallBuild, kFpWallBuild, sizeof(kFpWallBuild), kWallBuildPrologue,
                    (void*)wt_wall_build, &tramp) || !tramp) {
    wt_log("nocull: the wall record builder hook did not take; the sweep offers every sector "
           "but each wall keeps the builder's own rejections");
    return;
  }
  s_origBuild = (WallBuildFn)tramp;
  s_buildOk = 1;
  wt_log("nocull: rva 0x%X is the per-wall record builder; while the sweep runs its +0x%X skip "
         "byte is cleared after it returns, which retires the one-sided wall test, the "
         "both-endpoints-behind test and the 100000-unit far test together and leaves the "
         "six-plane frustum test behind them as the only authority", kRvaWallBuild, WALLREC_SKIP);
}

// The floor and ceiling hook carries the abstain rule (a sector poisoned by the off-map sentinel keeps its floor), so
// it goes in whether or not the census does.
static void install_surf_clip(void) {
  const unsigned char* at = (const unsigned char*)
      engine_ptr(kRvaSurfClip, kSurfClipPrologue + (unsigned)sizeof(kFpSurfClipTail));
  if (at && memcmp(at, kFpSurfClip, sizeof(kFpSurfClip)) == 0 &&
      memcmp(at + kSurfClipPrologue, kFpSurfClipTail, sizeof(kFpSurfClipTail)) == 0) {
    void* tramp = 0;
    if (hook_install(kRvaSurfClip, kFpSurfClip, sizeof(kFpSurfClip), kSurfClipPrologue,
                     (void*)wt_surf_clip, &tramp) && tramp) {
      s_origSurf = (SurfClipFn)tramp;
      return;
    }
    wt_log("nocull: the floor/ceiling hook did not install; sectors poisoned by the off-map "
           "sentinel keep losing their floors");
    return;
  }
  if (at)
    wt_log("nocull: the floor/ceiling test at rva 0x%X is %02X %02X %02X %02X %02X / "
           "%02X %02X %02X %02X %02X, not what we read; the abstain rule is not in",
           kRvaSurfClip, at[0], at[1], at[2], at[3], at[4],
           at[5], at[6], at[7], at[8], at[9]);
}

bool nocullwt_install() {
  s_base = (const unsigned char*)GetModuleHandleA(0);
  if (!s_base) { wt_log("nocull: no module base"); nocull_mark_unavailable(); return false; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    wt_log("nocull: not a DOS image"); nocull_mark_unavailable(); return false;
  }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    wt_log("nocull: not a PE image"); nocull_mark_unavailable(); return false;
  }
  s_imageSize = nt->OptionalHeader.SizeOfImage;

  s_pNumSectors = (const short*)engine_ptr(kRvaNumSectors, sizeof(short));
  s_pSectors = (const unsigned char*)engine_ptr(kRvaSectors, NOCULLWT_MAX_SECTORS * SEC_STRIDE);
  s_pPosX = (const int*)engine_ptr(kRvaGlobalPosX, sizeof(int));
  s_pPosY = (const int*)engine_ptr(kRvaGlobalPosY, sizeof(int));
  s_pPosZ = (const int*)engine_ptr(kRvaGlobalPosZ, sizeof(int));
  s_pCache = (const unsigned*)engine_ptr(kRvaMeshCache, NOCULLWT_MAX_SECTORS * sizeof(unsigned));
  s_pNumWalls = (const short*)engine_ptr(kRvaNumWalls, sizeof(short));
  s_pWalls = (const unsigned char*)engine_ptr(kRvaWalls, NOCULLWT_MAX_WALLS * WALL_STRIDE);
  s_pRenderer = (void* const*)engine_ptr(kRvaRenderer, sizeof(void*));
  s_pModeSel = (const int*)engine_ptr(kRvaModeSel, sizeof(int));
  s_pModeGate = (const unsigned char*)engine_ptr(kRvaModeGate, 1);
  const void* site = engine_ptr(kRvaSeedCall, 5);
  const void* op = engine_ptr(kRvaSeedSect, 7);
  const void* body = engine_ptr(kRvaScanSector, 5);
  const void* curSec = engine_ptr(kRvaGlobalCurSec, sizeof(short));
  s_pCurSec = (const short*)curSec;   // the cutaway spares the camera's own sector
  if (!s_pNumSectors || !s_pSectors || !s_pPosX || !s_pPosY || !s_pPosZ || !s_pCache ||
      !s_pNumWalls || !s_pWalls || !s_pRenderer || !s_pModeSel || !s_pModeGate || !site || !op || !body || !curSec) {
    wt_log("nocull: an engine address did not fit the image; installing nothing");
    nocull_mark_unavailable();
    return false;
  }
  if (!check_site()) { nocull_mark_unavailable(); return false; }

  // Taken before the patch: after it the site's rel32 points at us.
  s_orig = (ScanSectorFn)(s_base + kRvaScanSector);
  if (!hook_patch_call(kRvaSeedCall, kFpSeedCall, (void*)wt_scan_sector)) {
    wt_log("nocull: the seed call was not redirected; \"Disable culling\" stays unavailable "
           "and the free camera still flies");
    s_orig = 0;
    nocull_mark_unavailable();
    return false;
  }
  s_ok = 1;
  // Read first: the keys decide what is installed; the census is opt-in diagnostic scaffolding.
  const char* const ini = wt_ini_path();
  s_sweep     = GetPrivateProfileIntA("wtmod", "nocullsweep", 1, ini) ? 1 : 0;
  s_keepOneSided  = GetPrivateProfileIntA("wtmod", "nocullwalls", 1, ini) ? 0 : 1;
  s_keepBehind    = GetPrivateProfileIntA("wtmod", "nocullbehind", 0, ini) ? 0 : 1;
  s_keepFar       = GetPrivateProfileIntA("wtmod", "nocullfar", 0, ini) ? 0 : 1;
  s_dropCeilings  = GetPrivateProfileIntA("wtmod", "nocullceilings", 0, ini) ? 1 : 0;
  // THE CUTAWAY, default 1: the engine has no position test, so neither nocullwalls setting is a dollhouse. Costs ~25
  // fps against 44.
  {
    // Default 3 (TOTAL), the least wrong measured picture: 0 leaves holes, 1 leaves sliver pillars, 2 fills the walls
    // in and pops.
    const int mode = GetPrivateProfileIntA("wtmod", "nocullwallocclusion", WALLOCC_TOTAL, ini);
    s_wallOcc = mode ? 1 : 0;
    s_wallOccMode = mode;
    s_wallOccTotal = (mode == WALLOCC_TOTAL) ? 1 : 0;
  }
  s_clampSpan = GetPrivateProfileIntA("wtmod", "nocullclampspan", 1, ini) ? 1 : 0;
  s_noHorizon = GetPrivateProfileIntA("wtmod", "nocullhorizon", HORIZON_RECORDS, ini);
  if (s_noHorizon < 0 || s_noHorizon > HORIZON_ALL) s_noHorizon = HORIZON_RECORDS;
  s_hideMesh = GetPrivateProfileIntA("wtmod", "nocullhidemesh", 0, ini) ? 1 : 0;
  // wall_has_mesh answers false for both "no mesh" and "could not tell"; with no cache pointer it would hide the WHOLE
  // map, so that is checked here.
  if (!s_keepOneSided || !s_keepBehind || !s_keepFar)
    install_wall_build();
  // Installed whether or not the test is on, since it also carries the mesh path's counting.
  if (s_hideMesh) install_mesh_append();
  install_occlude();
  install_surf_clip();
  if (s_wallOcc) {
    // Checked against the bits, not trusted: MSVC /O2 has already folded one bool in this project.
    const unsigned bitsInf = 0x7F800000u, bitsMax = 0x7F7FFFFFu, bitsNan = 0x7FC00000u;
    const int okInf = span_not_finite(*(const float*)&bitsInf) ? 1 : 0;
    const int okMax = span_not_finite(*(const float*)&bitsMax) ? 0 : 1;
    const int okNan = span_not_finite(*(const float*)&bitsNan) ? 1 : 0;
    wt_log("nocull: finiteness predicate self-test -- +inf %s, +FLT_MAX %s, NaN %s%s",
           okInf ? "caught" : "MISSED", okMax ? "passed" : "WRONGLY CAUGHT",
           okNan ? "caught" : "MISSED",
           (okInf && okMax && okNan) ? "" : "  <- THE CLAMP CANNOT WORK");
    install_span();
  }
  wt_log("nocull: sweep %s (nocullsweep, default 1) -- every sector offered to the engine's own "
         "emit body instead of only the room the camera left", s_sweep ? "ON" : "off");
  wt_log("nocull: far test %s (nocullfar, default 1) -- the builder's 100000-unit distance cut, "
         "written for a camera inside the map; from outside it is what culls the far side of a "
         "level", s_keepFar ? "kept" : "SUPPRESSED while sweeping");
  wt_log("nocull: behind-the-axis test %s (nocullbehind, default 0) -- both endpoints behind the "
         "view axis; a near-plane rule and probably sound",
         s_keepBehind ? "kept" : "SUPPRESSED while sweeping");
  wt_log("nocull: one-sided wall rule %s (nocullwalls, default 1) -- it hides every wall facing "
         "away from the camera, which measured 816 on-screen surfaces a frame; suppressing it "
         "makes rooms solid boxes instead of see-through ones",
         s_keepOneSided ? "kept" : "SUPPRESSED while sweeping");
  wt_log("nocull: ceilings %s (nocullceilings, DEFAULT 0 as of the ship). On, the ceiling of "
         "every sector but the camera's own is dropped so interiors read from above; off, the "
         "map is drawn whole, which is the better picture now that the walls are right",
         s_dropCeilings ? "DROPPED" : "drawn");
  wt_log("nocull: wall exemption %s (nocullwallocclusion, default 3) -- a wall with no True3D "
         "mesh is drawn through a screen-space occlusion tree that only that population enters, "
         "and it assumes quads arrive front to back, which a camera outside the map breaks. "
         "Costs roughly half the frame rate; turn it off for speed and holes",
         s_wallOcc ? "ON while flying" : "off");
  wt_log("nocull: installed. rva 0x%X is FUN_007f9c80's seed call to the per-sector emit body "
         "at rva 0x%X; while the camera bracket is armed every sector is offered to that same "
         "body, and the engine's own visited set makes the ones the portal walk already reached "
         "free", kRvaSeedCall, kRvaScanSector);
  wt_log("nocull: budget %d masked-wall records, %d reserved per wall plus %d a sector; "
         "skip byte %s; the row is %s", NOCULLWT_MASKWALL_MAX, NOCULLWT_PER_WALL_RECORDS,
         NOCULLWT_SECTOR_MARGIN, s_buildOk ? "cleared while sweeping" : "NOT CLEARED",
         g_cfg.freecamnocull ? "on" : "off");
  return true;
}

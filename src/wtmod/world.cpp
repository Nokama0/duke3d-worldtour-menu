// See world.h. Four arrays resolved once and bounds-checked against the image, and a walk that cannot outrun its list.
#include <windows.h>
#include "world.h"
#include "esp_world.h"   // the pure layout, bounds and outline rules
#include "hook.h"   // wt_log

static const unsigned kRvaSprites        = 0xC86810;   // sprite[0], stride 0x2C
static const unsigned kRvaHeadSpriteStat = 0xE06240;   // short[MAXSTATUS + 1]
static const unsigned kRvaNextSpriteStat = 0xD7C230;   // short[MAXSPRITES]
// tilesizy's array ends where the sine table begins (0xD46A30 + 9216*2 = 0xD4B230) and tilesizx's at gotpic (0xE9F350 +
// 9216*2 = 0xEA3B50), which gives WORLD_MAXTILES; the facts note has the bases and neighbours, not the count.
static const unsigned kRvaSinTable       = 0xD4B230;   // short[2048], 16384-scaled
static const unsigned kRvaTileSizX       = 0xE9F350;   // short[MAXTILES]
static const unsigned kRvaTileSizY       = 0xD46A30;   // short[MAXTILES]
static const unsigned kRvaSectors        = 0xD4C230;   // sector[0], stride 0x28
static const unsigned kRvaWalls          = 0xD84230;   // wall[0], stride 0x20
static const unsigned kRvaNumSectors     = 0xEC0B60;   // short
static const unsigned kRvaNumWalls       = 0xD46A20;   // short
static const unsigned kRvaPlayers        = 0x5FD5C0;   // player[0], stride 0x408, 16 slots
static const unsigned kRvaLocalPlayer    = 0x7824CC;   // int32, the index processinput compares against
// hittype[], the per-sprite actor record parallel to sprite[] (dmgnumwt.cpp reads a victim's picnum/extra/owner from
// it): the classic 72-byte weaponhit layout (ten shorts, seven longs, temp_data[6]).
static const unsigned kRvaHitType        = 0x6107B0;   // hittype[0], stride 0x48
#define ACTOR_STRIDE    0x48
#define ACTOR_TEMPDATA  0x30   /* temp_data[0]; slot n is +4n from here */
#define ACTOR_BPOSX     0x24   /* bposx/y/z, the previous tic's position */
#define ACTOR_EXTRA     0x06   /* short: damage landed and waiting for the CON */

#define SPR_STRIDE   0x2C
#define SPR_X        0x00
#define SPR_Y        0x04
#define SPR_Z        0x08
#define SPR_CSTAT    0x0C
#define SPR_PICNUM   0x0E
#define SPR_PAL      0x11
#define SPR_XREPEAT  0x14
#define SPR_YREPEAT  0x15
#define SPR_SECTNUM  0x18
#define SPR_STATNUM  0x1A
#define SPR_XOFFSET  0x16   // signed char
#define SPR_YOFFSET  0x17   // signed char
#define SPR_ANG      0x1C   // short
#define SPR_OWNER    0x1E
#define SPR_LOTAG    0x26
#define SPR_HITAG    0x28
#define SPR_EXTRA    0x2A

static const unsigned char* s_sprites = 0;
static const short* s_headStat = 0;
static const short* s_nextStat = 0;
static const short* s_sin = 0;
static const short* s_tileX = 0;
static const short* s_tileY = 0;
static const unsigned char* s_sectors = 0;
static const unsigned char* s_walls = 0;
static const short* s_pNumSectors = 0;
static const short* s_pNumWalls = 0;
static const unsigned char* s_players = 0;
static const int* s_pLocalPlayer = 0;
static const unsigned char* s_actors = 0;
static int s_resolved = 0;   // 0 not tried, 1 usable, -1 refused for good
// Map arrays resolve in the same pass but fail alone: a build whose sector/wall/player addresses moved must still
// leave the sprite ESP working (esp_draw gates on world_ready(), the sprite half).
static int s_mapResolved = 0;

static const void* engine_ptr(const unsigned char* base, unsigned imageSize,
                              unsigned rva, unsigned size) {
  if (!base || rva > imageSize || size > imageSize - rva) {
    wt_log("world: rva 0x%X (%u bytes) is outside the image", rva, size);
    return 0;
  }
  return base + rva;
}

bool world_ready() {
  if (s_resolved) return s_resolved > 0;
  s_resolved = -1;
  const unsigned char* base = (const unsigned char*)GetModuleHandleA(0);
  if (!base) { wt_log("world: no module base"); return false; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("world: not a DOS image"); return false; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("world: not a PE image"); return false; }
  const unsigned size = nt->OptionalHeader.SizeOfImage;

  s_sprites  = (const unsigned char*)engine_ptr(base, size, kRvaSprites,
                                                WORLD_MAXSPRITES * SPR_STRIDE);
  s_headStat = (const short*)engine_ptr(base, size, kRvaHeadSpriteStat,
                                        (WORLD_MAXSTATUS + 1) * 2);
  s_nextStat = (const short*)engine_ptr(base, size, kRvaNextSpriteStat,
                                        WORLD_MAXSPRITES * 2);
  s_sin      = (const short*)engine_ptr(base, size, kRvaSinTable, 2048 * 2);
  s_tileX    = (const short*)engine_ptr(base, size, kRvaTileSizX, WORLD_MAXTILES * 2);
  s_tileY    = (const short*)engine_ptr(base, size, kRvaTileSizY, WORLD_MAXTILES * 2);
  if (!s_sprites || !s_headStat || !s_nextStat || !s_tileX || !s_tileY) {
    wt_log("world: an engine array did not fit a %u byte image; no sprite will be read", size);
    s_sprites = 0; s_headStat = 0; s_nextStat = 0; s_tileX = 0; s_tileY = 0;
    return false;
  }
  s_sectors     = (const unsigned char*)engine_ptr(base, size, kRvaSectors,
                                                   WORLD_MAXSECTORS * WORLD_SECTOR_STRIDE);
  s_walls       = (const unsigned char*)engine_ptr(base, size, kRvaWalls,
                                                   WORLD_MAXWALLS * WORLD_WALL_STRIDE);
  s_pNumSectors = (const short*)engine_ptr(base, size, kRvaNumSectors, 2);
  s_pNumWalls   = (const short*)engine_ptr(base, size, kRvaNumWalls, 2);
  s_players     = (const unsigned char*)engine_ptr(base, size, kRvaPlayers,
                                                   WORLD_MAXPLAYERS * WORLD_PLAYER_STRIDE);
  s_pLocalPlayer = (const int*)engine_ptr(base, size, kRvaLocalPlayer, 4);
  s_actors      = (const unsigned char*)engine_ptr(base, size, kRvaHitType,
                                                   WORLD_MAXSPRITES * ACTOR_STRIDE);
  if (!s_sectors || !s_walls || !s_pNumSectors || !s_pNumWalls
      || !s_players || !s_pLocalPlayer || !s_actors) {
    wt_log("world: a map or player array did not fit a %u byte image; "
           "no sector, wall or secret count will be read", size);
    s_sectors = 0; s_walls = 0; s_pNumSectors = 0; s_pNumWalls = 0;
    s_players = 0; s_pLocalPlayer = 0; s_actors = 0;
  } else {
    s_mapResolved = 1;
    wt_log("world: sectors at rva 0x%X (stride 0x%X, %d slots), walls at 0x%X "
           "(stride 0x%X, %d slots), counts at 0x%X/0x%X, players at 0x%X",
           kRvaSectors, WORLD_SECTOR_STRIDE, WORLD_MAXSECTORS,
           kRvaWalls, WORLD_WALL_STRIDE, WORLD_MAXWALLS,
           kRvaNumSectors, kRvaNumWalls, kRvaPlayers);
  }

  s_resolved = 1;
  wt_log("world: sprites at rva 0x%X (stride 0x%X, %d slots), stat heads at 0x%X (%d lists), "
         "links at 0x%X, tile sizes at 0x%X/0x%X (%d tiles)",
         kRvaSprites, SPR_STRIDE, WORLD_MAXSPRITES, kRvaHeadSpriteStat, WORLD_MAXSTATUS,
         kRvaNextSpriteStat, kRvaTileSizX, kRvaTileSizY, WORLD_MAXTILES);
  return true;
}

bool world_walk_begin(WorldWalk* w, int stat) {
  if (!w) return false;
  w->stat = stat;
  w->next = -1;
  w->steps = 0;
  if (!world_ready()) return false;
  if (stat < 0 || stat >= WORLD_MAXSTATUS) return false;
  const int head = s_headStat[stat];
  if (head < 0 || head >= WORLD_MAXSPRITES) return false;   // -1 is an empty list
  w->next = head;
  return true;
}

bool world_walk_next(WorldWalk* w, int* index) {
  if (!w || !index || w->next < 0) return false;
  if (!s_nextStat) return false;
  if (w->steps >= WORLD_MAXSPRITES) { w->next = -1; return false; }
  const int i = w->next;
  if (i < 0 || i >= WORLD_MAXSPRITES) { w->next = -1; return false; }
  w->steps++;
  *index = i;
  const int n = s_nextStat[i];
  w->next = (n >= 0 && n < WORLD_MAXSPRITES) ? n : -1;
  return true;
}

int world_walk_steps(const WorldWalk& w) { return w.steps; }

bool world_sprite(int index, WorldSprite* out) {
  if (!out) return false;
  const WorldSprite zero = { 0 };
  *out = zero;
  if (!s_sprites || index < 0 || index >= WORLD_MAXSPRITES) return false;
  const unsigned char* p = s_sprites + (unsigned)index * SPR_STRIDE;
  out->index   = index;
  out->x       = *(const int*)(p + SPR_X);
  out->y       = *(const int*)(p + SPR_Y);
  out->z       = *(const int*)(p + SPR_Z);
  // cstat's bit 15 is Build's invisible flag, so the short is read UNSIGNED.
  out->cstat   = *(const unsigned short*)(p + SPR_CSTAT);
  out->picnum  = *(const short*)(p + SPR_PICNUM);
  out->pal     = *(const unsigned char*)(p + SPR_PAL);
  out->xrepeat = *(const unsigned char*)(p + SPR_XREPEAT);
  out->yrepeat = *(const unsigned char*)(p + SPR_YREPEAT);
  out->sectnum = *(const short*)(p + SPR_SECTNUM);
  out->statnum = *(const short*)(p + SPR_STATNUM);
  out->ang     = *(const short*)(p + SPR_ANG);
  out->xoffset = *(const signed char*)(p + SPR_XOFFSET);
  out->yoffset = *(const signed char*)(p + SPR_YOFFSET);
  out->owner   = *(const short*)(p + SPR_OWNER);
  out->lotag   = *(const short*)(p + SPR_LOTAG);
  out->hitag   = *(const short*)(p + SPR_HITAG);
  out->extra   = *(const short*)(p + SPR_EXTRA);
  return true;
}

bool world_sprite_drawn(const WorldSprite& s) {
  if ((s.cstat & WORLD_CSTAT_INVISIBLE) != 0) return false;
  return s.xrepeat > 0 && s.yrepeat > 0;
}

const short* world_sintable() { return s_sin; }

int world_tilesizx(int picnum) {
  if (!s_tileX || picnum < 0 || picnum >= WORLD_MAXTILES) return 0;
  return s_tileX[picnum];
}
int world_tilesizy(int picnum) {
  if (!s_tileY || picnum < 0 || picnum >= WORLD_MAXTILES) return 0;
  return s_tileY[picnum];
}

// ------------------------------------------------------- sectors and walls
// Bounds rules are esp_world.h's and common.h's; counts come back clamped to the array maxima, so every index built
// from them is inside the real array.
int world_numsectors() {
  if (!s_mapResolved) return 0;
  const short n = *s_pNumSectors;
  return (n < 0 || n > WORLD_MAXSECTORS) ? 0 : n;
}

int world_numwalls() {
  if (!s_mapResolved) return 0;
  const short n = *s_pNumWalls;
  return (n < 0 || n > WORLD_MAXWALLS) ? 0 : n;
}

static const unsigned char* sector_ptr(int sectnum) {
  if (!s_mapResolved || !sector_index_ok(sectnum, world_numsectors())) return 0;
  return s_sectors + (unsigned)sectnum * WORLD_SECTOR_STRIDE;
}

int world_sector_lotag(int sectnum) {
  const unsigned char* sec = sector_ptr(sectnum);
  if (!sec) return -1;
  return *(const short*)(sec + SEC_LOTAG);
}

bool world_sector_wall_range(int sectnum, int* wallptr, int* wallnum) {
  if (!wallptr || !wallnum) return false;
  *wallptr = 0; *wallnum = 0;
  const unsigned char* sec = sector_ptr(sectnum);
  if (!sec) return false;
  const int wp = *(const short*)(sec + SEC_WALLPTR);
  const int wn = *(const short*)(sec + SEC_WALLNUM);
  if (!wall_range_ok(wp, wn, world_numwalls())) return false;
  *wallptr = wp;
  *wallnum = wn;
  return true;
}

bool world_sector_zrange(int sectnum, int* ceilingz, int* floorz) {
  if (!ceilingz || !floorz) return false;
  *ceilingz = 0; *floorz = 0;
  const unsigned char* sec = sector_ptr(sectnum);
  if (!sec) return false;
  *ceilingz = *(const int*)(sec + SEC_CEILINGZ);
  *floorz   = *(const int*)(sec + SEC_FLOORZ);
  return true;
}

bool world_sector_centre(int sectnum, int* cx, int* cy, int* cz) {
  if (!cx || !cy || !cz) return false;
  *cx = 0; *cy = 0; *cz = 0;
  const unsigned char* sec = sector_ptr(sectnum);
  if (!sec) return false;
  const int wp = *(const short*)(sec + SEC_WALLPTR);
  const int wn = *(const short*)(sec + SEC_WALLNUM);
  // The one loop here whose trip count comes from live memory: bounded by wallnum (wall_range_ok), and a sector whose
  // point2 chain doesn't close in its range is refused, so it can't spin in Present.
  const SectorCentre c = world_sector_centre_xy(s_walls, WORLD_WALL_STRIDE,
                                                wp, wn, world_numwalls());
  if (!c.ok) return false;
  *cx = c.x;
  *cy = c.y;
  *cz = *(const int*)(sec + SEC_FLOORZ);
  return true;
}

bool world_wall(int wallnum, WorldWall* out) {
  if (!out) return false;
  const WorldWall zero = { 0 };
  *out = zero;
  if (!s_mapResolved) return false;
  const int nw = world_numwalls();
  if (wallnum < 0 || wallnum >= nw || wallnum >= WORLD_MAXWALLS) return false;
  const unsigned char* wl = s_walls + (unsigned)wallnum * WORLD_WALL_STRIDE;
  out->index  = wallnum;
  out->x      = *(const int*)(wl + WAL_X);
  out->y      = *(const int*)(wl + WAL_Y);
  out->point2 = *(const short*)(wl + WAL_POINT2);
  out->nextsector = *(const short*)(wl + WAL_NEXTSECTOR);
  out->cstat      = *(const short*)(wl + WAL_CSTAT);
  out->overpicnum = *(const short*)(wl + WAL_OVERPICNUM);
  out->picnum = *(const short*)(wl + WAL_PICNUM);
  out->pal    = *(const unsigned char*)(wl + WAL_PAL);
  out->lotag  = *(const short*)(wl + WAL_LOTAG);
  out->hitag  = *(const short*)(wl + WAL_HITAG);
  return true;
}

bool world_sector_slope(int sectnum, SectorSlope* out) {
  if (!out) return false;
  const SectorSlope zero = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  *out = zero;
  const unsigned char* sec = sector_ptr(sectnum);
  if (!sec) return false;
  const int wallptr = *(const short*)(sec + SEC_WALLPTR);
  const int nw = world_numwalls();
  if (wallptr < 0 || wallptr >= nw || wallptr >= WORLD_MAXWALLS) return false;
  const unsigned char* w0 = s_walls + (unsigned)wallptr * WORLD_WALL_STRIDE;
  const int p2 = *(const short*)(w0 + WAL_POINT2);
  if (p2 < 0 || p2 >= nw || p2 >= WORLD_MAXWALLS) return false;
  const unsigned char* w1 = s_walls + (unsigned)p2 * WORLD_WALL_STRIDE;

  out->ceilingz      = *(const int*)(sec + SEC_CEILINGZ);
  out->floorz        = *(const int*)(sec + SEC_FLOORZ);
  out->ceilingheinum = *(const short*)(sec + SEC_CEILINGHEINUM);
  out->floorheinum   = *(const short*)(sec + SEC_FLOORHEINUM);
  out->ceilingSloped = (*(const short*)(sec + SEC_CEILINGSTAT) & SEC_STAT_SLOPED) ? 1 : 0;
  out->floorSloped   = (*(const short*)(sec + SEC_FLOORSTAT)   & SEC_STAT_SLOPED) ? 1 : 0;
  out->x0 = *(const int*)(w0 + WAL_X); out->y0 = *(const int*)(w0 + WAL_Y);
  out->x1 = *(const int*)(w1 + WAL_X); out->y1 = *(const int*)(w1 + WAL_Y);
  return true;
}

// ------------------------------------------------------- the actor table
// DAMAGE LANDED BUT NOT YET APPLIED: a weapon writes it into hittype[i].extra until the actor's CON reaches
// ifhitbyweapon (subtracts it from sprite[i].extra, sets -1): sprite[i].extra is stale meanwhile (applier 0x74EB38).
bool world_actor_pending_damage(int index, int* out) {
  if (!out || !s_actors) return false;
  if (index < 0 || index >= WORLD_MAXSPRITES) return false;
  *out = (int)*(const short*)(s_actors + (unsigned)index * ACTOR_STRIDE + ACTOR_EXTRA);
  return true;
}

bool world_actor_temp_data(int index, int slot, int* out) {
  if (!out || !s_actors) return false;
  if (index < 0 || index >= WORLD_MAXSPRITES) return false;
  if (slot < 0 || slot >= WORLD_ACTOR_TEMPDATA_SLOTS) return false;
  *out = *(const int*)(s_actors + (unsigned)index * ACTOR_STRIDE
                                + ACTOR_TEMPDATA + (unsigned)slot * 4);
  return true;
}

bool world_actor_bpos(int index, int* bx, int* by, int* bz) {
  if (!bx || !by || !bz) return false;
  *bx = 0; *by = 0; *bz = 0;
  if (!s_actors) return false;
  if (index < 0 || index >= WORLD_MAXSPRITES) return false;
  const unsigned char* a = s_actors + (unsigned)index * ACTOR_STRIDE;
  *bx = *(const int*)(a + ACTOR_BPOSX);
  *by = *(const int*)(a + ACTOR_BPOSX + 4);
  *bz = *(const int*)(a + ACTOR_BPOSX + 8);
  return true;
}

bool world_sprite_read_ang(int index, int* ang) {
  if (!ang) return false;
  *ang = 0;
  if (!s_sprites || index < 0 || index >= WORLD_MAXSPRITES) return false;
  *ang = (int)*(const short*)(s_sprites + (unsigned)index * SPR_STRIDE + SPR_ANG);
  return true;
}

bool world_sprite_write_ang(int index, int ang) {
  if (!s_sprites || index < 0 || index >= WORLD_MAXSPRITES) return false;
  *(short*)((unsigned char*)s_sprites + (unsigned)index * SPR_STRIDE + SPR_ANG) =
      (short)(ang & 2047);
  return true;
}

// THE MAP'S OWN SHADE BYTES, for fullbright: sector ceilingshade +0x14 / floorshade +0x1C, wall shade +0x14; they end
// up in g_paletteLookup.y, the pixel shader's SHADE ROW (patching the constant-buffer fill cannot work).
// Bounds-checked against the LIVE counts.
int world_sector_shade_count() {
  const int n = world_numsectors();
  return (n < 0 || n > WORLD_MAXSECTORS) ? 0 : n;
}

int world_wall_shade_count() {
  const int n = world_numwalls();
  return (n < 0 || n > WORLD_MAXWALLS) ? 0 : n;
}

bool world_sector_shade_rw(int sect, signed char* ceil, signed char* flor, bool write) {
  if (!s_sectors || sect < 0 || sect >= world_sector_shade_count()) return false;
  signed char* p = (signed char*)(s_sectors + (unsigned)sect * WORLD_SECTOR_STRIDE);
  if (write) { p[0x14] = *ceil; p[0x1C] = *flor; }
  else       { *ceil = p[0x14]; *flor = p[0x1C]; }
  return true;
}

bool world_wall_shade_rw(int wallnum, signed char* shade, bool write) {
  if (!s_walls || wallnum < 0 || wallnum >= world_wall_shade_count()) return false;
  signed char* p = (signed char*)(s_walls + (unsigned)wallnum * WORLD_WALL_STRIDE);
  if (write) p[0x14] = *shade;
  else       *shade = p[0x14];
  return true;
}

// A SPRITE'S OWN SHADE (spritetype +0x10, before pal), the third source of the shade row. Sprites can't be held
// (actors rewrite shade every tic, a recycled slot gets a dead sprite's shade), so lightwt.cpp borrows these around
// the draw.
bool world_sprite_shade_rw(int index, signed char* shade, bool write) {
  if (!s_sprites || !shade || index < 0 || index >= WORLD_MAXSPRITES) return false;
  signed char* p = (signed char*)(s_sprites + (unsigned)index * SPR_STRIDE + 0x10);
  if (write) *p = *shade;
  else       *shade = *p;
  return true;
}

// THE SHOOTER'S OWN BLOCKING BITS, cleared for one hitscan and put straight back, as shoot does itself before its
// hitscan (0072725E MOV EAX,0xfffffefe; 00727267 AND word [EBX+0xc],AX: cstat &= ~0x0101), else the ray stops on the
// shooter at distance 0. Same two bits, same moment, restored before anything else runs: the engine's own write.
bool world_sprite_clear_blocking(int index, int* savedCstat) {
  if (!s_sprites || !savedCstat || index < 0 || index >= WORLD_MAXSPRITES) return false;
  short* c = (short*)((unsigned char*)s_sprites + (unsigned)index * SPR_STRIDE + SPR_CSTAT);
  *savedCstat = (int)*c;
  *c = (short)(*c & (short)0xFEFE);
  return true;
}

bool world_sprite_restore_cstat(int index, int savedCstat) {
  if (!s_sprites || index < 0 || index >= WORLD_MAXSPRITES) return false;
  *(short*)((unsigned char*)s_sprites + (unsigned)index * SPR_STRIDE + SPR_CSTAT) =
      (short)savedCstat;
  return true;
}

// ------------------------------------------------------- the engine's cansee
// FUN_007600b0: x1 in ECX, y1 in EDX, six dwords pushed (z1 sect1 x2 y2 z2 sect2) and a bare RET, so the CALLER cleans
// 24 bytes. __fastcall is callee-clean and leaked stack until a level-load crash, hence the thunk.
static const unsigned kRvaCansee = 0xB00B0;
typedef int (*CanseeFn)(void);
static CanseeFn s_cansee = 0;

static int cansee_thunk(int x1, int y1, int z1, int sect1,
                        int x2, int y2, int z2, int sect2) {
  const void* fn = (const void*)s_cansee;
  int r = 0;
  __asm {
    mov  eax, sect2
    push eax
    mov  eax, z2
    push eax
    mov  eax, y2
    push eax
    mov  eax, x2
    push eax
    mov  eax, sect1
    push eax
    mov  eax, z1
    push eax
    mov  ecx, x1
    mov  edx, y1
    mov  eax, fn
    call eax
    add  esp, 24
    mov  r, eax
  }
  return r;
}
static int s_canseeResolved = 0;

bool world_cansee(int x1, int y1, int z1, int sect1,
                  int x2, int y2, int z2, int sect2) {
  if (!s_canseeResolved) {
    s_canseeResolved = 1;
    const unsigned char* base = (const unsigned char*)GetModuleHandleA(0);
    if (base) {
      const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
      if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE &&
            kRvaCansee < nt->OptionalHeader.SizeOfImage) {
          s_cansee = (CanseeFn)(void*)(base + kRvaCansee);
          wt_log("world: cansee resolved at rva 0x%X", kRvaCansee);
        }
      }
    }
    if (!s_cansee) wt_log("world: cansee did NOT resolve; no line of sight will be reported");
  }
  if (!s_cansee) return false;
  const int ns = world_numsectors();
  if (sect1 < 0 || sect1 >= ns || sect2 < 0 || sect2 >= ns) return false;
  return cansee_thunk(x1, y1, z1, sect1, x2, y2, z2, sect2) != 0;
}

// ------------------------------------------------------ the engine's hitscan
// FUN_00760300, compiled like cansee: x in ECX, y in EDX, twelve dwords pushed (z sect vx vy vz *hitsect *hitwall
// *hitsprite *hitx *hity *hitz cliptype) and a bare RET, so the caller drops 48 bytes. The short* outputs start at -1.
static const unsigned kRvaHitscan = 0xB0300;
static void* s_hitscan = 0;
static int s_hitscanResolved = 0;

bool world_hitscan(int x, int y, int z, int sect, int vx, int vy, int vz,
                   int* hitsect, int* hitwall, int* hitsprite,
                   int* hitx, int* hity, int* hitz) {
  if (!hitsect || !hitwall || !hitsprite || !hitx || !hity || !hitz) return false;
  *hitsect = -1; *hitwall = -1; *hitsprite = -1; *hitx = 0; *hity = 0; *hitz = 0;
  if (!s_hitscanResolved) {
    s_hitscanResolved = 1;
    const unsigned char* base = (const unsigned char*)GetModuleHandleA(0);
    if (base) {
      const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
      if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE && kRvaHitscan < nt->OptionalHeader.SizeOfImage)
          s_hitscan = (void*)(base + kRvaHitscan);
      }
    }
    wt_log("world: hitscan %s at rva 0x%X", s_hitscan ? "resolved" : "did NOT resolve",
           kRvaHitscan);
  }
  if (!s_hitscan) return false;
  const int ns = world_numsectors();
  if (sect < 0 || sect >= ns) return false;

  short hs = -1, hw = -1, hp = -1;
  int hx = 0, hy = 0, hz = 0;
  const void* fn = s_hitscan;
  const unsigned clip = WORLD_CLIPTYPE_SHOOT;
  short* phs = &hs; short* phw = &hw; short* php = &hp;
  int* phx = &hx; int* phy = &hy; int* phz = &hz;
  __asm {
    mov  eax, clip
    push eax
    mov  eax, phz
    push eax
    mov  eax, phy
    push eax
    mov  eax, phx
    push eax
    mov  eax, php
    push eax
    mov  eax, phw
    push eax
    mov  eax, phs
    push eax
    mov  eax, vz
    push eax
    mov  eax, vy
    push eax
    mov  eax, vx
    push eax
    mov  eax, sect
    push eax
    mov  eax, z
    push eax
    mov  ecx, x
    mov  edx, y
    mov  eax, fn
    call eax
    add  esp, 48
  }
  *hitsect = hs; *hitwall = hw; *hitsprite = hp;
  *hitx = hx; *hity = hy; *hitz = hz;
  return true;
}

#define P_SECRET_TOTAL 0x3EC   // int32, the level's own secret count
#define P_SECRET_FOUND 0x3F0   // int32, incremented by checksectors

bool world_player_secrets(int* found, int* total) {
  if (!found || !total) return false;
  *found = 0; *total = 0;
  if (!s_mapResolved) return false;
  const int snum = *s_pLocalPlayer;
  if (snum < 0 || snum >= WORLD_MAXPLAYERS) return false;
  const unsigned char* p = s_players + (unsigned)snum * WORLD_PLAYER_STRIDE;
  *found = *(const int*)(p + P_SECRET_FOUND);
  *total = *(const int*)(p + P_SECRET_TOTAL);
  return true;
}

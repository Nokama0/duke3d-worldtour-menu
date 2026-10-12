// Reading the level's sprites out of the running game by walking the STATUS LISTS, not all 16384 slots (two lists touch
// the level's tens of actors, not a 700KB array). Addresses/strides/offsets: docs/re/engine-facts.md 4.1/4.2. MAXSTATUS
// is 1024.
#ifndef WTMOD_WORLD_H
#define WTMOD_WORLD_H

const int WORLD_STAT_ACTOR       = 1;
const int WORLD_STAT_ZOMBIEACTOR = 2;

const int WORLD_MAXSPRITES = 16384;
const int WORLD_MAXSTATUS  = 1024;
const int WORLD_MAXTILES   = 9216;

const int      WORLD_MAXPLAYERS    = 16;
const unsigned WORLD_PLAYER_STRIDE = 0x408;

const int WORLD_CSTAT_INVISIBLE = 0x8000;

struct WorldSprite {
  int index;
  int x, y, z;
  int cstat;
  int picnum;
  int pal;
  int xrepeat, yrepeat;
  int ang;
  int xoffset, yoffset;
  int sectnum, statnum;
  int owner;
  int lotag;
  int hitag;
  int extra;
};

bool world_ready();

struct WorldWalk {
  int stat;
  int next;    // the index world_walk_next will hand back, or -1 when done
  int steps;   // cycle guard: a corrupt list must end, not spin in Present
};

bool world_walk_begin(WorldWalk* w, int stat);

bool world_walk_next(WorldWalk* w, int* index);

bool world_sprite(int index, WorldSprite* out);

bool world_sprite_drawn(const WorldSprite& s);

const short* world_sintable();
int world_tilesizx(int picnum);
int world_tilesizy(int picnum);

int world_walk_steps(const WorldWalk& w);

// Sectors and walls: every index is checked against BOTH the live count and the static array maximum, so a corrupt
// count cannot widen past the array.

int world_numsectors();
int world_numwalls();

int world_sector_lotag(int sectnum);

bool world_sector_centre(int sectnum, int* cx, int* cy, int* cz);

bool world_sector_wall_range(int sectnum, int* wallptr, int* wallnum);
bool world_sector_zrange(int sectnum, int* ceilingz, int* floorz);

struct WorldWall {
  int index;
  int x, y;
  int point2;   // the wall this one runs to; its vertex is the far end
  int nextsector;  // the sector on the far side, -1 when one-sided
  int cstat;       // bit 0 is the engine's clip bit: this wall blocks movement
  int overpicnum;  // the over picture; a breakable pane or vent is one of these
  int picnum;
  int pal;
  int lotag, hitag;
};
bool world_wall(int wallnum, WorldWall* out);

struct SectorSlope;
bool world_sector_slope(int sectnum, SectorSlope* out);

// Actor table hittype[]: the per-sprite actor record, parallel to sprite[], 16384 x 72 bytes; temp_data[6] (CON
// scratch) is its last 24.

// Damage landed on this actor but not yet applied to sprite.extra (its CON subtracts it at ifhitbyweapon and clears it
// to -1): sprite.extra alone shows health already spent. <= 0: none pending.
bool world_actor_pending_damage(int index, int* out);

const int WORLD_ACTOR_TEMPDATA_SLOTS = 6;
bool world_actor_temp_data(int index, int slot, int* out);

bool world_actor_bpos(int index, int* bx, int* by, int* bz);

// One sprite's angle, read and written: shoot AIMS FROM THE PLAYER'S OWN SPRITE in this fork (118 sprite-array
// references, none to the player array), so silent aim substitutes it around one shoot call.
bool world_sprite_read_ang(int index, int* ang);
bool world_sprite_write_ang(int index, int ang);

int  world_sector_shade_count();
int  world_wall_shade_count();
bool world_sector_shade_rw(int sect, signed char* ceil, signed char* flor, bool write);
bool world_wall_shade_rw(int wallnum, signed char* shade, bool write);

bool world_sprite_shade_rw(int index, signed char* shade, bool write);

bool world_sprite_clear_blocking(int index, int* savedCstat);
bool world_sprite_restore_cstat(int index, int savedCstat);

// The engine's cansee FUN_007600b0 is its own line of sight, called rather than approximated (8 params, eleven AI
// callers).
bool world_cansee(int x1, int y1, int z1, int sect1,
                  int x2, int y2, int z2, int sect2);


// The engine's shot mask: Build blocks on wall bit (cliptype & 0xFFFF) and sprite bit (>> 16). CLIPMASK1 is
// (256<<16)|64, the hitscan bits (shoot pushes 0x1000040); the movement mask 0x00010000 stopped rays on MASKWALL
// grates. Must equal common.h's rva::HITSCAN_CLIPTYPE; world.h includes nothing, so move both.
const unsigned WORLD_CLIPTYPE_SHOOT = 0x01000040;   // Duke's CLIPMASK1
bool world_hitscan(int x, int y, int z, int sect, int vx, int vy, int vz,
                   int* hitsect, int* hitwall, int* hitsprite,
                   int* hitx, int* hity, int* hitz);

// Secret counters (engine-facts.md 4.4: found +0x3F0, total +0x3EC). False, outputs zeroed, when unresolved. A total
// not filled in reads 0: shown as a bare count, not "3 of 0".
bool world_player_secrets(int* found, int* total);

#endif

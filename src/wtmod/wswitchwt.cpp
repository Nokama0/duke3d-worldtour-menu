// Weapon switch speed, engine half: reads one player field, asks the pure gate (wswitch.h), writes it back. wswitch_tic
// runs from wt_processinput's tail AFTER the original: the counter read is what the engine's step just left, the write
// is what the NEXT tic steps from.
#include <windows.h>
#include "wswitch.h"
#include "config.h"
#include "freecam.h"    // camera_active: the free camera's own bracket
#include "hook.h"

// World Tour's own offset: Megaton has weapon_pos at 0x33C with ang at 0x28C; here ang is at 0x2CC. Confirmed in the
// holster code (FUN_007500f0, 0x00750187: `holstered == 1 && *(short*)(p+0x388) == -9` -> 10), the -1 lowering start at
// 0x0074fe61 and the direct 10 for weapon 10.
#define P_WEAPON_POS 0x388   // int16
#define P_STRIDE     0x408

static const unsigned kRvaPlayers = 0x5FD5C0;   // player[0]
static const unsigned char* s_base = 0;
static int s_ready = 0;
static double s_carry = 0.0;

void wswitch_install(const unsigned char* base, unsigned imageSize) {
  if (!base) return;
  const unsigned last = kRvaPlayers + 15u * P_STRIDE + P_WEAPON_POS + 2u;
  if (last > imageSize) {
    wt_log("wswitch: player[15] + 0x%X does not fit the image; weapon switch speed stays off",
           P_WEAPON_POS);
    return;
  }
  s_base = base;
  s_ready = 1;
}

// Safe because wswitch_advance never moves a counter at rest (0) or off the flip value (-9), both the engine's alone,
// and never runs past -8 or 1.
void wswitch_tic(int snum, bool freezeActive) {
  if (!s_ready || snum < 0 || snum > 15) return;
  unsigned char* p = (unsigned char*)(s_base + kRvaPlayers + (unsigned)snum * P_STRIDE);
  const int pos = *(const short*)(p + P_WEAPON_POS);
  if (!wswitch_gate(s_ready != 0, true, freezeActive, camera_active(),
                    g_cfg.switchspeed, pos)) {
    s_carry = 0.0;
    return;
  }
  const int next = wswitch_advance(pos, firerate_steps(g_cfg.switchspeed, &s_carry));
  if (next != pos) *(short*)(p + P_WEAPON_POS) = (short)next;
}

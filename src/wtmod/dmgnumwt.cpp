// Floating damage numbers, the World Tour engine half; the rules are in dmgnum.h. ifhitbyweapon is FUN_006d2d90 (rva
// 0x22D90): __fastcall, one short in ECX; it reads hittype[i].extra, subtracts it from sprite[i].extra, clears extra
// and owner to -1, returns the weapon picnum (or -1). The values we need exist only BEFORE the original runs.

#include <windows.h>
#include "dmgnum.h"
#include "config.h"
#include "hook.h"
#include "canvas.h"
#include "project.h"
#include "esp_layout.h"   // esp_marker_in_range: the draw-distance rule every category asks
#include "wallclock.h"    // the unsigned millisecond clock dmgnum.h's ages are measured on
#include "font.h"         // font_height, FONT_UI
#include "ui.h"           // UI_SCALE_DEFAULT

static const unsigned kRvaIfHit = 0x22D90;    // FUN_006d2d90, ifhitbyweapon
static const unsigned char kFpIfHit[5]     = { 0x55, 0x8B, 0xEC, 0x51, 0x53 };
static const unsigned kIfHitPrologue       = 5;
static const unsigned char kFpIfHitTail[5] = { 0x0F, 0xBF, 0xC1, 0x56, 0x57 };
// The actor side table (Megaton: hittype; PDB: weaponhit). FUN_00723fc0 serialises it at size 0x120000 = 16384 x 72;
// field offsets confirmed at 0x0074EB38-0x0074EB65, where the damage applier writes all four.
static const unsigned kRvaHitType = 0x6107B0;
#define HITTYPE_STRIDE 72
#define HITTYPE_EXTRA   6      // short: the damage waiting to be applied
#define HITTYPE_OWNER   8      // short: who fired it
static const unsigned kRvaSprites = 0xC86810;
// The local player and its sprite, as camera.cpp resolves them: dmgnum_event_wanted wants damage the LOCAL PLAYER dealt
// (owner >= 0 && owner == localPlayerSprite).
static const unsigned kRvaPlayers   = 0x5FD5C0;   // player[0], stride 0x408
static const unsigned kRvaLocalPlyr = 0x7824CC;   // int
#define P_STRIDE 0x408
#define P_SPRITE 0x328     // int16, the player's own sprite index
#define SPR_STRIDE 0x2C
#define SPR_X      0x00
#define SPR_Y      0x04
#define SPR_Z      0x08
#define SPR_EXTRA  0x2A        // short: the victim's own health
#define DMG_MAXSPRITES 16384

typedef int (__fastcall* IfHitFn)(int spriteIndex);
static IfHitFn volatile s_orig = 0;
static const unsigned char* s_base = 0;
static unsigned s_imageSize = 0;
static int s_ok = 0;
static DmgRing s_ring;
static int s_loggedFirst = 0;
static int s_loggedEntry = 0;

static const unsigned char* dmg_engine_ptr(unsigned rva, unsigned size) {
  if (!s_base || !s_imageSize) return 0;
  if (rva >= s_imageSize || size > s_imageSize - rva) return 0;
  return s_base + rva;
}

static int hit_short(int i, unsigned off) {
  const unsigned char* p = dmg_engine_ptr(kRvaHitType + (unsigned)i * HITTYPE_STRIDE + off, 2);
  return p ? (int)*(const short*)p : 0;
}
static int spr_short(int i, unsigned off) {
  const unsigned char* p = dmg_engine_ptr(kRvaSprites + (unsigned)i * SPR_STRIDE + off, 2);
  return p ? (int)*(const short*)p : 0;
}
static int spr_int(int i, unsigned off) {
  const unsigned char* p = dmg_engine_ptr(kRvaSprites + (unsigned)i * SPR_STRIDE + off, 4);
  return p ? *(const int*)p : 0;
}

// The local player's sprite index, or -1 when unreadable.
static int local_player_sprite() {
  const unsigned char* pl = dmg_engine_ptr(kRvaLocalPlyr, 4);
  if (!pl) return -1;
  const int snum = *(const int*)pl;
  if (snum < 0 || snum > 15) return -1;
  const unsigned char* q = dmg_engine_ptr(kRvaPlayers + (unsigned)snum * P_STRIDE + P_SPRITE, 2);
  return q ? (int)*(const short*)q : -1;
}

static int __fastcall wt_ifhitbyweapon(int i) {
  IfHitFn orig = s_orig;
  while (!orig) { YieldProcessor(); orig = s_orig; }
  // Read BEFORE the original: it clears extra and owner on every path.
  if (g_cfg.dmgnumbers && s_ok && i >= 0 && i < DMG_MAXSPRITES) {
    const int damage = hit_short(i, HITTYPE_EXTRA);
    const int owner  = hit_short(i, HITTYPE_OWNER);
    const int health = spr_short(i, SPR_EXTRA);
    const int mine   = local_player_sprite();
    // Proves the hook is entered before any filter can reject the event, so "never called" and "called and filtered"
    // differ.
    if (!s_loggedEntry) {
      s_loggedEntry = 1;
      wt_log("dmgnum: hook entered -- victim sprite %d, damage %d, owner %d, victim health %d, "
             "local player sprite %d. The event is kept only when owner >= 0 and owner equals "
             "that last number, so a -1 there rejects every hit", i, damage, owner, health, mine);
    }
    // Passing -1 for the local sprite made owner >= 0 && owner == -1 unsatisfiable: every event rejected, nothing drew.
    if (dmgnum_event_wanted(owner, mine, health > 0, damage)) {
      DmgEvent ev;
      ev.x = spr_int(i, SPR_X);
      ev.y = spr_int(i, SPR_Y);
      ev.z = spr_int(i, SPR_Z);
      ev.damage = damage;
      ev.kill = (health - damage) <= 0;
      ev.victim = i;
      ev.t0 = wallclock_ms();
      ev.live = true;
      dmgnum_push(&s_ring, ev);
      if (!s_loggedFirst) {
        s_loggedFirst = 1;
        wt_log("dmgnum: first hit KEPT -- victim sprite %d, damage %d, owner %d, health %d "
               "before the hit. A damage of zero here would mean the +%d field is not what the "
               "disassembly said it was", i, damage, owner, health, HITTYPE_EXTRA);
      }
    }
  }
  return orig(i);
}

void dmgnum_draw(int screenW, int screenH) {
  if (!g_cfg.dmgnumbers || !s_ok) return;
  if (screenW <= 0 || screenH <= 0) return;
  if (!project_frame_begin()) return;   // no 3D frame this frame: draw nothing
  const unsigned now = wallclock_ms();
  // Its own canvas frame at the default scale: esp_draw opens and closes its own, so text drawn after it would use
  // whatever scale was last set.
  canvas_begin(screenW, screenH);
  canvas_set_scale(UI_SCALE_DEFAULT);
  for (int k = 0; k < DMGNUM_MAX; k++) {
    const DmgEvent& e = s_ring.e[k];
    if (!dmgnum_is_live(e, now)) continue;
    ScreenPt p;
    if (!project_world_to_screen(e.x, e.y, e.z, &p)) continue;
    if (!esp_marker_in_range(p, g_cfg.espdrawdist, g_cfg.espnolimit)) continue;
    char s[16];
    wsprintfA(s, "%d", e.damage);
    const double prog = dmgnum_progress(e.t0, now);
    const int textH = font_height[FONT_UI];
    const int rise = dmgnum_rise_px(prog, textH)
                   + dmgnum_stack_index(s_ring, k, now) * textH;
    const unsigned col = canvas_with_alpha(
        dmgnum_colour(e.kill, canvas_esp_colour(true), C_HAZARD), dmgnum_alpha(prog));
    canvas_text(p.x - canvas_text_width(s, FONT_UI) / 2, p.y - rise, s, col, FONT_UI);
  }
  canvas_end();
}

bool dmgnum_install() {
  s_base = (const unsigned char*)GetModuleHandleA(0);
  if (!s_base) { wt_log("dmgnum: no module base"); return false; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)s_base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("dmgnum: not a DOS image"); return false; }
  const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(s_base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("dmgnum: not a PE image"); return false; }
  s_imageSize = nt->OptionalHeader.SizeOfImage;
  dmgnum_reset(&s_ring);

  if (!dmg_engine_ptr(kRvaHitType, (unsigned)DMG_MAXSPRITES * HITTYPE_STRIDE) ||
      !dmg_engine_ptr(kRvaSprites, (unsigned)DMG_MAXSPRITES * SPR_STRIDE)) {
    wt_log("dmgnum: the actor table at rva 0x%X or the sprite array at rva 0x%X does not fit "
           "the image; damage numbers stay off", kRvaHitType, kRvaSprites);
    return false;
  }
  const unsigned char* at =
      dmg_engine_ptr(kRvaIfHit, kIfHitPrologue + (unsigned)sizeof(kFpIfHitTail));
  if (!at) { wt_log("dmgnum: rva 0x%X is outside the image", kRvaIfHit); return false; }
  if (memcmp(at, kFpIfHit, sizeof(kFpIfHit)) != 0) {
    wt_log("dmgnum: rva 0x%X is %02X %02X %02X %02X %02X, not the push ebp / mov ebp,esp / "
           "push ecx / push ebx we read; damage numbers stay off", kRvaIfHit,
           at[0], at[1], at[2], at[3], at[4]);
    return false;
  }
  if (memcmp(at + kIfHitPrologue, kFpIfHitTail, sizeof(kFpIfHitTail)) != 0) {
    const unsigned char* t = at + kIfHitPrologue;
    wt_log("dmgnum: rva 0x%X at +%u is %02X %02X %02X %02X %02X, not the movsx eax,cx / push "
           "esi / push edi we read; damage numbers stay off", kRvaIfHit, kIfHitPrologue,
           t[0], t[1], t[2], t[3], t[4]);
    return false;
  }
  void* tramp = 0;
  if (!hook_install(kRvaIfHit, kFpIfHit, sizeof(kFpIfHit), kIfHitPrologue,
                    (void*)wt_ifhitbyweapon, &tramp) || !tramp) {
    wt_log("dmgnum: the ifhitbyweapon hook did not take; damage numbers stay off");
    return false;
  }
  s_orig = (IfHitFn)tramp;
  s_ok = 1;
  wt_log("dmgnum: rva 0x%X is ifhitbyweapon, and the actor table is at rva 0x%X, stride %d, "
         "damage at +%d and owner at +%d. Nothing engine-side is written: the hook copies the "
         "record before the original clears it", kRvaIfHit, kRvaHitType, HITTYPE_STRIDE,
         HITTYPE_EXTRA, HITTYPE_OWNER);
  return true;
}

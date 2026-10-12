// Automatic weapon switching, engine half: the rules are pure (autoswitch.h); this reads the player, calls the original
// and puts back what the rule says should not have changed. The grant (addammo 0x20F50, gotweapon at 0x20F9E) is never
// touched.
#include <windows.h>
#include <intrin.h>   // _ReturnAddress
#include "autoswitch.h"
#include "config.h"
#include "hook.h"

// addweapon (0x20F90): the NEW PICKUP switch (CON VM at 0x650DA and four other callers; switches at 0x20FB5..0x20FFD).
// checkavailweapon (0x21120): the AMMO EMPTY switch (eleven call sites; writes curr_weapon at 0x211EC). Both write five
// fields: curr_weapon alone leaves the HUD weapon mid-raise.
static const unsigned kRvaAddWeapon        = 0x20F90;
static const unsigned kRvaCheckAvailWeapon = 0x21120;
static const unsigned kRvaPlayers          = 0x5FD5C0;
static const unsigned kRvaLocalPlayer      = 0x7824CC;   // int: my player index
static const unsigned kRvaWChoice          = 0x158FCE0 - 0x6B0000;   // ud.wchoice

#define P_STRIDE      0x408
#define P_CURR_WEAPON 0x2FA   // int16
#define P_LAST_WEAPON 0x2FC   // int16
#define P_WANTFIRE    0x302   // int16, a weapon key queued during a fire cycle
#define P_KICKBACK    0x31C   // int16
#define P_CLUBFRAME   0x32E   // int16
#define P_WEAPON_POS  0x388   // int16
#define P_HOLSTER     0x3C8   // int8
#define P_SPRITE      0x328   // int16

#define WCHOICE_STRIDE_INTS 13   // ud.wchoice is 13 ints a player, stride 0x34

// push esi; movsx esi,dx; cmp byte [esi+ecx+0x3CA], 0
static const unsigned char kFpAddWeapon[] = {
  0x56, 0x0F, 0xBF, 0xF2, 0x80, 0xBC, 0x0E, 0xCA, 0x03, 0x00, 0x00, 0x00
};
// TWELVE: hook_install refuses a fingerprint longer than the prologue, and 7 refused BOTH hooks; 7 also splits the
// 8-byte `cmp` (0x20F90 has boundaries at 1, 4, 12). 12 is a boundary at both sites.
static const unsigned kAddWeaponPrologue = 12;

// movzx eax,word [ecx+0x302]; push ebx; push esi; or ebx,-1
static const unsigned char kFpCheckAvail[] = {
  0x0F, 0xB7, 0x81, 0x02, 0x03, 0x00, 0x00, 0x53, 0x56, 0x83, 0xCB, 0xFF
};
static const unsigned kCheckAvailPrologue = 12;   // 7/8/9/12 are all boundaries

typedef void (__fastcall* AddWeaponFn)(void* player, int weapon);
typedef void (__fastcall* CheckAvailFn)(void* player);

static unsigned char* s_base = 0;
static AddWeaponFn    s_origAdd = 0;
static CheckAvailFn   s_origAvail = 0;
static bool           s_live = false;
// Raised during the empty-switch hook: checkavailweapon reaches addweapon by a TAIL JMP at 0x21163, so the return
// address names no pickup.
static int            s_inCheckAvail = 0;
// The "pickup" call sites: the CON VM's addweapon opcode (0x650DA) and the knee re-switch (0x64F70); the return address
// is the instruction AFTER each 5-byte call. An ALLOWLIST: the weapon-select dispatch calls addweapon too
// (0x9FEB6..0xA00AB).
static const unsigned kRetConPickup  = 0x650DA + 5;
static const unsigned kRetKneeSwitch = 0x64F70 + 5;
static int            s_preferenceApplied = 0;

static int local_player_index() {
  if (!s_base) return -1;
  const int n = *(const int*)(s_base + kRvaLocalPlayer);
  return (n >= 0 && n <= 15) ? n : -1;
}

// THE HOOKS RUN FOR EVERY PLAYER, and ud.wchoice goes into the net packet: suppressing someone else's weapon would
// desync them.
static bool is_local(void* player) {
  const int n = local_player_index();
  if (n < 0 || !player) return false;
  return (unsigned char*)player == s_base + kRvaPlayers + (unsigned)n * P_STRIDE;
}

static WeaponSwitchState read_state(void* p) {
  WeaponSwitchState s = {};
  const unsigned char* q = (const unsigned char*)p;
  s.currWeapon      = *(const short*)(q + P_CURR_WEAPON);
  s.lastWeapon      = *(const short*)(q + P_LAST_WEAPON);
  s.weaponPos       = *(const short*)(q + P_WEAPON_POS);
  s.kickbackPic     = *(const short*)(q + P_KICKBACK);
  s.randomClubFrame = *(const short*)(q + P_CLUBFRAME);
  s.holster         = *(const unsigned char*)(q + P_HOLSTER);
  return s;
}

static void write_state(void* p, const WeaponSwitchState& s) {
  unsigned char* q = (unsigned char*)p;
  *(short*)(q + P_CURR_WEAPON) = (short)s.currWeapon;
  *(short*)(q + P_LAST_WEAPON) = (short)s.lastWeapon;
  *(short*)(q + P_WEAPON_POS)  = (short)s.weaponPos;
  *(short*)(q + P_KICKBACK)    = (short)s.kickbackPic;
  *(short*)(q + P_CLUBFRAME)   = (short)s.randomClubFrame;
  *(unsigned char*)(q + P_HOLSTER) = (unsigned char)s.holster;
}

static bool caller_is_pickup(const void* ret) {
  if (!ret || !s_base) return false;
  const unsigned rva = (unsigned)((const unsigned char*)ret - s_base);
  return rva == kRetConPickup || rva == kRetKneeSwitch;
}

static void __fastcall wt_add_weapon(void* player, int weapon) {
  AddWeaponFn orig = s_origAdd;
  while (!orig) { YieldProcessor(); orig = s_origAdd; }

  const bool mine = is_local(player);
  const bool guard = mine && !s_inCheckAvail &&
                     caller_is_pickup(_ReturnAddress()) &&
                     !autoswitch_pickup_allowed(g_cfg.wswitchauto, g_cfg.wswitchonpickup);
  if (!guard) { orig(player, weapon); return; }

  const WeaponSwitchState before = read_state(player);
  orig(player, weapon);
  const WeaponSwitchState after = read_state(player);
  if (weapon_switch_changed(before, after)) write_state(player, before);
}

static void __fastcall wt_check_avail_weapon(void* player) {
  CheckAvailFn orig = s_origAvail;
  while (!orig) { YieldProcessor(); orig = s_origAvail; }

  if (!is_local(player)) { orig(player); return; }

  if (!s_preferenceApplied) { s_preferenceApplied = 1; autoswitch_apply_preference(); }

  const int wantFire = *(const short*)((const unsigned char*)player + P_WANTFIRE);
  const WeaponSwitchState before = read_state(player);

  s_inCheckAvail++;
  orig(player);
  s_inCheckAvail--;

  const WeaponSwitchState after = read_state(player);
  if (!autoswitch_restore_after_empty(g_cfg.wswitchauto, g_cfg.wswitchonempty,
                                      wantFire, after.currWeapon))
    return;
  if (weapon_switch_changed(before, after)) write_state(player, before);
}

// The preferred fallback goes to the FRONT of the engine's list.
void autoswitch_apply_preference() {
  if (!s_live) return;
  const int weapon = autoswitch_prefer_weapon(g_cfg.wswitchprefer);
  if (weapon < 0) return;            // engine default: leave the order alone
  const int n = local_player_index();
  if (n < 0) return;
  int* wchoice = (int*)(s_base + kRvaWChoice) + n * WCHOICE_STRIDE_INTS;
  const int want = autoswitch_wchoice_value(weapon);
  // On the DECODED weapon: the engine reads 0 and 10 as weapon 9, so a raw compare would slide a duplicate over the tail.
  if (autoswitch_wchoice_weapon(wchoice[0]) == weapon) return;   // already first

  int at = -1;
  for (int k = 0; k < AUTOSWITCH_WEAPON_COUNT; k++)
    if (autoswitch_wchoice_weapon(wchoice[k]) == weapon) { at = k; break; }
  if (at < 0) at = AUTOSWITCH_WEAPON_COUNT - 1;
  for (int k = at; k > 0; k--) wchoice[k] = wchoice[k - 1];
  wchoice[0] = want;
}

bool autoswitch_install() {
  s_base = (unsigned char*)GetModuleHandleA(0);
  if (!s_base) { wt_log("autoswitch: no module base"); return false; }

  // CHECKAVAILWEAPON FIRST: its hook raises the flag that stands the pickup hook down during the tail-JMP.
  void* trampAvail = 0;
  if (!hook_install(kRvaCheckAvailWeapon, kFpCheckAvail, (unsigned)sizeof(kFpCheckAvail),
                    kCheckAvailPrologue, (void*)wt_check_avail_weapon, &trampAvail) ||
      !trampAvail) {
    wt_log("autoswitch: the checkavailweapon hook at rva 0x%X did not take; "
           "automatic weapon switching stays as the game does it",
           kRvaCheckAvailWeapon);
    return false;
  }
  s_origAvail = (CheckAvailFn)trampAvail;
  // LIVE AS SOON AS THE EMPTY HALF IS: set later, a checkavailweapon in between burned the one-shot preference.
  s_live = true;

  void* trampAdd = 0;
  if (!hook_install(kRvaAddWeapon, kFpAddWeapon, (unsigned)sizeof(kFpAddWeapon),
                    kAddWeaponPrologue, (void*)wt_add_weapon, &trampAdd) || !trampAdd) {
    wt_log("autoswitch: the addweapon hook at rva 0x%X did not take; the "
           "ammo-empty half is live and the pickup half stays the game's",
           kRvaAddWeapon);
    return true;
  }
  s_origAdd = (AddWeaponFn)trampAdd;

  wt_log("autoswitch: installed. addweapon rva 0x%X (pickup), checkavailweapon "
         "rva 0x%X (ammo empty), preference list ud.wchoice rva 0x%X. "
         "auto=%d pickup=%d empty=%d prefer=%s",
         kRvaAddWeapon, kRvaCheckAvailWeapon, kRvaWChoice,
         g_cfg.wswitchauto, g_cfg.wswitchonpickup, g_cfg.wswitchonempty,
         autoswitch_prefer_name(g_cfg.wswitchprefer));
  return true;
}

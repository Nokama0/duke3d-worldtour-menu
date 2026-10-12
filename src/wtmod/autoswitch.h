#pragma once
// Whether the game may change your weapon for you. The fallback order is the engine's: ud.wchoice is Duke's preference
// list, which checkavailweapon walks to the first weapon with ammo; the mod writes the preference to its front. It
// decodes `if (v == 0) v = 9; else v--`, so stored 1 is weapon 0 and stored 0 is weapon 9; only 0..9 are reachable.

// curr_weapon indices, as tbot.h lists them. Only 0..9 can be a wchoice entry.
const int AUTOSWITCH_WEAPON_COUNT = 10;

// Index 0 is "leave it alone" and must stay the default.
const int AUTOSWITCH_PREFER_DEFAULT = 0;
const int AUTOSWITCH_PREFER_MAX     = AUTOSWITCH_WEAPON_COUNT;   // 1..10 pick a weapon

inline int autoswitch_prefer_clamped(int v) {
  return (v < 0 || v > AUTOSWITCH_PREFER_MAX) ? AUTOSWITCH_PREFER_DEFAULT : v;
}

inline const char* autoswitch_prefer_name(int v) {
  switch (autoswitch_prefer_clamped(v)) {
    case 1:  return "Mighty Foot";
    case 2:  return "Pistol";
    case 3:  return "Shotgun";
    case 4:  return "Chaingun";
    case 5:  return "RPG";
    case 6:  return "Pipe bomb";
    case 7:  return "Shrinker";
    case 8:  return "Devastator";
    case 9:  return "Trip bomb";
    case 10: return "Freezer";
    default: return "Engine default";
  }
}

// The menu value as a curr_weapon index, or -1 for "leave it alone".
inline int autoswitch_prefer_weapon(int v) {
  const int c = autoswitch_prefer_clamped(v);
  return c == AUTOSWITCH_PREFER_DEFAULT ? -1 : c - 1;
}

// The inverse of the engine's decode; weapon 9 round-trips through 10 (not misread as "unset"). Compare decoded
// weapons, never stored ints: the engine reads 0 and 10 as one weapon.
inline int autoswitch_wchoice_weapon(int stored) {
  return stored == 0 ? AUTOSWITCH_WEAPON_COUNT - 1 : stored - 1;
}

inline int autoswitch_wchoice_value(int weapon) {
  if (weapon < 0 || weapon >= AUTOSWITCH_WEAPON_COUNT) return 0;
  return weapon + 1;
}

// Master off means neither event is allowed and the sub-options stop mattering.
inline bool autoswitch_pickup_allowed(int autoOn, int onPickup) {
  return autoOn != 0 && onPickup != 0;
}
inline bool autoswitch_empty_allowed(int autoOn, int onEmpty) {
  return autoOn != 0 && onEmpty != 0;
}

// checkavailweapon first services `wantweaponfire` (a key queued DURING a fire cycle), then the ammo-empty fallback, in
// one curr_weapon write; decide from the RESULT: an unowned or dry queued weapon falls through.
const int AUTOSWITCH_NO_REQUEST = -1;
inline bool autoswitch_restore_after_empty(int autoOn, int onEmpty,
                                           int wantWeaponFire, int chosenWeapon) {
  if (autoswitch_empty_allowed(autoOn, onEmpty)) return false;   // allowed: leave it
  if (wantWeaponFire >= 0 && wantWeaponFire == chosenWeapon) return false;
  return true;
}

// Every field addweapon writes; restoring curr_weapon alone leaves the HUD weapon mid-raise.
struct WeaponSwitchState {
  int currWeapon, lastWeapon, weaponPos, kickbackPic, randomClubFrame, holster;
};
// All six: picking up a weapon you hold rewrites four fields while curr_weapon stays, so a weapon-only test skips the
// restore.
inline bool weapon_switch_changed(const WeaponSwitchState& a, const WeaponSwitchState& b) {
  return a.currWeapon != b.currWeapon || a.lastWeapon != b.lastWeapon ||
         a.weaponPos != b.weaponPos || a.kickbackPic != b.kickbackPic ||
         a.randomClubFrame != b.randomClubFrame || a.holster != b.holster;
}

bool autoswitch_install();
void autoswitch_apply_preference();

// Switches, doors and locks: which tile is one, its family, and the extra marker line ("Needs red card", "Flip"). Pure
// and header-only; sprite and wall switches share one rule. Ported from Megaton's sets and re-checked against this
// game's names.h (build "Pagoda-DEF" 386): 18 two-frame bases, 4 MULTISWITCH frames, NUKEBUTTON, 22 DOORTILE*.
#ifndef WTMOD_ESP_SWITCH_H
#define WTMOD_ESP_SWITCH_H

#include "canvas.h"     // C_BLUE/C_RED/C_HAZARD: keycard_colour returns these named constants
#include "esp_pics.h"   // ESP_PIC_ACCESSCARD, ESP_PAL_KEY_*

// SW_DOOR is a door-tile "usable" wall or sprite with no pressed frame, so it has its own label. SW_EXIT is the
// NUKEBUTTON end-level switch; its `line` carries its own label.
enum SwitchFamily { SW_NONE = 0, SW_ACCESS = 1, SW_TWOSTATE = 2, SW_MULTI = 3,
                    SW_COMBINATION = 4, SW_DOOR = 5, SW_EXIT = 6 };
struct SwitchInfo { SwitchFamily family; int base; const char* line; int exitKind; };

const char* const ESP_SWITCH_LABEL = "Switch";
const char* const ESP_DOOR_LABEL   = "Usable";

// NUKEBUTTON 142 (names.h). On the sprite: pal 0 with lotag 65535 ends the level, pal 0 with lotag 32767 the episode, a
// non-zero pal with a small lotag is the secret exit (the lotag names the level). Sector lotags play no part.
const int ESP_PIC_NUKEBUTTON = 142;
enum { EXIT_NONE = 0, EXIT_NORMAL = 1, EXIT_SECRET = 2 };
inline int exit_kind_for(int picnum, int pal, int lotag) {
  if (picnum != ESP_PIC_NUKEBUTTON) return EXIT_NONE;
  if (pal != 0 && lotag > 0 && lotag < 32767) return EXIT_SECRET;
  if (pal == 0 && (lotag == 65535 || lotag == 32767)) return EXIT_NORMAL;
  return EXIT_NONE;
}

const int ESP_STAT_SWITCH = 0;
const int ESP_STAT_NUKEBUTTON = 5;

const int SWITCH_MULTI_BASE   = 146;
const int SWITCH_MULTI_FRAMES = 4;

// The twenty-two DOORTILE* tiles: a door-textured wall or sprite with a nonzero lotag fires its channel when pressed,
// with no switch frame to flip.
inline bool isadoorwall(int picnum) {
  if (picnum >= 150 && picnum <= 159) return true;   // DOORTILE5,6,1,2,3,4,7,8,9,10
  if (picnum == 395) return true;                    // DOORTILE22
  if (picnum >= 447 && picnum <= 449) return true;   // DOORTILE18,19,20
  if (picnum == 717) return true;                    // DOORTILE14
  if (picnum == 781) return true;                    // DOORTILE16
  if (picnum == 1102) return true;                   // DOORTILE15
  if (picnum == 1144) return true;                   // DOORTILE21
  if (picnum == 1169) return true;                   // DOORTILE17
  if (picnum == 1178 || picnum == 1179) return true; // DOORTILE11,12
  if (picnum == 4391) return true;                   // DOORTILE23
  return false;
}

inline int switch_base_for(int picnum) {
  // Eighteen two-frame families (picnum toggles between base and base + 1), per names.h: ACCESSSWITCH, SLOTDOOR,
  // LIGHTSWITCH, SPACEDOORSWITCH, SPACELIGHTSWITCH, FRANKENSTINESWITCH, DIPSWITCH, DIPSWITCH2, TECHSWITCH, DIPSWITCH3,
  // ACCESSSWITCH2, LIGHTSWITCH2, POWERSWITCH1, LOCKSWITCH1, POWERSWITCH2, HANDSWITCH, PULLSWITCH, ALIENSWITCH.
  static const int BASES[] = {
    130, 132, 134, 136, 138, 140, 162, 164, 166, 168, 170,
    712, 860, 862, 864, 1111, 1122, 1142
  };
  if (picnum >= SWITCH_MULTI_BASE && picnum < SWITCH_MULTI_BASE + SWITCH_MULTI_FRAMES)
    return SWITCH_MULTI_BASE;
  for (int i = 0; i < (int)(sizeof(BASES) / sizeof(BASES[0])); i++)
    if (picnum == BASES[i] || picnum == BASES[i] + 1) return BASES[i];
  if (isadoorwall(picnum)) return picnum;
  return -1;
}

inline SwitchInfo switch_classify(int picnum, int pal, int hitag, int lotag, int gotAccess) {
  SwitchInfo s = { SW_NONE, -1, 0, EXIT_NONE };
  const int exitKind = exit_kind_for(picnum, pal, lotag & 0xFFFF);
  if (exitKind != EXIT_NONE) {
    s.family = SW_EXIT;
    s.exitKind = exitKind;
    s.line = (exitKind == EXIT_SECRET) ? "Secret exit" : "Exit";
    return s;
  }
  const int base = switch_base_for(picnum);
  if (base < 0) return s;
  // The game ignores a lotag-0 switch outright, so a marker on one would mark nothing. lotag -1 is the end-of-level
  // switch and stays.
  if (lotag == 0) return s;
  s.base = base;
  if (base == 130 || base == 170) {
    s.family = SW_ACCESS;
    if (picnum == base + 1) return s;
    int bit = 0;
    const char* need = 0;
    if      (pal == 0)  { bit = 1; need = "Needs blue card";   }
    else if (pal == 21) { bit = 2; need = "Needs red card";    }
    else if (pal == 23) { bit = 4; need = "Needs yellow card"; }
    if (need && (gotAccess & bit) == 0) s.line = need;
    return s;
  }
  if (base == 162 || base == 166 || base == 1142) {
    s.family = SW_COMBINATION;
    const int want = hitag != 0 ? 1 : 0;
    s.line = (((picnum - base) & 1) != want) ? "Flip" : "Leave";
    return s;
  }
  if (isadoorwall(picnum)) {
    s.family = SW_DOOR;
    return s;
  }
  s.family = (base == SWITCH_MULTI_BASE) ? SW_MULTI : SW_TWOSTATE;
  return s;
}

// "Usable" for a door tile, "Switch" for every other family. SW_EXIT never reaches this: its label is SwitchInfo::line.
inline const char* switch_label(SwitchFamily family) {
  return family == SW_DOOR ? ESP_DOOR_LABEL : ESP_SWITCH_LABEL;
}

inline unsigned keycard_colour(int pal) {
  if (pal == ESP_PAL_KEY_BLUE)   return C_BLUE;
  if (pal == ESP_PAL_KEY_RED)    return C_RED;
  if (pal == ESP_PAL_KEY_YELLOW) return C_HAZARD;
  return 0;
}

// Whether a wall is worth a mark slot at scan time: switch_classify refuses lotag == 0 for every family, so an ordinary
// door could otherwise fill the cache before a real switch. lotag -1 is capturable, so this is `!= 0`.
inline bool esp_wall_switch_capturable(int picnum, int lotag) {
  return switch_base_for(picnum) >= 0 && lotag != 0;
}

#endif

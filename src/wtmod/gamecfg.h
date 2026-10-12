// Verbatim from Megaton's src/duke3dmod/gamecfg.h; kept identical on purpose -- do not edit here.
#pragma once
#include "bind.h"
#include <string.h>

// The game's own key bindings (`duke3d.cfg`, [KeyDefinitions]), so the panel can warn when a key it binds is also the
// game's. Truncation of a large file can only miss a warning, never give a wrong one.
const int GAMECFG_MAX = 16384;

// The game's name for a key, or null (also for codes common.h won't let the panel bind). Table from bin\duke3d.exe:
// {int scancode; const char* name;} pairs at file offset 0x1A9EA8, indexed by SDL scancode through "GP RStick"; names
// packed from 0x1A9B00 ("None") to 0x1A9E98, except "A" at 0x1A91FC.
inline const char* gamecfg_key_name(int vk) {
  // Letters and keypad digits are named as themselves; arrays beat 26 labels (VK A-Z are ASCII).
  static const char* const kLetters[26] = {
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"
  };
  if (vk >= 0x41 && vk <= 0x5A) return kLetters[vk - 0x41];

  static const char* const kDigits[10] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
  };
  if (vk >= 0x30 && vk <= 0x39) return kDigits[vk - 0x30];

  static const char* const kKeypadDigits[10] = {
    "KPad 0", "KPad 1", "KPad 2", "KPad 3", "KPad 4",
    "KPad 5", "KPad 6", "KPad 7", "KPad 8", "KPad 9"
  };
  if (vk >= 0x60 && vk <= 0x69) return kKeypadDigits[vk - 0x60];

  switch (vk) {
    // T and V are letters (kLetters covers them).
    case 0x09: return "Tab";
    case 0xC0: return "~";   // VK_OEM_3, not a backtick
    case 0x70: return "F1";
    case 0x71: return "F2";
    case 0x72: return "F3";
    case 0x73: return "F4";
    case 0x74: return "F5";
    case 0x75: return "F6";
    case 0x76: return "F7";
    case 0x77: return "F8";
    case 0x78: return "F9";
    case 0x79: return "F10";
    case 0x7A: return "F11";
    case 0x7B: return "F12";
    case 0x2D: return "Insert";
    case 0x24: return "Home";
    case 0x21: return "PgUp";
    case 0x2E: return "Delete";
    case 0x23: return "End";
    case 0x22: return "PgDown";
    case 0x0D: return "Enter";
    case 0x08: return "Backspace";
    case 0x20: return "Space";
    case 0xBD: return "-";
    case 0xBB: return "=";
    case 0xDB: return "[";
    case 0xDD: return "]";
    case 0xDC: return "\\";
    case 0xBA: return ";";
    case 0xDE: return "'";
    case 0xBC: return ",";
    case 0xBE: return ".";
    case 0xBF: return "/";
    case 0x13: return "Break";
    case 0x27: return "Right";
    case 0x25: return "Left";
    case 0x28: return "Down";
    case 0x26: return "Up";
    case 0x6F: return "KP /";
    case 0x6A: return "KP *";
    case 0x6D: return "KP -";
    case 0x6B: return "KP +";
    case 0x6E: return "KP .";
    case VK_BIND_LBUTTON:  return "Mouse 1";
    case VK_BIND_RBUTTON:  return "Mouse 2";
    case VK_BIND_MBUTTON:  return "Mouse 3";
    case VK_BIND_XBUTTON1: return "Mouse 4";
    case VK_BIND_XBUTTON2: return "MOUSE 5";   // the game's own capitals
    default: return nullptr;
  }
}

// ASCII lower, hand rolled: tolower is locale-dependent and undefined outside unsigned char.
inline char gamecfg_lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Whether a line is this section header, case-insensitively, ignoring trailing whitespace/CR (read in binary).
inline bool gamecfg_is_section(const char* line, const char* name) {
  int i = 0;
  for (; name[i]; i++) {
    if (!line[i]) return false;
    if (gamecfg_lower(line[i]) != gamecfg_lower(name[i])) return false;
  }
  for (; line[i] && line[i] != '\n'; i++) {
    if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r') return false;
  }
  return true;
}

// Whether the game's cfg binds this key. Only [KeyDefinitions] is scanned ([Comm Setup] holds the player's name,
// [Controls] has lines whose keys read like button names). Whole quoted token, case-insensitive ("T" must not
// match "GP RT", "F1" not "F12"; the game writes `MOUSE 5`/`Mouse 4` inconsistently).
inline bool gamecfg_key_bound(const char* text, int vk) {
  const char* name = gamecfg_key_name(vk);
  if (!text || !name) return false;
  const size_t nameLen = strlen(name);
  if (!nameLen) return false;

  const char* p = text;
  bool inSection = false;
  while (*p) {
    if (*p == '[') {
      // A new header ends the previous section; the last section runs to end of file.
      inSection = gamecfg_is_section(p, "[KeyDefinitions]");
    } else if (inSection) {
      const char* q = p;
      while (*q && *q != '\n') {
        if (*q == '"') {
          const char* tok = q + 1;
          const char* end = tok;
          while (*end && *end != '"' && *end != '\n') end++;
          if (*end != '"') break;             // unterminated: this line is junk
          if ((size_t)(end - tok) == nameLen) {
            bool same = true;
            for (size_t i = 0; i < nameLen; i++) {
              if (gamecfg_lower(tok[i]) != gamecfg_lower(name[i])) { same = false; break; }
            }
            if (same) return true;
          }
          q = end + 1;
          continue;
        }
        q++;
      }
    }
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
  }
  return false;
}

#pragma once
// Thumb buttons bound to a game action: Duke's config parser stores "MouseButton%i" for five slots only (rva 0x32187).
// The getinput hook ORs the action's bit into the record's `bits` (+8).

const int MB_ACTION_COUNT = 7;

// Index 0 is off and must stay 0. Bit numbers are measured (classic Duke's quick kick (19) differs in this fork) by
// logging mask changes per key (tbot_report_bits); bit 2 is fire, bits 17 and 23 sit up at rest.
struct MouseBindAction { const char* name; int bit; };
const MouseBindAction MB_ACTIONS[MB_ACTION_COUNT] = {
  { "Off",         -1 },
  { "Quick kick",  22 },
  { "Jetpack",     25 },
  { "Night vision",15 },
  { "Medkit",      16 },
  { "HoloDuke",    24 },
  { "Steroids",    12 },
};

inline int mousebind_action_clamped(int v) {
  return v < 0 ? 0 : (v >= MB_ACTION_COUNT ? 0 : v);
}
inline const char* mousebind_action_name(int v) {
  return MB_ACTIONS[mousebind_action_clamped(v)].name;
}

// 0 for "off" and out-of-range (a shift past 31 is undefined).
inline unsigned mousebind_action_mask(int v) {
  const int bit = MB_ACTIONS[mousebind_action_clamped(v)].bit;
  if (bit < 0 || bit > 31) return 0u;
  return 1u << bit;
}

inline unsigned mousebind_bits(int action4, bool down4, int action5, bool down5) {
  unsigned m = 0;
  if (down4) m |= mousebind_action_mask(action4);
  if (down5) m |= mousebind_action_mask(action5);
  return m;
}

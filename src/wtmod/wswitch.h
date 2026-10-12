// Verbatim from Megaton's src/duke3dmod/wswitch.h.
#pragma once
#include "firerate.h"

// weapon_pos runs -1..-8 lowering the old weapon, the engine flips -9 to 10, then 10..1 rises and 1 finishes at 0.
// wswitch_advance never passes -8 or 1 and never starts from rest (0) or the flip (-9): those are the engine's alone.
inline int wswitch_advance(int pos, int steps) {
  if (steps <= 0 || pos == 0 || pos == -9) return pos;
  if (pos < 0) {
    const int target = pos - steps;
    return target < -8 ? -8 : target;
  }
  const int target = pos - steps;
  return target < 1 ? 1 : target;
}

// Every condition the one player-state write needs: verified engine, LOCAL player, no freeze or camera bracket, a rate
// above identity, a running counter not on the flip.
inline bool wswitch_gate(bool verified, bool localPlayer, bool freezeActive, bool cameraActive, int ratePct, int pos) {
  return verified && localPlayer && !freezeActive && !cameraActive
      && firerate_rate_clamped(ratePct) > 100 && pos != 0 && pos != -9;
}

// The reason wswitch_gate refused (debug line, switch=); same term order as firerate_gate_reason.
inline const char* wswitch_gate_reason(bool verified, bool localPlayer, bool freezeActive, bool cameraActive, int ratePct, int pos) {
  if (!verified)  return "unverified";
  if (!localPlayer) return "remote";
  if (freezeActive) return "frozen";
  if (cameraActive) return "camera";
  if (firerate_rate_clamped(ratePct) <= 100) return "off";
  if (pos == 0)  return "rest";
  if (pos == -9) return "holster";
  return "on";
}

// DLL only: the tic-clock write from hooked_processinput's tail. Takes the player index because this engine's tic hook
// has it and reaches the struct by offset.
void wswitch_tic(int snum, bool freezeActive);
void wswitch_install(const unsigned char* base, unsigned imageSize);

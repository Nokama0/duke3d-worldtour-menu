// From Megaton's light.h.
#pragma once
#include "common.h"

// Render-side light levers, pure. Facts: docs/re/engine-facts.md.
namespace rva {
  const unsigned NOFOG_RVA           = 0x001E87EE;  // VA 0x005E87EE, uchar; 1 skips every glEnable/glDisable(GL_FOG)
  const unsigned R_USETILESHADES_RVA = 0x001B1AA4;  // VA 0x005B1AA4, int; 0 = unshaded variant
  const unsigned GETSHADEFACTOR_RVA  = 0x000786F0;  // VA 0x004786F0; cdecl float(int); patched to return 1.0
  const unsigned PS_VISIBILITY       = 0x268;       // player_struct +616, long; 0 = no distance fog, shade intact
  // nofog=1 only skips per-polygon glEnable/glDisable(GL_FOG), freezing the state the last HUD left; bglDisable puts
  // GL_FOG in the disabled state nofog relies on.
  const unsigned BGLDISABLE_PTR_RVA  = 0x017FF4F8;  // VA 0x01bff4f8, void (__stdcall*)(unsigned)
}
const unsigned GL_FOG_ENUM = 0x0B60;

// Fullbright arms when the gameplay switch is on, or the camera is active with its own switch on.
inline bool light_wants(bool fullbright, bool cameraActive, bool cameraFullbright) {
  return fullbright || (cameraActive && cameraFullbright);
}

inline bool fog_substitute(bool cameraActive, bool fogSwitch) {
  return cameraActive && fogSwitch;
}

// The prologue becomes `fld1; ret`, so every vertex colour factor is 1.0. 8 bytes, not 3: `55 8B EC` would match the
// wrong function in another build.
const int GETSHADEFACTOR_COUNT = 1;
const CodePatch GETSHADEFACTOR_SITES[GETSHADEFACTOR_COUNT] = {
  { rva::GETSHADEFACTOR_RVA, 8,
    { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C, 0x0F, 0xBF },    // push ebp; mov ebp,esp; sub esp,1Ch; movsx eax,...
    { 0xD9, 0xE8, 0xC3, 0x83, 0xEC, 0x1C, 0x0F, 0xBF } },  // fld1; ret
};

struct FogBracket { unsigned char* base; unsigned psRva; long saved; bool armed; };

inline bool fog_bracket_arm(FogBracket* b, unsigned char* base, unsigned psRva) {
  if (!b || !base || b->armed) return false;
  b->base = base; b->psRva = psRva;
  b->saved = read_field_bytes(base, psRva + rva::PS_VISIBILITY, 4);
  write_field_bytes(base, psRva + rva::PS_VISIBILITY, 4, 0);
  b->armed = true;
  return true;
}
inline bool fog_bracket_release(FogBracket* b) {
  if (!b || !b->armed) return false;
  write_field_bytes(b->base, b->psRva + rva::PS_VISIBILITY, 4, b->saved);
  b->armed = false;
  return true;
}

// Arm nofog then tileshades, release in reverse, both restoring what was READ.
struct LightLevers { unsigned char* base; long savedNofog; long savedTileShades; bool armed; };

inline bool light_levers_arm(LightLevers* l, unsigned char* base) {
  if (!l || !base || l->armed) return false;
  l->base = base;
  l->savedNofog      = read_field_bytes(base, rva::NOFOG_RVA, 1);
  l->savedTileShades = read_field_bytes(base, rva::R_USETILESHADES_RVA, 4);
  write_field_bytes(base, rva::NOFOG_RVA, 1, 1);
  write_field_bytes(base, rva::R_USETILESHADES_RVA, 4, 0);
  l->armed = true;
  return true;
}
inline bool light_levers_release(LightLevers* l) {
  if (!l || !l->armed) return false;
  write_field_bytes(l->base, rva::R_USETILESHADES_RVA, 4, l->savedTileShades);
  write_field_bytes(l->base, rva::NOFOG_RVA, 1, l->savedNofog);
  l->armed = false;
  return true;
}

inline bool& light_available_slot() { static bool avail = true; return avail; }
inline bool light_available() { return light_available_slot(); }
inline void light_mark_unavailable() { light_available_slot() = false; }
inline void light_reset_available() { light_available_slot() = true; }

inline bool light_row_unavailable() { return !light_available(); }

// Which value means "no fog" in the g-buffer pass is NOT measured; the field is a SHORT, 512 default.
const int FOG_VISIBILITY_DEFAULT = 0;
inline int fog_visibility_clamped(int v) {
  if (v < 0) return 0;
  if (v > 32767) return 32767;
  return v;
}

// HELD FOR THE WHOLE FLIGHT, not bracketed: the engine copies the visibility into its renderer before a bracket opens.
// ONE writer: a bracket beside the hold flickered.
void fog_hold_tic(bool cameraActive, short* pPlayerVis);
void fog_hold_release();
void fog_drop();                   // the camera landing, and DLL unload
bool fog_held();                   // whether the substitution is standing

// Megaton's levers are absent from a D3D11 deferred renderer, so lightwt.cpp patches the eight shade loads feeding
// g_shade. light_sync runs once a frame; light_drop releases at unload.
void light_sync();
void light_drop();

// Borrowed around the original displayrooms, not held: actors rewrite their shade every tic.
void light_draw_arm();
void light_draw_release();

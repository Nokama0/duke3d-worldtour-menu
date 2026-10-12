// Autosave's engine half. The `saveGame` handler (rva 0x955A0) is not callable, but its LAST act is the save:
// FUN_0071cc30 (rva 0x6CC30, __fastcall(short slot, std::string*)).
#include <windows.h>
#include "autosave.h"
#include "config.h"
#include "input.h"
#include "trigger.h"
#include "world.h"
#include "wallclock.h"
#include "hook.h"

#define P_GM      0x398   // int8
#define P_SPRITE  0x328   // int16, the player's own sprite index

// Classic Duke's game-mode bits: getinput's first test is `test byte ptr [esi+0x398], 0x11` (rva 0x79855), MODE_MENU|
// MODE_TYPE exactly. MODE_GAME is the 4 beside them.
static const int GM_GAME = 0x04;
static const int GM_NOT_IN_PLAY = 0x11;   // the game's menu, and typing

// titleOut is null-checked by the engine (06CDDD skips the write), so 0 is passed. The handler's thumbnail work around
// the call is not reproduced: a timer save must not force a frame (docs/re/autosave-save-call.md).
static const unsigned kRvaSaveGame  = 0x6CC30;    // FUN_0071cc30
static const unsigned kRvaPauseFlag = 0xEDFC76;   // int16, the same flag mouseaim reads

// push ebp; mov ebp,esp; sub esp,0x14; push ebx; push esi; push edi; mov edi,ecx (proves the slot arrives in ECX)
static const unsigned char kFpSaveGame[] = {
  0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14, 0x53, 0x56, 0x57, 0x8B, 0xF9
};

typedef int (__fastcall* SaveGameFn)(int slot, void* titleOut);

static SaveGameFn   s_save = 0;
static const short* s_pPause = 0;
static bool         s_live = false;
static AutosaveState s_state = { 0, 0, 0 };

bool autosave_install() {
  unsigned char* base = (unsigned char*)GetModuleHandleA(0);
  if (!base) { wt_log("autosave: no module base"); return false; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("autosave: not a DOS image"); return false; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("autosave: not a PE image"); return false; }
  const unsigned imageSize = nt->OptionalHeader.SizeOfImage;
  if (kRvaSaveGame + sizeof(kFpSaveGame) > imageSize ||
      kRvaPauseFlag + sizeof(short) > imageSize) {
    wt_log("autosave: an address is outside the image");
    return false;
  }
  const unsigned char* at = base + kRvaSaveGame;
  for (unsigned i = 0; i < sizeof(kFpSaveGame); i++) {
    if (at[i] != kFpSaveGame[i]) {
      wt_log("autosave: the save at rva 0x%X is not the function we expected "
             "(byte %u is %02X, not %02X); autosave is off for this session",
             kRvaSaveGame, i, at[i], kFpSaveGame[i]);
      return false;
    }
  }
  s_save = (SaveGameFn)at;
  s_pPause = (const short*)(base + kRvaPauseFlag);
  s_live = true;
  wt_log("autosave: installed. save at rva 0x%X, pause flag at rva 0x%X; "
         "every %d minute(s) into slot %d",
         kRvaSaveGame, kRvaPauseFlag,
         autosave_mins_clamped(g_cfg.autosavemins),
         autosave_slot_clamped(g_cfg.autosaveslot));
  return true;
}

// End of the tic, on the game thread: Present is on the RENDER thread and would read player state mid-tic. A null
// `playerBase` means nothing due. Writes no engine memory.
void autosave_tic(unsigned char* playerBase, bool cameraActive) {
  if (!s_live) return;

  const int gm = playerBase ? (int)*(const unsigned char*)(playerBase + P_GM) : 0;
  // The game's save path does not check health: a save on the tic the player dies would overwrite the slot.
  int health = 0;
  if (playerBase) {
    const int mySprite = (int)*(const short*)(playerBase + P_SPRITE);
    WorldSprite ws;
    if (world_sprite(mySprite, &ws)) health = ws.extra;
  }
  const bool paused = s_pPause && *s_pPause != 0;
  const int inPlay = (playerBase && (gm & GM_GAME) != 0 && (gm & GM_NOT_IN_PLAY) == 0 &&
                      health > 0 && !paused && !input_menu_open() && !cameraActive) ? 1 : 0;

  // The same cheap "is this still the same map?" hint the per-level scans use; a collision costs one restarted interval.
  const unsigned sig = trigger_map_signature(world_numsectors(), world_numwalls(), 0);
  if (!autosave_step(&s_state, g_cfg.autosavemins, wallclock_ms(), sig, inPlay)) return;

  const int slot = autosave_slot_clamped(g_cfg.autosaveslot);
  const int rc = s_save(slot, 0);
  wt_log("autosave: slot %d %s (rc=%d)", slot, rc == -1 ? "FAILED" : "saved", rc);
}

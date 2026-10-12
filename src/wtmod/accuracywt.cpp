// Perfect accuracy: the spread removed for one shot (decision in accuracy.h). MSVC inlined krand (IMUL ECX,[0x01565b58],
// 0x1a4e86d; ...; SUB EAX,ECX), so the instruction that APPLIES the spread is NOPed; the seed still advances.
#include <windows.h>
#include "accuracy.h"
#include "config.h"
#include "hook.h"

// One in-place edit: same length in and out, so nothing downstream moves.
struct AccEdit {
  unsigned rva;
  unsigned char len;
  unsigned char orig[5];
  unsigned char patch[5];
};

// The pistol, atwith 0xA23. Its spread sits inside shoot's `aim() == -1` guard (0072704A, 00727055): a no-op while
// autoaim holds a target; silent aim forces -1, so it always runs on a redirected shot.
static const AccEdit kEditsPistol[] = {
  // 00727075  ADD dword ptr [ESP + 0xc],EAX
  { 0x77075, 4, { 0x01, 0x44, 0x24, 0x0C, 0x00 }, { 0x90, 0x90, 0x90, 0x90, 0x00 } },
  // 0072707F  MOV EDX,0x68 -> 0x64: the folded +128 pitch centre
  { 0x7707F, 5, { 0xBA, 0x68, 0x00, 0x00, 0x00 }, { 0xBA, 0x64, 0x00, 0x00, 0x00 } },
  // 007270B7  SUB EDX,EAX
  { 0x770B7, 2, { 0x2B, 0xD0, 0x00, 0x00, 0x00 }, { 0x90, 0x90, 0x00, 0x00, 0x00 } },
};

// Chaingun (0x9E8) and shotgun (0xA35) share one branch, so accuracy.h refuses to union two toggles into one arm. All
// seven shotgun pellets converge on one point.
static const AccEdit kEditsHitscan[] = {
  // 007270FE  ADD EDX,EAX
  { 0x770FE, 2, { 0x03, 0xD0, 0x00, 0x00, 0x00 }, { 0x90, 0x90, 0x00, 0x00, 0x00 } },
  // 00727154  ADD EDX,ECX
  { 0x77154, 2, { 0x03, 0xD1, 0x00, 0x00, 0x00 }, { 0x90, 0x90, 0x00, 0x00, 0x00 } },
};

// The expander, atwith 0x990: the PLAYER's arm only. The actor arm (00725747 `TEST SI,SI; JS 0x00725886`, an enemy
// firing GROWSPARK, draws at 007258DB and 007258FC) is deliberately NOT patched.
static const AccEdit kEditsExpander[] = {
  // 0072583C  ADD dword ptr [ESP + 0xc],EAX
  { 0x7583C, 4, { 0x01, 0x44, 0x24, 0x0C, 0x00 }, { 0x90, 0x90, 0x90, 0x90, 0x00 } },
  // 0072584A  MOV ESI,0x68 -> 0x64: same
  { 0x7584A, 5, { 0xBE, 0x68, 0x00, 0x00, 0x00 }, { 0xBE, 0x64, 0x00, 0x00, 0x00 } },
  // 00725871  SUB ESI,EAX
  { 0x75871, 2, { 0x2B, 0xF0, 0x00, 0x00, 0x00 }, { 0x90, 0x90, 0x00, 0x00, 0x00 } },
};

static const AccEdit* edits_for(int pair, int* count) {
  *count = 0;
  switch (pair) {
    case ACC_PAIR_PISTOL:
      *count = (int)(sizeof(kEditsPistol) / sizeof(kEditsPistol[0]));
      return kEditsPistol;
    case ACC_PAIR_HITSCAN:
      *count = (int)(sizeof(kEditsHitscan) / sizeof(kEditsHitscan[0]));
      return kEditsHitscan;
    case ACC_PAIR_EXPANDER:
      *count = (int)(sizeof(kEditsExpander) / sizeof(kEditsExpander[0]));
      return kEditsExpander;
    default:
      return 0;
  }
}

// What one armed pair must put back: the ORIGINAL bytes read from memory, not the table's copy.
struct AccArmed {
  bool armed;
  int pair;
  int count;
  unsigned char* at[4];
  unsigned char len[4];
  unsigned char save[4][5];
  DWORD prot[4];
};

// All-or-nothing, fingerprint first: a mismatch means a binary this table does not describe, so nothing is touched and
// the pair latches off for the session (accuracy.h). That is what makes hardcoded addresses safe.
// No thread is suspended for these writes: only the game thread runs `shoot`, and it is the thread writing.
static bool patch_arm(AccArmed* a, int pair) {
  int n = 0;
  const AccEdit* e = edits_for(pair, &n);
  if (!a || !e || n <= 0 || n > 4) return false;
  a->armed = false;
  a->pair = pair;
  a->count = 0;

  unsigned char* base = (unsigned char*)GetModuleHandleA(0);
  if (!base) return false;

  // Pass one: read and compare, no writes.
  for (int i = 0; i < n; i++) {
    unsigned char* at = base + e[i].rva;
    DWORD old = 0;
    if (!VirtualProtect(at, e[i].len, PAGE_EXECUTE_READWRITE, &old)) return false;
    bool ok = true;
    for (int b = 0; b < e[i].len; b++) if (at[b] != e[i].orig[b]) { ok = false; break; }
    if (!ok) {
      // Restore protection on this site and every opened one, newest first.
      DWORD tmp = 0;
      VirtualProtect(at, e[i].len, old, &tmp);
      for (int k = i - 1; k >= 0; k--) VirtualProtect(a->at[k], a->len[k], a->prot[k], &tmp);
      wt_log("accuracy: pair %d site %d at rva 0x%X is not the byte string this table "
             "describes; nothing patched and the pair is off for the session",
             pair, i, e[i].rva);
      return false;
    }
    a->at[i] = at;
    a->len[i] = e[i].len;
    a->prot[i] = old;
    for (int b = 0; b < e[i].len; b++) a->save[i][b] = at[b];
  }

  // Pass two: write; every site is verified by now.
  for (int i = 0; i < n; i++)
    for (int b = 0; b < e[i].len; b++) a->at[i][b] = e[i].patch[b];

  a->count = n;
  a->armed = true;
  return true;
}

// Unconditional; protections go back in REVERSE: a pair's sites share a page, and restoring site 0 first would make it
// read-only for later writes.
static void patch_release(AccArmed* a) {
  if (!a || !a->armed) return;
  for (int i = 0; i < a->count; i++)
    for (int b = 0; b < a->len[i]; b++) a->at[i][b] = a->save[i][b];
  for (int i = a->count - 1; i >= 0; i--) {
    DWORD tmp = 0;
    VirtualProtect(a->at[i], a->len[i], a->prot[i], &tmp);
  }
  a->armed = false;
  a->count = 0;
}

static AccArmed s_armed = { false, ACC_PAIR_NONE, 0, { 0 }, { 0 }, { { 0 } }, { 0 } };
static bool s_logged[ACC_PAIR_COUNT] = { false, false, false };

int accuracy_arm_for_shot(short atwith, bool isPlayerShot) {
  const int pair = accuracy_pair_for(atwith, g_cfg.accpistol != 0, g_cfg.accchaingun != 0,
                                     g_cfg.accshotgun != 0, g_cfg.accexpander != 0);
  if (!accuracy_should_arm(pair, isPlayerShot)) return ACC_PAIR_NONE;
  // A refusal cannot change on retry; accuracy_note_arm_result latches the pair off.
  if (!accuracy_note_arm_result(pair, patch_arm(&s_armed, pair))) return ACC_PAIR_NONE;
  // One log line per pair: a shared budget spent on the pistol hid whether the chaingun ever armed.
  if (g_cfg.debuglog != 0 && pair >= 0 && pair < ACC_PAIR_COUNT && !s_logged[pair]) {
    s_logged[pair] = true;
    wt_log("accuracy: pair %d armed for atwith 0x%X (%d sites)",
           pair, (unsigned)(unsigned short)atwith, s_armed.count);
  }
  return pair;
}

void accuracy_release_after_shot() { patch_release(&s_armed); }

// Inline hooking, x86 only. The game's .text is decrypted at runtime by the store's protection, so this
// writes as little as it can and refuses unless the bytes under the patch match what the analysis read.
#include "hook.h"
#include "bounds.h"
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

// Enough for any prologue we copy; a wrong boundary is a refusal, not a sprayed page.
#define HOOK_MAX_PROLOGUE 32
#define HOOK_JMP_LEN      5
// Past this many threads we refuse the patch rather than write one that is only partly protected (the game runs
// about 115).
#define HOOK_MAX_THREADS  256
// Holds that found a thread stopped inside the patch bytes are released and retried this many times.
#define HOOK_HOLD_TRIES   8

static void hook_resume_all(HANDLE* held, int n) {
  for (int i = 0; i < n; i++) { ResumeThread(held[i]); CloseHandle(held[i]); }
}

// Suspends every thread but ours so a plain write never lands under an executing instruction. Returns the
// count, or -1 if it could not cover them all (nothing left suspended). No logging or allocation: a
// suspended thread may hold any lock.
static int hook_suspend_others(HANDLE* held, int max) {
  if (max > HOOK_MAX_THREADS) max = HOOK_MAX_THREADS;
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return -1;
  const DWORD pid = GetCurrentProcessId();
  const DWORD self = GetCurrentThreadId();
  // Ids and handles are all taken before the first suspension: security software hooks the Toolhelp and
  // thread-open calls, and a hook that needs a thread we already hold would deadlock us.
  DWORD ids[HOOK_MAX_THREADS];
  THREADENTRY32 te;
  te.dwSize = sizeof(te);
  int found = 0;
  int overflow = 0;
  if (Thread32First(snap, &te)) {
    do {
      if (te.dwSize < FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(DWORD)) continue;
      if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
      if (found >= max) { overflow = 1; break; }
      ids[found++] = te.th32ThreadID;
    } while (te.dwSize = sizeof(te), Thread32Next(snap, &te));
  }
  CloseHandle(snap);
  if (overflow) return -1;

  int opened = 0;
  for (int i = 0; i < found; i++) {
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, ids[i]);
    if (h) held[opened++] = h;                // a miss exited after the snapshot
  }
  int n = 0;
  for (int i = 0; i < opened; i++) {
    if (SuspendThread(held[i]) == (DWORD)-1) { CloseHandle(held[i]); continue; }
    held[n++] = held[i];
  }
  return n;
}

// Holds every other thread with none stopped past the first byte of [at, at+len): one there would resume into the middle
// of the new instruction. Such a thread is let run on and the hold retried. Returns the count held, or -1 (none held).
static int hook_hold_clear(HANDLE* held, int max, const unsigned char* at, unsigned len) {
  for (int attempt = 0; attempt < HOOK_HOLD_TRIES; attempt++) {
    const int n = hook_suspend_others(held, max);
    if (n < 0) return -1;
    int clear = 1;
    for (int i = 0; i < n && clear; i++) {
      CONTEXT c;
      c.ContextFlags = CONTEXT_CONTROL;
      // GetThreadContext also waits for the suspension to land; an unreadable EIP counts as inside.
      if (!GetThreadContext(held[i], &c) || eip_inside_patch((unsigned)c.Eip, (unsigned)(UINT_PTR)at, len))
        clear = 0;
    }
    if (clear) return n;
    hook_resume_all(held, n);
    Sleep(1);
  }
  return -1;
}

// SizeOfImage of the module at base, or 0 when its headers do not read as a PE image.
static unsigned hook_image_size(const unsigned char* base) {
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return 0;
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
  return nt->OptionalHeader.SizeOfImage;
}

static void hook_log_bytes(const char* what, const unsigned char* p, unsigned n) {
  char text[HOOK_MAX_PROLOGUE * 3 + 1];
  unsigned used = 0;
  for (unsigned i = 0; i < n && i < HOOK_MAX_PROLOGUE; i++)
    used += (unsigned)sprintf_s(text + used, sizeof(text) - used, i ? " %02X" : "%02X", p[i]);
  wt_log("hook: %s %s", what, text);
}

int hook_install(unsigned rva, const unsigned char* fingerprint, unsigned fpLen,
                 unsigned prologueLen, void* replacement, void** trampolineOut) {
  if (trampolineOut) *trampolineOut = 0;
  if (!fingerprint || !replacement || fpLen == 0 || fpLen > prologueLen ||
      prologueLen < HOOK_JMP_LEN || prologueLen > HOOK_MAX_PROLOGUE) {
    wt_log("hook: refusing rva 0x%X, arguments make no sense (fp=%u prologue=%u)", rva, fpLen, prologueLen);
    return 0;
  }

  unsigned char* base = (unsigned char*)GetModuleHandleA(0);
  if (!base) { wt_log("hook: no module base"); return 0; }
  // A different or smaller executable must log a refusal, not fault reading the fingerprint.
  if (!image_range_fits(rva, prologueLen, hook_image_size(base))) {
    wt_log("hook: rva 0x%X + %u lies outside the game image; installing nothing", rva, prologueLen);
    return 0;
  }
  unsigned char* target = base + rva;

  // The one check that keeps a patch from landing in the wrong place.
  if (memcmp(target, fingerprint, fpLen) != 0) {
    wt_log("hook: fingerprint MISMATCH at rva 0x%X (%p); installing nothing", rva, (void*)target);
    hook_log_bytes("expected", fingerprint, fpLen);
    hook_log_bytes("found   ", target, fpLen);
    return 0;
  }

  unsigned char* tramp = (unsigned char*)VirtualAlloc(0, 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!tramp) { wt_log("hook: no trampoline memory for rva 0x%X", rva); return 0; }
  memcpy(tramp, target, prologueLen);
  tramp[prologueLen] = 0xE9;
  *(int*)(tramp + prologueLen + 1) = (int)((target + prologueLen) - (tramp + prologueLen + HOOK_JMP_LEN));
  // Written, then executable and read-only for the rest of the session, before anything can jump to it.
  DWORD trampOld = 0;
  if (!VirtualProtect(tramp, 64, PAGE_EXECUTE_READ, &trampOld)) {
    wt_log("hook: trampoline for rva 0x%X could not be made executable (err %lu)", rva, GetLastError());
    VirtualFree(tramp, 0, MEM_RELEASE);
    return 0;
  }
  FlushInstructionCache(GetCurrentProcess(), tramp, 64);

  DWORD old = 0;
  if (!VirtualProtect(target, prologueLen, PAGE_EXECUTE_READWRITE, &old)) {
    wt_log("hook: rva 0x%X is not writable (err %lu)", rva, GetLastError());
    VirtualFree(tramp, 0, MEM_RELEASE);
    return 0;
  }
  // The game calls this function from its own thread, so the patch must appear at once: every other thread is held,
  // none of them inside the bytes replaced. Eight aligned bytes are also exchanged as one store.
  unsigned char patch[HOOK_MAX_PROLOGUE];
  memcpy(patch, target, prologueLen);
  patch[0] = 0xE9;
  *(int*)(patch + 1) = (int)((unsigned char*)replacement - (target + HOOK_JMP_LEN));
  for (unsigned i = HOOK_JMP_LEN; i < prologueLen; i++) patch[i] = 0x90;

  const int atomic = (prologueLen <= 8 && ((UINT_PTR)target & 7) == 0);
  HANDLE held[HOOK_MAX_THREADS];
  const int suspended = hook_hold_clear(held, HOOK_MAX_THREADS, target, prologueLen);
  if (suspended < 0) {
    wt_log("hook: rva 0x%X: the other threads could not be held clear of the patch; installing nothing", rva);
    DWORD back = 0;
    VirtualProtect(target, prologueLen, old, &back);
    VirtualFree(tramp, 0, MEM_RELEASE);
    return 0;
  }
  if (atomic) {
    LONG64 fresh;
    memcpy(&fresh, target, 8);                 // the bytes past the prologue, kept
    memcpy(&fresh, patch, prologueLen);
    InterlockedExchange64((volatile LONG64*)target, fresh);
  } else {
    memcpy(target, patch, prologueLen);
  }
  FlushInstructionCache(GetCurrentProcess(), target, prologueLen);
  hook_resume_all(held, suspended);
  DWORD ignored = 0;
  VirtualProtect(target, prologueLen, old, &ignored);

  if (trampolineOut) *trampolineOut = tramp;
  wt_log("hook: rva 0x%X fingerprint matched, patched at %p -> %p, trampoline %p (%s)",
         rva, (void*)target, replacement, (void*)tramp,
         atomic ? "atomic" : "plain write, threads held");
  wt_log("hook: rva 0x%X held %d other thread(s) for the write", rva, suspended);
  return 1;
}

int hook_patch_call(unsigned rva, const unsigned char* fingerprint, void* replacement) {
  if (!fingerprint || !replacement) {
    wt_log("hook: refusing call site rva 0x%X, arguments make no sense", rva);
    return 0;
  }
  unsigned char* base = (unsigned char*)GetModuleHandleA(0);
  if (!base) { wt_log("hook: no module base"); return 0; }
  if (!image_range_fits(rva, HOOK_JMP_LEN, hook_image_size(base))) {
    wt_log("hook: call site rva 0x%X lies outside the game image; installing nothing", rva);
    return 0;
  }
  unsigned char* target = base + rva;

  if (memcmp(target, fingerprint, HOOK_JMP_LEN) != 0) {
    wt_log("hook: call fingerprint MISMATCH at rva 0x%X (%p); installing nothing", rva, (void*)target);
    hook_log_bytes("expected", fingerprint, HOOK_JMP_LEN);
    hook_log_bytes("found   ", target, HOOK_JMP_LEN);
    return 0;
  }

  unsigned char patch[HOOK_JMP_LEN];
  patch[0] = 0xE8;
  *(int*)(patch + 1) = (int)((unsigned char*)replacement - (target + HOOK_JMP_LEN));

  DWORD old = 0;
  if (!VirtualProtect(target, HOOK_JMP_LEN, PAGE_EXECUTE_READWRITE, &old)) {
    wt_log("hook: call site rva 0x%X is not writable (err %lu)", rva, GetLastError());
    return 0;
  }
  // Five bytes mid-function in code the render thread runs: same rule as an unaligned prologue, hold every
  // other thread still for the write.
  HANDLE held[HOOK_MAX_THREADS];
  const int suspended = hook_hold_clear(held, HOOK_MAX_THREADS, target, HOOK_JMP_LEN);
  if (suspended < 0) {
    wt_log("hook: call site rva 0x%X: the other threads could not be held clear of the patch; "
           "installing nothing", rva);
    DWORD back = 0;
    VirtualProtect(target, HOOK_JMP_LEN, old, &back);
    return 0;
  }
  memcpy(target, patch, HOOK_JMP_LEN);
  FlushInstructionCache(GetCurrentProcess(), target, HOOK_JMP_LEN);
  hook_resume_all(held, suspended);

  DWORD ignored = 0;
  VirtualProtect(target, HOOK_JMP_LEN, old, &ignored);
  wt_log("hook: call site rva 0x%X fingerprint matched, now calls %p (held %d other thread(s))",
         rva, replacement, suspended);
  return 1;
}

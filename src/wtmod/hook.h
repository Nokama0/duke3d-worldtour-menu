// The mod's writes to the game's own code: one fingerprint check, one five-byte JMP, no length disassembler.
#ifndef WTMOD_HOOK_H
#define WTMOD_HOOK_H

void wt_log(const char* fmt, ...);

// Both installers run on the worker thread only, one call at a time: two at once would suspend each other.

// The first fpLen bytes must equal `fingerprint` (a moved or rebuilt function installs nothing). `prologueLen` bytes
// (boundary at or past five) go to the trampoline; leftovers are padded with 0x90. Returns 1 if patched.
int hook_install(unsigned rva, const unsigned char* fingerprint, unsigned fpLen,
                 unsigned prologueLen, void* replacement, void** trampolineOut);

// The fingerprint covers the call's encoded target, so both ends are checked. No trampoline: the caller can call the
// old target itself.
int hook_patch_call(unsigned rva, const unsigned char* fingerprint, void* replacement);

#endif

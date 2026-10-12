#pragma once
// Pure bounds rules behind the DLL's file paths, code patches and clipboard read; the suite pins them.
#include <stddef.h>
#include <string.h>

// True when [rva, rva+len) lies inside an image of imageSize bytes, without wrapping.
inline bool image_range_fits(unsigned rva, unsigned len, unsigned imageSize) {
  return rva <= imageSize && len <= imageSize - rva;
}

// A thread stopped past the first byte of a patch would resume into the middle of the new instruction; one stopped
// exactly at its start runs the new JMP whole.
inline bool eip_inside_patch(unsigned eip, unsigned start, unsigned len) {
  return eip > start && eip - start < len;
}

// "<folder of modulePath>\name" into out. `got` is GetModuleFileNameA's return for a buffer of modCap bytes: 0 or
// modCap means failed or truncated, and a truncated path's last backslash is a parent folder's. Any failure leaves out
// empty and answers false, so nothing is read or written one folder up. out may be modulePath itself.
inline bool path_beside_module(const char* modulePath, unsigned got, unsigned modCap, const char* name,
                               char* out, unsigned outCap) {
  if (!out || outCap == 0) return false;
  if (!modulePath || !name || got == 0 || got >= modCap) { out[0] = 0; return false; }
  unsigned folder = 0;                       // length up to and including the last backslash
  for (unsigned i = 0; i < got && modulePath[i]; i++)
    if (modulePath[i] == '\\') folder = i + 1;
  const size_t nameLen = strlen(name);
  if (folder == 0 || folder + nameLen >= outCap) { out[0] = 0; return false; }
  if (out != modulePath) memmove(out, modulePath, folder);
  memcpy(out + folder, name, nameLen + 1);
  return true;
}

// Bytes to copy from a clipboard block of blockSize bytes into a buffer of cap: stops at NUL, at the block's end
// (another program may place text with no terminator) and one short of cap for the terminator.
inline int clip_copy_len(const char* p, size_t blockSize, int cap) {
  int n = 0;
  while (p && n < cap - 1 && (size_t)n < blockSize && p[n]) n++;
  return n;
}

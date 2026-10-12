#include <windows.h>
#include "clipboard.h"
#include "bounds.h"

bool clipboard_read_text(char* out, int cap) {
  if (!out || cap <= 0) return false;
// Terminated first: every early return leaves a defined empty string.
  out[0] = 0;
// Asked before opening: the common case (an image or nothing on it) never takes the global clipboard.
  if (!IsClipboardFormatAvailable(CF_TEXT)) return false;
  if (!OpenClipboard(nullptr)) return false;
  int n = 0;
  const HANDLE h = GetClipboardData(CF_TEXT);
  if (h) {
// The handle is the clipboard's own: locked and unlocked, never freed.
    const char* p = (const char*)GlobalLock(h);
    if (p) {
      // Bounded by the block too: another program may place CF_TEXT with no terminator.
      n = clip_copy_len(p, GlobalSize(h), cap);
      memcpy(out, p, (size_t)n);
      out[n] = 0;
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return n > 0;
}

bool clipboard_write_text(const char* s) {
  if (!s) return false;
  int n = 0;
  while (s[n]) n++;
  if (!OpenClipboard(nullptr)) return false;
  bool ok = false;
  if (EmptyClipboard()) {
    // SetClipboardData requires GMEM_MOVEABLE.
    const HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)n + 1);
    if (h) {
      char* p = (char*)GlobalLock(h);
      if (p) {
        for (int i = 0; i < n; i++) p[i] = s[i];
        p[n] = 0;
        GlobalUnlock(h);
// The clipboard owns the handle on success only: free it after a failed set, never after a successful one.
        if (SetClipboardData(CF_TEXT, h)) ok = true;
        else GlobalFree(h);
      } else {
        GlobalFree(h);
      }
    }
  }
  CloseClipboard();
  return ok;
}

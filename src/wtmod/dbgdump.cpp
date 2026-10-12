// On-demand dump state: one counter, raised on the keyboard thread, read on the render thread (interlocked;
// a producer seeing an older number just reports a frame later).
#include <windows.h>
#include "dbgdump.h"
#include "hook.h"

static volatile LONG s_seq = 0;
static int s_enabled = 0;
static int s_vk = 0;

void dbgdump_configure(int enabled, int vk) {
  s_enabled = (enabled && vk > 0 && vk < 255) ? 1 : 0;
  s_vk = s_enabled ? vk : 0;
  if (s_enabled)
    wt_log("debugdump: on, bound to virtual key 0x%02X. Stand in front of what you want "
           "measured and press it; every producer reports the frame after the press", s_vk);
  else if (enabled)
    wt_log("debugdump: asked for but the key 0x%02X is not one this can bind (1..254), so it "
           "stays off", vk);
}

int dbgdump_key() { return s_enabled ? s_vk : 0; }

void dbgdump_request() {
  if (!s_enabled) return;
  const LONG n = InterlockedIncrement(&s_seq);
  // Always logged: a dump with nothing to say must not look like a key that never registered.
  wt_log("debugdump: dump %ld requested. The lines that follow describe the frame AFTER this "
         "press, not the one during it; if none follow, the producers ran and had nothing to "
         "report rather than the key having been missed", n);
}

long dbgdump_take(long* lastSeen) {
  const LONG n = s_seq;
  if (!s_enabled || !lastSeen || n == 0 || *lastSeen == n) return 0;
  *lastSeen = n;
  return (long)n;
}

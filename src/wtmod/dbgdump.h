// A key pressed while looking at the thing being measured, so every producer reports the frame after the press.
// `debugdump=1` enables it, `debugdumpkey` picks the key; off by default. details: docs/re/debug-dump-key.md
#pragma once

void dbgdump_configure(int enabled, int vk);

int dbgdump_key();

void dbgdump_request();

// A producer's once-per-press gate. `lastSeen` is the producer's own static, initially 0. Returns the dump number the
// first time this producer sees this press, else 0.
long dbgdump_take(long* lastSeen);

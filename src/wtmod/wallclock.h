#pragma once

// A free-running millisecond counter. UNSIGNED AND IT WRAPS, by design: every consumer measures an AGE as the unsigned
// difference `now - t0` (dmgnum.h), exact across the wrap. wallclock.cpp is DLL-only; tests hand their own `now`.
unsigned wallclock_ms();

#pragma once

// A row nothing reads would say a feature is on while it did nothing. The one latch left is set at
// run time by what can refuse screenshot mode (free camera off in the ini, or a failed shot_install fingerprint).
inline bool& unported_shot_slot() { static bool unavailable = false; return unavailable; }
inline bool unported_shot()       { return unported_shot_slot(); }
inline void unported_shot_mark_unavailable() { unported_shot_slot() = true; }

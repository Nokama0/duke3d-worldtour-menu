// Copied from Megaton's clipboard.h; keep identical.
#pragma once
// The Windows clipboard only, DLL-only; entry.h owns what a copy copies and a paste may insert. Fail-closed: any
// failure answers false and leaves the field untouched.

// Always NUL-terminated (empty on failure); `cap` is the buffer size in bytes.
bool clipboard_read_text(char* out, int cap);

bool clipboard_write_text(const char* s);

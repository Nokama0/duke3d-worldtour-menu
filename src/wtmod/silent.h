#pragma once

// Silent aim: the shot goes where the assist solved; the view never moves. A jump-thunk hook on `shoot(short i, short
// atwith)` (thunk RVA 0x316B -> body 0xF3640) swaps the solved angle and pitch into the player fields it reads and
// restores them. The solution is one tic old (~33 ms): hooked_shoot runs before aim_tic publishes.
bool silent_install();

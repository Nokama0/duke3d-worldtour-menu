// Perfect accuracy's two DLL-only entry points. Every decision is accuracy.h's; this pair only brackets ONE shoot call
// so .text is never left patched. This fork inlines krand: accuracywt.cpp's header has the disassembly.
#ifndef WTMOD_ACCURACYWT_H
#define WTMOD_ACCURACYWT_H

// Returns the AccPair armed or ACC_PAIR_NONE; MUST be paired with accuracy_release_after_shot on every path.
int accuracy_arm_for_shot(short atwith, bool isPlayerShot);

void accuracy_release_after_shot();

#endif

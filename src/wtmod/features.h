// Rows for features this port lacks show `(Unavailable)`; features_init sets exactly those. features.cpp also defines
// the clamp bodies config.cpp links.
#ifndef WTMOD_FEATURES_H
#define WTMOD_FEATURES_H

void features_init();

// Once per drawn frame, after menu_draw: pushes changed Config fields to their setters.
void features_sync();
// Outside the overlay gate, so the key works with the panel never opened.
void features_arm_freecam_key();

#endif

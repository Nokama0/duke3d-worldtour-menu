// The ESP markers, drawn under the panel from the Present hook inside the one overlay_begin/overlay_end bracket.
#ifndef WTMOD_ESP_H
#define WTMOD_ESP_H

// The only thing that runs on a frame with everything off.
bool esp_any_on();

// Drives the corner note.
bool esp_level_has_secret_exit();

// Opens and closes its own canvas frame. Does nothing when esp_any_on() is false, the sprite arrays did not resolve, or
// there is no projection.
void esp_draw(int screenW, int screenH);

#endif

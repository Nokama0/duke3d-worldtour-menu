// The overlay's batcher, as canvas.cpp uses it; not overlay.h's public API (the panel needs its own atlas, triangles,
// scissor and additive pass). Everything queues into the SAME vertex array as overlay_rect and overlay_text and draws
// in call order, so one save/restore of device state covers both.
#ifndef WTMOD_OVERLAY_INTERNAL_H
#define WTMOD_OVERLAY_INTERNAL_H

struct ID3D11Device;
struct ID3D11ShaderResourceView;

// One vertex: device pixels from the top left, atlas coordinates, colour in the overlay's 0xRRGGBBAA. The atlas
// contributes only alpha, so a flat fill is three of these on a fully opaque texel.
struct OvPoint { float x, y, u, v; unsigned rgba; };

int ov_frame_open();

ID3D11Device* ov_device();

void ov_size(int* w, int* h);

// The state the vertices queued after each call are drawn under. A change starts a new batch (generous ceiling, logs
// once if reached).
void ov_set_texture(ID3D11ShaderResourceView* srv);   // null: back to the readout's atlas
void ov_set_additive(int on);                         // 1: source adds to what is there
void ov_set_scissor(int x, int y, int w, int h);      // clip to a device rect
void ov_clear_scissor();

void ov_tri(const OvPoint& a, const OvPoint& b, const OvPoint& c);

#endif

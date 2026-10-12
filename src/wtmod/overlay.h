// An immediate-mode 2D drawer on the game's Direct3D 11 device, called from the Present hook. It restores every state it
// touches before overlay_end; a setup failure logs once and gives up for good.
#ifndef WTMOD_OVERLAY_H
#define WTMOD_OVERLAY_H

// WORLD overlays mark things in a level and drew across the tally and the game's menus. The panel is NOT gated on this:
// it is the only way to turn them off.
inline bool overlay_world_should_draw(bool inPlay) { return inPlay; }

struct IDXGISwapChain;

// Colours are 0xRRGGBBAA, straight (not premultiplied) alpha.
#define OVERLAY_RGBA(r, g, b, a)                                     \
  (((unsigned)(r) << 24) | ((unsigned)(g) << 16) |                   \
   ((unsigned)(b) << 8)  |  (unsigned)(a))

// Returns 1 when the calls below may be made; a 0 is permanent after a setup failure.
int overlay_begin(IDXGISwapChain* sc);

// Nothing reaches the device until overlay_end.
void overlay_rect(int x, int y, int w, int h, unsigned rgba);
void overlay_text(int x, int y, unsigned rgba, const char* s);

// Fixed-pitch font, so a width is a multiplication (0 before the first overlay_begin).
int overlay_text_width(const char* s);
int overlay_text_height();

// 0 before the first overlay_begin.
int overlay_width();
int overlay_height();

void overlay_end();

// Past the ceiling quads are silently dropped and the panel draws LAST, so an unlimited-range ESP once hid the menu that
// turns it off. Heavy callers ask first.
int overlay_verts_left();

#endif

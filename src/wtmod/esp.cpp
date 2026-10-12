// See esp.h. Markers are drawn far to near so the near label wins an overlap.
#include <windows.h>
#include <stdio.h>
#include "esp.h"
#include "esp_layout.h"
#include "actorstate.h"
#include "esp_pics.h"
#include "esp_switch.h"
#include "esp_tint.h"
#include "worldwt.h"
#include "esp_world.h"
#include "world.h"
#include "wallclock.h"
#include "overlay.h"
#include "project.h"
#include "canvas.h"
#include "font.h"
#include "config.h"
#include "ui.h"
#include "hook.h"
#include "camera.h"

bool esp_any_on() {
// Only the parent switches: espboxes/esphealth/espnames/espdistance default ON and draw nothing alone. Categories with
// no producer here are passed as zero, so adding one later is a compile error, not a silent no-op.
  return esp_any_category_on(g_cfg.espenemies, g_cfg.espitems,
                             g_cfg.espsecrets,
                             0,   // aimmarker:       no aim assist here
                             0,   // playermarker:    no free camera here
                             0,   // notice:          no teleport notice here
                             g_cfg.espexplodables,
                             g_cfg.espswitches,
                             g_cfg.espbreakables);
}

// switch_classify drops the line for a card already held. No player to read answers 0, so every line shows: a lock
// asking for a card you hold is noise, one silent about a card you need is wrong.
static int esp_got_access() {
  const int bits = camera_local_got_access();
  return bits < 0 ? 0 : bits;
}

// Markers carry one index for two sources: a sprite's is below WORLD_MAXSPRITES, a wall's is WORLD_MAXSPRITES + wallnum.
static const int ESP_WALL_INDEX_BASE = WORLD_MAXSPRITES;

// On dist2d and never depth: depth under-reads off to one side, so the range would move with the field of view.
static bool esp_dist_ok(const ScreenPt& p) {
  return esp_marker_in_range(p, g_cfg.espdrawdist, g_cfg.espnolimit);
}

// The raw `extra` is a short straight out of sprite memory: a negative one is a dead or garbage actor, clamped to zero.
static int esp_hp_for_display(int extra) {
  if (extra < 0) return 0;
  if (extra > ESP_STRENGTH_MAX) return ESP_STRENGTH_MAX;
  return extra;
}

// sprite.extra only drops when the actor's CON next reaches ifhitbyweapon, so the bar held still while an enemy was
// shot; the hits sit in hittype[i].extra. Subtracted so the number matches the next tic; a refusal or non-positive
// pending (it rests at -1) changes nothing.
static int esp_hp_less_pending(int index, int hp) {
  int pending = 0;
  if (!world_actor_pending_damage(index, &pending) || pending <= 0) return hp;
  return pending >= hp ? 0 : hp - pending;
}

// Whether the actor's CON has executed its first tic, when GAME.CON's `ifai 0` overrides fire (temp_data[5] is the ai).
// A refusal answers "has run", leaving the health as the sprite reports it.
static bool esp_actor_has_run(int index) {
  int ai = 0;
  if (!world_actor_temp_data(index, 5, &ai)) return true;
  return ai != 0;
}

// Brackets rather than a closed outline so markers do not merge into a grid in a crowd.
static void esp_draw_brackets(const ScreenBox& box, unsigned colour) {
  const int cx = esp_bracket_arm(box.w);
  const int cy = esp_bracket_arm(box.h);
  const int t  = esp_bracket_thickness(cx, cy);
  if (t <= 0) return;
  const int x0 = box.x, y0 = box.y;
  const int x1 = box.x + box.w, y1 = box.y + box.h;
  canvas_rect(x0,      y0,      cx, t,  colour);
  canvas_rect(x0,      y0,      t,  cy, colour);
  canvas_rect(x1 - cx, y0,      cx, t,  colour);
  canvas_rect(x1 - t,  y0,      t,  cy, colour);
  canvas_rect(x0,      y1 - t,  cx, t,  colour);
  canvas_rect(x0,      y1 - cy, t,  cy, colour);
  canvas_rect(x1 - cx, y1 - t,  cx, t,  colour);
  canvas_rect(x1 - t,  y1 - cy, t,  cy, colour);
}

// Past ESP_MARKER_MAX a marker is dropped rather than overwriting a live one.
static void esp_collect(EspMarker* markers, int* count, int* seq, int* overflow,
                        int kind, int index, const ScreenPt& p, const ScreenBox& box) {
  if (*count >= ESP_MARKER_MAX) { (*overflow)++; return; }
  EspMarker& m = markers[(*count)++];
  m.kind = kind; m.index = index; m.dist2d = p.dist2d; m.seq = (*seq)++;
  m.p = p; m.box = box;
}

// Items: the enemy's box and brackets in blue, with the name above. No health and no distance, as in Megaton.
static void esp_draw_item_marker(const EspMarker& m) {
  WorldSprite s;
  if (!world_sprite(m.index, &s)) return;
  const char* nm = esp_item_name_pal(s.picnum, s.pal);
  if (!nm) return;   // only a named pickup is ever collected

  esp_draw_brackets(m.box, canvas_esp_colour(false));
  canvas_text(esp_label_x(m.box.x, m.box.w, canvas_text_width(nm, FONT_SMALL)),
              esp_name_label_y(m.box.y), nm, C_BONE, FONT_SMALL);
}

// The name label is two runs in two faces (name FONT_SMALL, numbers FONT_MONO) so a column of enemies keeps its digits
// aligned; esp_label_runs centres the pair as one.
static void esp_draw_enemy_marker(const EspMarker& m) {
  WorldSprite s;
  if (!world_sprite(m.index, &s)) return;
  const ScreenBox& box = m.box;

  if (g_cfg.espboxes) esp_draw_brackets(box, canvas_esp_colour(true));

// Null for AS_ALIVE, else "Choking", "Playing dead" or "Cloaked"; the walk already refused the hidden states.
  const char* stateLabel = actor_state_label(actor_state_cached(m.index));

// settled: the sprite's own number, clamped. shown: GAME.CON's pending first-tic override applied (a dormant Assault
// Captain reads 60, not 30). maxhp is asked with `shown`, never the discounted value below, or the learned-ceiling
// fallback would learn from a mid-flight number.
  const int settled = esp_hp_for_display(s.extra);
  const int shown   = esp_display_hp(settled, esp_spawn_strength(s.picnum),
                                     esp_rule_max_health(s.picnum, s.pal),
                                     esp_actor_has_run(m.index));
  const int maxhp   = esp_max_health(s.picnum, s.pal, shown);
  const int hp      = esp_hp_less_pending(m.index, shown);

// maxhp == 0 means the maximum is unknown, so no bar (worse than none). A labelled state gets no bar either.
  if (g_cfg.esphealth && maxhp > 0 && stateLabel == 0) {
    const int barX = esp_bar_x(box.x);
    const int fill = canvas_bar_fill(box.h, hp, maxhp);
    canvas_rect(barX, box.y, ESP_BAR_W, box.h, C_WELL);
    if (fill > 0)
      canvas_rect(barX, esp_bar_fill_y(box.y, box.h, fill), ESP_BAR_W, fill,
                  canvas_health_colour(hp, maxhp));
  }

  if (g_cfg.espnames) {
    const char* nm = esp_enemy_name_pal(s.picnum, s.pal);
    char numBuf[32];
    int nameW = 0;
    if (nm) nameW = canvas_text_width(nm, FONT_SMALL);
// No maximum means no fraction: "640" is a fact, "640/4500" would claim how much is left.
    if (stateLabel)     _snprintf_s(numBuf, sizeof(numBuf), _TRUNCATE, "%s", stateLabel);
    else if (maxhp > 0) _snprintf_s(numBuf, sizeof(numBuf), _TRUNCATE, "%d/%d", hp, maxhp);
    else                _snprintf_s(numBuf, sizeof(numBuf), _TRUNCATE, "%d", hp);
    const int numW = canvas_text_width(numBuf, FONT_MONO);

    const EspLabelRuns runs = esp_label_runs(box.x, box.w, nameW, numW);
    const int nameY = esp_name_label_y(box.y);
    if (nm) canvas_text(runs.nameX, nameY, nm, C_BONE, FONT_SMALL);
    canvas_text(runs.numX,
                esp_label_mono_y(nameY, font_height[FONT_SMALL], font_height[FONT_MONO]),
                numBuf, C_BONE, FONT_MONO);
  }

  if (g_cfg.espdistance) {
    char dist[16];
    _snprintf_s(dist, sizeof(dist), _TRUNCATE, "%d m", m.p.dist2d / BUILD_UNITS_PER_METRE);
    canvas_text(esp_label_x(box.x, box.w, canvas_text_width(dist, FONT_MONO)),
                esp_dist_label_y(box.y, box.h), dist, C_BONEDIM, FONT_MONO);
  }
}

// A cross on the sector's centre, not a box: a box round a room would fill the screen at close range.
static void esp_draw_secret_marker(const EspMarker& m) {
  const unsigned colour = canvas_esp_secret_colour();
  canvas_rect(esp_secret_hbar_x(m.p.x), esp_secret_hbar_y(m.p.y),
              esp_secret_bar_span(), ESP_SECRET_T, colour);
  canvas_rect(esp_secret_vbar_x(m.p.x), esp_secret_vbar_y(m.p.y),
              ESP_SECRET_T, esp_secret_bar_span(), colour);
  canvas_text(esp_label_x(m.p.x, 0, canvas_text_width(ESP_SECRET_LABEL, FONT_SMALL)),
              esp_secret_name_label_y(m.p.y), ESP_SECRET_LABEL, colour, FONT_SMALL);
  char dist[16];
  _snprintf_s(dist, sizeof(dist), _TRUNCATE, "%d m", m.p.dist2d / BUILD_UNITS_PER_METRE);
  canvas_text(esp_label_x(m.p.x, 0, canvas_text_width(dist, FONT_MONO)),
              esp_secret_dist_label_y(m.p.y), dist, C_BONEDIM, FONT_MONO);
}

// A crack's name is re-derived from its live picnum; a wall's is one fixed string for all nineteen picnums.
static void esp_draw_explodable_marker(const EspMarker& m) {
  const char* nm;
  if (m.index >= ESP_WALL_INDEX_BASE) {
    nm = "Breakable";
  } else {
    WorldSprite sp;
    if (!world_sprite(m.index, &sp)) return;
    nm = esp_explodable_name(sp.picnum);
    if (!nm) return;   // the sprite changed under us between collect and draw
  }
  esp_draw_cross_mark(m.p, canvas_esp_explodable_colour(), nm, 0);
}

// Both branches re-read the four fields live: a switch pressed between collect and draw must show its new state.
static void esp_draw_switch_marker(const EspMarker& m) {
  int picnum = 0, pal = 0, hitag = 0, lotag = 0;
  if (m.index >= ESP_WALL_INDEX_BASE) {
    WorldWall wl;
    if (!world_wall(m.index - ESP_WALL_INDEX_BASE, &wl)) return;
    picnum = wl.picnum; pal = wl.pal; hitag = wl.hitag; lotag = wl.lotag;
  } else {
    WorldSprite sp;
    if (!world_sprite(m.index, &sp)) return;
    picnum = sp.picnum; pal = sp.pal; hitag = sp.hitag; lotag = sp.lotag;
  }
  const SwitchInfo si = switch_classify(picnum, pal, hitag, lotag, esp_got_access());
  if (si.family == SW_NONE) return;   // no longer a switch: draw nothing, not a stale mark

  if (si.family == SW_ACCESS) {
    esp_draw_card_mark(m.p, keycard_colour(pal), switch_label(si.family), si.line);
    return;
  }
// A secret exit takes the secrets colour; its own label is si.line, so no second line.
  const unsigned colour = (si.family == SW_EXIT && si.exitKind == EXIT_SECRET)
                          ? canvas_esp_secret_colour() : canvas_esp_switch_colour();
  const char* label = (si.family == SW_EXIT) ? si.line : switch_label(si.family);
  const char* line2 = (si.family == SW_EXIT) ? 0 : si.line;
  esp_draw_cross_mark(m.p, colour, label, line2);
}

// THE COMBINATION TINT, in the in-world layer so geometry occludes it. Only a wall sprite has a face.
static void esp_tint_push(const WorldSprite& sp, const SwitchInfo& si) {

  const unsigned rgba = tint_colour(tint_verdict(si.family, si.line),
                                    C_TINT_FLIP, C_TINT_LEAVE);
  if (!rgba) return;
  if ((sp.cstat & 0x30) != 0x10) return;     // wall-aligned only
  const short* sin = world_sintable();
  if (!sin) return;
  const int tx = world_tilesizx(sp.picnum), ty = world_tilesizy(sp.picnum);
  if (tx <= 0 || ty <= 0) return;
// THE FLIP BITS NEGATE THE OFFSETS (xoff on cstat bit 2, yoff on bit 3), as the engine's wall-sprite branch does;
// without it a flipped sprite's span is displaced by twice its offset. world.cpp reads the fields as signed chars.
  int xoff = sp.xoffset, yoff = sp.yoffset;
  if (sp.cstat & 4) xoff = -xoff;
  if (sp.cstat & 8) yoff = -yoff;
  double ax, ay, bx, by;
  tint_wall_sprite_endpoints(sp.x, sp.y, sp.ang, sp.xrepeat, tx, xoff, sin,
                             &ax, &ay, &bx, &by);
  int top = 0, bottom = 0;
  tint_wall_sprite_zrange(sp.z, sp.yrepeat, ty, yoff,
                          (sp.cstat & 0x80) != 0, &top, &bottom);
  int cz = 0, fz = 0;
  const int haveSector = world_sector_zrange(sp.sectnum, &cz, &fz) ? 1 : 0;
  if (haveSector)
    tint_clamp_to_sector(cz, false, fz, false, &top, &bottom);

  {

  }

  WorldQuad3 q;
  q.p[0].x = (int)ax; q.p[0].y = (int)ay; q.p[0].z = top;
  q.p[1].x = (int)bx; q.p[1].y = (int)by; q.p[1].z = top;
  q.p[2].x = (int)bx; q.p[2].y = (int)by; q.p[2].z = bottom;
  q.p[3].x = (int)ax; q.p[3].y = (int)ay; q.p[3].z = bottom;
  q.rgba = rgba;
  q.pull = W3D_PULL_COINCIDENT;   // exactly on the wall sprite's own plane
  world3d_push_quad(q);
}

// The point-marker categories differ only in the kind they pass.
static void esp_collect_point(int kind, int index, int wx, int wy, int wz,
                              EspMarker* markers, int* count, int* seq, int* overflow) {
  ScreenPt p;
  if (!project_world_to_screen(wx, wy, wz, &p)) return;
  if (!esp_dist_ok(p)) return;
// A point has no sprite box; the anchor box is stored only because EspMarker::box is filled for every kind.
  esp_collect(markers, count, seq, overflow, kind, index, p, proj_anchor_box(p.x, p.y));
}

// Found secrets vanish on their own (checksectors zeroes the lotag on entry), so no found-set is kept; see
// WORLD_SECRET_LOTAG in esp_world.h before adding one.
static void esp_collect_secrets(EspMarker* markers, int* count, int* seq, int* overflow,
                                int* scanned, int* secret, int* nocentre) {
  const int ns = world_numsectors();
  for (int sec = 0; sec < ns; sec++) {
    (*scanned)++;
    if (!world_lotag_is_secret(world_sector_lotag(sec))) continue;
    (*secret)++;
    int wx = 0, wy = 0, wz = 0;
    if (!world_sector_centre(sec, &wx, &wy, &wz)) { (*nocentre)++; continue; }
    esp_collect_point(ESP_KIND_SECRET, sec, wx, wy, wz, markers, count, seq, overflow);
  }
}

// Walks the STANDABLE list, never nested inside the actor walks (each WorldWalk owns its cursor and budget).
static void esp_collect_cracks(EspMarker* markers, int* count, int* seq, int* overflow,
                               int* cracks) {
  WorldWalk w;
  if (!world_walk_begin(&w, ESP_STAT_STANDABLE)) return;
  int i = 0;
  while (world_walk_next(&w, &i)) {
    WorldSprite sp;
    if (!world_sprite(i, &sp)) continue;
    if (!esp_explodable_name(sp.picnum)) continue;
    // The game deletes a crack the instant it detonates, so being on this list is liveness.
    if (!esp_crack_visible(sp.hitag)) continue;
    (*cracks)++;
    esp_collect_point(ESP_KIND_EXPLODABLE, i, sp.x, sp.y, sp.z,
                      markers, count, seq, overflow);
  }
}

// WHETHER THIS LEVEL HAS A SECRET EXIT, for the corner note. Set by the switch walk (the exit button lives on statnum 5,
// MISC) BEFORE the draw-distance and frustum culls: the note says the level HAS one, not that you can see it.
static bool s_levelHasSecretExit = false;
bool esp_level_has_secret_exit() { return s_levelHasSecretExit; }

// Two sequential drains: statnum 0 for every switch, statnum 5 for the NUKEBUTTON alone.
static void esp_collect_switch_sprites(EspMarker* markers, int* count, int* seq,
                                       int* overflow, int* switches) {
  const int stats[2] = { ESP_STAT_SWITCH, ESP_STAT_NUKEBUTTON };
  for (int pass = 0; pass < 2; pass++) {
    WorldWalk w;
    if (!world_walk_begin(&w, stats[pass])) continue;
    int i = 0;
    while (world_walk_next(&w, &i)) {
      WorldSprite sp;
      if (!world_sprite(i, &sp)) continue;
      // NUKEBUTTON has no pressed frame, so it is not in switch_base_for's set.
      const bool isExit = (sp.picnum == ESP_PIC_NUKEBUTTON);
      if (pass == 1 && !isExit) continue;
      if (switch_base_for(sp.picnum) < 0 && !isExit) continue;
      if ((sp.cstat & SPR_CSTAT_INVISIBLE) != 0) continue;
      const SwitchInfo si = switch_classify(sp.picnum, sp.pal, sp.hitag, sp.lotag,
                                            esp_got_access());
      if (si.family == SW_NONE) continue;
      // Before the category gate and any cull: the note is about the LEVEL.
      if (si.family == SW_EXIT && si.exitKind == EXIT_SECRET) s_levelHasSecretExit = true;
      // An exit button answers to its OWN category: a secret exit must appear with only "Secret areas" on.
      if (si.family == SW_EXIT) {
        const int on = (si.exitKind == EXIT_SECRET) ? g_cfg.espsecrets : g_cfg.espswitches;
        if (!on) continue;
      } else if (!g_cfg.espswitches) {
        continue;
      }
      // The tint has no switch of its own: it draws whenever this category does.
      esp_tint_push(sp, si);
      (*switches)++;
      esp_collect_point(ESP_KIND_SWITCH, i, sp.x, sp.y, sp.z,
                        markers, count, seq, overflow);
    }
  }
}

// Breakable panels and switch walls come from a sector-by-sector wall walk, this file's most expensive step, hence
// cached. It caches only WHICH walls and WHERE; picnum and tags are re-read live at draw time, so a stale cache is only
// ever late to notice a NEW mark.
static EspWallMark s_wallMarks[ESP_WALL_MAX];
static int s_wallCount = 0;
static int s_wallOverflow = 0;
static int s_wallScanned = 0;
static int s_wallScanSig = 0;
static unsigned s_wallScanMs = 0;

// A level change is caught by the count signature, so a new map's marks appear on its first frame.
static const unsigned ESP_WALL_SCAN_PERIOD_MS = 1000;

static void esp_wall_scan(int wantBreakables, int wantSwitches) {
  if (!esp_world_should_scan(wantBreakables, wantSwitches)) {
// Off drops the latched marks too, so re-enabling never starts from a mark the level no longer has.
    s_wallCount = 0; s_wallOverflow = 0; s_wallScanned = 0;
    return;
  }
  const int ns = world_numsectors();
  const int nw = world_numwalls();
  const int sig = (ns << 16) ^ nw;   // cheapest "new level" proxy available
  const unsigned now = wallclock_ms();
  if (s_wallScanned && sig == s_wallScanSig && (now - s_wallScanMs) < ESP_WALL_SCAN_PERIOD_MS)
    return;
  s_wallScanned = 1;
  s_wallScanSig = sig;
  s_wallScanMs = now;
  s_wallCount = 0;
  s_wallOverflow = 0;

  for (int sec = 0; sec < ns; sec++) {
    int wallptr = 0, wallnum = 0;
// A RANGE, not two indices: a corrupt wallnum must not send this loop past the array.
    if (!world_sector_wall_range(sec, &wallptr, &wallnum)) continue;
    int ceilz = 0, florz = 0;
    if (!world_sector_zrange(sec, &ceilz, &florz)) continue;
    const int z = esp_wall_mid_z(florz, ceilz);
    for (int k = 0; k < wallnum; k++) {
      const int wi = wallptr + k;
      WorldWall wl;
      if (!world_wall(wi, &wl)) continue;
      int kind = -1;
      if (wantBreakables && esp_breakable_name(wl.picnum)) kind = ESP_WKIND_BREAKABLE;
      else if (wantSwitches && esp_wall_switch_capturable(wl.picnum, wl.lotag))
        kind = ESP_WKIND_SWITCH;
      if (kind < 0) continue;
// The midpoint needs point2's vertex; a point2 outside this sector's range is corrupt map memory.
      if (!world_point2_ok(wl.point2, wi, wallptr, wallnum)) continue;
      WorldWall other;
      if (!world_wall(wl.point2, &other)) continue;
      if (s_wallCount >= ESP_WALL_MAX) { s_wallOverflow++; continue; }
      EspWallMark& m = s_wallMarks[s_wallCount++];
      m.kind = kind;
      m.wallnum = wi;
      m.x = esp_wall_mid(wl.x, other.x);
      m.y = esp_wall_mid(wl.y, other.y);
      m.z = z;
    }
  }
}

// Each mark is filtered by its own switch so one scan serves both categories.
static void esp_collect_wall_marks(EspMarker* markers, int* count, int* seq, int* overflow,
                                   int* walls) {
  const int n = esp_wall_count_clamped(s_wallCount);
  for (int k = 0; k < n; k++) {
    const EspWallMark& m = s_wallMarks[k];
    if (!esp_wall_mark_wanted(m.kind, g_cfg.espbreakables, g_cfg.espswitches)) continue;
    // Re-read live, so a broken panel or a de-tagged switch stops drawing before the next scan.
    WorldWall wl;
    if (!world_wall(m.wallnum, &wl)) continue;
    if (m.kind == ESP_WKIND_BREAKABLE) {
      if (!esp_breakable_name(wl.picnum)) continue;
    } else {
      if (switch_classify(wl.picnum, wl.pal, wl.hitag, wl.lotag,
                          esp_got_access()).family == SW_NONE) continue;
    }
    (*walls)++;
    const int kind = (m.kind == ESP_WKIND_SWITCH) ? ESP_KIND_SWITCH : ESP_KIND_EXPLODABLE;
    esp_collect_point(kind, ESP_WALL_INDEX_BASE + m.wallnum, m.x, m.y, m.z,
                      markers, count, seq, overflow);
  }
}

// Enemy first; the two tables are disjoint by construction.
static void esp_consider(const WorldSprite& s, EspMarker* markers, int* count,
                         int* seq, int* overflow, int* enemies, int* items) {
  const bool isEnemy = esp_is_enemy(s.picnum);
  const char* itemName = isEnemy ? 0 : esp_item_name_pal(s.picnum, s.pal);
  if (!isEnemy && !itemName) return;
  if (isEnemy && !g_cfg.espenemies) return;
  if (!isEnemy && !g_cfg.espitems) return;

  if (isEnemy) {
// LIVENESS IS THE STATE FILTER, NOT A HEALTH TEST: Duke resets a corpse's extra to 0 and clears its cstat, so `extra >=
// 0` kept a body's box and bar for good. actor_state separates the cases on sprite fields alone, cached.
    const ActorState state = actor_state_cached(s.index);
    if (!actor_state_shown(state)) return;
    (*enemies)++;
// Fed before the off-screen cull: the learned-ceiling fallback (esp_pics.h) learns from every live enemy.
    esp_max_health(s.picnum, s.pal,
                   esp_display_hp(esp_hp_for_display(s.extra),
                                  esp_spawn_strength(s.picnum),
                                  esp_rule_max_health(s.picnum, s.pal),
                                  esp_actor_has_run(s.index)));
  } else {
// A respawning item is hidden by cstat bit 15, not deleted.
    if (!esp_item_present(s.cstat, s.xrepeat)) return;
    (*items)++;
  }

  ScreenPt p;
  if (!project_world_to_screen(s.x, s.y, s.z, &p)) return;
  if (!esp_dist_ok(p)) return;

  const SpriteExtents ext = proj_sprite_extents(s.z, s.cstat,
                                                world_tilesizx(s.picnum),
                                                world_tilesizy(s.picnum),
                                                s.xrepeat, s.yrepeat);
  const ScreenBox box = esp_box_or_anchor(project_sprite_box(s.x, s.y, ext), p.x, p.y);
  esp_collect(markers, count, seq, overflow,
              isEnemy ? ESP_KIND_ENEMY : ESP_KIND_ITEM, s.index, p, box);
}

static int s_loggedFirstDraw = 0;

void esp_draw(int screenW, int screenH) {
  if (!esp_any_on()) return;
  if (screenW <= 0 || screenH <= 0) return;
// ONE CACHE GENERATION PER DRAWN FRAME, turned here and not on the tic: esp_draw runs on the RENDER thread, and a
// generation turned on the game thread would hand this walk a cache another thread was still filling.
  static unsigned s_gen = 0;
  actor_state_generation_begin(++s_gen);
  if (!world_ready()) return;          // logs its own refusal, once
  if (!project_frame_begin()) return;  // no 3D frame this frame: draw nothing

  EspMarker markers[ESP_MARKER_MAX];
  int count = 0, seq = 0, overflow = 0;
  int walked = 0, enemies = 0, items = 0;
  int secScanned = 0, secSecret = 0, secNoCentre = 0;
  int cracks = 0, switches = 0, wallMarks = 0;

  // Each pass owns its gate, so an unused category pays for none of its walk.
  if (g_cfg.espenemies || g_cfg.espitems) {
    // Never interleaved; each WorldWalk owns its own cursor and cycle guard.
    const int stats[2] = { WORLD_STAT_ACTOR, WORLD_STAT_ZOMBIEACTOR };
    for (int k = 0; k < 2; k++) {
      WorldWalk w;
      if (!world_walk_begin(&w, stats[k])) continue;
      int i = 0;
      while (world_walk_next(&w, &i)) {
        walked++;
        WorldSprite s;
        if (!world_sprite(i, &s)) continue;
// SIZE ONLY, NOT VISIBILITY: cstat bit 15 would drop the enemy AS_CLOAKED exists for (an Assault Captain mid-teleport
// is invisible by that bit). Items keep the visibility test, in esp_item_present.
        if (s.xrepeat <= 0 || s.yrepeat <= 0) continue;   // zero-sized
        esp_consider(s, markers, &count, &seq, &overflow, &enemies, &items);
      }
    }
  }

  if (g_cfg.espsecrets)
    esp_collect_secrets(markers, &count, &seq, &overflow,
                        &secScanned, &secSecret, &secNoCentre);

  if (g_cfg.espexplodables)
    esp_collect_cracks(markers, &count, &seq, &overflow, &cracks);

// OR'd with espsecrets so a secret exit button is walked with only "Secret areas" on. Recomputed every frame.
  s_levelHasSecretExit = false;
  if (g_cfg.espswitches || g_cfg.espsecrets)
    esp_collect_switch_sprites(markers, &count, &seq, &overflow, &switches);

  esp_wall_scan(g_cfg.espbreakables, g_cfg.espswitches);
  if (g_cfg.espbreakables || g_cfg.espswitches)
    esp_collect_wall_marks(markers, &count, &seq, &overflow, &wallMarks);

// The note belongs to the level, so it draws on a frame where every marker was culled.
  const bool showSecretExitNote = g_cfg.espsecrets != 0 && esp_level_has_secret_exit();
  if (count == 0 && !showSecretExitNote) return;   // nothing at all, so no canvas frame

  esp_sort_far_to_near(markers, count);

// Default UI scale (at 100% the canvas resolves the BAKED atlas, which nothing releases). THE FRAME'S REMAINING ROOM
// DECIDES HOW MANY MARKERS DRAW, with ESP_PANEL_RESERVE_VERTS kept for the panel, else the menu drawn after is dropped.
  {
    const int left = overlay_verts_left() - ESP_PANEL_RESERVE_VERTS;
    const int afford = left > 0 ? left / ESP_VERTS_PER_MARKER : 0;
    if (afford < count) {
      esp_keep_nearest(markers, count, afford);
      static int said = 0;
      if (g_cfg.debuglog != 0 && said < 8) {
        said++;
        wt_log("esp: %d markers but room for %d this frame -- kept the nearest; "
               "the panel keeps %d vertices",
               count, afford, ESP_PANEL_RESERVE_VERTS);
      }
      count = afford;
    }
  }
  esp_sort_far_to_near(markers, count);

  canvas_begin(screenW, screenH);
  canvas_set_scale(UI_SCALE_DEFAULT);
  // Drawn before the markers so a marker in that corner sits over it.
  esp_draw_secret_exit_note(showSecretExitNote);
  for (int i = 0; i < count; i++) {
    switch (markers[i].kind) {
      case ESP_KIND_ENEMY:      esp_draw_enemy_marker(markers[i]);      break;
      case ESP_KIND_ITEM:       esp_draw_item_marker(markers[i]);       break;
      case ESP_KIND_SECRET:     esp_draw_secret_marker(markers[i]);     break;
      case ESP_KIND_EXPLODABLE: esp_draw_explodable_marker(markers[i]); break;
      case ESP_KIND_SWITCH:     esp_draw_switch_marker(markers[i]);     break;
      default: break;
    }
  }
  canvas_end();

  if (!s_loggedFirstDraw) {
    s_loggedFirstDraw = 1;
    wt_log("esp: first drawn frame, walked %d sprites on statnum %d/%d "
           "(%d enemies, %d pickups), sectors %d of which %d unfound secrets "
           "(%d with no readable centre), %d cracks, %d switch sprites, "
           "%d wall marks of %d cached%s, %d markers%s",
           walked, WORLD_STAT_ACTOR, WORLD_STAT_ZOMBIEACTOR, enemies, items,
           secScanned, secSecret, secNoCentre, cracks, switches,
           wallMarks, s_wallCount,
           s_wallOverflow ? " (some dropped: the wall cache is full)" : "",
           count, overflow ? " (some dropped: the marker table is full)" : "");
  }
}

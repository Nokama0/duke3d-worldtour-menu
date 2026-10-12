// Enemy and pickup picnums, names and spawn health, from the game's own CON data (build "Pagoda-DEF" 386). Pure.
#ifndef WTMOD_ESP_PICS_H
#define WTMOD_ESP_PICS_H

// Null when not an enemy. A named entry may have a null name: esp.cpp draws the box with no label.
const char* esp_enemy_name(int picnum);

// An Assault Captain is a LIZTROOP* with pal 21, so a picnum-only key labels every Captain "Assault Trooper".
const char* esp_enemy_name_pal(int picnum, int pal);

// GAME.CON's overrides run on an actor's FIRST EXECUTED TIC (`ifai 0`), so a dormant Assault Captain (spawn 30, max 60)
// would read 30/60. Also requires health at spawn strength, so a misread hasRun costs one wrong number, not a full bar.
inline int esp_display_hp(int hp, int spawnStrength, int ruledMax, bool hasRun) {
  if (hasRun) return hp;
  if (ruledMax <= 0 || ruledMax == spawnStrength) return hp;   // no override pending
  return hp == spawnStrength ? ruledMax : hp;
}

// No badguy() equivalent exists here, so THIS TABLE IS the classification: a missing hostile gets no marker.
bool esp_is_enemy(int picnum);

const char* esp_item_name(int picnum);

// The keycard: its pal says which lock it opens.
const char* esp_item_name_pal(int picnum, int pal);

const int ESP_PIC_ACCESSCARD = 60;
const int ESP_PAL_KEY_BLUE   = 0;
const int ESP_PAL_KEY_RED    = 21;
const int ESP_PAL_KEY_YELLOW = 23;

// Table sizes and entry i's picnum (-1 out of range), so tables can be swept.
int esp_enemy_count();
int esp_enemy_picnum(int i);
int esp_item_count();
int esp_item_picnum(int i);

// Max health comes from the CON source baked into the table, GAME.CON's two first-tic overrides below, and a learned
// ceiling when the table is silent.

// What spawn() puts in `extra`, or 0 when no CON declares it (RECON, RAT, GREENSLIME, the feeding Enforcer).
int esp_spawn_strength(int picnum);

const int ESP_PAL_BOOSTED = 21;

// `state checktrooppalette` adds this to a pal-21 Assault Trooper on its first tic.
const int ESP_TROOP_BOOST = 30;

// BOSS1..BOSS5PALSTRENGTH (USER.CON): a pal-21 boss has its strength ASSIGNED this, not added to it.
const int ESP_BOSS_PAL_STRENGTH = 1000;

// The eight picnums whose actors invoke `state checktrooppalette`.
bool esp_is_trooper(int picnum);

// The ten boss picnums whose CON has an `ifspritepal 21` strength branch.
bool esp_is_paletted_boss(int picnum);

// The spawn strength with those two overrides on top; 0 when no maximum can be stated truthfully.
int esp_rule_max_health(int picnum, int pal);

// Last resort: the highest health ever seen for this (picnum, pal). A non-positive `current` establishes nothing; past
// capacity a new pair reports no maximum rather than a full bar.
int esp_track_max_health(int picnum, int pal, int current);

// The rule first, the learned ceiling only when the rule is silent; never less than `current`. Zero is answered
// normally (Duke keeps actors alive at zero: an empty bar); a negative or garbage read reports none.
int esp_max_health(int picnum, int pal, int current);

// Non-zero: the learned table is full and some enemies show no bar.
int esp_max_health_overflow();

void esp_max_health_reset();

const int ESP_MAX_HEALTH_TYPES = 64;

// What sprite.extra can hold; more is a garbage read.
const int ESP_STRENGTH_MAX = 32767;


// CRACK1..CRACK4, the wall-aligned sprites the game detonates. Necessary but not sufficient for a marker:
// esp_crack_visible (esp_world.h) is the separate hitag gate.
const char* esp_explodable_name(int picnum);
int esp_explodable_count();
int esp_explodable_picnum(int i);

// Destructible wall panels. Once broken the engine rewrites the picnum to a ...BROKE tile outside this set, so "still in
// this set" is "not yet broken".
const char* esp_breakable_name(int picnum);
int esp_breakable_count();
int esp_breakable_picnum(int i);

#endif

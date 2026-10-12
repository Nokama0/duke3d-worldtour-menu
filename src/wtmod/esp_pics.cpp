// See esp_pics.h for where every number in this file was read from.
#include "esp_pics.h"

// picnum, the game's own word for it (not the CON identifier: "Assault Trooper", not "LIZTROOP"; every animation and
// stay-put variant shares it), and the strength its CON `actor` line declares (0 where the engine spawns it).
struct EnemyEntry { int picnum; const char* name; int strength; };

static const EnemyEntry ENEMIES[] = {
  {  675, "Egg",                 20 },  // EGG, the Alien Queen's; actor EGG TOUGH
  { 1267, "Rat",                  0 },  // RAT, spawned by the engine, no actor line
  { 1550, "Shark",               35 },
  { 1680, "Assault Trooper",     30 },
  { 1681, "Assault Trooper",     30 },
  { 1682, "Assault Trooper",     30 },
  { 1715, "Assault Trooper",     30 },
  { 1725, "Assault Trooper",     30 },
  { 1741, "Assault Trooper",     30 },
  { 1742, "Assault Trooper",     30 },
  { 1744, "Assault Trooper",     30 },
  { 1810, "Cannon",             400 },  // CANNON, useractor enemy
  { 1820, "Octabrain",          175 },
  { 1821, "Octabrain",          175 },
  { 1880, "Sentry Drone",       150 },
  { 1920, "Assault Commander",  350 },
  { 1921, "Assault Commander",  350 },
  { 1960, "Recon Patrol",         0 },  // RECON, engine-spawned, no actor line
  { 1975, "Pig Cop Tank",       500 },  // TANK, useractor enemy
  { 2000, "Pig Cop",            100 },
  { 2001, "Pig Cop",            100 },
  { 2045, "Pig Cop",            100 },
  { 2120, "Enforcer",           100 },
  { 2121, "Enforcer",           100 },
  { 2150, "Enforcer",           100 },
  { 2160, "Enforcer",             0 },  // LIZMANFEEDING, no actor line of its own
  { 2165, "Enforcer",           100 },
  { 2360, "Turret",              40 },  // ROTATEGUN, actor ROTATEGUN ROTTURRETSTRENGTH
  { 2370, "Protozoid Slimer",     0 },  // GREENSLIME and its seven animation
  { 2371, "Protozoid Slimer",     0 },  // states, all engine-driven with no
  { 2372, "Protozoid Slimer",     0 },  // actor line to read a strength from
  { 2373, "Protozoid Slimer",     0 },
  { 2374, "Protozoid Slimer",     0 },
  { 2375, "Protozoid Slimer",     0 },
  { 2376, "Protozoid Slimer",     0 },
  { 2377, "Protozoid Slimer",     0 },
  { 2420, "Organic turret",      30 },  // ORGANTIC, actor ORGANTIC TURRETSTRENGTH
  { 2630, "Battlelord",        4500 },
  { 2631, "Battlelord",        4500 },
  { 2710, "Overlord",          4500 },
  { 2711, "Overlord",          4500 },
  { 2760, "Cycloid Emperor",   4500 },
  { 2761, "Cycloid Emperor",   4500 },
  { 4610, "Protector Drone",    300 },
  { 4611, "Protector Drone",    300 },
  { 4670, "Protector Drone",    300 },
  { 4671, "Protector Drone",     20 },  // NEWBEASTHANGDEAD, useractor enemy TOUGH
  { 4690, "Protector Drone",    300 },
  { 4740, "Alien Queen",       6000 },
  { 4741, "Alien Queen",       6000 },
  // World Tour's own two, from its own scripts.
  { 5180, "Firefly Trooper",     50 },  // FIREFLY, FIREFLYTROOPER.CON, FF_STRENGTH
  { 5310, "Alien Overlord",    6000 },  // BOSS5, EPISODE5BOSS.CON
  { 5311, "Alien Overlord",    6000 },
};

static const int ENEMY_COUNT = (int)(sizeof(ENEMIES) / sizeof(ENEMIES[0]));

// A linear scan, deliberately: fifty-odd entries; a sorted table would be harder to read against the CON.
static const EnemyEntry* enemy_entry(int picnum) {
  for (int i = 0; i < ENEMY_COUNT; i++)
    if (ENEMIES[i].picnum == picnum) return &ENEMIES[i];
  return 0;
}

const char* esp_enemy_name(int picnum) {
  const EnemyEntry* e = enemy_entry(picnum);
  return e ? e->name : 0;
}
bool esp_is_enemy(int picnum) { return enemy_entry(picnum) != 0; }
int esp_enemy_count() { return ENEMY_COUNT; }
int esp_enemy_picnum(int i) { return (i >= 0 && i < ENEMY_COUNT) ? ENEMIES[i].picnum : -1; }

int esp_spawn_strength(int picnum) {
  const EnemyEntry* e = enemy_entry(picnum);
  return e ? e->strength : 0;
}

// Names come from the quote each pickup prints ("AMMO FOR CHAINGUN CANNON!" -> "Chaingun ammo"). HEAVYHBOMB (26) is
// omitted: it is both the pipebomb-box pickup and every live thrown pipebomb; HBOMBAMMO (47) is unambiguous.
struct ItemEntry { int picnum; const char* name; };

static const ItemEntry ITEMS[] = {
  {   21, "Pistol" },
  {   22, "Chaingun cannon" },
  {   23, "RPG" },
  {   24, "Freezethrower" },
  {   25, "Shrinker" },
  {   27, "Laser tripbomb" },
  {   28, "Shotgun" },
  {   29, "Devastator" },
  {   37, "Freezethrower ammo" },
  {   40, "Pistol ammo" },
  {   41, "Chaingun ammo" },
  {   42, "Devastator ammo" },
  {   44, "RPG ammo" },
  {   45, "Expander ammo" },
  {   46, "Shrinker ammo" },
  {   47, "Pipebombs" },
  {   48, "Pistol ammo" },           // AMMOLOTS, the big box
  {   49, "Shotgun ammo" },
  {   51, "Small medkit" },
  {   52, "Large medkit" },
  {   53, "Portable medkit" },
  {   54, "Armour" },
  {   55, "Steroids" },
  {   56, "Scuba gear" },
  {   57, "Jetpack" },
  {   59, "Night vision goggles" },
  {   60, "Keycard" },               // ACCESSCARD, coloured by pal below
  {   61, "Boots" },
  {  100, "Atomic health" },
  { 1348, "Holoduke" },
  // World Tour's own weapon and its ammo, from FLAMETHROWER.CON's two `actor` blocks.
  { 5134, "Flamethrower" },
  { 5135, "Flamethrower ammo" },
};

static const int ITEM_COUNT = (int)(sizeof(ITEMS) / sizeof(ITEMS[0]));

const char* esp_item_name(int picnum) {
  for (int i = 0; i < ITEM_COUNT; i++)
    if (ITEMS[i].picnum == picnum) return ITEMS[i].name;
  return 0;
}
int esp_item_count() { return ITEM_COUNT; }
int esp_item_picnum(int i) { return (i >= 0 && i < ITEM_COUNT) ? ITEMS[i].picnum : -1; }

const char* esp_item_name_pal(int picnum, int pal) {
  const char* nm = esp_item_name(picnum);
  if (nm == 0 || picnum != ESP_PIC_ACCESSCARD) return nm;
  if (pal == ESP_PAL_KEY_BLUE)   return "Blue keycard";
  if (pal == ESP_PAL_KEY_RED)    return "Red keycard";
  if (pal == ESP_PAL_KEY_YELLOW) return "Yellow keycard";
  return nm;   // a pal the game itself has no lock colour for
}

// The eight picnums whose actors invoke `state checktrooppalette`; 1683..1714 are animation frames.
bool esp_is_trooper(int picnum) {
  return picnum == 1680 || picnum == 1681 || picnum == 1682 || picnum == 1715
      || picnum == 1725 || picnum == 1741 || picnum == 1742 || picnum == 1744;
}

// Every boss and stay-put boss whose CON has the `ifspritepal 21 -> strength BOSSnPALSTRENGTH` branch.
bool esp_is_paletted_boss(int picnum) {
  return picnum == 2630 || picnum == 2631 || picnum == 2710 || picnum == 2711
      || picnum == 2760 || picnum == 2761 || picnum == 4740 || picnum == 4741
      || picnum == 5310 || picnum == 5311;
}

// pal 21 on a trooper picnum is an Assault Captain (GAME.CON doubles its hitpoints); other enemies go by picnum alone.
const char* esp_enemy_name_pal(int picnum, int pal) {
  if (pal == ESP_PAL_BOOSTED && esp_is_trooper(picnum)) return "Assault Captain";
  return esp_enemy_name(picnum);
}

int esp_rule_max_health(int picnum, int pal) {
  const int base = esp_spawn_strength(picnum);
  if (base <= 0 || base > ESP_STRENGTH_MAX) return 0;

  if (pal == ESP_PAL_BOOSTED) {
    // A pal-21 boss has its strength REPLACED, not added to.
    if (esp_is_paletted_boss(picnum)) return ESP_BOSS_PAL_STRENGTH;
    if (esp_is_trooper(picnum)) {
      // Widened: the sum of two bounded operands is not itself bounded.
      const long long boosted = (long long)base + (long long)ESP_TROOP_BOOST;
      return boosted > ESP_STRENGTH_MAX ? 0 : (int)boosted;
    }
  }
  return base;
}

// Keyed on (picnum, pal) like the rule, so a 60-HP trooper is never conflated with a 30-HP one.
static int s_pic[ESP_MAX_HEALTH_TYPES];
static int s_pal[ESP_MAX_HEALTH_TYPES];
static int s_max[ESP_MAX_HEALTH_TYPES];
static int s_count = 0;
static int s_overflow = 0;

int esp_track_max_health(int picnum, int pal, int current) {
  if (current <= 0) return 0;
  for (int i = 0; i < s_count; i++) {
    if (s_pic[i] == picnum && s_pal[i] == pal) {
      if (current > s_max[i]) s_max[i] = current;
      return s_max[i];
    }
  }
  if (s_count < ESP_MAX_HEALTH_TYPES) {
    s_pic[s_count] = picnum;
    s_pal[s_count] = pal;
    s_max[s_count] = current;
    s_count++;
    return current;
  }
  // Past capacity: no maximum, so the label is a bare number and no bar (a permanently full bar would be wrong).
  if (s_overflow < 2147483647) s_overflow++;
  return 0;
}

int esp_max_health(int picnum, int pal, int current) {
  // `< 0`, not `<= 0`: Duke keeps actors alive and shootable at zero health, so 0 wants an empty bar. Negative is a
  // garbage read.
  if (current < 0) return 0;
  const int ruled = esp_rule_max_health(picnum, pal);
  if (ruled > 0) {
    // Never below current, so the bar fill never draws past full.
    return ruled > current ? ruled : current;
  }
  return esp_track_max_health(picnum, pal, current);
}

int esp_max_health_overflow() { return s_overflow; }

void esp_max_health_reset() {
  s_count = 0;
  s_overflow = 0;
}

// These two tables come from movestandables and checkhitwall's case lists; every picnum was resolved in THIS game's
// names.h (build "Pagoda-DEF" 386).

// CRACK1..CRACK4 = 546..549 (names.h 174-177). One name for all four.
static const int EXPLODABLE_PICS[] = { 546, 547, 548, 549 };
static const int EXPLODABLE_COUNT =
    (int)(sizeof(EXPLODABLE_PICS) / sizeof(EXPLODABLE_PICS[0]));

const char* esp_explodable_name(int picnum) {
  for (int i = 0; i < EXPLODABLE_COUNT; i++)
    if (EXPLODABLE_PICS[i] == picnum) return "Explodable";
  return 0;
}
int esp_explodable_count() { return EXPLODABLE_COUNT; }
int esp_explodable_picnum(int i) {
  return (i >= 0 && i < EXPLODABLE_COUNT) ? EXPLODABLE_PICS[i] : -1;
}

// The nineteen destructible panels: W_TECHWALL variants (293..307, 4130..4142), VENDMACHINE 1212, COLAMACHINE 1215 and
// W_MILKSHELF 4181. Their broken counterparts lie outside the set, so membership means "not yet broken".
static const int BREAKABLE_PICS[] = {
  293, 297, 299, 301, 305, 306, 307,
  1212, 1215,
  4130, 4131, 4132, 4133, 4134, 4136, 4138, 4140, 4142, 4181
};
static const int BREAKABLE_COUNT =
    (int)(sizeof(BREAKABLE_PICS) / sizeof(BREAKABLE_PICS[0]));

const char* esp_breakable_name(int picnum) {
  for (int i = 0; i < BREAKABLE_COUNT; i++)
    if (BREAKABLE_PICS[i] == picnum) return "Breakable";
  return 0;
}
int esp_breakable_count() { return BREAKABLE_COUNT; }
int esp_breakable_picnum(int i) {
  return (i >= 0 && i < BREAKABLE_COUNT) ? BREAKABLE_PICS[i] : -1;
}

// Megaton's config.h plus this port's own keys (the "World Tour's own settings" block below); everything above it is
// Megaton's, so the copied menu.cpp reads the fields it expects. File: wtmod.ini beside the executable.
#pragma once

// config_load's per-line read buffer, shared with the tests that check overlong/boundary lines.
const int CONFIG_LINE_BUFSIZE = 512;

struct Config {
  int menukey;   // virtual-key code that opens the panel; default VK_INSERT (0x2D)
  int debuglog;
  // Separate from debuglog (~6000 lines of chatter): one line per shot plus the jumpiness summary and vanish ledger.
  int debugaim;
  // The master is 1 (the stock game): the feature is the ABILITY to turn automatic switching off.
  int wswitchauto;
  int wswitchonpickup;
  int wswitchonempty;
  int wswitchprefer;     // 0 = engine default, else 1..10 (autoswitch.h)
  int uiscale;   // percent, UI_SCALE_MIN..UI_SCALE_MAX (ui.h)
  // Logical pixels at scale 100; -1/-1/0/0 is "unset" (panel_layout.h uses its default box).
  int panelx, panely, panelw, panelh;
  int fovon;
  int fov;       // degrees, 70..140, default 90
  int espenemies; // the parent switch for enemy ESP; defaults OFF
  int espboxes;
  int esphealth;
  int espnames;
  int espdistance;
  int espitems;
  int espsecrets;
  int espexplodables;  // crack sprites
  int espbreakables;   // breakable wall panels
  int espswitches;
  int dmgnumbers;
  // Switches whose lotag chain ends at a secret sector.
  int secrettriggers;
  // Sub-switch of espsecrets. Renamed from `triggervolume` (old key still loads).
  int secretvolume;
  // Sub-switch of espsecrets. A HINT: it cannot see a keycard, locked activator or one-way drop.
  int espsecretpath;
  // BUILD units (512/metre) for every ESP category and dmgnumbers/secrettriggers; defaults to the MAXIMUM.
  int espdrawdist;
  // 1 turns the range cull off: every marker draws.
  int espnolimit;
  int aimon;      // the parent switch for aim assist (default off)
  // 1 = assist only while the hold button is down, 0 = always while aimon.
  int aimhold;
  // Shared VK space (bind.h), default Mouse 5. Replaces the raw SDL `aimbutton`, migrated once on load.
  int aimbind;
  int aimtarget;  // 0 nearest the crosshair, 1 nearest the player (TargetMode)
  int aimpoint;   // 0 mid-body, 1 upper body (AimPoint)
  int aimsmooth;  // AIM_SMOOTH_MIN..MAX (target.h); 0 snaps
  int aimcone;    // degrees, AIM_CONE_MIN..MAX (target.h), crosshair mode only
  // Defaults ON. Reaches the shot only with Duke's auto-aim OFF (otherwise `shoot` re-derives the aim; see aim.h).
  int aimlead;
  int aimdebug;
  // Percentage of the computed lead that reaches the aim point; defaults 70, since Duke's burst-and-stop AI makes a
  // full lead overshoot.
  int aimleadscale;
  // Independent of espdrawdist (aim_within_range).
  int aimrange;
  // `tbot`, not `trig`: secret trigger ESP owns that prefix.
  int tboton;
  int tbotwhen;   // TBOT_WHEN_CROSSHAIR (0) or TBOT_WHEN_AIMBOT (1)
  int tbotdelay;  // milliseconds the condition must hold, 0..500
  int autosavemins;  // 0 = off (autosave.h)
  int autosaveslot;  // 0..9; ini-only
  // Independent of aimon. Holding auto_aim off stops the re-derive but the RPG's homing lock reads that result too.
  int silenton;
  int silentmaxangle;  // degrees, SILENT_MAXANGLE_MIN..MAX
  // Only gates the toggle key; the camera's state lives in camera.cpp.
  int freecamon;
  // Clamped 1..254. Default 0x56 (V): F6 collided with Megaton's Quick_Save.
  int freecamkey;
  // Lands the player at the look-ray's hit; the camera keeps flying.
  int freecamteleportkey;
  // camera.cpp ORs these with the game's bindings, so a rebound Crouch is kept and C descends as well.
  int freecamfwdkey, freecambackkey, freecamleftkey, freecamrightkey;
  int freecamupkey, freecamdownkey, freecamfastkey, freecamslowkey;
  int freecamspeed;       // percentage of FREECAM_BASE_SPEED, 1..500
  // `freecamsens` is retired; config_load still recognises it so an old ini loses the line.
  int freecamshowplayer;
  // Defaults off: the forced sort is O(bunches^2) per frame (nocull.h).
  int freecamnocull;
  int freecamdebug;      // own switch, so reading numbers needs no log
  // DEFAULTS ON: a correction, not a feature (freecam.h's transports).
  int freecamtransport;
  int freecamteleportmarker;
  int freecamfog;         // light.h
  int fogvisibility;      // ini only: what "Remove fog" writes over every sector's visibility byte; 0 is least fog
  // DIAGNOSTIC ONLY, ini only: non-zero makes silent aim use the player's angle plus this many BUILD UNITS (2048 to the
  // circle), to tell whether the write reaches shoot.
  int silenttest;
  int freecamfullbright;
  int freecamshot;        // hides weapon, crosshair, HUD and markers while flying (shot.h)
  int fullbright;         // during play, camera or not
  int mouseaimon;      // modern (raw, equal-axis) mouse aim
  int mouseaimsens;    // degrees of yaw per raw count, x1000 (MOUSEAIM_SENS_MIN..MAX)
  int mouseaimvscale;  // pitch as a percentage of the yaw rate
  int mouseaimleadcap;   // ini-only: per-frame yaw lead cap in angle units, 0 = the built-in MOUSEAIM_LEAD_CAP_UNITS
  // Decided per shot from `atwith` (accuracy.h), even where the engine shares a spread branch.
  int accpistol;    // atwith 0xa23
  int accchaingun;  // atwith 0x9e8
  int accshotgun;   // atwith 0xa35
  int accexpander;  // atwith 0x990
  int firerate;     // percentage of stock (FIRERATE_MIN..MAX in FIRERATE_STEP steps)
  int menuanim;     // 0 Off, 1 Simple, 2 Glass (ANIM_*, anim.h)
  int switchspeed;  // same range and step as firerate

  // World Tour's own settings. `panelkey` -> menukey, `mouseaim` -> mouseaimon; mouseaimsens/vscale keep Megaton's names
  // but THIS port's range and default (mouseaim.h).
  int borderless;   // restyle the window and fill the monitor it is on
  // ini keys x/y/w/h; named win* because panelx..panelh are the PANEL's box.
  int winx, winy, winw, winh;
  int dump;         // write the decrypted image out once, for analysis
  int fpscap;       // 0 = off, else 20..1000
  int smoothview;   // draw the camera at a fractional angle
  int overlay;      // draw our own pixels over the game's at all
  int readout;      // the overlay's own text readout; defaults ON
  int canvastest;
  // Ctrl+this copies the viewpoint, Shift+this flies to the clipboard's (campos.h). Ini-only, clamped 1..254.
  int freecamcopykey;
  int freecampastekey;
  // The GAME has no slot for these (it stores nothing past index 4): an index into MB_ACTIONS, 0 is off.
  int mouse4action;
  int mouse5action;
};

// Longest "state: <key> <old> -> <new>" line, with room to spare: callers size one stack buffer and never truncate.
const int CONFIG_STATE_LINE_CAP = 96;

// Every 0/1 field, in ini-key order; RANGES are excluded so a dragged slider doesn't log a line per pixel.
int config_switch_count();
const char* config_switch_name(int i);
int config_switch_value(const Config& c, int i);

// Empty string (buf untouched) for a null key/buffer or cap <= 0.
const char* config_state_line(const char* key, int oldV, int newV, char* buf, int cap);

// Writes differing table entries to outIdx; returns the count (never more than cap).
int config_state_diff(const Config& prev, const Config& cur, int* outIdx, int cap);

// In config.cpp because it logs.
void config_log_state_changes(const Config& prev, const Config& cur);

// The panel's four binding rows (BIND_ROW_*, bind.h) map to four Config members; an out-of-range row reads 0 and
// writes nothing.
int  config_bind_get(const Config& c, int row);
void config_bind_set(Config& c, int row, int vk);

// Logged when config_load resets a saved binding: an old `aimbutton=1` (Mouse 1) is no longer bindable.
const char* const CONFIG_LOG_AIMBUTTON1_RESET =
    "aim button Mouse 1 is no longer bindable, reset to Mouse 5";

extern Config g_cfg;
extern bool g_cfgDirty;

// Worked out once in wtmod.cpp. Empty before config_load.
const char* wt_ini_path();

void config_load(const char* path);
bool config_save(const char* path);

// What the panel calls instead of config_save, which rewrites the whole file and would delete the player's comments;
// this writes only changed keys. Clears g_cfgDirty either way.
void config_write_changed(const char* path);

// Records that the file and g_cfg agree (what config_write_changed measures against); a caller writing the file
// another way must call it.
void config_note_saved();

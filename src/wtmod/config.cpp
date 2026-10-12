// Megaton's config.cpp extended with this port's keys; three marked blocks are this port's (defaults, parse arms, save
// lines for the World Tour settings). Everything else is Megaton's, unedited.
#include "config.h"
#include "autoswitch.h"
#include "common.h"
#include "log.h"
#include "fov.h"
#include "light.h"       // fog_visibility_clamped, FOG_VISIBILITY_DEFAULT
#include "esp_layout.h"   // ESP_DRAWDIST_DEFAULT, esp_draw_distance_clamped
#include "target.h"       // AIM_CONE_DEFAULT, aim_cone_clamped, aim_smooth_clamped
#include "input.h"        // MOUSE_BUTTON_MIN/MAX for the legacy aimbutton SDL button
#include "bind.h"         // VK_BIND_XBUTTON2, bind_from_sdl_mouse, BIND_ROW_* enum
#include "freecam.h"      // FREECAM_SPEED_DEFAULT, freecam_speed_clamped
#include "ui.h"
#include "mouseaim.h"     // MOUSEAIM_SENS/VSCALE_DEFAULT, their _clamped functions
#include "firerate.h"     // FIRERATE_MIN/MAX/STEP, firerate_rate_clamped
#include "tbot.h"
#include "anim.h"         // ANIM_OFF, anim_style_clamped
#include "autosave.h"     // AUTOSAVE_*_DEFAULT, autosave_mins_clamped, autosave_slot_clamped
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include "mousebind.h"   // mousebind_action_clamped: the thumb-button rows

Config g_cfg;
bool g_cfgDirty = false;

// Lines we did not recognise, kept verbatim so a newer config survives an older DLL (capacity limited; beyond it they
// are dropped).
static char s_unknown[128][CONFIG_LINE_BUFSIZE];
static int  s_unknownCount = 0;

static void set_defaults() {
  g_cfg.menukey  = 0x2D;   // VK_INSERT
  g_cfg.debuglog = 0;
  g_cfg.debugaim  = 0;
  g_cfg.wswitchauto     = 1;   // stock behaviour out of the box
  g_cfg.wswitchonpickup = 1;
  g_cfg.wswitchonempty  = 1;
  g_cfg.wswitchprefer   = AUTOSWITCH_PREFER_DEFAULT;
  g_cfg.uiscale  = UI_SCALE_DEFAULT;
  g_cfg.panelx = -1; g_cfg.panely = -1; g_cfg.panelw = 0; g_cfg.panelh = 0;
  g_cfg.fovon    = 0;
  // FOV_DEFAULT, not literal 90: the View tab's reset glyph restores the same
  // constant, so the two cannot disagree.
  g_cfg.fov      = FOV_DEFAULT;
  g_cfg.espenemies = 0;
  g_cfg.espboxes    = 1;
  g_cfg.esphealth   = 1;
  g_cfg.espnames    = 1;
  g_cfg.espdistance = 1;
  g_cfg.espitems    = 0;   // parent switch, every feature defaults off
  g_cfg.espsecrets  = 0;
  g_cfg.espexplodables = 0;
  g_cfg.espbreakables  = 0;
  g_cfg.espswitches    = 0;
  g_cfg.dmgnumbers     = 0;
  g_cfg.secrettriggers = 0;
  g_cfg.secretvolume   = 0;   // sub-switch of espsecrets
  g_cfg.espsecretpath  = 0;   // sub-switch of espsecrets too
  g_cfg.espdrawdist = ESP_DRAWDIST_DEFAULT;
  g_cfg.espnolimit = 0;
  g_cfg.aimon     = 0;
  g_cfg.aimhold   = 1;   // hold to aim: the conservative half of an off switch
  g_cfg.aimbind   = VK_BIND_XBUTTON2;   // Mouse 5, the outer thumb button
  g_cfg.aimtarget = 0;   // nearest the crosshair
  g_cfg.aimpoint  = 0;   // mid-body, the widest part of the target
  g_cfg.aimsmooth = AIM_SMOOTH_DEFAULT;   // snap: the whole remaining turn, every tic
  g_cfg.aimcone   = AIM_CONE_DEFAULT;
  g_cfg.aimlead   = 1;   // sub-option of a switch already off
  g_cfg.aimleadscale = AIM_LEADSCALE_DEFAULT;   // a tuning default, see config.h
  g_cfg.aimrange = AIM_RANGE_DEFAULT;    // independent of espdrawdist; defaults to max
  g_cfg.aimdebug  = 0;
  g_cfg.silenton       = 0;
  g_cfg.silentmaxangle = SILENT_MAXANGLE_DEFAULT;
  // Default toggle key V, not F6: F6 is stock Megaton's Quick_Save, and V is unbound in both key tables beside WASD.
  g_cfg.freecamon         = 0;
  g_cfg.freecamkey        = 0x56;   // 'V'
  g_cfg.freecamteleportkey = 0x54;  // 'T'
  g_cfg.freecamfwdkey   = 0x57;   // 'W'
  g_cfg.freecambackkey  = 0x53;   // 'S'
  g_cfg.freecamleftkey  = 0x41;   // 'A'
  g_cfg.freecamrightkey = 0x44;   // 'D'
  g_cfg.freecamupkey    = 0x20;   // VK_SPACE
  g_cfg.freecamdownkey  = 0x43;   // 'C'
  g_cfg.freecamfastkey  = 0xA0;   // VK_LSHIFT
  g_cfg.freecamslowkey  = 0xA2;   // VK_LCONTROL
  g_cfg.freecamspeed      = FREECAM_SPEED_DEFAULT;
  g_cfg.freecamshowplayer = 0;
  g_cfg.freecamnocull     = 0;
  g_cfg.freecamfog        = 0;
  g_cfg.fogvisibility = FOG_VISIBILITY_DEFAULT;
  g_cfg.silenttest = 0;
  g_cfg.freecamfullbright = 0;
  g_cfg.freecamshot       = 0;
  g_cfg.fullbright        = 0;
  g_cfg.mouseaimon     = 0;
  g_cfg.mouseaimsens   = MOUSEAIM_SENS_DEFAULT;
  g_cfg.mouseaimvscale = MOUSEAIM_VSCALE_DEFAULT;
  g_cfg.mouseaimleadcap = 0;
  g_cfg.accpistol   = 0;
  g_cfg.accchaingun = 0;
  g_cfg.accshotgun  = 0;
  g_cfg.accexpander = 0;
  g_cfg.firerate = FIRERATE_DEFAULT;
  g_cfg.menuanim = ANIM_OFF;   // panel draws as it always did
  g_cfg.switchspeed = FIRERATE_DEFAULT;   // same identity-stop range as firerate
  g_cfg.tboton = 0;
  g_cfg.tbotwhen = TBOT_WHEN_CROSSHAIR;
  g_cfg.tbotdelay = TBOT_DELAY_MS_DEFAULT;
  g_cfg.autosavemins = AUTOSAVE_MINS_DEFAULT;   // 0: off, like every other feature
  g_cfg.autosaveslot = AUTOSAVE_SLOT_DEFAULT;
  g_cfg.freecamdebug = 0;
  g_cfg.freecamtransport = 1;
  g_cfg.freecamteleportmarker = 0;
  g_cfg.freecamcopykey  = 0x2D;   // Ctrl+Insert
  g_cfg.freecampastekey = 0x2D;   // Shift+Insert
  g_cfg.mouse4action = 0;         // off: the game never had these bound either
  g_cfg.mouse5action = 0;
  // --- World Tour's own settings: the defaults read_cfg used. menukey/mouseaimon/mouseaimsens/mouseaimvscale are set
  // above under Megaton's field names.
  g_cfg.borderless = 0;
  g_cfg.winx = 0; g_cfg.winy = 0; g_cfg.winw = 0; g_cfg.winh = 0;
  g_cfg.dump       = 0;
  g_cfg.fpscap     = 0;    // 0 is off, and off is what the game ships as
  g_cfg.smoothview = 0;
  g_cfg.overlay    = 0;
  // OFF: the port's early debug box (fps, player position and angle) had no menu row, so it stayed on; the ini still
  // reads it.
  g_cfg.readout    = 0;
  g_cfg.canvastest = 0;
}

// Real mouse aiming turned the two mouse-aim ranges from a percentage trim into degrees of yaw per raw count x1000;
// an old value can fall outside the new range, so say so once rather than clamp silently.
static void note_mouseaim_clamp(const char* key, int had, int now) {
  if (had == now) return;
  LOGI("cfg: %s=%d is outside mouse aim's range now that it means degrees per count; using %d",
       key, had, now);
}

static void trim(char* s) {
  size_t n = strlen(s);
  while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\t')) s[--n] = 0;
}

void config_load(const char* path) {
  set_defaults();
  s_unknownCount = 0;
  g_cfgDirty = false;

  FILE* f = nullptr;
  if (fopen_s(&f, path, "r") != 0 || !f) { config_note_saved(); return; }

  bool sawAimbind = false;
  // Whether the legacy value that decided aimbind was Mouse 1; reported after the whole file so a superseding value
  // isn't logged as reset.
  bool resetMouse1 = false;
  bool sawSecretVolume = false;
  char line[CONFIG_LINE_BUFSIZE];
  while (fgets(line, sizeof(line), f)) {
    // No newline could mean truncation OR a line that exactly fills the buffer: peek one more char; anything but EOF
    // or a newline is the rest of the line and must be drained.
    if (!strchr(line, '\n')) {
      int c = fgetc(f);
      if (c != EOF && c != '\n') {         // more of this line follows: truly truncated
        while (c != '\n' && c != EOF) c = fgetc(f);
        continue;                          // skip the fragment entirely
      }
    }

    trim(line);
    if (!line[0] || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    const char* key = line;
    int val = atoi(eq + 1);

    if      (!strcmp(key, "menukey"))  g_cfg.menukey  = clamp_int(val, 1, 254);
    else if (!strcmp(key, "debuglog")) g_cfg.debuglog = val ? 1 : 0;
    else if (!strcmp(key, "debugaim"))  g_cfg.debugaim  = val ? 1 : 0;
    else if (!strcmp(key, "wswitchauto"))     g_cfg.wswitchauto     = val ? 1 : 0;
    else if (!strcmp(key, "wswitchonpickup")) g_cfg.wswitchonpickup = val ? 1 : 0;
    else if (!strcmp(key, "wswitchonempty"))  g_cfg.wswitchonempty  = val ? 1 : 0;
    else if (!strcmp(key, "wswitchprefer"))   g_cfg.wswitchprefer   = autoswitch_prefer_clamped(val);
    else if (!strcmp(key, "uiscale"))  g_cfg.uiscale  = ui_scale_clamped(val);
    else if (!strcmp(key, "panelx"))   g_cfg.panelx   = val < -1 ? -1 : val;
    else if (!strcmp(key, "panely"))   g_cfg.panely   = val < -1 ? -1 : val;
    else if (!strcmp(key, "panelw"))   g_cfg.panelw   = val < 0 ? 0 : val;
    else if (!strcmp(key, "panelh"))   g_cfg.panelh   = val < 0 ? 0 : val;
    else if (!strcmp(key, "fovon"))    g_cfg.fovon    = val ? 1 : 0;
    else if (!strcmp(key, "fov"))      g_cfg.fov      = clamp_int(val, FOV_MIN, FOV_MAX);
    else if (!strcmp(key, "espenemies")) g_cfg.espenemies = val ? 1 : 0;
    else if (!strcmp(key, "espboxes"))    g_cfg.espboxes    = val ? 1 : 0;
    else if (!strcmp(key, "esphealth"))   g_cfg.esphealth   = val ? 1 : 0;
    else if (!strcmp(key, "espnames"))    g_cfg.espnames    = val ? 1 : 0;
    else if (!strcmp(key, "espdistance")) g_cfg.espdistance = val ? 1 : 0;
    else if (!strcmp(key, "espitems"))    g_cfg.espitems    = val ? 1 : 0;
    else if (!strcmp(key, "espsecrets"))  g_cfg.espsecrets  = val ? 1 : 0;
    else if (!strcmp(key, "espexplodables")) g_cfg.espexplodables = val ? 1 : 0;
    else if (!strcmp(key, "espbreakables"))  g_cfg.espbreakables  = val ? 1 : 0;
    else if (!strcmp(key, "espswitches"))    g_cfg.espswitches    = val ? 1 : 0;
    else if (!strcmp(key, "dmgnumbers"))     g_cfg.dmgnumbers     = val ? 1 : 0;
    else if (!strcmp(key, "secrettriggers")) g_cfg.secrettriggers = val ? 1 : 0;
    else if (!strcmp(key, "secretvolume")) { g_cfg.secretvolume = val ? 1 : 0; sawSecretVolume = true; }
    else if (!strcmp(key, "triggervolume")) { if (!sawSecretVolume) g_cfg.secretvolume = val ? 1 : 0; }
    else if (!strcmp(key, "espsecretpath")) g_cfg.espsecretpath = val ? 1 : 0;
    else if (!strcmp(key, "espdrawdist")) g_cfg.espdrawdist = esp_draw_distance_clamped(val);
    else if (!strcmp(key, "espnolimit")) g_cfg.espnolimit = clamp_int(val, 0, 1);
    else if (!strcmp(key, "aimon"))     g_cfg.aimon     = val ? 1 : 0;
    else if (!strcmp(key, "aimhold"))   g_cfg.aimhold   = val ? 1 : 0;
    else if (!strcmp(key, "aimbind")) { g_cfg.aimbind = clamp_int(val, 1, 254); sawAimbind = true; }
    // Pre-migration `aimbutton` held an SDL BUTTON NUMBER, which read straight into aimbind would shift every hold
    // button by one; bind_from_sdl_mouse converts, and out of range leaves the default (clamping would bind Mouse 1,
    // which the panel refuses). `aimbind` wins wherever it appears.
    else if (!strcmp(key, "aimbutton")) {
      if (!sawAimbind) {
        const int vk = bind_from_sdl_mouse(val);
        g_cfg.aimbind = vk ? vk : VK_BIND_XBUTTON2;
        resetMouse1 = (vk == 0 && val == MOUSE_BUTTON_MIN);
      }
    }
    else if (!strcmp(key, "aimtarget")) g_cfg.aimtarget = val ? 1 : 0;
    else if (!strcmp(key, "aimpoint"))  g_cfg.aimpoint  = val ? 1 : 0;
    else if (!strcmp(key, "aimsmooth")) g_cfg.aimsmooth = aim_smooth_clamped(val);
    else if (!strcmp(key, "aimcone"))   g_cfg.aimcone   = aim_cone_clamped(val);
    else if (!strcmp(key, "aimlead"))   g_cfg.aimlead   = val ? 1 : 0;
    else if (!strcmp(key, "aimleadscale")) g_cfg.aimleadscale = aim_leadscale_clamped(val);
    else if (!strcmp(key, "aimrange")) g_cfg.aimrange = aim_range_clamped(val);   // independent of espdrawdist
    else if (!strcmp(key, "aimdebug"))  g_cfg.aimdebug  = val ? 1 : 0;
    else if (!strcmp(key, "silenton"))       g_cfg.silenton       = val ? 1 : 0;
    else if (!strcmp(key, "silentmaxangle")) g_cfg.silentmaxangle = silent_maxangle_clamped(val);
    else if (!strcmp(key, "freecamon"))         g_cfg.freecamon   = val ? 1 : 0;
    else if (!strcmp(key, "freecamkey"))        g_cfg.freecamkey  = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamteleportkey")) g_cfg.freecamteleportkey = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamfwdkey"))   g_cfg.freecamfwdkey   = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecambackkey"))  g_cfg.freecambackkey  = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamleftkey"))  g_cfg.freecamleftkey  = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamrightkey")) g_cfg.freecamrightkey = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamupkey"))    g_cfg.freecamupkey    = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamdownkey"))  g_cfg.freecamdownkey  = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamfastkey"))  g_cfg.freecamfastkey  = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamslowkey"))  g_cfg.freecamslowkey  = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecamcopykey"))  g_cfg.freecamcopykey  = clamp_int(val, 1, 254);
    else if (!strcmp(key, "freecampastekey")) g_cfg.freecampastekey = clamp_int(val, 1, 254);
    else if (!strcmp(key, "mouse4action")) g_cfg.mouse4action = mousebind_action_clamped(val);
    else if (!strcmp(key, "mouse5action")) g_cfg.mouse5action = mousebind_action_clamped(val);
    else if (!strcmp(key, "freecamspeed"))      g_cfg.freecamspeed = freecam_speed_clamped(val);
    // Retired: turn rate derives from mouse aim (freecam_sens_from_mouseaim); recognised and discarded so an old
    // ini's freecamsens= does not survive in s_unknown's write-back.
    else if (!strcmp(key, "freecamsens")) { }
    else if (!strcmp(key, "freecamshowplayer")) g_cfg.freecamshowplayer = val ? 1 : 0;
    else if (!strcmp(key, "freecamnocull"))     g_cfg.freecamnocull     = val ? 1 : 0;
    else if (!strcmp(key, "freecamfog"))        g_cfg.freecamfog        = val ? 1 : 0;
    else if (!strcmp(key, "fogvisibility")) g_cfg.fogvisibility = fog_visibility_clamped(val);
    else if (!strcmp(key, "silenttest")) g_cfg.silenttest = val;
    else if (!strcmp(key, "freecamfullbright")) g_cfg.freecamfullbright = val ? 1 : 0;
    else if (!strcmp(key, "freecamshot"))       g_cfg.freecamshot       = val ? 1 : 0;
    else if (!strcmp(key, "fullbright"))        g_cfg.fullbright        = val ? 1 : 0;
    else if (!strcmp(key, "mouseaimon"))     g_cfg.mouseaimon     = val ? 1 : 0;
    else if (!strcmp(key, "mouseaimsens")) {
      g_cfg.mouseaimsens = mouseaim_sens_clamped(val);
      note_mouseaim_clamp("mouseaimsens", val, g_cfg.mouseaimsens);
    }
    else if (!strcmp(key, "mouseaimvscale")) {
      g_cfg.mouseaimvscale = mouseaim_vscale_clamped(val);
      note_mouseaim_clamp("mouseaimvscale", val, g_cfg.mouseaimvscale);
    }
    else if (!strcmp(key, "mouseaimleadcap")) g_cfg.mouseaimleadcap = clamp_int(val, 0, 1024);
    else if (!strcmp(key, "accpistol"))   g_cfg.accpistol   = val ? 1 : 0;
    else if (!strcmp(key, "accchaingun")) g_cfg.accchaingun = val ? 1 : 0;
    else if (!strcmp(key, "accshotgun"))  g_cfg.accshotgun  = val ? 1 : 0;
    else if (!strcmp(key, "accexpander")) g_cfg.accexpander = val ? 1 : 0;
    else if (!strcmp(key, "firerate")) g_cfg.firerate = firerate_rate_clamped(val);
    else if (!strcmp(key, "menuanim")) g_cfg.menuanim = anim_style_clamped(val);
    else if (!strcmp(key, "switchspeed")) g_cfg.switchspeed = firerate_rate_clamped(val);
    else if (!strcmp(key, "tboton"))    g_cfg.tboton = val ? 1 : 0;
    else if (!strcmp(key, "tbotwhen"))  g_cfg.tbotwhen = tbot_when_clamped(val);
    else if (!strcmp(key, "tbotdelay")) g_cfg.tbotdelay = tbot_delay_ms_clamped(val);
    else if (!strcmp(key, "autosavemins")) g_cfg.autosavemins = autosave_mins_clamped(val);
    else if (!strcmp(key, "autosaveslot")) g_cfg.autosaveslot = autosave_slot_clamped(val);
    else if (!strcmp(key, "freecamdebug")) g_cfg.freecamdebug = clamp_int(val, 0, 1);
    else if (!strcmp(key, "freecamtransport")) g_cfg.freecamtransport = clamp_int(val, 0, 1);
    else if (!strcmp(key, "freecamteleportmarker")) g_cfg.freecamteleportmarker = clamp_int(val, 0, 1);
    // --- World Tour's own keys: each clamp is the one read_cfg applied, so existing values read back the same.
    else if (!strcmp(key, "borderless")) g_cfg.borderless = val ? 1 : 0;
    else if (!strcmp(key, "x")) g_cfg.winx = val;
    else if (!strcmp(key, "y")) g_cfg.winy = val;
    else if (!strcmp(key, "w")) g_cfg.winw = val;
    else if (!strcmp(key, "h")) g_cfg.winh = val;
    else if (!strcmp(key, "dump")) g_cfg.dump = val ? 1 : 0;
    // 0 is off; else held in a range the pacing can hit: below 20 the spin holds the render thread for most of a
    // frame, above 1000 a frame is shorter than the wait's overhead.
    else if (!strcmp(key, "fpscap")) g_cfg.fpscap = val > 0 ? clamp_int(val, 20, 1000) : 0;
    else if (!strcmp(key, "smoothview")) g_cfg.smoothview = val ? 1 : 0;
    else if (!strcmp(key, "overlay"))    g_cfg.overlay    = val ? 1 : 0;
    else if (!strcmp(key, "readout"))    g_cfg.readout    = val ? 1 : 0;
    else if (!strcmp(key, "canvastest")) g_cfg.canvastest = val ? 1 : 0;
    // This port's spelling of two keys Megaton has a field for: same meaning and clamp; the last line in the file wins.
    else if (!strcmp(key, "panelkey")) g_cfg.menukey    = clamp_int(val, 1, 254);
    else if (!strcmp(key, "mouseaim")) g_cfg.mouseaimon = val ? 1 : 0;
    else if (s_unknownCount < 128) {
      *eq = '=';
      strncpy_s(s_unknown[s_unknownCount++], line, _TRUNCATE);
    }
  }
  fclose(f);
  if (resetMouse1 && !sawAimbind) LOGI("%s", CONFIG_LOG_AIMBUTTON1_RESET);
  config_note_saved();
}

// Megaton's whole-file save, kept but NOT called: it rewrites wtmod.ini from the Config alone, deleting the
// installer's comments and the player's notes. The panel saves through config_write_changed instead.
bool config_save(const char* path) {
  FILE* f = nullptr;
  if (fopen_s(&f, path, "w") != 0 || !f) return false;
  fputs("[wtmod]\n", f);
  fprintf(f, "menukey=%d\n",  g_cfg.menukey);
  fprintf(f, "debuglog=%d\n", g_cfg.debuglog);
  fprintf(f, "debugaim=%d\n", g_cfg.debugaim);
  fprintf(f, "wswitchauto=%d\n",     g_cfg.wswitchauto);
  fprintf(f, "wswitchonpickup=%d\n", g_cfg.wswitchonpickup);
  fprintf(f, "wswitchonempty=%d\n",  g_cfg.wswitchonempty);
  fprintf(f, "wswitchprefer=%d\n",   g_cfg.wswitchprefer);
  fprintf(f, "uiscale=%d\n",  g_cfg.uiscale);
  fprintf(f, "panelx=%d\n",   g_cfg.panelx);
  fprintf(f, "panely=%d\n",   g_cfg.panely);
  fprintf(f, "panelw=%d\n",   g_cfg.panelw);
  fprintf(f, "panelh=%d\n",   g_cfg.panelh);
  fprintf(f, "fovon=%d\n",    g_cfg.fovon);
  fprintf(f, "fov=%d\n",      g_cfg.fov);
  fprintf(f, "espenemies=%d\n", g_cfg.espenemies);
  fprintf(f, "espboxes=%d\n",    g_cfg.espboxes);
  fprintf(f, "esphealth=%d\n",   g_cfg.esphealth);
  fprintf(f, "espnames=%d\n",    g_cfg.espnames);
  fprintf(f, "espdistance=%d\n", g_cfg.espdistance);
  fprintf(f, "espitems=%d\n",    g_cfg.espitems);
  fprintf(f, "espsecrets=%d\n",  g_cfg.espsecrets);
  fprintf(f, "espexplodables=%d\n", g_cfg.espexplodables);
  fprintf(f, "espbreakables=%d\n",  g_cfg.espbreakables);
  fprintf(f, "espswitches=%d\n",    g_cfg.espswitches);
  fprintf(f, "dmgnumbers=%d\n",     g_cfg.dmgnumbers);
  fprintf(f, "secrettriggers=%d\n", g_cfg.secrettriggers);
  fprintf(f, "secretvolume=%d\n",   g_cfg.secretvolume);   // legacy triggervolume never written back
  fprintf(f, "espsecretpath=%d\n", g_cfg.espsecretpath);
  fprintf(f, "espdrawdist=%d\n", g_cfg.espdrawdist);
  fprintf(f, "espnolimit=%d\n", g_cfg.espnolimit);
  fprintf(f, "aimon=%d\n",     g_cfg.aimon);
  fprintf(f, "aimhold=%d\n",   g_cfg.aimhold);
  fprintf(f, "aimbind=%d\n", g_cfg.aimbind);   // legacy aimbutton never written back
  fprintf(f, "aimtarget=%d\n", g_cfg.aimtarget);
  fprintf(f, "aimpoint=%d\n",  g_cfg.aimpoint);
  fprintf(f, "aimsmooth=%d\n", g_cfg.aimsmooth);
  fprintf(f, "aimcone=%d\n",   g_cfg.aimcone);
  fprintf(f, "aimlead=%d\n",   g_cfg.aimlead);
  fprintf(f, "aimleadscale=%d\n", g_cfg.aimleadscale);
  fprintf(f, "aimrange=%d\n", g_cfg.aimrange);
  fprintf(f, "aimdebug=%d\n",  g_cfg.aimdebug);
  fprintf(f, "silenton=%d\n",       g_cfg.silenton);
  fprintf(f, "silentmaxangle=%d\n", g_cfg.silentmaxangle);
  fprintf(f, "freecamon=%d\n",         g_cfg.freecamon);
  fprintf(f, "freecamkey=%d\n",        g_cfg.freecamkey);
  fprintf(f, "freecamteleportkey=%d\n", g_cfg.freecamteleportkey);
  fprintf(f, "freecamspeed=%d\n",      g_cfg.freecamspeed);
  fprintf(f, "freecamshowplayer=%d\n", g_cfg.freecamshowplayer);
  fprintf(f, "freecamnocull=%d\n",     g_cfg.freecamnocull);
  fprintf(f, "freecamfog=%d\n",        g_cfg.freecamfog);
  fprintf(f, "fogvisibility=%d\n",     g_cfg.fogvisibility);
  fprintf(f, "silenttest=%d\n",       g_cfg.silenttest);
  fprintf(f, "freecamfullbright=%d\n", g_cfg.freecamfullbright);
  fprintf(f, "freecamshot=%d\n",       g_cfg.freecamshot);
  fprintf(f, "fullbright=%d\n",        g_cfg.fullbright);
  fprintf(f, "mouseaimon=%d\n",       g_cfg.mouseaimon);
  fprintf(f, "mouseaimsens=%d\n",     g_cfg.mouseaimsens);
  fprintf(f, "mouseaimvscale=%d\n",   g_cfg.mouseaimvscale);
  fprintf(f, "mouseaimleadcap=%d\n",  g_cfg.mouseaimleadcap);
  fprintf(f, "accpistol=%d\n",   g_cfg.accpistol);
  fprintf(f, "accchaingun=%d\n", g_cfg.accchaingun);
  fprintf(f, "accshotgun=%d\n",  g_cfg.accshotgun);
  fprintf(f, "accexpander=%d\n", g_cfg.accexpander);
  fprintf(f, "firerate=%d\n",    g_cfg.firerate);
  fprintf(f, "menuanim=%d\n",    g_cfg.menuanim);
  fprintf(f, "switchspeed=%d\n", g_cfg.switchspeed);
  fprintf(f, "tboton=%d\n",    g_cfg.tboton);
  fprintf(f, "tbotwhen=%d\n",  g_cfg.tbotwhen);
  fprintf(f, "tbotdelay=%d\n", g_cfg.tbotdelay);
  fprintf(f, "autosavemins=%d\n", g_cfg.autosavemins);
  fprintf(f, "autosaveslot=%d\n", g_cfg.autosaveslot);   // ini-only, no panel row
  fprintf(f, "freecamdebug=%d\n", g_cfg.freecamdebug);
  fprintf(f, "freecamtransport=%d\n", g_cfg.freecamtransport);
  fprintf(f, "freecamteleportmarker=%d\n", g_cfg.freecamteleportmarker);
  fprintf(f, "freecamcopykey=%d\n", g_cfg.freecamcopykey);
  fprintf(f, "freecampastekey=%d\n", g_cfg.freecampastekey);
  fprintf(f, "mouse4action=%d\n", g_cfg.mouse4action);
  fprintf(f, "mouse5action=%d\n", g_cfg.mouse5action);
  // --- World Tour's own keys, in this port's spelling (`panelkey`, `mouseaim`) so the file reads as written by hand;
  // both spellings load.
  fprintf(f, "borderless=%d\n", g_cfg.borderless);
  fprintf(f, "x=%d\n", g_cfg.winx);
  fprintf(f, "y=%d\n", g_cfg.winy);
  fprintf(f, "w=%d\n", g_cfg.winw);
  fprintf(f, "h=%d\n", g_cfg.winh);
  fprintf(f, "dump=%d\n", g_cfg.dump);
  fprintf(f, "fpscap=%d\n", g_cfg.fpscap);
  fprintf(f, "smoothview=%d\n", g_cfg.smoothview);
  fprintf(f, "overlay=%d\n", g_cfg.overlay);
  fprintf(f, "readout=%d\n", g_cfg.readout);
  fprintf(f, "canvastest=%d\n", g_cfg.canvastest);
  fprintf(f, "panelkey=%d\n", g_cfg.menukey);
  fprintf(f, "mouseaim=%d\n", g_cfg.mouseaimon);
  for (int i = 0; i < s_unknownCount; i++) fprintf(f, "%s\n", s_unknown[i]);
  fclose(f);
  g_cfgDirty = false;
  config_note_saved();
  return true;
}

// ----------------------------- World Tour's own: writing back ONE key -------
// config_save rewrites the whole file, deleting the installer's comments and the player's notes; the panel calls
// WritePrivateProfileStringA per changed key instead, against a shadow of what the file holds.

static const char* const INI_SECTION = "wtmod";

// name is the spelling this port writes; alias the second spelling the field also loads from (the LAST line wins on
// load, so a stale alias line would undo the write). Pointer-to-member: a wrong field won't compile.
struct ConfigKey { const char* name; const char* alias; int Config::*field; };
static const ConfigKey KEYS[] = {
  { "debuglog", 0, &Config::debuglog },
  { "debugaim", 0, &Config::debugaim },
  { "wswitchauto", 0, &Config::wswitchauto },
  { "wswitchonpickup", 0, &Config::wswitchonpickup },
  { "wswitchonempty", 0, &Config::wswitchonempty },
  { "wswitchprefer", 0, &Config::wswitchprefer },
  { "uiscale", 0, &Config::uiscale },
  { "panelx", 0, &Config::panelx },
  { "panely", 0, &Config::panely },
  { "panelw", 0, &Config::panelw },
  { "panelh", 0, &Config::panelh },
  { "fovon", 0, &Config::fovon },
  { "fov", 0, &Config::fov },
  { "espenemies", 0, &Config::espenemies },
  { "espboxes", 0, &Config::espboxes },
  { "esphealth", 0, &Config::esphealth },
  { "espnames", 0, &Config::espnames },
  { "espdistance", 0, &Config::espdistance },
  { "espitems", 0, &Config::espitems },
  { "espsecrets", 0, &Config::espsecrets },
  { "espexplodables", 0, &Config::espexplodables },
  { "espbreakables", 0, &Config::espbreakables },
  { "espswitches", 0, &Config::espswitches },
  { "dmgnumbers", 0, &Config::dmgnumbers },
  { "secrettriggers", 0, &Config::secrettriggers },
  { "secretvolume", 0, &Config::secretvolume },
  { "espsecretpath", 0, &Config::espsecretpath },
  { "espdrawdist", 0, &Config::espdrawdist },
  { "espnolimit", 0, &Config::espnolimit },
  { "aimon", 0, &Config::aimon },
  { "aimhold", 0, &Config::aimhold },
  { "aimbind", 0, &Config::aimbind },
  { "aimtarget", 0, &Config::aimtarget },
  { "aimpoint", 0, &Config::aimpoint },
  { "aimsmooth", 0, &Config::aimsmooth },
  { "aimcone", 0, &Config::aimcone },
  { "aimlead", 0, &Config::aimlead },
  { "aimleadscale", 0, &Config::aimleadscale },
  { "aimrange", 0, &Config::aimrange },
  { "aimdebug", 0, &Config::aimdebug },
  { "silenton", 0, &Config::silenton },
  { "silentmaxangle", 0, &Config::silentmaxangle },
  { "freecamon", 0, &Config::freecamon },
  { "freecamkey", 0, &Config::freecamkey },
  { "freecamteleportkey", 0, &Config::freecamteleportkey },
  { "freecamspeed", 0, &Config::freecamspeed },
  { "freecamshowplayer", 0, &Config::freecamshowplayer },
  { "freecamnocull", 0, &Config::freecamnocull },
  { "freecamfog", 0, &Config::freecamfog },
  { "fogvisibility", 0, &Config::fogvisibility },
  { "silenttest", 0, &Config::silenttest },
  { "freecamfullbright", 0, &Config::freecamfullbright },
  { "freecamshot", 0, &Config::freecamshot },
  { "fullbright", 0, &Config::fullbright },
  { "mouseaimsens", 0, &Config::mouseaimsens },
  { "mouseaimvscale", 0, &Config::mouseaimvscale },
  { "mouseaimleadcap", 0, &Config::mouseaimleadcap },
  { "accpistol", 0, &Config::accpistol },
  { "accchaingun", 0, &Config::accchaingun },
  { "accshotgun", 0, &Config::accshotgun },
  { "accexpander", 0, &Config::accexpander },
  { "firerate", 0, &Config::firerate },
  { "menuanim", 0, &Config::menuanim },
  { "switchspeed", 0, &Config::switchspeed },
  { "tboton", 0, &Config::tboton },
  { "tbotwhen", 0, &Config::tbotwhen },
  { "tbotdelay", 0, &Config::tbotdelay },
  { "autosavemins", 0, &Config::autosavemins },
  { "autosaveslot", 0, &Config::autosaveslot },
  { "freecamdebug", 0, &Config::freecamdebug },
  { "freecamtransport", 0, &Config::freecamtransport },
  { "freecamteleportmarker", 0, &Config::freecamteleportmarker },
  { "freecamfwdkey", 0, &Config::freecamfwdkey },
  { "freecambackkey", 0, &Config::freecambackkey },
  { "freecamleftkey", 0, &Config::freecamleftkey },
  { "freecamrightkey", 0, &Config::freecamrightkey },
  { "freecamupkey", 0, &Config::freecamupkey },
  { "freecamdownkey", 0, &Config::freecamdownkey },
  { "freecamfastkey", 0, &Config::freecamfastkey },
  { "freecamslowkey", 0, &Config::freecamslowkey },
  { "freecamcopykey", 0, &Config::freecamcopykey },
  { "freecampastekey", 0, &Config::freecampastekey },
  { "mouse4action", 0, &Config::mouse4action },
  { "mouse5action", 0, &Config::mouse5action },
  { "borderless", 0, &Config::borderless },
  { "x", 0, &Config::winx },
  { "y", 0, &Config::winy },
  { "w", 0, &Config::winw },
  { "h", 0, &Config::winh },
  { "dump", 0, &Config::dump },
  { "fpscap", 0, &Config::fpscap },
  { "smoothview", 0, &Config::smoothview },
  { "overlay", 0, &Config::overlay },
  { "readout", 0, &Config::readout },
  { "canvastest", 0, &Config::canvastest },
  { "panelkey", "menukey", &Config::menukey },
  { "mouseaim", "mouseaimon", &Config::mouseaimon },
};
static const int KEY_COUNT = (int)(sizeof(KEYS) / sizeof(KEYS[0]));

// Every Config member is an int with exactly one entry above, so the sizes agree; a field added without a key fails
// this.
static_assert(sizeof(Config) == KEY_COUNT * sizeof(int),
              "every Config field needs one ConfigKey entry");

static Config s_onDisk;
void config_note_saved() { s_onDisk = g_cfg; }

static bool ini_has_key(const char* path, const char* key) {
  char buf[16];
  GetPrivateProfileStringA(INI_SECTION, key, "\x01", buf, (DWORD)sizeof buf, path);
  return buf[0] != '\x01';
}

void config_write_changed(const char* path) {
  g_cfgDirty = false;
  if (!path || !path[0]) return;
  for (int i = 0; i < KEY_COUNT; i++) {
    const int now = g_cfg.*KEYS[i].field;
    if (now == s_onDisk.*KEYS[i].field) continue;
    char val[16];
    sprintf_s(val, "%d", now);
    if (!WritePrivateProfileStringA(INI_SECTION, KEYS[i].name, val, path)) {
      // One line, not one per frame (a read-only game folder would fill the log while dragging a slider); the shadow
      // is left alone, so the next change retries.
      static bool logged = false;
      if (!logged) {
        logged = true;
        LOGE("config: could not write %s=%s to %s (err %lu); settings will not persist",
             KEYS[i].name, val, path, GetLastError());
      }
      return;
    }
    if (KEYS[i].alias && ini_has_key(path, KEYS[i].alias))
      WritePrivateProfileStringA(INI_SECTION, KEYS[i].alias, val, path);
    s_onDisk.*KEYS[i].field = now;
  }
}


// The four capture rows drive DIFFERENT Config members; the row -> field mapping lives here, pinned by the suite. An
// out-of-range row reads 0 and writes nothing.
int config_bind_get(const Config& c, int row) {
  switch (row) {
    case BIND_ROW_MENU:     return c.menukey;
    case BIND_ROW_AIM:      return c.aimbind;
    case BIND_ROW_CAMERA:   return c.freecamkey;
    case BIND_ROW_TELEPORT: return c.freecamteleportkey;
    default:                return 0;
  }
}

void config_bind_set(Config& c, int row, int vk) {
  switch (row) {
    case BIND_ROW_MENU:     c.menukey            = vk; break;
    case BIND_ROW_AIM:      c.aimbind            = vk; break;
    case BIND_ROW_CAMERA:   c.freecamkey         = vk; break;
    case BIND_ROW_TELEPORT: c.freecamteleportkey = vk; break;
    default: break;
  }
}

struct ConfigSwitch { const char* name; int Config::*field; };
static const ConfigSwitch SWITCHES[] = {
  { "debuglog",          &Config::debuglog },
  { "debugaim",           &Config::debugaim },
  { "wswitchauto",        &Config::wswitchauto },
  { "wswitchonpickup",    &Config::wswitchonpickup },
  { "wswitchonempty",     &Config::wswitchonempty },
  { "fovon",              &Config::fovon },
  { "espenemies",         &Config::espenemies },
  { "espboxes",           &Config::espboxes },
  { "esphealth",          &Config::esphealth },
  { "espnames",           &Config::espnames },
  { "espdistance",        &Config::espdistance },
  { "espitems",           &Config::espitems },
  { "espsecrets",         &Config::espsecrets },
  { "espexplodables",     &Config::espexplodables },
  { "espbreakables",      &Config::espbreakables },
  { "espswitches",        &Config::espswitches },
  { "dmgnumbers",         &Config::dmgnumbers },
  { "secrettriggers",     &Config::secrettriggers },
  { "secretvolume",       &Config::secretvolume },
  { "espsecretpath",      &Config::espsecretpath },
  { "espnolimit",         &Config::espnolimit },
  { "aimon",              &Config::aimon },
  { "aimhold",            &Config::aimhold },
  { "aimtarget",          &Config::aimtarget },
  { "aimpoint",           &Config::aimpoint },
  { "aimlead",            &Config::aimlead },
  { "aimdebug",           &Config::aimdebug },
  { "silenton",           &Config::silenton },
  { "freecamon",          &Config::freecamon },
  { "freecamshowplayer",  &Config::freecamshowplayer },
  { "freecamnocull",      &Config::freecamnocull },
  { "freecamfog",         &Config::freecamfog },
  { "freecamfullbright",  &Config::freecamfullbright },
  { "freecamshot",        &Config::freecamshot },
  { "fullbright",         &Config::fullbright },
  { "mouseaimon",         &Config::mouseaimon },
  { "accpistol",          &Config::accpistol },
  { "accchaingun",        &Config::accchaingun },
  { "accshotgun",         &Config::accshotgun },
  { "accexpander",        &Config::accexpander },
  { "tboton",             &Config::tboton },
};
static const int SWITCH_COUNT = sizeof(SWITCHES) / sizeof(SWITCHES[0]);

int config_switch_count() { return SWITCH_COUNT; }

const char* config_switch_name(int i) {
  return (i >= 0 && i < SWITCH_COUNT) ? SWITCHES[i].name : nullptr;
}

int config_switch_value(const Config& c, int i) {
  if (i < 0 || i >= SWITCH_COUNT) return 0;
  return (c.*(SWITCHES[i].field)) ? 1 : 0;
}

const char* config_state_line(const char* key, int oldV, int newV, char* buf, int cap) {
  if (!key || !buf || cap <= 0) return "";
  _snprintf_s(buf, cap, _TRUNCATE, "state: %s %d -> %d", key, oldV ? 1 : 0, newV ? 1 : 0);
  return buf;
}

int config_state_diff(const Config& prev, const Config& cur, int* outIdx, int cap) {
  if (!outIdx || cap <= 0) return 0;
  int n = 0;
  for (int i = 0; i < SWITCH_COUNT && n < cap; i++) {
    if (config_switch_value(prev, i) != config_switch_value(cur, i)) outIdx[n++] = i;
  }
  return n;
}

void config_log_state_changes(const Config& prev, const Config& cur) {
  int idx[SWITCH_COUNT];
  const int n = config_state_diff(prev, cur, idx, SWITCH_COUNT);
  for (int i = 0; i < n; i++) {
    char buf[CONFIG_STATE_LINE_CAP];
    config_state_line(config_switch_name(idx[i]),
                      config_switch_value(prev, idx[i]),
                      config_switch_value(cur, idx[i]),
                      buf, sizeof(buf));
    LOGI("%s", buf);
  }
}

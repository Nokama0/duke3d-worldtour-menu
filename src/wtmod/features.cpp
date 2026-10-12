// See features.h. Marks the rows that cannot act and carries the one group that can into the code that does it.
#include "features.h"
#include "config.h"
#include "nocull.h"
#include "light.h"
#include "mouseaim.h"
#include "fovwt.h"
#include "hook.h"
#include "input.h"
#include "camera.h"

// What was last handed to mouseaim.cpp, seeded from the ini (what the hook was installed with), so the first sync
// pushes only changes.
static int s_mouseaimon = -1, s_mouseaimsens = -1, s_mouseaimvscale = -1;

// esp.cpp reads g_cfg itself every frame; these only give the log one line per change.
static int s_espenemies = -1, s_espitems = -1;

// Same for the field of view: fovwt.cpp reads g_cfg itself.
static int s_fovon = -1;
// What the window thread was last told the camera toggle key is; 0 leaves the key to Duke.
static int s_freecamkey = -1;

void features_init() {
  // Accuracy and culling latches are set only by what can refuse them: a site whose bytes differ (accuracy_should_arm
  // reads the same latch), freecamon=0, or a failed nocullwt_install fingerprint.
  if (!g_cfg.freecamon) nocull_mark_unavailable();
  // Fullbright takes THREE writers: lightwt.cpp holds sector and wall shade at 0 and borrows each sprite's shade around
  // the draw; fogwt.cpp holds player visibility at 0 (so Remove fog is a no-op once it is on).

  s_mouseaimon     = g_cfg.mouseaimon;
  s_mouseaimsens   = g_cfg.mouseaimsens;
  s_mouseaimvscale = g_cfg.mouseaimvscale;
  s_espenemies     = g_cfg.espenemies;
  s_espitems       = g_cfg.espitems;
  s_fovon          = g_cfg.fovon;
  // No verdict on the patch yet: this runs seconds BEFORE fov_install, so fov_installed() is false regardless.
  wt_log("features: fov %s (%d degrees); fov_install has not run yet, its own line "
         "reports whether the setaspect call took", s_fovon ? "on" : "off", g_cfg.fov);
  s_freecamkey     = 0;   // armed by the first features_sync after camera_install succeeds
  input_set_freecam_key(s_freecamkey);
  // The aim cluster's switches on their own line: tbot_tic logs only an edge.
  wt_log("features: aim cluster -- aimon=%d aimhold=%d tboton=%d tbotwhen=%d "
         "silenton=%d acc(p=%d c=%d s=%d e=%d) espenemies=%d",
         g_cfg.aimon, g_cfg.aimhold, g_cfg.tboton, g_cfg.tbotwhen, g_cfg.silenton,
         g_cfg.accpistol, g_cfg.accchaingun, g_cfg.accshotgun, g_cfg.accexpander,
         g_cfg.espenemies);
  wt_log("features: fullbright and accuracy live, culling %s; "
         "mouse aim live (on=%d sens=%d vscale=%d); "
         "esp live (enemies=%d items=%d drawdist=%d nolimit=%d)",
         g_cfg.freecamon ? "waiting on its install" : "unavailable (freecamon=0)",
         s_mouseaimon, s_mouseaimsens, s_mouseaimvscale,
         s_espenemies, s_espitems, g_cfg.espdrawdist, g_cfg.espnolimit);
}

void features_sync() {
  // The setters cross to the mouse-polling thread by interlocked write, so call on change only.
  if (g_cfg.mouseaimon != s_mouseaimon) {
    s_mouseaimon = g_cfg.mouseaimon;
    mouseaim_set_enabled(s_mouseaimon);
    // Not a silent no-op: the hook is installed only if the ini asked at startup, so a panel switch needs a restart.
    wt_log("features: mouseaim %s%s", s_mouseaimon ? "on" : "off",
           mouseaim_installed() ? "" : " (needs a restart; the hook is not installed)");
  }
  if (g_cfg.mouseaimsens != s_mouseaimsens) {
    s_mouseaimsens = g_cfg.mouseaimsens;
    mouseaim_set_sens(s_mouseaimsens);
  }
  if (g_cfg.mouseaimvscale != s_mouseaimvscale) {
    s_mouseaimvscale = g_cfg.mouseaimvscale;
    mouseaim_set_vscale(s_mouseaimvscale);
  }
  // No setter: esp.cpp reads the same g_cfg on the same thread one call later; the line shows the row acting.
  if (g_cfg.espenemies != s_espenemies) {
    s_espenemies = g_cfg.espenemies;
    wt_log("features: esp enemies %s", s_espenemies ? "on" : "off");
  }
  if (g_cfg.espitems != s_espitems) {
    s_espitems = g_cfg.espitems;
    wt_log("features: esp items %s", s_espitems ? "on" : "off");
  }
  if (g_cfg.fovon != s_fovon) {
    s_fovon = g_cfg.fovon;
    // Installed at startup only if the ini asked, so switching on needs a restart; OFF is immediate.
    wt_log("features: fov %s (%d degrees)%s", s_fovon ? "on" : "off", g_cfg.fov,
           fov_installed() ? "" : " (needs a restart; the setaspect call is not patched)");
  }

  // Fullbright's map-shade hold, synced per frame (two count reads and a compare).
  light_sync();

  // Called from here too, so a bind row that changed g_cfg.freecamkey during the draw takes effect the same frame.
  features_arm_freecam_key();
}

// features_init runs ~3 s before camera_install, so the key arms only once the hook is in; here rather than in the
// panel-draw path so it works before the panel opens. Change-guarded.
void features_arm_freecam_key() {
  const int wantKey = (g_cfg.freecamon && camera_installed()) ? g_cfg.freecamkey : 0;
  if (wantKey == s_freecamkey) return;
  s_freecamkey = wantKey;
  input_set_freecam_key(s_freecamkey);
  wt_log("features: free camera %s", s_freecamkey ? "armed" : "off; its key is Duke's again");
}

// target.h and freecam.h declare these clamps; one clamp_int against the declaring header's constants keeps sliders and
// the ini parser agreeing.
#include "common.h"
#include "target.h"
#include "freecam.h"

int aim_cone_clamped(int degrees) { return clamp_int(degrees, AIM_CONE_MIN, AIM_CONE_MAX); }
int aim_smooth_clamped(int pct)   { return clamp_int(pct, AIM_SMOOTH_MIN, AIM_SMOOTH_MAX); }
int aim_leadscale_clamped(int pct) { return clamp_int(pct, AIM_LEADSCALE_MIN, AIM_LEADSCALE_MAX); }
int aim_range_clamped(int units)  { return clamp_int(units, AIM_RANGE_MIN, AIM_RANGE_MAX); }

int silent_maxangle_clamped(int degrees) {
  return clamp_int(degrees, SILENT_MAXANGLE_MIN, SILENT_MAXANGLE_MAX);
}


// The engine reads that feed actorstate.h's rules. temp_data[4] is an INDEX, not a pointer (common.h's action_read
// dereferences it as Megaton's build does; here every sample was t4=0x594 and unreadable).
#include <windows.h>
#include "actorstate.h"
#include "esp_pics.h"
#include "world.h"
#include "config.h"
#include "wallclock.h"
#include "hook.h"
// temp_data[4] is a ONE-BASED INDEX into a vector<int> of script addresses; an entry is the address of the five longs.
static const unsigned kActionRecordBytes = 20;   // five longs
// No shipped CON set comes close: 444 declared actions, 3677 table entries measured. A larger count is not our table.
static const unsigned kMaxActionEntries = 262144;
// The table holds one entry per script address referenced, every label AND every if/else target (a packed table with a
// fixed base decoded 0 of 444 declarations).
static const unsigned kRvaActionVecBegin = 0xEDF144;
// The table is HEAP-allocated: read live each call, every index bounds-checked, nothing cached. begin and end are two
// non-atomic loads, but the vector is built while the CONs compile, before any actor exists; the count cap bounds the
// damage.
static const unsigned kRvaActionVecEnd   = 0xEDF148;

// Only CON-DRIVEN actors keep an action index: hardcoded actors and sector effectors use temp_data[4] as scratch, so a
// picnum with no compiled script is never decoded (actorscrptr[picnum] is the engine's test).
static const unsigned kRvaActorScrPtr = 0x7350B0;

static const unsigned char* s_base = 0;
static unsigned s_imageSize = 0;
static bool s_decodeLive = false;

static bool actor_has_script(int picnum) {
  if (!s_decodeLive || picnum < 0 || picnum >= WORLD_MAXTILES) return false;
  const unsigned off = kRvaActorScrPtr + (unsigned)picnum * 4u;
  if (off + 4u > s_imageSize) return false;
  return *(const unsigned*)(s_base + off) != 0u;
}

static ActorAction action_at(int idx) {
  ActorAction a = {};
  if (!s_decodeLive || idx <= 0) return a;
  const int* const* beg = *(const int* const**)(s_base + kRvaActionVecBegin);
  const int* const* end = *(const int* const**)(s_base + kRvaActionVecEnd);
  if (!beg || !end || end < beg) return a;
  if (((unsigned)(UINT_PTR)beg & 3u) != 0u) return a;   // not a vector<int> base
  const unsigned count = (unsigned)(end - beg);
  if (count > kMaxActionEntries) return a;
  if ((unsigned)(idx - 1) >= count) return a;

  const unsigned char* rec = (const unsigned char*)beg[idx - 1];
  // Outside the image is a table we do not understand. Checked as an unsigned OFFSET: `rec + 20 > s_base + imageSize`
  // wraps near the top of the address space and faults.
  if (rec < s_base) return a;
  if ((unsigned)(rec - s_base) > s_imageSize - kActionRecordBytes) return a;
  const int* r = (const int*)rec;
  a.startframe = r[0]; a.numframes = r[1]; a.viewtype = r[2];
  a.incval     = r[3]; a.delay     = r[4];
  a.readable   = true;
  a.plausible  = action_plausible(a.startframe, a.numframes, a.viewtype);
  return a;
}

void actorstate_install() {
  unsigned char* base = (unsigned char*)GetModuleHandleA(0);
  if (!base) { wt_log("actorstate: no module base"); return; }
  const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) { wt_log("actorstate: not a DOS image"); return; }
  const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) { wt_log("actorstate: not a PE image"); return; }
  const unsigned imageSize = nt->OptionalHeader.SizeOfImage;
  if (kRvaActionVecEnd + 4u > imageSize || kRvaActorScrPtr + 4u > imageSize) {
    wt_log("actorstate: the action vector rva 0x%X is outside the image", kRvaActionVecBegin);
    return;
  }
  s_base = base;
  s_imageSize = imageSize;
  s_decodeLive = true;

  // PROVE ONE DECODE BEFORE TRUSTING ANY: ATROOPSTAND at index 1428 (`action ATROOPSTAND 0 1 5 1 1`) must come back, else
  // a patched exe or total conversion would decode garbage silently; the finer states go off for the session.
  const ActorAction probe = action_at(1428);
  if (!action_record_is(probe, 0, 1, 5, 1, 1)) {
    s_decodeLive = false;
    wt_log("actorstate: the action table at rva 0x%X does not decode index 1428 to "
           "ATROOPSTAND (got readable=%d (%d,%d,%d,%d,%d)); choking, playing dead "
           "and the commander's death record stay off this session (a different "
           "CON set, or a patched exe?)",
           kRvaActionVecBegin, (int)probe.readable, probe.startframe,
           probe.numframes, probe.viewtype, probe.incval, probe.delay);
    return;
  }

  const int* const* beg = *(const int* const**)(base + kRvaActionVecBegin);
  const int* const* end = *(const int* const**)(base + kRvaActionVecEnd);
  const unsigned count = (beg && end && end >= beg) ? (unsigned)(end - beg) : 0u;
  wt_log("actorstate: action decode live -- script-address table at rva 0x%X holds %u "
         "entries, indexed one-based from temp_data[4]; picnums are gated on "
         "actorscrptr rva 0x%X. Choking, playing dead and the commander's death "
         "records are read from the engine's own table this session",
         kRvaActionVecBegin, count, kRvaActorScrPtr);
}

static int s_cmdrLines = 0;

static ActorStateCache s_cache = { 0, {0}, {0} };

void actor_state_generation_begin(unsigned gen) { actor_cache_begin(&s_cache, gen); }

// Every term of actor_state's verdict, for the vanish ledger; handed to the caller, not stashed in a static (two threads).
struct ActorStateDetail {
  int picnum, statnum, cstat, xrepeat;
  int extra, rawExtra, pending, t4;
  int badguy, hittable, choking, playDead, dyingRecord, cmdrRecord;
  ActorAction rec;
};

// NO CACHE, safe on any clock: actor_state_cached's generation turns on the RENDER thread while the triggerbot, aimbot
// and silent aim ask on the GAME thread. One function for all callers keeps the Commander's death record from reaching
// the ESP and nothing that shoots.
static ActorState actor_state_live_detail(int i, ActorStateDetail* d) {
  WorldSprite sp;
  if (d) { ActorStateDetail z = {}; *d = z; }
  if (!world_sprite(i, &sp)) return AS_NONE;
  const bool badguy = esp_is_enemy(sp.picnum);
  const int  cstat  = sp.cstat & 0xFFFF;
  const bool hittable = (cstat & AS_CSTAT_HITTABLE) != 0;

  // A sitting Assault Trooper carries damage it has not applied (actorstate.h).
  int pending = 0;
  world_actor_pending_damage(i, &pending);
  const int extra = actor_effective_extra(sp.picnum, sp.extra, pending);

  bool choking = false, playDead = false, dyingRecord = false;
  const bool canDecode = s_decodeLive && actor_has_script(sp.picnum);
  if (canDecode && badguy && extra >= 0 && (hittable || extra == 0)) {
    int t4 = 0;
    if (world_actor_temp_data(i, 4, &t4) && t4 != 0) {
      const ActorAction self = action_at(t4);
      if (hittable) {
        choking = actor_record_is_suffering(self);
        // The commander's death record outranks extra >= 0 (actorstate.h): its phases flicker `extra`.
        dyingRecord = sp.picnum == ACTOR_PICNUM_COMMANDER &&
                      actor_record_is_commander_dying(self);
      } else if (action_record_is(self, 54, 0, 0, 0, 0)) {
        // Only a "down" record can be playing dead: neighbours at index -1/+1.
        playDead = actor_record_is_playdead(self, action_at(t4 - 1), action_at(t4 + 1));
      }
    }
  }

  // OUTSIDE THE RECORD GATE: it needs extra >= 0 and half the commander's death frames are negative; the zero frames
  // between them read alive and the triggerbot fired into them.

  const ActorState st = actor_state(sp.statnum, badguy, sp.xrepeat, cstat, extra,
                                    choking, playDead, dyingRecord);

  if (d) {
    d->picnum = sp.picnum; d->statnum = sp.statnum; d->cstat = cstat;
    d->xrepeat = sp.xrepeat; d->extra = extra; d->rawExtra = sp.extra;
    d->pending = pending; d->badguy = badguy ? 1 : 0;
    d->hittable = hittable ? 1 : 0; d->choking = choking ? 1 : 0;
    d->playDead = playDead ? 1 : 0; d->dyingRecord = dyingRecord ? 1 : 0;
    d->cmdrRecord = 0;
    d->t4 = 0;
    ActorAction dr = {};
    int dt4 = 0;
    if (canDecode && world_actor_temp_data(i, 4, &dt4)) { d->t4 = dt4; dr = action_at(dt4); }
    d->rec = dr;
    d->cmdrRecord = (sp.picnum == ACTOR_PICNUM_COMMANDER &&
                     actor_record_is_commander_dying(dr)) ? 1 : 0;
  }

  // Every input to the verdict, gate terms included (a record never READ and one read that disagrees look alike).
  // Budgeted and throttled by time so a sleeping commander cannot spend it.
  static unsigned cmdrLastMs = 0;
  const unsigned nowMs = wallclock_ms();
  if (g_cfg.debuglog != 0 && s_cmdrLines < 40 &&
      (nowMs - cmdrLastMs) >= 250u &&
      (sp.picnum == ACTOR_PICNUM_COMMANDER || sp.picnum == 1921)) {
    cmdrLastMs = nowMs;
    s_cmdrLines++;
    int t4 = 0;
    const bool haveT4 = world_actor_temp_data(i, 4, &t4);
    ActorAction rec = {};
    if (haveT4) rec = action_at(t4);
    wt_log("cmdr: spr %d pic %d extra=%d (raw %d pend %d) cstat=0x%04X hittable=%d "
           "| gate table=%d badguy=%d extraOk=%d pass=%d "
           "| t4=%d readable=%d rec=(%d,%d,%d,%d,%d) dying=%d | state=%s",
           i, sp.picnum, extra, sp.extra, pending, (unsigned)cstat, (int)hittable,
           canDecode ? 1 : 0, (int)badguy, (int)(extra >= 0),
           (int)(canDecode && badguy && extra >= 0 && (hittable || extra == 0)),
           t4, (int)rec.readable,
           rec.startframe, rec.numframes, rec.viewtype, rec.incval, rec.delay,
           (int)dyingRecord, actor_state_name(st));
  }
  return st;
}

ActorState actor_state_live(int i) { return actor_state_live_detail(i, 0); }

// An enemy leaving BOTH the ESP and targeting. ESP thread only, once per sprite per generation (from the game thread too
// it would MANUFACTURE gone/BACK pairs).
#define VANISH_UNKNOWN 0
#define VANISH_SHOWN   1
// The return trip is the evidence: returns are never budgeted or filtered, ordinary deaths are remembered silently.
#define VANISH_HIDDEN  2
static void vanish_note(int i, ActorState st, const ActorStateDetail& d) {
  if (g_cfg.debugaim == 0 || !d.badguy || i < 0 || i >= WORLD_MAXSPRITES) return;
  static unsigned char s_seen[WORLD_MAXSPRITES] = {0};
  static unsigned char s_lastState[WORLD_MAXSPRITES] = {0};   // AS_* + 1; 0 = unseen
  static int s_goneLines = 0;

  // A labelled state is rare: every entry and exit, deduped by (picnum, state).
  const unsigned char stNow = (unsigned char)(st + 1);
  const unsigned char stBefore = s_lastState[i];
  s_lastState[i] = stNow;
  if (stBefore != 0 && stBefore != stNow) {
    const ActorState was = (ActorState)(stBefore - 1);
    if (actor_state_labelled(st) || actor_state_labelled(was)) {
      static int  s_stateKey[24] = {0};
      static int  s_stateCount = 0;
      const int key = d.picnum * 64 + (int)was * 8 + (int)st;
      bool known = false;
      for (int k = 0; k < s_stateCount; k++) if (s_stateKey[k] == key) { known = true; break; }
      if (!known && s_stateCount < 24) {
        s_stateKey[s_stateCount++] = key;
        wt_log("state: spr %d pic %d %s -> %s | targetable %d -> %d, shown %d -> %d "
               "| statnum=%d cstat=0x%04X extra=%d | choking=%d playdead=%d "
               "| t4=%d rec=(%d,%d,%d,%d,%d) readable=%d",
               i, d.picnum, actor_state_name(was), actor_state_name(st),
               (int)actor_state_targetable(was), (int)actor_state_targetable(st),
               (int)actor_state_shown(was), (int)actor_state_shown(st),
               d.statnum, (unsigned)d.cstat, d.extra, d.choking, d.playDead,
               d.t4, d.rec.startframe, d.rec.numframes, d.rec.viewtype,
               d.rec.incval, d.rec.delay, (int)d.rec.readable);
      }
    }
  }

  const unsigned char now = actor_state_shown(st) ? VANISH_SHOWN : VANISH_HIDDEN;
  const unsigned char before = s_seen[i];
  s_seen[i] = now;

  if (before == VANISH_UNKNOWN || before == now) return;

  const bool back = (now == VANISH_SHOWN);
  const bool suspicious = d.hittable && d.extra >= 0;
  if (!back && (!suspicious || s_goneLines >= 100)) return;
  if (!back) s_goneLines++;

  wt_log("vanish: spr %d pic %d %s | state=%s statnum=%d cstat=0x%04X xrep=%d "
         "extra=%d (raw %d pend %d) | hittable=%d choking=%d playdead=%d "
         "dyingRecord=%d cmdrRecord=%d | t4=%d rec=(%d,%d,%d,%d,%d) "
         "readable=%d plausible=%d",
         i, d.picnum, back ? "BACK" : "gone",
         actor_state_name(st), d.statnum, (unsigned)d.cstat, d.xrepeat,
         d.extra, d.rawExtra, d.pending, d.hittable, d.choking, d.playDead,
         d.dyingRecord, d.cmdrRecord, d.t4,
         d.rec.startframe, d.rec.numframes, d.rec.viewtype, d.rec.incval,
         d.rec.delay, (int)d.rec.readable, (int)d.rec.plausible);
}

// Once per generation; only the ESP uses it (it asks several times a frame; the render thread owns the generation).
ActorState actor_state_cached(int i) {
  ActorState s = AS_NONE;
  if (actor_cache_get(&s_cache, i, &s)) return s;
  ActorStateDetail d;
  s = actor_state_live_detail(i, &d);
  vanish_note(i, s, d);
  actor_cache_put(&s_cache, i, s);
  return s;
}

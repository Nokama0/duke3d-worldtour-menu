#pragma once
// The aim ledger's rules. EARLY (condition only just true, heldTics <= SETTLE: a delay would fix it) is not WILD (held a
// while, still missed: the aim itself is wrong).

const int AIMDIAG_SETTLE_TICS = 3;

inline bool aimdiag_streq(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *a == *b) { a++; b++; }
  return *a == *b;
}

// NOT "the shot landed" (the caller ANDs in the hitscan). An unreadable size answers "lands" rather than invent a miss.
inline bool aimdiag_aim_on_box(int lat, int vert, int halfW, int halfH) {
  if (halfW <= 0 || halfH <= 0) return true;
  const int al = lat  < 0 ? -lat  : lat;
  const int av = vert < 0 ? -vert : vert;
  return al <= halfW && av <= halfH;
}

// `botFiring` IS REQUIRED: heldTics is reset on every refusal, so the player's own missed pulls scored EARLY.
inline const char* aimdiag_shot_verdict(bool botFiring, bool onBox, int heldTics) {
  if (!botFiring) return "manual";
  if (onBox) return "hit";
  return heldTics <= AIMDIAG_SETTLE_TICS ? "EARLY" : "WILD";
}
inline bool aimdiag_verdict_is_anomaly(const char* v) {
  return aimdiag_streq(v, "EARLY") || aimdiag_streq(v, "WILD");
}

// The one thing that can prove actor_damage_lands wrong. BOTH ENDS must hold: a Firefly changes size mid-fight, so a
// drop beside its CURRENT small xrepeat looked like a wrong rule.
inline bool aimdiag_contradicts_discard(int extraNow, int extraThen,
                                        bool undamageableNow, bool undamageableThen) {
  return extraNow < extraThen && undamageableNow && undamageableThen;
}

// Stop reasons tbot_decide can name, tallied so the summary says which dominated. "blocked" is three answers (wall /
// target died / ray elsewhere); the finer names are log only.
const int AIMJUMP_R_OTHER = 0, AIMJUMP_R_BLOCKED = 1, AIMJUMP_R_NOLOCK = 2,
          AIMJUMP_R_RANGE = 3, AIMJUMP_R_NOTENEMY = 4, AIMJUMP_R_WEAPON = 5,
          AIMJUMP_R_DELAY = 6, AIMJUMP_R_NOHIT = 7, AIMJUMP_R_DIED = 8,
          AIMJUMP_R_WALL = 9, AIMJUMP_R_STRAY = 10;
const int AIMJUMP_REASONS = 11;

inline const char* aimjump_blocked_reason(bool rayOnTarget, bool rayAlive,
                                          bool rayHitWall) {
  if (rayOnTarget && !rayAlive) return "died";
  if (rayHitWall) return "wall";
  return "stray";
}
inline bool aimjump_reason_is_kill(const char* r) { return aimdiag_streq(r, "died"); }

inline int aimjump_reason_index(const char* r) {
  if (aimdiag_streq(r, "blocked"))  return AIMJUMP_R_BLOCKED;
  if (aimdiag_streq(r, "nolock"))   return AIMJUMP_R_NOLOCK;
  if (aimdiag_streq(r, "range"))    return AIMJUMP_R_RANGE;
  if (aimdiag_streq(r, "notenemy")) return AIMJUMP_R_NOTENEMY;
  if (aimdiag_streq(r, "weapon"))   return AIMJUMP_R_WEAPON;
  if (aimdiag_streq(r, "delay"))    return AIMJUMP_R_DELAY;
  if (aimdiag_streq(r, "nohit"))    return AIMJUMP_R_NOHIT;
  if (aimdiag_streq(r, "died"))     return AIMJUMP_R_DIED;
  if (aimdiag_streq(r, "wall"))     return AIMJUMP_R_WALL;
  if (aimdiag_streq(r, "stray"))    return AIMJUMP_R_STRAY;
  return AIMJUMP_R_OTHER;
}
inline const char* aimjump_reason_name(int i) {
  switch (i) {
    case AIMJUMP_R_BLOCKED:  return "blocked";
    case AIMJUMP_R_NOLOCK:   return "nolock";
    case AIMJUMP_R_RANGE:    return "range";
    case AIMJUMP_R_NOTENEMY: return "notenemy";
    case AIMJUMP_R_WEAPON:   return "weapon";
    case AIMJUMP_R_DELAY:    return "delay";
    case AIMJUMP_R_NOHIT:    return "nohit";
    case AIMJUMP_R_DIED:     return "died";
    case AIMJUMP_R_WALL:     return "wall";
    case AIMJUMP_R_STRAY:    return "stray";
    default:                 return "other";
  }
}

// `flips` IS the jumpiness: a bot that opens once and holds has one, one chattering at an enemy behind a grate has
// hundreds.
struct AimJump {
  int ticsSeen;      // tics the bot was asked at all
  int ticsFiring;    // of those, tics it held the fire bit
  int flips;         // FIRE<->stop transitions
  int killStops;     // of those, the ones where the target had just died
  int shots;         // shots the ledger recorded in this interval
  int early;         // of those, verdict EARLY
  int wild;          // of those, verdict WILD
  int targets;       // distinct target indices locked
  int lastTarget;    // -1 = none: 0 is a valid sprite index
  int wasFiring;     // previous tic's decision
  int stops[AIMJUMP_REASONS];
  unsigned startedMs;
};

inline void aimjump_reset(AimJump* j, unsigned nowMs) {
  if (!j) return;
  const int keepTarget = j->lastTarget;
  const int keepFiring = j->wasFiring;
  AimJump z = {};
  *j = z;
  j->lastTarget = keepTarget;   // an unbroken lock is not a new one
  j->wasFiring  = keepFiring;   // just because the clock ticked
  j->startedMs  = nowMs;
}

inline void aimjump_tic(AimJump* j, int firing, int target, const char* reason) {
  if (!j) return;
  j->ticsSeen++;
  if (firing) j->ticsFiring++;
  if (firing != j->wasFiring) {
    j->flips++;
    if (!firing) {
      const int r = aimjump_reason_index(reason);
      if (r >= 0 && r < AIMJUMP_REASONS) j->stops[r]++;
      if (aimjump_reason_is_kill(reason)) j->killStops++;
    }
  }
  j->wasFiring = firing;
  if (target >= 0 && target != j->lastTarget) { j->targets++; j->lastTarget = target; }
}

inline void aimjump_shot(AimJump* j, const char* verdict) {
  if (!j) return;
  j->shots++;
  if (aimdiag_streq(verdict, "EARLY")) j->early++;
  else if (aimdiag_streq(verdict, "WILD")) j->wild++;
}

// TWO QUESTIONS: "has the interval elapsed" decides when to RESET, "was there anything in it" whether to LOG.
inline bool aimjump_elapsed(const AimJump& j, unsigned nowMs, unsigned periodMs) {
  return (unsigned)(nowMs - j.startedMs) >= periodMs;
}
inline bool aimjump_worth_reporting(const AimJump& j) {
  return j.ticsFiring != 0 || j.flips != 0 || j.shots != 0;
}

inline int aimjump_top_stop(const AimJump& j) {
  int best = 0;
  for (int i = 1; i < AIMJUMP_REASONS; i++) if (j.stops[i] > j.stops[best]) best = i;
  return best;
}

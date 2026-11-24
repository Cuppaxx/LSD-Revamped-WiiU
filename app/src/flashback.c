/* flashback.c - see flashback.h */
#include "flashback.h"
#include "save.h"
#include "level.h"
#include <string.h>
#include <stdio.h>

void lsd_log(const char *fmt, ...);

static int s_active, s_disabled_days;         /* flashbackDisabledForDays: RAM only, like Revamped */
static float s_total, s_t, s_secs;            /* flashback length; time in this jump; length of this jump (8..16 s) */
static int s_env_day = 1;
static int s_has_override; static SaveEC s_ev; /* the event being replayed */

static float frand(unsigned *rng, float lo, float hi) { return lo + (rng_next(rng) % 100000) / 100000.0f * (hi - lo); }

int fb_can(void) { return s_disabled_days <= 0 && g_save.nec > 100; }
int fb_active(void) { return s_active; }
int fb_disabled_days(void) { return s_disabled_days; }
float fb_total_seconds(void) { return s_total; }
int fb_env_day(void) { return s_env_day; }
void fb_greyman(void) { s_disabled_days = 10; lsd_log("flashbacks disabled for 10 days (grey man)"); }
void fb_day_ended(void) { if (s_disabled_days > 0) s_disabled_days--; }

/* a uniformly random entity contribution; its dream (missing or out of range: a random enabled dream, like Revamped
 * does for old saves without a dream name) */
static int pick(const DreamList *dl, unsigned *rng) {
  s_has_override = 0;
  if (!g_save.nec || !dl->n) return 0;
  s_ev = g_save.ec[rng_next(rng) % (unsigned)g_save.nec];
  int d = s_ev.dream;
  if (d < 0 || d >= dl->n || !dream_enabled(dl, d)) {
    int n = 0; d = 0;
    for (int i = 0; i < dl->n; i++) if (dream_enabled(dl, i)) { n++; if (rng_next(rng) % (unsigned)n == 0) d = i; }
    lsd_log("flashback: event dream %d not available, random dream %d instead", s_ev.dream, d);
  } else s_has_override = 1;
  lsd_log("flashback: event day %d (%d,%d) in %s at %.2f,%.2f,%.2f yaw %.1f entity '%s'", s_ev.day, s_ev.d, s_ev.u, dl->d[d].name, s_ev.x, s_ev.y, s_ev.z, s_ev.yaw, s_ev.id);
  return d;
}

int fb_begin(const DreamList *dl, unsigned *rng) {
  s_active = 1; s_t = 0;
  s_total = frand(rng, 60.0f, 360.0f);                    /* MIN/MAX_SECONDS_IN_DREAM, not the dream's own length */
  s_secs = frand(rng, 8.0f, 16.0f);                       /* MIN/MAX_SECONDS_IN_FLASHBACK */
  s_env_day = 1 + (int)(rng_next(rng) % 365u);            /* dream.RandomEnvironment() + a random texture set */
  lsd_log("flashback: %.0f s, %d entity contributions in the pool", s_total, g_save.nec);
  return pick(dl, rng);
}

int fb_next(const DreamList *dl, unsigned *rng) {
  if (rng_next(rng) & 1) s_env_day = 1 + (int)(rng_next(rng) % 365u);   /* 1 in 2: a new random texture set */
  s_secs = frand(rng, 8.0f, 16.0f);
  return pick(dl, rng);
}

void fb_after_load(Player *pl, Dream *d) {
  if (!s_active) return;
  s_t = 0;
  if (s_has_override) {                                   /* the player's feet and facing when the contribution was logged */
    pl->x = s_ev.x; pl->y = s_ev.y + 0.01f; pl->z = s_ev.z; pl->vy = 0;
    pl->yaw = s_ev.yaw * 0.0174532925f; pl->pitch = 0; pl->into_wall = 0; pl->wall_t = 0;
    s_has_override = 0;
  }
  if (s_ev.id[0] && d)                                    /* "if entity ev.entityID exists: SetActive(true)" (the intended one, */
    for (int i = 0; i < d->nent; i++)                     /*  not Revamped's first-event bug) */
      if (!strcmp(d->ent[i].name, s_ev.id)) { d->ent[i].active = 1; break; }
}

int fb_update(float dt) {
  if (!s_active) return 0;
  s_t += dt;
  if (s_t >= s_secs) { s_t = 0; return 1; }
  return 0;
}

void fb_end(void) { if (s_active) lsd_log("flashback over"); s_active = 0; s_has_override = 0; }

void fb_tint(Frame *f) {
  int n = f->w * f->h; uint32_t *p = f->color;
  for (int i = 0; i < n; i++) {
    uint32_t c = p[i], r = (c >> 16 & 255) + 102, g = (c >> 8 & 255) + 102, b = (c & 255) + 102;
    p[i] = 0xFF000000u | (r > 255 ? 255u : r) << 16 | (g > 255 ? 255u : g) << 8 | (b > 255 ? 255u : b);
  }
}

void fb_link_color(float rgb[3], unsigned *rng) {
  static const float cols[7][3] = { {1, 1, 1}, {1, 0, 0}, {1, 0.92f, 0.016f}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {1, 0, 1} };
  memcpy(rgb, cols[rng_next(rng) % 7u], sizeof cols[0]);   /* (a script's SetNextTransitionColor still wins: main applies it after) */
}

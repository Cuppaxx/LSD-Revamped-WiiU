/* view.c - see view.h */
#include "view.h"
#include "save.h"
#include <math.h>

int g_view_now;
static float s_cur = 40.0f, s_target = 40.0f;       /* AUTO: the distance drawn now and where it is heading */
static float s_win_t, s_win_dt, s_acc, s_max; static int s_n;
static int s_test;                                   /* start.txt override: 0 none, > 0 tiles, -1 AUTO */

#define VIEW_MIN 15.0f
#define WINDOW 0.5f

void view_test_set(int tiles) { s_test = tiles; }
static void window_reset(void) { s_win_t = s_win_dt = s_acc = s_max = 0; s_n = 0; }
void view_new_dream(void) { if (s_cur > 60.0f) s_cur = 60.0f; if (s_target > s_cur) s_target = s_cur; window_reset(); }

float view_frame(float dt, unsigned work_us, float fog_end) {
  if (s_test > 0) { g_view_now = s_test; return (float)s_test; }
  if (s_test == 0 && g_opt.view != VIEW_AUTO) { int v = view_tiles(g_opt.view); g_view_now = v; return (float)v; }
  float cap = (float)view_tiles(g_opt.vcap);
  if (g_opt.fogmode == FOG_CLASSIC) cap = fminf(cap, fmaxf(VIEW_MIN, fog_end));   /* nothing to win past the fog end */
  float budget = g_opt.vtarget ? 33333.0f : 16667.0f;                            /* microseconds per frame */
  if (dt > 0 && dt < 0.25f) {                                                    /* (load hitches and pauses do not count) */
    s_acc += (float)work_us; if ((float)work_us > s_max) s_max = (float)work_us; s_win_dt += dt; s_n++;
  }
  s_win_t += dt;
  if (s_win_t >= WINDOW) {
    if (s_n >= 4) {
      /* the cost grows about with the area in view (distance squared): step by the square root of the ratio, at most
       * x1.25 up / x0.6 down per window; between 60 % and 82 % of the budget nothing changes (no hunting) */
      float avg = s_acc / s_n, m = 0.75f * avg + 0.25f * s_max, fdt = s_win_dt / s_n * 1e6f;
      float goal = 0.72f * budget, k = 1.0f;
      if (m > 0.82f * budget) k = fmaxf(0.6f, sqrtf(goal / m));
      else if (fdt > budget * 1.12f && m > 0.45f * budget) k = 0.92f;          /* frames too long although the render work fits: back off a little */
      else if (m < 0.60f * budget && fdt < budget * 1.08f) k = fminf(1.25f, sqrtf(goal / fmaxf(m, 1.0f)));
      if (k < 1.0f) s_target = s_cur * k;
      else if (k > 1.0f) s_target = fmaxf(s_target, s_cur * k);
    }
    window_reset();
  }
  if (s_target > cap) s_target = cap;
  if (s_target < VIEW_MIN) s_target = VIEW_MIN;
  /* ease towards the target: 40 tiles/s out, 160 tiles/s in */
  if (s_cur < s_target) s_cur = fminf(s_target, s_cur + 40.0f * dt);
  else if (s_cur > s_target) s_cur = fmaxf(s_target, s_cur - 160.0f * dt);
  if (s_cur > cap) s_cur = cap;
  if (s_cur < VIEW_MIN) s_cur = VIEW_MIN;
  g_view_now = (int)(s_cur + 0.5f);
  return s_cur;
}

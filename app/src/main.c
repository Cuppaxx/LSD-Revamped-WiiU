/* LSD: Revamped for Wii U. Front end (title screen, menus, pause, save data: menu.c, save.c) and the game loop: the original
 * dreams (multi-chunk levels, spawn points, per-day environment, dream graph, Lua scripts, object models), software PS1-style
 * renderer at 320x240 (or a larger internal resolution from the graphics options).
 * Data: SD:/wiiu/apps/lsdr/data/ (made with tools/convert.py) or the wuhb's content/data/. Save: SD:/wiiu/apps/lsdr/save.txt */
#include <SDL2/SDL.h>
#include <whb/file.h>
#include <whb/sdcard.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lsd.h"
#include "raster.h"
#include "render.h"
#include "collide.h"
#include "player.h"
#include "audio.h"
#include "dream.h"
#include "level.h"
#include "script.h"
#include "model.h"
#include "save.h"
#include "menu.h"
#include "input.h"
#include "sfx.h"
#include "video.h"
#include "special.h"
#include "flashback.h"
#include "view.h"
#ifndef LSDR_START_FILE                  /* test builds may rename the test hook / log / shot files so parallel test setups */
#define LSDR_START_FILE "start.txt"      /* sharing one (emulated) SD card do not read each other's files */
#endif
#ifndef LSDR_LOG_NAME
#define LSDR_LOG_NAME "log.txt"
#endif
#ifndef LSDR_SHOT_NAME
#define LSDR_SHOT_NAME "shot%d.bmp"
#endif

static int test_threads; static float test_tp[3]; static int test_has_tp; static float test_at[2]; static int test_has_at; static float test_daylen; static float test_cam[5]; static int test_has_cam; static int test_day;
static float test_face; static int test_has_face;
static int test_nvd, test_vd[16], test_fogmode = -1; static float test_vd_t[16];   /* start.txt "vdist T:N ..." / "vfog N" (view.c) */
static int test_demo, test_debug; static float test_views[24][6]; static int test_nviews; static float test_affine = -1;
static int test_nshots, test_shoti; static float test_shot_t[32]; static const char *s_savebase;   /* start.txt "shots T ...": frame dumps to shotN.bmp */
static int test_nkeys, test_keyi; static float test_key_t[64]; static char test_key[64];   /* start.txt "keys T:K ...": scripted menu input (U D L R A B S=+) at T seconds */          /* start.txt: demo = auto-walk until a controller is touched; debug = X/Y/D-pad debug keys */
static FILE *lfile; static const char *sdpath;
static char ring[14][80]; static int ring_n;

void lsd_log(const char *fmt, ...) {
  char b[300]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  snprintf(ring[ring_n++ % 14], 80, "%s", b);
  if (lfile) { fprintf(lfile, "[%6u] %s\n", (unsigned)SDL_GetTicks(), b); fflush(lfile); }
}

uint8_t *lsd_read_file(const char *rel, size_t *len) {
  char p[300];
  if (1) {   /* the SD card (Cemu: no mount path, but its emulated SD card answers at fs:/vol/external01) */
    snprintf(p, sizeof p, "%s/wiiu/apps/lsdr/data/%s", sdpath ? sdpath : "fs:/vol/external01", rel);
    FILE *f = fopen(p, "rb");
    if (f) {
      fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
      uint8_t *buf = (uint8_t *)malloc((size_t)n + 1);
      if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) { fclose(f); *len = (size_t)n; return buf; }
      free(buf); fclose(f);
    }
  }
  snprintf(p, sizeof p, "/vol/content/data/%s", rel);
  uint32_t n = 0; char *d = WHBReadWholeFile(p, &n);
  if (!d) return NULL;
  uint8_t *buf = (uint8_t *)malloc((size_t)n + 1);
  if (buf) memcpy(buf, d, n);
  WHBFreeWholeFile(d);
  *len = n;
  return buf;
}


typedef struct { const Cam *cam; const Env *env; const SkyFx *fx; } SkyJob;
static void sky_band(Frame *f, int y0, int y1, void *ud) { SkyJob *j = (SkyJob *)ud; env_sky_rows(f, j->cam, j->env, y0, y1); if (j->fx) skyfx_band(f, j->cam, j->fx, y0, y1); }
unsigned lsd_ticks(void) { return SDL_GetTicks(); }
static void fmt1(char *out, size_t n, float v) { snprintf(out, n, "%d.%d", (int)v, (int)(fabsf(v - (int)v) * 10)); }

/* ---- music ---- */
static char song_name[48];
static void set_song_name(const char *path) {                       /* "STG00/LoadedTracks/BGA/BGA-STANDERD.wav" -> "BGA-STANDERD" */
  const char *b = strrchr(path, '/'); b = b ? b + 1 : path;
  snprintf(song_name, sizeof song_name, "%s", b); char *dot = strrchr(song_name, '.'); if (dot) *dot = 0;
}
/* one style of music for the whole day (rollSongStyle). On every dream load (DreamSystem): songNumber = day 1 ? 2 :
 * random(0..4) (SONGS_PER_DREAM 5), song = list[style][songNumber % count]. The Additional Dreams have no SongLibrary and play
 * the lists of the original dream with the same number (ADDnn -> STGnn, OriginalSongLibrary[GetDreamIndex % 14]).
 * Files missing from the bundle (only the standard style is copied by default) fall back to the next song / another style. */
static int try_song(const char *p) {
  size_t len; uint8_t *buf = lsd_read_file(p, &len);
  if (!buf) return 0;
  int rc = audio_music_start(buf, len); free(buf); lsd_log("music %s rc=%d", p, rc);
  if (rc == 0) { set_song_name(p); return 1; }
  return 0;
}
static void start_music(const Level *lv, int style, unsigned *rng, const DreamList *dl, int day) {
  const Dream *d = lv->dream;
  int has = 0; for (int k = 0; k < 7; k++) has += d->nsong[k];
  if (!has) {                                       /* a mod dream: the song lists of the original dream with its number */
    int o = sfx_mod_songs(d->folder);
    if (o >= 0 && o < dl->norig) d = &dl->d[o];
  }
  int num = day == 1 ? g_sfx_day1_song : (int)(rng_next(rng) % (unsigned)g_sfx_songs_per_dream);
  for (int pass = 0; pass < 2; pass++)              /* pass 0: the rolled style, pass 1: the other styles in order */
    for (int k = 0; k < 7; k++) {
      int si = pass == 0 ? style : k; if (pass == 0 && k > 0) break;
      if (pass == 1 && si == style) continue;
      int n = d->nsong[si]; if (!n) continue;
      for (int j = 0; j < n; j++) if (try_song(d->song[si][(num + j) % n])) return;
    }
  audio_music_stop(); song_name[0] = 0;
}
static void start_menu_music(void) {
  const char *p = menu_music_for_day(g_save.day);
  size_t len; uint8_t *buf = lsd_read_file(p, &len);
  if (!buf) { audio_music_stop(); return; }
  int rc = audio_music_start(buf, len); free(buf); lsd_log("menu music %s rc=%d", p, rc);
}

/* pick the next dream like a random link: weighted among the linkable dreams, never the current one */
/* (with ADDITIONAL DREAMS on, the originals and the mod's dreams are one pool) */
static int next_dream(const DreamList *dl, int cur, unsigned *rng) {
  float total = 0;
  for (int i = 0; i < dl->n; i++) if (i != cur && dl->d[i].linkable && dream_enabled(dl, i)) total += dl->d[i].weight;
  if (total <= 0) return cur;
  float r = (rng_next(rng) % 10000) / 10000.0f * total;
  for (int i = 0; i < dl->n; i++) if (i != cur && dl->d[i].linkable && dream_enabled(dl, i)) { r -= dl->d[i].weight; if (r <= 0) return i; }
  return cur;
}
/* the first day: DreamJournal.GetFirstDream (a random FirstDay dream) over the enabled journals: Bright Moon Cottage or
 * Dreamscape when the Additional Dreams are on */
static int first_dream_pick(const DreamList *dl, unsigned *rng) {
  int pick = -1, n = 0;
  for (int i = 0; i < dl->n; i++) if (dl->d[i].firstday && dream_enabled(dl, i)) { n++; if (rng_next(rng) % (unsigned)n == 0) pick = i; }
  return pick >= 0 ? pick : 0;
}
/* the next day's dream from the graph position (GraphSpawnMap.Get): each journal has its own 19x19 map; with both enabled
 * one of the two cells is taken at random (a cell only one map fills always gives that dream); -1 = none */
static int graph_dream(const DreamList *dl, int gx, int gy, unsigned *rng) {
  int c = (gy + 9) * GRAPH_SIZE + (gx + 9);
  if (c < 0 || c >= GRAPH_SIZE * GRAPH_SIZE) return -1;
  int a = dl->has_graph ? dl->graph[c] : -1, b = dl->has_graph2 && g_addl_enabled ? dl->graph2[c] : -1;
  if (a >= dl->n) a = -1;
  if (b >= dl->n) b = -1;
  if (a >= 0 && b >= 0) return rng_next(rng) & 1 ? b : a;
  return a >= 0 ? a : b;
}


/* ---- the grey man: turns up at most once per visit to a dream, on even days, 1 in 12 (GreymanSpawner), walks at the player and
 * whites the screen out when he gets within 2 units (Greyman) ---- */
typedef struct { int on, hit, spawned_visit; float x, y, z, yaw, age, walked, lx, lz; int phase; float ph_t; } Grey;
static int test_grey;
static void grey_spawn(Grey *g, const Player *p, float dist) {
  g->x = p->x + sinf(p->yaw) * dist; g->z = p->z + cosf(p->yaw) * dist; g->y = p->y + 0.1f;
  g->yaw = atan2f(p->x - g->x, p->z - g->z); g->on = 1; g->hit = 0; g->age = 0; g->spawned_visit = 1;
}
static void grey_roll(Grey *g, const Dream *d, const Player *p, int day, unsigned *rng) {
  if (g->spawned_visit || (day % 2) != 0 || !d->grey || (rng_next(rng) % 12) != 0) return;
  grey_spawn(g, p, 10.0f);
}
static void grey_new_dream(Grey *g, const Dream *d, const Player *p, int day, unsigned *rng) {
  memset(g, 0, sizeof *g); g->lx = p->x; g->lz = p->z;
  if (test_grey) { grey_spawn(g, p, 7.0f); return; }
  grey_roll(g, d, p, day, rng);                                    /* may scare the player by spawning right away */
}

/* ---- the dream graph (DreamSequence.EvaluateGraphPosition / ChartZone) ---- */
typedef struct { int n, sd, su, ld, lu; } Contrib;                  /* area contributions: count, sums, last */
static void contrib_add(Contrib *c, int d, int u) { c->n++; c->sd += d; c->su += u; c->ld = d; c->lu = u; }
static int wrap9(int v) { return v < -9 ? 9 : v > 9 ? -9 : v; }
static void eval_list(const Contrib *c, int *gx, int *gy) {
  if (!c->n) { *gx = *gy = 0; return; }
  *gx = wrap9(c->sd / c->n + c->ld / 3); *gy = wrap9(c->su / c->n + c->lu / 3);   /* the average plus a third of the last */
}
/* DreamSequence.EvaluateGraphPosition: two lists per day, area (LBD chunks, grey man) and entity (LogGraphContribution,
 * GraphContributor); with no entity contribution the area position counts twice: pos = wrap((A + E) / 2) */
static void graph_pos2(const Contrib *area, const Contrib *ent, int *gx, int *gy) {
  int ax, ay, ex, ey; eval_list(area, &ax, &ay);
  if (ent && ent->n) eval_list(ent, &ex, &ey); else { ex = ax; ey = ay; }
  *gx = wrap9((ax + ex) / 2); *gy = wrap9((ay + ey) / 2);
}
static void zone_color(int x, int y, float rgb[3]) {                /* fade colour by chart zone */
  float r = 1, g = 1, b = 1;
  if (x <= -4 && y >= 4) { r = 0; g = 1; b = 0; }
  else if (x <= 3 && y >= 4) { r = 0; g = 0; b = 1; }
  else if (x >= 4 && y >= 4) { r = 1; g = 0; b = 1; }
  else if (x >= 4 && y >= -3) { r = 0; g = 0; b = 0; }
  else if (x >= 4 && y <= -4) { r = 0; g = 1; b = 1; }
  else if (x <= -4 && y <= -4) { r = 1; g = 1; b = 0; }
  else if (x <= 3 && y <= -4) { r = 1; g = 0; b = 0; }
  rgb[0] = r; rgb[1] = g; rgb[2] = b;
}
/* which chunk of the dream is the player over (LBDGraphContributor.positionToLBDIndex)? -1 = none */
static int chunk_index(const Dream *d, float x, float y, float z) {
  if (d->layout == 2) return -1;                 /* mesh dreams: no area contributions (gcontrib entities instead) */
  if (d->layout == 0) {
    int yp = (int)(z / 20), xm = (yp % 2 == 1) ? 10 : 0, xp = (int)((x + xm) / 20);
    if (z < 0 || yp >= d->tilew || xp < 0 || xp > d->tilew) return -1;
    return yp * d->tilew + xp;
  }
  int fy = (int)y; return fy < 0 ? -1 : fy;
}
static float day_length(const Dream *d, unsigned *rng) { if (test_daylen > 0) return test_daylen;
  return d->length > 0 ? d->length : 60.0f + (rng_next(rng) % 30000) / 100.0f; }

static int s_req_link = -2, s_next_override = -1, s_req_endday = 0, s_lua_end = 0, s_day = 1; static Contrib *s_area, *s_entc; static char s_zone[12] = "white";
/* the day's entity contributions with where the player stood (saved as `ec` lines at the day end: the flashback pool) */
typedef struct { int d, u; float x, y, z, yaw; int dream; char id[48]; } DayEC;
static DayEC s_dayec[512]; static int s_ndayec; static const Player *s_pl; static int s_cur_dream;
#define graph_pos(a, gx, gy) graph_pos2((a), s_entc, (gx), (gy))
static void h_transition(int d) { s_req_link = d; }
static void h_set_next(int d) { s_next_override = d; }
static void h_end_day(void) { s_req_endday = 1; }
static void h_log_graph(int d, int u, const char *id) {
  if (!s_entc || fb_active()) return;              /* a flashback has no CurrentSequence: nothing is recorded */
  contrib_add(s_entc, d, u);
  if (s_ndayec < 512) { DayEC *e = &s_dayec[s_ndayec++]; e->d = d; e->u = u; e->dream = s_cur_dream;
    e->x = s_pl ? s_pl->x : 0; e->y = s_pl ? s_pl->y : 0; e->z = s_pl ? s_pl->z : 0; e->yaw = s_pl ? s_pl->yaw * 57.2957795f : 0;
    snprintf(e->id, sizeof e->id, "%s", id ? id : ""); }
  lsd_log("entity graph contribution %d,%d from %s", d, u, id ? id : "?");
}
static int h_day(void) { return s_day; }
static const char *h_zone(void) { return s_zone; }
static const ScriptHost s_host = { h_transition, h_set_next, h_end_day, h_log_graph, h_day, h_zone };
static void zone_name(int x, int y, char *out) {
  const char *n = "white";
  if (x <= -4 && y >= 4) n = "green"; else if (x <= 3 && y >= 4) n = "blue"; else if (x >= 4 && y >= 4) n = "magenta";
  else if (x >= 4 && y >= -3) n = "black"; else if (x >= 4 && y <= -4) n = "cyan"; else if (x <= -4 && y <= -4) n = "yellow"; else if (x <= 3 && y <= -4) n = "red";
  snprintf(out, 12, "%s", n);
}

/* ---- video: internal resolution and how it is scaled to the TV (graphics options) ---- */
static SDL_Renderer *ren; static SDL_Texture *tex, *mtex, *utex; static Frame *fr, *mf, *uf;   /* mf: 320x240 (splash, loading, movies, text days); uf: the menus, 320 or 426 x 240 by the aspect option */
static int frame_alloc(Frame *f, int w, int h) {
  free(f->color); free(f->depth);
  f->color = (uint32_t *)malloc((size_t)w * h * 4); f->depth = (float *)malloc((size_t)w * h * 4); f->w = w; f->h = h;
  return f->color && f->depth ? 0 : -1;
}
static void apply_video(Cam *cam) {
  static const int res_h[] = { 240, 300, 360, 480 };
  int h = res_h[g_opt.res], w = g_opt.aspect ? (h * 16 / 9) & ~1 : h * 4 / 3;
  if (!fr->color || fr->w != w || fr->h != h) {
    if (frame_alloc(fr, w, h)) { lsd_log("video: %dx%d does not fit in memory, back to 320x240", w, h); g_opt.res = 0; g_opt.aspect = 0; w = 320; h = 240; frame_alloc(fr, w, h); }
    if (tex) SDL_DestroyTexture(tex);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    raster_clear(fr, 0xFF000000u);
    lsd_log("video: internal %dx%d", w, h);
  }
  cam->focal = 207.0f * h / 240.0f;               /* the same vertical field of view at every resolution; 16:9 sees more to the sides */
  g_snap_step = g_opt.snap ? h / 240.0f : 0.0f;  /* snap to the PS1's 320x240 grid, scaled */
}
static void dump_bmp(const Frame *f, int n) {                /* test hook: the frame as a 24-bit BMP next to the save */
  char p[300]; snprintf(p, sizeof p, "%s/wiiu/apps/lsdr/" LSDR_SHOT_NAME, s_savebase, n);
  FILE *o = fopen(p, "wb"); if (!o) return;
  int rowb = (f->w * 3 + 3) & ~3, size = 54 + rowb * f->h;
  uint8_t h[54] = { 'B', 'M' }; uint32_t v[] = { (uint32_t)size, 0, 54, 40, (uint32_t)f->w, (uint32_t)f->h };
  for (int i = 0; i < 6; i++) for (int k = 0; k < 4; k++) h[2 + i * 4 + k] = (uint8_t)(v[i] >> (8 * k));
  h[26] = 1; h[28] = 24; fwrite(h, 1, 54, o);
  uint8_t *row = (uint8_t *)calloc(1, (size_t)rowb);
  for (int y = f->h - 1; y >= 0; y--) { for (int x = 0; x < f->w; x++) { uint32_t c = f->color[y * f->w + x]; row[x * 3] = c & 255; row[x * 3 + 1] = c >> 8 & 255; row[x * 3 + 2] = c >> 16 & 255; } fwrite(row, 1, (size_t)rowb, o); }
  free(row); fclose(o); lsd_log("test shot %d (%dx%d)", n, f->w, f->h);
}
static void frame_dst(const Frame *f, SDL_Rect *dst, int *ow, int *oh) {   /* where a frame lands on the output */
  *ow = 1280; *oh = 720; SDL_GetRendererOutputSize(ren, ow, oh);
  dst->x = dst->y = 0; dst->w = *ow; dst->h = *oh;
  if (g_opt.scaling != 2) {                       /* fit or integer: keep the frame's aspect, centred */
    float s = fminf((float)*ow / f->w, (float)*oh / f->h);
    if (g_opt.scaling == 1 && s >= 1.0f) s = floorf(s);
    dst->w = (int)(f->w * s); dst->h = (int)(f->h * s); dst->x = (*ow - dst->w) / 2; dst->y = (*oh - dst->h) / 2;
  }
}
/* GamePad touch (0..1 of the screen, which shows the TV picture) -> pixel of frame f; 0 = outside it */
static int touch_to_frame(const Frame *f, float tx, float ty, int *x, int *y) {
  SDL_Rect d; int ow, oh; frame_dst(f, &d, &ow, &oh);
  if (d.w <= 0 || d.h <= 0) return 0;
  *x = (int)((tx * ow - d.x) * f->w / d.w); *y = (int)((ty * oh - d.y) * f->h / d.h);
  return *x >= 0 && *y >= 0 && *x < f->w && *y < f->h;
}
static void present(SDL_Texture *t, const Frame *f) {
  if (test_shoti < test_nshots && SDL_GetTicks() >= (uint32_t)(test_shot_t[test_shoti] * 1000)) dump_bmp(f, test_shoti++);
  SDL_UpdateTexture(t, NULL, f->color, f->w * 4);
  SDL_Rect dst; int ow, oh; frame_dst(f, &dst, &ow, &oh);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren); SDL_RenderCopy(ren, t, NULL, &dst); SDL_RenderPresent(ren);
}
static void apply_ui_frame(void) {                /* the menu frame follows the aspect option: 320x240 or 426x240 */
  int w = menu_frame_w(g_opt.aspect);
  if (uf->color && uf->w == w) return;
  frame_alloc(uf, w, MENU_H); raster_clear(uf, 0xFF000000u);
  if (utex) SDL_DestroyTexture(utex);
  utex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, MENU_H);
}
static void apply_options(Cam *cam) {
  audio_set_volume(g_opt.music_vol, g_opt.sfx_vol);
  if (!test_threads) raster_threads_set(g_opt.threads);
  g_draw_models = g_opt.models;
  g_addl_enabled = g_opt.addl;
  render_set_affine(test_affine >= 0 ? test_affine : g_opt.affine * 0.25f);
  apply_video(cam);
  apply_ui_frame();
}
/* the game frame as a dw x 240 menu background (the centre part of the same aspect, nearest neighbour) */
static void snapshot(const Frame *src, uint32_t *dst, int dw) {
  int sw = src->h * dw / MENU_H; if (sw > src->w) sw = src->w; int sx0 = (src->w - sw) / 2;
  for (int y = 0; y < MENU_H; y++) { const uint32_t *row = src->color + (size_t)(y * src->h / MENU_H) * src->w + sx0;
    for (int x = 0; x < dw; x++) dst[y * dw + x] = row[x * sw / dw]; }
}
static void fade_frame(Frame *f, float fade, const float rgb[3]) {
  int m = (int)((1.0f - fade) * 256), fr_ = (int)(rgb[0] * fade * 255), fg_ = (int)(rgb[1] * fade * 255), fb_ = (int)(rgb[2] * fade * 255);
  for (int i = 0; i < f->w * f->h; i++) { uint32_t c = f->color[i]; f->color[i] = 0xFF000000u | (((((c >> 16 & 255) * m) >> 8) + fr_) << 16) | (((((c >> 8 & 255) * m) >> 8) + fg_) << 8) | ((((c & 255) * m) >> 8) + fb_); }
}

/* what happens when the screen has faded out */
enum { GO_NONE, GO_LINK /* same day */, GO_DAYEND /* next day */, GO_START /* begin a day at `day` */, GO_TITLE, GO_EVENT /* a dream object's movie (special.c) */ };

int main(void) {
  WHBMountSdCard(); sdpath = WHBGetSdCardMountPath();
  const char *savebase = sdpath ? sdpath : "fs:/vol/external01"; s_savebase = savebase;   /* Cemu: no mount path, but its emulated SD card answers here */
  save_init(savebase);                                 /* also creates SD:/wiiu/apps/lsdr */
  { char p[300]; snprintf(p, sizeof p, "%s/wiiu/apps/lsdr/" LSDR_LOG_NAME, savebase); lfile = fopen(p, "wb"); }
  video_set_base(savebase);                            /* movies: <SD>/wiiu/apps/lsdr/data/MOVIE (not in the wuhb) */
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) return 1;
  audio_init();                                        /* one device for the whole run: music and voices */
  SDL_Window *win = SDL_CreateWindow("LSD", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, 0);
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
  if (!win || !ren) return 2;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  input_init();                                        /* GamePad, Pro Controllers, Classic Controllers, Wii Remotes (more when they connect) */

  int nolink = 0, start_idx = -1, start_log = 0, test_menu = 0, test_splash = 0, have_start = 0; float cycle_s = 0;
  { size_t sn; uint8_t *sb = lsd_read_file(LSDR_START_FILE, &sn);       /* test hook (not shipped): "<dream index> [nolink] [cycleSECONDS] [log] [threadsN] [tp X Y Z] [at X Z] [grey] [daylenN] [cam X Y Z YAWDEG PITCHDEG] [today N] [demo] [debug] [menu]" */
    if (sb) { char t[1024] = ""; memcpy(t, sb, sn < 1023 ? sn : 1023);
      char *vw = strstr(t, "views"); if (vw) { vw += 5; float v[6]; int k;      /* views T:X,Y,Z,YAW,PITCH ... : the fixed camera moves at T s */
        while (test_nviews < 24 && sscanf(vw, " %f:%f,%f,%f,%f,%f%n", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &k) == 6) { memcpy(test_views[test_nviews++], v, sizeof v); vw += k; }
        if (test_nviews) { test_has_cam = 1; memcpy(test_cam, test_views[0] + 1, sizeof test_cam); } } sscanf(t, "%d", &start_idx); nolink = strstr(t, "nolink") != NULL; start_log = strstr(t, "log") != NULL;
      char *cy = strstr(t, "cycle"); if (cy) cycle_s = (float)atof(cy + 5); char *th = strstr(t, "thread"); if (th) test_threads = atoi(th + 6); char *tp = strstr(t, "tp"); if (tp && sscanf(tp + 2, "%f %f %f", &test_tp[0], &test_tp[1], &test_tp[2]) == 3) test_has_tp = 1; char *at = strstr(t, "at"); if (at && sscanf(at + 2, "%f %f", &test_at[0], &test_at[1]) == 2) test_has_at = 1; if (strstr(t, "grey")) test_grey = 1; char *dlen = strstr(t, "daylen"); if (dlen) test_daylen = (float)atof(dlen + 6);
      char *cm = strstr(t, "cam"); if (cm && sscanf(cm + 3, "%f %f %f %f %f", &test_cam[0], &test_cam[1], &test_cam[2], &test_cam[3], &test_cam[4]) == 5) test_has_cam = 1;
      char *vdh = strstr(t, "vdist"); if (vdh) { vdh += 5; float vt; int vv, k;   /* vdist T:N ... : from T s a fixed view of N tiles (0 = option, -1 = AUTO) */
        while (test_nvd < 16 && sscanf(vdh, " %f:%d%n", &vt, &vv, &k) == 2) { test_vd_t[test_nvd] = vt; test_vd[test_nvd++] = vv; vdh += k; } }
      char *vfg = strstr(t, "vfog"); if (vfg) test_fogmode = atoi(vfg + 4);    /* vfog N: FOG 0 classic, 1 scaled, 2 off */
      char *af = strstr(t, "affine"); if (af) test_affine = (float)atof(af + 6);   /* affine X: texture warping override */
      char *td = strstr(t, "today"); if (td) test_day = atoi(td + 5);
      char *ks = strstr(t, "keys"); if (ks) { ks += 4; float kt; char kc; int k;
        while (test_nkeys < 64 && sscanf(ks, " %f:%c%n", &kt, &kc, &k) == 2) { test_key_t[test_nkeys] = kt; test_key[test_nkeys++] = kc; ks += k; } }
      char *pd = strstr(t, "pad"); if (pd) { pd += 3; float pt, ps; char pn[16]; int k;   /* pad T:NAME:SECS ... : hold a button / stick */
        while (sscanf(pd, " %f:%15[^:]:%f%n", &pt, pn, &ps, &k) == 3) { input_test_hold(pn, pt, ps); pd += k; } }
      if (strstr(t, "footdbg")) g_foot_log = 1;
      char *spc = strstr(t, "spchoice"); if (spc) g_sp_test_choice = atoi(spc + 8);   /* special days: always choice N */
      char *fc = strstr(t, "face"); if (fc) { test_face = (float)atof(fc + 4); test_has_face = 1; }   /* face DEG: the player's yaw at the start */
      if (strstr(t, "tank")) g_opt.scheme = 1;
      char *sh = strstr(t, "shots"); if (sh) { sh += 5; float v; int k; while (test_nshots < 32 && sscanf(sh, " %f%n", &v, &k) == 1) { test_shot_t[test_nshots++] = v; sh += k; } }
      have_start = 1; test_splash = strstr(t, "splash") != NULL;
      test_demo = strstr(t, "demo") != NULL; test_debug = strstr(t, "debug") != NULL; test_menu = strstr(t, "menu") != NULL;
      lsd_log("start.txt test hooks: %s", t); free(sb); } }
  { int nt = test_threads > 0 ? test_threads : 4; size_t tn; uint8_t *tb = lsd_read_file("threads.txt", &tn);   /* optional: worker count for the rasterizer */
    if (tb) { char t[8] = ""; memcpy(t, tb, tn < 7 ? tn : 7); int v = atoi(t); if (v >= 1 && v <= 5) { nt = v; test_threads = v; } free(tb); }
    raster_threads_init(nt); }
  fr = (Frame *)calloc(1, sizeof(Frame)); mf = (Frame *)calloc(1, sizeof(Frame)); uf = (Frame *)calloc(1, sizeof(Frame));
  frame_alloc(mf, MENU_W, MENU_H);
  mtex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, MENU_W, MENU_H);
  uint32_t *pause_bg = (uint32_t *)malloc(menu_frame_w(1) * MENU_H * 4); int pause_bg_w = MENU_W;
  Cam cam = { 10, 8, -6, 0, -0.5f, 207.0f, 21.0f };
  apply_options(&cam);
  lsd_log("rasterizer threads: %d", raster_threads());

  static DreamList dl; static Level lv; memset(&lv, 0, sizeof lv);
  unsigned rng = SDL_GetTicks() * 2654435761u + 12345;
  int drc = dreams_load(&dl);
  if (drc) { lsd_log("dreams.txt failed rc=%d", drc); }
  g_addl_enabled = g_opt.addl;
  lsd_log("dreams: %d original + %d additional (%s)", dl.norig, dl.n - dl.norig, g_addl_enabled ? "on" : "off");
  int day = 1, cur = 0;
  float cycle_t = 0;
  int song_style = (int)(rng_next(&rng) % 7);
  Player pl; memset(&pl, 0, sizeof pl);
  int have_level = 0;
  LMesh grey_mesh; int have_grey = 0; Grey gm; memset(&gm, 0, sizeof gm);
  { size_t gn; uint8_t *gb = lsd_read_file("ETC/SYMSPY.MOM", &gn); if (gb) { have_grey = lsd_load_mom_mesh(gb, gn, &grey_mesh) == 0; free(gb); } lsd_log("grey man model: %s", have_grey ? "loaded" : "missing"); }
  menu_init();
  sp_init();                                           /* specials.txt: special days, intro / ending / chart face / object movies */
  sfx_init();                                         /* footsteps, link/fall sounds, the SE bank (audio.txt) */

  int manual = 0, fly = 0, show_log = start_log, show_map = 0; float lx = 0, ly = 0, rx = 0, ry = 0;
  uint64_t us_sky = 0, us_lvl = 0; int nfr = 0; unsigned last_work_us = 0, us_max = 0; int st_tiny = 0, st_tris = 0; uint32_t last = SDL_GetTicks(), fps_t = last; int fps_n = 0, fps = 0;
  float auto_t = 0, fade = 0; int fade_dir = 0 /* 1 out, -1 in */, pending = -1, go = GO_NONE, run = 1;
  float fade_rgb[3] = {0, 0, 0};
  float fade_rate = 2.5f, fade_in_rate = 2.0f; int fade_blend = BLEND_ALPHA;   /* per-fade speed (1/s) and blend (special.c); reset after each fade */
  int sp_ok_start = 0;                                 /* this GO_START is DreamSystem.BeginDream (CONTINUE): a special day may play */
  float flash_t = 0, flash_len = 0, flash_rgb[3] = {1, 1, 1};   /* DreamSystem.Flash: a colour that fades out */
  Contrib area = {0}, entc = {0}; s_area = &area; s_entc = &entc; s_pl = &pl; script_init(&s_host); float chunk_t = 0; int last_chunk = -1; int graph_x = 0, graph_y = 0;
  float day_left = 0;
  int run_toggle = 0;
  RenderStats st = {0};
  Menu menu; int in_menu = 1, paused = 0;           /* in_menu: title screens (no dream loaded); paused: pause menu over a dream */
  menu_open(&menu, 0);

  /* start a day in dream `target` (title: continue / new game / dream select) */
#define BEGIN_DAY(target) do { pending = (target); go = GO_START; fade_dir = 1; fade_rgb[0] = fade_rgb[1] = fade_rgb[2] = 0; } while (0)
  /* the boot splash ("PORTED BY CUPPAX"); start.txt test runs skip it unless they ask for "splash" */
  int splash_on = !have_start || test_splash; uint32_t splash_t0 = 0;
  if (start_idx >= 0 && start_idx < dl.n && !test_menu && !drc) {   /* test hook: straight into a dream */
    day = test_day > 0 ? test_day : 1; in_menu = 0; BEGIN_DAY(start_idx); fade = 1.0f; splash_on = 0;
    save_disable();                                            /* test runs must not touch the player's journal */
  } else if (splash_on) { audio_set_volume(g_opt.music_vol, g_opt.sfx_vol); splash_chime(); splash_t0 = SDL_GetTicks(); }
  else start_menu_music();

  while (run) {
    int nav[8], nnav = 0, any_button = 0;         /* menu button presses this frame */
    input_end_frame();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) { run = 0; continue; }            /* HOME menu -> close */
      int b = input_event(&e);
      if (b < 0) continue;
      manual = 1; any_button = 1; if (test_nkeys || test_debug) lsd_log("button %s", input_button_name(b));
      if (nnav < 8) { if (b == BA) nav[nnav++] = NAV_OK; else if (b == BB) nav[nnav++] = NAV_BACK; else if (b == BPLUS) nav[nnav++] = NAV_START; }
      if (in_menu || paused || !test_debug) continue;
      if ((b == BLEFT || b == BRIGHT) && dl.n && !fade_dir) { pending = b == BRIGHT ? (cur + 1) % dl.n : (cur + dl.n - 1) % dl.n; go = GO_LINK; fade_dir = 1; audio_music_fade(1.0f); }   /* debug: next/previous dream */
      if (b == BX) { if (!show_log && !show_map) show_log = 1; else if (show_log) { show_log = 0; show_map = 1; } else show_map = 0; }
      if (b == BY) { fly = !fly; if (!fly) { pl.x = cam.x; pl.z = cam.z; pl.y = cam.y - EYE_HEIGHT; pl.vy = 0; pl.yaw = cam.yaw; } }
    }
    input_frame(SDL_GetTicks() / 1000.0f);
    if (g_in.touched) manual = 1;
    if (g_in.corner_tap && nnav < 8) nav[nnav++] = NAV_START;    /* GamePad touch screen: top right corner pauses / resumes */
    lx = g_in.lx; ly = g_in.ly; rx = g_in.rx; ry = g_in.ry;
    if (!in_menu && !paused && g_opt.run_mode && input_act_pressed(ACT_RUN)) run_toggle = !run_toggle;
    while (test_keyi < test_nkeys && SDL_GetTicks() >= (uint32_t)(test_key_t[test_keyi] * 1000) && nnav < 8) {   /* test hook: scripted presses */
      char k = test_key[test_keyi++]; lsd_log("test key %c", k);
      { int tb = k == 'A' ? BA : k == 'B' ? BB : k == 'S' ? BPLUS : k == 'U' ? BUP : k == 'D' ? BDOWN : k == 'L' ? BLEFT : BRIGHT;   /* also a button press (Interact, bindings) */
        if (!menu_capturing(&menu)) g_in.pressed[tb] = 1; }
      nav[nnav++] = k == 'U' ? NAV_UP : k == 'D' ? NAV_DOWN : k == 'L' ? NAV_LEFT : k == 'R' ? NAV_RIGHT : k == 'A' ? NAV_OK : k == 'B' ? NAV_BACK : NAV_START;
    }
    if ((in_menu || paused) && menu_capturing(&menu)) {           /* OPTIONS > CONTROLS: the next button (any controller) gets bound */
      nnav = 0;
      for (int b = 0; b < BN; b++) if (g_in.pressed[b]) { menu_capture(&menu, b); apply_options(&cam); lsd_log("bound %s", input_button_name(b)); break; }
    }
    uint32_t now = SDL_GetTicks(); float dt = (now - last) / 1000.0f; last = now; if (dt > 0.1f) dt = 0.1f;

    /* ---- boot splash: any button skips; then the title fades in from white ---- */
    if (splash_on) {
      float st_ = (now - splash_t0) / 1000.0f;
      if (any_button || nnav || st_ >= splash_length()) {
        splash_on = 0;
        lsd_log("splash done at %.2f s%s", st_, any_button || nnav ? " (skipped)" : "");
        if (!sp_start_intro(&rng)) { start_menu_music(); fade = 1.0f; fade_dir = -1; fade_rgb[0] = fade_rgb[1] = fade_rgb[2] = 1.0f; }   /* intro movie, else the title */
        nnav = 0; any_button = 0;
      } else { splash_draw(mf, st_); present(mtex, mf); continue; }
    }

    /* ---- full-screen sequences (special.c): intro, special days, movies, ending, chart face, credits, journal ---- */
    if (sp_active()) {
      SpInput si; memset(&si, 0, sizeof si);
      for (int i = 0; i < nnav; i++) { si.skip |= nav[i] == NAV_OK || nav[i] == NAV_START; si.start |= nav[i] == NAV_START; si.back |= nav[i] == NAV_BACK;
        si.up |= nav[i] == NAV_UP; si.down |= nav[i] == NAV_DOWN; si.left |= nav[i] == NAV_LEFT; si.right |= nav[i] == NAV_RIGHT; }
      si.up |= g_in.pressed[BUP]; si.down |= g_in.pressed[BDOWN]; si.left |= g_in.pressed[BLEFT] | g_in.pressed[BL]; si.right |= g_in.pressed[BRIGHT] | g_in.pressed[BR];
      if (sp_overlay()) {                                    /* the chart under the chart face highlight */
        MenuCtx mc = { &dl, paused, day, cur, 0, 0, 0, NULL, paused ? pause_bg : NULL, pause_bg_w };
        menu_draw(&menu, mf, &mc, dt);
      }
      Frame *sf = sp_menu_frame() ? uf : mf;               /* journal / credits: the menu frame (16:9 aware) */
      int r = sp_frame(sf, dt, &si);
      present(sf == uf ? utex : mtex, sf);
      if (r == SPR_TITLE || (r == SPR_EVENT_DONE && fb_active())) {   /* to the title, which fades in from black over 1 s */
        if (have_level) { da_free(); level_free(&lv); have_level = 0; }
        fb_end(); audio_music_stop();
        save_write();                                        /* (the day itself was saved where it changed) */
        in_menu = 1; paused = 0; menu_open(&menu, 0); start_menu_music();
        fade = 1.0f; fade_dir = -1; fade_in_rate = 1.0f; fade_blend = BLEND_ALPHA; fade_rgb[0] = fade_rgb[1] = fade_rgb[2] = 0;
        last = SDL_GetTicks();
      } else if (r == SPR_DAY_DONE) {                        /* a special day: a finished day at (0,0), then the next day */
        int ended365 = day == 365;
        save_add_history(0, 0); graph_x = graph_y = 0;
        day = day % 365 + 1; if (day == 1) g_save.year++;
        g_save.day = day; g_save.graph_x = g_save.graph_y = 0; g_save.has_game = 1; fb_day_ended(); save_write();
        lsd_log("special day over -> day %d", day);
        if (ended365 && sp_start_ending()) {}
        else if (sp_is_special_day(day) && sp_start_special_day(day, &rng)) {}
        else {                                               /* tomorrow's dream from graph[0,0] (the port chains days) */
          int t = graph_dream(&dl, 0, 0, &rng); if (t < 0 || t >= dl.n) t = next_dream(&dl, -1, &rng);
          BEGIN_DAY(t); fade = 1.0f; last = SDL_GetTicks();
        }
      } else if (r == SPR_EVENT_DONE) {                      /* EndDream() after the movie: the normal day end */
        graph_pos(&area, &graph_x, &graph_y);
        int gd = graph_dream(&dl, graph_x, graph_y, &rng);
        pending = gd >= 0 ? gd : next_dream(&dl, cur, &rng); go = GO_DAYEND; fade = 1.0f; fade_dir = 1; fade_rgb[0] = fade_rgb[1] = fade_rgb[2] = 0;
        last = SDL_GetTicks();
      } else if (r == SPR_BACK) last = SDL_GetTicks();
      if (r != SPR_NONE || sp_active()) continue;
    }

    /* ---- menus (title or pause) ---- */
    if ((in_menu || paused) && !fade_dir) {
      MenuCtx mc = { &dl, paused, day, cur, 0, 0, 0, song_name[0] && paused ? song_name : NULL, paused ? pause_bg : NULL, pause_bg_w };
      if (paused) { mc.cur_has = area.n + entc.n > 0; graph_pos(&area, &mc.cur_gx, &mc.cur_gy); }
      int dir = -1;
      if (g_in.down[BUP] || ly < -0.5f) dir = NAV_UP;
      else if (g_in.down[BDOWN] || ly > 0.5f) dir = NAV_DOWN;
      else if (g_in.down[BLEFT] || lx < -0.5f) dir = NAV_LEFT;
      else if (g_in.down[BRIGHT] || lx > 0.5f) dir = NAV_RIGHT;
      int acts[9], na = 0, arg = 0, a;
      if ((a = menu_hold(&menu, dir, dt, &mc, &arg)) != MA_NONE) acts[na++] = a;
      int arg2 = arg;
      for (int i = 0; i < nnav && na < 9; i++) if ((a = menu_nav(&menu, nav[i], &mc, &arg2)) != MA_NONE) { acts[na++] = a; arg = arg2; }
      { int tx, ty;                                        /* GamePad touch: tap an item */
        if (g_in.tap && na < 9 && touch_to_frame(uf, g_in.tap_x, g_in.tap_y, &tx, &ty) && (a = menu_tap(&menu, tx, ty, &mc, &arg2)) != MA_NONE) { acts[na++] = a; arg = arg2; } }
      for (int i = 0; i < na; i++) {
        switch (acts[i]) {
          case MA_OPTIONS: apply_options(&cam); break;
          case MA_OPTIONS_DONE: save_write(); break;
          case MA_QUIT: case MA_SAVE_QUIT: run = 0; break;
          case MA_RESUME: paused = 0; last = SDL_GetTicks(); break;
          case MA_TITLE: paused = 0; go = GO_TITLE; fade_dir = 1; fade_rgb[0] = fade_rgb[1] = fade_rgb[2] = 0; break;
          case MA_CHART_OPEN: sp_chart_opened(); break;   /* the chart face (special.c) */
          case MA_JOURNAL: sp_start_journal(&dl, paused, day, cur); break;
          case MA_CREDITS: sp_start_credits(); break;
          case MA_FLASHBACK: {                     /* DreamSystem.BeginFlashback: FadeIn(black, 3 s), then the first event */
            if (!fb_can()) break;
            int t = fb_begin(&dl, &rng); day = g_save.day; BEGIN_DAY(t); fade_rate = 1.0f / 3.0f; audio_music_fade(3.0f);
            break; }
          case MA_NEW_GAME:                         /* (ResetProgress: the sequences, entity contributions and years go too) */
            g_save.year = 0; g_save.nec = 0; g_save.nsp = 0;
            g_save.day = 1; g_save.graph_x = g_save.graph_y = 0; g_save.nhist = 0; g_save.has_game = 1; save_write();
            day = 1; BEGIN_DAY(first_dream_pick(&dl, &rng)); lsd_log("new game"); break;
          case MA_CONTINUE: {
            day = g_save.day;                     /* DreamSystem.getRandomDream: day 1 = the first dream, then the graph */
            int t = day == 1 ? first_dream_pick(&dl, &rng) : graph_dream(&dl, g_save.graph_x, g_save.graph_y, &rng);
            if (t < 0 || t >= dl.n) t = next_dream(&dl, -1, &rng);
            BEGIN_DAY(t); lsd_log("continue: day %d, graph %d,%d -> %s", day, g_save.graph_x, g_save.graph_y, dl.d[t].name);
            sp_ok_start = 1; if (sp_is_special_day(day)) fade_rate = 1.0f;   /* BeginDream on a special day: FadeIn(black, 1 s) */
            break; }
          case MA_DREAM:
            if (arg < 0 || arg >= dl.n) break;
            if (paused) {                         /* jump there now, same day (like a link) */
              paused = 0; last = SDL_GetTicks();
              if (arg != cur || !have_level) { pending = arg; go = GO_LINK; fade_dir = 1; fade_rgb[0] = fade_rgb[1] = fade_rgb[2] = 0; }
            } else {                              /* from the title: a day that starts there */
              if (!g_save.has_game) { g_save.has_game = 1; g_save.day = 1; }
              day = g_save.day; BEGIN_DAY(arg);
            }
            lsd_log("dream select -> %s", dl.d[arg].name); break;
        }
      }
    }

    /* ---- input -> player ---- */
    if (!in_menu && !paused && !fade_dir && have_level) {         /* + (always) or the PAUSE binding pauses */
      int want = input_act_pressed(ACT_PAUSE) && !test_has_cam;
      for (int i = 0; i < nnav; i++) want |= nav[i] == NAV_START;
      if (want) { paused = 1; menu_open(&menu, 1); pause_bg_w = uf->w; snapshot(fr, pause_bg, pause_bg_w); audio_sfx(SFX_PRESS); da_silence(); }
    }
    PlayerInput pi; memset(&pi, 0, sizeof pi); pi.tank = g_opt.scheme;
    int live = !in_menu && !paused;
    if (live && manual) {
      /* the d-pad moves too (Revamped's Classic scheme; Wii Remotes have nothing else) */
      float dx = (float)(g_in.down[BRIGHT] - g_in.down[BLEFT]), dy = (float)(g_in.down[BDOWN] - g_in.down[BUP]);
      float mvx = fabsf(lx) > fabsf(dx) ? lx : dx, mvy = fabsf(ly) > fabsf(dy) ? ly : dy;
      float strafe = (float)(input_act_down(ACT_STRAFER) - input_act_down(ACT_STRAFEL));
      if (!pi.tank) {                                  /* FPS: stick moves and strafes, right stick looks, strafe buttons add */
        pi.mx = fmaxf(-1.0f, fminf(1.0f, mvx + strafe)); pi.mz = -mvy; pi.lx = rx; pi.ly = ry;
      } else {                                         /* TANK: stick up/down steps, left/right turns; strafe buttons; look buttons or right stick */
        pi.mz = -mvy; pi.mx = strafe; pi.turn = fabsf(mvx) > 0.0f ? mvx : (fabsf(rx) > 0.5f ? rx : 0.0f);
        pi.look_up = input_act_down(ACT_LOOKUP) || ry < -0.5f; pi.look_down = input_act_down(ACT_LOOKDOWN) || ry > 0.5f;
      }
      pi.look_behind = input_act_down(ACT_LOOKBEHIND);
      pi.fwd_raw = pi.mz > 0;
      pi.run = g_opt.run_mode ? run_toggle : input_act_down(ACT_RUN);
    }
    else if (!live || test_has_cam) { }                       /* menus / test: the player stands still under a fixed camera */
    else if (test_demo) {
      /* test hook: no controller touched yet: demo walk (the Cemu test). Run forward, wander a little; walls link to the next dream. */
      auto_t += dt;
      pi.mz = 1; pi.run = 1; pi.fwd_raw = 1; pi.lx = 0.35f * sinf(auto_t * 0.8f); pi.turn = pi.lx;
      if (nolink && pl.into_wall && pl.wall_t > 0.3f) pi.lx = 1.0f;   /* nolink test walks: turn away from walls */
      if (cycle_s > 0 && !fade_dir && (cycle_t += dt) > cycle_s) { cycle_t = 0; pending = (cur + 1) % dl.n; go = GO_LINK; fade_dir = 1; fade_rgb[0] = fade_rgb[1] = fade_rgb[2] = 0; }   /* test: visit every dream in turn */
    }
    if (live && have_level && !fade_dir) {
      if (fly) {
        if (!manual) cam.yaw += dt * 0.6f;       /* test hook: slow spin so a screenshot catches the surroundings */
        cam.yaw += rx * 2.0f * dt; cam.pitch -= ry * 1.5f * dt;
        if (cam.pitch > 1.5f) cam.pitch = 1.5f;
        if (cam.pitch < -1.5f) cam.pitch = -1.5f;
        float sp = 6.0f * (g_in.down[BB] ? 2.5f : 1.0f), fw = -ly * sp * dt, sd = lx * sp * dt;
        cam.x += sinf(cam.yaw) * cosf(cam.pitch) * fw + cosf(cam.yaw) * sd;
        cam.z += cosf(cam.yaw) * cosf(cam.pitch) * fw - sinf(cam.yaw) * sd;
        cam.y += sinf(cam.pitch) * fw;
        if (g_in.down[BZR]) cam.y += sp * dt;
        if (g_in.down[BZL]) cam.y -= sp * dt;
      } else {
        if (g_sctl.override) { pi.mx = 0; pi.mz = g_sctl.forward ? 1.0f : 0.0f; pi.lx = pi.ly = 0; pi.turn = 0; pi.run = 0; pi.external = 1; pi.look_behind = 0; }
        if (!g_sctl.can_control) { memset(&pi, 0, sizeof pi); pi.tank = g_opt.scheme; }
        level_update_live(&lv);
        player_update(&pl, &lv.world, &pi, dt, lv.dream->walk);
        while (pl.footsteps > 0) { pl.footsteps--; if (g_opt.footsteps) sfx_footstep(&lv, &pl); }   /* PlayerMovement.playFootstepSound */
        s_cur_dream = cur;
        s_day = day; { int zx, zy; graph_pos(&area, &zx, &zy); zone_name(zx, zy, s_zone); }
        if (fb_active()) snprintf(s_zone, sizeof s_zone, "white");   /* no CurrentSequence in a flashback: ChartZone is white */
        if (input_act_pressed(ACT_INTERACT)) g_sctl.interact_pressed = 1;   /* Input.Interact.WasPressed */
        script_frame(dt, (Dream *)lv.dream, &pl, &lv.world);
        level_update_live(&lv);                          /* scripts may have switched objects on/off */
        if (g_link_request > -2 || s_req_link > -2) {      /* Link trigger entity or DreamSystem.TransitionToDream */
          int t = g_link_request > -2 ? g_link_request : s_req_link;
          int snd = g_link_request > -2 ? g_link_sound : g_trans_sounds;   /* TriggerLink.PlayLinkSound / SetTransitionSounds */
          if (g_link_request <= -2) g_trans_sounds = 1;                    /* reset after every TransitionToDream */
          snprintf(g_force_spawn, sizeof g_force_spawn, "%s", g_link_request > -2 ? g_link_spawn : g_next_spawn[0] ? g_next_spawn : "-");
          g_link_request = -2; s_req_link = -2; g_next_spawn[0] = 0;
          if (t < 0) t = s_next_override >= 0 ? s_next_override : next_dream(&dl, cur, &rng);
          s_next_override = -1;
          if (t >= 0 && t < dl.n && (t != cur || strcmp(g_force_spawn, "-"))) { pending = t; go = GO_LINK; fade_dir = 1; graph_pos(&area, &graph_x, &graph_y); zone_color(graph_x, graph_y, fade_rgb);
            if (fb_active()) fb_link_color(fade_rgb, &rng);                  /* flashback: RandUtil.RandColor */
            if (g_has_next_rgb) { memcpy(fade_rgb, g_next_rgb, sizeof fade_rgb); g_has_next_rgb = 0; }   /* SetNextTransitionColor */
            audio_music_fade(1.0f); if (snd) sfx_link(&rng);
            lsd_log("scripted link -> %s", dl.d[t].name); }
        }
        if (g_flash_req) { g_flash_req = 0; flash_t = 0; flash_len = g_flash_secs > 0.05f ? g_flash_secs : 0.05f; memcpy(flash_rgb, g_flash_rgb, sizeof flash_rgb); }
        if (s_req_endday) { s_req_endday = 0; day_left = 0; s_lua_end = 1; }   /* Lua EndDream(): not a timeout (no random ending) */
        { float er[3];                                     /* VideoClip.Play(colour): FadeIn(colour, 1 s), then the movie */
          if (!fade_dir && sp_event_pending(er)) { go = GO_EVENT; fade_dir = 1; memcpy(fade_rgb, er, sizeof er); fade_rate = 1.0f; audio_music_stop(); } }
        if (fb_active() && !fade_dir && fb_update(dt)) {   /* the next flashback jump: lock controls, FadeIn(white, 2 s) */
          pending = fb_next(&dl, &rng); go = GO_LINK; fade_dir = 1; fade_rgb[0] = fade_rgb[1] = fade_rgb[2] = 1.0f; fade_rate = 0.5f;
          g_sctl.can_control = 0; audio_music_fade(2.0f); }
        float gdown;                                       /* a long fall onto ground below is fine (Dreamscape's cliffs) */
        if (pl.y < lv.world.miny - 20 || (pl.vy < -9.0f && !cw_ground(&lv.world, pl.x, pl.z, pl.y + PLAYER_STEP, &gdown))) { lsd_log("fell out of the world, respawning"); sfx_fall(&rng); level_spawn(&lv, &pl, day, &rng); }
        if (!nolink && pl.into_wall && pl.wall_t > LINK_DELAY && !fade_dir) { pending = next_dream(&dl, cur, &rng); go = GO_LINK; fade_dir = 1; graph_pos(&area, &graph_x, &graph_y); zone_color(graph_x, graph_y, fade_rgb); if (fb_active()) fb_link_color(fade_rgb, &rng); pl.into_wall = 0; pl.wall_t = 0; lsd_log("link: %s -> %s", lv.dream->name, dl.d[pending].name);
          audio_music_fade(1.0f); sfx_link(&rng); }                    /* Transition(): StopSong, the link sound */
        player_camera(&pl, &cam);
        for (int k = 0; k < test_nviews; k++) if (SDL_GetTicks() >= (uint32_t)(test_views[k][0] * 1000)) memcpy(test_cam, test_views[k] + 1, sizeof test_cam);
        if (test_has_cam) { cam.x = test_cam[0]; cam.y = test_cam[1]; cam.z = test_cam[2]; cam.yaw = test_cam[3] * 0.0174532925f; cam.pitch = test_cam[4] * 0.0174532925f; }   /* test: fixed camera, player logic still runs */
        da_frame(lv.dream, &pl, &cam, dt);               /* DreamAudio: the listener is the camera */
        /* graph contributions: every 0.25 s note the chunk we stand on */
        if ((chunk_t += dt) > 0.25f) { chunk_t = 0; int ci = chunk_index(lv.dream, pl.x, pl.y, pl.z);
          if (ci >= 0 && ci != last_chunk) { if (ci < lv.dream->nlgraph) contrib_add(&area, lv.dream->lgraph[ci][0], lv.dream->lgraph[ci][1]); last_chunk = ci; } }
      }
      if (have_grey && !fly) {
        float mvx = pl.x - gm.lx, mvz = pl.z - gm.lz; gm.lx = pl.x; gm.lz = pl.z;
        float mv = sqrtf(mvx * mvx + mvz * mvz);
        if (!gm.spawned_visit && mv < 5.0f) { gm.walked += mv; if (gm.walked >= 20.0f) { gm.walked = 0; grey_roll(&gm, lv.dream, &pl, day, &rng); } }   /* a roll every 20 units walked */
      }
      if (gm.on) {
        gm.age += dt;
        if (gm.age > 135.0f) gm.on = 0;                                     /* LIFETIME_SECONDS */
        else if (!gm.hit) {
          gm.x += sinf(gm.yaw) * 0.3f * dt; gm.z += cosf(gm.yaw) * 0.3f * dt;     /* MoveSpeed */
          float dx = pl.x - gm.x, dy = (pl.y + 0.3f) - gm.y, dz = pl.z - gm.z;
          if (sqrtf(dx * dx + dy * dy + dz * dz) < 2.0f) { gm.hit = 1; gm.phase = 1; gm.ph_t = 0; contrib_add(&area, -10, -10); fb_greyman(); lsd_log("the grey man found you"); }
        }
      }
      if (gm.phase == 1) { gm.ph_t += dt; if (gm.ph_t >= 0.1f) { gm.on = 0; gm.phase = 2; gm.ph_t = 0; } }
      else if (gm.phase == 2) { gm.ph_t += dt; if (gm.ph_t >= 3.0f) gm.phase = 0; }
      /* the day's timer (it keeps running across wall links) */
      day_left -= dt;
      if (day_left <= 0 && !fade_dir) {
        if (fb_active()) {                                 /* a flashback's timer / EndDream: FadeIn(white, 3 s) -> title, day unchanged */
          go = GO_TITLE; fade_dir = 1; fade_rgb[0] = fade_rgb[1] = fade_rgb[2] = 1.0f; fade_rate = 1.0f / 3.0f; fade_blend = BLEND_ALPHA; audio_music_fade(3.0f);
          lsd_log("flashback over (%s)", s_lua_end ? "EndDream" : "timer");
        } else {
        graph_pos(&area, &graph_x, &graph_y); zone_color(graph_x, graph_y, fade_rgb);
        int gd = graph_dream(&dl, graph_x, graph_y, &rng);
        pending = gd >= 0 ? gd : next_dream(&dl, cur, &rng); go = GO_DAYEND; fade_dir = 1; audio_music_fade(1.0f);
        if (s_lua_end) { fade_rate = 1.0f / 3.0f; fade_blend = BLEND_ALPHA; }   /* endDream(timedOut: false): zone colour, 3 s */
        else { float secs; sp_random_ending(&rng, fade_rgb, &secs, &fade_blend); fade_rate = secs > 0 ? 1.0f / secs : 1000.0f; }   /* timed out */
        lsd_log("day %d over: graph (%d,%d) from %d area + %d entity contributions -> %s", day, graph_x, graph_y, area.n, entc.n, dl.d[pending].name);
        }
        s_lua_end = 0;
      }
    }

    /* ---- fade / dream change ---- */
    if (fade_dir == 1) { fade += dt * fade_rate; if (fade >= 1.0f) {
        fade = 1.0f; fade_rate = 2.5f;
        int target = pending >= 0 ? pending : cur; pending = -1; int what = go; go = GO_NONE;
        int nday = what == GO_DAYEND ? day % 365 + 1 : day;
        int sp_go = what == GO_DAYEND && day == 365 ? 1                     /* day 365 ended: the ENDING movie */
                  : (what == GO_DAYEND || (what == GO_START && sp_ok_start)) && !fb_active() && sp_is_special_day(nday) ? 2 : 0;   /* a special day */
        sp_ok_start = 0;
        if (what == GO_TITLE) {                    /* give up the day (Revamped ForceEndDream: no progress, no day change) */
          da_free(); audio_music_stop(); level_free(&lv); have_level = 0; g_save.day = day; save_write(); fb_end();
          in_menu = 1; paused = 0; menu_open(&menu, 0); start_menu_music();
        } else if (what == GO_EVENT) {             /* the screen is in the clip's colour: the movie (the level stays loaded) */
          da_free(); sp_start_event();
        } else {
          /* draw a LOADING frame before the blocking load */
          if (!sp_go) { raster_clear(mf, 0xFF000000u); ui_text(mf, MENU_W / 2 - ui_text_w("LOADING", 2) / 2, MENU_H / 2 - 7, "LOADING", 0xFFB4B8DCu, 2);
            present(mtex, mf); }
          da_free(); audio_music_stop();               /* (2D voices go on: the link sound plays through the load) */
          if (what == GO_DAYEND) {
            day = day % 365 + 1;                   /* IncrementDayNumberWithSequence: after day 365 comes day 1 */
            if (day == 1) g_save.year++;           /* ... and a new year */
            fb_day_ended();                        /* flashbackDisabledForDays-- */
            g_save.day = day; g_save.graph_x = graph_x; g_save.graph_y = graph_y; g_save.has_game = 1; save_add_history(graph_x, graph_y);
            for (int k = 0; k < s_ndayec; k++) { const DayEC *c = &s_dayec[k]; save_add_ec(c->d, c->u, c->x, c->y, c->z, c->yaw, c->dream, c->id); }
          }
          if (what == GO_DAYEND || what == GO_START) { memset(&area, 0, sizeof area); memset(&entc, 0, sizeof entc); s_ndayec = 0; last_chunk = -1; song_style = (int)(rng_next(&rng) % 7); }
          if (sp_go == 1 && !sp_start_ending()) sp_go = sp_is_special_day(day) ? 2 : 0;   /* no ENDING movie: carry on */
          if (sp_go == 2 && !sp_start_special_day(day, &rng)) sp_go = 0;
          if (sp_go) {                             /* the movie / special day takes over (special.c); no dream now */
            if (have_level) { level_free(&lv); have_level = 0; }
            in_menu = 0; paused = 0; fade = 0; fade_dir = 0; fade_blend = BLEND_ALPHA;
            if (what == GO_DAYEND) save_write();
            last = SDL_GetTicks();
            continue;
          }
          cur = target; in_menu = 0;
          have_level = level_load(&lv, &dl, cur, fb_active() ? fb_env_day() : day, &rng) == 0;   /* flashback: random environment */
          if (have_level) {
            view_new_dream();                      /* AUTO starts low in every dream and climbs */
            s_day = day;                        /* scripts may ask for the day in start() (mesh dreams start at load) */
            level_spawn(&lv, &pl, day, &rng); script_level((Dream *)lv.dream, &dl, &lv.world); start_music(&lv, song_style, &rng, &dl, day);
            if (fb_active()) fb_after_load(&pl, (Dream *)lv.dream);   /* the recorded spot and facing, the event's entity on */
            level_update_live(&lv); da_level(lv.dream); s_cur_dream = cur;
            if (what == GO_DAYEND || what == GO_START) day_left = fb_active() ? (test_daylen > 0 ? test_daylen : fb_total_seconds()) : day_length(lv.dream, &rng);
            if (have_grey) grey_new_dream(&gm, lv.dream, &pl, day, &rng);
            if (cur < SAVE_MAX_DREAMS && !fb_active()) g_save.visited[cur] = 1;
            if (!fb_active()) g_save.last_dream = cur;
            if (test_has_at && what == GO_START) { pl.x = test_at[0]; pl.z = test_at[1]; pl.y = 50; cam.x = pl.x; cam.z = pl.z; cam.y = 3.0f; cam.pitch = -0.35f; fly = 1; }
            if (test_has_tp && what == GO_START) { pl.x = test_tp[0]; pl.y = test_tp[1]; pl.z = test_tp[2]; }
            if (test_has_face && what == GO_START) pl.yaw = test_face * 0.0174532925f;
            if (gm.on && test_has_at) { float gg; pl.yaw = 0; gm.x = pl.x + 1.5f; gm.z = pl.z + 2.0f; if (cw_ground(&lv.world, gm.x, gm.z, 1e6f, &gg)) gm.y = gg + 0.02f; gm.hit = 1; gm.yaw = 2.5f; }   /* test: park him in view of the spinning camera */
            player_camera(&pl, &cam);
          } else { lsd_log("dream %d failed to load, back to the title", cur); fb_end(); in_menu = 1; menu_open(&menu, 0); start_menu_music(); }
          if (what == GO_DAYEND) save_write();   /* autosave at every day end */
        }
        fade_dir = -1; last = SDL_GetTicks();
      } }
    else if (fade_dir == -1) { fade -= dt * fade_in_rate; if (fade <= 0) { fade = 0; fade_dir = 0; fade_in_rate = 2.0f; fade_blend = BLEND_ALPHA; } }

    /* ---- render ---- */
    if (in_menu || paused) {
      MenuCtx mc = { &dl, paused, day, cur, 0, 0, 0, song_name[0] && paused ? song_name : NULL, paused ? pause_bg : NULL, pause_bg_w };
      if (paused) { mc.cur_has = area.n + entc.n > 0; graph_pos(&area, &mc.cur_gx, &mc.cur_gy); }
      menu_draw(&menu, uf, &mc, dt);
      if (fade > 0) fade_frame(uf, fade, fade_rgb);
      present(utex, uf);
      fps_n++;
      if (now - fps_t >= 1000) { fps = fps_n; fps_n = 0; fps_t = now; }
      continue;
    }

    st.tris_drawn = st.tiles_drawn = st.tris_tiny = 0;
    if (have_level) {
      /* the view distance (view.c: a fixed choice or AUTO) and the fog. CLASSIC: the environment's fog, the view never past
       * the fog end (DreamEnvironment.DRAW_DISTANCE 20 = RENDER DISTANCE CLASSIC). SCALED: fog start/end stretched by
       * view / min(20, fog end), so the view shows the same fog gradient as the classic 20 tiles, and the last quarter fades
       * into the horizon colour (no edge, no popping while AUTO moves). OFF: no fog. */
      for (int k = 0; k < test_nvd; k++) if (SDL_GetTicks() >= (uint32_t)(test_vd_t[k] * 1000)) view_test_set(test_vd[k]);
      if (test_fogmode >= 0) g_opt.fogmode = test_fogmode;
      float vd = view_frame(dt, last_work_us, lv.env.end), e0 = fminf(20.0f, lv.env.end);
      cam.sky_cut = fmaxf(fminf(e0, vd), 1.0f);       /* the sky quads keep the classic cutoff */
      if (g_opt.fogmode == FOG_CLASSIC) cam.draw_dist = fminf(vd, lv.env.end) + 1.0f;
      else cam.draw_dist = vd + 1.0f;
      level_update_live(&lv);
      if (!fade_dir) models_update(dt, lv.dream);   /* animation clocks of the object models */
      skyfx_update(&lv.fx, dt);                        /* sunburst flicker, drifting clouds */
      uint64_t p0 = SDL_GetPerformanceCounter();
      fr->water_time = SDL_GetTicks() * 0.001f;
      if (g_opt.fogmode == FOG_CLASSIC) env_apply_fog(fr, &lv.env, 1.0f, cam.draw_dist, 0);
      else if (g_opt.fogmode == FOG_SCALED) env_apply_fog(fr, &lv.env, vd / fmaxf(e0, 1.0f), vd, 1);
      else fr->fog_on = 0;
      level_render(fr, &cam, &lv, &st);
      if (gm.on && have_grey) render_mesh(fr, &cam, &grey_mesh, gm.x, gm.y, gm.z, gm.yaw, &st);                      /* transform + clip + project into the triangle lists */
      uint64_t p1 = SDL_GetPerformanceCounter();
      SkyJob sj = { &cam, &lv.env, g_opt.skyfx ? &lv.fx : NULL };
      raster_render(fr, sky_band, &sj);                      /* sky + triangles, in bands across the worker threads */
      if (fb_active()) fb_tint(fr);                          /* FlashbackImageEffect: +0.4 on the 3D frame */
      uint64_t p2 = SDL_GetPerformanceCounter();
      us_sky += (p1 - p0) * 1000000 / SDL_GetPerformanceFrequency(); us_lvl += (p2 - p1) * 1000000 / SDL_GetPerformanceFrequency(); nfr++;
      last_work_us = (unsigned)((p2 - p0) * 1000000 / SDL_GetPerformanceFrequency());
      { unsigned w = last_work_us; if (w > us_max) us_max = w; } st_tiny += st.tris_tiny; st_tris += st.tris_drawn;
    } else raster_clear(fr, 0xFF181028u);

    int W = fr->w, H = fr->h;
    if (gm.phase) {                                           /* white flash: 0.1 s in, 3 s out */
      int a = gm.phase == 1 ? (int)(gm.ph_t / 0.1f * 256) : (int)((1.0f - gm.ph_t / 3.0f) * 256); if (a > 256) a = 256; if (a < 0) a = 0;
      for (int i = 0; i < W * H; i++) { uint32_t c = fr->color[i]; int R = ((c >> 16 & 255) * (256 - a) + 255 * a) >> 8, G = ((c >> 8 & 255) * (256 - a) + 255 * a) >> 8, B = ((c & 255) * (256 - a) + 255 * a) >> 8; fr->color[i] = 0xFF000000u | (R << 16) | (G << 8) | B; }
    }
    if (flash_len > 0) {
      if (!paused) flash_t += dt;
      float k = 1.0f - flash_t / flash_len; if (k <= 0) flash_len = 0; else fade_frame(fr, k, flash_rgb);
    }
    if (fade > 0) sp_fade_apply(fr, fade, fade_rgb, fade_blend);   /* Alpha, Add or Subtract (ToriiFader) */
    char hud[80], a[12], b2[12];
    fmt1(a, sizeof a, pl.x); fmt1(b2, sizeof b2, pl.z);
    if (g_opt.show_fps) { int sc = H / 240; if (sc < 1) sc = 1;
      if (have_level) snprintf(hud, sizeof hud, "%d FPS  %s%d", fps, g_opt.view == VIEW_AUTO ? "AUTO " : "VIEW ", g_view_now); else snprintf(hud, sizeof hud, "%d FPS", fps);
      ui_text(fr, W - 4 * sc - ui_text_w(hud, sc), 4 * sc, hud, 0xFFFFFF60u, sc); }
    if (show_log || show_map) {
      snprintf(hud, sizeof hud, "FPS %d D%d V%d TRI %d %s DAY %d T%d", fps, cur, g_view_now, st.tris_drawn, fly ? "FLY" : pl.into_wall ? "WALL" : pl.grounded ? "WALK" : "FALL", day, (int)day_left);
      draw_text(fr, 2, 2, hud, 0xFFFFFF00u);
      snprintf(hud, sizeof hud, "X %s Z %s", a, b2);
      draw_text(fr, 2, 9, hud, 0xFF80FF80u);
    }
    if (show_map && have_level) {
      for (int zz = 0; zz < 20; zz++) { char row[64] = ""; for (int xx = 0; xx < 20; xx++) { float gg; char c[8];
          if (cw_ground(&lv.world, (float)xx, (float)zz, 1e6f, &gg)) { int v = (int)(gg * 10 + 20); snprintf(c, sizeof c, "%02d ", v < 0 ? 0 : v > 99 ? 99 : v); } else snprintf(c, sizeof c, "-- ");
          strcat(row, c); }
        draw_text(fr, 2, 20 + zz * 6, row, 0xFFC0FFFFu); }
    }
    if (show_log) for (int k = 0; k < 14; k++) { int li = ring_n - 14 + k; if (li >= 0) draw_text(fr, 2, H - 86 + k * 6, ring[li % 14], 0xFFFFFFFFu); }
    present(tex, fr);

    fps_n++;
    if (now - fps_t >= 1000) { fps = fps_n; fps_n = 0; fps_t = now;
      int mi, md, mt; models_stats(&mi, &md, &mt);
      lsd_log("fps=%d %dx%d project=%uus raster=%uus work_max=%uus view=%d fog=%d tris=%d tiny=%d tiles=%d models=%d/%d (%d tris) pos=%.2f,%.2f,%.2f wall=%d grounded=%d audio_cb=%u voices=%d/%d steps=%d yaw=%.0f pitch=%.0f behind=%.0f", fps, W, H, (unsigned)(us_sky / (nfr ? nfr : 1)), (unsigned)(us_lvl / (nfr ? nfr : 1)), us_max, g_view_now, g_opt.fogmode, st_tris / (nfr ? nfr : 1), st_tiny / (nfr ? nfr : 1), st.tiles_drawn, md, mi, mt, pl.x, pl.y, pl.z, pl.into_wall, pl.grounded, audio_callbacks(), da_voices(), da_sounding(), pl.step_count, pl.yaw * 57.29578f, cam.pitch * 57.29578f, pl.look_behind); us_sky = us_lvl = 0; nfr = 0; us_max = 0; st_tiny = st_tris = 0; }
  }
  if (!in_menu && have_level && g_save.has_game) g_save.day = day;   /* leaving mid-day keeps the day (no progress), plus options and visited dreams */
  save_write();
  audio_shutdown();
  SDL_Quit();
  return 0;
}

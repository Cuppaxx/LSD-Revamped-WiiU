/* special.c - see special.h. Sources (Revamped): DreamSystem.cs (BeginDream special-day branch, PlayVideo, PlayVideos,
 * endDream random endings, playEnding), Game/IntroVideoController.cs, DreamControl/{Video,Text}SpecialDayControl.cs,
 * UI/Graph/UIGraph.cs (showFace), Torii/UI/ToriiFader.cs + FadeBlend.shader. Data: specials.txt (tools/special_assets.py). */
#include "special.h"
#include "video.h"
#include "journal.h"
#include "save.h"
#include "audio.h"
#include "menu.h"
#include "level.h"
#include "lsd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- specials.txt */
typedef struct { char kind; char asset[8]; char movie[16]; char tim[40]; char page[40]; } SpChoice;
typedef struct { int day; char kind; int always, dyn, upper, n; char set[32]; SpChoice c[8]; } SpDay;
static SpDay s_days[64]; static int s_ndays;
static char s_ending[16] = "ENDING";
static char s_face[24][16]; static int s_nface;
static char s_intro[8][16]; static int s_nintro;
typedef struct { char folder[16], ent[48], movie[16]; } SpClip;
static SpClip s_clips[16]; static int s_nclips;
static void en_load(void);                    /* the English text days (below) */

void sp_init(void) {
  size_t n; uint8_t *b = lsd_read_file("specials.txt", &n);
  if (!b) { lsd_log("specials.txt missing: no special days, intro, ending or chart face movies"); return; }
  char *t = (char *)malloc(n + 1); memcpy(t, b, n); t[n] = 0; free(b);
  SpDay *cd = NULL;
  for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
    char *cr = strchr(line, '\r'); if (cr) *cr = 0;
    if (line[0] == '#' || !line[0]) continue;
    char kind[16]; int k = 0;
    if (!strncmp(line, "day ", 4) && s_ndays < 64) {
      cd = &s_days[s_ndays]; memset(cd, 0, sizeof *cd);
      if (sscanf(line + 4, "%d %31s %15s %d %d %d %d", &cd->day, cd->set, kind, &cd->always, &cd->dyn, &cd->upper, &k) == 7) { cd->kind = kind[0]; s_ndays++; }
      else cd = NULL;
    } else if (!strncmp(line, "choice ", 7) && cd && cd->n < 8) {
      SpChoice *c = &cd->c[cd->n]; int al, dy, up;
      if (sscanf(line + 7, "%15s %7s %d %d %d %15s %39s %39s", kind, c->asset, &al, &dy, &up, c->movie, c->tim, c->page) == 8) {
        c->kind = kind[0];
        if (!strcmp(c->movie, "-")) c->movie[0] = 0;
        if (!strcmp(c->tim, "-")) c->tim[0] = 0;
        if (!strcmp(c->page, "-")) c->page[0] = 0;
        cd->n++;
      }
    } else if (!strncmp(line, "ending ", 7)) sscanf(line + 7, "%15s", s_ending);
    else if (!strncmp(line, "chartface ", 10) || !strncmp(line, "intro ", 6)) {
      int face = line[0] == 'c'; char *p = line + (face ? 10 : 6); int cnt = 0, off = 0;
      if (sscanf(p, "%d%n", &cnt, &off) != 1) continue;
      p += off;
      for (int i = 0; i < cnt; i++) {
        char nm[16]; if (sscanf(p, " %15s%n", nm, &off) != 1) break; p += off;
        if (face && s_nface < 24) snprintf(s_face[s_nface++], 16, "%s", nm);
        if (!face && s_nintro < 8) snprintf(s_intro[s_nintro++], 16, "%s", nm);
      }
    } else if (!strncmp(line, "movieclip ", 10) && s_nclips < 16) {
      SpClip *c = &s_clips[s_nclips];
      if (sscanf(line + 10, "%15s %47s %15s", c->folder, c->ent, c->movie) == 3) s_nclips++;
    }
  }
  free(t);
  en_load();
  lsd_log("specials: %d special days, %d intro, %d chart face movies, %d movie clips, ending %s", s_ndays, s_nintro, s_nface, s_nclips, s_ending);
}

int g_sp_test_choice = -1;                    /* test hook (start.txt "spchoice N"): always take choice N of a special day */
static const SpDay *find_day(int day) { for (int i = 0; i < s_ndays; i++) if (s_days[i].day == day) return &s_days[i]; return NULL; }

/* ---------------------------------------------------------------- fader */
static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
void sp_fade_apply(Frame *f, float a, const float rgb[3], int blend) {
  if (a <= 0) return;
  if (a > 1) a = 1;
  int A = (int)(a * 256), n = f->w * f->h; uint32_t *p = f->color;
  int cr = (int)(rgb[0] * 255), cg = (int)(rgb[1] * 255), cb = (int)(rgb[2] * 255);
  if (blend == BLEND_ADD) {                     /* dst + c*a */
    int ar = cr * A >> 8, ag = cg * A >> 8, ab = cb * A >> 8;
    for (int i = 0; i < n; i++) { uint32_t c = p[i]; p[i] = 0xFF000000u | (uint32_t)clamp255((int)(c >> 16 & 255) + ar) << 16 | (uint32_t)clamp255((int)(c >> 8 & 255) + ag) << 8 | (uint32_t)clamp255((int)(c & 255) + ab); }
  } else if (blend == BLEND_SUB) {              /* dst - (1-c)*a: darkens towards c */
    int sr = (255 - cr) * A >> 8, sg = (255 - cg) * A >> 8, sb = (255 - cb) * A >> 8;
    for (int i = 0; i < n; i++) { uint32_t c = p[i]; p[i] = 0xFF000000u | (uint32_t)clamp255((int)(c >> 16 & 255) - sr) << 16 | (uint32_t)clamp255((int)(c >> 8 & 255) - sg) << 8 | (uint32_t)clamp255((int)(c & 255) - sb); }
  } else {                                      /* dst*(1-a) + c*a */
    int m = 256 - A, ar = cr * A, ag = cg * A, ab = cb * A;
    for (int i = 0; i < n; i++) { uint32_t c = p[i]; p[i] = 0xFF000000u | (uint32_t)(((int)(c >> 16 & 255) * m + ar) >> 8) << 16 | (uint32_t)(((int)(c >> 8 & 255) * m + ag) >> 8) << 8 | (uint32_t)(((int)(c & 255) * m + ab) >> 8); }
  }
}

void sp_random_ending(unsigned *rng, float rgb[3], float *secs, int *blend) {
  int k = (int)(rng_next(rng) % 4u);
  static const float black[3] = {0, 0, 0}, red[3] = {1, 0, 0}, white[3] = {1, 1, 1};
  switch (k) {
    case 0: memcpy(rgb, black, sizeof black); *secs = 0; *blend = BLEND_ALPHA; break;          /* SetFade(black): at once */
    case 1: memcpy(rgb, black, sizeof black); *secs = 1.25f; *blend = BLEND_SUB; break;
    case 2: memcpy(rgb, red, sizeof red); *secs = 1.25f; *blend = BLEND_ALPHA; break;
    default: memcpy(rgb, white, sizeof white); *secs = 2.5f; *blend = BLEND_ADD; break;
  }
  lsd_log("random ending %d (%s, %.2f s)", k, k == 0 ? "cut to black" : k == 1 ? "subtract black" : k == 2 ? "red" : "add white", *secs);
}

/* ---------------------------------------------------------------- sequences */
enum { ST_OFF, ST_INTRO, ST_SPVIDEO, ST_SPTEXT, ST_EVENT, ST_ENDING, ST_FACE_HILITE, ST_FACE_VIDEOS, ST_CREDITS, ST_JOURNAL };
enum { P_PRE, P_PLAY, P_POST };               /* fade out of black (movie not yet running), movie/text, fade into black */
static int st, ph; static float fa, ph_t;   /* fade alpha (black, Alpha blend) */
static float pre_secs, post_secs; static int result;
static char movie[16]; static int face_i;
static uint32_t *tim_px; static int tim_w, tim_h;
static int face_pts[4][2], face_lit; static float face_t;
static int ev_pending; static float ev_rgb[3]; static char ev_movie[16];
static const float BLACK[3] = {0, 0, 0};

int sp_active(void) { return st != ST_OFF; }
int sp_overlay(void) { return st == ST_FACE_HILITE; }

/* start a movie sequence: pre = black fade-out seconds before the movie (0.1 intro: the movie runs during it),
 * post = fade into black when it ends, res = what to return then */
static void begin_movie(int state, const char *name, float pre, float post, int res) {
  st = state; ph = P_PRE; fa = 1.0f; pre_secs = pre; post_secs = post; result = res; ph_t = 0;
  snprintf(movie, sizeof movie, "%s", name ? name : "");
  audio_music_stop();                          /* music is stopped while a movie plays */
  if (state == ST_INTRO && video_open(movie) != 0) { ph = P_POST; fa = 1.0f; }   /* intro: play during its 0.1 s fade */
}

/* ---------------------------------------------------------------- English text days (SPDAY/texts_en.txt)
 * text <id> / "| line" ... / end; the first line is the title, then a blank line, then the body. Body lines are joined into
 * paragraphs (blank lines separate them) and word-wrapped to the screen with the 5x7 menu font: title at 2x (1x if it does
 * not fit) in the faces' yellow, underlined; body at 1x in white; centred; pages if it is ever too long for one screen. */
typedef struct { char id[8]; char title[80]; char *body; } EnText;
static EnText s_en[64]; static int s_nen;
static void en_load(void) {
  size_t n; uint8_t *b = lsd_read_file("SPDAY/texts_en.txt", &n);
  if (!b) { lsd_log("SPDAY/texts_en.txt missing: text days show the original Japanese TIMs"); return; }
  char *t = (char *)malloc(n + 1); memcpy(t, b, n); t[n] = 0; free(b);
  EnText *cur = NULL; int nline = 0; size_t blen = 0, bcap = 0; int para_open = 0;
  for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
    char *cr = strchr(line, '\r'); if (cr) *cr = 0;
    if (!strncmp(line, "text ", 5) && s_nen < 64) { cur = &s_en[s_nen++]; memset(cur, 0, sizeof *cur); sscanf(line + 5, "%7s", cur->id); nline = 0; blen = 0; bcap = 0; para_open = 0; continue; }
    if (!cur) continue;
    if (!strcmp(line, "end")) { cur = NULL; continue; }
    if (line[0] != '|') continue;
    const char *s = line + 1; if (*s == ' ') s++;
    while (*s == ' ') s++;
    if (nline++ == 0) { snprintf(cur->title, sizeof cur->title, "%s", s); continue; }
    size_t L = strlen(s);
    if (blen + L + 3 > bcap) { bcap = (blen + L + 3) * 2 + 256; cur->body = (char *)realloc(cur->body, bcap); }
    if (!L) { if (para_open) { cur->body[blen++] = '\n'; para_open = 0; } }      /* blank line: new paragraph */
    else { if (para_open) cur->body[blen++] = ' '; memcpy(cur->body + blen, s, L); blen += L; para_open = 1; }
    cur->body[blen] = 0;
  }
  free(t);
  lsd_log("English text days: %d", s_nen);
}
static const EnText *en_find(const char *id) { for (int i = 0; i < s_nen; i++) if (!strcmp(s_en[i].id, id)) return &s_en[i]; return NULL; }

#define EN_MAXL 64
static const EnText *en_cur; static char en_lines[EN_MAXL][56]; static int en_nl, en_page, en_npages, en_per_page, en_title_sc, en_pitch;
static int en_layout(const char *id) {
  en_cur = en_find(id); en_nl = 0; en_page = 0;
  if (!en_cur) return 0;
  const int maxc = (MENU_W - 2 * 16) / 6;          /* 1x: 6 px per character, 16 px margins: 48 characters */
  const char *p = en_cur->body ? en_cur->body : "";
  while (*p && en_nl < EN_MAXL) {
    if (*p == '\n') { if (en_nl < EN_MAXL) en_lines[en_nl++][0] = 0; p++; continue; }   /* paragraph gap */
    int n = 0, last_sp = -1;
    while (p[n] && p[n] != '\n' && n < maxc) { if (p[n] == ' ') last_sp = n; n++; }
    int take = n, skip = 0;
    if (p[n] && p[n] != '\n' && p[n] != ' ' && last_sp > 0) { take = last_sp; skip = 1; }   /* break at the last space */
    else if (p[n] == ' ') skip = 1;
    snprintf(en_lines[en_nl++], sizeof en_lines[0], "%.*s", take, p);
    p += take + skip;
  }
  en_title_sc = ui_text_w(en_cur->title, 2) <= MENU_W - 16 ? 2 : 1;
  int top = 14 + 7 * en_title_sc + 12, avail = MENU_H - top - 18;   /* title block, then the body down to the footer */
  en_pitch = 11; while (en_pitch > 8 && en_nl * en_pitch > avail) en_pitch--;
  en_per_page = avail / en_pitch; if (en_per_page < 1) en_per_page = 1;
  en_npages = (en_nl + en_per_page - 1) / en_per_page; if (en_npages < 1) en_npages = 1;
  return 1;
}
static void en_draw(Frame *f) {
  for (int k = 0; k < f->w * f->h; k++) f->color[k] = 0xFF000000u;
  if (!en_cur) return;
  int tw = ui_text_w(en_cur->title, en_title_sc), ty = 14;
  ui_text(f, f->w / 2 - tw / 2, ty, en_cur->title, 0xFFFFE04Au, en_title_sc);
  int uy = ty + 7 * en_title_sc + 3;               /* underline */
  for (int x = f->w / 2 - tw / 2; x < f->w / 2 + tw / 2 + 1; x++) if (x >= 0 && x < f->w) f->color[uy * f->w + x] = 0xFFB0902Cu;
  int top = 14 + 7 * en_title_sc + 12, avail = MENU_H - top - 18;
  int first = en_page * en_per_page, cnt = en_nl - first; if (cnt > en_per_page) cnt = en_per_page;
  int y = top + (en_npages == 1 ? (avail - cnt * en_pitch) / 2 : 0);   /* one page: centred vertically */
  for (int i = 0; i < cnt; i++, y += en_pitch) { const char *s = en_lines[first + i]; if (*s) ui_text(f, f->w / 2 - ui_text_w(s, 1) / 2, y, s, 0xFFFFFFFFu, 1); }
  if (en_npages > 1) { char b[24]; snprintf(b, sizeof b, "%d/%d", en_page + 1, en_npages); ui_text(f, f->w - 8 - ui_text_w(b, 1), f->h - 11, b, 0xFF6A6488u, 1); }
}

int sp_start_intro(unsigned *rng) {
  if (!g_opt.intro || !s_nintro) return 0;
  int have[8], nh = 0;                         /* only the OPENING movies present on the SD card (degrade: none, no intro) */
  for (int i = 0; i < s_nintro; i++) if (video_exists(s_intro[i])) have[nh++] = i;
  if (!nh) { lsd_log("intro: no OPENING movie on the SD card"); return 0; }
  begin_movie(ST_INTRO, s_intro[have[rng_next(rng) % (unsigned)nh]], 0.1f, 1.0f, SPR_TITLE);
  if (ph == P_POST) { st = ST_OFF; return 0; }   /* unreadable file */
  return 1;
}

int sp_is_special_day(int day) {
  const SpDay *d = find_day(day);
  return d && d->n > 0 && (g_opt.specialdays || d->always);
}

static int choice_ok(const SpChoice *c) {
  if (c->kind == 'v') return c->movie[0] && video_exists(c->movie);
  if (en_find(c->asset)) return 1;                /* the English text (SPDAY/texts_en.txt) */
  size_t n; uint8_t *b = c->tim[0] ? lsd_read_file(c->tim, &n) : NULL;
  if (!b && c->page[0]) b = lsd_read_file(c->page, &n);
  if (b) { free(b); return 1; }
  return 0;
}

int sp_start_special_day(int day, unsigned *rng) {
  const SpDay *d = find_day(day);
  if (!d || !d->n) return 0;
  /* RandomSpecialDay: one choice uniformly (duplicates count twice); a choice whose files are missing is replaced by a
   * random one of those available (the movies live on the SD card and may not be there) */
  const SpChoice *c = &d->c[g_sp_test_choice >= 0 && g_sp_test_choice < d->n ? g_sp_test_choice : (int)(rng_next(rng) % (unsigned)d->n)];
  if (!choice_ok(c)) {
    int ok[8], nok = 0; for (int i = 0; i < d->n; i++) if (choice_ok(&d->c[i])) ok[nok++] = i;
    lsd_log("special day %d: %s %s missing, %d of %d choices available", day, c->kind == 'v' ? "movie" : "text", c->kind == 'v' ? c->movie : c->tim, nok, d->n);
    c = nok ? &d->c[ok[rng_next(rng) % (unsigned)nok]] : NULL;
  }
  if (!c) {                                    /* nothing to show: the day passes on a black screen */
    st = ST_SPTEXT; ph = P_POST; fa = 1.0f; post_secs = 1.0f; result = SPR_DAY_DONE; ph_t = 0; free(tim_px); tim_px = NULL; en_cur = NULL;
    return 1;
  }
  save_add_sp(g_save.year, day, c->kind == 'v' ? 'v' : 't', c->asset);
  if (c->kind == 'v') { lsd_log("special day %d (%s): video %s", day, d->set, c->movie); begin_movie(ST_SPVIDEO, c->movie, 0.5f, 1.0f, SPR_DAY_DONE); return 1; }
  free(tim_px); tim_px = NULL;
  audio_music_stop();
  st = ST_SPTEXT; ph = P_PRE; fa = 1.0f; pre_secs = 1.0f; post_secs = 1.0f; result = SPR_DAY_DONE; ph_t = 0;
  if (en_layout(c->asset)) {                   /* the English text, drawn with the menu font (the TIMs are Japanese) */
    lsd_log("special day %d (%s): text %s (English, %d lines, %d page%s)", day, d->set, c->asset, en_nl, en_npages, en_npages > 1 ? "s" : "");
    return 1;
  }
  lsd_log("special day %d (%s): text %s (no English text: original TIM %s)", day, d->set, c->asset, c->tim);
  size_t n; uint8_t *b = c->tim[0] ? lsd_read_file(c->tim, &n) : NULL;
  if (!b && c->page[0]) b = lsd_read_file(c->page, &n);
  if (b) { tim_px = lsd_decode_tim(b, n, &tim_w, &tim_h); free(b); }
  return 1;
}

int sp_start_ending(void) {
  if (!video_exists(s_ending)) { lsd_log("ending: MOVIE/%s.mjv missing", s_ending); return 0; }
  begin_movie(ST_ENDING, s_ending, 0.5f, 1.0f, SPR_TITLE);
  return 1;
}

void sp_request_event(const char *folder, const char *entity, float r, float g, float b) {
  for (int i = 0; i < s_nclips; i++)
    if (!strcmp(s_clips[i].folder, folder) && !strcmp(s_clips[i].ent, entity)) {
      if (ev_pending || st != ST_OFF) return;  /* FadeIn(forced: false): dropped while a fade/movie runs */
      ev_pending = 1; ev_rgb[0] = r; ev_rgb[1] = g; ev_rgb[2] = b; snprintf(ev_movie, sizeof ev_movie, "%s", s_clips[i].movie);
      lsd_log("VideoClip.Play: %s/%s -> %s", folder, entity, ev_movie);
      return;
    }
  lsd_log("VideoClip.Play: no movie for %s/%s", folder, entity);
}
int sp_event_pending(float rgb[3]) { if (!ev_pending) return 0; ev_pending = 0; memcpy(rgb, ev_rgb, sizeof ev_rgb); return 1; }
int sp_start_event(void) {
  begin_movie(ST_EVENT, ev_movie, 0.5f, 1.0f, SPR_EVENT_DONE);
  return 1;
}

int sp_chart_opened(void) {
  static const int pts[4][2] = { {-1, 1}, {1, 1}, {0, 0}, {0, -3} };
  if (g_save.seenface || !s_nface) return 0;
  int first[4];
  for (int k = 0; k < 4; k++) { first[k] = -1;
    for (int i = 0; i < g_save.nhist; i++) if (g_save.hist[i][0] == pts[k][0] && g_save.hist[i][1] == pts[k][1]) { first[k] = i; break; }
    if (first[k] < 0) return 0; }
  int any = 0; for (int i = 0; i < s_nface && !any; i++) any = video_exists(s_face[i]);
  if (!any) { lsd_log("chart face complete, but no chart face movie on the SD card: not shown yet"); return 0; }
  /* highlight order: the order in which those positions first appear in the history */
  int ord[4] = {0, 1, 2, 3};
  for (int a = 0; a < 4; a++) for (int b = a + 1; b < 4; b++) if (first[ord[b]] < first[ord[a]]) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
  for (int k = 0; k < 4; k++) { face_pts[k][0] = pts[ord[k]][0]; face_pts[k][1] = pts[ord[k]][1]; }
  st = ST_FACE_HILITE; face_t = 0; face_lit = 0; fa = 0; ph = P_PLAY;
  lsd_log("chart face complete: showing it");
  return 1;
}

void sp_start_credits(void) { credits_open(); st = ST_CREDITS; fa = 1.0f; ph = P_PRE; pre_secs = 0.5f; }
void sp_start_journal(const DreamList *dl, int in_game, int day, int cur_dream) { journal_open(dl, in_game, day, cur_dream); st = ST_JOURNAL; fa = 0; }

static int finish(void) { int r = result; st = ST_OFF; video_close(); free(tim_px); tim_px = NULL; return r; }

static void draw_tim(Frame *f) {
  for (int k = 0; k < f->w * f->h; k++) f->color[k] = 0xFF000000u;
  if (!tim_px) return;
  int dw = tim_w, dh = tim_h;                   /* the PS1 page is 320x240: 1:1 in the 320x240 frame */
  int ox = (f->w - dw) / 2, oy = (f->h - dh) / 2;
  for (int y = 0; y < dh; y++) { if (oy + y < 0 || oy + y >= f->h) continue;
    for (int x = 0; x < dw; x++) { if (ox + x < 0 || ox + x >= f->w) continue; f->color[(oy + y) * f->w + ox + x] = tim_px[y * tim_w + x] | 0xFF000000u; } }
}

/* movie states: P_PRE fade from black, P_PLAY, P_POST fade to black (picture frozen, sound off) */
static int movie_frame(Frame *f, float dt, const SpInput *in) {
  if (ph == P_PRE) {
    if (st == ST_INTRO) video_update();         /* the intro plays under its 0.1 s fade */
    fa -= dt / pre_secs;
    if (fa <= 0) { fa = 0; ph = P_PLAY;
      if (st != ST_INTRO && video_open(movie) != 0) { ph = P_POST; fa = 0; } }   /* missing movie: straight to the end */
  } else if (ph == P_PLAY) {
    int running = video_update();
    if (!running || in->skip) {                 /* natural end or A/+ (only while playing, once) */
      lsd_log("movie %s %s at %.1f s", movie, running ? "skipped" : "ended", video_time());
      video_stop_audio(); ph = P_POST;
      if (st == ST_FACE_VIDEOS) post_secs = 0.5f;
    }
  } else {
    fa += dt / post_secs;
    if (fa >= 1) { fa = 1;
      if (st == ST_FACE_VIDEOS) {               /* next of the 20, or back to the title */
        video_close();
        while (++face_i < s_nface && !video_exists(s_face[face_i])) {}
        if (face_i < s_nface) { snprintf(movie, sizeof movie, "%s", s_face[face_i]); ph = P_PRE; pre_secs = 0.5f; }
        else { result = SPR_TITLE; video_draw(f); return finish(); }
      } else { for (int k = 0; k < f->w * f->h; k++) f->color[k] = 0xFF000000u; return finish(); }
    }
  }
  if (video_is_open()) video_draw(f); else for (int k = 0; k < f->w * f->h; k++) f->color[k] = 0xFF000000u;
  sp_fade_apply(f, fa, BLACK, BLEND_ALPHA);
  return SPR_NONE;
}

int sp_menu_frame(void) { return st == ST_JOURNAL || st == ST_CREDITS; }

int sp_frame(Frame *f, float dt, const SpInput *in) {
  switch (st) {
    case ST_OFF: return SPR_NONE;
    case ST_INTRO: case ST_SPVIDEO: case ST_EVENT: case ST_ENDING: case ST_FACE_VIDEOS:
      return movie_frame(f, dt, in);
    case ST_SPTEXT:                             /* no timeout: A/+ from the very first frame (even while fading in) */
      if (ph != P_POST && in->skip) {         /* English text with pages: A turns the page, the last one closes */
        if (en_cur && en_page + 1 < en_npages) en_page++; else { ph = P_POST; lsd_log("text day closed"); } }
      if (ph == P_PRE) { fa -= dt / pre_secs; if (fa <= 0) { fa = 0; ph = P_PLAY; } }
      else if (ph == P_POST) { fa += dt / post_secs; if (fa >= 1) { for (int k = 0; k < f->w * f->h; k++) f->color[k] = 0xFF000000u; en_cur = NULL; return finish(); } }
      if (en_cur) en_draw(f); else draw_tim(f); sp_fade_apply(f, fa, BLACK, BLEND_ALPHA);
      return SPR_NONE;
    case ST_FACE_HILITE: {                      /* drawn over the chart (main drew it): 1.5 s, then a square every 1.2 s */
      face_t += dt;
      int lit = face_t < 1.5f ? 0 : 1 + (int)((face_t - 1.5f) / 1.2f); if (lit > 4) lit = 4;
      for (int k = 0; k < lit; k++) {
        int x = 65 + (face_pts[k][0] + 9) * 10, y = 16 + (9 - face_pts[k][1]) * 10;
        for (int yy = y + 1; yy < y + 9; yy++) for (int xx = x + 1; xx < x + 9; xx++) if (xx >= 0 && yy >= 0 && xx < f->w && yy < f->h) f->color[yy * f->w + xx] = 0xFF00FF00u;
      }
      if (face_t >= 1.5f + 4 * 1.2f) {          /* seenChartFace = true, save, PlayVideos: stop music, FadeIn(black, 0.5 s) */
        if (!g_save.seenface) { g_save.seenface = 1; save_write(); audio_music_stop(); }
        fa += dt / 0.5f;
        if (fa >= 1) { fa = 1; face_i = 0; while (face_i < s_nface && !video_exists(s_face[face_i])) face_i++;
          st = ST_FACE_VIDEOS; result = SPR_TITLE;
          if (face_i >= s_nface) return finish();
          snprintf(movie, sizeof movie, "%s", s_face[face_i]); ph = P_PRE; pre_secs = 0.5f; post_secs = 0.5f; }
      }
      sp_fade_apply(f, fa, BLACK, BLEND_ALPHA);
      return SPR_NONE; }
    case ST_CREDITS: {
      int done = credits_frame(f, dt, in);
      if (ph == P_PRE) { fa -= dt / pre_secs; if (fa <= 0) { fa = 0; ph = P_PLAY; } }
      sp_fade_apply(f, fa, BLACK, BLEND_ALPHA);
      if (done) { st = ST_OFF; return SPR_BACK; }
      return SPR_NONE; }
    case ST_JOURNAL:
      if (journal_frame(f, dt, in)) { st = ST_OFF; return SPR_BACK; }
      return SPR_NONE;
  }
  return SPR_NONE;
}

/* journal.c - see journal.h. Drawing uses menu.c's exported pieces: ui_text/ui_text_w/ui_rect (5x7 font), title_px
 * (ETC/TITLE.TIM: the small yellow face icon at 267,157), menu_hash (stars). Revamped references: UI/Title/
 * UICurrentDayDisplay.cs (Day NNN, colour by day % 41), UIYearsDisplay.cs (an icon per year, "xN" past 10),
 * UICurrentJournalDisplay.cs (journal name), UI/Graph/UIGraph.cs (chart squares), UI/Credits/ (credits roll). */
#include "journal.h"
#include "save.h"
#include "menu.h"
#include "audio.h"
#include "flashback.h"
#include "lsd.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t *title_px; extern int title_w, title_h;   /* menu.c */
uint32_t menu_hash(uint32_t x);                           /* menu.c */

#define C_SEL   0xFFFFE04Au
#define C_ITEM  0xFFB4B8DCu
#define C_DIM   0xFF6A6488u
#define C_HEAD  0xFFFFFFFFu

static void px(Frame *f, int x, int y, uint32_t c) { if ((unsigned)x < (unsigned)f->w && (unsigned)y < (unsigned)f->h) f->color[y * f->w + x] = c; }
static void box(Frame *f, int x, int y, int w, int h, uint32_t c) { for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) px(f, xx, yy, c); }
static void text_c(Frame *f, int cx, int y, const char *s, uint32_t c, int sc) { ui_text(f, cx - ui_text_w(s, sc) / 2, y, s, c, sc); }
static void face(Frame *f, int x, int y) {               /* the yellow face from TITLE.TIM, keyed on its navy */
  if (!title_px || title_w < 277 || title_h < 167) { box(f, x + 2, y + 2, 6, 6, C_SEL); return; }
  for (int j = 0; j < 10; j++) for (int i = 0; i < 10; i++) {
    uint32_t s = title_px[(157 + j) * title_w + 267 + i]; int r = s >> 16 & 255, g = s >> 8 & 255;
    if (r + g > 120) px(f, x + i, y + j, 0xFF000000u | s);
  }
}
static void backdrop(Frame *f, float t) { menu_backdrop(f, t); }   /* the menu's navy starfield (menu.c) */
static void header(Frame *f, const char *title, float t) { menu_header(f, title, t); }
static void zone_of(int x, int y, const char **name, uint32_t *col) {   /* main.c zone_name / zone_color */
  const char *n = "WHITE"; uint32_t c = 0xFFFFFFFFu;
  if (x <= -4 && y >= 4) { n = "GREEN"; c = 0xFF00FF00u; } else if (x <= 3 && y >= 4) { n = "BLUE"; c = 0xFF4060FFu; }
  else if (x >= 4 && y >= 4) { n = "MAGENTA"; c = 0xFFFF00FFu; } else if (x >= 4 && y >= -3) { n = "BLACK"; c = 0xFF505050u; }
  else if (x >= 4 && y <= -4) { n = "CYAN"; c = 0xFF00FFFFu; } else if (x <= -4 && y <= -4) { n = "YELLOW"; c = 0xFFFFEB04u; }
  else if (x <= 3 && y <= -4) { n = "RED"; c = 0xFFFF2020u; }
  *name = n; *col = c;
}

/* ================================================================ journal */
enum { PG_OVERVIEW, PG_CHART, PG_DREAMS, PG_SPECIAL, PG_N };
static const char *pg_names[PG_N] = { "JOURNAL", "DREAM CHART", "DREAMS", "SPECIAL DAYS" };
static const DreamList *j_dl; static int j_in_game, j_day, j_cur, j_page, j_scroll; static float j_t;

void journal_open(const DreamList *dl, int in_game, int day, int cur_dream) {
  j_dl = dl; j_in_game = in_game; j_day = day; j_cur = cur_dream; j_page = 0; j_scroll = 0; j_t = 0;
  audio_sfx(SFX_PRESS);
}

static uint32_t day_colour(int day) {                     /* UICurrentDayDisplay: by day % 41 */
  int m = day % 41;
  return m <= 10 ? 0xFFC78DA2u : m <= 20 ? 0xFF887FC5u : m <= 30 ? 0xFF6ECC84u : 0xFFFFFFFFu;
}

static int ec_for_dream(int d) { int n = 0; for (int i = 0; i < g_save.nec; i++) n += g_save.ec[i].dream == d; return n; }

static void page_overview(Frame *f) {
  char b[96]; int day = j_in_game ? j_day : g_save.day;
  snprintf(b, sizeof b, "DAY %03d", day); text_c(f, f->w / 2, 38, b, day_colour(day), 3);
  /* years: one face per completed year, "xN" past 10 (UIYearsDisplay) */
  int y = g_save.year;
  if (y > 10) { int w = 10 + 4 + ui_text_w("X999", 1); face(f, f->w / 2 - w / 2, 62); snprintf(b, sizeof b, "X%d", y); ui_text(f, f->w / 2 - w / 2 + 14, 63, b, C_SEL, 1); }
  else for (int i = 0; i < y; i++) face(f, f->w / 2 - y * 6 + i * 12, 62);
  if (!y) text_c(f, f->w / 2, 63, "FIRST YEAR", C_DIM, 1);
  snprintf(b, sizeof b, "%s", g_opt.addl && j_dl && j_dl->n > j_dl->norig ? "ORIGINAL + ADDITIONAL DREAMS" : "ORIGINAL DREAMS");
  text_c(f, f->w / 2, 76, b, C_DIM, 1);
  int seen = 0, total = 0;
  for (int i = 0; j_dl && i < j_dl->n; i++) if (dream_enabled(j_dl, i)) { total++; seen += i < SAVE_MAX_DREAMS && g_save.visited[i]; }
  int nv = 0, nt = 0; for (int i = 0; i < g_save.nsp; i++) { nv += g_save.sp[i].kind == 'v'; nt += g_save.sp[i].kind != 'v'; }
  const char *zn = "-"; uint32_t zc = C_ITEM; if (g_save.nhist) zone_of(g_save.graph_x, g_save.graph_y, &zn, &zc);
  struct { const char *k; char v[48]; uint32_t c; } rows[8]; int n = 0;
#define ROW(K, C, ...) do { rows[n].k = (K); rows[n].c = (C); snprintf(rows[n].v, sizeof rows[n].v, __VA_ARGS__); n++; } while (0)
  ROW("DAYS DREAMT", C_ITEM, "%d", g_save.year * 365 + g_save.day - 1);
  ROW("YEARS COMPLETED", C_ITEM, "%d", g_save.year);
  ROW("DREAMS VISITED", C_ITEM, "%d / %d", seen, total);
  ROW("SPECIAL DAYS SEEN", C_ITEM, "%d  (%d FILM, %d TEXT)", g_save.nsp, nv, nt);
  if (g_save.nhist) ROW("LAST DAY ON THE CHART", zc, "%+d,%+d %s", g_save.graph_x, g_save.graph_y, zn);
  else ROW("LAST DAY ON THE CHART", zc, "-");
  ROW("MEMORIES (OBJECTS)", C_ITEM, "%d", g_save.nec);
  if (fb_disabled_days() > 0) ROW("FLASHBACK", 0xFFA0A0A0u, "GONE FOR %d DAYS", fb_disabled_days());
  else if (g_save.nec > 100) ROW("FLASHBACK", C_SEL, "AVAILABLE");
  else ROW("FLASHBACK", C_DIM, "NEEDS %d MORE", 101 - g_save.nec);
  ROW("CHART FACE", g_save.seenface ? C_SEL : C_DIM, "%s", g_save.seenface ? "SEEN" : "NOT YET");
#undef ROW
  if (n > 8) n = 8;
  for (int i = 0; i < n; i++) {
    int yy = 96 + i * 15;
    ui_rect(f, 28, yy - 4, f->w - 56, 14, i & 1 ? 0xFF201848u : 0xFF2C2260u, 120);
    ui_text(f, 36, yy, rows[i].k, C_DIM, 1);
    ui_text(f, f->w - 36 - ui_text_w(rows[i].v, 1), yy, rows[i].v, rows[i].c, 1);
  }
}

static void page_chart(Frame *f) {
  /* 19x19 cells of 9 px from (x0,y0); zones faintly tinted; days as UIGraph squares (a shade darker every 10 days, the most
   * recent flashing red), today's heading (pause) as a white frame */
  const int cs = 9, x0 = f->w / 2 - 19 * cs / 2, y0 = 36;
  for (int gy = 0; gy < 19; gy++) for (int gx = 0; gx < 19; gx++) {
    const char *zn; uint32_t zc; zone_of(gx - 9, 9 - gy, &zn, &zc);
    box(f, x0 + gx * cs, y0 + gy * cs, cs - 1, cs - 1, 0xFF18103Cu);
    ui_rect(f, x0 + gx * cs, y0 + gy * cs, cs - 1, cs - 1, zc, 40);
  }
  for (int k = 0; k < 19 * cs; k++) { px(f, x0 + 9 * cs + cs / 2, y0 + k, 0xFF6A6488u); px(f, x0 + k, y0 + 9 * cs + cs / 2, 0xFF6A6488u); }
  int n = g_save.nhist;
  for (int i = 0; i < n; i++) {
    int ago = n - 1 - i, mod = 5 - ago / 10; if (mod < 1) mod = 1;
    uint32_t v = (uint32_t)(51 * mod), col = 0xFF000000u | v << 16 | v << 8 | v;
    if (ago == 0 && fmodf(j_t, 0.25f) > 0.125f) col = 0xFFFF0000u;
    box(f, x0 + (g_save.hist[i][0] + 9) * cs + 1, y0 + (9 - g_save.hist[i][1]) * cs + 1, cs - 3, cs - 3, col);
  }
  ui_text(f, x0 - 4 - ui_text_w("STATIC", 1), y0 + 9 * cs + 1, "STATIC", C_DIM, 1);
  ui_text(f, x0 + 19 * cs + 4, y0 + 9 * cs + 1, "DYNAMIC", C_DIM, 1);
  text_c(f, f->w / 2, y0 - 9, "UPPER", C_DIM, 1); text_c(f, f->w / 2, y0 + 19 * cs + 2, "DOWNER", C_DIM, 1);
  char b[48]; snprintf(b, sizeof b, "%d DAY%s", n, n == 1 ? "" : "S"); ui_text(f, 8, f->h - 24, b, C_ITEM, 1);
}

static int list_rows(void) { return 13; }
static void page_dreams(Frame *f) {
  int n = j_dl ? j_dl->n : 0, rows = list_rows(), top = j_scroll;
  char b[64];
  for (int r = 0; r < rows && top + r < n; r++) {
    int i = top + r, y = 38 + r * 13, vis = i < SAVE_MAX_DREAMS && g_save.visited[i], en = dream_enabled(j_dl, i);
    if (j_in_game && i == j_cur) ui_rect(f, 26, y - 3, f->w - 52, 12, 0xFF6050A0u, 110);
    ui_text(f, 30, y, vis ? "*" : " ", 0xFFFFC040u, 1);
    snprintf(b, sizeof b, "%s", j_dl->d[i].name); ui_text(f, 40, y, b, !en ? 0xFF403860u : vis ? C_ITEM : C_DIM, 1);
    int m = ec_for_dream(i);
    if (m) { snprintf(b, sizeof b, "%d MEM", m); ui_text(f, f->w - 34 - ui_text_w(b, 1), y, b, C_DIM, 1); }
    if (i == j_dl->norig) for (int x = 30; x < f->w - 30; x++) if ((x / 2) & 1) px(f, x, y - 4, 0xFF4A4078u);
  }
  if (top > 0) text_c(f, f->w / 2, 31, "^", C_DIM, 1);
  if (top + rows < n) text_c(f, f->w / 2, 38 + rows * 13, "V", C_DIM, 1);
}

static void page_special(Frame *f) {
  int n = g_save.nsp, rows = list_rows(), top = j_scroll;
  if (!n) { text_c(f, f->w / 2, 100, "NO SPECIAL DAYS YET", C_DIM, 1); text_c(f, f->w / 2, 114, "SOME DAYS ARE NOT LIKE THE OTHERS", 0xFF4A4078u, 1); return; }
  char b[64];
  for (int r = 0; r < rows && top + r < n; r++) {
    const SaveSP *s = &g_save.sp[n - 1 - (top + r)];        /* newest first */
    int y = 38 + r * 13;
    ui_rect(f, 28, y - 3, f->w - 56, 12, r & 1 ? 0xFF201848u : 0xFF2C2260u, 110);
    snprintf(b, sizeof b, "YEAR %d  DAY %03d", s->year + 1, s->day); ui_text(f, 36, y, b, day_colour(s->day), 1);
    snprintf(b, sizeof b, "%s %s", s->kind == 'v' ? "FILM" : "TEXT", s->asset); ui_text(f, f->w - 36 - ui_text_w(b, 1), y, b, s->kind == 'v' ? C_SEL : C_ITEM, 1);
  }
  if (top > 0) text_c(f, f->w / 2, 31, "^", C_DIM, 1);
  if (top + rows < n) text_c(f, f->w / 2, 38 + rows * 13, "V", C_DIM, 1);
}

int journal_frame(Frame *f, float dt, const SpInput *in) {
  j_t += dt;
  if (in->back || in->start) { audio_sfx(SFX_PRESS); return 1; }
  if (in->left || in->right) { j_page = (j_page + (in->right ? 1 : PG_N - 1)) % PG_N; j_scroll = 0; audio_sfx(SFX_HOVER); }
  int total = j_page == PG_DREAMS ? (j_dl ? j_dl->n : 0) : j_page == PG_SPECIAL ? g_save.nsp : 0, maxs = total - list_rows();
  if (maxs < 0) maxs = 0;
  if (in->down && j_scroll < maxs) { j_scroll++; audio_sfx(SFX_HOVER); }
  if (in->up && j_scroll > 0) { j_scroll--; audio_sfx(SFX_HOVER); }
  backdrop(f, j_t);
  header(f, pg_names[j_page], j_t);
  switch (j_page) { case PG_OVERVIEW: page_overview(f); break; case PG_CHART: page_chart(f); break; case PG_DREAMS: page_dreams(f); break; default: page_special(f); }
  for (int k = 0; k < PG_N; k++) box(f, f->w / 2 - PG_N * 5 + k * 10, f->h - 22, 6, 3, k == j_page ? C_SEL : 0xFF4A4078u);   /* page dots */
  ui_text(f, 8, f->h - 11, "< > PAGE", C_DIM, 1);
  ui_text(f, f->w - 8 - ui_text_w("B BACK", 1), f->h - 11, "B BACK", C_DIM, 1);
  return 0;
}

/* ================================================================ credits (UICredits: 800x600 reference canvas -> 0.4) */
typedef struct { char type; char flags[4]; char text[64]; uint32_t *img; int iw, ih; } CLine;
static CLine *c_lines; static int c_n; static float c_y, c_t; static int c_height, c_loaded;

/* UTF-8 -> the font's ASCII: Latin-1 letters lose their accents, Cyrillic is transliterated, anything else becomes '?' */
static void translit(const char *s, char *out, int cap) {
  static const char lat1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  static const char *cyr[32] = { "a","b","v","g","d","e","zh","z","i","y","k","l","m","n","o","p","r","s","t","u","f","kh","ts","ch","sh","shch","","y","","e","yu","ya" };
  int n = 0; const unsigned char *p = (const unsigned char *)s;
  while (*p && n < cap - 4) {
    unsigned cp; int len;
    if (*p < 0x80) { cp = *p; len = 1; } else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = (*p & 0x1Fu) << 6 | (p[1] & 0x3Fu); len = 2; }
    else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { cp = (*p & 0x0Fu) << 12 | (p[1] & 0x3Fu) << 6 | (p[2] & 0x3Fu); len = 3; } else { cp = '?'; len = 1; }
    p += len;
    if (cp < 0x80) out[n++] = (char)cp;
    else if (cp >= 0xC0 && cp <= 0xFF) out[n++] = lat1[cp - 0xC0];
    else if (cp >= 0x410 && cp <= 0x44F) { const char *t = cyr[(cp - 0x410) & 31]; while (*t && n < cap - 1) out[n++] = *t++; }
    else out[n++] = '?';
  }
  out[n] = 0;
}

static void credits_load(void) {
  if (c_loaded) return;
  c_loaded = 1;
  size_t n; uint8_t *b = lsd_read_file("CREDITS/credits.txt", &n);
  if (!b) { lsd_log("credits: CREDITS/credits.txt missing"); return; }
  char *t = (char *)malloc(n + 1); memcpy(t, b, n); t[n] = 0; free(b);
  int cap = 512; c_lines = (CLine *)calloc((size_t)cap, sizeof(CLine));
  for (char *line = strtok(t, "\n"); line && c_n < cap; line = strtok(NULL, "\n")) {
    char *cr = strchr(line, '\r'); if (cr) *cr = 0;
    CLine *L = &c_lines[c_n];
    if (!strncmp(line, "section ", 8)) {
      char img[64]; int sort, off = 0;
      if (sscanf(line + 8, "%63s %d %n", img, &sort, &off) < 2) continue;
      L->type = 's'; translit(line + 8 + off, L->text, sizeof L->text);
      if (strcmp(img, "-")) { size_t in; uint8_t *ib = lsd_read_file(img, &in); if (ib) { L->img = lsd_decode_tim(ib, in, &L->iw, &L->ih); free(ib); } }
      c_n++;
    } else if (!strncmp(line, "name ", 5)) {
      char fl[4] = ""; int off = 0;
      if (sscanf(line + 5, "%3s%n", fl, &off) < 1) continue;
      const char *txt = line + 5 + off; if (*txt == ' ') txt++;
      L->type = 'n'; snprintf(L->flags, sizeof L->flags, "%s", fl); translit(txt, L->text, sizeof L->text);
      c_n++;
    }
  }
  free(t);
  lsd_log("credits: %d lines", c_n);
}

void credits_open(void) { credits_load(); c_y = 240; c_t = 0; c_height = 0; audio_sfx(SFX_PRESS); }

/* returns the column height; draws when f != NULL. Layout at 0.4 of the reference: title, then per section the image (36 px
 * wide, the TIMs are 72 px), the section title, the names, a gap; then the end cap */
static int credits_layout(Frame *f, int y) {
  int y0 = y;
#define VIS(hh) (f && y + (hh) > 0 && y < f->h)
  if (VIS(16)) text_c(f, f->w / 2, y, "LSD: REVAMPED", C_HEAD, 2);
  y += 20; if (VIS(8)) text_c(f, f->w / 2, y, "FOR WII U", C_DIM, 1); y += 24;
  for (int i = 0; i < c_n; i++) {
    CLine *L = &c_lines[i];
    if (L->type == 's') {
      y += 10;
      if (L->img && L->iw > 0) {
        int dw = 36, dh = L->ih * 36 / L->iw;
        if (VIS(dh)) for (int yy = 0; yy < dh; yy++) for (int xx = 0; xx < dw; xx++) {
          uint32_t c = L->img[(yy * L->ih / dh) * L->iw + xx * L->iw / dw]; if ((c & 0xFFFFFFu) == 0) continue;
          px(f, f->w / 2 - dw / 2 + xx, y + yy, c | 0xFF000000u); }
        y += dh + 4;
      }
      if (VIS(8)) text_c(f, f->w / 2, y, L->text, C_SEL, 1);
      y += 14;
    } else {
      if (L->text[0] && VIS(8)) {
        uint32_t c = strchr(L->flags, 'b') ? C_HEAD : strchr(L->flags, 'i') ? 0xFFC8B4F0u : C_ITEM;
        text_c(f, f->w / 2, y, L->text, c, 1);
        if (strchr(L->flags, 'b')) text_c(f, f->w / 2 + 1, y, L->text, c, 1);   /* bold: doubled */
      }
      y += 10;
    }
  }
  y += 30; if (VIS(8)) text_c(f, f->w / 2, y, "PORTED BY CUPPAX", C_SEL, 1);
  y += 12; if (VIS(8)) text_c(f, f->w / 2, y, "THANK YOU FOR DREAMING", C_DIM, 1);
  y += 12;
#undef VIS
  return y - y0;
}

int credits_frame(Frame *f, float dt, const SpInput *in) {
  c_t += dt;
  if (!c_height) c_height = credits_layout(NULL, 0);
  c_y -= 20.0f * dt;                                       /* 50 reference units per second = 20 px/s at 240 lines */
  for (int k = 0; k < f->w * f->h; k++) f->color[k] = 0xFF000000u;
  credits_layout(f, (int)c_y);
  if (in->back) { audio_sfx(SFX_PRESS); return 1; }       /* the Run button (B) ends it at once */
  return c_y < -(float)(c_height + 200);                   /* past its height + 500 reference units */
}
